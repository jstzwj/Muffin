#include "commands/StylizeController.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/ClipboardController.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "projection/SelectionSerializer.h"
#include "EditorTestUtils.h"
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <iostream>
#include <variant>

using namespace muffin;

void testStylizeCollapsedSkeletons() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleBold(), "bold should wrap word");
  require(session.markdownText() == QStringLiteral("**alpha**"), "bold word-wrap text mismatch");
  require(selection.cursorPosition().text.textOffset == 7, "bold word-wrap cursor mismatch");
  require(selection.cursorPosition().text.sourceOffset == 7, "bold word-wrap source cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleItalic(), "italic should wrap word");
  require(session.markdownText() == QStringLiteral("*alpha*"), "italic word-wrap text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "italic word-wrap cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleCode(), "code should wrap word");
  require(session.markdownText() == QStringLiteral("`alpha`"), "code word-wrap text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "code word-wrap cursor mismatch");
  require(selection.cursorPosition().text.sourceOffset == 6, "code word-wrap source cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.insertLink(), "link skeleton should insert");
  require(session.markdownText() == QStringLiteral("alpha[](url)"), "link skeleton text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "link skeleton cursor mismatch");
}
void testTypingIntoCollapsedStyleSkeletons() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  StylizeController stylize;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("a  b"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(stylize.toggleBold(), "bold skeleton should insert before typing");
  require(input.insertText(QStringLiteral("B")), "typing into bold skeleton should work");
  require(session.markdownText() == QStringLiteral("a **B** b"), "bold skeleton typing mismatch");

  session.setMarkdownText(QStringLiteral("a  b"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(stylize.toggleCode(), "code skeleton should insert before typing");
  require(input.insertText(QStringLiteral("C")), "typing into code skeleton should work");
  require(session.markdownText() == QStringLiteral("a `C` b"), "code skeleton typing mismatch");

  session.setMarkdownText(QStringLiteral("a  b"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(stylize.toggleItalic(), "italic skeleton should insert before typing");
  require(input.insertText(QStringLiteral("I")), "typing into italic skeleton should work");
  require(session.markdownText() == QStringLiteral("a *I* b"), "italic skeleton typing mismatch");
}
void testStylizeSelectionWrap() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.toggleBold(), "bold should wrap selection");
  require(session.markdownText() == QStringLiteral("a**lph**a"), "bold wrap text mismatch");
  require(!selection.selection().isCollapsed(), "bold wrap should preserve selected text range");
  require(selection.selection().startOffset() == 3, "bold wrap selection start mismatch");
  require(selection.selection().endOffset() == 6, "bold wrap selection end mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.toggleItalic(), "italic should wrap selection");
  require(session.markdownText() == QStringLiteral("a*lph*a"), "italic wrap text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.toggleCode(), "code should wrap selection");
  require(session.markdownText() == QStringLiteral("a`lph`a"), "code wrap text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.insertLink(), "link should wrap selection");
  require(session.markdownText() == QStringLiteral("a[lph](url)a"), "link wrap text mismatch");
}
void testStylizeUndoRedoTextDelta() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleBold(), "bold should create transaction");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "style undo should use TextDeltaCommand");
  require(undo.textDeltaCommand().delta.start == 0, "style undo delta start mismatch");
  require(undo.textDeltaCommand().delta.removedText == QStringLiteral("alpha"), "style undo removed text mismatch");
  require(undo.textDeltaCommand().delta.insertedText == QStringLiteral("**alpha**"), "style undo inserted text mismatch");
  session.applyTextDelta(
      undo.textDeltaCommand().delta.start,
      undo.textDeltaCommand().delta.insertedText.size(),
      undo.textDeltaCommand().delta.removedText,
      true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText() == QStringLiteral("alpha"), "style undo text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "style undo cursor mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  require(redo.isTextDeltaCommand(), "style redo should use TextDeltaCommand");
  session.applyTextDelta(
      redo.textDeltaCommand().delta.start,
      redo.textDeltaCommand().delta.removedText.size(),
      redo.textDeltaCommand().delta.insertedText,
      true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText() == QStringLiteral("**alpha**"), "style redo text mismatch");
  require(selection.cursorPosition().text.textOffset == 7, "style redo cursor mismatch");
}
void testHeadingAndListItemStylize() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("# Title!"), false);
  setSelection(selection, blockAt(session, 0), 0, 5);
  require(stylize.toggleBold(), "heading selection style should wrap body text");
  require(session.markdownText() == QStringLiteral("# **Title**!"), "heading style should preserve marker");
  require(selection.selection().startOffset() == 2, "heading style selection start mismatch");
  require(selection.selection().endOffset() == 7, "heading style selection end mismatch");

  session.setMarkdownText(QStringLiteral("- alpha!\n- beta"), false);
  setSelection(selection, listItemAt(session, 0, 0), 0, 5);
  require(stylize.toggleItalic(), "unordered list style should wrap item body");
  require(session.markdownText() == QStringLiteral("- *alpha*!\n- beta"), "unordered list style should preserve marker");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setSelection(selection, listItemAt(session, 0, 0), 0, 5);
  require(stylize.toggleCode(), "ordered list style should wrap item body");
  require(session.markdownText() == QStringLiteral("1. `alpha`\n2. beta"), "ordered list style should preserve marker");
}
void testStylizeCrossParagraphSelectionWrap() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, blockAt(session, 2), 3);
  require(stylize.toggleBold(), "bold should wrap cross paragraph selection by block");
  require(session.markdownText() == QStringLiteral("al**pha**\n\n**beta**\n\n**gam**ma"), "cross paragraph bold wrap mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\n- alpha\n- beta\n\nomega"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, listItemAt(session, 1, 1), 2);
  require(stylize.toggleItalic(), "italic should wrap cross block selection by block");
  require(session.markdownText() == QStringLiteral("# Ti*tle*\n\n- *alpha*\n- *be*ta\n\nomega"), "cross block italic wrap mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCrossSelection(selection, blockAt(session, 1), 2, blockAt(session, 0), 2);
  require(stylize.toggleCode(), "code should wrap reverse cross paragraph selection by block");
  require(session.markdownText() == QStringLiteral("al`pha`\n\n`be`ta"), "reverse cross paragraph code wrap mismatch");
}
void testClipboardPlainText() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(clipboard.copy(), "copy should use editable selection");
  require(QApplication::clipboard()->text() == QStringLiteral("lph"), "clipboard copy text mismatch");

  require(clipboard.cut(), "cut should use editable selection");
  require(QApplication::clipboard()->text() == QStringLiteral("lph"), "clipboard cut text mismatch");
  require(session.markdownText() == QStringLiteral("aa"), "clipboard cut document mismatch");

  QApplication::clipboard()->setText(QStringLiteral("XYZ"));
  require(clipboard.paste(), "paste should insert clipboard text");
  require(session.markdownText() == QStringLiteral("aXYZa"), "clipboard paste document mismatch");
}
void testClipboardMarkdownPreservesLinePrefix() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  session.setMarkdownText(QStringLiteral("## 技术栈\n\n> quoted text\n\n- list item"), false);
  setSelection(selection, blockAt(session, 0), 1, 3);
  require(clipboard.copy(), "heading markdown copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("## 术栈"), "heading markdown copy should preserve heading marker");

  MarkdownNode* quoteParagraph = childAt(blockAt(session, 1), 0);
  setSelection(selection, quoteParagraph, 0, 6);
  require(clipboard.copy(), "blockquote markdown copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("> quoted"), "blockquote markdown copy should preserve quote marker");

  MarkdownNode* listItem = listItemAt(session, 2, 0);
  setSelection(selection, listItem, 0, 4);
  require(clipboard.copy(), "list markdown copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("- list"), "list markdown copy should preserve list marker");
}
void testClipboardCrossMarkdownPreservesStartPrefix() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  session.setMarkdownText(QStringLiteral("## Block Quote\n\n> Muffin treats Markdown as the source of truth.\n>\n> The editor surface"), false);
  MarkdownNode* secondQuoteParagraph = childAt(blockAt(session, 1), 1);
  setCrossSelection(selection, blockAt(session, 0), 1, secondQuoteParagraph, 10);
  require(clipboard.copy(), "cross heading quote markdown copy should work");
  require(QApplication::clipboard()->text() ==
              QStringLiteral("## lock Quote\n\n> Muffin treats Markdown as the source of truth.\n>\n> The editor"),
          "cross heading quote markdown copy should preserve heading marker");

  setCrossSelection(selection, secondQuoteParagraph, 10, blockAt(session, 0), 1);
  require(clipboard.copy(), "reverse cross heading quote markdown copy should work");
  require(QApplication::clipboard()->text() ==
              QStringLiteral("## lock Quote\n\n> Muffin treats Markdown as the source of truth.\n>\n> The editor"),
          "reverse cross heading quote markdown copy should preserve heading marker");
}
void testSelectionSerializerFormats() {
  DocumentSession session;
  SelectionController selection;
  SelectionSerializer serializer;

  session.setMarkdownText(QStringLiteral("## Headings\n\nalpha"), false);
  setCrossSelection(selection, blockAt(session, 0), 1, blockAt(session, 1), 2);

  SelectionExportResult markdown =
      serializer.exportSelection(SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::Markdown});
  require(markdown.mimeType == QStringLiteral("text/markdown"), "markdown export mime mismatch");
  require(markdown.text == QStringLiteral("## eadings\n\nal"), "markdown export should preserve only structural prefix");

  SelectionExportResult plainText =
      serializer.exportSelection(SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::PlainText});
  require(plainText.mimeType == QStringLiteral("text/plain"), "plain text export mime mismatch");
  require(plainText.text == QStringLiteral("eadings\nal"), "plain text export mismatch");

  SelectionExportResult html =
      serializer.exportSelection(SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::Html});
  require(html.mimeType == QStringLiteral("text/html"), "html export mime mismatch");
  require(html.text.contains(QStringLiteral("<h2>eadings</h2>")), "html export should render markdown to HTML");
}
void testSelectionSerializerCrossComplexInlineEdges() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(QStringLiteral("Plain **bold** and `code`\n\nMiddle\n\nTail $x+y$ and [link](https://example.com)"), false);
  setCrossSelection(selection, blockAt(session, 0), 6, blockAt(session, 2), 8);

  const QString markdown = selectedMarkdown(session, selection);
  require(markdown == QStringLiteral("**bold** and `code`\n\nMiddle\n\nTail $x+y$"),
          "complex inline cross selection should preserve only selected edge text");

  const QString plainText = selectedPlainText(session, selection);
  require(plainText == QStringLiteral("bold and code\nMiddle\nTail x+y"), "complex inline cross selection plain text mismatch");
}
int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testStylizeCollapsedSkeletons);
  RUN_TEST(testTypingIntoCollapsedStyleSkeletons);
  RUN_TEST(testStylizeSelectionWrap);
  RUN_TEST(testStylizeUndoRedoTextDelta);
  RUN_TEST(testHeadingAndListItemStylize);
  RUN_TEST(testStylizeCrossParagraphSelectionWrap);
  RUN_TEST(testClipboardPlainText);
  RUN_TEST(testClipboardMarkdownPreservesLinePrefix);
  RUN_TEST(testClipboardCrossMarkdownPreservesStartPrefix);
  RUN_TEST(testSelectionSerializerFormats);
  RUN_TEST(testSelectionSerializerCrossComplexInlineEdges);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}