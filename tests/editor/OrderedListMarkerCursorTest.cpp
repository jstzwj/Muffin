#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/SelectionController.h"
#include "theme/RenderTheme.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QFontMetricsF>

#include <iostream>

using namespace muffin;

namespace {
// Build a single-line document holding `marker` (e.g. "34."), place the caret at its end,
// type the space that turns it into an ordered-list item, and return the resulting caret hit.
HitTestResult typeSpaceToConvert(const QString& marker) {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(720, 460);
  session.setMarkdownText(marker, false);
  view.setDocument(session.document());

  setCursor(controller.selection(), blockAt(session, 0), marker.size());
  controller.inputController().insertText(QStringLiteral(" "));
  view.setDocument(session.document());

  view.setCursorPosition(controller.selection().cursorPosition());
  return view.cursorHit();
}
}  // namespace

// Regression: typing the space after an ordered-list marker ("34. ") converted the line
// into a list item, but the caret landed INSIDE the marker (between the digits and the ".")
// because the list-item content origin was a fixed indent that multi-digit markers overflow.
// The caret must sit to the right of the marker's full text width.
void testCaretLandsAfterMarker() {
  for (const QString& marker : {QStringLiteral("1."), QStringLiteral("34."), QStringLiteral("123."), QStringLiteral("1000.")}) {
    const HitTestResult hit = typeSpaceToConvert(marker);
    require(hit.cursorRect.x() > 0.0, "caret rect should be computed for an ordered list item");

    // The marker glyph width in the same font the renderer uses for markers.
    const QFontMetricsF metrics(RenderTheme::defaultTheme().paragraphFont());
    const qreal markerWidth = metrics.horizontalAdvance(marker);

    require(hit.cursorRect.x() > hit.blockRect.left() + markerWidth,
            "ordered-list caret must sit right of the marker, not inside it");
  }
}

// Regression companion: the caret must still resolve to the list item's content, and the
// resulting markdown must be the ordered list form.
void testConversionProducesList() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(720, 460);
  session.setMarkdownText(QStringLiteral("34."), false);
  view.setDocument(session.document());

  setCursor(controller.selection(), blockAt(session, 0), 3);
  controller.inputController().insertText(QStringLiteral(" "));

  require(session.markdownText() == QStringLiteral("34. "), "typing space should form '34. '");
  require(blockAt(session, 0)->type() == BlockType::List, "document root child should be a List");

  const CursorPosition c = controller.selection().cursorPosition();
  require(c.isValid(), "cursor should be valid after list conversion");
  require(c.text.sourceOffset == 4, "cursor should sit after the marker (source offset 4)");
  MarkdownNode* item = session.document().node(c.blockId);
  require(item != nullptr && item->type() == BlockType::ListItem, "cursor block should be the list item");
}

// The list-wide indent must keep sibling items column-aligned: every item's content starts
// at the same x even when their markers differ in width ("1." vs "10.").
void testSiblingContentIsColumnAligned() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(720, 460);
  session.setMarkdownText(QStringLiteral("1. one\n2. two\n10. ten"), false);
  view.setDocument(session.document());

  MarkdownNode* list = blockAt(session, 0);
  require(list != nullptr && list->type() == BlockType::List, "root should hold a list");
  require(list->children().size() >= 3, "list should have at least three items");

  qreal firstContentX = -1.0;
  for (const auto& item : list->children()) {
    setCursor(controller.selection(), item.get(), 0);
    view.setCursorPosition(controller.selection().cursorPosition());
    const qreal x = view.cursorHit().cursorRect.x();
    if (firstContentX < 0.0) {
      firstContentX = x;
    } else {
      require(qAbs(x - firstContentX) < 0.5, "all list items should share a content column");
    }
  }
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  testCaretLandsAfterMarker();
  testConversionProducesList();
  testSiblingContentIsColumnAligned();
  QApplication::clipboard()->clear();
  return 0;
}
