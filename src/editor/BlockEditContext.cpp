#include "editor/BlockEditContext.h"

#include "app/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "editor/SelectionController.h"

namespace muffin {
namespace {

bool isEditableTextBlock(BlockType type) {
  return type == BlockType::Paragraph || type == BlockType::Heading || type == BlockType::ListItem || type == BlockType::TableCell;
}

bool isInlineEditableNode(BlockType type) {
  return type == BlockType::Paragraph || type == BlockType::Heading || type == BlockType::TableCell;
}

qsizetype paragraphContentStartIncludingCommonMarkIndent(const QString& markdown, qsizetype astStart) {
  qsizetype lineStart = astStart;
  while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  qsizetype start = astStart;
  while (start > lineStart && astStart - start < 3 && markdown.at(start - 1) == QLatin1Char(' ')) {
    --start;
  }
  return start == lineStart ? start : astStart;
}

}  // namespace

BlockEditContextResolver::BlockEditContextResolver(DocumentSession* session, SelectionController* selection)
    : session_(session), selection_(selection) {}

bool BlockEditContextResolver::current(BlockEditContext& context) const {
  if (!session_ || !selection_ || !selection_->hasCursor()) {
    return false;
  }
  return forBlock(selection_->cursorPosition().blockId, context);
}

bool BlockEditContextResolver::forBlock(NodeId blockId, BlockEditContext& context) const {
  if (!session_) {
    return false;
  }

  MarkdownNode* node = session_->document().node(blockId);
  if (!node || !isEditableTextBlock(node->type())) {
    return false;
  }

  return fill(*node, context);
}

bool BlockEditContextResolver::fill(MarkdownNode& displayNode, BlockEditContext& context) const {
  if (!session_) {
    return false;
  }

  MarkdownNode* editable = &displayNode;
  if (displayNode.type() == BlockType::ListItem) {
    editable = primaryParagraph(displayNode);
    if (!editable) {
      qsizetype lineStart = -1;
      qsizetype contentStart = -1;
      qsizetype lineEnd = -1;
      BlockEditContext markerContext;
      markerContext.node = &displayNode;
      markerContext.contentRange.byteStart = sourceOffsetForLineColumn(
          session_->markdownText(), displayNode.sourceRange().lineStart, qMax(1, displayNode.sourceRange().columnStart));
      if (markerContext.contentRange.byteStart >= 0 && listItemLineBounds(markerContext, lineStart, contentStart, lineEnd)) {
        context.node = &displayNode;
        context.editableNode = nullptr;
        context.blockId = displayNode.id();
        context.blockType = displayNode.type();
        context.blockRange.byteStart = lineStart;
        context.blockRange.byteEnd = lineEnd;
        context.contentRange.byteStart = contentStart;
        context.contentRange.byteEnd = lineEnd;
        context.cursorTextOffset = 0;
        context.cursorSourceOffset = contentStart;
        context.contentText = session_->markdownText().mid(contentStart, lineEnd - contentStart);
        context.visibleText = context.contentText;
        context.plainInlineEditable = context.contentText.trimmed().isEmpty();
        context.supportsVisibleOffsetMapping = context.plainInlineEditable;
        return context.plainInlineEditable;
      }
    }
  }

  if (!editable || !isInlineEditableNode(editable->type())) {
    return false;
  }

  const SourceRange range = editable->sourceRange();
  const QString markdown = session_->markdownText();
  qsizetype start = editable->type() == BlockType::TableCell && range.byteEnd >= range.byteStart
                       ? range.byteStart
                       : sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = editable->type() == BlockType::TableCell && range.byteEnd >= range.byteStart
                            ? range.byteEnd
                            : sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (start < 0 || end < start) {
    return false;
  }

  if (editable->type() == BlockType::Heading) {
    while (start < end && markdown.at(start) == QLatin1Char('#')) {
      ++start;
    }
    if (start < end && markdown.at(start).isSpace()) {
      ++start;
    }
  } else if (editable->type() == BlockType::Paragraph) {
    start = paragraphContentStartIncludingCommonMarkIndent(markdown, start);
  }

  context.node = &displayNode;
  context.editableNode = editable;
  context.blockId = displayNode.id();
  context.blockType = displayNode.type();
  context.blockRange = range;
  context.blockRange.byteStart = range.byteStart;
  context.blockRange.byteEnd = range.byteEnd;
  context.contentRange.byteStart = start;
  context.contentRange.byteEnd = end;
  context.cursorTextOffset = selection_ && selection_->hasCursor() && selection_->cursorPosition().blockId == displayNode.id()
                              ? selection_->cursorPosition().text.textOffset
                              : 0;
  const qsizetype cursorStoredSourceOffset =
      selection_ && selection_->hasCursor() && selection_->cursorPosition().blockId == displayNode.id()
          ? selection_->cursorPosition().text.sourceOffset
          : -1;
  context.contentText = markdown.mid(start, end - start);
  InlineProjectionState projectionState;
  if (selection_ && selection_->hasCursor()) {
    projectionState = InlineProjectionState::forSelection(selection_->selection(), displayNode.id(), start);
  } else {
    CursorPosition cursor;
    cursor.blockId = displayNode.id();
    cursor.text.textOffset = context.cursorTextOffset;
    cursor.text.sourceOffset = cursorStoredSourceOffset;
    projectionState = InlineProjectionState::forCursor(cursor, displayNode.id(), start);
  }
  context.inlineProjection = InlineProjection(editable->inlines(), context.contentText, projectionState);
  context.visibleText = context.inlineProjection.visibleText();
  context.plainInlineEditable = InlineProjection::isPlainInlineSource(editable->inlines(), context.contentText);
  qsizetype localSourceOffset = -1;
  const bool hasStoredSourceOffset = cursorStoredSourceOffset >= start && cursorStoredSourceOffset <= end;
  context.supportsVisibleOffsetMapping =
      context.plainInlineEditable || hasStoredSourceOffset ||
      context.inlineProjection.sourceOffsetForVisibleOffset(context.cursorTextOffset, localSourceOffset);
  if (context.plainInlineEditable) {
    if (hasStoredSourceOffset) {
      context.cursorSourceOffset = cursorStoredSourceOffset;
      context.cursorTextOffset = qBound<qsizetype>(0, cursorStoredSourceOffset - start, context.contentText.size());
    } else {
      context.cursorSourceOffset = start + qBound<qsizetype>(0, context.cursorTextOffset, context.contentText.size());
    }
  } else if (hasStoredSourceOffset) {
    context.cursorSourceOffset = cursorStoredSourceOffset;
    qsizetype visibleOffset = -1;
    if (context.inlineProjection.visibleOffsetForSourceOffset(cursorStoredSourceOffset - start, visibleOffset)) {
      context.cursorTextOffset = visibleOffset;
    }
  } else if (context.supportsVisibleOffsetMapping) {
    context.cursorSourceOffset = start + localSourceOffset;
  }
  return context.plainInlineEditable || context.supportsVisibleOffsetMapping;
}

bool BlockEditContextResolver::selectionContext(BlockEditContext& context, qsizetype& start, qsizetype& end) const {
  if (!selection_ || !selection_->hasCursor()) {
    return false;
  }

  const SelectionRange range = selection_->selection();
  if (!range.isSingleBlock() || range.isCollapsed()) {
    return false;
  }
  if (!forBlock(range.focus.blockId, context)) {
    return false;
  }
  if (!context.plainInlineEditable) {
    return false;
  }

  start = qBound<qsizetype>(0, range.startOffset(), context.contentText.size());
  end = qBound<qsizetype>(0, range.endOffset(), context.contentText.size());
  return start < end;
}

bool BlockEditContextResolver::selectionSourceRange(qsizetype& start, qsizetype& end) const {
  if (!selection_ || !selection_->hasCursor() || !session_) {
    return false;
  }

  const SelectionRange range = selection_->selection();
  if (range.isCollapsed()) {
    return false;
  }

  BlockEditContext anchor;
  BlockEditContext focus;
  if (!forBlock(range.anchor.blockId, anchor) || !forBlock(range.focus.blockId, focus)) {
    return false;
  }

  const qsizetype anchorOffset =
      anchor.contentRange.byteStart + qBound<qsizetype>(0, range.anchor.text.textOffset, anchor.contentText.size());
  const qsizetype focusOffset =
      focus.contentRange.byteStart + qBound<qsizetype>(0, range.focus.text.textOffset, focus.contentText.size());
  start = qMin(anchorOffset, focusOffset);
  end = qMax(anchorOffset, focusOffset);
  return start < end;
}

bool BlockEditContextResolver::blockSourceRange(const MarkdownNode& node, qsizetype& start, qsizetype& end) const {
  if (!session_) {
    return false;
  }
  const SourceRange range = node.sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return false;
  }
  const QString markdown = session_->markdownText();
  start = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  end = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (end >= 0 && range.lineEnd > range.lineStart && end < markdown.size()) {
    end = qMin<qsizetype>(markdown.size(), end);
  }
  return start >= 0 && end >= start;
}

