#pragma once

#include "projection/SelectionSerializer.h"
#include "editor/EditorContextHolder.h"

#include <QObject>

namespace muffin {

class InputController;

class ClipboardController final : public QObject, private EditorContextHolder {
  Q_OBJECT

public:
  explicit ClipboardController(QObject* parent = nullptr);

  using EditorContextHolder::setContext;
  void setInputController(InputController* inputController);

  bool copy();
  bool cut();
  bool paste();
  bool copyAsPlainText();
  bool copyAsMarkdown();
  bool copyAsHtml();
  bool pasteAsPlainText();

private:
  InputController* inputController_ = nullptr;
  SelectionSerializer selectionSerializer_;
};

}  // namespace muffin
