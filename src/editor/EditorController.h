#pragma once

#include "app/DocumentSession.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/frontmatter/FrontMatterController.h"
#include "blocks/html/HtmlBlockController.h"
#include "blocks/math/MathBlockController.h"
#include "blocks/table/TableController.h"
#include "commands/ParagraphController.h"
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
  FrontMatterController& frontMatterController();
  CodeFenceController& codeFenceController();
  HtmlBlockController& htmlBlockController();
  MathBlockController& mathBlockController();
  TableController& tableController();
  ClipboardController& clipboardController();
  ParagraphController& paragraphController();
  BrushQueue& brushQueue();

  bool canUndo() const;
  bool canRedo() const;
  void undo();
  void redo();
  bool selectAll();
  bool selectCurrentBlock();
  bool selectCurrentFormatSpan();
  bool moveBlockUp();
  bool moveBlockDown();
  void clearHistoryAndSelection();
  void activateHit(HitTestResult hit);

signals:
  void cursorChanged(HitTestResult hit);
  void stateChanged();

private:
  void exitAllLiteralEditModes();
  bool enterLiteralEditMode(HitTestResult::Zone zone);
  void applySnapshot(const DocumentSnapshot& snapshot);
  void applyTransaction(const EditTransaction& transaction, bool undo);
  CursorPosition remapSnapshotCursor(const CursorPosition& snapshotCursor) const;
  bool selectWordAtCursor(const BlockEditContext& context);
  bool swapTopLevelBlocks(MarkdownNode& upper, MarkdownNode& lower);

  DocumentSession* session_ = nullptr;
  EditorView* view_ = nullptr;
  SelectionController selection_;
  UndoStack undoStack_;
  BrushQueue brushQueue_;
  InputController inputController_;
  StylizeController stylizeController_;
  ParagraphController paragraphController_;
  FrontMatterController frontMatterController_;
  CodeFenceController codeFenceController_;
  HtmlBlockController htmlBlockController_;
  MathBlockController mathBlockController_;
  TableController tableController_;
  ClipboardController clipboardController_;
};

}  // namespace muffin
