#pragma once

#include "app/DocumentSession.h"
#include "document/MarkdownTypes.h"
#include "edit/EditTransaction.h"

#include <QObject>

namespace muffin {

class BrushQueue;
class DocumentSession;
class MarkdownNode;
class SelectionController;
class UndoStack;

class StylizeController final : public QObject {
  Q_OBJECT

public:
  explicit StylizeController(QObject* parent = nullptr);

  void setDocumentSession(DocumentSession* session);
  void setSelectionController(SelectionController* selection);
  void setUndoStack(UndoStack* undoStack);
  void setBrushQueue(BrushQueue* brushQueue);

  bool toggleBold();
  bool toggleItalic();
  bool toggleCode();
  bool insertLink();

signals:
  void unsupportedStyleRequested(QString reason);

private:
  struct ParagraphStyleContext {
    MarkdownNode* node = nullptr;
    MarkdownNode* editableNode = nullptr;
    qsizetype sourceStart = -1;
    qsizetype sourceEnd = -1;
    qsizetype anchorOffset = 0;
    qsizetype focusOffset = 0;
    QString sourceText;
  };

  bool wrapOrInsert(QString openMarker, QString closeMarker, QString placeholder, EditTransaction::Kind kind, QString label);
  bool wrapMultiBlockSelection(QString openMarker, QString closeMarker, EditTransaction::Kind kind, QString label);
  bool paragraphContext(ParagraphStyleContext& context) const;
  bool paragraphContextFor(NodeId blockId, const SelectionRange& selection, ParagraphStyleContext& context, bool requirePlainInline = true) const;
  bool fillEditableContext(
      MarkdownNode& displayNode,
      const SelectionRange& selection,
      ParagraphStyleContext& context,
      bool requirePlainInline = true) const;
  bool isPlainInlineEditable(const MarkdownNode& node, const QString& sourceText) const;
  MarkdownNode* primaryParagraph(MarkdownNode& node) const;
  qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column) const;
  qsizetype sourceOffsetForLineEnd(const QString& text, int line) const;
  bool applyStyleDelta(
      EditTransaction::Kind kind,
      const QString& label,
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      qsizetype nextAnchorSourceOffset,
      qsizetype nextFocusSourceOffset,
      QVector<LocalEditNodeHint> nodeHints = {});
  CursorPosition cursorForSourceOffset(qsizetype sourceOffset) const;
  MarkdownNode* paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset) const;

  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
  UndoStack* undoStack_ = nullptr;
  BrushQueue* brushQueue_ = nullptr;
};

}  // namespace muffin
