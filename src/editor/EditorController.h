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
  BrushQueue& brushQueue();

  bool canUndo() const;
  bool canRedo() const;
  void undo();
  void redo();
  bool toggleBold();
  bool toggleItalic();
  bool toggleCode();
  bool insertLink();
  bool insertTableRowBefore();
  bool insertTableRowAfter();
  bool deleteTableRow();
  bool moveTableRowUp();
  bool moveTableRowDown();
  bool insertTableColumnBefore();
  bool insertTableColumnAfter();
  bool deleteTableColumn();
  bool moveTableColumnLeft();
  bool moveTableColumnRight();
  bool setTableColumnAlignment(TableAlignment alignment);
  bool resizeTable(int rows, int columns);
  bool deleteTable();
  bool insertTable();
  bool insertFrontMatter(FrontMatterFormat format);
  bool enterFrontMatterEditMode();
  bool exitFrontMatterEditMode();
  bool enterCodeFenceEditMode();
  bool exitCodeFenceEditMode();
  bool setCodeFenceLanguage(QString language);
  bool setCodeFenceLanguage(NodeId codeId, QString language);
  bool enterHtmlBlockEditMode();
  bool exitHtmlBlockEditMode();
  bool setHtmlBlockSource(QString html);
  bool enterMathBlockEditMode();
  bool exitMathBlockEditMode();
  bool setMathBlockTex(QString tex);

  // Paragraph-level commands
  ParagraphController& paragraphController();
  bool setHeadingLevel(int level);
  bool promoteHeading();
  bool demoteHeading();
  bool insertFormulaBlock();
  bool insertCodeBlock();
  bool insertLinkReference();
  bool toggleQuote();
  bool convertToOrderedList();
  bool convertToUnorderedList();
  bool convertToTaskList();
  bool insertParagraphBefore();
  bool insertParagraphAfter();

  bool copy();
  bool cut();
  bool paste();
  bool selectAll();
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
