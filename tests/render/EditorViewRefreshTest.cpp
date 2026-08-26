#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"

#include "EditorViewTestUtils.h"

#include <QApplication>
#include <QSettings>


#include "render/BlockLayout.h"

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
#undef RUN_TEST
  qInfo("All refresh tests passed.");
  return 0;
}