bool BlockEditContextResolver::listItemLineBounds(
    const BlockEditContext& context,
    qsizetype& lineStart,
    qsizetype& contentStart,
    qsizetype& lineEnd) const {
  if (!session_ || !context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }

  const QString markdown = session_->markdownText();
  lineStart = context.contentRange.byteStart;
  while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  lineEnd = context.contentRange.byteStart;
  while (lineEnd < markdown.size() && markdown.at(lineEnd) != QLatin1Char('\n')) {
    ++lineEnd;
  }

  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const QString marker = listMarkerFor(line);
  if (marker.isEmpty()) {
    return false;
  }
  contentStart = line.indexOf(marker) + marker.size() + lineStart;
  return contentStart >= lineStart && contentStart <= lineEnd;
}

QString BlockEditContextResolver::listMarkerFor(const QString& line) const {
  qsizetype index = 0;
  while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
    ++index;
  }
  if (index + 2 <= line.size() && (line.at(index) == QLatin1Char('-') || line.at(index) == QLatin1Char('*') ||
                                   line.at(index) == QLatin1Char('+')) &&
      line.at(index + 1).isSpace()) {
    return line.mid(index, 2);
  }

  qsizetype numberStart = index;
  while (index < line.size() && line.at(index).isDigit()) {
    ++index;
  }
  if (index > numberStart && index + 2 <= line.size() && line.at(index) == QLatin1Char('.') && line.at(index + 1).isSpace()) {
    return line.mid(numberStart, index - numberStart + 2);
  }
  return {};
}

