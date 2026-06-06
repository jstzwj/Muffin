#pragma once

#include "blocks/literal/LiteralBlockController.h"
#include "document/MarkdownTypes.h"
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

class FrontMatterController final : public QObject {
  Q_OBJECT

public:
  explicit FrontMatterController(QObject* parent = nullptr);

  void setDocumentSession(DocumentSession* session);
  void setSelectionController(SelectionController* selection);
  void setUndoStack(UndoStack* undoStack);
  void setBrushQueue(BrushQueue* brushQueue);

  NodeId currentFrontMatterId() const;
  bool isEditing() const;
  bool enterEditMode();
  bool exitEditMode();

  bool insertText(QString text);
  bool deleteBackward();
  bool deleteForward();
  bool deleteSelection();
  bool setContent(QString content);
  bool insertFrontMatter(FrontMatterFormat format);
  QString tabText() const;

signals:
  void frontMatterCommandRejected(QString reason);

private:
  LiteralBlockController literal_;
  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
  UndoStack* undoStack_ = nullptr;
  BrushQueue* brushQueue_ = nullptr;
};

}  // namespace muffin
