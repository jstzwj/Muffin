#pragma once

#include "document/DocumentSession.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/EditorContext.h"
#include "editor/TextBlockCommandBuilder.h"

#include <QObject>
#include <QVector>

class QKeyEvent;
class QInputMethodEvent;

namespace muffin {

class CodeFenceController;
class EditorView;
class LiteralBlockController;
class MarkdownNode;
class SelectionController;
class TableController;

class InputController final : public QObject {
  Q_OBJECT

public:
  explicit InputController(QObject* parent = nullptr);

  void setContext(const EditorContext& ctx);
  void setTableController(TableController* tableController);
  void setCodeFenceController(CodeFenceController* codeFenceController);

  bool insertText(QString text);
  bool insertParagraphBreak();
  bool insertBlockAfterCurrentBlock(QString text = {});
  bool deleteBackward();
  bool deleteForward();
  bool indentListItem();
  bool outdentListItem();
  bool deleteSelection();
  bool hasEditableSelection() const;
  bool handleInputMethod(QInputMethodEvent* event);

  void performLocalEdit(
      EditTransaction::Kind kind,
      const QString& label,
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      CursorPosition preferredCursor,
      qsizetype fallbackSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {},
      bool preferLaterEmptyAtOffset = false,
      bool structureEdit = false);

  bool eventFilter(QObject* watched, QEvent* event) override;

  // Exit the active literal editor when the caret is no longer on its block. Called after
  // command-driven cursor moves (e.g. insert-paragraph-before/after) so the caret and the next
  // keystroke follow the new position instead of being trapped in the literal block.
  void reconcileLiteralEditorForCursor();

signals:
  void unsupportedEditRequested(QString reason);
  void selectAllRequested();

private:
  bool handleKeyPress(QKeyEvent* event);
  bool hasActiveLiteralEditor() const;
  void syncLiteralEditMode(NodeId newBlockId);
  bool insertTextIntoActiveLiteral(QString text);
  bool tryInsertOptionalDefinitionTitle(QString text);
  bool deleteBackwardInActiveLiteral();
  bool deleteForwardInActiveLiteral();
  bool deleteSelectionInActiveLiteral();
  bool exitActiveLiteralEditor();
  QString activeLiteralTabText() const;
  bool insertIntoEmptyDocument(QString text);
  bool shouldIndentListItemFromKeyboard() const;
  bool editParagraph(TextBlockCommandBuilder::Operation operation, QString text = {});
  bool applyTextCommand(const TextBlockCommandBuilder::Command& command);
  bool replaceSelection(QString text, EditTransaction::Kind kind, QString label);
  bool tryRemoveExactWholeBlockSelection(EditTransaction::Kind kind, const QString& label);
  bool tryRemoveEmptyLiteralBlock(EditTransaction::Kind kind, const QString& label);
  bool tryRemoveEmptyDefinitionBlock(EditTransaction::Kind kind, const QString& label);
  bool collapseTrailingCaretToEndOfLastBlock();
  bool selectionSourceRange(qsizetype& start, qsizetype& end) const;
  bool blockSelectionSourceRange(qsizetype& start, qsizetype& end) const;
  BlockEditContextResolver contextResolver() const;
  CursorPosition cursorFor(NodeId blockId, qsizetype offset) const;
  CursorPosition cursorForNode(MarkdownNode& node, qsizetype offset) const;
  CursorPosition cursorForSourceOffset(qsizetype sourceOffset, bool preferLaterEmptyAtOffset = false) const;
  CursorPosition cursorAfterEdit(CursorPosition preferredCursor, qsizetype fallbackSourceOffset, bool preferLaterEmptyAtOffset = false) const;
  MarkdownNode* paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset, bool preferLaterEmptyAtOffset = false) const;
  MarkdownNode* selectableBlockByDirection(NodeId current, int direction) const;
  qsizetype selectableTextLength(const MarkdownNode& node) const;
  bool moveCursorHorizontal(int direction, bool extendSelection);
  bool moveCursorVertical(int direction, bool extendSelection);
  void setCursorOrExtend(CursorPosition cursor, bool extendSelection);
  void applyEdit(EditTransaction::Kind kind, const QString& label, QString nextText, qsizetype nextSourceOffset);
  void applyEdit(EditTransaction::Kind kind, const QString& label, QString nextText, qsizetype nextSourceOffset, bool preferLaterEmptyAtOffset);
  void applyLocalEdit(
      EditTransaction::Kind kind,
      const QString& label,
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      CursorPosition preferredCursor,
      qsizetype fallbackSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {},
      bool preferLaterEmptyAtOffset = false,
      bool structureEdit = false);
  void applyEdit(
      EditTransaction::Kind kind,
      const QString& label,
      QString nextText,
      CursorPosition preferredCursor,
      qsizetype fallbackSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {},
      bool preferLaterEmptyAtOffset = false);
  QString printableText(QKeyEvent* event) const;

  LiteralBlockController* activeLiteralEditor() const;

  EditorContext ctx_;
  CodeFenceController* codeFenceController_ = nullptr;
  TableController* tableController_ = nullptr;
};

}  // namespace muffin
