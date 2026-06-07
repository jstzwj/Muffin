#include "commands/ParagraphController.h"

#include "app/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "document/SourceRangeUtil.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/InlineSplit.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"

#include <algorithm>

namespace muffin {

ParagraphController::ParagraphController(QObject* parent) : QObject(parent) {}

void ParagraphController::setContext(const EditorContext& ctx) {
  ctx_ = ctx;
}

// ---------------------------------------------------------------------------
// Query methods
// ---------------------------------------------------------------------------

int ParagraphController::currentHeadingLevel() const {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    return 0;
  }
  return context.headingLevel;
}

bool ParagraphController::isOnEditableBlock() const {
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor()) {
    return false;
  }
  // Block-level commands should not apply when a multi-block selection is active
  const SelectionRange sel = ctx_.selection->selection();
  if (!sel.isCollapsed()) {
    return false;
  }
  const NodeId blockId = ctx_.selection->cursorPosition().blockId;
  if (!blockId.isValid()) {
    return false;
  }
  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node) {
    return false;
  }
  return node->type() == BlockType::Paragraph || node->type() == BlockType::Heading;
}

// ---------------------------------------------------------------------------
// resolveBlockContext — gather current block's source ranges
// ---------------------------------------------------------------------------

bool ParagraphController::resolveBlockContext(BlockContext& context) const {
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor()) {
    return false;
  }

  const NodeId blockId = ctx_.selection->cursorPosition().blockId;
  if (!blockId.isValid()) {
    return false;
  }

  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node) {
    return false;
  }

  // Only Paragraph and Heading support paragraph-level commands
  if (node->type() != BlockType::Paragraph && node->type() != BlockType::Heading) {
    return false;
  }

  const SourceRange range = node->sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return false;
  }

  const QString markdown = ctx_.session->markdownText();
  qsizetype blockStart = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype blockEnd = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (blockStart < 0 || blockEnd < blockStart) {
    return false;
  }

  // Compute content start (skip heading prefix)
  qsizetype contentStart = blockStart;
  int headingLevel = 0;
  if (node->type() == BlockType::Heading) {
    headingLevel = node->headingLevel();
    while (contentStart < blockEnd && markdown.at(contentStart) == QLatin1Char('#')) {
      ++contentStart;
    }
    if (contentStart < blockEnd && markdown.at(contentStart).isSpace()) {
      ++contentStart;
    }
  }

  context.node = node;
  context.editableNode = node;
  context.blockId = blockId;
  context.blockType = node->type();
  context.blockStart = blockStart;
  context.blockEnd = blockEnd;
  context.contentStart = contentStart;
  context.contentEnd = blockEnd;
  context.contentText = markdown.mid(contentStart, blockEnd - contentStart);
  context.headingLevel = headingLevel;
  context.cursorSourceOffset = ctx_.selection->cursorPosition().text.sourceOffset;
  return true;
}

// ---------------------------------------------------------------------------
// applyBlockDelta — core mutation pipeline (mirrors StylizeController::applyStyleDelta)
// ---------------------------------------------------------------------------

