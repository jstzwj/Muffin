#include "document/DocumentSession.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/literal/LiteralBlockController.h"
#include "projection/InlineProjection.h"
#include "document/MarkdownNode.h"
#include "projection/SelectionSerializer.h"
#include "edit/UndoStack.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/ClipboardController.h"
#include "editor/EditorContext.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "editor/SourceEditorWidget.h"
#include "editor/TextBlockCommandBuilder.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cstdlib>
#include <iostream>
#include <variant>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

void require(bool condition, const QString& message) {
  if (!condition) {
    std::cerr << message.toStdString() << "\n";
    std::exit(1);
  }
}

void runTest(const char* name, void (*test)()) {
  std::cerr << "RUN " << name << "\n";
  test();
}

EditTransaction requireTextDeltaCommand(UndoStack& stack, const char* message) {
  require(stack.canUndo(), "expected undo command");
  EditTransaction transaction = stack.takeUndo();
  require(transaction.isTextDeltaCommand(), message);
  require(transaction.textDeltaCommand().isValid(), "text delta command should be valid");
  return transaction;
}

MarkdownNode* blockAt(const DocumentSession& session, qsizetype index) {
  const auto& children = session.document().root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "block index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

MarkdownNode* childAt(MarkdownNode* node, qsizetype index) {
  require(node != nullptr, "parent node should exist");
  const auto& children = node->children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "child index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

MarkdownNode* listItemAt(const DocumentSession& session, qsizetype listIndex, qsizetype itemIndex) {
  return childAt(blockAt(session, listIndex), itemIndex);
}

MarkdownNode* firstChildOfType(MarkdownNode* node, BlockType type) {
  require(node != nullptr, "parent node should exist");
  for (const auto& child : node->children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  require(false, "child type not found");
  return nullptr;
}

MarkdownNode* maybeFirstChildOfType(MarkdownNode* node, BlockType type) {
  if (!node) {
    return nullptr;
  }
  for (const auto& child : node->children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  return nullptr;
}

MarkdownNode* firstBlockOfType(const DocumentSession& session, BlockType type) {
  for (const auto& child : session.document().root().children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  require(false, "block type not found");
  return nullptr;
}

int listDepthForItem(const MarkdownNode* item) {
  int depth = 0;
  const MarkdownNode* node = item;
  while (node) {
    if (node->type() == BlockType::ListItem) {
      ++depth;
    }
    node = node->parent();
  }
  return depth;
}

void setCursor(SelectionController& selection, MarkdownNode* block, qsizetype offset) {
  CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = offset;
  selection.setCursorPosition(cursor);
}

void setSourceCursor(SelectionController& selection, MarkdownNode* block, qsizetype visibleOffset, qsizetype sourceOffset) {
  CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = visibleOffset;
  cursor.text.sourceOffset = sourceOffset;
  selection.setCursorPosition(cursor);
}

bool pressKey(InputController& input, QObject* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  return input.eventFilter(target, &event);
}

void wireInput(
    InputController& input,
    DocumentSession& session,
    SelectionController& selection,
    UndoStack& undoStack,
    BrushQueue& brushQueue,
    EditorView* view = nullptr,
    QHash<int, LiteralBlockController*> literalEditors = {}) {
  input.setContext({&session, &selection, &undoStack, &brushQueue, view, literalEditors});
}

void setSelection(SelectionController& selection, MarkdownNode* block, qsizetype anchor, qsizetype focus) {
  SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = block->id();
  range.anchor.text.textOffset = anchor;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = block->id();
  range.focus.text.textOffset = focus;
  selection.setSelection(range);
}

void setSourceSelection(
    SelectionController& selection,
    MarkdownNode* block,
    MarkdownNode* textNode,
    qsizetype anchorTextOffset,
    qsizetype anchorSourceOffset,
    qsizetype focusTextOffset,
    qsizetype focusSourceOffset) {
  SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = textNode->id();
  range.anchor.text.textOffset = anchorTextOffset;
  range.anchor.text.sourceOffset = anchorSourceOffset;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = textNode->id();
  range.focus.text.textOffset = focusTextOffset;
  range.focus.text.sourceOffset = focusSourceOffset;
  selection.setSelection(range);
}

void setCrossSelection(
    SelectionController& selection,
    MarkdownNode* anchorBlock,
    qsizetype anchorOffset,
    MarkdownNode* focusBlock,
    qsizetype focusOffset) {
  SelectionRange range;
  range.anchor.blockId = anchorBlock->id();
  range.anchor.text.nodeId = anchorBlock->id();
  range.anchor.text.textOffset = anchorOffset;
  range.focus.blockId = focusBlock->id();
  range.focus.text.nodeId = focusBlock->id();
  range.focus.text.textOffset = focusOffset;
  selection.setSelection(range);
}

QString selectedMarkdown(const DocumentSession& session, const SelectionController& selection) {
  return SelectionSerializer().exportMarkdown(session.document(), selection.selection());
}

QString selectedPlainText(const DocumentSession& session, const SelectionController& selection) {
  return SelectionSerializer().exportPlainText(session.document(), selection.selection());
}

void wireClipboard(ClipboardController& clipboard, DocumentSession& session, SelectionController& selection, InputController& input) {
  clipboard.setContext({&session, &selection, nullptr, nullptr});
  clipboard.setInputController(&input);
}

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

void testSecondInlineMathHitMapsToItsSourceOffset() {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("行内公式使用单美元符号：")));
  inlines.push_back(InlineNode::inlineMath(QStringLiteral("E = mc^2")));
  inlines.push_back(InlineNode::text(QStringLiteral("  和 ")));
  inlines.push_back(InlineNode::inlineMath(QStringLiteral("a_1 + b_1 = c_1")));
  inlines.push_back(InlineNode::text(QStringLiteral("。")));

  const QString source = QStringLiteral("行内公式使用单美元符号：$E = mc^2$  和 $a_1 + b_1 = c_1$。");
  InlineLayout layout;
  layout.build(inlines, source, RenderTheme::typoraLike(), 900.0, RenderTheme::typoraLike().paragraphFont(), InlineLayout::BuildOptions{});

  const qsizetype secondMathSourceStart = source.indexOf(QStringLiteral("$a_1"));
  const qsizetype afterBSourceOffset = source.indexOf(QStringLiteral("b_1")) + 1;
  const QRectF secondStartCursor = layout.cursorRectForSourceOffset(secondMathSourceStart + 1);
  const QRectF secondEndCursor = layout.cursorRectForSourceOffset(source.indexOf(QStringLiteral("$。")));
  const qreal ratio = static_cast<qreal>(afterBSourceOffset - (secondMathSourceStart + 1)) / QStringLiteral("a_1 + b_1 = c_1").size();
  const QPointF hitPoint(secondStartCursor.left() + (secondEndCursor.left() - secondStartCursor.left()) * ratio, secondStartCursor.center().y());

  const qsizetype hitSourceOffset = layout.hitTestSourceOffset(hitPoint);
  require(qAbs(hitSourceOffset - afterBSourceOffset) <= 1, QStringLiteral("second inline math hit source mismatch: %1 vs %2").arg(hitSourceOffset).arg(afterBSourceOffset));
}

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
  layout.build(block->inlines(), source, RenderTheme::typoraLike(), 900.0, RenderTheme::typoraLike().paragraphFont(), InlineLayout::BuildOptions{});
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

void testInputEnterAtParagraphEdgesCreatesEditableEmptyParagraph() {
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

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  const NodeId originalAlphaId = blockAt(session, 0)->id();
  require(input.insertParagraphBreak(), "enter at paragraph start should create empty paragraph");
  require(session.markdownText() == QStringLiteral("\n\nalpha"), "paragraph start enter text mismatch");
  require(session.document().root().children().size() == 2, "paragraph start enter should create virtual empty block");
  require(session.lastLocalEditChangedTopLevelStructure(), "paragraph start enter should mark top-level structure changed");
  brushQueue.flush();
  require(requests.size() == 1, "paragraph start enter should request one refresh");
  require(!requests.last().fullLayoutDirty, "paragraph start enter should not require full layout refresh");
  require(requests.last().topLevelRangeDirty.isValid(), "paragraph start enter should request top-level range refresh");
  require(requests.last().topLevelRangeDirty.first == 0, "paragraph start enter range first mismatch");
  require(requests.last().topLevelRangeDirty.oldCount == 1, "paragraph start enter range old count mismatch");
  require(requests.last().topLevelRangeDirty.newCount == 2, "paragraph start enter range new count mismatch");
  require(requests.last().layoutDirtyBlocks.isEmpty(), "paragraph start enter range refresh should not keep block dirty ids");
  require(blockAt(session, 1)->id() == originalAlphaId, "paragraph start enter should preserve original paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "paragraph start enter cursor should stay on original paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "paragraph start enter cursor offset mismatch");
  require(input.insertText(QStringLiteral("x")), "typing after paragraph start enter should edit original paragraph");
  require(session.markdownText() == QStringLiteral("\n\nxalpha"), "typing after paragraph start enter should insert at original paragraph start");
  require(!session.lastLocalEditChangedTopLevelStructure(), "typing after paragraph start enter should keep original paragraph identity stable");
  brushQueue.flush();
  require(requests.size() == 2, "typing after paragraph start enter should request another refresh");
  require(!requests.last().fullLayoutDirty, "typing after paragraph start enter should request block refresh");
  require(requests.last().layoutDirtyBlocks.size() == 1, "typing after paragraph start enter should dirty one block");
  require(requests.last().layoutDirtyBlocks.contains(blockAt(session, 1)->id()), "typing after paragraph start enter should dirty original paragraph");
  require(blockAt(session, 1)->id() == originalAlphaId, "typing after paragraph start enter should keep original paragraph id");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  const NodeId repeatedAlphaId = blockAt(session, 0)->id();
  require(input.insertParagraphBreak(), "enter at paragraph start should create empty paragraph for repeat case");
  require(input.insertParagraphBreak(), "repeated enter in leading empty paragraph should create another empty paragraph");
  require(input.insertParagraphBreak(), "third enter in leading empty paragraph should create another empty paragraph");
  require(session.markdownText() == QStringLiteral("\n\n\n\n\n\nalpha"), "leading empty paragraph repeated enter text mismatch");
  require(session.document().root().children().size() == 4, "leading empty paragraph repeated enter should create virtual blocks");
  require(blockAt(session, 3)->id() == repeatedAlphaId, "repeated paragraph start enter should keep original paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 3)->id(), "repeated paragraph start enter cursor should stay on original paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "repeated paragraph start enter cursor offset mismatch");
  require(input.insertText(QStringLiteral("123")), "typing after repeated paragraph start enter should edit original paragraph");
  require(session.markdownText() == QStringLiteral("\n\n\n\n\n\n123alpha"), "typing after repeated paragraph start enter text mismatch");
  require(session.document().root().children().size() == 4, "typing after repeated paragraph start enter should preserve virtual paragraph count");
  require(blockAt(session, 3)->id() == repeatedAlphaId, "typing after repeated paragraph start enter should keep original paragraph once");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertParagraphBreak(), "enter at paragraph end should create empty paragraph");
  require(session.markdownText() == QStringLiteral("alpha\n\n"), "paragraph end enter text mismatch");
  require(session.document().root().children().size() == 2, "paragraph end enter should create virtual empty block");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "paragraph end enter cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "paragraph end enter cursor offset mismatch");
  require(input.insertParagraphBreak(), "enter in trailing empty paragraph should create another empty paragraph");
  require(session.markdownText() == QStringLiteral("alpha\n\n\n\n"), "trailing empty paragraph repeated enter text mismatch");
  require(session.document().root().children().size() == 3, "trailing empty paragraph repeated enter should create another virtual block");
  require(selection.cursorPosition().blockId == blockAt(session, 2)->id(), "trailing empty paragraph repeated enter cursor block mismatch");
  require(input.insertText(QStringLiteral("after")), "typing into trailing empty paragraph should work");
  require(session.markdownText() == QStringLiteral("alpha\n\n\n\nafter"), "typing into trailing empty paragraph text mismatch");

  session.setMarkdownText(QString(12, QLatin1Char('\n')), false);
  const qsizetype emptyRunCount = session.document().root().children().size();
  const NodeId thirdEmptyId = blockAt(session, 2)->id();
  const NodeId fourthEmptyId = blockAt(session, 3)->id();
  setCursor(selection, blockAt(session, 2), 0);
  require(input.insertParagraphBreak(), "enter in middle empty paragraph should create immediate empty paragraph");
  require(session.markdownText() == QString(14, QLatin1Char('\n')), "middle empty paragraph enter text mismatch");
  require(session.document().root().children().size() == emptyRunCount + 1, "middle empty paragraph enter block count mismatch");
  require(blockAt(session, 2)->id() == thirdEmptyId, "middle empty paragraph enter should preserve current empty id");
  require(blockAt(session, 4)->id() == fourthEmptyId, "middle empty paragraph enter should shift old next empty paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 3)->id(), "middle empty paragraph enter cursor should move to inserted empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "middle empty paragraph enter cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("alpha") + QString(13, QLatin1Char('\n')) + QStringLiteral("beta"), false);
  const qsizetype surroundedRunCount = session.document().root().children().size();
  const NodeId alphaId = blockAt(session, 0)->id();
  const NodeId surroundedThirdEmptyId = blockAt(session, 3)->id();
  const NodeId surroundedFourthEmptyId = blockAt(session, 4)->id();
  const NodeId betaId = blockAt(session, 7)->id();
  setCursor(selection, blockAt(session, 3), 0);
  require(input.insertParagraphBreak(), "enter in surrounded middle empty paragraph should create immediate empty paragraph");
  require(session.markdownText() == QStringLiteral("alpha") + QString(15, QLatin1Char('\n')) + QStringLiteral("beta"),
          "surrounded middle empty paragraph enter text mismatch");
  require(session.document().root().children().size() == surroundedRunCount + 1,
          "surrounded middle empty paragraph enter block count mismatch");
  require(blockAt(session, 0)->id() == alphaId, "surrounded middle empty paragraph enter should preserve alpha id");
  require(blockAt(session, 3)->id() == surroundedThirdEmptyId, "surrounded middle empty paragraph enter should preserve current empty id");
  require(blockAt(session, 5)->id() == surroundedFourthEmptyId, "surrounded middle empty paragraph enter should shift old next empty paragraph");
  require(blockAt(session, 8)->id() == betaId, "surrounded middle empty paragraph enter should preserve beta id");
  require(selection.cursorPosition().blockId == blockAt(session, 4)->id(),
          "surrounded middle empty paragraph enter cursor should move to inserted empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "surrounded middle empty paragraph enter cursor offset mismatch");
}

void testLocalReparsePreservesUntouchedNodeIds() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  const NodeId secondId = blockAt(session, 1)->id();
  const NodeId thirdId = blockAt(session, 2)->id();
  setCursor(selection, blockAt(session, 0), 5);

  require(input.insertText(QStringLiteral("!")), "local text input should work");
  require(session.markdownText() == QStringLiteral("alpha!\n\nbeta\n\ngamma"), "local input text mismatch");
  require(blockAt(session, 1)->id() == secondId, "local input should preserve untouched second paragraph id");
  require(blockAt(session, 2)->id() == thirdId, "local input should preserve untouched third paragraph id");

  session.setMarkdownText(QStringLiteral("\n\nalpha"), false);
  const NodeId alphaId = blockAt(session, 1)->id();
  setCursor(selection, blockAt(session, 0), 0);
  require(input.insertText(QStringLiteral("x")), "typing into leading empty paragraph should work");
  require(session.markdownText() == QStringLiteral("x\n\nalpha"), "leading empty paragraph fill text mismatch");
  require(blockAt(session, 1)->id() == alphaId, "filling leading empty paragraph should preserve following paragraph id");
}

void testBlockEditContextSeparatesBlockAndContentRanges() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 0);

  BlockEditContextResolver resolver(&session, &selection);
  BlockEditContext context;
  require(resolver.current(context), "block edit context should resolve heading");
  require(context.blockRange.byteStart == 0, "heading block range should start at marker");
  require(context.blockRange.byteEnd == 9, "heading block range end mismatch");
  require(context.contentRange.byteStart == 3, "heading content range should skip marker");
  require(context.contentRange.byteEnd == 9, "heading content range end mismatch");
  require(context.contentText == QStringLiteral("Status"), "heading content text mismatch");
}

void testTextBlockCommandBuilderCreatesStructuralEnterCommands() {
  DocumentSession session;
  SelectionController selection;

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 0);

  BlockEditContextResolver resolver(&session, &selection);
  BlockEditContext context;
  require(resolver.current(context), "builder test context should resolve heading");

  TextBlockCommandBuilder builder(&session, &resolver);
  TextBlockCommandBuilder::Command before =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(before.valid && before.handled, "builder should produce heading-start enter command");
  require(before.hasLocalEdit(), "builder heading-start command should be local edit");
  require(before.sourceStart == 0, "builder heading-start source start mismatch");
  require(before.removedLength == 0, "builder heading-start removed length mismatch");
  require(before.insertedText == QStringLiteral("\n\n"), "builder heading-start inserted text mismatch");
  require(before.preferredCursor.blockId == blockAt(session, 0)->id(), "builder heading-start cursor should prefer original heading");
  require(before.preferredCursor.text.textOffset == 0, "builder heading-start cursor offset mismatch");
  require(before.fallbackSourceOffset == 2, "builder heading-start fallback source offset mismatch");
  require(!before.preferLaterEmptyAtOffset, "builder heading-start should not prefer inserted empty paragraph");

  setCursor(selection, blockAt(session, 0), 6);
  require(resolver.current(context), "builder heading-end context should resolve");
  TextBlockCommandBuilder::Command after =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(after.valid && after.handled, "builder should produce heading-end enter command");
  require(after.hasLocalEdit(), "builder heading-end command should be local edit");
  require(after.sourceStart == 9, "builder heading-end source start mismatch");
  require(after.removedLength == 0, "builder heading-end removed length mismatch");
  require(after.insertedText == QStringLiteral("\n\n"), "builder heading-end inserted text mismatch");
  require(!after.preferredCursor.isValid(), "builder heading-end should use fallback cursor for new block");

  session.setMarkdownText(QString(12, QLatin1Char('\n')), false);
  const NodeId middleEmptyId = blockAt(session, 2)->id();
  const qsizetype middleEmptyOffset = blockAt(session, 2)->sourceRange().byteEnd;
  setCursor(selection, blockAt(session, 2), 0);
  require(resolver.current(context), "builder middle-empty enter context should resolve");
  TextBlockCommandBuilder::Command middleEmptyEnter =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Enter);
  require(middleEmptyEnter.valid && middleEmptyEnter.handled, "builder should produce middle-empty enter command");
  require(middleEmptyEnter.hasLocalEdit(), "builder middle-empty enter should be local edit");
  require(middleEmptyEnter.sourceStart == middleEmptyOffset, "builder middle-empty enter source start mismatch");
  require(middleEmptyEnter.removedLength == 0, "builder middle-empty enter removed length mismatch");
  require(middleEmptyEnter.insertedText == QStringLiteral("\n\n"), "builder middle-empty enter inserted text mismatch");
  require(!middleEmptyEnter.preferredCursor.isValid(), "builder middle-empty enter should use fallback cursor for inserted block");
  require(middleEmptyEnter.fallbackSourceOffset == middleEmptyOffset + 2, "builder middle-empty enter fallback source offset mismatch");
  require(middleEmptyEnter.nodeHints.size() == 1, "builder middle-empty enter should provide one node hint");
  require(middleEmptyEnter.nodeHints.first().nodeId == middleEmptyId, "builder middle-empty enter hint id mismatch");
  require(middleEmptyEnter.nodeHints.first().targetSourceOffset == blockAt(session, 2)->sourceRange().byteStart,
          "builder middle-empty enter hint offset mismatch");
  require(!middleEmptyEnter.preferLaterEmptyAtOffset, "builder middle-empty enter should not prefer later empty fallback");

  session.setMarkdownText(QStringLiteral("\n\n\n\nalpha"), false);
  const NodeId currentEmptyId = blockAt(session, 1)->id();
  setCursor(selection, blockAt(session, 1), 0);
  require(resolver.current(context), "builder empty-empty backspace context should resolve");
  TextBlockCommandBuilder::Command emptyBackspace =
      builder.buildTextEdit(context, TextBlockCommandBuilder::Operation::Backspace);
  require(emptyBackspace.valid && emptyBackspace.handled, "builder should produce empty-empty backspace command");
  require(emptyBackspace.hasLocalEdit(), "builder empty-empty backspace should be local edit");
  require(emptyBackspace.insertedText.isEmpty(), "builder empty-empty backspace inserted text mismatch");
  require(emptyBackspace.preferredCursor.blockId == currentEmptyId, "builder empty-empty backspace should prefer current empty paragraph");
  require(emptyBackspace.preferredCursor.text.textOffset == 0, "builder empty-empty backspace cursor offset mismatch");
  require(emptyBackspace.nodeHints.size() == 1, "builder empty-empty backspace should provide one node hint");
  require(emptyBackspace.nodeHints.first().nodeId == currentEmptyId, "builder empty-empty backspace hint id mismatch");
  require(emptyBackspace.nodeHints.first().targetSourceOffset == emptyBackspace.fallbackSourceOffset,
          "builder empty-empty backspace hint offset mismatch");
  require(emptyBackspace.preferLaterEmptyAtOffset, "builder empty-empty backspace should prefer later empty fallback");
}

