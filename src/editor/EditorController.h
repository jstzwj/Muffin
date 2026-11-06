#pragma once

#include "app/RenderCommandFacade.h"
#include "document/DocumentSession.h"
#include "blocks/code/CodeFenceController.h"
#include "blocks/code/CodeFenceScrollController.h"
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
#include "Export.h"
#include "document/BlockStructure.h"

#include <QObject>

namespace muffin {

class EditorView;

class MUFFIN_UI_EXPORT EditorController final : public QObject {
  Q_OBJECT

public:
  explicit EditorController(QObject* parent = nullptr);

  void attach(DocumentSession* session, EditorView* view);
  void detach();

  DocumentSession* session() const;
  SelectionController& selection();
  const SelectionController& selection() const;
  UndoStack& undoStack();
  const UndoStack& undoStack() const;
  InputController& inputController();
  StylizeController& stylizeController();
  LiteralBlockController& frontMatterLiteral();
  CodeFenceController& codeFenceController();
  CodeFenceScrollController& codeFenceScroll();
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
  bool selectCurrentWord();
  // Caret navigation for the Edit ▸ Select ▸ "Jump to" group.
  bool moveDocumentStart();
  bool moveDocumentEnd();
  bool moveBlockStart();       // start of the current block's text (≈ line start)
  bool moveBlockEnd();         // end of the current block's text (≈ line end)
  bool selectNextOccurrence(); // select the next occurrence of the current selection/word
  // Apply a full-document text replacement (re-parse) as ONE undoable step. Use this instead of
  // session->applyMarkdownText for batch operations (Find/Replace, image transforms, Upload/Move
  // All Images) so they land on the undo stack — otherwise Ctrl+Z skips them entirely.
  void applyMarkdownTextWithUndo(QString text, const QString& label);
  bool replaceSourceRangeWithUndo(qsizetype start, qsizetype length, const QString& text, const QString& label);
  // Select the whole current top-level block's source range (markers included)
  // so a subsequent delete removes the entire block via the exact-whole-block
  // path. Returns false on the trailing caret to avoid deleting the last block.
  bool selectCurrentBlockForRemoval();
  CursorFormatState queryCursorFormatState() const;
  CursorCommandState queryCursorCommandState() const;
  bool moveBlockUp();
  bool moveBlockDown();
  enum class BlockAction {
    Copy,
    Cut,
    Delete,
    Duplicate,
    InsertBefore,
    InsertAfter,
    MoveUp,
    MoveDown,
    Paragraph,
    Heading1,
    Heading2,
    Heading3,
    Heading4,
    Heading5,
    Heading6,
    BulletList,
    OrderedList,
    TaskList,
    Quote,
    Code
  };
  BlockTarget blockTarget(NodeId id) const;
  bool canPerformBlockAction(BlockTarget target, BlockAction action) const;
  bool performBlockAction(BlockTarget target, BlockAction action);
  bool moveBlock(BlockTarget source, BlockTarget destination, bool after);
  void clearHistoryAndSelection();
  void activateHit(HitTestResult hit);

  bool insertFrontMatter(FrontMatterFormat format);
  bool enterHtmlEditMode();
  bool exitHtmlEditMode();
  QString sanitizedHtmlPreview() const;
  bool isOnImage() const;
  QString imageSrcAtCursor() const;
  bool imageSourceRangeAtCursor(qsizetype& outStart, qsizetype& outEnd) const;

  // Accessibility seam (EditorAccessibility adapters): the accessible-text offsets ARE the
  // document's UTF-16 source offsets, so these map between the QAccessibleTextInterface
  // contract and the existing cursor machinery.
  bool selectionSourceRange(qsizetype& start, qsizetype& end) const;
  bool selectSourceRange(qsizetype start, qsizetype end);
  CursorPosition cursorForSourceOffset(qsizetype sourceOffset) const;
  void setCursorForSourceOffset(qsizetype sourceOffset);

signals:
  void historySelectionRestored(qsizetype anchor, qsizetype cursor);
  void cursorChanged(HitTestResult hit);
  void stateChanged();
  void frontMatterCommandRejected(QString reason);
  void htmlCommandRejected(QString reason);
  void mathCommandRejected(QString reason);

private:
  void finishHistoryReplay(const EditTransaction& transaction, bool undo);
  void remapCurrentSelection();
  bool activateHtmlSource(CursorPosition initialPosition);
  void deactivateLiteralEditors();
  void exitAllLiteralEditModes();
  MarkdownNode* htmlImageBlockAtCursor() const;
  bool enterLiteralEditMode(HitTestResult::Zone zone);
  LiteralBlockController* literalForZone(HitTestResult::Zone zone);
  bool applySnapshot(const DocumentSnapshot& snapshot);
  // Returns true when the transaction's state change landed (directly or via the
  // full-reparse fallback); false when nothing was applied and the caller must
  // restore the transaction to its original undo/redo stack.
  bool applyTransaction(const EditTransaction& transaction, bool undo);
  CursorPosition remapSnapshotCursor(const CursorPosition& snapshotCursor) const;
  bool selectWordAtCursor(const BlockEditContext& context);
  bool applyBlockPlan(BlockSourcePlan plan, const QString& label, bool preserveReading);

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
  CodeFenceScrollController codeFenceScroll_;
  LiteralBlockController htmlLiteral_;
  LiteralBlockController mathLiteral_;
  TableController tableController_;
  ClipboardController clipboardController_;
};

} // namespace muffin
