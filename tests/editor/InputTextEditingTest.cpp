#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "projection/InlineProjection.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include "EditorTestUtils.h"

#include <QApplication>

#include <iostream>

using namespace muffin;

// testInputInsertAndBackspace (lines 216-235)
void testInputInsertAndBackspace() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.insertText(QStringLiteral("!")), "insert text should edit paragraph");
  require(session.markdownText() == QStringLiteral("alpha!"), "insert text result mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "insert text cursor mismatch");

  require(input.deleteBackward(), "backspace should edit paragraph");
  require(session.markdownText() == QStringLiteral("alpha"), "backspace result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace cursor mismatch");
  require(undoStack.canUndo(), "input should push undo transaction");
}

// testInputEnterMovesCursorToNewParagraph (lines 237-264)
void testInputEnterMovesCursorToNewParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  QVector<BrushQueue::RefreshRequest> requests;
  QObject::connect(&brushQueue, &BrushQueue::refreshRequested, [&requests](BrushQueue::RefreshRequest request) {
    requests.push_back(std::move(request));
  });
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha beta"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.insertParagraphBreak(), "enter should split paragraph");
  require(session.markdownText() == QStringLiteral("alpha\n\nbeta"), "enter split text mismatch");
  require(session.document().root().children().size() == 2, "enter should create two paragraphs");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "enter cursor should move to new paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "enter cursor offset should be start of new paragraph");
  brushQueue.flush();
  require(requests.size() == 1, "enter split should request one refresh");
  require(!requests.last().fullLayoutDirty, "enter split should not request full layout refresh");
  require(requests.last().topLevelRangeDirty.isValid(), "enter split should request top-level range refresh");
  require(requests.last().topLevelRangeDirty.first == 0, "enter split range first mismatch");
  require(requests.last().topLevelRangeDirty.oldCount == 1, "enter split range old count mismatch");
  require(requests.last().topLevelRangeDirty.newCount == 2, "enter split range new count mismatch");
}

