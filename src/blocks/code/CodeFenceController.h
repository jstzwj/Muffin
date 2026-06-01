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

class CodeFenceController final : public QObject {
  Q_OBJECT

public:
  explicit CodeFenceController(QObject* parent = nullptr);

  void setDocumentSession(DocumentSession* session);
  void setSelectionController(SelectionController* selection);
  void setUndoStack(UndoStack* undoStack);
  void setBrushQueue(BrushQueue* brushQueue);

  NodeId currentCodeFenceId() const;
  bool isEditing() const;
  bool enterEditMode();
  bool exitEditMode();

  bool insertText(QString text);
  bool deleteBackward();
  bool deleteForward();
  bool deleteSelection();
  bool setLanguage(QString language);
  bool setLanguageFor(NodeId codeId, QString language);
  bool setContent(QString content);

signals:
  void codeCommandRejected(QString reason);

private:
  bool mutateCurrentCodeFence(QString label, EditTransaction::Kind kind, const std::function<bool(MarkdownNode&, qsizetype&)>& mutate);
  bool mutateCodeFence(
      NodeId requestedCodeId,
      QString label,
      EditTransaction::Kind kind,
      const std::function<bool(MarkdownNode&, qsizetype&)>& mutate);
  MarkdownNode* currentCodeFence() const;
  MarkdownNode* codeFenceById(NodeId id) const;
  MarkdownNode* codeFenceByIndex(int index) const;
  MarkdownNode* findCodeFenceById(MarkdownNode& node, NodeId id) const;
  int codeFenceIndexFor(NodeId id) const;
  CursorPosition cursorFor(NodeId codeId, qsizetype offset) const;
  bool currentSelectionRange(qsizetype& startOffset, qsizetype& endOffset) const;

  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
  UndoStack* undoStack_ = nullptr;
  BrushQueue* brushQueue_ = nullptr;
  NodeId editingCodeId_;
  int editingCodeIndex_ = -1;
};

}  // namespace muffin
