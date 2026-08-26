#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"

#include "EditorViewTestUtils.h"

#include <QApplication>
#include <QImage>
#include <QScrollBar>
#include <QSettings>


#include "render/BlockLayout.h"
#include "document/TopLevelRangeChange.h"

using namespace muffin;

namespace {

// refreshVisibleBlocks must bypass the BuiltStamp coalescing check: its callers (mermaid async
// arrival, spell toggle, smart-punct rendering, codeBlockWrap) change state OUTSIDE the stamp's
// {selection, revision} key, so "already current" blocks must still rebuild.
void testRefreshVisibleBlocksRebuildsStampedBlock() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(600, 400);

  // A code line far wider than the viewport: with wrap on it occupies multiple visual rows,
  // with wrap off a single row (plus the fence gains a horizontal scrollbar).
  QString longLine;
  for (int i = 0; i < 60; ++i) {
    longLine += QStringLiteral("abcdefgh");
  }
  session.setMarkdownText(QStringLiteral("```cpp\n%1\n```\n\nafter").arg(longLine), false);
  view.setDocument(session.document());

  MarkdownNode* fence = blockAt(session, 0);
  const BlockLayout* layout = view.blockLayoutForNode(fence->id());
  require(layout != nullptr, "fence should have a layout");
  const qreal wrappedHeight = layout->rect().height();

  // Install the stamp the way a keystroke would (same selection + revision).
  require(view.refreshBlock(fence->id(), session.document()), "refreshBlock should succeed");

  {
    SettingsOverride wrapOff("markdown/codeBlockWrap", false);
    // Non-forced refresh must be a stamp no-op (height unchanged) — locks the coalescing contract.
    require(view.refreshBlocks(QVector<NodeId>{fence->id()}, session.document()), "stamped refresh ok");
    require(view.blockLayoutForNode(fence->id())->rect().height() == wrappedHeight,
            "stamped (non-forced) refresh must not rebuild");
    require(view.refreshVisibleBlocks(session.document()), "refreshVisibleBlocks should succeed");
    const BlockLayout* rebuilt = view.blockLayoutForNode(fence->id());
    require(rebuilt != nullptr, "fence layout should exist after refresh");
    require(rebuilt->rect().height() < wrappedHeight,
            "wrap-off rebuild must shrink the fence height; a stamp skip would leave the wrapped height");
  }

  // And back on: the same force path must restore the wrapped height.
  {
    SettingsOverride wrapOn("markdown/codeBlockWrap", true);
    require(view.refreshVisibleBlocks(session.document()), "second refresh should succeed");
    const BlockLayout* restored = view.blockLayoutForNode(fence->id());
    require(qAbs(restored->rect().height() - wrappedHeight) < 0.5,
            "wrap-on rebuild must restore the wrapped height");
  }
}

// Keystroke-path semantics are unchanged: refreshBlocks still honors the stamp (an identical
// {selection, revision} refresh is a no-op, not a rebuild).
void testRefreshBlocksStillHonorsStamp() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(600, 400);

  session.setMarkdownText(QStringLiteral("alpha\n\n```js\nlet x = 1;\n```\n\nbeta"), false);
  view.setDocument(session.document());

  MarkdownNode* fence = blockAt(session, 1);
  require(view.refreshBlock(fence->id(), session.document()), "first refresh should rebuild");
  // A stamped second refresh reports success without needing a rebuild — the observable contract
  // is just "true, no fallback to setDocument".
  require(view.refreshBlock(fence->id(), session.document()), "stamped refresh should still succeed");
  require(view.refreshBlocks(QVector<NodeId>{fence->id()}, session.document()),
          "stamped refreshBlocks should still succeed");
}

// Records the region Qt asks the viewport to repaint. QWidget::grab() full-renders, so dirty-rect
// bugs are invisible to pixel captures — the only honest observable is the paint event's region.
class PaintRegionSpy final : public QObject {
 public:
  using QObject::QObject;
  QRect boundingRect;
  bool sawPaint = false;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Paint) {
      sawPaint = true;
      boundingRect = boundingRect.united(static_cast<QPaintEvent*>(event)->region().boundingRect());
    }
    return QObject::eventFilter(watched, event);
  }
};

// refreshTopLevelRange must dirty the caret rects even when the rebuilt range is nowhere near the
// caret: without the union, a range refresh that relocates (or hides) the caret leaves its old
// pixels standing on the incremental repaint.
void testRefreshTopLevelRangeDirtiesCaretRects() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(600, 400);
  view.show();

  // Caret in the FIRST block; the range rebuild targets the LAST block, so the range's
  // {old, new, shifted} rects cannot cover the caret position.
  session.setMarkdownText(QStringLiteral("para one\n\npara two\n\npara three"), false);
  view.setDocument(session.document());
  setCursor(controller.selection(), blockAt(session, 0), 4);
  view.repaint();
  QApplication::processEvents();

  const QRect caretViewportRect =
      view.effectiveCursorRect().translated(0.0, -static_cast<qreal>(view.verticalScrollBar()->value())).toAlignedRect();

  PaintRegionSpy spy;
  view.viewport()->installEventFilter(&spy);
  // Rebuild the last top-level slot (index 2) in place — a minimal range dirty that excludes
  // the caret entirely, so only the caret union can bring the repaint region up to the caret.
  TopLevelRangeChange range;
  range.first = 2;
  range.oldCount = 1;
  range.newCount = 1;
  range.documentRevision = session.document().revision();
  require(view.refreshTopLevelRange(range, session.document()), "range refresh should succeed");
  QApplication::processEvents();

  require(spy.sawPaint, "a paint event should have been requested");
  require(spy.boundingRect.intersects(caretViewportRect.adjusted(-6, -6, 6, 6)),
          "refreshTopLevelRange must dirty the caret rect even when the range is elsewhere");
  view.viewport()->removeEventFilter(&spy);
}

}  // namespace

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("EditorViewRefreshTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testRefreshVisibleBlocksRebuildsStampedBlock);
  RUN_TEST(testRefreshBlocksStillHonorsStamp);
  RUN_TEST(testRefreshTopLevelRangeDirtiesCaretRects);
#undef RUN_TEST
  qInfo("All refresh tests passed.");
  return 0;
}
