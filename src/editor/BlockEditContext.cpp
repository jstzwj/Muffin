#include "editor/BlockEditContext.h"

#include "document/BlockPredicates.h"
#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "document/NodeNavigation.h"
#include "document/SourceRangeUtil.h"
#include "editor/SelectionController.h"

#include <QSettings>

namespace muffin {
namespace {

// markdown/* smart punctuation for the render path (Convert on Rendering). Mirrors the same-named
// helper in BlockLayoutBuilder.cpp so the editing projection and the painted projection apply the
// identical conversion — and thus agree on folded-token boundaries (so backspace/delete/cursor act
// on the whole source token, not a single dash).
SmartPunctRenderOptions smartPunctRenderOptions() {
  SmartPunctRenderOptions opts;
  const bool rendering = QSettings().value(QStringLiteral("markdown/convertOnRendering"), false).toBool();
  opts.convertQuotes = rendering && QSettings().value(QStringLiteral("markdown/smartQuotes"), false).toBool();
  opts.convertDashes = rendering && QSettings().value(QStringLiteral("markdown/smartDashes"), false).toBool();
  opts.convertEllipsis = opts.convertDashes;
  opts.doubleQuoteStyle = QSettings().value(QStringLiteral("markdown/doubleQuoteStyle"), 0).toInt();
  opts.singleQuoteStyle = QSettings().value(QStringLiteral("markdown/singleQuoteStyle"), 0).toInt();
  return opts;
}

bool isInlineEditableNode(BlockType type) {
  return type == BlockType::Paragraph || type == BlockType::Heading || type == BlockType::TableCell;
}

bool isDefinitionBlock(BlockType type) {
  return type == BlockType::LinkDefinition || type == BlockType::FootnoteDefinition;
}

// Byte offset of the first editable literal character: just past the opening fence/marker line.
// For "```\nfoo\n```" that is index 4 (the 'f'); for an empty "```\n\n```" it is the blank line.
template <typename Text>
qsizetype literalContentStartOffsetOf(const Text& markdown, const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  const qsizetype start = qBound<qsizetype>(0, range.byteStart, markdown.size());
  const qsizetype newlineAt = markdown.indexOf(QLatin1Char('\n'), start);
  const qsizetype bound = qMin<qsizetype>(markdown.size(), qMax(range.byteEnd, start));
  return (newlineAt >= 0 && newlineAt < bound) ? newlineAt + 1 : start;
}

template <typename Text>
qsizetype taskContentStartForListLine(const Text& markdown, qsizetype lineStart, qsizetype lineEnd, qsizetype markerContentStart) {
  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const ListLineInfo info = listLineInfoFor(line);
  if (!info.valid || !info.task) {
    return markerContentStart;
  }
  return lineStart + info.taskContentStart;
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
  // O(log n) line/column → byte offset via the incrementally-maintained cache. Empty /
  // degenerate-range nodes (freshly-created paragraphs, VEPs) carry no byte span, so their content
  // bounds must be recovered from line/column; the sourceOffsetForLineColumn/sourceOffsetForLineEnd
  // free functions scan from byte 0 (O(document)), which froze the editor on every Enter that
  // creates an empty paragraph (~145 ms ×2 per Enter on an 18 MB doc — fill() runs both inside the
  // caret-resolve walk and again after the match is found).
  const auto& lineOffsets = session_->document().lineOffsets();

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
      markerContext.contentRange.byteStart = lineOffsets.offsetForLineColumn(
          displayNode.sourceRange().lineStart, qMax(1, displayNode.sourceRange().columnStart));
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
  const PieceTable& markdown = session_->markdownText();
  qsizetype start = range.byteEnd > range.byteStart
                       ? range.byteStart
                       : lineOffsets.offsetForLineColumn(range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = editable->type() == BlockType::Heading
                            ? headingContentEndOffset(*editable, markdown)
                            : (range.byteEnd > range.byteStart
                                   ? range.byteEnd
                                   : lineOffsets.lineEndOffset(range.lineEnd));
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
  // Inlines are stored relative to the owning top-level block's byteStart; InlineProjection's
  // sourceBase must be the content offset within the block (start - top-level byteStart).
  const qsizetype inlineBase = start - editable->topLevelBlock()->sourceRange().byteStart;
  context.inlineProjection = InlineProjection(editable->inlines(), context.contentText, projectionState, inlineBase,
                                              16.0, 0, smartPunctRenderOptions());
  context.visibleText = context.inlineProjection.visibleText();
  context.plainInlineEditable = InlineProjection::isPlainInlineSource(editable->inlines(), context.contentText, inlineBase);
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
  const PieceTable& markdown = session_->markdownText();
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

  const PieceTable& markdown = session_->markdownText();
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
  // NOTE: deliberately does NOT climb out of containers, unlike nextEditableTextBlock below. A
  // backspace at the start of a nested editable leaf is intercepted before reaching here — a list
  // item's caret lives on the ListItem (so the dispatcher routes to list merge/outdent), and a
  // block quote's first line routes to quote outdent — so the only way in with no previous sibling
  // is an artificial inner-paragraph caret, where climbing out would destructively escape the
  // container. Keeping the immediate-sibling lookup is the safe, correct behaviour here.
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
  // Climb out of the caret's container (block quote, list, …) to the block that actually follows in
  // the source. A nested editable leaf has its siblings INSIDE the container, so node->nextSibling()
  // alone never finds an editable block sitting OUTSIDE — and the resulting silent no-op left a
  // forward-delete at a container boundary doing nothing, inconsistent with a top-level caret (which
  // merges fine). See NodeNavigation.h. (The backward direction is handled differently — see
  // previousEditableTextBlock.)
  MarkdownNode* candidate = nextSiblingAcrossContainers(const_cast<MarkdownNode&>(node));
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
    // Prune subtrees whose source range cannot contain the offset. Without this the walk is
    // O(total nodes) whenever the offset matches no inline-text block (a gap between blocks, a
    // non-text block, or — with preferLaterEmptyAtOffset — any match, since it does not early-return).
    // That made cursorForSourceOffset ~15 s/call on a 112k-block document: typing right after a
    // `---` rule (insertBlockAfterCurrentBlock, preferLaterEmptyAtOffset=true) froze ~50 s, and the
    // 3rd dash that creates the rule froze ~47 s. sourceRange is a superset of the content range the
    // match test uses, so this prunes only subtrees that could not match anyway.
    if (!child->sourceRange().containsByte(sourceOffset)) {
      continue;
    }
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
      contentStartOut = literalContentStartOffsetOf(session_->markdownText(), node);
      return &node;
    }
  }
  for (const auto& child : node.children()) {
    if (!child->sourceRange().containsByte(sourceOffset)) {
      continue;  // prune: this subtree's source cannot contain the offset (see nodeAtContentSourceOffset)
    }
    if (MarkdownNode* found = literalBlockAtSourceOffset(*child, sourceOffset, contentStartOut)) {
      return found;
    }
  }
  return nullptr;
}

qsizetype BlockEditContextResolver::literalContentStartOffset(const MarkdownNode& node) const {
  if (!session_) {
    return node.sourceRange().byteStart;
  }
  return literalContentStartOffsetOf(session_->markdownText(), node);
}

}  // namespace muffin
