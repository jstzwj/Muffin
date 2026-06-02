#include "commands/StylizeController.h"

#include "app/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"

#include <algorithm>

namespace muffin {

StylizeController::StylizeController(QObject* parent) : QObject(parent) {}

void StylizeController::setDocumentSession(DocumentSession* session) {
  session_ = session;
}

void StylizeController::setSelectionController(SelectionController* selection) {
  selection_ = selection;
}

void StylizeController::setUndoStack(UndoStack* undoStack) {
  undoStack_ = undoStack;
}

void StylizeController::setBrushQueue(BrushQueue* brushQueue) {
  brushQueue_ = brushQueue;
}

bool StylizeController::toggleBold() {
  return wrapOrInsert(QStringLiteral("**"), QStringLiteral("**"), QString(), EditTransaction::Kind::InsertText, QStringLiteral("Bold"));
}

bool StylizeController::toggleItalic() {
  return wrapOrInsert(QStringLiteral("*"), QStringLiteral("*"), QString(), EditTransaction::Kind::InsertText, QStringLiteral("Italic"));
}

bool StylizeController::toggleCode() {
  return wrapOrInsert(QStringLiteral("`"), QStringLiteral("`"), QString(), EditTransaction::Kind::InsertText, QStringLiteral("Code"));
}

bool StylizeController::insertLink() {
  return wrapOrInsert(QStringLiteral("["), QStringLiteral("](url)"), QString(), EditTransaction::Kind::InsertText, QStringLiteral("Link"));
}

bool StylizeController::wrapOrInsert(
    QString openMarker,
    QString closeMarker,
    QString placeholder,
    EditTransaction::Kind kind,
    QString label) {
  if (selection_ && selection_->hasCursor()) {
    const SelectionRange selection = selection_->selection();
    if (selection.anchor.isValid() && selection.focus.isValid() && selection.anchor.blockId != selection.focus.blockId) {
      if (placeholder.isEmpty()) {
        return wrapMultiBlockSelection(std::move(openMarker), std::move(closeMarker), kind, std::move(label));
      }
      emit unsupportedStyleRequested(QStringLiteral("Link currently supports a single editable block selection."));
      return false;
    }
  }

  ParagraphStyleContext context;
  if (!paragraphContext(context)) {
    emit unsupportedStyleRequested(QStringLiteral("Only plain paragraph text can be styled in this M4 slice."));
    return false;
  }

  const qsizetype start = qMin(context.anchorOffset, context.focusOffset);
  const qsizetype end = qMax(context.anchorOffset, context.focusOffset);
  QString nextParagraph = context.sourceText;
  qsizetype nextCursorOffset = 0;
  qsizetype nextAnchorSourceOffset = -1;
  qsizetype nextFocusSourceOffset = -1;

  if (start == end) {
    const QString skeleton = openMarker + placeholder + closeMarker;
    nextParagraph.insert(start, skeleton);
    nextCursorOffset = start + openMarker.size() + placeholder.size();
    nextAnchorSourceOffset = context.sourceStart + nextCursorOffset;
    nextFocusSourceOffset = nextAnchorSourceOffset;
  } else {
    nextParagraph.insert(end, closeMarker);
    nextParagraph.insert(start, openMarker);
    nextCursorOffset = end + openMarker.size() + closeMarker.size();
    Q_UNUSED(nextCursorOffset);
    nextAnchorSourceOffset = context.sourceStart + start + openMarker.size();
    nextFocusSourceOffset = context.sourceStart + end + openMarker.size();
  }

  return applyStyleDelta(
      kind,
      label,
      context.sourceStart,
      context.sourceEnd - context.sourceStart,
      std::move(nextParagraph),
      nextAnchorSourceOffset,
      nextFocusSourceOffset,
      QVector<LocalEditNodeHint>{LocalEditNodeHint{context.editableNode->id(), context.sourceStart, context.editableNode->type()}});
}

bool StylizeController::wrapMultiBlockSelection(
    QString openMarker,
    QString closeMarker,
    EditTransaction::Kind kind,
    QString label) {
  if (!session_ || !selection_ || !selection_->hasCursor()) {
    return false;
  }

  const SelectionRange selection = selection_->selection();
  if (selection.isCollapsed() || !selection.anchor.isValid() || !selection.focus.isValid() ||
      selection.anchor.blockId == selection.focus.blockId) {
    return false;
  }

  ParagraphStyleContext anchor;
  ParagraphStyleContext focus;
  if (!paragraphContextFor(selection.anchor.blockId, selection, anchor) ||
      !paragraphContextFor(selection.focus.blockId, selection, focus)) {
    emit unsupportedStyleRequested(QStringLiteral("Only plain paragraph, heading, and list item text can be styled in this M4 slice."));
    return false;
  }

  struct EditSpan {
    NodeId nodeId;
    BlockType nodeType = BlockType::Unknown;
    qsizetype sourceStart = -1;
    qsizetype sourceEnd = -1;
    qsizetype startOffset = 0;
    qsizetype endOffset = 0;
  };

  const bool anchorFirst = anchor.sourceStart <= focus.sourceStart;
  const qsizetype selectionStart = anchorFirst ? anchor.sourceStart + qBound<qsizetype>(0, selection.anchor.text.textOffset, anchor.sourceText.size())
                                               : focus.sourceStart + qBound<qsizetype>(0, selection.focus.text.textOffset, focus.sourceText.size());
  const qsizetype selectionEnd = anchorFirst ? focus.sourceStart + qBound<qsizetype>(0, selection.focus.text.textOffset, focus.sourceText.size())
                                             : anchor.sourceStart + qBound<qsizetype>(0, selection.anchor.text.textOffset, anchor.sourceText.size());

  QVector<EditSpan> spans;
  const auto collect = [&](const auto& self, MarkdownNode& node) -> void {
    const bool paragraphInsideListItem =
        node.type() == BlockType::Paragraph && node.parent() && node.parent()->type() == BlockType::ListItem;
    if (!paragraphInsideListItem &&
        (node.type() == BlockType::Paragraph || node.type() == BlockType::Heading || node.type() == BlockType::ListItem)) {
      SelectionRange blockSelection;
      blockSelection.anchor.blockId = node.id();
      blockSelection.anchor.text.nodeId = node.id();
      blockSelection.focus = blockSelection.anchor;
      ParagraphStyleContext context;
      if (fillEditableContext(node, blockSelection, context) && context.sourceEnd > selectionStart &&
          context.sourceStart < selectionEnd) {
        EditSpan span;
        span.sourceStart = context.sourceStart;
        span.sourceEnd = context.sourceEnd;
        span.startOffset = qBound<qsizetype>(0, selectionStart - context.sourceStart, context.sourceText.size());
        span.endOffset = qBound<qsizetype>(0, selectionEnd - context.sourceStart, context.sourceText.size());
        if (span.startOffset < span.endOffset) {
          span.nodeId = context.editableNode->id();
          span.nodeType = context.editableNode->type();
          spans.push_back(span);
        }
      }
      if (node.type() == BlockType::ListItem) {
        return;
      }
    }

    for (const auto& child : node.children()) {
      self(self, *child);
    }
  };
  collect(collect, session_->document().root());

  if (spans.isEmpty()) {
    emit unsupportedStyleRequested(QStringLiteral("No editable text is selected."));
    return false;
  }

  std::sort(spans.begin(), spans.end(), [](const EditSpan& a, const EditSpan& b) {
    return a.sourceStart > b.sourceStart;
  });

  const QString beforeText = session_->markdownText();
  const qsizetype sourceStart = spans.last().sourceStart;
  const qsizetype sourceEnd = spans.first().sourceEnd;
  QString replacement = beforeText.mid(sourceStart, sourceEnd - sourceStart);
  QVector<LocalEditNodeHint> nodeHints;
  for (const EditSpan& span : spans) {
    const qsizetype absoluteEnd = span.sourceStart + span.endOffset;
    const qsizetype absoluteStart = span.sourceStart + span.startOffset;
    replacement.insert(absoluteEnd - sourceStart, closeMarker);
    replacement.insert(absoluteStart - sourceStart, openMarker);
    nodeHints.push_back(LocalEditNodeHint{span.nodeId, span.sourceStart, span.nodeType});
  }

  const qsizetype nextAnchorSourceOffset = (anchorFirst ? selectionStart : selectionEnd) + openMarker.size();
  const qsizetype insertedBeforeFocus = anchorFirst
                                            ? spans.size() * (openMarker.size() + closeMarker.size()) - closeMarker.size()
                                            : openMarker.size();
  const qsizetype nextFocusSourceOffset = (anchorFirst ? selectionEnd : selectionStart) + insertedBeforeFocus;

  return applyStyleDelta(
      kind,
      label,
      sourceStart,
      sourceEnd - sourceStart,
      std::move(replacement),
      nextAnchorSourceOffset,
      nextFocusSourceOffset,
      std::move(nodeHints));
}

bool StylizeController::paragraphContext(ParagraphStyleContext& context) const {
  if (!session_ || !selection_ || !selection_->hasCursor()) {
    return false;
  }

  const SelectionRange selection = selection_->selection();
  if (!selection.anchor.isValid() || !selection.focus.isValid()) {
    return false;
  }
  if (selection.anchor.blockId != selection.focus.blockId) {
    return false;
  }

  return paragraphContextFor(selection.focus.blockId, selection, context);
}

bool StylizeController::paragraphContextFor(
    NodeId blockId,
    const SelectionRange& selection,
    ParagraphStyleContext& context,
    bool requirePlainInline) const {
  if (!session_) {
    return false;
  }

  MarkdownNode* node = session_->document().node(blockId);
  if (!node ||
      (node->type() != BlockType::Paragraph && node->type() != BlockType::Heading && node->type() != BlockType::ListItem)) {
    return false;
  }

  return fillEditableContext(*node, selection, context, requirePlainInline);
}

bool StylizeController::fillEditableContext(
    MarkdownNode& displayNode,
    const SelectionRange& selection,
    ParagraphStyleContext& context,
    bool requirePlainInline) const {
  MarkdownNode* editable = &displayNode;
  if (displayNode.type() == BlockType::ListItem) {
    editable = primaryParagraph(displayNode);
  }

  if (!editable || (editable->type() != BlockType::Paragraph && editable->type() != BlockType::Heading)) {
    return false;
  }

  const SourceRange range = editable->sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return false;
  }