// testInputEnterSplitsComplexInlineParagraphs (lines 266-315)
void testInputEnterSplitsComplexInlineParagraphs() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  setCursor(selection, blockAt(session, 0), 11);
  require(input.insertParagraphBreak(), "enter should split strong inline paragraph");
  require(session.markdownText() == QStringLiteral("before **bold**\n\nafter"), "strong inline split mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "strong inline split cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "strong inline split cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before `code` after"), false);
  setCursor(selection, blockAt(session, 0), 11);
  require(input.insertParagraphBreak(), "enter should split code inline paragraph");
  require(session.markdownText() == QStringLiteral("before `code`\n\nafter"), "code inline split mismatch");

  session.setMarkdownText(QStringLiteral("before $x+y$ after"), false);
  setCursor(selection, blockAt(session, 0), 10);
  require(input.insertParagraphBreak(), "enter should split inline math paragraph");
  require(session.markdownText() == QStringLiteral("before $x+y$\n\nafter"), "inline math split mismatch");

  session.setMarkdownText(QStringLiteral("Plain text can mix **bold**"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("Plain text can mix b").size(), QStringLiteral("Plain text can mix **b").size());
  require(input.insertParagraphBreak(), "enter inside strong inline should split wrapper");
  require(session.markdownText() == QStringLiteral("Plain text can mix **b**\n\n**old**"), "strong inline wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("Plain text can mix *italic*"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("Plain text can mix ita").size(), QStringLiteral("Plain text can mix *ita").size());
  require(input.insertParagraphBreak(), "enter inside emphasis inline should split wrapper");
  require(session.markdownText() == QStringLiteral("Plain text can mix *ita*\n\n*lic*"), "emphasis inline wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("Plain text can mix ~~through~~"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("Plain text can mix thr").size(), QStringLiteral("Plain text can mix ~~thr").size());
  require(input.insertParagraphBreak(), "enter inside strike inline should split wrapper");
  require(session.markdownText() == QStringLiteral("Plain text can mix ~~thr~~\n\n~~ough~~"), "strike inline wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("before `code` after"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("before co").size(), QStringLiteral("before `co").size());
  require(input.insertParagraphBreak(), "enter inside code inline should split wrapper");
  require(session.markdownText() == QStringLiteral("before `co`\n\n`de` after"), "code inline wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("before $x+y$ after"), false);
  setSourceCursor(selection, blockAt(session, 0), QStringLiteral("before x").size(), QStringLiteral("before $x").size());
  require(input.insertParagraphBreak(), "enter inside math inline should split wrapper");
  require(session.markdownText() == QStringLiteral("before $x$\n\n$+y$ after"), "math inline wrapper split mismatch");
}

// testInputEditsComplexInlineSourcePositions (lines 317-353)
void testInputEditsComplexInlineSourcePositions() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  setSourceCursor(selection, blockAt(session, 0), 9, 11);
  require(input.insertText(QStringLiteral("X")), "typing inside strong inline should edit markdown source");
  require(session.markdownText() == QStringLiteral("before **boXld** after"), "strong inline source insert mismatch");
  require(selection.cursorPosition().text.sourceOffset == 12, "strong inline source cursor mismatch");

  session.setMarkdownText(QStringLiteral("before `code` after"), false);
  setSourceCursor(selection, blockAt(session, 0), 9, 9);
  require(input.insertText(QStringLiteral("X")), "typing inside code inline should edit markdown source");
  require(session.markdownText() == QStringLiteral("before `cXode` after"), "code inline source insert mismatch");

  session.setMarkdownText(QStringLiteral("before $x+y$ after"), false);
  setSourceCursor(selection, blockAt(session, 0), 9, 9);
  require(input.insertText(QStringLiteral("0")), "typing inside inline math should edit markdown source");
  require(session.markdownText() == QStringLiteral("before $x0+y$ after"), "inline math source insert mismatch");
  require(selection.cursorPosition().text.sourceOffset == QStringLiteral("before $x0").size(), "inline math source cursor should stay after inserted text");
  require(input.insertText(QStringLiteral("1")), "consecutive typing inside inline math should keep source cursor");
  require(session.markdownText() == QStringLiteral("before $x01+y$ after"), "consecutive inline math insert should not drift");

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  setSourceCursor(selection, blockAt(session, 0), 10, 12);
  require(input.deleteBackward(), "backspace inside strong inline should edit markdown source");
  require(session.markdownText() == QStringLiteral("before **bod** after"), "strong inline source backspace mismatch");

  session.setMarkdownText(QStringLiteral("before `code` after"), false);
  setSourceCursor(selection, blockAt(session, 0), 9, 9);
  require(input.deleteForward(), "delete inside code inline should edit markdown source");
  require(session.markdownText() == QStringLiteral("before `cde` after"), "code inline source delete mismatch");
}

// testSecondInlineMathHitMapsToItsSourceOffset (lines 355-376)
void testSecondInlineMathHitMapsToItsSourceOffset() {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("行内公式使用单美元符号：")));
  inlines.push_back(InlineNode::inlineMath(QStringLiteral("E = mc^2")));
  inlines.push_back(InlineNode::text(QStringLiteral("  和 ")));
  inlines.push_back(InlineNode::inlineMath(QStringLiteral("a_1 + b_1 = c_1")));
  inlines.push_back(InlineNode::text(QStringLiteral("。")));

  const QString source = QStringLiteral("行内公式使用单美元符号：$E = mc^2$  和 $a_1 + b_1 = c_1$。");
  InlineLayout layout;
  layout.build(inlines, source, RenderTheme::defaultTheme(), 900.0, RenderTheme::defaultTheme().paragraphFont(), InlineLayout::BuildOptions{});

  const qsizetype secondMathSourceStart = source.indexOf(QStringLiteral("$a_1"));
  const qsizetype afterBSourceOffset = source.indexOf(QStringLiteral("b_1")) + 1;
  const QRectF secondStartCursor = layout.cursorRectForSourceOffset(secondMathSourceStart + 1);
  const QRectF secondEndCursor = layout.cursorRectForSourceOffset(source.indexOf(QStringLiteral("$。")));
  const qreal ratio = static_cast<qreal>(afterBSourceOffset - (secondMathSourceStart + 1)) / QStringLiteral("a_1 + b_1 = c_1").size();
  const QPointF hitPoint(secondStartCursor.left() + (secondEndCursor.left() - secondStartCursor.left()) * ratio, secondStartCursor.center().y());

  const qsizetype hitSourceOffset = layout.hitTestSourceOffset(hitPoint);
  require(qAbs(hitSourceOffset - afterBSourceOffset) <= 1, QStringLiteral("second inline math hit source mismatch: %1 vs %2").arg(hitSourceOffset).arg(afterBSourceOffset));
}

// testSecondInlineMathHitInsertAfterEquals (lines 378-410)
void testSecondInlineMathHitInsertAfterEquals() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  const QString source = QStringLiteral("Inline math uses single dollar delimiters: $E = mc^2$ and $a_1 + b_1 = c_1$.");
  session.setMarkdownText(source, false);
  MarkdownNode* block = blockAt(session, 0);

  InlineLayout layout;
  layout.build(block->inlines(), source, RenderTheme::defaultTheme(), 900.0, RenderTheme::defaultTheme().paragraphFont(), InlineLayout::BuildOptions{});
  require(layout.mathAtomCount() == 2, "two inactive inline math runs should collapse");

  const QString secondMath = QStringLiteral("a_1 + b_1 = c_1");
  const qsizetype secondMathSourceStart = source.indexOf(QStringLiteral("$a_1"));
  const qsizetype equalsSourceOffset = source.indexOf(QStringLiteral("= c_1"));
  const qsizetype insertSourceOffset = equalsSourceOffset + 1;
  const QRectF secondStartCursor = layout.cursorRectForSourceOffset(secondMathSourceStart + 1);
  const QRectF secondEndCursor = layout.cursorRectForSourceOffset(source.indexOf(QStringLiteral("$.")));
  const qreal ratio = static_cast<qreal>(insertSourceOffset - (secondMathSourceStart + 1)) / secondMath.size();
  const QPointF hitPoint(secondStartCursor.left() + (secondEndCursor.left() - secondStartCursor.left()) * ratio, secondStartCursor.center().y());
  const qsizetype hitSourceOffset = layout.hitTestSourceOffset(hitPoint);
  require(qAbs(hitSourceOffset - insertSourceOffset) <= 1,
          QStringLiteral("second inline math equals hit source mismatch: %1 vs %2").arg(hitSourceOffset).arg(insertSourceOffset));

  setSourceCursor(selection, block, QStringLiteral("Inline math uses single dollar delimiters: E = mc^2 and a_1 + b_1 =").size(), hitSourceOffset);
  require(input.insertText(QStringLiteral("d")), "typing after second inline math equals should edit source");
  require(session.markdownText() == QStringLiteral("Inline math uses single dollar delimiters: $E = mc^2$ and $a_1 + b_1 =d c_1$."),
          "second inline math insert should stay after equals");
}

// testCodeAndMathCursorAfterSourceInsert (lines 412-433)
void testCodeAndMathCursorAfterSourceInsert() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("$123$"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 1);
  require(input.insertText(QStringLiteral("a")), "typing before first math char should work");
  require(session.markdownText() == QStringLiteral("$a123$"), "math insert before first char mismatch");
  require(selection.cursorPosition().text.sourceOffset == 2, "math cursor source should stay after inserted char");
  require(selection.cursorPosition().text.textOffset == 1, "math cursor visible offset should stay after inserted char");

  session.setMarkdownText(QStringLiteral("`123`"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 1);
  require(input.insertText(QStringLiteral("a")), "typing before first code char should work");
  require(session.markdownText() == QStringLiteral("`a123`"), "code insert before first char mismatch");
  require(selection.cursorPosition().text.sourceOffset == 2, "code cursor source should stay after inserted char");
  require(selection.cursorPosition().text.textOffset == 1, "code cursor visible offset should stay after inserted char");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInputInsertAndBackspace);
  RUN_TEST(testInputEnterMovesCursorToNewParagraph);
  RUN_TEST(testInputEnterSplitsComplexInlineParagraphs);
  RUN_TEST(testInputEditsComplexInlineSourcePositions);
  RUN_TEST(testSecondInlineMathHitMapsToItsSourceOffset);
  RUN_TEST(testSecondInlineMathHitInsertAfterEquals);
  RUN_TEST(testCodeAndMathCursorAfterSourceInsert);
#undef RUN_TEST
  return 0;
}
