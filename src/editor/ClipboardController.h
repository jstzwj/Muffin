#pragma once

#include "document/SelectionSerializer.h"

#include <QObject>

namespace muffin {

class DocumentSession;
class InputController;
class SelectionController;

class ClipboardController final : public QObject {
  Q_OBJECT

public:
  explicit ClipboardController(QObject* parent = nullptr);

  void setDocumentSession(DocumentSession* session);
  void setSelectionController(SelectionController* selectionController);
  void setInputController(InputController* inputController);

  bool copy();
  bool cut();
  bool paste();

private:
  DocumentSession* session_ = nullptr;
  SelectionController* selectionController_ = nullptr;
  InputController* inputController_ = nullptr;
  SelectionSerializer selectionSerializer_;
};

}  // namespace muffin
