#pragma once

#include "blocks/literal/LiteralBlockController.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/EditorContext.h"

#include <QObject>

#include <functional>

namespace muffin {

class HtmlBlockController final : public QObject {
  Q_OBJECT

public:
  explicit HtmlBlockController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);

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
