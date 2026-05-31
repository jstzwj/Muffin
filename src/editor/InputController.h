#pragma once

#include "document/NodeId.h"
#include "edit/EditTransaction.h"

#include <QObject>

class QKeyEvent;
class QInputMethodEvent;

namespace muffin {

class BrushQueue;
class CodeFenceController;
class DocumentSession;
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
  enum class Operation {
    InsertText,
    Backspace,
    Delete,
    Enter
  };

  struct ParagraphEditContext {
    MarkdownNode* node = nullptr;
    MarkdownNode* editableNode = nullptr;
    qsizetype sourceStart = -1;
    qsizetype sourceEnd = -1;
    qsizetype cursorOffset = 0;
    QString sourceText;
  };

  bool handleKeyPress(QKeyEvent* event);
  bool editParagraph(Operation operation, QString text = {});
  bool replaceSelection(QString text, EditTransaction::Kind kind, QString label);
  bool mergeWithPreviousParagraph(const ParagraphEditContext& context);
  bool mergeWithNextParagraph(const ParagraphEditContext& context);
  bool splitListItem(const ParagraphEditContext& context);
  bool exitListItem(const ParagraphEditContext& context);
  bool outdentListItem(const ParagraphEditContext& context);
  bool listItemLineBounds(const ParagraphEditContext& context, qsizetype& lineStart, qsizetype& contentStart, qsizetype& lineEnd) const;
  QString listMarkerFor(const QString& line) const;
  bool paragraphContext(ParagraphEditContext& context) const;
  bool paragraphContextFor(NodeId blockId, ParagraphEditContext& context) const;
  bool fillEditableContext(MarkdownNode& displayNode, ParagraphEditContext& context) const;
  bool selectionContext(ParagraphEditContext& context, qsizetype& start, qsizetype& end) const;
  bool selectionSourceRange(qsizetype& start, qsizetype& end) const;
  bool blockSelectionSourceRange(qsizetype& start, qsizetype& end) const;
  bool blockSourceRange(const MarkdownNode& node, qsizetype& start, qsizetype& end) const;
  bool isPlainParagraph(const MarkdownNode& node, const QString& sourceText) const;
  bool isPlainInlineEditable(const MarkdownNode& node, const QString& sourceText) const;
  MarkdownNode* primaryParagraph(MarkdownNode& node) const;
  MarkdownNode* previousPlainParagraph(const MarkdownNode& node, ParagraphEditContext& context) const;
  MarkdownNode* nextPlainParagraph(const MarkdownNode& node, ParagraphEditContext& context) const;
  qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column) const;
  qsizetype sourceOffsetForLineEnd(const QString& text, int line) const;
  CursorPosition cursorFor(NodeId blockId, qsizetype offset) const;
  CursorPosition cursorForNode(MarkdownNode& node, qsizetype offset) const;
  CursorPosition cursorForSourceOffset(qsizetype sourceOffset, bool preferLaterEmptyAtOffset = false) const;
  MarkdownNode* paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset) const;
  MarkdownNode* selectableBlockByDirection(NodeId current, int direction) const;
  qsizetype selectableTextLength(const MarkdownNode& node) const;
  bool moveCursorHorizontal(int direction, bool extendSelection);
  bool moveCursorVertical(int direction, bool extendSelection);
  void setCursorOrExtend(CursorPosition cursor, bool extendSelection);
  void applyEdit(EditTransaction::Kind kind, const QString& label, QString nextText, qsizetype nextSourceOffset);
  void applyEdit(EditTransaction::Kind kind, const QString& label, QString nextText, qsizetype nextSourceOffset, bool preferLaterEmptyAtOffset);
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