MarkdownNode* BlockEditContextResolver::previousEditableTextBlock(const MarkdownNode& node, BlockEditContext& context) const {
  MarkdownNode* candidate = node.previousSibling() ? const_cast<MarkdownNode*>(node.previousSibling()) : nullptr;
  if (!candidate && node.parent() && node.parent()->type() == BlockType::ListItem) {
    candidate = node.parent()->previousSibling();
  }
  if (!candidate) {
    return nullptr;
  }
  MarkdownNode* editable = lastEditableDescendant(*candidate);
  return editable && fill(*editable, context) ? context.node : nullptr;
}

MarkdownNode* BlockEditContextResolver::nextEditableTextBlock(const MarkdownNode& node, BlockEditContext& context) const {
  MarkdownNode* candidate = node.nextSibling() ? const_cast<MarkdownNode*>(node.nextSibling()) : nullptr;
  if (!candidate && node.parent() && node.parent()->type() == BlockType::ListItem) {
    candidate = node.parent()->nextSibling();
  }
  if (!candidate) {
    return nullptr;
  }
  MarkdownNode* editable = firstEditableDescendant(*candidate);
  return editable && fill(*editable, context) ? context.node : nullptr;
}

MarkdownNode* BlockEditContextResolver::nodeAtContentSourceOffset(
    MarkdownNode& node,
    qsizetype sourceOffset,
    bool preferLaterEmptyAtOffset) const {
  MarkdownNode* matched = nullptr;
  if (isEditableTextBlock(node.type())) {
    BlockEditContext context;
    if (fill(node, context) && sourceOffset >= context.contentRange.byteStart && sourceOffset <= context.contentRange.byteEnd) {
      matched = &node;
      if (!preferLaterEmptyAtOffset || context.contentRange.byteStart != context.contentRange.byteEnd) {
        return matched;
      }
    }
  }

  for (const auto& child : node.children()) {
    if (MarkdownNode* found = nodeAtContentSourceOffset(*child, sourceOffset, preferLaterEmptyAtOffset)) {
      matched = found;
      if (!preferLaterEmptyAtOffset) {
        return matched;
      }
    }
  }
  return matched;
}

qsizetype BlockEditContextResolver::sourceOffsetForLineColumn(const QString& text, int line, int column) const {
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

qsizetype BlockEditContextResolver::sourceOffsetForLineEnd(const QString& text, int line) const {
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

MarkdownNode* BlockEditContextResolver::lastEditableDescendant(MarkdownNode& node) const {
  if (isEditableTextBlock(node.type())) {
    return &node;
  }
  auto& children = node.children();
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    if (MarkdownNode* found = lastEditableDescendant(*it->get())) {
      return found;
    }
  }
  return nullptr;
}

MarkdownNode* BlockEditContextResolver::firstEditableDescendant(MarkdownNode& node) const {
  if (isEditableTextBlock(node.type())) {
    return &node;
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = firstEditableDescendant(*child)) {
      return found;
    }
  }
  return nullptr;
}

MarkdownNode* BlockEditContextResolver::primaryParagraph(MarkdownNode& node) const {
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child.get();
    }
  }
  return nullptr;
}

}  // namespace muffin