bool ParagraphController::applyBlockDelta(
    EditTransaction::Kind kind,
    const QString& label,
    qsizetype sourceStart,
    qsizetype removedLength,
    QString insertedText,
    qsizetype nextCursorSourceOffset,
    QVector<LocalEditNodeHint> nodeHints,
    bool structureEdit) {
  if (!ctx_.session || sourceStart < 0 || removedLength < 0 ||
      sourceStart + removedLength > ctx_.session->markdownText().size()) {
    return false;
  }

  const CursorPosition beforeCursor =
      ctx_.selection && ctx_.selection->hasCursor() ? ctx_.selection->cursorPosition() : CursorPosition();
  const QString removedText = ctx_.session->markdownText().mid(sourceStart, removedLength);

  QVector<NodeId> affectedNodes;
  for (const LocalEditNodeHint& hint : nodeHints) {
    if (hint.nodeId.isValid() && !affectedNodes.contains(hint.nodeId)) {
      affectedNodes.push_back(hint.nodeId);
    }
  }

  const bool appliedLocally = ctx_.session->applyTextDelta(sourceStart, removedLength, insertedText, true, std::move(nodeHints));
  if (!appliedLocally) {
    return false;
  }

  const CursorPosition nextCursor = cursorForSourceOffset(
      qBound<qsizetype>(0, nextCursorSourceOffset, ctx_.session->markdownText().size()));
  if (ctx_.selection && nextCursor.isValid()) {
    ctx_.selection->setCursorPosition(nextCursor);
  }

  // Build the cursor for the undo transaction — prefer the resolved cursor,
  // fall back to the beforeCursor if the source offset lands in a non-text block
  const CursorPosition undoCursor = nextCursor.isValid() ? nextCursor : beforeCursor;

  if (undoCursor.blockId.isValid() && !affectedNodes.contains(undoCursor.blockId)) {
    affectedNodes.push_back(undoCursor.blockId);
  }

  if (ctx_.undoStack && beforeCursor.isValid()) {
    ctx_.undoStack->push(EditTransaction(
        kind,
        label,
        TextDeltaCommand{
            TextDelta{sourceStart, removedText, insertedText},
            beforeCursor,
            undoCursor,
            std::move(affectedNodes)}));
  }

  if (ctx_.brushQueue) {
    if (structureEdit || ctx_.session->lastLocalEditChangedTopLevelStructure()) {
      ctx_.brushQueue->requestTopLevelRangeRefresh(ctx_.session->lastLocalTopLevelRangeChange());
    } else if (!affectedNodes.isEmpty()) {
      ctx_.brushQueue->requestBlocksRefresh(std::move(affectedNodes));
    } else {
      ctx_.brushQueue->requestFullRefresh();
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// cursorForSourceOffset / paragraphAtSourceOffset
// (mirrors StylizeController's implementation)
// ---------------------------------------------------------------------------

CursorPosition ParagraphController::cursorForSourceOffset(qsizetype sourceOffset) const {
  CursorPosition cursor;
  if (!ctx_.session) {
    return cursor;
  }

  MarkdownNode* node = paragraphAtSourceOffset(ctx_.session->document().root(), sourceOffset);
  if (!node) {
    return cursor;
  }

  MarkdownNode* editable = node;
  if (node->type() == BlockType::ListItem) {
    editable = primaryParagraph(*node);
  }
  if (!editable) {
    return cursor;
  }

  const SourceRange range = editable->sourceRange();
  const QString markdown = ctx_.session->markdownText();
  qsizetype contentStart = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype contentEnd = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (contentStart < 0 || contentEnd < contentStart) {
    return cursor;
  }

  if (editable->type() == BlockType::Heading) {
    while (contentStart < contentEnd && markdown.at(contentStart) == QLatin1Char('#')) {
      ++contentStart;
    }
    if (contentStart < contentEnd && markdown.at(contentStart).isSpace()) {
      ++contentStart;
    }
  }

  cursor.blockId = node->id();
  cursor.text.nodeId = editable->id();
  cursor.text.textOffset = qBound<qsizetype>(0, sourceOffset - contentStart, contentEnd - contentStart);
  cursor.text.sourceOffset = sourceOffset;
  return cursor;
}

MarkdownNode* ParagraphController::paragraphAtSourceOffset(MarkdownNode& node, qsizetype sourceOffset) const {
  const SourceRange range = node.sourceRange();
  if (range.byteStart >= 0 && range.byteEnd >= range.byteStart &&
      (sourceOffset < range.byteStart || sourceOffset > range.byteEnd)) {
    return nullptr;
  }

  if (node.type() == BlockType::Paragraph || node.type() == BlockType::Heading ||
      node.type() == BlockType::LinkDefinition || node.type() == BlockType::FootnoteDefinition) {
    return &node;
  }

  for (const auto& child : node.children()) {
    if (MarkdownNode* found = paragraphAtSourceOffset(*child, sourceOffset)) {
      return found;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Heading commands
// ---------------------------------------------------------------------------

bool ParagraphController::setHeadingLevel(int level) {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    return false;
  }

  if (context.headingLevel == level) {
    return true;  // no-op
  }

  QString newPrefix;
  if (level > 0) {
    newPrefix = QStringLiteral("#").repeated(level) + QLatin1Char(' ');
  }

  const QString insertedText = newPrefix + context.contentText;
  const qsizetype removedLength = context.blockEnd - context.blockStart;

  // Compute cursor offset within the content
  const qsizetype cursorContentOffset =
      context.cursorSourceOffset >= context.contentStart
          ? qBound<qsizetype>(0, context.cursorSourceOffset - context.contentStart, context.contentText.size())
          : 0;
  const qsizetype nextCursorOffset = context.blockStart + newPrefix.size() + cursorContentOffset;

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      level > 0 ? QStringLiteral("Set Heading %1").arg(level) : QStringLiteral("Set Paragraph"),
      context.blockStart,
      removedLength,
      insertedText,
      nextCursorOffset,
      {LocalEditNodeHint{context.blockId, context.blockStart, level > 0 ? BlockType::Heading : BlockType::Paragraph}},
      true);
}

bool ParagraphController::promoteHeading() {
  const int level = currentHeadingLevel();
  if (level <= 1) {
    return false;
  }
  return setHeadingLevel(level - 1);
}

bool ParagraphController::demoteHeading() {
  const int level = currentHeadingLevel();
  if (level <= 0 || level >= 6) {
    return false;
  }
  return setHeadingLevel(level + 1);
}

// ---------------------------------------------------------------------------
// Block insert commands
// ---------------------------------------------------------------------------

bool ParagraphController::insertFormulaBlock() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    // Even without a block context, try inserting at document end
    if (!ctx_.session) {
      return false;
    }
    const QString markdown = ctx_.session->markdownText();
    const QString inserted = markdown.isEmpty() ? QStringLiteral("$$\n\n$$") : QStringLiteral("\n\n$$\n\n$$");
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Formula Block"),
        offset, 0, inserted,
        offset + (markdown.isEmpty() ? 3 : 5),  // cursor between $$ markers
        {}, true);
  }

  const QString inserted = QStringLiteral("\n\n$$\n\n$$");
  const qsizetype nextCursor = context.blockEnd + 5;  // between $$ markers
  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Formula Block"),
      context.blockEnd, 0, inserted,
      nextCursor, {}, true);
}

