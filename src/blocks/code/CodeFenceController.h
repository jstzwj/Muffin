#pragma once

#include "blocks/literal/LiteralBlockController.h"
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
  QString tabText() const;

signals:
  void codeCommandRejected(QString reason);

private:
  bool setLanguageForCodeFence(NodeId requestedCodeId, QString language);

  LiteralBlockController literal_;
  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
  UndoStack* undoStack_ = nullptr;
  BrushQueue* brushQueue_ = nullptr;
};

}  // namespace muffin
