#include "commands/StylizeController.h"

#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"

#include <algorithm>

namespace muffin {

// ============================================================================
// Public entry points
// ============================================================================

StylizeController::StylizeController(QObject* parent) : QObject(parent) {}

bool StylizeController::toggleBold() {
  return toggleStyle(InlineType::Strong,
                     QStringLiteral("**"), QStringLiteral("**"),
                     EditTransaction::Kind::InsertText, QStringLiteral("Bold"));
}

bool StylizeController::toggleItalic() {
  return toggleStyle(InlineType::Emphasis,
                     QStringLiteral("*"), QStringLiteral("*"),
                     EditTransaction::Kind::InsertText, QStringLiteral("Italic"));
}

bool StylizeController::toggleCode() {
  return toggleStyle(InlineType::Code,
                     QStringLiteral("`"), QStringLiteral("`"),
                     EditTransaction::Kind::InsertText, QStringLiteral("Code"));
}

bool StylizeController::toggleStrikethrough() {
  return toggleStyle(InlineType::Strikethrough,
                     QStringLiteral("~~"), QStringLiteral("~~"),
                     EditTransaction::Kind::InsertText, QStringLiteral("Strikethrough"));
}

bool StylizeController::toggleInlineMath() {
  return toggleStyle(InlineType::InlineMath,
                     QStringLiteral("$"), QStringLiteral("$"),
                     EditTransaction::Kind::InsertText, QStringLiteral("Inline Formula"));
}

// ============================================================================
// Main dispatch: toggleStyle
// ============================================================================

bool StylizeController::toggleStyle(
    InlineType type, QString openMarker, QString closeMarker,
    EditTransaction::Kind kind, QString label) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  const SelectionRange sel = ctx_.selection->selection();
  if (!sel.anchor.isValid() || !sel.focus.isValid()) {
    return false;
  }

  // Multi-block path
  if (!sel.isCollapsed() && !sel.isSingleBlock()) {
    return toggleMultiBlock(type, openMarker, closeMarker, kind, label);
  }

  auto resolver = ctx_.contextResolver();
  BlockEditContext context;

  if (sel.isCollapsed()) {
    if (!resolver.current(context)) {
      return false;
    }
    return toggleCollapsed(context, type, openMarker, closeMarker, kind, label);
  }

  // Single-block range selection
  qsizetype localStart = -1, localEnd = -1;
  if (!resolver.selectionContext(context, localStart, localEnd)) {
    return false;
  }
  return toggleRange(context, localStart, localEnd,
                     type, openMarker, closeMarker, kind, label);
}

// ============================================================================
// Collapsed cursor toggle
// ============================================================================

bool StylizeController::toggleCollapsed(
    const BlockEditContext& context, InlineType type,
    const QString& openMarker, const QString& closeMarker,
    EditTransaction::Kind kind, const QString& label) {
  if (!context.editableNode || context.editableNode->inlines().isEmpty()) {
    return false;
  }

  const qsizetype contentBase = context.contentRange.byteStart;
  const qsizetype localOffset = qBound<qsizetype>(
      0, context.cursorSourceOffset - contentBase,
      context.contentText.size());

  QString contentText = context.contentText;
  qsizetype nextAnchorLocal = -1;

  const InlineNode* wrapping = findWrappingNode(
      context.editableNode->inlines(), type, contentBase, localOffset);

  if (wrapping) {
    // ── REMOVE: strip open/close markers ──
    const InlineRange openRange = wrapping->openMarkerRange();
    const InlineRange closeRange = wrapping->closeMarkerRange();
    if (!openRange.isValid() || !closeRange.isValid()) {
      return false;
    }

    const qsizetype openLocal = openRange.start - contentBase;
    const qsizetype openLen = openRange.length();
    const qsizetype closeLocal = closeRange.start - contentBase;
    const qsizetype closeLen = closeRange.length();

    // Remove in reverse order to preserve offsets
    contentText.remove(closeLocal, closeLen);
    contentText.remove(openLocal, openLen);

    // Compute adjusted cursor position
    nextAnchorLocal = localOffset;
    if (nextAnchorLocal >= closeLocal + closeLen) {
      nextAnchorLocal -= closeLen;
    } else if (nextAnchorLocal > closeLocal) {
      nextAnchorLocal = closeLocal;
    }
    if (nextAnchorLocal >= openLocal + openLen) {
      nextAnchorLocal -= openLen;
    } else if (nextAnchorLocal > openLocal) {
      nextAnchorLocal = openLocal;
    }
    nextAnchorLocal = qBound<qsizetype>(0, nextAnchorLocal, contentText.size());
  } else {
    // ── INSERT SKELETON ──
    const QString skeleton = openMarker + closeMarker;
    contentText.insert(localOffset, skeleton);
    nextAnchorLocal = localOffset + openMarker.size();
  }

  const qsizetype nextSourceOffset = contentBase + nextAnchorLocal;
  return applyStyleDelta(
      kind, label,
      contentBase,
      context.contentRange.byteEnd - contentBase,
      std::move(contentText),
      nextSourceOffset, nextSourceOffset,
      QVector<LocalEditNodeHint>{
          LocalEditNodeHint{context.editableNode->id(),
                            contentBase,
                            context.editableNode->type()}});
}