bool ParagraphController::insertCodeBlock() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    if (!ctx_.session) {
      return false;
    }
    const QString markdown = ctx_.session->markdownText();
    const QString inserted = markdown.isEmpty() ? QStringLiteral("```\n\n```") : QStringLiteral("\n\n```\n\n```");
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Code Block"),
        offset, 0, inserted,
        offset + (markdown.isEmpty() ? 4 : 6),
        {}, true);
  }

  const QString inserted = QStringLiteral("\n\n```\n\n```");
  const qsizetype nextCursor = context.blockEnd + 6;  // between ``` markers
  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Code Block"),
      context.blockEnd, 0, inserted,
      nextCursor, {}, true);
}

bool ParagraphController::insertLinkReference() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    if (!ctx_.session) {
      return false;
    }
    const QString markdown = ctx_.session->markdownText();
    const QString inserted = markdown.isEmpty() ? QStringLiteral("[]: ") : QStringLiteral("\n\n[]: ");
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Link Reference"),
        offset, 0, inserted,
        offset + (markdown.isEmpty() ? 1 : 3),
        {}, true);
  }

  const QString inserted = QStringLiteral("\n\n[]: ");
  const qsizetype nextCursor = context.blockEnd + 3;
  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Link Reference"),
      context.blockEnd, 0, inserted,
      nextCursor, {}, true);
}

