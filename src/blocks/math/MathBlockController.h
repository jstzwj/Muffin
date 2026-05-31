#pragma once

#include "document/NodeId.h"
#include "edit/EditTransaction.h"

#include <QObject>

#include <functional>

namespace muffin {

class BrushQueue;
class DocumentSession;
class MarkdownNode;
class SelectionController;
class UndoStack;

class MathBlockController final : public QObject {
  Q_OBJECT

public:
  explicit MathBlockController(QObject* parent = nullptr);

  void setDocumentSession(DocumentSession* session);
  void setSelectionController(SelectionController* selection);
  void setUndoStack(UndoStack* undoStack);
  void setBrushQueue(BrushQueue* brushQueue);

  NodeId currentMathBlockId() const;
  bool isEditing() const;
  bool enterEditMode();
  bool exitEditMode();

  bool insertText(QString text);
  bool deleteBackward();
  bool deleteForward();
  bool setTex(QString tex);

signals:
  void mathCommandRejected(QString reason);

private:
  bool mutateCurrentMathBlock(QString label, EditTransaction::Kind kind, const std::function<bool(MarkdownNode&, qsizetype&)>& mutate);
  MarkdownNode* currentMathBlock() const;
  MarkdownNode* mathBlockById(NodeId id) const;
  MarkdownNode* mathBlockByIndex(int index) const;
  MarkdownNode* findMathBlockById(MarkdownNode& node, NodeId id) const;
  int mathBlockIndexFor(NodeId id) const;
  CursorPosition cursorFor(NodeId mathId, qsizetype offset) const;

  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
  UndoStack* undoStack_ = nullptr;
  BrushQueue* brushQueue_ = nullptr;
  NodeId editingMathId_;
  int editingMathIndex_ = -1;
};

}  // namespace muffin
