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

  require(session.markdownText().toString() == QStringLiteral("34. "), "typing space should form '34. '");
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

// End-to-end for the authored list split (Enter-Enter): the blank line between the halves is a
// real VEP block. The caret lands on it at normal line height — NOT a degenerate caret spanning
// the whole list — and typing there inserts at column 0, which interrupts the list in cmark and
// re-parses into TWO real lists around the new paragraph.
void testTypingOnSplitBlankLineSplitsList() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(720, 460);
  session.setMarkdownText(QStringLiteral("1. First\n2. Second\n\n\n1. Third"), false);
  view.setDocument(session.document());

  MarkdownNode* list = blockAt(session, 0);
  require(list != nullptr && list->type() == BlockType::List, "root should hold a list");
  MarkdownNode* vep = nullptr;
  for (const auto& child : list->children()) {
    const SourceRange range = child->sourceRange();
    if (child->type() == BlockType::Paragraph && range.byteEnd == range.byteStart) { vep = child.get(); }
  }
  require(vep != nullptr, "split blank line should be a VEP between the items");

  setCursor(controller.selection(), vep, 0);
  view.setCursorPosition(controller.selection().cursorPosition());
  const HitTestResult hit = view.cursorHit();
  const qreal lineHeight = QFontMetricsF(view.theme().paragraphFont()).height();
  require(hit.cursorRect.height() > 0.0 && hit.cursorRect.height() < lineHeight * 1.6,
          "caret on the split blank line must be a normal line-height caret");

  controller.inputController().insertText(QStringLiteral("hello"));
  view.setDocument(session.document());
  const auto& top = session.document().root().children();
  require(top.size() == 3, "typed text should split the loose list into two lists + a paragraph");
  require(top.at(0)->type() == BlockType::List, "first top-level block should stay a list");
  require(top.at(1)->type() == BlockType::Paragraph, "typed text should become a paragraph");
  require(top.at(2)->type() == BlockType::List, "tail should re-parse as a new list");
}

// The user-facing split flow: caret at the end of a middle item, Enter creates an empty item,
// Enter again exits it and splits the list in two. The post-edit caret must resolve ON ITS OWN
// to the blank line between the lists (the split VEP) — not vanish until the next mouse click.
void testEnterEnterCaretLandsOnSplitBlankLine() {
  for (const QString marker : {QStringLiteral("1. "), QStringLiteral("- ")}) {
    DocumentSession session;
    EditorController controller;
    EditorView view;
    controller.attach(&session, &view);
    view.resize(720, 460);
    const QString head = marker == QLatin1String("1. ")
                             ? QStringLiteral("1. First\n2. Second\n3. Third")
                             : QStringLiteral("- First\n- Second\n- Third");
    session.setMarkdownText(head, false);
    view.setDocument(session.document());

    // Caret at the end of the SECOND item's content.
    MarkdownNode* list = blockAt(session, 0);
    setCursor(controller.selection(), list->children().at(1).get(), QStringLiteral("Second").size());
    require(controller.inputController().insertParagraphBreak(), "first Enter should create the empty item");
    view.setDocument(session.document());
    require(controller.inputController().insertParagraphBreak(), "second Enter should exit and split the list");
    view.setDocument(session.document());

    // The split text: two blank lines, tail renumbered for ordered.
    const QString expected = marker == QLatin1String("1. ")
                                 ? QStringLiteral("1. First\n2. Second\n\n\n1. Third")
                                 : QStringLiteral("- First\n- Second\n\n\n- Third");
    const QString actual = session.markdownText().toString();
    require(actual == expected,
            QStringLiteral("split text mismatch: expected '%1', actual '%2'").arg(expected, actual));

    // Caret must be valid and resolve to the VEP on the split blank line.
    const CursorPosition caret = controller.selection().cursorPosition();
    require(caret.isValid(), "caret must remain valid after the split");
    MarkdownNode* caretNode = session.document().node(caret.blockId);
    require(caretNode != nullptr, "caret block must resolve to a node");
    const SourceRange caretRange = caretNode->sourceRange();
    require(caretNode->type() == BlockType::Paragraph && caretRange.byteEnd == caretRange.byteStart,
            "caret must land on the split blank line's VEP, not on a list item");
    view.setCursorPosition(caret);
    const HitTestResult hit = view.cursorHit();
    const qreal lineHeight = QFontMetricsF(view.theme().paragraphFont()).height();
    require(hit.cursorRect.height() > 0.0 && hit.cursorRect.height() < lineHeight * 1.6,
            "caret after the split must render at normal line height");
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
  testTypingOnSplitBlankLineSplitsList();
  testEnterEnterCaretLandsOnSplitBlankLine();
  QApplication::clipboard()->clear();
  return 0;
}
