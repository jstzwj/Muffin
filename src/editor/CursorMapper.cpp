#include "editor/CursorMapper.h"

#include "document/MarkdownNode.h"

#include <QVector>

namespace muffin {

CursorPosition cursorForBlock(NodeId blockId, qsizetype offset, const DocumentSession* session) {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = offset;
  const auto* node = session ? session->document().node(blockId) : nullptr;
  if (node && node->type() == BlockType::TableCell) {
    for (auto* owner = node->parent(); owner; owner = owner->parent()) {
      if (owner->type() == BlockType::Table) {
        cursor.blockId = owner->id();
        break;
      }
    }
  }
  return cursor;
}

CursorPosition cursorForSourceOffset(const BlockEditContextResolver& resolver, qsizetype sourceOffset,
                                     bool preferLaterEmptyAtOffset) {
  CursorPosition cursor;
  if (!resolver.session()) {
    return cursor;
  }

  MarkdownNode* host = resolver.session()->document().topLevelBlockAtOffset(sourceOffset);
  if (!host) {
    return cursor;
  }

  // topLevelBlockAtOffset returns the earliest containing block at a shared boundary. Preserve
  // preferLaterEmptyAtOffset by considering only the immediately following top-level blocks whose
  // inclusive source ranges contain the same offset. In normal text there is exactly one candidate.
  QVector<MarkdownNode*> candidates;
  candidates.push_back(host);
  for (MarkdownNode* next = host->nextSibling(); next && next->sourceRange().containsByte(sourceOffset);
       next = next->nextSibling()) {
    candidates.push_back(next);
  }

  if (preferLaterEmptyAtOffset) {
    for (const auto* candidate : candidates) {
      const auto range = candidate->sourceRange();
      if (candidate->type() == BlockType::Paragraph && range.byteStart == sourceOffset &&
          range.byteEnd == sourceOffset) {
        cursor = cursorForBlock(candidate->id(), 0, resolver.session());
        cursor.text.sourceOffset = sourceOffset;
        return cursor;
      }
    }
  }

  // A source offset that lands inside a literal block (code/math/HTML/front matter) resolves to
  // that block. nodeAtContentSourceOffset only matches inline-editable text, so without this a
  // freshly committed "```"/"$$" block would leave the post-edit caret unresolvable.
  for (MarkdownNode* candidate : candidates) {
    qsizetype literalContentStart = -1;
    if (MarkdownNode* literal = resolver.literalBlockAtSourceOffset(*candidate, sourceOffset, literalContentStart)) {
      cursor.blockId = literal->id();
      cursor.text.nodeId = literal->id();
      cursor.text.textOffset = qBound<qsizetype>(0, sourceOffset - literalContentStart, literal->literal().size());
      cursor.text.sourceOffset = sourceOffset;
      return cursor;
    }
  }

  // If the offset is on a top-level block whose subtree hosts NO inline-editable text, resolve it
  // straight to a block-after caret instead of walking the tree. This is intentionally BROADER than
  // isNonTextLeafBlock (caretRestsOnNonTextBlock): it ALSO covers literal blocks (code/math/html/
  // front matter) whose offset sits outside their literal content region — those have no inline text
  // to land on either, so a block-after caret is the right resolution (offsets INSIDE a literal were
  // already returned by literalBlockAtSourceOffset above). Containers (lists/tables/quotes) have
  // editable descendants, so firstEditableDescendant is non-null for them and this is skipped —
  // leaving list-exit / table-caret resolution on its existing path (which relies on
  // cursorForSourceOffset returning invalid there).
  if (!resolver.firstEditableDescendant(*host)) {
    return blockAfterCursor(*host, sourceOffset);
  }

  MarkdownNode* node = nullptr;
  for (MarkdownNode* candidate : candidates) {
    if (MarkdownNode* found = resolver.nodeAtContentSourceOffset(*candidate, sourceOffset, preferLaterEmptyAtOffset)) {
      node = found;
      if (!preferLaterEmptyAtOffset) {
        break;
      }
    }
  }
  if (!node) {
    return cursor;
  }
  return cursorForSourceInNode(resolver, *node, sourceOffset);
}

CursorPosition cursorForSourceInNode(const BlockEditContextResolver& resolver, MarkdownNode& node,
                                     qsizetype sourceOffset) {
  BlockEditContext context;
  if (!resolver.fill(node, context)) {
    return {};
  }
  return cursorForSourceInContext(resolver, context, sourceOffset);
}

CursorPosition cursorForSourceInContext(const BlockEditContextResolver& resolver, const BlockEditContext& context,
                                        qsizetype sourceOffset) {
  const qsizetype localSourceOffset =
      qBound<qsizetype>(0, sourceOffset - context.contentRange.byteStart, context.contentText.size());
  qsizetype visibleOffset = -1;
  if (context.inlineProjection.visibleOffsetForSourceOffset(localSourceOffset, visibleOffset)) {
    CursorPosition cursorForVisible = cursorForBlock(context.blockId, visibleOffset, resolver.session());
    cursorForVisible.text.sourceOffset = sourceOffset;
    return cursorForVisible;
  }
  CursorPosition fallbackCursor = cursorForBlock(
      context.blockId, qBound<qsizetype>(0, localSourceOffset, context.visibleText.size()), resolver.session());
  fallbackCursor.text.sourceOffset = sourceOffset;
  return fallbackCursor;
}

CursorPosition blockAfterCursor(const MarkdownNode& host, qsizetype sourceOffset) {
  CursorPosition cursor;
  cursor.blockId = host.id();
  cursor.text.nodeId = host.id();
  cursor.text.sourceOffset = sourceOffset;
  cursor.afterBlock = true;
  return cursor;
}

CursorPosition cursorAfterEdit(const BlockEditContextResolver& resolver, CursorPosition preferredCursor,
                               qsizetype fallbackSourceOffset, bool preferLaterEmptyAtOffset) {
  const DocumentSession* session = resolver.session();
  if (session && preferredCursor.isValid()) {
    if (preferredCursor.text.sourceOffset >= 0) {
      CursorPosition sourceCursor =
          cursorForSourceOffset(resolver, preferredCursor.text.sourceOffset, preferLaterEmptyAtOffset);
      if (sourceCursor.isValid()) {
        return sourceCursor;
      }
    }
    if (MarkdownNode* node = session->document().node(preferredCursor.blockId)) {
      return cursorForBlock(node->id(), preferredCursor.text.textOffset, session);
    }
  }
  CursorPosition cursor = cursorForSourceOffset(resolver, fallbackSourceOffset, preferLaterEmptyAtOffset);
  if (!cursor.isValid() && session && fallbackSourceOffset >= 0) {
    if (auto* host = session->document().topLevelBlockAtOffset(fallbackSourceOffset))
      return blockAfterCursor(*host, fallbackSourceOffset);
  }
  return cursor;
}

} // namespace muffin