void testInputMergeParagraphs() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 1), 0);

  require(input.deleteBackward(), "backspace at paragraph start should merge previous paragraph");
  require(session.markdownText() == QStringLiteral("alpha beta"), "backspace merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace merge cursor mismatch");
  EditTransaction mergeUndo = requireTextDeltaCommand(undoStack, "backspace merge should use text delta command");
  require(mergeUndo.textDeltaCommand().delta.removedText == QStringLiteral("\n\n"), "backspace merge removed text mismatch");
  require(mergeUndo.textDeltaCommand().delta.insertedText == QStringLiteral(" "), "backspace merge inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta"), false);
  setCursor(selection, blockAt(session, 0), 5);

  require(input.deleteForward(), "delete at paragraph end should merge next paragraph");
  require(session.markdownText() == QStringLiteral("alpha beta"), "delete merge result mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "delete merge cursor mismatch");
  mergeUndo = requireTextDeltaCommand(undoStack, "delete merge should use text delta command");
  require(mergeUndo.textDeltaCommand().delta.removedText == QStringLiteral("\n\n"), "delete merge removed text mismatch");
  require(mergeUndo.textDeltaCommand().delta.insertedText == QStringLiteral(" "), "delete merge inserted text mismatch");
}

void testInputBackspaceAtParagraphStartDeletesStructuralBoundary() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteBackward(), "backspace at document start should be handled as no-op");
  require(session.markdownText() == QStringLiteral("alpha"), "backspace at document start should not change text");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace at document start cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace at document start cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("alpha\n\n\n\nbeta"), false);
  const NodeId betaId = blockAt(session, 2)->id();
  setCursor(selection, blockAt(session, 2), 0);
  require(input.deleteBackward(), "backspace after empty paragraph should remove one empty boundary");
  require(session.markdownText() == QStringLiteral("alpha\n\nbeta"), "backspace empty paragraph boundary mismatch");
  require(blockAt(session, 1)->id() == betaId, "backspace empty paragraph should preserve following paragraph id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "backspace empty boundary cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace empty boundary cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n\n\nalpha"), false);
  const NodeId secondEmptyId = blockAt(session, 1)->id();
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace from second empty paragraph should remove one empty boundary");
  require(session.markdownText() == QStringLiteral("\n\nalpha"), "backspace second empty paragraph text mismatch");
  require(session.document().root().children().size() == 2, "backspace second empty paragraph block count mismatch");
  require(blockAt(session, 0)->id() == secondEmptyId, "backspace second empty paragraph should preserve current empty id");
  require(selection.hasCursor(), "backspace second empty paragraph should keep cursor");
  require(session.document().node(selection.cursorPosition().blockId) != nullptr, "backspace second empty paragraph cursor block should exist");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace second empty paragraph cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace second empty paragraph cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n\n\n\n\nalpha"), false);
  const NodeId thirdEmptyId = blockAt(session, 2)->id();
  setCursor(selection, blockAt(session, 2), 0);
  require(input.deleteBackward(), "backspace from third empty paragraph should remove one empty boundary");
  require(session.markdownText() == QStringLiteral("\n\n\n\nalpha"), "backspace third empty paragraph text mismatch");
  require(session.document().root().children().size() == 3, "backspace third empty paragraph block count mismatch");
  require(blockAt(session, 1)->id() == thirdEmptyId, "backspace third empty paragraph should preserve current empty id");
  require(selection.hasCursor(), "backspace third empty paragraph should keep cursor");
  require(session.document().node(selection.cursorPosition().blockId) != nullptr, "backspace third empty paragraph cursor block should exist");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "backspace third empty paragraph cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace third empty paragraph cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n#### Heading Level 4 With `code`"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace from heading after empty paragraph should remove empty boundary");
  require(session.markdownText() == QStringLiteral("#### Heading Level 4 With `code`"), "backspace should preserve current heading marker");
  require(blockAt(session, 0)->type() == BlockType::Heading, "backspace should keep current block as heading");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace preserved heading cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "backspace preserved heading cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\nbody"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace after heading should merge into heading");
  require(session.markdownText() == QStringLiteral("# Title body"), "backspace heading merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace heading merge cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "backspace heading merge cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before **bold**\n\nafter"), false);
  setCursor(selection, blockAt(session, 1), 0);
  require(input.deleteBackward(), "backspace after complex inline paragraph should merge");
  require(session.markdownText() == QStringLiteral("before **bold** after"), "backspace complex inline merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "backspace complex inline cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 11, "backspace complex inline cursor offset mismatch");
}

void testInputDeleteAtParagraphEndDeletesStructuralBoundary() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\n# Title"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.deleteForward(), "delete before heading should merge heading into paragraph");
  require(session.markdownText() == QStringLiteral("alpha Title"), "delete heading merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete heading merge cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "delete heading merge cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("\n\n#### Heading Level 4 With `code`"), false);
  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteForward(), "delete from empty paragraph before heading should remove empty boundary");
  require(session.markdownText() == QStringLiteral("#### Heading Level 4 With `code`"), "delete should preserve next heading marker");
  require(blockAt(session, 0)->type() == BlockType::Heading, "delete should keep next block as heading");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete preserved heading cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "delete preserved heading cursor offset mismatch");

  session.setMarkdownText(QStringLiteral("before **bold**\n\nafter"), false);
  setCursor(selection, blockAt(session, 0), 11);
  require(input.deleteForward(), "delete after complex inline paragraph should merge");
  require(session.markdownText() == QStringLiteral("before **bold** after"), "delete complex inline merge mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "delete complex inline cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 11, "delete complex inline cursor offset mismatch");
}

