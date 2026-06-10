#pragma once

#include "../TestUtils.h"

#include "document/DocumentSession.h"
#include "commands/ParagraphController.h"
#include "document/MarkdownNode.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/SelectionController.h"

#include <QApplication>

#include <cstdlib>
#include <iostream>

inline muffin::EditTransaction requireTextDeltaCommand(muffin::UndoStack& stack, const char* message) {
  require(stack.canUndo(), "expected undo command");
  muffin::EditTransaction transaction = stack.takeUndo();
  require(transaction.isTextDeltaCommand(), message);
  require(transaction.textDeltaCommand().isValid(), "text delta command should be valid");
  return transaction;
}

inline muffin::MarkdownNode* blockAt(const muffin::DocumentSession& session, qsizetype index) {
  const auto& children = session.document().root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "block index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

inline muffin::MarkdownNode* firstChildOfType(muffin::MarkdownNode* node, muffin::BlockType type) {
  require(node != nullptr, "parent node should exist");
  for (const auto& child : node->children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  require(false, "child type not found");
  return nullptr;
}

inline void setCursor(muffin::SelectionController& selection, muffin::MarkdownNode* block, qsizetype offset) {
  muffin::CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = offset;
  selection.setCursorPosition(cursor);
}

inline void setSourceCursor(muffin::SelectionController& selection, muffin::MarkdownNode* block, qsizetype visibleOffset, qsizetype sourceOffset) {
  muffin::CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = visibleOffset;
  cursor.text.sourceOffset = sourceOffset;
  selection.setCursorPosition(cursor);
}

inline void setSelection(muffin::SelectionController& selection, muffin::MarkdownNode* block, qsizetype anchor, qsizetype focus) {
  muffin::SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = block->id();
  range.anchor.text.textOffset = anchor;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = block->id();
  range.focus.text.textOffset = focus;
  selection.setSelection(range);
}

inline void setSourceSelection(muffin::SelectionController& selection, muffin::MarkdownNode* block,
                        qsizetype anchorText, qsizetype anchorSource,
                        qsizetype focusText, qsizetype focusSource) {
  muffin::SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = block->id();
  range.anchor.text.textOffset = anchorText;
  range.anchor.text.sourceOffset = anchorSource;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = block->id();
  range.focus.text.textOffset = focusText;
  range.focus.text.sourceOffset = focusSource;
  selection.setSelection(range);
}

inline void wireParagraph(
    muffin::ParagraphController& paragraph,
    muffin::DocumentSession& session,
    muffin::SelectionController& selection,
    muffin::UndoStack& undoStack,
    muffin::BrushQueue& brushQueue) {
  muffin::EditorContext ctx;
  ctx.session = &session;
  ctx.selection = &selection;
  ctx.undoStack = &undoStack;
  ctx.brushQueue = &brushQueue;
  paragraph.setContext(ctx);
}