// ============================================================================
// Selection range toggle
// ============================================================================

bool StylizeController::toggleRange(
    const BlockEditContext& context,
    qsizetype localSelStart, qsizetype localSelEnd,
    InlineType type,
    const QString& openMarker, const QString& closeMarker,
    EditTransaction::Kind kind, const QString& label) {
  if (!context.editableNode || context.editableNode->inlines().isEmpty()) {
    return false;
  }

  const qsizetype contentBase = context.contentRange.byteStart;

  // 1. Detect style state
  const bool allHasStyle = hasStyleInRange(
      context.inlineProjection, type, localSelStart, localSelEnd);

  // 2. Collect overlapping markers from InlineNode tree
  QVector<MarkerSpan> markers = collectOverlappingMarkers(
      context.editableNode->inlines(), type,
      contentBase, localSelStart, localSelEnd);

  // 3. Build toggled content
  ToggledContent result = buildToggledContent(
      context.contentText, markers,
      localSelStart, localSelEnd,
      allHasStyle, openMarker, closeMarker);

  // 4. Compute cursor positions
  const qsizetype nextAnchorSource = contentBase + result.adjustedSelStart;
  const qsizetype nextFocusSource = contentBase + result.adjustedSelEnd;

  return applyStyleDelta(
      kind, label,
      contentBase,
      context.contentRange.byteEnd - contentBase,
      std::move(result.text),
      nextAnchorSource, nextFocusSource,
      QVector<LocalEditNodeHint>{
          LocalEditNodeHint{context.editableNode->id(),
                            contentBase,
                            context.editableNode->type()}});
}

// ============================================================================
// Multi-block toggle
// ============================================================================