  const QString markdown = session_->markdownText();
  qsizetype sourceStart = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype sourceEnd = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (sourceStart < 0 || sourceEnd < sourceStart) {
    return false;
  }

  if (editable->type() == BlockType::Heading) {
    while (sourceStart < sourceEnd && markdown.at(sourceStart) == QLatin1Char('#')) {
      ++sourceStart;
    }
    if (sourceStart < sourceEnd && markdown.at(sourceStart).isSpace()) {
      ++sourceStart;
    }
  }

  context.node = &displayNode;
  context.editableNode = editable;
  context.sourceStart = sourceStart;
  context.sourceEnd = sourceEnd;
  context.anchorOffset = qBound<qsizetype>(0, selection.anchor.text.textOffset, sourceEnd - sourceStart);
  context.focusOffset = qBound<qsizetype>(0, selection.focus.text.textOffset, sourceEnd - sourceStart);
  context.sourceText = markdown.mid(sourceStart, sourceEnd - sourceStart);
  return !requirePlainInline || isPlainInlineEditable(*editable, context.sourceText);
}

bool StylizeController::isPlainInlineEditable(const MarkdownNode& node, const QString& sourceText) const {
  QString plain;
  for (const InlineNode& inlineNode : node.inlines()) {
    switch (inlineNode.type()) {
      case InlineType::Text:
        plain += inlineNode.text();
        break;
      case InlineType::SoftBreak:
        plain += QLatin1Char('\n');
        break;
      case InlineType::LineBreak:
        plain += QStringLiteral("  \n");
        break;
      default:
        return false;
    }
  }
  return plain == sourceText;
}