void testInputUndoRedoSnapshots() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertText(QStringLiteral("!")), "insert should create transaction");

  const EditTransaction undo = undoStack.takeUndo();
  require(undo.isTextDeltaCommand(), "plain insert undo should use text delta");
  require(undo.textDeltaCommand().delta.start == 5, "plain insert delta start mismatch");
  require(undo.textDeltaCommand().delta.removedText.isEmpty(), "plain insert delta removed text mismatch");
  require(undo.textDeltaCommand().delta.insertedText == QStringLiteral("!"), "plain insert delta inserted text mismatch");
  session.applyTextDelta(undo.textDeltaCommand().delta.start, undo.textDeltaCommand().delta.insertedText.size(), undo.textDeltaCommand().delta.removedText, true);
  selection.setCursorPosition(undo.textDeltaCommand().beforeCursor);
  require(session.markdownText() == QStringLiteral("alpha"), "undo snapshot text mismatch");
  require(selection.cursorPosition().text.textOffset == 5, "undo snapshot cursor mismatch");

  const EditTransaction redo = undoStack.takeRedo();
  session.applyTextDelta(redo.textDeltaCommand().delta.start, redo.textDeltaCommand().delta.removedText.size(), redo.textDeltaCommand().delta.insertedText, true);
  selection.setCursorPosition(redo.textDeltaCommand().afterCursor);
  require(session.markdownText() == QStringLiteral("alpha!"), "redo snapshot text mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "redo snapshot cursor mismatch");
}

