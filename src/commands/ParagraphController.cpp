#include "commands/ParagraphController.h"

#include "app/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "editor/BlockEditContext.h"
#include "editor/BrushQueue.h"
#include "editor/SelectionController.h"
#include "edit/UndoStack.h"

#include <algorithm>

namespace muffin {

ParagraphController::ParagraphController(QObject* parent) : QObject(parent) {}

void ParagraphController::setDocumentSession(DocumentSession* session) {
  session_ = session;
}

void ParagraphController::setSelectionController(SelectionController* selection) {
  selection_ = selection;
}

void ParagraphController::setUndoStack(UndoStack* undoStack) {
  undoStack_ = undoStack;
}

void ParagraphController::setBrushQueue(BrushQueue* brushQueue) {
  brushQueue_ = brushQueue;
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
  if (!session_ || !selection_ || !selection_->hasCursor()) {
    return false;
  }
  // Block-level commands should not apply when a multi-block selection is active
  const SelectionRange sel = selection_->selection();
  if (!sel.isCollapsed()) {
    return false;
  }
  const NodeId blockId = selection_->cursorPosition().blockId;
  if (!blockId.isValid()) {
    return false;
  }
  MarkdownNode* node = session_->document().node(blockId);
  if (!node) {
    return false;
  }
  return node->type() == BlockType::Paragraph || node->type() == BlockType::Heading;
}

// ---------------------------------------------------------------------------
// resolveBlockContext — gather current block's source ranges
// ---------------------------------------------------------------------------

bool ParagraphController::resolveBlockContext(BlockContext& context) const {
  if (!session_ || !selection_ || !selection_->hasCursor()) {
    return false;
  }

  const NodeId blockId = selection_->cursorPosition().blockId;
  if (!blockId.isValid()) {
    return false;
  }

  MarkdownNode* node = session_->document().node(blockId);
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

  const QString markdown = session_->markdownText();
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
  context.cursorSourceOffset = selection_->cursorPosition().text.sourceOffset;
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
  if (!session_ || sourceStart < 0 || removedLength < 0 ||
      sourceStart + removedLength > session_->markdownText().size()) {
    return false;
  }

  const CursorPosition beforeCursor =
      selection_ && selection_->hasCursor() ? selection_->cursorPosition() : CursorPosition();
  const QString removedText = session_->markdownText().mid(sourceStart, removedLength);

  QVector<NodeId> affectedNodes;
  for (const LocalEditNodeHint& hint : nodeHints) {
    if (hint.nodeId.isValid() && !affectedNodes.contains(hint.nodeId)) {
      affectedNodes.push_back(hint.nodeId);
    }
  }

  const bool appliedLocally = session_->applyTextDelta(sourceStart, removedLength, insertedText, true, std::move(nodeHints));
  if (!appliedLocally) {
    return false;
  }

  const CursorPosition nextCursor = cursorForSourceOffset(
      qBound<qsizetype>(0, nextCursorSourceOffset, session_->markdownText().size()));
  if (selection_ && nextCursor.isValid()) {
    selection_->setCursorPosition(nextCursor);
  }

  // Build the cursor for the undo transaction — prefer the resolved cursor,
  // fall back to the beforeCursor if the source offset lands in a non-text block
  const CursorPosition undoCursor = nextCursor.isValid() ? nextCursor : beforeCursor;

  if (undoCursor.blockId.isValid() && !affectedNodes.contains(undoCursor.blockId)) {
    affectedNodes.push_back(undoCursor.blockId);
  }

  if (undoStack_ && beforeCursor.isValid()) {
    undoStack_->push(EditTransaction(
        kind,
        label,
        TextDeltaCommand{
            TextDelta{sourceStart, removedText, insertedText},
            beforeCursor,
            undoCursor,
            std::move(affectedNodes)}));
  }

  if (brushQueue_) {
    if (structureEdit) {
      brushQueue_->requestFullRefresh();
    } else if (!affectedNodes.isEmpty()) {
      brushQueue_->requestBlocksRefresh(std::move(affectedNodes));
    } else {
      brushQueue_->requestFullRefresh();
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
  if (!session_) {
    return cursor;
  }

  MarkdownNode* node = paragraphAtSourceOffset(session_->document().root(), sourceOffset);
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
  const QString markdown = session_->markdownText();
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

  if (node.type() == BlockType::Paragraph || node.type() == BlockType::Heading) {
    return &node;
  }

  for (const auto& child : node.children()) {
    if (MarkdownNode* found = paragraphAtSourceOffset(*child, sourceOffset)) {
      return found;
    }
  }
  return nullptr;
}

MarkdownNode* ParagraphController::primaryParagraph(MarkdownNode& node) const {
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child.get();
    }
  }
  return nullptr;
}

qsizetype ParagraphController::sourceOffsetForLineColumn(const QString& text, int line, int column) const {
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

qsizetype ParagraphController::sourceOffsetForLineEnd(const QString& text, int line) const {
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
    if (!session_) {
      return false;
    }
    const QString markdown = session_->markdownText();
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
    if (!session_) {
      return false;
    }
    const QString markdown = session_->markdownText();
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
    if (!session_) {
      return false;
    }
    const QString markdown = session_->markdownText();
    const QString inserted = markdown.isEmpty() ? QStringLiteral("[label]: url") : QStringLiteral("\n\n[label]: url");
    const qsizetype offset = markdown.size();
    return applyBlockDelta(
        EditTransaction::Kind::InsertText,
        QStringLiteral("Insert Link Reference"),
        offset, 0, inserted,
        offset + (markdown.isEmpty() ? 1 : 3),  // cursor at "label"
        {}, true);
  }

  const QString inserted = QStringLiteral("\n\n[label]: url");
  const qsizetype nextCursor = context.blockEnd + 3;  // cursor at "label"
  return applyBlockDelta(
      EditTransaction::Kind::InsertText,
      QStringLiteral("Insert Link Reference"),
      context.blockEnd, 0, inserted,
      nextCursor, {}, true);
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
    const QString blockSource = session_->markdownText().mid(context.blockStart, context.blockEnd - context.blockStart);
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
    const QString markdown = session_->markdownText();
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
  const QString blockSource = session_->markdownText().mid(context.blockStart, context.blockEnd - context.blockStart);
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

}  // namespace muffin