MarkdownNode* StylizeController::primaryParagraph(MarkdownNode& node) const {
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child.get();
    }
  }
  return nullptr;
}

qsizetype StylizeController::sourceOffsetForLineColumn(const QString& text, int line, int column) const {
  if (line <= 0 || column <= 0) {
    return -1;
  }

  int currentLine = 1;
  qsizetype offset = 0;
  while (currentLine < line && offset < text.size()) {
    if (text.at(offset) == QLatin1Char('\n')) {
      ++currentLine;
    }
    ++offset;
  }

  if (currentLine != line) {
    return -1;
  }
  return qMin(offset + column - 1, text.size());
}

qsizetype StylizeController::sourceOffsetForLineEnd(const QString& text, int line) const {
  if (line <= 0) {
    return -1;
  }

  int currentLine = 1;
  qsizetype offset = 0;
  while (offset < text.size()) {
    if (currentLine == line && text.at(offset) == QLatin1Char('\n')) {
      return offset;
    }
    if (text.at(offset) == QLatin1Char('\n')) {
      ++currentLine;
    }
    ++offset;
  }
  return currentLine == line ? text.size() : -1;
}

bool StylizeController::applyStyleDelta(
    EditTransaction::Kind kind,
    const QString& label,
    qsizetype sourceStart,
    qsizetype removedLength,
    QString insertedText,
    qsizetype nextAnchorSourceOffset,
    qsizetype nextFocusSourceOffset,
    QVector<LocalEditNodeHint> nodeHints) {
  if (!session_ || sourceStart < 0 || removedLength < 0 || sourceStart + removedLength > session_->markdownText().size()) {
    return false;
  }

  const CursorPosition beforeCursor = selection_ && selection_->hasCursor() ? selection_->cursorPosition() : CursorPosition();
  const QString removedText = session_->markdownText().mid(sourceStart, removedLength);
  QVector<NodeId> affectedNodes;
  for (const LocalEditNodeHint& hint : nodeHints) {
    if (hint.nodeId.isValid() && !affectedNodes.contains(hint.nodeId)) {
      affectedNodes.push_back(hint.nodeId);
    }
  }
  if (!session_->applyTextDelta(sourceStart, removedLength, insertedText, true, std::move(nodeHints))) {
    return false;
  }

  SelectionRange nextSelection;
  nextSelection.anchor = cursorForSourceOffset(nextAnchorSourceOffset);
  nextSelection.focus = cursorForSourceOffset(nextFocusSourceOffset);
  if (selection_ && nextSelection.focus.isValid()) {
    selection_->setSelection(nextSelection);
  }
  if (nextSelection.anchor.blockId.isValid() && !affectedNodes.contains(nextSelection.anchor.blockId)) {
    affectedNodes.push_back(nextSelection.anchor.blockId);
  }
  if (nextSelection.focus.blockId.isValid() && !affectedNodes.contains(nextSelection.focus.blockId)) {
    affectedNodes.push_back(nextSelection.focus.blockId);
  }
  if (undoStack_ && beforeCursor.isValid() && nextSelection.focus.isValid()) {
    undoStack_->push(EditTransaction(
        kind,
        label,
        TextDeltaCommand{
            TextDelta{sourceStart, removedText, insertedText},
            beforeCursor,
            nextSelection.focus,
            std::move(affectedNodes)}));
  }
  if (brushQueue_) {
    if (!affectedNodes.isEmpty()) {
      brushQueue_->requestBlocksRefresh(std::move(affectedNodes));
    } else {
      brushQueue_->requestFullRefresh();
    }
  }
  return true;
}