void testControllerUndoRedoRemapsCursorAfterReparse() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("$123$"), false);
  view.setDocument(session.document());
  setSourceCursor(controller.selection(), blockAt(session, 0), 0, 1);
  require(controller.inputController().insertText(QStringLiteral("a")), "controller input should edit inline math");
  require(session.markdownText() == QStringLiteral("$a123$"), "controller inline math insert mismatch");

  controller.undo();
  require(session.markdownText() == QStringLiteral("$123$"), "controller undo text mismatch");
  require(controller.selection().hasCursor(), "controller undo should keep cursor");
  require(controller.selection().cursorPosition().blockId == blockAt(session, 0)->id(), "controller undo cursor should remap to reparsed block");
  require(controller.selection().cursorPosition().text.sourceOffset == 1, "controller undo cursor source mismatch");
  view.setCursorPosition(controller.selection().cursorPosition());
  require(view.hitTest(view.nodeRect(blockAt(session, 0)->id()).center()).isValid(), "controller undo view should keep valid layout hit");

  controller.redo();
  require(session.markdownText() == QStringLiteral("$a123$"), "controller redo text mismatch");
  require(controller.selection().hasCursor(), "controller redo should keep cursor");
  require(controller.selection().cursorPosition().blockId == blockAt(session, 0)->id(), "controller redo cursor should remap to reparsed block");
  require(controller.selection().cursorPosition().text.sourceOffset == 2, "controller redo cursor source mismatch");
}

void testInputSelectionReplaceAndDelete() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.insertText(QStringLiteral("X")), "typing should replace selection");
  require(session.markdownText() == QStringLiteral("aXa"), "selection replace text mismatch");
  require(selection.cursorPosition().text.textOffset == 2, "selection replace cursor mismatch");
  EditTransaction replaceUndo = requireTextDeltaCommand(undoStack, "selection replace should use text delta command");
  require(replaceUndo.textDeltaCommand().delta.start == 1, "selection replace delta start mismatch");
  require(replaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"), "selection replace removed text mismatch");
  require(replaceUndo.textDeltaCommand().delta.insertedText == QStringLiteral("X"), "selection replace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.deleteBackward(), "backspace should delete selection");
  require(session.markdownText() == QStringLiteral("aa"), "backspace selection delete mismatch");
  EditTransaction backspaceUndo = requireTextDeltaCommand(undoStack, "selection backspace should use text delta command");
  require(backspaceUndo.textDeltaCommand().delta.start == 1, "selection backspace delta start mismatch");
  require(backspaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"), "selection backspace removed text mismatch");
  require(backspaceUndo.textDeltaCommand().delta.insertedText.isEmpty(), "selection backspace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setSelection(selection, blockAt(session, 0), 1, 4);
  require(input.deleteForward(), "delete should delete selection");
  require(session.markdownText() == QStringLiteral("aa"), "delete selection mismatch");
  EditTransaction deleteUndo = requireTextDeltaCommand(undoStack, "selection delete should use text delta command");
  require(deleteUndo.textDeltaCommand().delta.start == 1, "selection delete delta start mismatch");
  require(deleteUndo.textDeltaCommand().delta.removedText == QStringLiteral("lph"), "selection delete removed text mismatch");
  require(deleteUndo.textDeltaCommand().delta.insertedText.isEmpty(), "selection delete inserted text mismatch");
}

void testInputCrossParagraphSelectionReplaceAndDelete() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, blockAt(session, 2), 3);
  require(selectedMarkdown(session, selection) == QStringLiteral("pha\n\nbeta\n\ngam"), "cross selection markdown mismatch");
  require(selectedPlainText(session, selection) == QStringLiteral("pha\nbeta\ngam"), "cross selection plain text mismatch");

  require(input.insertText(QStringLiteral("X")), "typing should replace cross paragraph selection");
  require(session.markdownText() == QStringLiteral("alXma"), "cross paragraph replace mismatch");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "cross paragraph replace cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 3, "cross paragraph replace cursor offset mismatch");
  EditTransaction replaceUndo = requireTextDeltaCommand(undoStack, "cross paragraph replace should use text delta command");
  require(replaceUndo.textDeltaCommand().delta.start == 2, "cross paragraph replace delta start mismatch");
  require(replaceUndo.textDeltaCommand().delta.removedText == QStringLiteral("pha\n\nbeta\n\ngam"), "cross paragraph replace removed text mismatch");
  require(replaceUndo.textDeltaCommand().delta.insertedText == QStringLiteral("X"), "cross paragraph replace inserted text mismatch");

  session.setMarkdownText(QStringLiteral("# Title\n\n- alpha\n- beta\n\nomega"), false);
  setCrossSelection(selection, blockAt(session, 0), 2, blockAt(session, 2), 2);
  require(input.deleteSelection(), "delete should remove cross block selection");
  require(session.markdownText() == QStringLiteral("# Tiega"), "cross block delete mismatch");
  EditTransaction deleteUndo = requireTextDeltaCommand(undoStack, "cross block delete should use text delta command");
  require(deleteUndo.textDeltaCommand().delta.start == 4, "cross block delete delta start mismatch");
  require(deleteUndo.textDeltaCommand().delta.removedText == QStringLiteral("tle\n\n- alpha\n- beta\n\nom"), "cross block delete removed text mismatch");
  require(deleteUndo.textDeltaCommand().delta.insertedText.isEmpty(), "cross block delete inserted text mismatch");
}

void testHeadingInput() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("# Title"), false);
  setCursor(selection, blockAt(session, 0), 5);
  require(input.insertText(QStringLiteral("!")), "heading text insert should edit heading body");
  require(session.markdownText() == QStringLiteral("# Title!"), "heading insert should preserve marker");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "heading insert cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "heading insert cursor offset mismatch");

  setCursor(selection, blockAt(session, 0), 0);
  require(input.deleteBackward(), "heading start backspace should be handled");
  require(session.markdownText() == QStringLiteral("# Title!"), "heading start backspace should not remove marker");
}

