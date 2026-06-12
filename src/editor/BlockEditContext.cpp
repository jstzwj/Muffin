#include "editor/BlockEditContext.h"

#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "document/SourceRangeUtil.h"
#include "editor/SelectionController.h"

namespace muffin {
namespace {

bool isEditableTextBlock(BlockType type) {
  return type == BlockType::Paragraph || type == BlockType::Heading || type == BlockType::ListItem || type == BlockType::TableCell ||
         type == BlockType::LinkDefinition || type == BlockType::FootnoteDefinition;
}

bool isInlineEditableNode(BlockType type) {
  return type == BlockType::Paragraph || type == BlockType::Heading || type == BlockType::TableCell;
}

bool isDefinitionBlock(BlockType type) {
  return type == BlockType::LinkDefinition || type == BlockType::FootnoteDefinition;
}

// Literal blocks edit through dedicated controllers (code/math/HTML/front matter) rather than
// the inline-text model. The cursor for such a block is anchored on the block node itself, with
// the caret expressed as an offset into the literal content.
bool isLiteralBlockType(BlockType type) {
  return type == BlockType::CodeFence || type == BlockType::MathBlock ||
         type == BlockType::HtmlBlock || type == BlockType::FrontMatter;
}

// Byte offset of the first editable literal character: just past the opening fence/marker line.
// For "```\nfoo\n```" that is index 4 (the 'f'); for an empty "```\n\n```" it is the blank line.
qsizetype literalContentStartOffset(const QString& markdown, const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  const qsizetype start = qBound<qsizetype>(0, range.byteStart, markdown.size());
  const qsizetype newlineAt = markdown.indexOf(QLatin1Char('\n'), start);
  const qsizetype bound = qMin<qsizetype>(markdown.size(), qMax(range.byteEnd, start));
  return (newlineAt >= 0 && newlineAt < bound) ? newlineAt + 1 : start;
}

qsizetype taskContentStartForListLine(const QString& markdown, qsizetype lineStart, qsizetype lineEnd, qsizetype markerContentStart) {
  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const ListLineInfo info = listLineInfoFor(line);
  if (!info.valid || !info.task) {
    return markerContentStart;
  }
  return lineStart + info.taskContentStart;
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

bool localSourceOffsetForCursor(const BlockEditContext& context, const CursorPosition& cursor, qsizetype& localSourceOffset) {
  if (cursor.text.sourceOffset >= context.contentRange.byteStart && cursor.text.sourceOffset <= context.contentRange.byteEnd) {
    localSourceOffset = cursor.text.sourceOffset - context.contentRange.byteStart;
    return true;
  }
  if (context.plainInlineEditable) {
    localSourceOffset = qBound<qsizetype>(0, cursor.text.textOffset, context.contentText.size());
    return true;
  }
  return context.inlineProjection.sourceOffsetForVisibleOffset(cursor.text.textOffset, localSourceOffset);
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
  if (isDefinitionBlock(displayNode.type())) {
    const DefinitionBlock definition = displayNode.definition();
    const SourceRange range = displayNode.sourceRange();
    if (!definition.markerRange.isValid()) {
      return false;
    }
    context.node = &displayNode;
    context.editableNode = &displayNode;
    context.blockId = displayNode.id();
    context.blockType = displayNode.type();
    context.blockRange = range;
    context.blockRange.byteStart = range.byteStart;
    context.blockRange.byteEnd = range.byteEnd;
    context.contentRange.byteStart = definition.markerRange.start;
    context.contentRange.byteEnd = definition.sourceRange.isValid()
                                       ? definition.sourceRange.end
                                       : qMax(definition.markerRange.end,
                                              qMax(definition.destinationRange.end,
                                                   qMax(definition.titleRange.end, definition.noteRange.end)));
    context.cursorTextOffset = selection_ && selection_->hasCursor() && selection_->cursorPosition().blockId == displayNode.id()
                                ? selection_->cursorPosition().text.textOffset
                                : 0;
    const qsizetype cursorStoredSourceOffset =
        selection_ && selection_->hasCursor() && selection_->cursorPosition().blockId == displayNode.id()
            ? selection_->cursorPosition().text.sourceOffset
            : -1;
    context.contentText = session_->markdownText().mid(context.contentRange.byteStart,
                                                       context.contentRange.byteEnd - context.contentRange.byteStart);
    context.visibleText = context.contentText;
    context.plainInlineEditable = true;
    context.supportsVisibleOffsetMapping = true;
    if (cursorStoredSourceOffset >= context.contentRange.byteStart && cursorStoredSourceOffset <= context.contentRange.byteEnd) {
      context.cursorSourceOffset = cursorStoredSourceOffset;
      context.cursorTextOffset = cursorStoredSourceOffset - context.contentRange.byteStart;
    } else {
      context.cursorSourceOffset =
          context.contentRange.byteStart + qBound<qsizetype>(0, context.cursorTextOffset, context.contentText.size());
    }
    return true;
  }

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
        const qsizetype textStart = taskContentStartForListLine(session_->markdownText(), lineStart, lineEnd, contentStart);
        context.node = &displayNode;
        context.editableNode = nullptr;
        context.blockId = displayNode.id();
        context.blockType = displayNode.type();
        context.blockRange.byteStart = lineStart;
        context.blockRange.byteEnd = lineEnd;
        context.contentRange.byteStart = textStart;
        context.contentRange.byteEnd = lineEnd;
        context.cursorTextOffset = 0;
        context.cursorSourceOffset = textStart;
        context.contentText = session_->markdownText().mid(textStart, lineEnd - textStart);
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
  qsizetype start = range.byteEnd > range.byteStart
                       ? range.byteStart
                       : sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = editable->type() == BlockType::Heading
                            ? headingContentEndOffset(*editable, markdown)
                            : (range.byteEnd > range.byteStart
                                   ? range.byteEnd
                                   : sourceOffsetForLineEnd(markdown, range.lineEnd));
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
    if (displayNode.type() == BlockType::ListItem) {
      qsizetype lineStart = start;
      while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
        --lineStart;
      }
      qsizetype lineEnd = start;
      while (lineEnd < markdown.size() && markdown.at(lineEnd) != QLatin1Char('\n')) {
        ++lineEnd;
      }
      start = taskContentStartForListLine(markdown, lineStart, lineEnd, start);
    }
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
  context.inlineProjection = InlineProjection(editable->inlines(), context.contentText, projectionState, start);
  context.visibleText = context.inlineProjection.visibleText();
  context.plainInlineEditable = InlineProjection::isPlainInlineSource(editable->inlines(), context.contentText, start);
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
  const NodeId focusContextId = range.focus.text.nodeId.isValid() ? range.focus.text.nodeId : range.focus.blockId;
  if (!forBlock(focusContextId, context)) {
    return false;
  }
  qsizetype anchorOffset = -1;
  qsizetype focusOffset = -1;
  if (!localSourceOffsetForCursor(context, range.anchor, anchorOffset) || !localSourceOffsetForCursor(context, range.focus, focusOffset)) {
    return false;
  }

  start = qMin(anchorOffset, focusOffset);
  end = qMax(anchorOffset, focusOffset);
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
  const NodeId anchorContextId = range.anchor.text.nodeId.isValid() ? range.anchor.text.nodeId : range.anchor.blockId;
  const NodeId focusContextId = range.focus.text.nodeId.isValid() ? range.focus.text.nodeId : range.focus.blockId;
  if (!forBlock(anchorContextId, anchor) || !forBlock(focusContextId, focus)) {
    return false;
  }

  qsizetype anchorLocalOffset = -1;
  qsizetype focusLocalOffset = -1;
  if (!localSourceOffsetForCursor(anchor, range.anchor, anchorLocalOffset) ||
      !localSourceOffsetForCursor(focus, range.focus, focusLocalOffset)) {
    return false;
  }
  start = qMin(anchor.contentRange.byteStart + anchorLocalOffset, focus.contentRange.byteStart + focusLocalOffset);
  end = qMax(anchor.contentRange.byteStart + anchorLocalOffset, focus.contentRange.byteStart + focusLocalOffset);
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
  const ListLineInfo info = listLineInfoFor(line);
  if (!info.valid) {
    return false;
  }
  contentStart = lineStart + info.contentStart;
  return contentStart >= lineStart && contentStart <= lineEnd;
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

MarkdownNode* BlockEditContextResolver::literalBlockAtSourceOffset(
    MarkdownNode& node, qsizetype sourceOffset, qsizetype& contentStartOut) const {
  if (!session_) {
    return nullptr;
  }
  if (isLiteralBlockType(node.type())) {
    const SourceRange range = node.sourceRange();
    if (sourceOffset >= range.byteStart && sourceOffset <= range.byteEnd) {
      contentStartOut = literalContentStartOffset(session_->markdownText(), node);
      return &node;
    }
  }
  for (const auto& child : node.children()) {
    if (MarkdownNode* found = literalBlockAtSourceOffset(*child, sourceOffset, contentStartOut)) {
      return found;
    }
  }
  return nullptr;
}

}  // namespace muffin