bool ParagraphController::insertFootnoteDefinition() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    if (!ctx_.session) {
      return false;
    }
    const QString markdown = ctx_.session->markdownText();
    const QString inserted = markdown.isEmpty() ? QStringLiteral("[^]: ") : QStringLiteral("\n\n[^]: ");
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Footnote"),
        offset, 0, inserted,
        offset + (markdown.isEmpty() ? 2 : 4),
        {}, true);
  }

  const QString inserted = QStringLiteral("\n\n[^]: ");
  const qsizetype nextCursor = context.blockEnd + 4;
  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Footnote"),
      context.blockEnd, 0, inserted,
      nextCursor, {}, true);
}

bool ParagraphController::insertHorizontalRule() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    if (!ctx_.session) {
      return false;
    }
    const QString markdown = ctx_.session->markdownText();
    const QString inserted = markdown.isEmpty() ? QStringLiteral("---\n\n") : QStringLiteral("\n\n---\n\n");
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Horizontal Rule"),
        offset, 0, inserted,
        offset + inserted.size(),
        {}, true);
  }

  const QString inserted = QStringLiteral("\n\n---\n\n");
  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Horizontal Rule"),
      context.blockEnd, 0, inserted,
      context.blockEnd + inserted.size(),
      {}, true);
}

// ---------------------------------------------------------------------------
// Block conversion commands
// ---------------------------------------------------------------------------

bool ParagraphController::toggleQuote() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    return false;
  }

  // Check if the block is inside a block quote
  MarkdownNode* parent = context.node->parent();
  if (parent && parent->type() == BlockType::BlockQuote) {
    // Unwrap: remove the "> " prefix from the block source
    const QString blockSource = ctx_.session->markdownText().mid(context.blockStart, context.blockEnd - context.blockStart);
    QString unwrapped;
    const QStringList lines = blockSource.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
      if (line.startsWith(QStringLiteral("> "))) {
        unwrapped += line.mid(2);
      } else if (line.startsWith(QLatin1Char('>'))) {
        unwrapped += line.mid(1);
      } else {
        unwrapped += line;
      }
      unwrapped += QLatin1Char('\n');
    }
    if (!unwrapped.isEmpty() && unwrapped.back() == QLatin1Char('\n')) {
      unwrapped.chop(1);
    }

    // The parent BlockQuote's source range is what we replace
    const SourceRange parentRange = parent->sourceRange();
    const QString markdown = ctx_.session->markdownText();
    const qsizetype parentStart = sourceOffsetForLineColumn(markdown, parentRange.lineStart, qMax(1, parentRange.columnStart));
    const qsizetype parentEnd = sourceOffsetForLineEnd(markdown, parentRange.lineEnd);

    const qsizetype cursorContentOffset =
        context.cursorSourceOffset >= context.contentStart
            ? qBound<qsizetype>(0, context.cursorSourceOffset - context.contentStart, context.contentText.size())
            : 0;
    // After unwrapping, cursor offset shifts because "> " (2 chars) is removed from the first line
    const qsizetype nextCursor = parentStart + cursorContentOffset;

    return applyBlockDelta(
        EditTransaction::Kind::ReplaceDocumentText,
        QStringLiteral("Unwrap Quote"),
        parentStart,
        parentEnd - parentStart,
        unwrapped,
        nextCursor,
        {LocalEditNodeHint{context.blockId, parentStart, BlockType::Paragraph}},
        true);
  }

  // Wrap: prepend "> " to the block source
  const QString blockSource = ctx_.session->markdownText().mid(context.blockStart, context.blockEnd - context.blockStart);
  QStringList lines = blockSource.split(QLatin1Char('\n'));
  for (QString& line : lines) {
    line.prepend(QStringLiteral("> "));
  }
  const QString quoted = lines.join(QLatin1Char('\n'));

  const qsizetype cursorContentOffset =
      context.cursorSourceOffset >= context.contentStart
          ? qBound<qsizetype>(0, context.cursorSourceOffset - context.contentStart, context.contentText.size())
          : 0;
  const qsizetype nextCursor = context.blockStart + 2 + cursorContentOffset;

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Wrap in Quote"),
      context.blockStart,
      context.blockEnd - context.blockStart,
      quoted,
      nextCursor,
      {},
      true);
}