void testHeadingEnterAtStartInsertsParagraphBeforeBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("## Status"), false);
  const NodeId headingId = blockAt(session, 0)->id();
  setCursor(selection, blockAt(session, 0), 0);

  require(input.insertParagraphBreak(), "heading start enter should edit document");
  require(session.markdownText() == QStringLiteral("\n\n## Status"), "heading start enter should insert before heading marker");
  require(session.document().root().children().size() == 2, "heading start enter should create leading empty paragraph");
  require(blockAt(session, 1)->id() == headingId, "heading start enter should preserve heading id");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "heading start enter cursor should stay on original heading");
  require(selection.cursorPosition().text.textOffset == 0, "heading start enter cursor should be at original heading start");
  EditTransaction enterUndo = requireTextDeltaCommand(undoStack, "heading start enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.start == 0, "heading start enter delta start mismatch");
  require(enterUndo.textDeltaCommand().delta.removedText.isEmpty(), "heading start enter removed text mismatch");
  require(enterUndo.textDeltaCommand().delta.insertedText == QStringLiteral("\n\n"), "heading start enter inserted text mismatch");

  session.setMarkdownText(QStringLiteral("## Status"), false);
  setCursor(selection, blockAt(session, 0), 6);

  require(input.insertParagraphBreak(), "heading end enter should edit document");
  require(session.markdownText() == QStringLiteral("## Status\n\n"), "heading end enter should insert after heading block");
  require(session.document().root().children().size() == 2, "heading end enter should create trailing empty paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "heading end enter cursor should move to empty paragraph");
  require(selection.cursorPosition().text.textOffset == 0, "heading end enter cursor should be at empty paragraph start");
  enterUndo = requireTextDeltaCommand(undoStack, "heading end enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.insertedText == QStringLiteral("\n\n"), "heading end enter inserted text mismatch");

  session.setMarkdownText(QStringLiteral("## Headings"), false);
  setCursor(selection, blockAt(session, 0), 3);

  require(input.insertParagraphBreak(), "heading middle enter should split heading");
  require(session.markdownText() == QStringLiteral("## Hea\n\n## dings"), "heading middle enter should preserve heading marker on second half");
  require(session.document().root().children().size() == 2, "heading middle enter should create two headings");
  require(blockAt(session, 0)->type() == BlockType::Heading, "heading middle enter first block should remain heading");
  require(blockAt(session, 1)->type() == BlockType::Heading, "heading middle enter second block should be heading");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "heading middle enter cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "heading middle enter cursor should be at second heading start");
  enterUndo = requireTextDeltaCommand(undoStack, "heading middle enter should use text delta command");
  require(enterUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n\n## ")), "heading middle enter delta should insert split marker");
}

void testListItemInput() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 5);
  require(input.insertText(QStringLiteral("!")), "unordered list item insert should edit item body");
  require(session.markdownText() == QStringLiteral("- alpha!\n- beta"), "unordered list insert should preserve marker");
  require(selection.cursorPosition().blockId == listItemAt(session, 0, 0)->id(), "unordered list cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 6, "unordered list cursor offset mismatch");

  setSelection(selection, listItemAt(session, 0, 0), 1, 5);
  require(input.insertText(QStringLiteral("X")), "unordered list selection replace should edit item body");
  require(session.markdownText() == QStringLiteral("- aX!\n- beta"), "unordered list selection replace mismatch");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 5);
  require(input.insertText(QStringLiteral("!")), "ordered list item insert should edit item body");
  require(session.markdownText() == QStringLiteral("1. alpha!\n2. beta"), "ordered list insert should preserve marker");
}

void testListItemEditingCommands() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split unordered list item");
  require(session.markdownText() == QStringLiteral("- al\n- pha\n- beta"), "unordered list split mismatch");
  require(selection.cursorPosition().blockId == listItemAt(session, 0, 1)->id(), "unordered split cursor block mismatch");
  require(selection.cursorPosition().text.textOffset == 0, "unordered split cursor offset mismatch");
  EditTransaction listUndo = requireTextDeltaCommand(undoStack, "unordered list split should use text delta command");
  require(listUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n- ")), "unordered list split delta inserted text mismatch");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 2);
  require(input.insertParagraphBreak(), "enter should split ordered list item");
  require(session.markdownText() == QStringLiteral("1. al\n2. pha\n2. beta"), "ordered list split mismatch");
  listUndo = requireTextDeltaCommand(undoStack, "ordered list split should use text delta command");
  require(listUndo.textDeltaCommand().delta.insertedText.contains(QStringLiteral("\n2. ")), "ordered list split delta inserted text mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix **bold**\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix b").size(),
                  QStringLiteral("- Plain text can mix **b").size());
  require(input.insertParagraphBreak(), "enter inside strong inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix **b**\n- **old**\n- beta"),
          "strong inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix *italic*\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix ita").size(),
                  QStringLiteral("- Plain text can mix *ita").size());
  require(input.insertParagraphBreak(), "enter inside emphasis inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix *ita*\n- *lic*\n- beta"),
          "emphasis inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- Plain text can mix ~~through~~\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("Plain text can mix thr").size(),
                  QStringLiteral("- Plain text can mix ~~thr").size());
  require(input.insertParagraphBreak(), "enter inside strike inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- Plain text can mix ~~thr~~\n- ~~ough~~\n- beta"),
          "strike inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- before `code` after\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("before co").size(), QStringLiteral("- before `co").size());
  require(input.insertParagraphBreak(), "enter inside code inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- before `co`\n- `de` after\n- beta"), "code inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- before $x+y$ after\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), QStringLiteral("before x").size(), QStringLiteral("- before $x").size());
  require(input.insertParagraphBreak(), "enter inside math inline list item should split wrapper");
  require(session.markdownText() == QStringLiteral("- before $x$\n- $+y$ after\n- beta"), "math inline list wrapper split mismatch");

  session.setMarkdownText(QStringLiteral("- \n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 0);
  require(input.insertParagraphBreak(), "enter on empty list item should exit list");
  require(session.markdownText() == QStringLiteral("\n\n- beta"), "empty list enter exit mismatch");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 0);
  require(input.deleteBackward(), "backspace at list start should exit top-level list item");
  require(session.markdownText() == QStringLiteral("alpha\n- beta"), "top-level list backspace exit mismatch");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  require(input.indentListItem(), "tab should indent list item");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "list item indent mismatch");
  MarkdownNode* nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  require(nestedList != nullptr && nestedList->children().size() == 1, "list indent should create a nested list");
  require(undoStack.canUndo(), "list indent should be undoable");

  nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  setCursor(selection, childAt(nestedList, 0), 0);
  require(input.outdentListItem(), "shift-tab should outdent nested list item");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "list item outdent mismatch");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 0), 0);
  require(!input.indentListItem(), "first list item should not structurally indent without a previous sibling");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "first list item structural indent should leave markdown unchanged");

  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);
  session.setMarkdownText(QStringLiteral("- alpha\n  - beta"), false);
  nestedList = firstChildOfType(listItemAt(session, 0, 0), BlockType::List);
  setCursor(selection, childAt(nestedList, 0), 0);
  QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
  require(input.eventFilter(&view, &backtab), "backtab key should outdent nested list item");
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "backtab key list outdent mismatch");
}

void testTabInRenderedTextInsertsZeroWidthSpace() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  EditorView view;
  QVector<BrushQueue::RefreshRequest> tabRefreshes;
  QObject::connect(&brushQueue, &BrushQueue::refreshRequested, [&tabRefreshes](BrushQueue::RefreshRequest request) {
    tabRefreshes.push_back(std::move(request));
  });
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  setCursor(selection, blockAt(session, 0), 0);
  QKeyEvent shortcutTab(QEvent::ShortcutOverride, Qt::Key_Tab, Qt::NoModifier);
  require(!input.eventFilter(&view, &shortcutTab), "tab shortcut override should continue to keypress delivery");
  require(shortcutTab.isAccepted(), "tab shortcut override should reserve tab for rendered editor input");
  QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &tab), "tab at paragraph start should insert a zero-width space");
  require(session.markdownText() == QStringLiteral("​alpha"), "paragraph tab should insert U+200B");

  session.setMarkdownText(QStringLiteral("alphabeta"), false);
  setCursor(selection, blockAt(session, 0), 5);
  QKeyEvent middleTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &middleTab), "tab in middle of paragraph should insert a zero-width space");
  require(session.markdownText() == QStringLiteral("alpha​beta"), "middle paragraph tab should insert U+200B");
  require(selection.cursorPosition().text.sourceOffset == QStringLiteral("alpha​").size(), "middle paragraph tab source offset mismatch");
  require(input.insertText(QStringLiteral("x")), "typing after middle paragraph tab should keep editing");
  require(session.markdownText() == QStringLiteral("alpha​xbeta"), "typing after middle paragraph tab should preserve U+200B");

  session.setMarkdownText(QStringLiteral("## Title"), false);
  setCursor(selection, blockAt(session, 0), 2);
  QKeyEvent headingTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &headingTab), "tab in heading should insert a zero-width space");
  require(session.markdownText() == QStringLiteral("## Ti​tle"), "heading tab should insert U+200B inside heading content");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setCursor(selection, listItemAt(session, 0, 1), 0);
  QKeyEvent listTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &listTab), "tab on list item should still indent structurally");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "list item tab should not fall back to plain spaces");
  require(!session.markdownText().contains(QChar(0x200b)), "unordered list tab should not insert U+200B");
  brushQueue.flush();
  require(!tabRefreshes.isEmpty() && !tabRefreshes.last().fullLayoutDirty, "unordered list structural tab should not request full refresh");
  require(tabRefreshes.last().topLevelRangeDirty.isValid(), "unordered list structural tab should request top-level range refresh");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  tabRefreshes.clear();
  setSourceCursor(selection, listItemAt(session, 0, 1), 2, QStringLiteral("- alpha\n- be").size());
  QKeyEvent listMiddleTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &listMiddleTab), "tab in middle of unordered list item should insert U+200B");
  require(session.markdownText() == QStringLiteral("- alpha\n- be​ta"), "unordered list middle tab should not indent structurally");
  brushQueue.flush();
  require(maybeFirstChildOfType(listItemAt(session, 0, 0), BlockType::List) == nullptr,
          "unordered list middle tab should not create nested list structure");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  tabRefreshes.clear();
  setSourceCursor(selection, listItemAt(session, 0, 1), 0, QStringLiteral("1. alpha\n2. ").size());
  QKeyEvent orderedListTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &orderedListTab), "tab on ordered list item should indent structurally");
  require(session.markdownText() == QStringLiteral("1. alpha\n  2. beta"), "ordered list tab should add structural leading spaces");
  require(!session.markdownText().contains(QChar(0x200b)), "ordered list tab should not insert U+200B");
  brushQueue.flush();
  require(!tabRefreshes.isEmpty() && !tabRefreshes.last().fullLayoutDirty, "ordered list structural tab should not request full refresh");
  require(tabRefreshes.last().topLevelRangeDirty.isValid(), "ordered list structural tab should request top-level range refresh");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 1), 2, QStringLiteral("1. alpha\n2. be").size());
  QKeyEvent orderedListMiddleTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &orderedListMiddleTab), "tab in middle of ordered list item should insert U+200B");
  require(session.markdownText() == QStringLiteral("1. alpha\n2. be​ta"), "ordered list middle tab should not indent structurally");

  session.setMarkdownText(QStringLiteral("- M0/M2 alpha\n- M3 beta"), false);
  tabRefreshes.clear();
  setSourceCursor(selection, listItemAt(session, 0, 0), 0, QStringLiteral("- ").size());
  QKeyEvent firstUnorderedListTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &firstUnorderedListTab), "tab at first unordered item content start should insert U+200B");
  require(session.markdownText() == QStringLiteral("- ​M0/M2 alpha\n- M3 beta"),
          "first unordered item tab should fall back to content indentation");
  require(maybeFirstChildOfType(listItemAt(session, 0, 0), BlockType::List) == nullptr,
          "first unordered item tab should not create impossible nested list structure");

  session.setMarkdownText(QStringLiteral("1. M0/M2 alpha\n2. M3 beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 0), 0, QStringLiteral("1. ").size());
  QKeyEvent firstOrderedListTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &firstOrderedListTab), "tab at first ordered item content start should insert U+200B");
  require(session.markdownText() == QStringLiteral("1. ​M0/M2 alpha\n2. M3 beta"),
          "first ordered item tab should fall back to content indentation");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 1), 1, QStringLiteral("- alpha\n- b").size());
  QKeyEvent unorderedVisualStartTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &unorderedVisualStartTab), "tab near unordered item visual start should indent structurally");
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "unordered visual-start tab should indent item");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  setSourceCursor(selection, listItemAt(session, 0, 1), 1, QStringLiteral("1. alpha\n2. b").size());
  QKeyEvent orderedVisualStartTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  require(input.eventFilter(&view, &orderedVisualStartTab), "tab near ordered item visual start should indent structurally");
  require(session.markdownText() == QStringLiteral("1. alpha\n  2. beta"), "ordered visual-start tab should indent item");

  DocumentSession realSession;
  EditorController controller;
  EditorView realView;
  realSession.setMarkdownText(QStringLiteral("alphabeta"), false);
  realView.resize(640, 360);
  realView.setDocument(realSession.document());
  controller.attach(&realSession, &realView);
  setSourceCursor(controller.selection(), blockAt(realSession, 0), 5, 5);
  QKeyEvent realShortcut(QEvent::ShortcutOverride, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(&realView, &realShortcut);
  require(realShortcut.isAccepted(), "real editor view should accept tab shortcut override");
  QKeyEvent realTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(&realView, &realTab);
  require(realSession.markdownText() == QStringLiteral("alpha​beta"), "real editor tab should insert U+200B in paragraph");
}

void testListTabFromRenderedClick() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(720, 420);

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  view.setDocument(session.document());
  MarkdownNode* secondItem = listItemAt(session, 0, 1);
  const QRectF secondRect = view.nodeRect(secondItem->id());
  const QPointF unorderedStart(secondRect.left() + 4.0, secondRect.center().y());
  QMouseEvent unorderedPress(
      QEvent::MouseButtonPress,
      unorderedStart,
      QPointF(unorderedStart),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &unorderedPress);
  QMouseEvent unorderedRelease(
      QEvent::MouseButtonRelease,
      unorderedStart,
      QPointF(unorderedStart),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &unorderedRelease);
  QKeyEvent unorderedTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(&view, &unorderedTab);
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "clicking unordered item start then tab should indent item");

  QKeyEvent unorderedBacktab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
  QApplication::sendEvent(&view, &unorderedBacktab);
  require(session.markdownText() == QStringLiteral("- alpha\n- beta"), "shift-tab after unordered indent should outdent item");

  session.setMarkdownText(QStringLiteral("1. alpha\n2. beta"), false);
  view.setDocument(session.document());
  MarkdownNode* orderedSecond = listItemAt(session, 0, 1);
  const QRectF orderedRect = view.nodeRect(orderedSecond->id());
  const QPointF orderedStart(orderedRect.left() + 4.0, orderedRect.center().y());
  QMouseEvent orderedPress(
      QEvent::MouseButtonPress,
      orderedStart,
      QPointF(orderedStart),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &orderedPress);
  QMouseEvent orderedRelease(
      QEvent::MouseButtonRelease,
      orderedStart,
      QPointF(orderedStart),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &orderedRelease);
  QKeyEvent orderedTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(&view, &orderedTab);
  require(session.markdownText() == QStringLiteral("1. alpha\n  2. beta"), "clicking ordered item start then tab should indent item");

  session.setMarkdownText(QStringLiteral("- alpha\n- beta"), false);
  view.setDocument(session.document());
  secondItem = listItemAt(session, 0, 1);
  const QRectF viewportRect = view.nodeRect(secondItem->id());
  const QPointF viewportStart(viewportRect.left() + 4.0, viewportRect.center().y());
  QMouseEvent viewportPress(
      QEvent::MouseButtonPress,
      viewportStart,
      QPointF(viewportStart),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &viewportPress);
  QMouseEvent viewportRelease(
      QEvent::MouseButtonRelease,
      viewportStart,
      QPointF(viewportStart),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &viewportRelease);
  QKeyEvent viewportShortcut(QEvent::ShortcutOverride, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &viewportShortcut);
  require(viewportShortcut.isAccepted(), "viewport tab shortcut override should be accepted");
  QKeyEvent viewportTab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &viewportTab);
  require(session.markdownText() == QStringLiteral("- alpha\n  - beta"), "viewport tab after list click should indent item");
  MarkdownNode* indentedViewportItem = listItemAt(session, 0, 0)->children().size() > 1
                                           ? childAt(firstChildOfType(listItemAt(session, 0, 0), BlockType::List), 0)
                                           : nullptr;
  require(indentedViewportItem != nullptr, "viewport tab should parse indented unordered item as nested list item");
  require(listDepthForItem(indentedViewportItem) == 2, "viewport tab should increase unordered list AST depth");
}

void testInputEmptyCodeFenceBackspaceRemovesBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController codeFence;
  LiteralBlockController mathBlock(LiteralBlockSpec{
      BlockType::MathBlock, HitTestResult::Zone::Math,
      QStringLiteral("No math block is active."),
      QStringLiteral("Edit Math Block"),
      QStringLiteral("Backspace Math Block"),
      QStringLiteral("Delete Math Block Text"),
      QStringLiteral("Delete Math Block Selection"),
      QStringLiteral("Set Math Block TeX"),
      QStringLiteral("  ")});
  codeFence.setContext({&session, &selection, &undoStack, &brushQueue});
  mathBlock.setContext({&session, &selection, &undoStack, &brushQueue});

  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, nullptr,
      {{static_cast<int>(BlockType::MathBlock), &mathBlock}});
  input.setCodeFenceController(&codeFence);

  // Empty code fence between paragraphs
  session.setMarkdownText(QStringLiteral("before\n\n```cpp\n```\n\nafter"), false);
  require(session.document().root().children().size() == 3, "expected 3 blocks: para, code, para");
  MarkdownNode* code = blockAt(session, 1);
  require(code->type() == BlockType::CodeFence, "second block should be code fence");
  require(code->literal().isEmpty(), "code fence literal should be empty");

  // Set cursor in code block and enter edit mode
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = code->id();
  hit.textNodeId = code->id();
  hit.textOffset = 0;
  selection.setHitResult(hit);
  require(codeFence.enterEditMode(), "enter code edit should work");
  require(codeFence.isEditing(), "code fence should be editing");

  // Backspace on empty code fence should remove the block
  require(input.deleteBackward(), "backspace on empty code fence should succeed");
  require(!session.markdownText().contains(QStringLiteral("```")), "empty code fence should be removed after backspace");
  // "before" and "after" paragraphs remain (they may be merged into one paragraph)
  require(session.document().root().children().size() >= 1, "should have at least 1 block after removing code fence");
  require(!codeFence.isEditing(), "code fence should no longer be editing after removal");

  // Undo entry should be a RemoveNodeCommand
  require(undoStack.canUndo(), "removing empty code fence should push undo");
  EditTransaction undo = undoStack.takeUndo();
  require(undo.isRemoveNodeCommand(), "expected RemoveNodeCommand for empty block removal");
  require(undo.removeNodeCommand().nodeType == BlockType::CodeFence, "removed node type should be CodeFence");
}

