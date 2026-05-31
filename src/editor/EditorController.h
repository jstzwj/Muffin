#pragma once

#include "app/DocumentSession.h"
#include "commands/StylizeController.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/ClipboardController.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"

#include <QObject>

namespace muffin {

class EditorView;

class EditorController final : public QObject {
  Q_OBJECT

public:
  explicit EditorController(QObject* parent = nullptr);

  void attach(DocumentSession* session, EditorView* view);
  void detach();

  SelectionController& selection();
  const SelectionController& selection() const;
  UndoStack& undoStack();
  const UndoStack& undoStack() const;
  InputController& inputController();
  StylizeController& stylizeController();
  ClipboardController& clipboardController();
  BrushQueue& brushQueue();

  bool canUndo() const;
  bool canRedo() const;
  void undo();
  void redo();
  bool toggleBold();
  bool toggleItalic();
  bool toggleCode();
  bool insertLink();
  bool copy();
  bool cut();
  bool paste();
  void clearHistoryAndSelection();

signals:
  void cursorChanged(HitTestResult hit);
  void stateChanged();

private:
  void applySnapshot(const DocumentSnapshot& snapshot);

  DocumentSession* session_ = nullptr;
  EditorView* view_ = nullptr;
  SelectionController selection_;
  UndoStack undoStack_;
  BrushQueue brushQueue_;
  InputController inputController_;
  StylizeController stylizeController_;
  ClipboardController clipboardController_;
};

}  // namespace muffin
