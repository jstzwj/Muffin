#pragma once

#include "blocks/literal/LiteralBlockController.h"
#include "document/MarkdownTypes.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/EditorContext.h"

#include <QObject>

#include <functional>

namespace muffin {

class FrontMatterController final : public QObject {
  Q_OBJECT

public:
  explicit FrontMatterController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);

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
  EditorContext ctx_;
};

}  // namespace muffin
