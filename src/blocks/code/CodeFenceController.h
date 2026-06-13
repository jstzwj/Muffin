#pragma once

#include "blocks/literal/LiteralBlockController.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/EditorContext.h"

#include <QObject>

#include <functional>

namespace muffin {

class CodeFenceController final : public QObject {
  Q_OBJECT

public:
  explicit CodeFenceController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);

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
  bool hasPendingTrailingNewline() const;
  void clearPendingTrailingNewline();
  QString tabText() const;

signals:
  void codeCommandRejected(QString reason);

private:
  bool setLanguageForCodeFence(NodeId requestedCodeId, QString language);

  LiteralBlockController literal_;
  EditorContext ctx_;
};

}  // namespace muffin
