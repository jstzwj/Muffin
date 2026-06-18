#include "commands/ParagraphController.h"

#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/BlockEditContext.h"
#include "document/SourceRangeUtil.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/InlineSplit.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"

#include <algorithm>
#include <QSettings>

namespace {

// markdown/* insertion-default readers. Each maps a combo INDEX (as persisted by PrefsMarkdownPage)
// to the real emitted value. Mirrors the editor/indentSize convention in
// TextBlockCommandBuilder.cpp::indentUnit() — this is the single place each mapping lives.

// markdown/headingStyle: 0 = ATX (#), 1 = setext (===/---). Setext is only valid for levels 1-2.
int headingStyleIndex() {
  return qBound(0, QSettings().value(QStringLiteral("markdown/headingStyle"), 0).toInt(), 1);
}

// markdown/unorderedList: 0 = '-', 1 = '*', 2 = '+'.
QChar unorderedListMarker() {
  const QChar markers[3] = {QLatin1Char('-'), QLatin1Char('*'), QLatin1Char('+')};
  return markers[qBound(0, QSettings().value(QStringLiteral("markdown/unorderedList"), 0).toInt(), 2)];
}

// markdown/defaultCodeLang: 0 = (empty), 1 = cpp, 2 = python, 3 = javascript, 4 = java.
QString defaultCodeLang() {
  const QString langs[5] = {
      QString(), QStringLiteral("cpp"), QStringLiteral("python"),
      QStringLiteral("javascript"), QStringLiteral("java")};
  return langs[qBound(0, QSettings().value(QStringLiteral("markdown/defaultCodeLang"), 0).toInt(), 4)];
}

// markdown/autoCodeLang: 0 = when inserting via markdown code, 1 = always, 2 = never. The code-block
// insert commands in this file ARE the "via markdown code" path, so 0 and 1 both emit a language.
bool shouldEmitCodeLang() {
  return qBound(0, QSettings().value(QStringLiteral("markdown/autoCodeLang"), 0).toInt(), 2) != 2;
}

// The opening fence with optional language, e.g. "```" or "```cpp" (no trailing newline). Callers
// append "\n". Using its size() keeps cursor offsets correct regardless of language length.
QString codeFenceOpen() {
  return shouldEmitCodeLang() ? QStringLiteral("```") + defaultCodeLang() : QStringLiteral("```");
}

}  // namespace

namespace muffin {

ParagraphController::ParagraphController(QObject* parent) : QObject(parent) {}

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

// Shared precondition for block-level queries: a session, a caret, and a collapsed selection.
// A multi-block selection disables every block-level command (insert-paragraph, heading, etc.).
bool ParagraphController::hasCollapsedBlockCursor() const {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return false;
  }
  return ctx_.selection->selection().isCollapsed();
}