void testInputEmptyCodeFenceDeleteRemovesBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController codeFence;
  LiteralBlockController mathBlock(LiteralBlockSpec{
      BlockType::MathBlock, HitTestResult::Zone::Math,
      QStringLiteral("No math block is active."),
      QStringLiteral("Edit Math Block"),
      QStringLiteral("Backspace Math Block"),
      QStringLiteral("Delete Math Block Text"),
      QStringLiteral("Delete Math Block Selection"),
      QStringLiteral("Set Math Block TeX"),
      QStringLiteral("  ")});
  codeFence.setContext({&session, &selection, &undoStack, &brushQueue});
  mathBlock.setContext({&session, &selection, &undoStack, &brushQueue});

  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, nullptr,
      {{static_cast<int>(BlockType::MathBlock), &mathBlock}});
  input.setCodeFenceController(&codeFence);

  session.setMarkdownText(QStringLiteral("```cpp\n```"), false);
  require(session.document().root().children().size() == 1, "expected 1 code fence block");
  MarkdownNode* code = blockAt(session, 0);
  require(code->type() == BlockType::CodeFence, "first block should be code fence");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = code->id();
  hit.textNodeId = code->id();
  hit.textOffset = 0;
  selection.setHitResult(hit);
  require(codeFence.enterEditMode(), "enter code edit should work");

  // Delete on empty code fence should remove the block
  require(input.deleteForward(), "delete on empty code fence should succeed");
  require(!session.markdownText().contains(QStringLiteral("```")), "empty code fence should be removed after delete");
}

