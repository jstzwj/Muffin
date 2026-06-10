#pragma once

#include "../TestUtils.h"

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

#include <QApplication>
#include <QKeyEvent>

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

inline muffin::MarkdownNode* childAt(muffin::MarkdownNode* node, qsizetype index) {
  require(node != nullptr, "parent node should exist");
  const auto& children = node->children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "child index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

inline muffin::MarkdownNode* listItemAt(const muffin::DocumentSession& session, qsizetype listIndex, qsizetype itemIndex) {
  return childAt(blockAt(session, listIndex), itemIndex);
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

inline muffin::MarkdownNode* maybeFirstChildOfType(muffin::MarkdownNode* node, muffin::BlockType type) {
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

inline muffin::MarkdownNode* firstBlockOfType(const muffin::DocumentSession& session, muffin::BlockType type) {
  for (const auto& child : session.document().root().children()) {
    if (child->type() == type) {
      return child.get();
    }
  }
  require(false, "block type not found");
  return nullptr;
}

inline int listDepthForItem(const muffin::MarkdownNode* item) {
  int depth = 0;
  const muffin::MarkdownNode* node = item;
  while (node) {
    if (node->type() == muffin::BlockType::ListItem) {
      ++depth;
    }
    node = node->parent();
  }
  return depth;
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

inline muffin::CursorPosition inlineCursor(muffin::NodeId blockId, qsizetype textOffset, qsizetype sourceOffset) {
  muffin::CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = textOffset;
  cursor.text.sourceOffset = sourceOffset;
  return cursor;
}

inline bool pressKey(muffin::InputController& input, QObject* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  return input.eventFilter(target, &event);
}

inline void sendKey(QWidget* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(target, &event);
}

inline void wireInput(
    muffin::InputController& input,
    muffin::DocumentSession& session,
    muffin::SelectionController& selection,
    muffin::UndoStack& undoStack,
    muffin::BrushQueue& brushQueue,
    muffin::EditorView* view = nullptr,
    QHash<int, muffin::LiteralBlockController*> literalEditors = {}) {
  input.setContext({&session, &selection, &undoStack, &brushQueue, view, literalEditors});
}

inline void wireStyle(
    muffin::StylizeController& stylize,
    muffin::DocumentSession& session,
    muffin::SelectionController& selection,
    muffin::UndoStack& undoStack,
    muffin::BrushQueue& brushQueue) {
  stylize.setContext({&session, &selection, &undoStack, &brushQueue});
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

inline void setSourceSelection(
    muffin::SelectionController& selection,
    muffin::MarkdownNode* block,
    muffin::MarkdownNode* textNode,
    qsizetype anchorTextOffset,
    qsizetype anchorSourceOffset,
    qsizetype focusTextOffset,
    qsizetype focusSourceOffset) {
  muffin::SelectionRange range;
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

inline void setCrossSelection(
    muffin::SelectionController& selection,
    muffin::MarkdownNode* anchorBlock,
    qsizetype anchorOffset,
    muffin::MarkdownNode* focusBlock,
    qsizetype focusOffset) {
  muffin::SelectionRange range;
  range.anchor.blockId = anchorBlock->id();
  range.anchor.text.nodeId = anchorBlock->id();
  range.anchor.text.textOffset = anchorOffset;
  range.focus.blockId = focusBlock->id();
  range.focus.text.nodeId = focusBlock->id();
  range.focus.text.textOffset = focusOffset;
  selection.setSelection(range);
}

inline QString selectedMarkdown(const muffin::DocumentSession& session, const muffin::SelectionController& selection) {
  return muffin::SelectionSerializer().exportMarkdown(session.document(), selection.selection());
}

inline QString selectedPlainText(const muffin::DocumentSession& session, const muffin::SelectionController& selection) {
  return muffin::SelectionSerializer().exportPlainText(session.document(), selection.selection());
}

inline void wireClipboard(muffin::ClipboardController& clipboard, muffin::DocumentSession& session, muffin::SelectionController& selection, muffin::InputController& input) {
  clipboard.setContext({&session, &selection, nullptr, nullptr});
  clipboard.setInputController(&input);
}
