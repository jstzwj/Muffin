#pragma once

#include "document/MarkdownTypes.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/CursorPosition.h"

#include <QString>

#include <functional>

namespace muffin {

class BrushQueue;
class DocumentSession;
class MarkdownNode;
class SelectionController;
class UndoStack;

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

  void setDocumentSession(DocumentSession* session);
  void setSelectionController(SelectionController* selection);
  void setUndoStack(UndoStack* undoStack);
  void setBrushQueue(BrushQueue* brushQueue);
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

  MarkdownNode* currentBlock() const;
  MarkdownNode* blockById(NodeId id) const;
  MarkdownNode* blockByIndex(int index) const;
  int blockIndexFor(NodeId id) const;
  CursorPosition cursorFor(NodeId id, qsizetype offset) const;
  bool currentSelectionRange(qsizetype& startOffset, qsizetype& endOffset) const;
  void setEditingBlock(NodeId id, int index);

private:
  LiteralBlockSpec spec_;
  RejectedFn rejectedHandler_;
  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
  UndoStack* undoStack_ = nullptr;
  BrushQueue* brushQueue_ = nullptr;
  NodeId editingId_;
  int editingIndex_ = -1;
};

}  // namespace muffin
