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
  QString tabText() const;

signals:
  void htmlCommandRejected(QString reason);

private:
  LiteralBlockController literal_;
};

}  // namespace muffin
