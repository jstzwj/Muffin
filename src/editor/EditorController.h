#pragma once

#include "document/DocumentSession.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/literal/LiteralBlockController.h"
#include "blocks/table/TableController.h"
#include "commands/ParagraphController.h"
#include "commands/StylizeController.h"
#include "document/MarkdownTypes.h"
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
  LiteralBlockController& frontMatterLiteral();
  CodeFenceController& codeFenceController();
  LiteralBlockController& htmlLiteral();
  LiteralBlockController& mathLiteral();
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

  bool insertFrontMatter(FrontMatterFormat format);
  QString sanitizedHtmlPreview() const;

signals:
  void cursorChanged(HitTestResult hit);
  void stateChanged();
  void frontMatterCommandRejected(QString reason);
  void htmlCommandRejected(QString reason);
  void mathCommandRejected(QString reason);

private:
  void exitAllLiteralEditModes();
  bool enterLiteralEditMode(HitTestResult::Zone zone);
  LiteralBlockController* literalForZone(HitTestResult::Zone zone);
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
  LiteralBlockController frontMatterLiteral_;
  CodeFenceController codeFenceController_;
  LiteralBlockController htmlLiteral_;
  LiteralBlockController mathLiteral_;
  TableController tableController_;
  ClipboardController clipboardController_;
};

}  // namespace muffin
