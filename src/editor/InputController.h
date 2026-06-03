#pragma once

#include "app/DocumentSession.h"
#include "document/NodeId.h"
#include "edit/EditTransaction.h"
#include "editor/TextBlockCommandBuilder.h"

#include <QObject>
#include <QVector>

class QKeyEvent;
class QInputMethodEvent;

namespace muffin {

class BrushQueue;
class CodeFenceController;
class EditorView;
class HtmlBlockController;
class MathBlockController;
class MarkdownNode;
class SelectionController;
class TableController;
class UndoStack;

class InputController final : public QObject {
  Q_OBJECT

public:
  explicit InputController(QObject* parent = nullptr);

  void setDocumentSession(DocumentSession* session);
  void setSelectionController(SelectionController* selection);
  void setUndoStack(UndoStack* undoStack);
  void setBrushQueue(BrushQueue* brushQueue);
  void setTableController(TableController* tableController);
  void setCodeFenceController(CodeFenceController* codeFenceController);
  void setHtmlBlockController(HtmlBlockController* htmlBlockController);
  void setMathBlockController(MathBlockController* mathBlockController);
  void attach(EditorView* view);

  bool insertText(QString text);
  bool insertParagraphBreak();
  bool deleteBackward();
  bool deleteForward();
  bool indentListItem();
  bool outdentListItem();
  bool deleteSelection();
  bool hasEditableSelection() const;
  bool handleInputMethod(QInputMethodEvent* event);

  bool eventFilter(QObject* watched, QEvent* event) override;

signals:
  void unsupportedEditRequested(QString reason);

private:
  bool handleKeyPress(QKeyEvent* event);
  bool editParagraph(TextBlockCommandBuilder::Operation operation, QString text = {});
  bool applyTextCommand(const TextBlockCommandBuilder::Command& command);
  bool replaceSelection(QString text, EditTransaction::Kind kind, QString label);
  bool tryRemoveExactWholeBlockSelection(EditTransaction::Kind kind, const QString& label);
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
      bool preferLaterEmptyAtOffset = false);
  void applyEdit(
      EditTransaction::Kind kind,
      const QString& label,
      QString nextText,
      CursorPosition preferredCursor,
      qsizetype fallbackSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {},
      bool preferLaterEmptyAtOffset = false);
  QString printableText(QKeyEvent* event) const;

  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
  UndoStack* undoStack_ = nullptr;
  BrushQueue* brushQueue_ = nullptr;
  CodeFenceController* codeFenceController_ = nullptr;
  HtmlBlockController* htmlBlockController_ = nullptr;
  MathBlockController* mathBlockController_ = nullptr;
  TableController* tableController_ = nullptr;
  EditorView* view_ = nullptr;
};

}  // namespace muffin
