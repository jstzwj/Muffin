#pragma once

#include <QObject>

namespace muffin {

class InputController;

class ClipboardController final : public QObject {
  Q_OBJECT

public:
  explicit ClipboardController(QObject* parent = nullptr);

  void setInputController(InputController* inputController);

  bool copy();
  bool cut();
  bool paste();

private:
  InputController* inputController_ = nullptr;
};

}  // namespace muffin