bool ParagraphController::convertToOrderedList() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    return false;
  }

  const QString inserted = QStringLiteral("1. ") + context.contentText;
  const qsizetype cursorContentOffset =
      context.cursorSourceOffset >= context.contentStart
          ? qBound<qsizetype>(0, context.cursorSourceOffset - context.contentStart, context.contentText.size())
          : 0;
  const qsizetype nextCursor = context.blockStart + 3 + cursorContentOffset;

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Convert to Ordered List"),
      context.blockStart,
      context.blockEnd - context.blockStart,
      inserted,
      nextCursor,
      {},
      true);
}

bool ParagraphController::convertToUnorderedList() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    return false;
  }

  const QString inserted = QStringLiteral("- ") + context.contentText;
  const qsizetype cursorContentOffset =
      context.cursorSourceOffset >= context.contentStart
          ? qBound<qsizetype>(0, context.cursorSourceOffset - context.contentStart, context.contentText.size())
          : 0;
  const qsizetype nextCursor = context.blockStart + 2 + cursorContentOffset;

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Convert to Unordered List"),
      context.blockStart,
      context.blockEnd - context.blockStart,
      inserted,
      nextCursor,
      {},
      true);
}

bool ParagraphController::convertToTaskList() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    return false;
  }

  const QString inserted = QStringLiteral("- [ ] ") + context.contentText;
  const qsizetype cursorContentOffset =
      context.cursorSourceOffset >= context.contentStart
          ? qBound<qsizetype>(0, context.cursorSourceOffset - context.contentStart, context.contentText.size())
          : 0;
  const qsizetype nextCursor = context.blockStart + 6 + cursorContentOffset;

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Convert to Task List"),
      context.blockStart,
      context.blockEnd - context.blockStart,
      inserted,
      nextCursor,
      {},
      true);
}

// ---------------------------------------------------------------------------
// Paragraph insert commands
// ---------------------------------------------------------------------------

bool ParagraphController::insertParagraphBefore() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    return false;
  }

  return applyBlockDelta(
      EditTransaction::Kind::SplitParagraph,
      QStringLiteral("Insert Paragraph Before"),
      context.blockStart,
      0,
      QStringLiteral("\n\n"),
      context.blockStart + 1,
      {LocalEditNodeHint{context.blockId, context.blockStart + 2, context.blockType}},
      true);
}

bool ParagraphController::insertParagraphAfter() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    return false;
  }

  return applyBlockDelta(
      EditTransaction::Kind::SplitParagraph,
      QStringLiteral("Insert Paragraph After"),
      context.blockEnd,
      0,
      QStringLiteral("\n\n"),
      context.blockEnd + 1,
      {LocalEditNodeHint{context.blockId, context.blockStart, context.blockType}},
      true);
}

// ---------------------------------------------------------------------------
// Toggle commands (Typora-style)
// ---------------------------------------------------------------------------

qsizetype ParagraphController::nodeSourceStart(const MarkdownNode& node) const {
  const SourceRange range = fullBlockSourceRange(node, ctx_.session->markdownText());
  if (range.byteStart >= 0) {
    return range.byteStart;
  }
  return sourceOffsetForLineColumn(ctx_.session->markdownText(), range.lineStart, qMax(1, range.columnStart));
}

qsizetype ParagraphController::nodeSourceEnd(const MarkdownNode& node) const {
  const SourceRange range = fullBlockSourceRange(node, ctx_.session->markdownText());
  if (range.byteEnd >= 0) {
    return range.byteEnd;
  }
  return sourceOffsetForLineEnd(ctx_.session->markdownText(), node.sourceRange().lineEnd);
}

