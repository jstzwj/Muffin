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
#include "projection/HtmlMarkdownImporter.h"
#include "EditorTestUtils.h"
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMimeData>
#include <QSettings>
#include <QTemporaryDir>
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
  require(session.markdownText().toString() == QStringLiteral("**alpha**"), "bold word-wrap text mismatch");
  require(selection.cursorPosition().text.textOffset == 7, "bold word-wrap cursor mismatch");
  require(selection.cursorPosition().text.sourceOffset == 7, "bold word-wrap source cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleItalic(), "italic should wrap word");
  require(session.markdownText().toString() == QStringLiteral("*alpha*"), "italic word-wrap text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "italic word-wrap cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.toggleCode(), "code should wrap word");
  require(session.markdownText().toString() == QStringLiteral("`alpha`"), "code word-wrap text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "code word-wrap cursor mismatch");
  require(selection.cursorPosition().text.sourceOffset == 6, "code word-wrap source cursor mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(stylize.insertLink(), "link skeleton should insert");
  require(session.markdownText().toString() == QStringLiteral("alpha[](url)"), "link skeleton text mismatch");
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
  require(session.markdownText().toString() == QStringLiteral("a **B** b"), "bold skeleton typing mismatch");

  session.setMarkdownText(QStringLiteral("a  b"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(stylize.toggleCode(), "code skeleton should insert before typing");
  require(input.insertText(QStringLiteral("C")), "typing into code skeleton should work");
  require(session.markdownText().toString() == QStringLiteral("a `C` b"), "code skeleton typing mismatch");

  session.setMarkdownText(QStringLiteral("a  b"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(stylize.toggleItalic(), "italic skeleton should insert before typing");
  require(input.insertText(QStringLiteral("I")), "typing into italic skeleton should work");
  require(session.markdownText().toString() == QStringLiteral("a *I* b"), "italic skeleton typing mismatch");
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
  require(session.markdownText().toString() == QStringLiteral("a**lph**a"), "bold wrap text mismatch");
  require(!selection.selection().isCollapsed(), "bold wrap should preserve selected text range");
  require(selection.selection().startOffset() == 3, "bold wrap selection start mismatch");
  require(selection.selection().endOffset() == 6, "bold wrap selection end mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.toggleItalic(), "italic should wrap selection");
  require(session.markdownText().toString() == QStringLiteral("a*lph*a"), "italic wrap text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.toggleCode(), "code should wrap selection");
  require(session.markdownText().toString() == QStringLiteral("a`lph`a"), "code wrap text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(stylize.insertLink(), "link should wrap selection");
  require(session.markdownText().toString() == QStringLiteral("a[lph](url)a"), "link wrap text mismatch");
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
  require(undo.textDeltaCommand().delta.insertedText == QStringLiteral("**alpha**"),
          "style undo inserted text mismatch");
  session.applyTextDelta(undo.textDeltaCommand().delta.start, undo.textDeltaCommand().delta.insertedText.size(),
                         undo.textDeltaCommand().delta.removedText, true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText().toString() == QStringLiteral("alpha"), "style undo text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "style undo cursor mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  require(redo.isTextDeltaCommand(), "style redo should use TextDeltaCommand");
  session.applyTextDelta(redo.textDeltaCommand().delta.start, redo.textDeltaCommand().delta.removedText.size(),
                         redo.textDeltaCommand().delta.insertedText, true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText().toString() == QStringLiteral("**alpha**"), "style redo text mismatch");
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
  require(session.markdownText().toString() == QStringLiteral("# **Title**!"), "heading style should preserve marker");
  require(selection.selection().startOffset() == 2, "heading style selection start mismatch");
  require(selection.selection().endOffset() == 7, "heading style selection end mismatch");

  session.setMarkdownText(QStringLiteral("- alpha!\n- beta"), false);
  setSelection(selection, listItemAt(session, 0, 0), 0, 5);
  require(stylize.toggleItalic(), "unordered list style should wrap item body");
  require(session.markdownText().toString() == QStringLiteral("- *alpha*!\n- beta"),
          "unordered list style should preserve marker");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setSelection(selection, listItemAt(session, 0, 0), 0, 5);
  require(stylize.toggleCode(), "ordered list style should wrap item body");
  require(session.markdownText().toString() == QStringLiteral("1. `alpha`\n2. beta"),
          "ordered list style should preserve marker");
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
  require(session.markdownText().toString() == QStringLiteral("al**pha**\n\n**beta**\n\n**gam**ma"),
          "cross paragraph bold wrap mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\n- alpha\n- beta\n\nomega"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, listItemAt(session, 1, 1), 2);
  require(stylize.toggleItalic(), "italic should wrap cross block selection by block");
  require(session.markdownText().toString() == QStringLiteral("# Ti*tle*\n\n- *alpha*\n- *be*ta\n\nomega"),
          "cross block italic wrap mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCrossSelection(selection, blockAt(session, 1), 2, blockAt(session, 0), 2);
  require(stylize.toggleCode(), "code should wrap reverse cross paragraph selection by block");
  require(session.markdownText().toString() == QStringLiteral("al`pha`\n\n`be`ta"),
          "reverse cross paragraph code wrap mismatch");
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
  require(session.markdownText().toString() == QStringLiteral("aa"), "clipboard cut document mismatch");

  QApplication::clipboard()->setText(QStringLiteral("XYZ"));
  require(clipboard.paste(), "paste should insert clipboard text");
  require(session.markdownText().toString() == QStringLiteral("aXYZa"), "clipboard paste document mismatch");
}
void testClipboardMarkdownPartialBlocks() {
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
  require(QApplication::clipboard()->text() == QStringLiteral("术栈"),
          "partial heading copy must not add its heading marker");

  MarkdownNode* quoteParagraph = childAt(blockAt(session, 1), 0);
  setSelection(selection, quoteParagraph, 0, 6);
  require(clipboard.copy(), "blockquote markdown copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("quoted"),
          "partial quote copy must not add its quote marker");

  MarkdownNode* listItem = listItemAt(session, 2, 0);
  setSelection(selection, listItem, 0, 4);
  require(clipboard.copy(), "list markdown copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("list"),
          "partial list copy must not add its list marker");
}
void testClipboardCrossMarkdownPartialEdges() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);

  session.setMarkdownText(
      QStringLiteral("## Block Quote\n\n> Muffin treats Markdown as the source of truth.\n>\n> The editor surface"),
      false);
  MarkdownNode* secondQuoteParagraph = childAt(blockAt(session, 1), 1);
  setCrossSelection(selection, blockAt(session, 0), 1, secondQuoteParagraph, 10);
  require(clipboard.copy(), "cross heading quote markdown copy should work");
  require(QApplication::clipboard()->text() ==
              QStringLiteral("lock Quote\n\n> Muffin treats Markdown as the source of truth.\n>\nThe editor"),
          "cross-block copy preserves interior structure without adding syntax to partial edges");

  setCrossSelection(selection, secondQuoteParagraph, 10, blockAt(session, 0), 1);
  require(clipboard.copy(), "reverse cross heading quote markdown copy should work");
  require(QApplication::clipboard()->text() ==
              QStringLiteral("lock Quote\n\n> Muffin treats Markdown as the source of truth.\n>\nThe editor"),
          "reverse cross-block copy uses the same partial-edge rules");
}
void testSelectionSerializerFormats() {
  DocumentSession session;
  SelectionController selection;
  SelectionSerializer serializer;

  session.setMarkdownText(QStringLiteral("## Headings\n\nalpha"), false);
  setCrossSelection(selection, blockAt(session, 0), 1, blockAt(session, 1), 2);

  SelectionExportResult markdown = serializer.exportSelection(
      SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::Markdown});
  require(markdown.mimeType == QStringLiteral("text/markdown"), "markdown export mime mismatch");
  require(markdown.text == QStringLiteral("eadings\n\nal"),
          "markdown export should not add an unselected structural prefix");

  SelectionExportResult plainText = serializer.exportSelection(
      SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::PlainText});
  require(plainText.mimeType == QStringLiteral("text/plain"), "plain text export mime mismatch");
  require(plainText.text == QStringLiteral("eadings\nal"), "plain text export mismatch");

  SelectionExportResult html = serializer.exportSelection(
      SelectionExportRequest{&session.document(), selection.selection(), SelectionExportFormat::Html});
  require(html.mimeType == QStringLiteral("text/html"), "html export mime mismatch");
  require(html.text.contains(QStringLiteral("<p>eadings</p>")) && !html.text.contains(QStringLiteral("<h2>")),
          "HTML must not turn a heading fragment into a whole heading");
}
void testClipboardBlockCoverageAcrossFormats() {
  const QString body = QStringLiteral("你好世界");
  for (const QString& markdown :
       {QStringLiteral("### 你好世界"), QStringLiteral("   ### 你好世界"), QStringLiteral("你好世界\n---"),
        QStringLiteral("> 你好世界"), QStringLiteral("- 你好世界"), QStringLiteral("12. 你好世界"),
        QStringLiteral("- [x] 你好世界")}) {
    for (const auto offsets : {QPair<qsizetype, qsizetype>{2, 4}, {0, 2}, {1, 3}, {0, 4}}) {
      for (const bool reverse : {false, true}) {
        for (const bool preferMarkdown : {false, true}) {
          SettingsOverride preference("editor/copyAsMarkdown", preferMarkdown);
          DocumentSession session;
          SelectionController selection;
          UndoStack undo;
          BrushQueue brush;
          InputController input;
          ClipboardController clipboard;
          wireInput(input, session, selection, undo, brush);
          wireClipboard(clipboard, session, selection, input);
          session.setMarkdownText(markdown + QStringLiteral("\n\nuntouched"), false);
          auto* node = blockAt(session, 0);
          while (node->type() == BlockType::List || node->type() == BlockType::BlockQuote)
            node = childAt(node, 0);
          setSelection(selection, node, reverse ? offsets.second : offsets.first,
                       reverse ? offsets.first : offsets.second);
          const QString plain = body.mid(offsets.first, offsets.second - offsets.first);
          const bool whole = offsets.first == 0 && offsets.second == body.size();
          const QString expected = whole ? markdown : plain;
          const QString detail = QStringLiteral("block=%1 range=%2..%3 reverse=%4 preferMarkdown=%5")
                                     .arg(markdown)
                                     .arg(offsets.first)
                                     .arg(offsets.second)
                                     .arg(reverse)
                                     .arg(preferMarkdown);
          require(clipboard.copy(), detail + QStringLiteral(" copy"));
          const auto* mime = QApplication::clipboard()->mimeData();
          require(mime->data("text/markdown") == expected.toUtf8(),
                  detail + QStringLiteral(" Markdown coverage actual=[%1] expected=[%2]")
                               .arg(QString::fromUtf8(mime->data("text/markdown")), expected));
          require(mime->text() == (preferMarkdown ? expected : plain), detail + QStringLiteral(" plain target"));
          require(mime->html().contains(plain), detail + QStringLiteral(" HTML keeps selected content"));
          if (!whole)
            require(!mime->html().contains(QStringLiteral("<h3>")) && !mime->html().contains(QStringLiteral("<h2>")) &&
                        !mime->html().contains(QStringLiteral("<li>")) &&
                        !mime->html().contains(QStringLiteral("<blockquote>")),
                    detail + QStringLiteral(" HTML must remain an inline text fragment"));
          require(clipboard.copyAsMarkdown() && QApplication::clipboard()->text() == expected,
                  detail + QStringLiteral(" explicit Markdown copy"));
          require(clipboard.copyAsPlainText() && QApplication::clipboard()->text() == plain,
                  detail + QStringLiteral(" explicit plain-text copy"));
          require(clipboard.copyAsHtml() && QApplication::clipboard()->mimeData()->html().contains(plain),
                  detail + QStringLiteral(" explicit HTML copy"));
        }
      }
    }
  }
}
void testClipboardPartialEdgeStructures() {
  for (const QString& first : {QStringLiteral("### 你好世界"), QStringLiteral("你好世界\n---"),
                               QStringLiteral("- 你好世界"), QStringLiteral("> 你好世界")}) {
    for (const bool wholeFirst : {false, true}) {
      for (const bool wholeLast : {false, true}) {
        for (const bool reverse : {false, true}) {
          DocumentSession session;
          SelectionController selection;
          session.setMarkdownText(first + QStringLiteral("\n\n## 中间块\n\n### 再见朋友\n\n尾段"), false);
          auto* firstBlock = blockAt(session, 0);
          while (firstBlock->type() == BlockType::List || firstBlock->type() == BlockType::BlockQuote)
            firstBlock = childAt(firstBlock, 0);
          auto* lastBlock = blockAt(session, 2);
          const qsizetype firstOffset = wholeFirst ? 0 : 2;
          const qsizetype lastOffset = wholeLast ? 4 : 2;
          setCrossSelection(selection, reverse ? lastBlock : firstBlock, reverse ? lastOffset : firstOffset,
                            reverse ? firstBlock : lastBlock, reverse ? firstOffset : lastOffset);
          const QString expected = (wholeFirst ? first : QStringLiteral("世界")) + QStringLiteral("\n\n## 中间块\n\n") +
                                   (wholeLast ? QStringLiteral("### 再见朋友") : QStringLiteral("再见"));
          require(selectedMarkdown(session, selection) == expected,
                  QStringLiteral("partial edge structure in %1 wholeFirst=%2 wholeLast=%3 reverse=%4")
                      .arg(first)
                      .arg(wholeFirst)
                      .arg(wholeLast)
                      .arg(reverse));
        }
      }
    }
  }
  for (const QString& markdown :
       {QStringLiteral("> 你好世界\n> 第二行结束"), QStringLiteral("> 你好世界  \n> \\> 正文")}) {
    DocumentSession session;
    SelectionController selection;
    session.setMarkdownText(markdown, false);
    auto* paragraph = childAt(blockAt(session, 0), 0);
    // Source endpoints come from the same mapping used by real mouse selection.
    setSourceSelection(selection, paragraph, paragraph, 2, 4, 4, markdown.size());
    const QString expected = markdown.contains(QStringLiteral("第二行")) ? QStringLiteral("世界\n第二行结束")
                                                                         : QStringLiteral("世界  \n\\> 正文");
    require(selectedMarkdown(session, selection) == expected,
            "partial multiline quote omits container prefixes but retains literal greater-than text");
  }
  for (const QString& markdown : {QStringLiteral("开头\n\n- 父项\n  - 你好世界"),
                                  QStringLiteral("开头\n\n| 左列 | 右列 |\n| --- | --- |\n| 甲乙 | 丙丁 |\n\n尾段")}) {
    DocumentSession session;
    EditorController controller;
    controller.attach(&session, nullptr);
    session.setMarkdownText(markdown, false);
    const bool table = markdown.contains(QLatin1Char('|'));
    const auto end = table ? markdown.indexOf(QStringLiteral("丙")) + 1 : markdown.indexOf(QStringLiteral("你好")) + 2;
    controller.selectSourceRange(1, end);
    const QString expected = table ? markdown.mid(1, end - 1) : QStringLiteral("头\n\n- 父项\n你好");
    require(selectedMarkdown(session, controller.selection()) == expected,
            "partial trailing list removes ancestor indentation while a table retains preceding selected cells");
    const auto range = controller.selection().selection();
    controller.selection().setSelection({range.focus, range.anchor});
    require(selectedMarkdown(session, controller.selection()) == expected,
            "nested and table edge coverage is independent of selection direction");
  }
}

void testClipboardHeadingFragmentCutPasteUndo() {
  SettingsOverride preference("editor/copyAsMarkdown", true);
  for (const QString& heading : {QStringLiteral("### 你好世界"), QStringLiteral("### 你好**世界**")}) {
    DocumentSession session;
    EditorController controller;
    controller.attach(&session, nullptr);
    const QString original = QStringLiteral("前言🙂\n\n") + heading + QStringLiteral("\n\n落点");
    session.setMarkdownText(original, false);
    const auto start = original.indexOf(QStringLiteral("世界"));
    controller.selectSourceRange(start, start + 2);
    require(controller.clipboardController().copy(), "copy a heading fragment through actual controller selection");
    require(QApplication::clipboard()->mimeData()->data("text/markdown") == QStringLiteral("世界").toUtf8(),
            "copying only 世界 must not copy heading or unselected emphasis markers");
    const auto destination = original.indexOf(QStringLiteral("落点")) + 1;
    controller.selectSourceRange(destination, destination);
    require(controller.clipboardController().paste(), "paste heading text into an ordinary paragraph");
    QString expected = original;
    expected.insert(destination, QStringLiteral("世界"));
    require(session.markdownText().toString() == expected, "pasting a heading fragment must not create a heading");
    controller.undo();
    require(session.markdownText().toString() == original, "undo heading-fragment paste");
    controller.redo();
    require(session.markdownText().toString() == expected, "redo heading-fragment paste");
    controller.undo();
    controller.selectSourceRange(start, start + 2);
    require(controller.clipboardController().cut(), "cut a heading fragment");
    expected = original;
    expected.remove(start, 2);
    require(session.markdownText().toString() == expected &&
                QApplication::clipboard()->text() == QStringLiteral("世界"),
            "cut removes only the selected text and preserves the heading structure");
    controller.undo();
    require(session.markdownText().toString() == original &&
                selectedMarkdown(session, controller.selection()) == QStringLiteral("世界"),
            "undo cut restores both text and the exact partial selection");
  }
}
void testSelectionSerializerCrossComplexInlineEdges() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(
      QStringLiteral("Plain **bold** and `code`\n\nMiddle\n\nTail $x+y$ and [link](https://example.com)"), false);
  setCrossSelection(selection, blockAt(session, 0), 6, blockAt(session, 2), 8);

  const QString markdown = selectedMarkdown(session, selection);
  require(markdown == QStringLiteral("**bold** and `code`\n\nMiddle\n\nTail $x+y$"),
          "complex inline cross selection should preserve only selected edge text");

  const QString plainText = selectedPlainText(session, selection);
  require(plainText == QStringLiteral("bold and code\nMiddle\nTail x+y"),
          "complex inline cross selection plain text mismatch");
}

// Clear Formatting strips every inline marker from the cursor's paragraph while
// leaving content (math, images) intact. One generic rule covers every styling
// type: the source region outside a node's content range is its delimiter syntax.
void testClearFormatting() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  struct Case {
    QString input;
    QString expected;
    qsizetype cursor;         // visible offset to drop the cursor before clearing
    qsizetype expectedCursor; // visible offset the cursor should land on after clearing
  };
  const Case cases[] = {
      {QStringLiteral("**bold** and *italic*"), QStringLiteral("bold and italic"), 3, 3},
      {QStringLiteral("`code` here"), QStringLiteral("code here"), 2, 2},
      {QStringLiteral("~~strike~~"), QStringLiteral("strike"), 2, 2},
      {QStringLiteral("[label](https://x)"), QStringLiteral("label"), 2, 2},
      {QStringLiteral("__u__ _i_"), QStringLiteral("u i"), 1, 1},
      // Nested formatting: outer + inner markers all stripped, content kept.
      {QStringLiteral("**bold *and* more**"), QStringLiteral("bold and more"), 5, 5},
      // Content is preserved: inline math and images are not "formatting".
      {QStringLiteral("**bold** $x^2$"), QStringLiteral("bold $x^2$"), 2, 2},
      {QStringLiteral("**b** ![alt](u)"), QStringLiteral("b ![alt](u)"), 1, 1},
      // Underline is raw HTML (<u></u>); its tags are stripped, inner text kept.
      {QStringLiteral("a <u>under</u> b"), QStringLiteral("a under b"), 4, 4},
  };

  for (const Case& c : cases) {
    session.setMarkdownText(c.input, false);
    setCursor(selection, blockAt(session, 0), c.cursor);
    require(stylize.clearFormatting(), "clearFormatting should report a change when formatting is present");
    if (session.markdownText().toString() != c.expected) {
      std::cerr << "clearFormatting text mismatch: input [" << c.input.toUtf8().data() << "] expected ["
                << c.expected.toUtf8().data() << "] got [" << session.markdownText().toString().toUtf8().data()
                << "]\n";
    }
    require(session.markdownText().toString() == c.expected, "clearFormatting should strip markers, keep content");
    require(selection.cursorPosition().blockId.isValid(), "clearFormatting should keep a valid cursor");
    require(selection.cursorPosition().text.textOffset == c.expectedCursor,
            "clearFormatting should preserve the cursor's visible position");
    require(undoStack.canUndo(), "clearFormatting should push an undo entry");
    undoStack.clear();
  }

  // No formatting → no-op: returns false, text untouched, nothing pushed to undo.
  session.setMarkdownText(QStringLiteral("just plain text"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(!stylize.clearFormatting(), "clearFormatting should be a no-op when no formatting is present");
  require(session.markdownText().toString() == QStringLiteral("just plain text"),
          "clearFormatting no-op must not alter text");
  require(!undoStack.canUndo(), "clearFormatting no-op must not push undo");

  // Math/images alone are content, not formatting → also a no-op.
  session.setMarkdownText(QStringLiteral("$x^2$ and ![alt](u)"), false);
  setCursor(selection, blockAt(session, 0), 2);
  require(!stylize.clearFormatting(), "clearFormatting should not treat math/images as formatting");
  require(session.markdownText().toString() == QStringLiteral("$x^2$ and ![alt](u)"),
          "clearFormatting must leave math/images untouched");
}

void testClearFormattingUndoRedo() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  StylizeController stylize;
  wireStyle(stylize, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("**bold** and *italic*"), false);
  setCursor(selection, blockAt(session, 0), 3);
  require(stylize.clearFormatting(), "clearFormatting should clear the paragraph");
  require(session.markdownText().toString() == QStringLiteral("bold and italic"), "clearFormatting text mismatch");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "clearFormatting undo should use TextDeltaCommand");
  require(undo.textDeltaCommand().delta.removedText == QStringLiteral("**bold** and *italic*"),
          "clearFormatting undo removedText should be the formatted original");
  require(undo.textDeltaCommand().delta.insertedText == QStringLiteral("bold and italic"),
          "clearFormatting undo insertedText should be the cleared text");
  // Undo: replace the cleared text with the formatted original.
  session.applyTextDelta(undo.textDeltaCommand().delta.start, undo.textDeltaCommand().delta.insertedText.size(),
                         undo.textDeltaCommand().delta.removedText, true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText().toString() == QStringLiteral("**bold** and *italic*"),
          "clearFormatting undo should restore formatting");

  // Redo: re-apply the clearing.
  const EditTransaction redo = undoStack.takeRedo();
  require(redo.isTextDeltaCommand(), "clearFormatting redo should use TextDeltaCommand");
  session.applyTextDelta(redo.textDeltaCommand().delta.start, redo.textDeltaCommand().delta.removedText.size(),
                         redo.textDeltaCommand().delta.insertedText, true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText().toString() == QStringLiteral("bold and italic"),
          "clearFormatting redo should re-clear");
}
void testBrowserHeadingClipboard() {
  const auto github = QStringLiteral(
      "<p>MikuDanceStudio: <a href='https://learnmmd.com/downloads/'>MikuMikuDance</a>.</p>"
      "<div class='markdown-heading'><h3 class='heading-element' style='font-weight:600'>Features</h3>"
      "<a class='anchor' aria-label='Permalink: Features' href='https://github.com/example/studio#features' "
      "style='opacity:0'><svg aria-hidden='true'><path d='M0 0'/></svg></a></div>"
      "<ul><li><strong>Models</strong>: PMD / PMX</li><li><strong>UI</strong>: EN/JP</li></ul>"
      "<div class='markdown-heading'><h3>Building from Source</h3>"
      "<a class='anchor' href='https://github.com/example/studio#building-from-source'><svg></svg></a></div>"
      "<p>Dependencies: <a href='https://cmake.org/'>CMake</a>.</p>");
  const auto expected = QStringLiteral("MikuDanceStudio: [MikuMikuDance](https://learnmmd.com/downloads/)\\.\n\n"
                                       "### Features\n\n- **Models**: PMD / PMX\n- **UI**: EN/JP\n\n"
                                       "### Building from Source\n\nDependencies: [CMake](https://cmake.org/)\\.");
  require(importHtmlMarkdown(github) == expected,
          QStringLiteral("GitHub heading permalink controls must not become empty links: ") +
              importHtmlMarkdown(github));
  require(importHtmlMarkdown("<h3><a href='#features'><svg></svg></a>Features</h3>") == "### Features",
          "permalink icons inside headings do not alter heading text");
  require(importHtmlMarkdown("<h3><a href='guide.md#features'><strong>Features</strong></a></h3>") ==
              "### [**Features**](guide.md#features)",
          "headings retain real links and emphasis");
  require(importHtmlMarkdown("<p><a href='guide.md'><img src='icon.png' alt='Guide'></a></p>") ==
              "[![Guide](icon.png)](guide.md)",
          "linked images remain meaningful link content");
  require(importHtmlMarkdown("<h3>Features<a href='#features'> \n </a></h3>") == "### Features",
          "whitespace-only heading links are omitted");

  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);
  session.setMarkdownText(QStringLiteral("replace me"), false);
  setSelection(selection, blockAt(session, 0), 0, 10);
  auto* mime = new QMimeData;
  mime->setHtml("<html><body><p>Outside selection</p><!--StartFragment-->" + github +
                "<!--EndFragment--><p>Also outside selection</p></body></html>");
  mime->setText(QStringLiteral("Plain text fallback"));
  QApplication::clipboard()->setMimeData(mime);
  require(clipboard.paste(), "paste a browser HTML fragment");
  require(session.markdownText().toString() == expected,
          "browser paste retains headings, list formatting and actual links without page controls");
  const auto outline = session.document().outline();
  require(outline.size() == 2 && outline[0].level == 3 && outline[0].title == "Features" &&
              outline[1].title == "Building from Source",
          "pasted browser headings remain clean in the document outline");
}

void testRichClipboardInterop() {
  require(importHtmlMarkdown("<p>- literal</p><p>1. literal</p><p>$value$ &amp;amp;</p>") ==
              "\\- literal\n\n1\\. literal\n\n\\$value\\$ \\&amp;",
          "HTML text cannot accidentally become Markdown lists, math or entities");
  require(importHtmlMarkdown("<p><code> x </code> <code>   </code></p>") == "`  x  ` `   `",
          "HTML inline code preserves significant leading and trailing spaces");
  require(importHtmlMarkdown("<p><b>one</b> <i>two</i> <a href='https://example.com'>three</a></p>") ==
              "**one** *two* [three](https://example.com)",
          "HTML inline spacing is preserved independently of fonts");
  const auto importedTable = importHtmlMarkdown("<table><tr><th>Name</th><th>Value</th></tr>"
                                                "<tr><td><b>bold</b></td><td>a<br>b</td></tr></table>");
  require(importedTable.contains("| **bold** | a  <br>b |"),
          QStringLiteral("HTML spreadsheet preserves cell structure and rich content: ") + importedTable);
  require(importHtmlMarkdown("<pre><code class='language-cpp'>int a = 1;\n\n  x();\n</code></pre>") ==
              "```cpp\nint a = 1;\n\n  x();\n```",
          "HTML code retains blank lines and indentation");
  const auto unsafe = importHtmlMarkdown("<p><a href='javascript:alert(1)'>safe label</a>"
                                         "<img src='https://example.com/a.png' alt='image'></p><script>bad()</script>");
  require(!unsafe.contains("javascript") && !unsafe.contains("bad()") &&
              unsafe.contains("![image](https://example.com/a.png)"),
          "HTML import shares URL policy and strips active content");
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  ClipboardController clipboard;
  wireInput(input, session, selection, undoStack, brushQueue);
  wireClipboard(clipboard, session, selection, input);
  const auto reset = [&] {
    session.setMarkdownText(QStringLiteral("text"), false);
    setSelection(selection, blockAt(session, 0), 0, 4);
  };
  reset();
  auto* rich = new QMimeData;
  rich->setHtml(
      QStringLiteral("<h2>中文标题</h2><p><strong>重点</strong>与 <a href=\"https://example.com\">链接</a></p>"
                     "<ul><li>第一项</li><li>第二项</li></ul><script>unwanted()</script>"));
  rich->setText(QStringLiteral("plain fallback"));
  QApplication::clipboard()->setMimeData(rich);
  require(clipboard.paste(), "paste rich content");
  const auto imported = session.markdownText().toString();
  require(imported.contains(QStringLiteral("## 中文标题")) && imported.contains(QStringLiteral("**重点**")) &&
              imported.contains(QStringLiteral("[链接](https://example.com)")) &&
              imported.contains(QStringLiteral("第一项")) && !imported.contains("unwanted"),
          QStringLiteral("rich paste preserves semantic Markdown: ") + imported);
  reset();
  require(clipboard.pasteAsPlainText() && session.markdownText().toString() == "plain fallback",
          "plain paste explicitly ignores HTML");

  reset();
  QApplication::clipboard()->setText(QStringLiteral("名称\t说明\r\n中文\t\"换行\n单元格\"\r\n"));
  require(clipboard.paste(), "paste spreadsheet cells");
  require(session.markdownText().toString().contains(QStringLiteral("| 中文 | 换行<br>单元格 |")) &&
              blockAt(session, 0)->type() == BlockType::Table,
          "spreadsheet TSV becomes an editable table");

  session.setMarkdownText(QStringLiteral("**中文** [链接](note.md)"), false);
  const auto* paragraph = blockAt(session, 0);
  selection.setSelection(
      {inlineCursor(paragraph->id(), 0, 0), inlineCursor(paragraph->id(), 5, session.markdownText().size())});
  const auto preference = QSettings().value("editor/copyAsMarkdown");
  QSettings().setValue("editor/copyAsMarkdown", false);
  require(clipboard.copy(), "copy rich selection");
  const auto* copied = QApplication::clipboard()->mimeData();
  require(copied->hasHtml() && copied->hasFormat("text/markdown") && copied->html().contains("<strong>") &&
              !copied->text().contains("**"),
          QStringLiteral(
              "copy provides rich HTML and Markdown independently of plain text preference: formats=%1 html=%2 text=%3")
              .arg(copied->formats().join(','), copied->html(), copied->text()));
  const auto exact = QString::fromUtf8(copied->data("text/markdown"));
  reset();
  require(clipboard.paste() && session.markdownText().toString() == exact,
          "Muffin round trip uses exact Markdown MIME");
  if (preference.isValid())
    QSettings().setValue("editor/copyAsMarkdown", preference);
  else
    QSettings().remove("editor/copyAsMarkdown");

  session.setMarkdownText(QStringLiteral("```cpp\nvalue\n```\n"), false);
  EditorView literalView;
  literalView.setDocument(session.document());
  EditorController literalEditor;
  literalEditor.attach(&session, &literalView);
  setCursor(literalEditor.selection(), blockAt(session, 0), 0);
  require(literalEditor.codeFenceController().enterEditMode(), "literal paste fixture enters code editing");
  auto* code = new QMimeData;
  code->setHtml("<b>changed</b>");
  code->setText("a\tb\nc\td");
  QApplication::clipboard()->setMimeData(code);
  require(literalEditor.clipboardController().paste() && session.markdownText().toString().contains("a\tb\nc\td"),
          "literal paste keeps original tabs and text, ignoring HTML and table conversion");
}

void testHtmlImportStructure() {
  struct Case {
    const char* html;
    const char* markdown;
  };
  const Case cases[] = {
      {"<div><h3>Features<a href='#features'><strong><code></code></strong></a></h3></div>", "### Features"},
      {"<h3>Features</h3><a href='#features'><code></code><u></u><em></em></a><p>Body</p>", "### Features\n\nBody"},
      {"<h3>Features</h3><a href='#features'>\xe2\x80\x8b</a>", "### Features"},
      {"<p>one<a href='#empty'><span> </span></a>two</p>", "one two"},
      {"<h3>Features</h3><h4><b></b></h4><blockquote><p><br></p></blockquote><p>Body</p>", "### Features\n\nBody"},
      {"<p><code></code><strong></strong><a href='#empty'><img></a></p>", ""},
      {"<div>\n  <section>\n<h3>Features</h3>\n </section>\n</div>\n<p>Body</p>", "### Features\n\nBody"},
      {"<div>before<section><p>middle</p></section>after</div>", "before\n\nmiddle\n\nafter"},
      {"<div><h3>Features</h3></div>\n<div><ul><li><b>Models</b></li><li>Motion</li></ul></div>",
       "### Features\n\n- **Models**\n- Motion"},
      {"<p>before</p><div><pre><code>one\n\n\nthree\n</code></pre></div><p>after</p>",
       "before\n\n```\none\n\n\nthree\n```\n\nafter"},
      {"<h3>Features<span hidden><a href='#features'>Permalink</a></span></h3>", "### Features"},
      {"<div hidden='until-found'><h3>Hidden</h3></div><p>Body</p>", "Body"},
      {"<custom-wrapper hidden><h3>Hidden</h3></custom-wrapper><p>Body</p>", "Body"},
      {"<custom-wrapper style='display:none'><h3>Hidden</h3></custom-wrapper><p>Body</p>", "Body"},
      {"<h3>Features<a href='#features' style='DISPLAY: /* note */ none !important'>#</a></h3>", "### Features"},
      {"<p><span style='display:none!important; display:inline'>Hidden</span>Body</p>", "Body"},
      {"<p><span style='display:none; display:inline'>Visible</span></p>", "Visible"},
      {"<h3><a href='guide.md#features'><strong>Features</strong></a></h3>", "### [**Features**](guide.md#features)"},
      {"<a href='guide.md#features'><div><h3>Features</h3><p>Body</p></div></a>",
       "### [Features](guide.md#features)\n\n[Body](guide.md#features)"},
      {"<a href='guide.md'><ul><li>Feature</li><li>Motion</li></ul></a>",
       "- [Feature](guide.md)\n- [Motion](guide.md)"},
      {"<strong><p>First</p><p>Second</p></strong>", "**First**\n\n**Second**"},
      {"<a href='guide.md'><table><tr><td>First</td><td>Last</td></tr></table></a>",
       "| [First](guide.md) | [Last](guide.md) |\n| --- | --- |"},
      {"<a href='guide.md'><pre><code> one\n\n two\n</code></pre></a>", "```\n one\n\n two\n```"},
      {"<p><a href='guide.md'> <b>Guide</b> </a>text</p>", "[**Guide**](guide.md) text"},
      {"<p><a href='guide.md'><img src='icon.png' alt=''></a></p>", "[![](icon.png)](guide.md)"},
      {"<p><a href='guide.md'><img alt='Guide'></a></p>", "[Guide](guide.md)"},
      {"<p><span aria-hidden='true'>Visible</span> text</p>", "Visible text"},
      {"<p><span style='font-weight:7000'>normal</span> <span style='font-weight:700'>bold</span></p>",
       "normal **bold**"},
      {"<p><span style='font-weight:700; font-weight:normal'>normal</span></p>", "normal"},
      {"<table><tr><td>First</td><td></td></tr><tr><td></td><td>Last</td></tr></table>",
       "| First |  |\n| --- | --- |\n|  | Last |"},
      {"<table><tr><td></td><td></td></tr><tr><td>First</td><td>Last</td></tr></table>",
       "|  |  |\n| --- | --- |\n| First | Last |"},
      {"<table><thead><tr><th></th><th></th></tr></thead><tbody><tr><td>First</td><td>Last</td></tr></tbody></table>",
       "|  |  |\n| --- | --- |\n| First | Last |"},
      {"<pre><code>\xe2\x80\x8b\n</code></pre>", "```\n\xe2\x80\x8b\n```"},
  };
  for (const auto& test : cases) {
    const auto result = importHtmlMarkdown(QString::fromUtf8(test.html));
    require(result == QString::fromUtf8(test.markdown),
            QStringLiteral("HTML content projection mismatch:\nHTML: %1\nExpected: %2\nActual: %3")
                .arg(QString::fromUtf8(test.html), QString::fromUtf8(test.markdown), result));
  }
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  QTemporaryDir settingsDirectory;
  require(settingsDirectory.isValid(), "temporary clipboard test settings");
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinClipboardTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testClearFormatting);
  RUN_TEST(testClearFormattingUndoRedo);
  RUN_TEST(testStylizeCollapsedSkeletons);
  RUN_TEST(testTypingIntoCollapsedStyleSkeletons);
  RUN_TEST(testStylizeSelectionWrap);
  RUN_TEST(testStylizeUndoRedoTextDelta);
  RUN_TEST(testHeadingAndListItemStylize);
  RUN_TEST(testStylizeCrossParagraphSelectionWrap);
  RUN_TEST(testClipboardPlainText);
  RUN_TEST(testBrowserHeadingClipboard);
  RUN_TEST(testRichClipboardInterop);
  RUN_TEST(testHtmlImportStructure);
  RUN_TEST(testClipboardMarkdownPartialBlocks);
  RUN_TEST(testClipboardCrossMarkdownPartialEdges);
  RUN_TEST(testClipboardBlockCoverageAcrossFormats);
  RUN_TEST(testClipboardPartialEdgeStructures);
  RUN_TEST(testClipboardHeadingFragmentCutPasteUndo);
  RUN_TEST(testSelectionSerializerFormats);
  RUN_TEST(testSelectionSerializerCrossComplexInlineEdges);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
