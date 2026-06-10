#include "document/DocumentSession.h"
#include "commands/ParagraphController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"

#include "ParagraphTestUtils.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

void testToggleCodeBlockSplitMiddle() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.toggleCodeBlock(), "split code block at middle should succeed");

  require(session.markdownText() == QStringLiteral("Hello\n\n```\n\n```\n\nworld"),
          "split middle text mismatch");

  require(session.document().root().children().size() >= 3, "should have at least 3 blocks");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first block should be paragraph");
  require(blockAt(session, 1)->type() == BlockType::CodeFence, "second block should be code fence");
  require(blockAt(session, 2)->type() == BlockType::Paragraph, "third block should be paragraph");
}

void testToggleCodeBlockSplitAtStart() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(paragraph.toggleCodeBlock(), "split code block at start should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\n\n```")), "should contain empty code fence");
  require(md.contains(QLatin1String("Hello")), "text should be preserved after code fence");
  bool foundCodeFence = false;
  for (const auto& child : session.document().root().children()) {
    if (child->type() == BlockType::CodeFence) { foundCodeFence = true; break; }
  }
  require(foundCodeFence, "document should contain a CodeFence block");
}

void testToggleCodeBlockSplitAtEnd() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.toggleCodeBlock(), "split code block at end should succeed");

  require(session.markdownText() == QStringLiteral("Hello\n\n```\n\n```\n\n"),
          "split at end text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first block should be paragraph");
  require(blockAt(session, 1)->type() == BlockType::CodeFence, "second block should be code fence");
}

void testToggleCodeBlockHeadingSplit() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("## Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 8);
  require(paragraph.toggleCodeBlock(), "heading split should succeed");

  require(session.markdownText() == QStringLiteral("## Hello\n\n```\n\n```\n\n## world"),
          "heading split text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Heading, "first should be heading");
  require(blockAt(session, 0)->headingLevel() == 2, "first heading level should be 2");
  require(blockAt(session, 1)->type() == BlockType::CodeFence, "second should be code fence");
  require(blockAt(session, 2)->type() == BlockType::Heading, "third should be heading");
  require(blockAt(session, 2)->headingLevel() == 2, "third heading level should be 2");
}

void testToggleCodeBlockInlineBold() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("before **bold text** after"), false);
  setSourceCursor(selection, blockAt(session, 0), 13, 13);
  require(paragraph.toggleCodeBlock(), "inline bold split should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\n\n```")), "should contain empty code fence");
  require(md.startsWith(QLatin1String("before **bold**")), "bold should be closed in first paragraph");
  require(md.contains(QLatin1String("**text** after")), "bold should be reopened in last paragraph");
}

void testToggleCodeBlockWithSelection() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceSelection(selection, blockAt(session, 0), 3, 3, 8, 8);
  require(paragraph.toggleCodeBlock(), "selection wrap in code block should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\nlo wo\n```")), "selected text should be in code block");
  require(md.startsWith(QLatin1String("Hel")), "before text should be paragraph A");
  require(md.contains(QLatin1String("rld")), "after text should be paragraph B");
}

void testToggleCodeBlockSelectionHeading() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("## Hello world"), false);
  setSourceSelection(selection, blockAt(session, 0), 3, 6, 8, 11);
  require(paragraph.toggleCodeBlock(), "heading selection wrap should succeed");

  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\nlo wo\n```")), "selected text in code block");
  require(md.contains(QLatin1String("## Hel")), "before part keeps heading prefix");
  require(md.contains(QLatin1String("## rld")), "after part gets heading prefix");
}

void testToggleCodeBlockConvertsBack() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("```\ncode here\n```"), false);
  require(blockAt(session, 0)->type() == BlockType::CodeFence, "should start as code fence");

  setCursor(selection, blockAt(session, 0), 4);
  require(paragraph.toggleCodeBlock(), "code -> paragraph should succeed");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "should become paragraph");
  require(session.markdownText() == QStringLiteral("code here"), "code -> paragraph text mismatch");
}

void testToggleCodeBlockEmptyConvertsBack() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("```\n```"), false);
  require(blockAt(session, 0)->type() == BlockType::CodeFence, "should start as empty code fence");

  setCursor(selection, blockAt(session, 0), 0);
  require(paragraph.toggleCodeBlock(), "empty code -> paragraph should succeed");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "should become paragraph");
}

void testToggleCodeBlockMathToCode() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("$$\nx=1\n$$"), false);
  require(blockAt(session, 0)->type() == BlockType::MathBlock, "should start as math block");

  setCursor(selection, blockAt(session, 0), 2);
  require(paragraph.toggleCodeBlock(), "math -> code should succeed");

  require(blockAt(session, 0)->type() == BlockType::CodeFence, "should become code fence");
  const QString md = session.markdownText();
  require(md.contains(QLatin1String("```\nx=1\n```")), "math -> code text mismatch");
}

void testToggleFormulaBlockSplitMiddle() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("Hello world"), false);
  setSourceCursor(selection, blockAt(session, 0), 5, 5);
  require(paragraph.toggleFormulaBlock(), "formula split at middle should succeed");

  require(session.markdownText() == QStringLiteral("Hello\n\n$$\n\n$$\n\nworld"),
          "formula split text mismatch");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "first should be paragraph");
  require(blockAt(session, 1)->type() == BlockType::MathBlock, "second should be math block");
  require(blockAt(session, 2)->type() == BlockType::Paragraph, "third should be paragraph");
}

void testToggleFormulaBlockConvertsBack() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("$$\nx^2\n$$"), false);
  require(blockAt(session, 0)->type() == BlockType::MathBlock, "should start as math block");

  setCursor(selection, blockAt(session, 0), 2);
  require(paragraph.toggleFormulaBlock(), "math -> paragraph should succeed");

  require(blockAt(session, 0)->type() == BlockType::Paragraph, "should become paragraph");
  require(session.markdownText() == QStringLiteral("x^2"), "math -> paragraph text mismatch");
}

void testToggleFormulaBlockCodeToMath() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  ParagraphController paragraph;
  wireParagraph(paragraph, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("```\ncode\n```"), false);
  require(blockAt(session, 0)->type() == BlockType::CodeFence, "should start as code fence");

  setCursor(selection, blockAt(session, 0), 2);
  require(paragraph.toggleFormulaBlock(), "code -> math should succeed");

  require(blockAt(session, 0)->type() == BlockType::MathBlock, "should become math block");
  const QString md = session.markdownText();
  require(md.contains(QLatin1String("$$\ncode\n$$")), "code -> math text mismatch");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testToggleCodeBlockSplitMiddle);
  RUN_TEST(testToggleCodeBlockSplitAtStart);
  RUN_TEST(testToggleCodeBlockSplitAtEnd);
  RUN_TEST(testToggleCodeBlockHeadingSplit);
  RUN_TEST(testToggleCodeBlockInlineBold);
  RUN_TEST(testToggleCodeBlockWithSelection);
  RUN_TEST(testToggleCodeBlockSelectionHeading);
  RUN_TEST(testToggleCodeBlockConvertsBack);
  RUN_TEST(testToggleCodeBlockEmptyConvertsBack);
  RUN_TEST(testToggleCodeBlockMathToCode);
  RUN_TEST(testToggleFormulaBlockSplitMiddle);
  RUN_TEST(testToggleFormulaBlockConvertsBack);
  RUN_TEST(testToggleFormulaBlockCodeToMath);
#undef RUN_TEST
  return 0;
}