bool ParagraphController::convertLiteralBlockToParagraph(MarkdownNode& node) {
  const qsizetype start = nodeSourceStart(node);
  qsizetype end = nodeSourceEnd(node);

  if (start < 0 || end < start) return false;

  const QString content = node.literal();
  const qsizetype cursorOffset = qBound<qsizetype>(0,
      ctx_.selection->cursorPosition().text.textOffset, content.size());

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Convert to Paragraph"),
      start, end - start, content,
      start + cursorOffset,
      {LocalEditNodeHint{node.id(), start, BlockType::Paragraph}},
      true);
}

bool ParagraphController::convertLiteralBlockToType(MarkdownNode& node, BlockType targetType) {
  const qsizetype start = nodeSourceStart(node);
  qsizetype end = nodeSourceEnd(node);

  if (start < 0 || end < start) return false;

  const QString content = node.literal();
  QString blockSource;
  qsizetype contentOffset;
  if (targetType == BlockType::CodeFence) {
    blockSource = content.isEmpty()
                      ? QStringLiteral("```\n```")
                      : QStringLiteral("```\n%1\n```").arg(content);
    contentOffset = 4;  // past "```\n"
  } else {
    blockSource = content.isEmpty()
                      ? QStringLiteral("$$\n$$")
                      : QStringLiteral("$$\n%1\n$$").arg(content);
    contentOffset = 3;  // past "$$\n"
  }

  const qsizetype cursorOffset = qBound<qsizetype>(0,
      ctx_.selection->cursorPosition().text.textOffset, content.size());

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      targetType == BlockType::CodeFence
          ? QStringLiteral("Convert to Code Block")
          : QStringLiteral("Convert to Formula Block"),
      start, end - start, blockSource,
      start + contentOffset + cursorOffset,
      {LocalEditNodeHint{node.id(), start, targetType}},
      true);
}

bool ParagraphController::insertBlockAfterNode(
    MarkdownNode& node,
    const QString& blockSource,
    qsizetype cursorInBlock,
    const QString& label) {
  const qsizetype end = nodeSourceEnd(node);
  if (end < 0) return false;

  const QString inserted = QStringLiteral("\n\n") + blockSource;
  return applyBlockDelta(
      EditTransaction::Kind::InsertText, label,
      end, 0, inserted,
      end + 2 + cursorInBlock,
      {}, true);
}

