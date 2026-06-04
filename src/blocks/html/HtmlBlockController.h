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

class HtmlBlockController final : public QObject {
  Q_OBJECT

public:
  explicit HtmlBlockController(QObject* parent = nullptr);

  void setDocumentSession(DocumentSession* session);
  void setSelectionController(SelectionController* selection);
  void setUndoStack(UndoStack* undoStack);
  void setBrushQueue(BrushQueue* brushQueue);

  NodeId currentHtmlBlockId() const;
  bool isEditing() const;
  bool enterEditMode();
  bool exitEditMode();

  bool insertText(QString text);
  bool deleteBackward();
  bool deleteForward();
  bool deleteSelection();
  bool setHtml(QString html);
  QString sanitizedPreview() const;

signals:
  void htmlCommandRejected(QString reason);

private:
  bool mutateCurrentHtmlBlock(QString label, EditTransaction::Kind kind, const std::function<bool(MarkdownNode&, qsizetype&)>& mutate);
  bool currentSelectionRange(qsizetype& startOffset, qsizetype& endOffset) const;
  MarkdownNode* currentHtmlBlock() const;
  MarkdownNode* htmlBlockById(NodeId id) const;
  MarkdownNode* htmlBlockByIndex(int index) const;
  MarkdownNode* findHtmlBlockById(MarkdownNode& node, NodeId id) const;
  int htmlBlockIndexFor(NodeId id) const;
  CursorPosition cursorFor(NodeId htmlId, qsizetype offset) const;

  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
  UndoStack* undoStack_ = nullptr;
  BrushQueue* brushQueue_ = nullptr;
  NodeId editingHtmlId_;
  int editingHtmlIndex_ = -1;
};

}  // namespace muffin