void testInputNonEmptyCodeFenceBackspaceDoesNotRemoveBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  CodeFenceController codeFence;
  LiteralBlockController mathBlock(LiteralBlockSpec{
      BlockType::MathBlock, HitTestResult::Zone::Math,
      QStringLiteral("No math block is active."),
      QStringLiteral("Edit Math Block"),
      QStringLiteral("Backspace Math Block"),
      QStringLiteral("Delete Math Block Text"),
      QStringLiteral("Delete Math Block Selection"),
      QStringLiteral("Set Math Block TeX"),
      QStringLiteral("  ")});
  codeFence.setContext({&session, &selection, &undoStack, &brushQueue});
  mathBlock.setContext({&session, &selection, &undoStack, &brushQueue});

  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue, nullptr,
      {{static_cast<int>(BlockType::MathBlock), &mathBlock}});
  input.setCodeFenceController(&codeFence);

  session.setMarkdownText(QStringLiteral("```cpp\nhello\n```"), false);
  MarkdownNode* code = blockAt(session, 0);
  require(code->type() == BlockType::CodeFence, "first block should be code fence");
  require(!code->literal().isEmpty(), "code fence should have content");

  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Code;
  hit.blockId = code->id();
  hit.textNodeId = code->id();
  hit.textOffset = 0;
  selection.setHitResult(hit);
  require(codeFence.enterEditMode(), "enter code edit should work");

  // Backspace on non-empty code fence should delete character, not remove block
  require(input.deleteBackward(), "backspace should succeed");
  require(session.markdownText().contains(QStringLiteral("```cpp")), "non-empty code fence should not be removed");
}

void testDefinitionBlockFieldEditing() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[]: "), false);
  MarkdownNode* link = blockAt(session, 0);
  require(link->type() == BlockType::LinkDefinition, "empty link template should parse as link definition");
  const DefinitionBlock emptyLink = link->definition();
  setSourceCursor(selection, link, emptyLink.labelRange.start - emptyLink.markerRange.start, emptyLink.labelRange.start);
  require(input.insertText(QStringLiteral("1")), "typing label should edit link definition");
  link = blockAt(session, 0);
  const DefinitionBlock labeledLink = link->definition();
  require(labeledLink.label == QStringLiteral("1"), "link label should update");

  setSourceCursor(selection, link, labeledLink.destinationRange.start - labeledLink.markerRange.start, labeledLink.destinationRange.start);
  require(input.insertText(QStringLiteral("https://example.com")), "typing url should edit link definition");
  link = blockAt(session, 0);
  const DefinitionBlock urlLink = link->definition();
  require(urlLink.destination == QStringLiteral("https://example.com"), "link destination should update");

  HitTestResult titleHit;
  titleHit.blockId = link->id();
  titleHit.textNodeId = link->id();
  titleHit.textOffset = urlLink.titleRange.start - urlLink.markerRange.start;
  titleHit.sourceOffset = urlLink.titleRange.start;
  titleHit.zone = HitTestResult::Zone::Text;
  titleHit.definitionField = HitTestResult::DefinitionField::Title;
  selection.setHitResult(titleHit);
  require(input.insertText(QStringLiteral("Example")), "typing title should edit link definition");
  require(session.markdownText() == QStringLiteral("[1]: https://example.com  \"Example\""), "link definition markdown mismatch");

  session.setMarkdownText(QStringLiteral("[^]: "), false);
  MarkdownNode* footnote = blockAt(session, 0);
  require(footnote->type() == BlockType::FootnoteDefinition, "empty footnote template should parse as footnote definition");
  const DefinitionBlock emptyFootnote = footnote->definition();
  setSourceCursor(selection, footnote, emptyFootnote.noteRange.start - emptyFootnote.markerRange.start, emptyFootnote.noteRange.start);
  require(input.insertText(QStringLiteral("note")), "typing note should edit footnote definition");
  require(session.markdownText() == QStringLiteral("[^]: note"), "footnote markdown mismatch");
}

void testEmptyDefinitionBackspaceDeletesBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("alpha\n\n[]: "), false);
  MarkdownNode* link = firstBlockOfType(session, BlockType::LinkDefinition);
  require(link->type() == BlockType::LinkDefinition, "document should contain link definition");
  require(link->definition().label.isEmpty(), QStringLiteral("empty link label expected: '%1'").arg(link->definition().label));
  require(link->definition().destination.isEmpty(), QStringLiteral("empty link destination expected: '%1'").arg(link->definition().destination));
  require(link->definition().title.isEmpty(), QStringLiteral("empty link title expected: '%1'").arg(link->definition().title));
  setSourceCursor(selection, link, 0, link->definition().markerRange.start);
  require(input.deleteBackward(), "backspace should delete empty link definition");
  require(session.markdownText() == QStringLiteral("alpha"),
          QStringLiteral("empty link definition deletion mismatch: '%1'").arg(session.markdownText()));
}

void testOptionalLinkDefinitionTitleInsertionAddsQuotes() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[1]: url"), false);
  MarkdownNode* link = blockAt(session, 0);
  require(link->type() == BlockType::LinkDefinition, "link definition should parse");
  const DefinitionBlock definition = link->definition();
  require(definition.title.isEmpty(), "link definition should start without title");
  require(!definition.titleQuoted, "link definition should start without title quotes");
  HitTestResult titleHit;
  titleHit.blockId = link->id();
  titleHit.textNodeId = link->id();
  titleHit.textOffset = definition.titleRange.start - definition.markerRange.start;
  titleHit.sourceOffset = definition.titleRange.start;
  titleHit.zone = HitTestResult::Zone::Text;
  titleHit.definitionField = HitTestResult::DefinitionField::Title;
  selection.setHitResult(titleHit);

  require(input.insertText(QStringLiteral("Example")), "typing optional title should add quoted title");
  require(session.markdownText() == QStringLiteral("[1]: url  \"Example\""),
          QStringLiteral("optional title insertion mismatch: '%1'").arg(session.markdownText()));
}