bool ParagraphController::insertCodeBlockWithSplit() {
  BlockContext context;
  if (!resolveBlockContext(context)) return false;

  const QString markdown = ctx_.session->markdownText();
  const SelectionRange sel = ctx_.selection->selection();

  // Extract heading prefix for the second paragraph (same as buildSplitTextBlock)
  QString headingPrefix;
  if (context.blockType == BlockType::Heading && context.blockStart >= 0 &&
      context.blockStart < context.contentStart) {
    headingPrefix = markdown.mid(context.blockStart, context.contentStart - context.blockStart);
  }

  // Work with content text (same pattern as buildSplitTextBlock)
  QString nextContent = context.contentText;

  if (sel.isCollapsed()) {
    // No selection: split at cursor — same as buildSplitTextBlock with code block in between
    qsizetype contentOffset = context.cursorSourceOffset - context.contentStart;
    qsizetype nextOffset = normalizeSplitOffset(nextContent, contentOffset);

    // blockBreak: paragraph-break + code-block + paragraph-break + heading-prefix
    QString blockBreak = QStringLiteral("\n\n```\n\n```\n\n") + headingPrefix;
    blockBreak = insertionWithInlineSplit(blockBreak, nextContent, nextOffset);
    nextContent.insert(nextOffset, blockBreak);

    // Cursor inside the empty code block content area
    const qsizetype codeContentPos = blockBreak.indexOf(QStringLiteral("```\n")) + 4;
    const qsizetype nextCursor = context.contentStart + nextOffset + codeContentPos;

    return applyBlockDelta(
        EditTransaction::Kind::ReplaceDocumentText,
        QStringLiteral("Insert Code Block"),
        context.contentStart,
        context.blockEnd - context.contentStart,
        nextContent, nextCursor,
        {LocalEditNodeHint{context.blockId, context.blockStart, BlockType::CodeFence}},
        true);
  }

  // With selection: selected text goes into the code block
  const qsizetype selStart = qMin(sel.anchor.text.sourceOffset, sel.focus.text.sourceOffset);
  const qsizetype selEnd = qMax(sel.anchor.text.sourceOffset, sel.focus.text.sourceOffset);
  const qsizetype selContentStart = qBound<qsizetype>(0, selStart - context.contentStart, nextContent.size());
  const qsizetype selContentEnd = qBound<qsizetype>(0, selEnd - context.contentStart, nextContent.size());

  const QString selectedText = nextContent.mid(selContentStart, selContentEnd - selContentStart);

  // Remove selected text, then split at the removal point (same as collapsed case)
  nextContent.remove(selContentStart, selContentEnd - selContentStart);
  qsizetype nextOffset = selContentStart;
  nextOffset = normalizeSplitOffset(nextContent, nextOffset);

  QString blockBreak = QStringLiteral("\n\n```\n%1\n```\n\n").arg(selectedText) + headingPrefix;
  blockBreak = insertionWithInlineSplit(blockBreak, nextContent, nextOffset);
  nextContent.insert(nextOffset, blockBreak);

  // Cursor at the end of selected text inside the code block
  const qsizetype codeContentPos = blockBreak.indexOf(QStringLiteral("```\n")) + 4;
  const qsizetype nextCursor = context.contentStart + nextOffset + codeContentPos + selectedText.size();

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Insert Code Block"),
      context.contentStart,
      context.blockEnd - context.contentStart,
      nextContent, nextCursor,
      {LocalEditNodeHint{context.blockId, context.blockStart, BlockType::CodeFence}},
      true);
}

bool ParagraphController::insertFormulaBlockWithSplit() {
  BlockContext context;
  if (!resolveBlockContext(context)) return false;

  const QString markdown = ctx_.session->markdownText();
  const SelectionRange sel = ctx_.selection->selection();

  QString headingPrefix;
  if (context.blockType == BlockType::Heading && context.blockStart >= 0 &&
      context.blockStart < context.contentStart) {
    headingPrefix = markdown.mid(context.blockStart, context.contentStart - context.blockStart);
  }

  QString nextContent = context.contentText;

  if (sel.isCollapsed()) {
    qsizetype contentOffset = context.cursorSourceOffset - context.contentStart;
    qsizetype nextOffset = normalizeSplitOffset(nextContent, contentOffset);

    QString blockBreak = QStringLiteral("\n\n$$\n\n$$\n\n") + headingPrefix;
    blockBreak = insertionWithInlineSplit(blockBreak, nextContent, nextOffset);
    nextContent.insert(nextOffset, blockBreak);

    const qsizetype codeContentPos = blockBreak.indexOf(QStringLiteral("$$\n")) + 3;
    const qsizetype nextCursor = context.contentStart + nextOffset + codeContentPos;

    return applyBlockDelta(
        EditTransaction::Kind::ReplaceDocumentText,
        QStringLiteral("Insert Formula Block"),
        context.contentStart,
        context.blockEnd - context.contentStart,
        nextContent, nextCursor,
        {LocalEditNodeHint{context.blockId, context.blockStart, BlockType::MathBlock}},
        true);
  }

  const qsizetype selStart = qMin(sel.anchor.text.sourceOffset, sel.focus.text.sourceOffset);
  const qsizetype selEnd = qMax(sel.anchor.text.sourceOffset, sel.focus.text.sourceOffset);
  const qsizetype selContentStart = qBound<qsizetype>(0, selStart - context.contentStart, nextContent.size());
  const qsizetype selContentEnd = qBound<qsizetype>(0, selEnd - context.contentStart, nextContent.size());

  const QString selectedText = nextContent.mid(selContentStart, selContentEnd - selContentStart);

  nextContent.remove(selContentStart, selContentEnd - selContentStart);
  qsizetype nextOffset = selContentStart;
  nextOffset = normalizeSplitOffset(nextContent, nextOffset);

  QString blockBreak = QStringLiteral("\n\n$$\n%1\n$$\n\n").arg(selectedText) + headingPrefix;
  blockBreak = insertionWithInlineSplit(blockBreak, nextContent, nextOffset);
  nextContent.insert(nextOffset, blockBreak);

  const qsizetype codeContentPos = blockBreak.indexOf(QStringLiteral("$$\n")) + 3;
  const qsizetype nextCursor = context.contentStart + nextOffset + codeContentPos + selectedText.size();

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Insert Formula Block"),
      context.contentStart,
      context.blockEnd - context.contentStart,
      nextContent, nextCursor,
      {LocalEditNodeHint{context.blockId, context.blockStart, BlockType::MathBlock}},
      true);
}

