#pragma once

#include "blocks/literal/LiteralBlockController.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/EditorContext.h"

#include <QObject>

#include <functional>

namespace muffin {

class MathBlockController final : public QObject {
  Q_OBJECT

public:
  explicit MathBlockController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);

  NodeId currentMathBlockId() const;
  bool isEditing() const;
  bool enterEditMode();
  bool exitEditMode();

  bool insertText(QString text);
  bool deleteBackward();
  bool deleteForward();
  bool deleteSelection();
  bool setTex(QString tex);
  QString tabText() const;

signals:
  void mathCommandRejected(QString reason);

private:
  LiteralBlockController literal_;
};

}  // namespace muffin