bool ParagraphController::isOnEditableBlock() const {
  if (!hasCollapsedBlockCursor()) {
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

bool ParagraphController::canInsertAdjacentParagraph() const {
  BlockContext context;
  return resolveInsertionContext(context);
}

// ---------------------------------------------------------------------------
// resolveInsertionContext — context for insert-paragraph-before/after.
// Accepts Paragraph/Heading (via resolveBlockContext) and CodeFence/MathBlock.
// For literal blocks only blockStart/blockEnd (the line span) are filled;
// content fields are left at their defaults since they are unused by the
// adjacent-insertion commands.
// ---------------------------------------------------------------------------

bool ParagraphController::resolveInsertionContext(BlockContext& context) const {
  if (!hasCollapsedBlockCursor()) {
    return false;
  }
  // Paragraph/Heading: reuse the rich context (content, heading level, etc.).
  if (resolveBlockContext(context)) {
    return true;
  }
  const NodeId blockId = ctx_.selection->cursorPosition().blockId;
  if (!blockId.isValid()) {
    return false;
  }
  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node) {
    return false;
  }
  // Only literal blocks with well-defined delimiters support adjacent insertion.
  if (node->type() != BlockType::CodeFence && node->type() != BlockType::MathBlock) {
    return false;
  }

  // The insertion point is the block's plain line span (column 1 of the first line through the end
  // of the last line), NOT the whole-block range from fullBlockSourceRange — that one extends a
  // math block past its closing "$$"/"\]" for snapshot replacement, which would swallow the closing
  // delimiter and land the new paragraph in the wrong place. See SourceRangeUtil::blockLineSpan.
  const SourceRange span = blockLineSpan(*node, ctx_.session->markdownText());
  if (span.byteStart < 0 || span.byteEnd < span.byteStart) {
    return false;
  }

  context.node = node;
  context.editableNode = node;
  context.blockId = blockId;
  context.blockType = node->type();
  context.blockStart = span.byteStart;
  context.blockEnd = span.byteEnd;
  return true;
}

// ---------------------------------------------------------------------------
// resolveBlockContext — gather current block's source ranges
// ---------------------------------------------------------------------------

bool ParagraphController::resolveBlockContext(BlockContext& context) const {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
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
  if (!ctx_.hasSession() || sourceStart < 0 || removedLength < 0 ||
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
// cursorForSourceOffset
// ---------------------------------------------------------------------------

CursorPosition ParagraphController::cursorForSourceOffset(qsizetype sourceOffset) const {
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

  QString newPrefix;        // ATX prefix ("# "). Empty for setext.
  QString trailingBlock;    // setext underline ("\n===" / "\n---"). Empty for ATX.
  const bool setext = level > 0 && (level == 1 || level == 2) && headingStyleIndex() == 1 &&
                      !context.contentText.isEmpty();
  if (level > 0) {
    if (setext) {
      const QChar line = (level == 1) ? QLatin1Char('=') : QLatin1Char('-');
      trailingBlock = QStringLiteral("\n") + QString(line).repeated(qMax(1, context.contentText.size()));
    } else {
      newPrefix = QStringLiteral("#").repeated(level) + QLatin1Char(' ');
    }
  }

  const QString insertedText = newPrefix + context.contentText + trailingBlock;
  const qsizetype removedLength = context.blockEnd - context.blockStart;

  // Compute cursor offset within the content
  const qsizetype cursorContentOffset =
      context.cursorSourceOffset >= context.contentStart
          ? qBound<qsizetype>(0, context.cursorSourceOffset - context.contentStart, context.contentText.size())
          : 0;
  // For ATX the content follows newPrefix; for setext newPrefix is empty so the cursor still lands on
  // the content at blockStart (a setext heading's first line is the content text).
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
    if (!ctx_.hasSession()) {
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
    if (!ctx_.hasSession()) {
      return false;
    }
    const QString markdown = ctx_.session->markdownText();
    const QString open = codeFenceOpen();
    const QString inserted = markdown.isEmpty() ? open + QStringLiteral("\n\n```")
                                                : QStringLiteral("\n\n") + open + QStringLiteral("\n\n```");
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Code Block"),
        offset, 0, inserted,
        offset + (markdown.isEmpty() ? open.size() + 1 : 2 + open.size() + 1),
        {}, true);
  }

  const QString open = codeFenceOpen();
  const QString inserted = QStringLiteral("\n\n") + open + QStringLiteral("\n\n```");
  const qsizetype nextCursor = context.blockEnd + 2 + open.size() + 1;  // between ``` markers
  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Code Block"),
      context.blockEnd, 0, inserted,
      nextCursor, {}, true);
}

bool ParagraphController::insertLinkReference() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    if (!ctx_.hasSession()) {
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
    if (!ctx_.hasSession()) {
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
    if (!ctx_.hasSession()) {
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

bool ParagraphController::insertTableOfContents() {
  BlockContext context;
  if (!resolveBlockContext(context)) {
    if (!ctx_.hasSession()) {
      return false;
    }
    const QString markdown = ctx_.session->markdownText();
    const QString inserted = markdown.isEmpty() ? QStringLiteral("[TOC]\n\n") : QStringLiteral("\n\n[TOC]\n\n");
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Table of Contents"),
        offset, 0, inserted,
        offset + inserted.size(),
        {}, true);
  }

  const QString inserted = QStringLiteral("\n\n[TOC]\n\n");
  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Table of Contents"),
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

  const QString inserted = unorderedListMarker() + QStringLiteral(" ") + context.contentText;
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

  const QString inserted = unorderedListMarker() + QStringLiteral(" [ ] ") + context.contentText;
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

bool ParagraphController::toggleTaskListItem(NodeId blockId) {
  if (!ctx_.hasSession() || !blockId.isValid()) {
    return false;
  }
  MarkdownNode* node = ctx_.session->document().node(blockId);
  if (!node || node->type() != BlockType::ListItem || !node->isTaskItem()) {
    return false;
  }

  const QString markdown = ctx_.session->markdownText();
  // The toggle target is the inner character of "[ ]"/"[x]" on the item's first
  // source line. Resolve that line and reuse the shared list-line scanner so the
  // offset agrees with how the parser and serializer see the marker.
  const qsizetype lineStart = lineStartOffset(markdown, node->sourceRange().byteStart);
  if (lineStart < 0) {
    return false;
  }
  const qsizetype newline = markdown.indexOf(QLatin1Char('\n'), lineStart);
  const QString line = newline < 0 ? markdown.mid(lineStart) : markdown.mid(lineStart, newline - lineStart);
  const ListLineInfo info = listLineInfoFor(line);
  if (!info.task) {
    return false;  // Defensive: node claims task-item identity but source disagrees.
  }

  const qsizetype toggleOffset = lineStart + info.taskMarkerStart + 1;
  const QChar current = markdown.at(toggleOffset);
  const QChar next = current.isSpace() ? QLatin1Char('x') : QLatin1Char(' ');
  // A single-character swap shifts no offsets, so the caret stays put; only fall
  // back to the item's content start when there is no caret to preserve.
  const qsizetype contentStart = lineStart + info.taskContentStart;
  const qsizetype nextCursor = ctx_.hasCursor()
      ? qBound<qsizetype>(0, ctx_.selection->cursorPosition().text.sourceOffset, markdown.size())
      : contentStart;

  return applyBlockDelta(
      EditTransaction::Kind::ReplaceDocumentText,
      QStringLiteral("Toggle Task"),
      toggleOffset,
      1,
      QString(next),
      nextCursor,
      {},
      true);
}

MarkdownNode* ParagraphController::currentListItem() const {
  if (!ctx_.hasSession() || !ctx_.hasCursor()) {
    return nullptr;
  }
  const NodeId blockId = ctx_.selection->cursorPosition().blockId;
  if (!blockId.isValid()) {
    return nullptr;
  }
  // The caret's block may be the list item itself or a child of it; walk up to
  // the nearest ListItem.
  MarkdownNode* node = ctx_.session->document().node(blockId);
  while (node && node->type() != BlockType::ListItem) {
    node = node->parent();
  }
  return node;
}

bool ParagraphController::isOnListItem() const {
  return currentListItem() != nullptr;
}

bool ParagraphController::isOnTaskItem() const {
  MarkdownNode* item = currentListItem();
  return item && item->isTaskItem();
}

bool ParagraphController::toggleCurrentTaskListItem() {
  MarkdownNode* item = currentListItem();
  if (item && item->isTaskItem()) {
    return toggleTaskListItem(item->id());
  }
  return false;
}

bool ParagraphController::insertAlert(AlertKind kind) {
  const char* marker = nullptr;
  switch (kind) {
    case AlertKind::Note: marker = "NOTE"; break;
    case AlertKind::Tip: marker = "TIP"; break;
    case AlertKind::Important: marker = "IMPORTANT"; break;
    case AlertKind::Warning: marker = "WARNING"; break;
    case AlertKind::Caution: marker = "CAUTION"; break;
    default: return false;
  }
  // A GFM alert is a blockquote whose first line is [!KIND]; the following ">"
  // line carries the content. Place the caret on that line, ready to type.
  const QString markerStr = QString::fromLatin1(marker);
  const QString body = QStringLiteral("\n\n> [!%1]\n> ").arg(markerStr);

  BlockContext context;
  if (!resolveBlockContext(context)) {
    if (!ctx_.hasSession()) {
      return false;
    }
    const QString markdown = ctx_.session->markdownText();
    const QString inserted = markdown.isEmpty()
        ? QStringLiteral("> [!%1]\n> ").arg(markerStr)
        : body;
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Alert"),
        offset, 0, inserted,
        offset + inserted.size(),
        {}, true);
  }

  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Alert"),
      context.blockEnd, 0, body,
      context.blockEnd + body.size(),
      {}, true);
}

// ---------------------------------------------------------------------------
// Paragraph insert commands
// ---------------------------------------------------------------------------

bool ParagraphController::insertParagraphBefore() {
  BlockContext context;
  if (!resolveInsertionContext(context)) {
    return false;
  }

  return applyBlockDelta(
      EditTransaction::Kind::SplitParagraph,
      QStringLiteral("Insert Paragraph Before"),
      context.blockStart,
      0,
      QStringLiteral("\n\n"),
      // The new empty paragraph is created at the insertion point (the blank line
      // before the block); its zero-length content range begins exactly here.
      context.blockStart,
      {LocalEditNodeHint{context.blockId, context.blockStart + 2, context.blockType}},
      true);
}

bool ParagraphController::insertParagraphAfter() {
  BlockContext context;
  if (!resolveInsertionContext(context)) {
    return false;
  }

  return applyBlockDelta(
      EditTransaction::Kind::SplitParagraph,
      QStringLiteral("Insert Paragraph After"),
      context.blockEnd,
      0,
      QStringLiteral("\n\n"),
      // After inserting "\n\n" at the block end, the new empty paragraph's content
      // begins right after the two inserted newlines.
      context.blockEnd + 2,
      {LocalEditNodeHint{context.blockId, context.blockStart, context.blockType}},
      true);
}

// ---------------------------------------------------------------------------
// Toggle commands
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
    const QString open = codeFenceOpen();
    blockSource = content.isEmpty()
                      ? open + QStringLiteral("\n```")
                      : (open + QStringLiteral("\n%1\n```")).arg(content);
    contentOffset = open.size() + 1;  // past opening fence + newline
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
    const QString open = codeFenceOpen();
    QString blockBreak = QStringLiteral("\n\n") + open + QStringLiteral("\n\n```\n\n") + headingPrefix;
    blockBreak = insertionWithInlineSplit(blockBreak, nextContent, nextOffset);
    nextContent.insert(nextOffset, blockBreak);

    // Cursor inside the empty code block content area
    const qsizetype codeContentPos = blockBreak.indexOf(open + QLatin1Char('\n')) + open.size() + 1;
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

  const QString open = codeFenceOpen();
  QString blockBreak = (QStringLiteral("\n\n") + open + QStringLiteral("\n%1\n```\n\n")).arg(selectedText) + headingPrefix;
  blockBreak = insertionWithInlineSplit(blockBreak, nextContent, nextOffset);
  nextContent.insert(nextOffset, blockBreak);

  // Cursor at the end of selected text inside the code block
  const qsizetype codeContentPos = blockBreak.indexOf(open + QLatin1Char('\n')) + open.size() + 1;
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
  if (!ctx_.hasSession() || !ctx_.hasCursor()) return false;

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
      const QString open = codeFenceOpen();
      return insertBlockAfterNode(*t, open + QStringLiteral("\n\n```"), open.size() + 3,
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
  if (!ctx_.hasSession() || !ctx_.hasCursor()) return false;

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