bool ParagraphController::toggleCodeBlock() {
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor()) return false;

  const NodeId blockId = ctx_.selection->cursorPosition().blockId;
  if (!blockId.isValid()) return false;

  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node) return false;

  const BlockType type = node->type();

  // Table cells/rows: walk up to Table, insert after it
  if (type == BlockType::TableCell || type == BlockType::TableRow) {
    MarkdownNode* t = node;
    while (t && t->type() != BlockType::Table) {
      t = t->parent();
    }
    if (t) {
      return insertBlockAfterNode(*t, QStringLiteral("```\n\n```"), 6,
                                  QStringLiteral("Insert Code Block"));
    }
    return insertCodeBlock();
  }

  switch (type) {
    case BlockType::CodeFence:
      return convertLiteralBlockToParagraph(*node);
    case BlockType::MathBlock:
      return convertLiteralBlockToType(*node, BlockType::CodeFence);
    case BlockType::Paragraph:
    case BlockType::Heading: {
      // Only split for top-level paragraphs/headings
      MarkdownNode* parent = node->parent();
      if (parent && parent->type() == BlockType::Document) {
        return insertCodeBlockWithSplit();
      }
      return insertCodeBlock();  // nested paragraph (blockquote/list) -> fallback
    }
    default:
      return insertCodeBlock();  // fallback
  }
}

bool ParagraphController::toggleFormulaBlock() {
  if (!ctx_.session || !ctx_.selection || !ctx_.selection->hasCursor()) return false;

  const NodeId blockId = ctx_.selection->cursorPosition().blockId;
  if (!blockId.isValid()) return false;

  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node) return false;

  const BlockType type = node->type();

  // Table cells/rows: walk up to Table, insert after it
  if (type == BlockType::TableCell || type == BlockType::TableRow) {
    MarkdownNode* t = node;
    while (t && t->type() != BlockType::Table) {
      t = t->parent();
    }
    if (t) {
      return insertBlockAfterNode(*t, QStringLiteral("$$\n\n$$"), 5,
                                  QStringLiteral("Insert Formula Block"));
    }
    return insertFormulaBlock();
  }

  switch (type) {
    case BlockType::MathBlock:
      return convertLiteralBlockToParagraph(*node);
    case BlockType::CodeFence:
      return convertLiteralBlockToType(*node, BlockType::MathBlock);
    case BlockType::Paragraph:
    case BlockType::Heading: {
      MarkdownNode* parent = node->parent();
      if (parent && parent->type() == BlockType::Document) {
        return insertFormulaBlockWithSplit();
      }
      return insertFormulaBlock();
    }
    default:
      return insertFormulaBlock();
  }
}

}  // namespace muffin
