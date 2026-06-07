#pragma once

#include "document/SelectionSerializer.h"
#include "editor/EditorContext.h"

#include <QObject>

namespace muffin {

class InputController;

class ClipboardController final : public QObject {
  Q_OBJECT

public:
  explicit ClipboardController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);
  void setInputController(InputController* inputController);

  bool copy();
  bool cut();
  bool paste();
  bool copyAsPlainText();
  bool copyAsMarkdown();
  bool copyAsHtml();
  bool pasteAsPlainText();

private:
  EditorContext ctx_;
  InputController* inputController_ = nullptr;
  SelectionSerializer selectionSerializer_;
};

}  // namespace muffin