bool StylizeController::toggleMultiBlock(
    InlineType type,
    const QString& openMarker, const QString& closeMarker,
    EditTransaction::Kind kind, const QString& label) {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  const SelectionRange sel = ctx_.selection->selection();
  if (sel.isCollapsed() || !sel.anchor.isValid() || !sel.focus.isValid() ||
      sel.anchor.blockId == sel.focus.blockId) {
    return false;
  }

  auto resolver = ctx_.contextResolver();

  // Compute absolute selection range
  qsizetype absSelStart = -1, absSelEnd = -1;
  if (!resolver.selectionSourceRange(absSelStart, absSelEnd)) {
    return false;
  }

  // Collect editable blocks overlapping the selection
  struct EditBlock {
    MarkdownNode* editableNode;
    qsizetype contentBase;
    qsizetype contentEnd;
    qsizetype localSelStart;
    qsizetype localSelEnd;
  };

  QVector<EditBlock> blocks;
  const auto collect = [&](const auto& self, MarkdownNode& node) -> void {
    const bool isParagraphInList =
        node.type() == BlockType::Paragraph &&
        node.parent() && node.parent()->type() == BlockType::ListItem;

    if (!isParagraphInList &&
        (node.type() == BlockType::Paragraph ||
         node.type() == BlockType::Heading ||
         node.type() == BlockType::ListItem)) {

      // Resolve the editable node (list item → primary paragraph)
      MarkdownNode* editable = &node;
      if (node.type() == BlockType::ListItem) {
        editable = primaryParagraph(node);
        if (!editable) {
          // Skip list items without editable content
          for (const auto& child : node.children()) {
            self(self, *child);
          }
          return;
        }
      }

      if (editable->type() != BlockType::Paragraph &&
          editable->type() != BlockType::Heading) {
        for (const auto& child : node.children()) {
          self(self, *child);
        }
        return;
      }

      BlockEditContext ctx;
      if (resolver.fill(*editable, ctx)) {
        const qsizetype cBase = ctx.contentRange.byteStart;
        const qsizetype cEnd = ctx.contentRange.byteEnd;
        if (cEnd > absSelStart && cBase < absSelEnd) {
          EditBlock block;
          block.editableNode = editable;
          block.contentBase = cBase;
          block.contentEnd = cEnd;
          block.localSelStart = qBound<qsizetype>(0, absSelStart - cBase,
                                                   ctx.contentText.size());
          block.localSelEnd = qBound<qsizetype>(0, absSelEnd - cBase,
                                                 ctx.contentText.size());
          if (block.localSelStart < block.localSelEnd) {
            blocks.push_back(block);
          }
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
  collect(collect, ctx_.session->document().root());

  if (blocks.isEmpty()) {
    return false;
  }

  // Sort blocks in reverse source order for safe editing
  std::sort(blocks.begin(), blocks.end(),
            [](const EditBlock& a, const EditBlock& b) {
              return a.contentBase > b.contentBase;
            });

  // Process each block
  const QString markdown = ctx_.session->markdownText();
  const qsizetype totalStart = blocks.last().contentBase;
  const qsizetype totalEnd = blocks.first().contentEnd;
  QString replacement = markdown.mid(totalStart, totalEnd - totalStart);

  // Track offset shift as we process blocks from end to start
  qsizetype runningShift = 0;
  qsizetype nextAnchorSource = -1;
  qsizetype nextFocusSource = -1;

  const bool anchorFirst = sel.anchor.blockId == sel.focus.blockId
                               ? true  // shouldn't happen in multi-block
                               : ctx_.session->document().node(sel.anchor.blockId) &&
                                     ctx_.session->document()
                                         .node(sel.anchor.blockId)
                                         ->sourceRange()
                                         .byteStart <=
                                     ctx_.session->document()
                                         .node(sel.focus.blockId)
                                         ->sourceRange()
                                         .byteStart;

  for (const EditBlock& block : blocks) {
    const qsizetype relBase = block.contentBase - totalStart;
    const qsizetype relSelStart = block.localSelStart + relBase - runningShift;
    const qsizetype relSelEnd = block.localSelEnd + relBase - runningShift;

    // Build context for detection
    BlockEditContext ctx;
    if (!resolver.fill(*block.editableNode, ctx)) {
      continue;
    }

    const bool allHasStyle = hasStyleInRange(
        ctx.inlineProjection, type, block.localSelStart, block.localSelEnd);

    QVector<MarkerSpan> markers = collectOverlappingMarkers(
        block.editableNode->inlines(), type,
        block.contentBase, block.localSelStart, block.localSelEnd);

    ToggledContent result = buildToggledContent(
        ctx.contentText, markers,
        block.localSelStart, block.localSelEnd,
        allHasStyle, openMarker, closeMarker);

    // Replace the block's content in the accumulated replacement string
    const qsizetype oldLen = block.contentEnd - block.contentBase;
    replacement.replace(relBase, oldLen, result.text);

    // Compute cursor offsets for the first and last blocks
    const qsizetype blockAnchorSource = block.contentBase + result.adjustedSelStart;
    const qsizetype blockFocusSource = block.contentBase + result.adjustedSelEnd;

    // We only need anchor (first block) and focus (last block)
    // blocks are sorted in reverse order, so:
    // first block = the one closest to start = last in the vector
    // last block = the one closest to end = first in the vector
    // Since we iterate in reverse, the first iteration is the last block (focus side),
    // and the last iteration is the first block (anchor side)
    if (block.contentBase == blocks.first().contentBase) {
      // This is the last block in source order = focus side (if anchorFirst)
      // or anchor side (if !anchorFirst)
      if (anchorFirst) {
        nextFocusSource = blockFocusSource;
      } else {
        nextAnchorSource = blockAnchorSource;
      }
    }
    if (block.contentBase == blocks.last().contentBase) {
      // This is the first block in source order = anchor side (if anchorFirst)
      // or focus side (if !anchorFirst)
      if (anchorFirst) {
        nextAnchorSource = blockAnchorSource;
      } else {
        nextFocusSource = blockFocusSource;
      }
    }

    runningShift += oldLen - result.text.size();
  }

  if (nextAnchorSource < 0 || nextFocusSource < 0) {
    return false;
  }

  QVector<LocalEditNodeHint> nodeHints;
  for (const EditBlock& block : blocks) {
    nodeHints.push_back(LocalEditNodeHint{
        block.editableNode->id(), block.contentBase, block.editableNode->type()});
  }

  return applyStyleDelta(
      kind, label,
      totalStart, totalEnd - totalStart,
      std::move(replacement),
      nextAnchorSource, nextFocusSource,
      std::move(nodeHints));
}

// ============================================================================
// insertLink / insertImage (non-toggle, but now work on formatted text)
// ============================================================================

bool StylizeController::insertLink() {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  const SelectionRange sel = ctx_.selection->selection();
  if (!sel.anchor.isValid() || !sel.focus.isValid()) {
    return false;
  }

  // Cross-block links not supported
  if (sel.anchor.blockId != sel.focus.blockId) {
    emit unsupportedStyleRequested(
        QStringLiteral("Link currently supports a single editable block selection."));
    return false;
  }

  auto resolver = ctx_.contextResolver();
  BlockEditContext context;

  if (sel.isCollapsed()) {
    if (!resolver.current(context)) {
      return false;
    }
    const qsizetype contentBase = context.contentRange.byteStart;
    const qsizetype localOffset = qBound<qsizetype>(
        0, context.cursorSourceOffset - contentBase,
        context.contentText.size());

    QString contentText = context.contentText;
    contentText.insert(localOffset, QStringLiteral("[](url)"));
    const qsizetype nextOffset = contentBase + localOffset + 1;  // cursor after [

    return applyStyleDelta(
        EditTransaction::Kind::InsertText, QStringLiteral("Link"),
        contentBase,
        context.contentRange.byteEnd - contentBase,
        std::move(contentText),
        nextOffset, nextOffset,
        QVector<LocalEditNodeHint>{
            LocalEditNodeHint{context.editableNode->id(), contentBase,
                              context.editableNode->type()}});
  }

  qsizetype localStart = -1, localEnd = -1;
  if (!resolver.selectionContext(context, localStart, localEnd)) {
    return false;
  }

  const qsizetype contentBase = context.contentRange.byteStart;
  const QString& contentText = context.contentText;
  const QString selected = contentText.mid(localStart, localEnd - localStart);
  const QString replacement =
      contentText.left(localStart) +
      QStringLiteral("[") + selected + QStringLiteral("](url)") +
      contentText.mid(localEnd);

  const qsizetype nextAnchor = contentBase + localStart + 1;
  const qsizetype nextFocus = contentBase + localStart + 1 + selected.size();

  return applyStyleDelta(
      EditTransaction::Kind::InsertText, QStringLiteral("Link"),
      contentBase,
      context.contentRange.byteEnd - contentBase,
      std::move(replacement),
      nextAnchor, nextFocus,
      QVector<LocalEditNodeHint>{
          LocalEditNodeHint{context.editableNode->id(), contentBase,
                            context.editableNode->type()}});
}

bool StylizeController::insertImage() {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }

  const SelectionRange sel = ctx_.selection->selection();
  if (!sel.anchor.isValid() || !sel.focus.isValid()) {
    return false;
  }

  if (sel.anchor.blockId != sel.focus.blockId) {
    emit unsupportedStyleRequested(
        QStringLiteral("Image currently supports a single editable block selection."));
    return false;
  }

  auto resolver = ctx_.contextResolver();
  BlockEditContext context;

  if (sel.isCollapsed()) {
    if (!resolver.current(context)) {
      return false;
    }
    const qsizetype contentBase = context.contentRange.byteStart;
    const qsizetype localOffset = qBound<qsizetype>(
        0, context.cursorSourceOffset - contentBase,
        context.contentText.size());

    QString contentText = context.contentText;
    contentText.insert(localOffset, QStringLiteral("![](url)"));
    const qsizetype nextOffset = contentBase + localOffset + 2;  // cursor after ![

    return applyStyleDelta(
        EditTransaction::Kind::InsertText, QStringLiteral("Image"),
        contentBase,
        context.contentRange.byteEnd - contentBase,
        std::move(contentText),
        nextOffset, nextOffset,
        QVector<LocalEditNodeHint>{
            LocalEditNodeHint{context.editableNode->id(), contentBase,
                              context.editableNode->type()}});
  }

  qsizetype localStart = -1, localEnd = -1;
  if (!resolver.selectionContext(context, localStart, localEnd)) {
    return false;
  }

  const qsizetype contentBase = context.contentRange.byteStart;
  const QString& contentText = context.contentText;
  const QString selected = contentText.mid(localStart, localEnd - localStart);
  const QString replacement =
      contentText.left(localStart) +
      QStringLiteral("![") + selected + QStringLiteral("](url)") +
      contentText.mid(localEnd);

  const qsizetype nextAnchor = contentBase + localStart + 2;
  const qsizetype nextFocus = contentBase + localStart + 2 + selected.size();

  return applyStyleDelta(
      EditTransaction::Kind::InsertText, QStringLiteral("Image"),
      contentBase,
      context.contentRange.byteEnd - contentBase,
      std::move(replacement),
      nextAnchor, nextFocus,
      QVector<LocalEditNodeHint>{
          LocalEditNodeHint{context.editableNode->id(), contentBase,
                            context.editableNode->type()}});
}

// ============================================================================
// Style detection
// ============================================================================

bool StylizeController::hasStyleInRange(
    const InlineProjection& projection, InlineType type,
    qsizetype localSourceStart, qsizetype localSourceEnd) const {
  bool foundContent = false;

  for (const auto& span : projection.spans()) {
    // Skip non-content spans
    if (span.kind == InlineSpanKind::OpenMarker ||
        span.kind == InlineSpanKind::CloseMarker ||
        span.kind == InlineSpanKind::HiddenSyntax) {
      continue;
    }

    // Check overlap with selection (using source offsets)
    if (span.sourceEnd <= localSourceStart || span.sourceStart >= localSourceEnd) {
      continue;
    }

    foundContent = true;

    bool hasStyle = false;
    switch (type) {
      case InlineType::Strong:
        hasStyle = span.bold;
        break;
      case InlineType::Emphasis:
        hasStyle = span.italic;
        break;
      case InlineType::Strikethrough:
        hasStyle = span.strike;
        break;
      case InlineType::Code:
        hasStyle = (span.type == InlineType::Code &&
                    span.kind == InlineSpanKind::Text);
        break;
      case InlineType::InlineMath:
        hasStyle = (span.type == InlineType::InlineMath &&
                    span.kind == InlineSpanKind::Text);
        break;
      default:
        break;
    }

    if (!hasStyle) {
      return false;
    }
  }

  return foundContent;
}

// ============================================================================
// InlineNode tree helpers
// ============================================================================

const InlineNode* StylizeController::findWrappingNode(
    const QVector<InlineNode>& inlines, InlineType type,
    qsizetype contentBase, qsizetype localSourceOffset) const {
  for (const InlineNode& node : inlines) {
    if (node.type() == type) {
      const InlineRange content = node.contentRange();
      if (content.isValid()) {
        const qsizetype localStart = content.start - contentBase;
        const qsizetype localEnd = content.end - contentBase;
        if (localSourceOffset >= localStart && localSourceOffset <= localEnd) {
          return &node;
        }
      }
    }

    // Recurse into children (for nested formatting like bold > italic)
    if (!node.children().isEmpty()) {
      if (const InlineNode* found =
              findWrappingNode(node.children(), type, contentBase, localSourceOffset)) {
        return found;
      }
    }
  }
  return nullptr;
}

QVector<StylizeController::MarkerSpan>
StylizeController::collectOverlappingMarkers(
    const QVector<InlineNode>& inlines, InlineType targetType,
    qsizetype contentBase,
    qsizetype localSelStart, qsizetype localSelEnd) const {
  QVector<MarkerSpan> markers;
  collectOverlappingMarkersRecursive(
      inlines, targetType, contentBase,
      localSelStart, localSelEnd, markers);
  return markers;
}

void StylizeController::collectOverlappingMarkersRecursive(
    const QVector<InlineNode>& inlines, InlineType targetType,
    qsizetype contentBase,
    qsizetype localSelStart, qsizetype localSelEnd,
    QVector<MarkerSpan>& markers) const {
  for (const InlineNode& node : inlines) {
    if (node.type() == targetType) {
      const InlineRange content = node.contentRange();
      const InlineRange openRange = node.openMarkerRange();
      const InlineRange closeRange = node.closeMarkerRange();

      if (content.isValid() && openRange.isValid() && closeRange.isValid()) {
        const qsizetype localContentStart = content.start - contentBase;
        const qsizetype localContentEnd = content.end - contentBase;

        // Does this node's content overlap with the selection?
        if (localContentStart < localSelEnd && localContentEnd > localSelStart) {
          markers.append({openRange.start - contentBase,
                          openRange.end - contentBase});
          markers.append({closeRange.start - contentBase,
                          closeRange.end - contentBase});
        }
      }

      // Recurse into children of this node to find same-type markers
      // at deeper nesting levels (unlikely but safe to handle)
      if (!node.children().isEmpty()) {
        collectOverlappingMarkersRecursive(
            node.children(), targetType, contentBase,
            localSelStart, localSelEnd, markers);
      }
    } else {
      // Not the target type, recurse into children
      if (!node.children().isEmpty()) {
        collectOverlappingMarkersRecursive(
            node.children(), targetType, contentBase,
            localSelStart, localSelEnd, markers);
      }
    }
  }
}

// ============================================================================
// Content builder
// ============================================================================

StylizeController::ToggledContent StylizeController::buildToggledContent(
    const QString& contentText,
    const QVector<MarkerSpan>& markers,
    qsizetype localSelStart, qsizetype localSelEnd,
    bool allHasStyle,
    const QString& openMarker, const QString& closeMarker) const {
  // Sort markers by position ascending
  QVector<MarkerSpan> sorted = markers;
  std::sort(sorted.begin(), sorted.end(),
            [](const MarkerSpan& a, const MarkerSpan& b) {
              return a.localStart < b.localStart;
            });

  // Build result left-to-right, skipping marker ranges
  QString result;
  result.reserve(contentText.size());
  qsizetype pos = 0;
  qsizetype removedBeforeSelStart = 0;
  qsizetype removedBeforeSelEnd = 0;

  for (const MarkerSpan& marker : sorted) {
    // Clamp marker to content bounds
    const qsizetype mStart = qMax<qsizetype>(0, marker.localStart);
    const qsizetype mEnd = qMin<qsizetype>(contentText.size(), marker.localEnd);
    if (mStart >= mEnd) {
      continue;
    }

    // Add text before this marker
    if (mStart > pos) {
      result += contentText.mid(pos, mStart - pos);
    }

    // Track removal impact on selection boundaries
    if (mEnd <= localSelStart) {
      // Marker entirely before selection
      const qsizetype len = mEnd - mStart;
      removedBeforeSelStart += len;
      removedBeforeSelEnd += len;
    } else if (mStart >= localSelEnd) {
      // Marker entirely after selection — no impact on selection offsets
    } else {
      // Marker overlaps with selection
      const qsizetype overlapBeforeSel =
          qMax<qsizetype>(0, localSelStart - mStart);
      const qsizetype overlapInSel =
          qMin(mEnd, localSelEnd) - qMax(mStart, localSelStart);
      const qsizetype overlapAfterSel =
          qMax<qsizetype>(0, mEnd - localSelEnd);
      Q_UNUSED(overlapAfterSel);
      removedBeforeSelStart += overlapBeforeSel;
      removedBeforeSelEnd += overlapBeforeSel + qMax<qsizetype>(0, overlapInSel);
    }

    pos = mEnd;
  }

  // Add remaining text after last marker
  if (pos < contentText.size()) {
    result += contentText.mid(pos);
  }

  // Compute adjusted selection
  qsizetype adjustedStart = localSelStart - removedBeforeSelStart;
  qsizetype adjustedEnd = localSelEnd - removedBeforeSelEnd;

  // Clamp to valid range
  adjustedStart = qBound<qsizetype>(0, adjustedStart, result.size());
  adjustedEnd = qBound<qsizetype>(0, adjustedEnd, result.size());

  // If NOT all-has-style, ADD new markers around the adjusted selection
  if (!allHasStyle) {
    result.insert(adjustedEnd, closeMarker);
    result.insert(adjustedStart, openMarker);
    // Adjust selection to be inside the new markers
    adjustedStart += openMarker.size();
    adjustedEnd += openMarker.size();
  }

  return {std::move(result), adjustedStart, adjustedEnd};
}

// ============================================================================
// Delta application (unchanged)
// ============================================================================

bool StylizeController::applyStyleDelta(
    EditTransaction::Kind kind,
    const QString& label,
    qsizetype sourceStart,
    qsizetype removedLength,
    QString insertedText,
    qsizetype nextAnchorSourceOffset,
    qsizetype nextFocusSourceOffset,
    QVector<LocalEditNodeHint> nodeHints) {
  if (!ctx_.hasSession() || sourceStart < 0 || removedLength < 0 ||
      sourceStart + removedLength > ctx_.session->markdownText().size()) {
    return false;
  }

  const CursorPosition beforeCursor =
      ctx_.selection && ctx_.selection->hasCursor()
          ? ctx_.selection->cursorPosition()
          : CursorPosition();
  const QString removedText =
      ctx_.session->markdownText().mid(sourceStart, removedLength);
  QVector<NodeId> affectedNodes;
  for (const LocalEditNodeHint& hint : nodeHints) {
    if (hint.nodeId.isValid() && !affectedNodes.contains(hint.nodeId)) {
      affectedNodes.push_back(hint.nodeId);
    }
  }
  if (!ctx_.session->applyTextDelta(sourceStart, removedLength, insertedText,
                                     true, std::move(nodeHints))) {
    return false;
  }

  SelectionRange nextSelection;
  nextSelection.anchor = cursorForSourceOffset(nextAnchorSourceOffset);
  nextSelection.focus = cursorForSourceOffset(nextFocusSourceOffset);
  if (ctx_.selection && nextSelection.focus.isValid()) {
    ctx_.selection->setSelection(nextSelection);
  }
  if (nextSelection.anchor.blockId.isValid() &&
      !affectedNodes.contains(nextSelection.anchor.blockId)) {
    affectedNodes.push_back(nextSelection.anchor.blockId);
  }
  if (nextSelection.focus.blockId.isValid() &&
      !affectedNodes.contains(nextSelection.focus.blockId)) {
    affectedNodes.push_back(nextSelection.focus.blockId);
  }
  if (ctx_.undoStack && beforeCursor.isValid() && nextSelection.focus.isValid()) {
    ctx_.undoStack->push(EditTransaction(
        kind, label,
        TextDeltaCommand{
            TextDelta{sourceStart, removedText, insertedText},
            beforeCursor, nextSelection.focus,
            std::move(affectedNodes)}));
  }
  if (ctx_.brushQueue) {
    if (!affectedNodes.isEmpty()) {
      ctx_.brushQueue->requestBlocksRefresh(std::move(affectedNodes));
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
  }
  return true;
}

CursorPosition StylizeController::cursorForSourceOffset(qsizetype sourceOffset) const {
  CursorPosition cursor;
  if (!ctx_.hasSession()) {
    return cursor;
  }

  auto resolver = ctx_.contextResolver();
  MarkdownNode* node = resolver.nodeAtContentSourceOffset(
      ctx_.session->document().root(), sourceOffset);
  if (!node) {
    return cursor;
  }

  BlockEditContext context;
  if (!resolver.fill(*node, context)) {
    return cursor;
  }

  cursor.blockId = node->id();
  cursor.text.nodeId = context.editableNode ? context.editableNode->id() : node->id();
  cursor.text.textOffset = qBound<qsizetype>(
      0, sourceOffset - context.contentRange.byteStart, context.contentText.size());
  cursor.text.sourceOffset = sourceOffset;
  return cursor;
}

}  // namespace muffin