void testDefinitionDestinationEditDoesNotRestoreStaleSourceText() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[ref]: <https://old.example/a b> \"\""), false);
  MarkdownNode* link = blockAt(session, 0);
  require(link->type() == BlockType::LinkDefinition, "link definition should parse");
  const DefinitionBlock definition = link->definition();
  setSourceSelection(
      selection,
      link,
      link,
      definition.destinationRange.start - definition.markerRange.start,
      definition.destinationRange.start,
      definition.destinationRange.end - definition.markerRange.start,
      definition.destinationRange.end);

  require(input.insertText(QStringLiteral("https://new.example/path")), "replacing destination should edit definition");
  require(session.markdownText() == QStringLiteral("[ref]: <https://new.example/path> \"\""),
          QStringLiteral("edited destination should keep angle destination and empty title shape: '%1'").arg(session.markdownText()));
  require(!session.markdownText().contains(QStringLiteral("https://old.example/a b")),
          QStringLiteral("stale sourceText destination should not be restored: '%1'").arg(session.markdownText()));
}

void testDefinitionTitleEditDoesNotRestoreSingleQuotedSourceText() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[ref]: url 'old title'"), false);
  MarkdownNode* link = blockAt(session, 0);
  require(link->type() == BlockType::LinkDefinition, "single quoted title definition should parse");
  const DefinitionBlock definition = link->definition();
  setSourceSelection(
      selection,
      link,
      link,
      definition.titleRange.start - definition.markerRange.start,
      definition.titleRange.start,
      definition.titleRange.end - definition.markerRange.start,
      definition.titleRange.end);

  require(input.insertText(QStringLiteral("new title")), "replacing title should edit definition");
  require(session.markdownText() == QStringLiteral("[ref]: url 'new title'"),
          QStringLiteral("edited title should keep single quote shape: '%1'").arg(session.markdownText()));
  require(!session.markdownText().contains(QStringLiteral("old title")),
          QStringLiteral("stale single-quoted title should not be restored: '%1'").arg(session.markdownText()));
}

void testFootnoteDefinitionEnterAtNoteEndCreatesParagraphAfter() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  session.setMarkdownText(QStringLiteral("[^1]: note"), false);
  MarkdownNode* footnote = blockAt(session, 0);
  require(footnote->type() == BlockType::FootnoteDefinition, "footnote should parse as definition block");
  const DefinitionBlock definition = footnote->definition();
  setSourceCursor(selection, footnote, definition.noteRange.end - definition.markerRange.start, definition.noteRange.end);

  require(input.insertParagraphBreak(), "enter at footnote note end should create paragraph after definition");
  require(session.markdownText() == QStringLiteral("[^1]: note\n\n"),
          QStringLiteral("footnote enter markdown mismatch: '%1'").arg(session.markdownText()));
  require(session.document().root().children().size() == 2, "footnote enter should create trailing empty paragraph");
  require(blockAt(session, 1)->type() == BlockType::Paragraph, "footnote enter should focus trailing paragraph");
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "footnote enter cursor should move to trailing paragraph");
}

void testEmptyHeadingBackspaceConvertsToParagraph() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Empty heading followed by a paragraph
  session.setMarkdownText(QStringLiteral("### \n\nafter"), false);
  require(session.document().root().children().size() == 2, "expected heading + paragraph");
  require(blockAt(session, 0)->type() == BlockType::Heading, "first block should be heading");
  setCursor(selection, blockAt(session, 0), 0);

  require(input.deleteBackward(), "backspace on empty heading should convert to paragraph");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "empty heading should become paragraph after backspace");
  require(!session.markdownText().contains(QLatin1Char('#')), "backspace should remove heading prefix");
  require(selection.cursorPosition().blockId == blockAt(session, 0)->id(), "cursor should stay on converted block");

  // Empty heading at document start
  session.setMarkdownText(QStringLiteral("## "), false);
  require(blockAt(session, 0)->type() == BlockType::Heading, "solo heading should be heading");
  setCursor(selection, blockAt(session, 0), 0);

  require(input.deleteBackward(), "backspace on solo empty heading should convert to paragraph");
  require(blockAt(session, 0)->type() == BlockType::Paragraph, "solo empty heading should become paragraph");
  require(session.markdownText().isEmpty() || !session.markdownText().contains(QLatin1Char('#')),
          "solo empty heading backspace should remove marker");

  // Non-empty heading should NOT convert — normal merge behavior
  session.setMarkdownText(QStringLiteral("## Title\n\nafter"), false);
  require(blockAt(session, 0)->type() == BlockType::Heading, "titled heading should be heading");
  setCursor(selection, blockAt(session, 0), 0);

  require(input.deleteBackward(), "backspace on non-empty heading start should not convert");
  require(blockAt(session, 0)->type() == BlockType::Heading, "non-empty heading should remain heading after backspace");
  require(session.markdownText().contains(QLatin1String("## Title")), "non-empty heading backspace should preserve marker");
}

void testEmptyHeadingDeleteRemovesBlock() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);

  // Empty heading followed by a paragraph
  session.setMarkdownText(QStringLiteral("before\n\n### \n\nafter"), false);
  require(session.document().root().children().size() == 3, "expected para + heading + para");
  require(blockAt(session, 1)->type() == BlockType::Heading, "middle block should be heading");
  setCursor(selection, blockAt(session, 1), 0);

  require(input.deleteForward(), "delete on empty heading should remove block");
  require(session.markdownText() == QStringLiteral("before\n\nafter"),
          QStringLiteral("empty heading delete text mismatch: '%1'").arg(session.markdownText()));
  require(selection.cursorPosition().blockId == blockAt(session, 1)->id(), "cursor should move to next paragraph");

  // Solo empty heading — no next block, delete is a no-op
  session.setMarkdownText(QStringLiteral("### "), false);
  require(blockAt(session, 0)->type() == BlockType::Heading, "solo empty heading should be heading");
  setCursor(selection, blockAt(session, 0), 0);

  require(input.deleteForward(), "delete on solo empty heading with no next block should be handled");
  require(session.markdownText() == QStringLiteral("### "),
          "solo empty heading delete with no next block should be a no-op");

  // Non-empty heading should NOT remove — normal merge behavior
  session.setMarkdownText(QStringLiteral("## Title\n\nafter"), false);
  require(blockAt(session, 0)->type() == BlockType::Heading, "titled heading should be heading");
  setCursor(selection, blockAt(session, 0), 6);

  require(input.deleteForward(), "delete at non-empty heading end should merge, not remove");
  require(session.markdownText().contains(QLatin1String("Title")), "non-empty heading delete should preserve content");
}

}  // namespace

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
  RUN_TEST(testInputEnterAtParagraphEdgesCreatesEditableEmptyParagraph);
  RUN_TEST(testLocalReparsePreservesUntouchedNodeIds);
  RUN_TEST(testBlockEditContextSeparatesBlockAndContentRanges);
  RUN_TEST(testTextBlockCommandBuilderCreatesStructuralEnterCommands);
  RUN_TEST(testInputMergeParagraphs);
  RUN_TEST(testInputBackspaceAtParagraphStartDeletesStructuralBoundary);
  RUN_TEST(testInputDeleteAtParagraphEndDeletesStructuralBoundary);
  RUN_TEST(testInputUndoRedoSnapshots);
  RUN_TEST(testControllerUndoRedoRemapsCursorAfterReparse);
  RUN_TEST(testInputSelectionReplaceAndDelete);
  RUN_TEST(testInputCrossParagraphSelectionReplaceAndDelete);
  RUN_TEST(testHeadingInput);
  RUN_TEST(testHeadingEnterAtStartInsertsParagraphBeforeBlock);
  RUN_TEST(testListItemInput);
  RUN_TEST(testListItemEditingCommands);
  RUN_TEST(testTabInRenderedTextInsertsZeroWidthSpace);
  RUN_TEST(testListTabFromRenderedClick);
  RUN_TEST(testInputEmptyCodeFenceBackspaceRemovesBlock);
  RUN_TEST(testInputEmptyCodeFenceDeleteRemovesBlock);
  RUN_TEST(testInputNonEmptyCodeFenceBackspaceDoesNotRemoveBlock);
  RUN_TEST(testDefinitionBlockFieldEditing);
  RUN_TEST(testEmptyDefinitionBackspaceDeletesBlock);
  RUN_TEST(testOptionalLinkDefinitionTitleInsertionAddsQuotes);
  RUN_TEST(testDefinitionDestinationEditDoesNotRestoreStaleSourceText);
  RUN_TEST(testDefinitionTitleEditDoesNotRestoreSingleQuotedSourceText);
  RUN_TEST(testFootnoteDefinitionEnterAtNoteEndCreatesParagraphAfter);
  RUN_TEST(testEmptyHeadingBackspaceConvertsToParagraph);
  RUN_TEST(testEmptyHeadingDeleteRemovesBlock);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