CursorPosition StylizeController::cursorForSourceOffset(qsizetype sourceOffset) const {
  CursorPosition cursor;
  if (!session_) {
    return cursor;
  }

  MarkdownNode* node = paragraphAtSourceOffset(session_->document().root(), sourceOffset);
  if (!node) {
    return cursor;
  }

  const QString markdown = session_->markdownText();
  ParagraphStyleContext context;
  SelectionRange selection;
  selection.anchor.blockId = node->id();
  selection.anchor.text.nodeId = node->id();
  selection.focus = selection.anchor;
  if (!fillEditableContext(*node, selection, context, false)) {
    return cursor;
  }
  cursor.blockId = node->id();
  cursor.text.nodeId = node->id();
  cursor.text.textOffset = qBound<qsizetype>(0, sourceOffset - context.sourceStart, context.sourceText.size());
  cursor.text.sourceOffset = sourceOffset;
  return cursor;
}

MarkdownNode* StylizeController::paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset) const {
  if (node.type() == BlockType::Paragraph || node.type() == BlockType::Heading || node.type() == BlockType::ListItem) {
    SelectionRange selection;
    selection.anchor.blockId = node.id();
    selection.anchor.text.nodeId = node.id();
    selection.focus = selection.anchor;
    ParagraphStyleContext context;
    if (fillEditableContext(node, selection, context, false) && sourceOffset >= context.sourceStart &&
        sourceOffset <= context.sourceEnd) {
      return &node;
    }
  }

  for (const auto& child : node.children()) {
    if (MarkdownNode* found = paragraphAtSourceOffset(*child, sourceOffset)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace muffin
