#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "parser/CmarkGfmParser.h"

#include "EditorTestUtils.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

// Regression guard for the "type `>` to start a blockquote and the caret vanishes" bug.
//
// cmark emits a *childless* BlockQuote for a `>` / `> ` line that has no following content. A
// blockquote is only editable through its child paragraph (unlike a ListItem, which is itself
// editable), so an empty blockquote left the caret nowhere to land: typing `>` produced an invalid
// cursor (caret disappeared) and the next keystroke was rejected because resolver.current() could
// not resolve an editable block. The fix demotes childless blockquotes back to a Paragraph at parse
// time (mirroring the lone-list-marker demotion), so the opener stays editable while empty and
// materialises into a real blockquote once content follows (`> text`).

const MarkdownNode* firstChild(const MarkdownNode& node) {
  const auto& kids = node.children();
  return kids.empty() ? nullptr : kids.front().get();
}

void testEmptyBlockquoteDemotedToParagraphOnParse() {
  CmarkGfmParser parser;
  ParseOptions options;
  // A lone `>` / `> ` must NOT parse as a childless BlockQuote; it is folded to a Paragraph so the
  // caret always has an editable block to sit in.
  for (const QString& md : {QStringLiteral(">"), QStringLiteral("> "), QStringLiteral(">\n")}) {
    ParseResult result = parser.parseDocument(md, options);
    require(result.root != nullptr, "parse should produce a root");
    require(result.root->children().size() == 1, "lone '>' line should be a single top-level block");
    const MarkdownNode* block = firstChild(*result.root);
    require(block != nullptr, "top-level block should exist");
    require(block->type() == BlockType::Paragraph,
            "empty blockquote opener must demote to a Paragraph (not a childless BlockQuote)");
  }
}

void testBlockquoteWithContentIsNotDemoted() {
  CmarkGfmParser parser;
  ParseOptions options;
  ParseResult result = parser.parseDocument(QStringLiteral("> text"), options);
  const MarkdownNode* quote = firstChild(*result.root);
  require(quote != nullptr && quote->type() == BlockType::BlockQuote, "real blockquote must survive");
  const MarkdownNode* inner = firstChild(*quote);
  require(inner != nullptr && inner->type() == BlockType::Paragraph,
          "blockquote with content must keep its child paragraph");
}

void testTypeBlockquoteOpenerFromEmptyDocumentKeepsCaretValid() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, nullptr, {});
  require(session.markdownText().isEmpty(), "start empty");

  require(input.insertText(QStringLiteral(">")), "insert '>' should succeed");
  require(selection.cursorPosition().blockId.isValid(), "caret must remain valid after typing '>'");

  require(input.insertText(QStringLiteral(" ")), "insert space should succeed");
  require(selection.cursorPosition().blockId.isValid(), "caret must remain valid after typing '> '");

  // Real content now follows: the quote must materialise and the caret must land inside the child
  // paragraph (not on the container and not lost).
  require(input.insertText(QStringLiteral("H")), "insert 'H' should succeed");
  require(session.markdownText() == QStringLiteral("> H"), "text should be '> H'");
  const MarkdownNode* quote = firstChild(session.document().root());
  require(quote != nullptr && quote->type() == BlockType::BlockQuote, "should now be a real blockquote");
  const MarkdownNode* inner = firstChild(*quote);
  require(inner != nullptr && inner->type() == BlockType::Paragraph, "blockquote should have a child paragraph");
  require(selection.cursorPosition().blockId == inner->id(),
          "caret must land inside the blockquote's child paragraph");
  require(selection.cursorPosition().text.textOffset == 1, "caret must sit after the typed 'H'");
}

void testTypeBlockquoteOpenerAtParagraphStartKeepsCaretValid() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, nullptr, {});

  session.setMarkdownText(QStringLiteral("hello"), false);
  setCursor(selection, blockAt(session, 0), 0);

  require(input.insertText(QStringLiteral(">")), "insert '>' at paragraph start should succeed");
  // ">hello" is a real blockquote (cmark accepts '>' without a space); caret must be inside the child.
  const MarkdownNode* quote = firstChild(session.document().root());
  require(quote != nullptr && quote->type() == BlockType::BlockQuote, "'>hello' should be a blockquote");
  const MarkdownNode* inner = firstChild(*quote);
  require(inner != nullptr, "blockquote must have a child paragraph");
  require(selection.cursorPosition().blockId.isValid(), "caret must remain valid");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testEmptyBlockquoteDemotedToParagraphOnParse);
  RUN_TEST(testBlockquoteWithContentIsNotDemoted);
  RUN_TEST(testTypeBlockquoteOpenerFromEmptyDocumentKeepsCaretValid);
  RUN_TEST(testTypeBlockquoteOpenerAtParagraphStartKeepsCaretValid);
#undef RUN_TEST
  return 0;
}
