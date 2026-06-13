#pragma once

#include "document/MarkdownTypes.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/CursorPosition.h"
#include "editor/EditorContext.h"

#include <QString>

#include <functional>

namespace muffin {

class MarkdownNode;

struct LiteralBlockSpec {
  BlockType blockType = BlockType::Unknown;
  HitTestResult::Zone hitZone = HitTestResult::Zone::None;
  QString rejectedReason;
  QString editLabel;
  QString backspaceLabel;
  QString deleteLabel;
  QString deleteSelectionLabel;
  QString setContentLabel;
  QString tabText;
};

class LiteralBlockController final {
public:
  using MutateFn = std::function<bool(MarkdownNode&, qsizetype&)>;
  using RejectedFn = std::function<void(QString)>;

  explicit LiteralBlockController(LiteralBlockSpec spec);

  void setContext(const EditorContext& ctx);
  void setRejectedHandler(RejectedFn handler);

  BlockType blockType() const;
  HitTestResult::Zone hitZone() const;
  QString tabText() const;

  NodeId currentBlockId() const;
  bool isEditing() const;
  bool enterEditMode();
  bool exitEditMode();

  bool insertText(QString text);
  bool deleteBackward();
  bool deleteForward();
  bool deleteSelection();
  bool setContent(QString content);
  bool mutateCurrentBlock(QString label, EditTransaction::Kind kind, const MutateFn& mutate);
  bool mutateBlock(NodeId requestedId, QString label, EditTransaction::Kind kind, const MutateFn& mutate);

  // Phantom trailing line for an indented code block. cmark strips a real trailing empty line,
  // so Enter at the end is held only in the node's literal as a trailing '\n' (rendered as a
  // blank line) and committed to the document once the next character lands on it. Because cmark
  // never produces a trailing newline on an indented-code literal, that '\n' IS the phantom — no
  // separate flag is needed, and the state self-heals on any re-parse (which rebuilds the literal
  // without it). See MarkdownSerializer for why the phantom is safe to keep on the node.
  bool hasPendingTrailingNewline() const;
  void clearPendingTrailingNewline();

  MarkdownNode* currentBlock() const;
  MarkdownNode* blockById(NodeId id) const;
  MarkdownNode* blockByIndex(int index) const;
  int blockIndexFor(NodeId id) const;
  CursorPosition cursorFor(NodeId id, qsizetype offset) const;
  bool currentSelectionRange(qsizetype& startOffset, qsizetype& endOffset) const;
  void setEditingBlock(NodeId id, int index);

private:
  bool shouldDeferTrailingNewline() const;
  bool deferTrailingNewline();
  LiteralBlockSpec spec_;
  RejectedFn rejectedHandler_;
  EditorContext ctx_;
  NodeId editingId_;
  int editingIndex_ = -1;
};

}  // namespace muffin
