#include "editor/TextBlockCommandBuilder.h"

#include "document/MarkdownNode.h"
#include "document/PendingBlockMarker.h"
#include "document/SourceRangeUtil.h"
#include "editor/InlineSplit.h"

#include <QStringList>

namespace muffin {
namespace {

bool hasBlockQuoteAncestor(const MarkdownNode* node) {
  for (const MarkdownNode* parent = node ? node->parent() : nullptr; parent; parent = parent->parent()) {
    if (parent->type() == BlockType::BlockQuote) {
      return true;
    }
  }
  return false;
}

int blockQuoteDepth(const MarkdownNode* node) {
  int depth = 0;
  for (const MarkdownNode* parent = node ? node->parent() : nullptr; parent; parent = parent->parent()) {
    if (parent->type() == BlockType::BlockQuote) {
      ++depth;
    }
  }
  return depth;
}

qsizetype lineStartForOffset(const QString& text, qsizetype offset) {
  qsizetype lineStart = qBound<qsizetype>(0, offset, text.size());
  while (lineStart > 0 && text.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  return lineStart;
}

QString blockQuotePrefixForLine(const QString& line) {
  QString prefix;
  qsizetype index = 0;
  while (index < line.size()) {
    const qsizetype markerStart = index;
    while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
      ++index;
    }
    if (index >= line.size() || line.at(index) != QLatin1Char('>')) {
      break;
    }
    ++index;
    if (index < line.size() && line.at(index) == QLatin1Char(' ')) {
      ++index;
    }
    prefix += line.mid(markerStart, index - markerStart);
  }
  return prefix;
}

struct BlockQuoteMarkerRange {
  qsizetype start = -1;
  qsizetype end = -1;
  bool valid = false;
};

BlockQuoteMarkerRange nthBlockQuoteMarkerRange(const QString& line, int depth) {
  BlockQuoteMarkerRange range;
  if (depth <= 0) {
    return range;
  }

  qsizetype index = 0;
  int currentDepth = 0;
  while (index < line.size()) {
    while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
      ++index;
    }
    if (index >= line.size() || line.at(index) != QLatin1Char('>')) {
      return range;
    }

    const qsizetype markerStart = index;
    ++index;
    if (index < line.size() && line.at(index) == QLatin1Char(' ')) {
      ++index;
    }
    ++currentDepth;
    if (currentDepth == depth) {
      range.start = markerStart;
      range.end = index;
      range.valid = true;
      return range;
    }
  }
  return range;
}

QString leadingSpaces(qsizetype count) {
  return QString(qMax<qsizetype>(0, count), QLatin1Char(' '));
}

qsizetype lineEndForOffset(const QString& text, qsizetype offset) {
  qsizetype lineEnd = qBound<qsizetype>(0, offset, text.size());
  while (lineEnd < text.size() && text.at(lineEnd) != QLatin1Char('\n')) {
    ++lineEnd;
  }
  return lineEnd;
}

qsizetype nextLineStart(const QString& text, qsizetype lineEnd) {
  return lineEnd < text.size() && text.at(lineEnd) == QLatin1Char('\n') ? lineEnd + 1 : lineEnd;
}

qsizetype orderedSiblingRunEnd(const QString& markdown, qsizetype start, qsizetype markerColumn) {
  qsizetype pos = qBound<qsizetype>(0, start, markdown.size());
  while (pos < markdown.size()) {
    const qsizetype lineEnd = lineEndForOffset(markdown, pos);
    const QString line = markdown.mid(pos, lineEnd - pos);
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
      break;
    }

    const ListLineInfo info = listLineInfoFor(line);
    if (!info.valid) {
      qsizetype leading = 0;
      while (leading < line.size() && line.at(leading) == QLatin1Char(' ')) {
        ++leading;
      }
      if (leading <= markerColumn) {
        break;
      }
    } else if (info.markerStart < markerColumn) {
      break;
    }

    pos = nextLineStart(markdown, lineEnd);
  }
  return pos;
}

QString renumberOrderedSiblings(QString text, qsizetype markerColumn, int nextNumber) {
  qsizetype pos = 0;
  while (pos < text.size()) {
    const qsizetype lineEnd = lineEndForOffset(text, pos);
    const QString line = text.mid(pos, lineEnd - pos);
    const ListLineInfo info = listLineInfoFor(line);
    if (info.valid && info.ordered && info.markerStart == markerColumn) {
      const qsizetype numberStart = pos + info.markerStart;
      qsizetype numberEnd = numberStart;
      while (numberEnd < text.size() && text.at(numberEnd).isDigit()) {
        ++numberEnd;
      }
      const QString replacement = QString::number(nextNumber);
      text.replace(numberStart, numberEnd - numberStart, replacement);
      const qsizetype delta = replacement.size() - (numberEnd - numberStart);
      pos = lineEnd + delta;
      ++nextNumber;
    } else {
      pos = lineEnd;
    }
    pos = nextLineStart(text, pos);
  }
  return text;
}

}  // namespace

TextBlockCommandBuilder::TextBlockCommandBuilder(DocumentSession* session, const BlockEditContextResolver* resolver)
    : session_(session), resolver_(resolver) {}

bool TextBlockCommandBuilder::Command::hasLocalEdit() const {
  return sourceStart >= 0 && removedLength >= 0;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildTextEdit(
    const BlockEditContext& context,
    Operation operation,
    QString text) const {
  Command command;
  if (!session_ || !context.node) {
    return command;
  }

  if (!context.plainInlineEditable && context.cursorSourceOffset < context.contentRange.byteStart &&
      operation != Operation::Enter) {
    return command;
  }

  QString nextParagraph = context.contentText;
  qsizetype nextOffset = context.plainInlineEditable ? qBound<qsizetype>(0, context.cursorTextOffset, nextParagraph.size())
                                                     : qBound<qsizetype>(0, context.cursorSourceOffset - context.contentRange.byteStart, nextParagraph.size());
  command.kind = EditTransaction::Kind::InsertText;
  command.label = QStringLiteral("Insert Text");

  switch (operation) {
    case Operation::InsertText:
      nextParagraph.insert(nextOffset, text);
      command.sourceStart = context.contentRange.byteStart + nextOffset;
      command.removedLength = 0;
      command.insertedText = text;
      nextOffset += text.size();
      command.kind = EditTransaction::Kind::InsertText;
      command.label = QStringLiteral("Insert Text");
      break;
    case Operation::Backspace:
      if (nextOffset <= 0) {
        if (context.node->type() == BlockType::ListItem) {
          if (context.contentText.trimmed().isEmpty()) {
            return buildOutdentListItem(context);
          }
          Command mergeCmd = buildMergeWithPreviousListItem(context);
          if (mergeCmd.valid) return mergeCmd;
          return buildOutdentListItem(context);
        }
        // Block-quote outdent (mirrors Typora handler V): the FIRST child of a block quote
        // drops one nesting level on backspace instead of merging. Checked before Heading so
        // "> # Title" outdents the quote rather than converting the heading, and after ListItem
        // so "> - item" still routes to list logic. Applies to empty first-child quote lines
        // too (Typora pops them out the same way); buildOutdentBlockQuote handles the empty
        // case so the quote is removed cleanly rather than split malformedly.
        if (context.node->previousSibling() == nullptr &&
            context.node->parent() &&
            context.node->parent()->type() == BlockType::BlockQuote) {
          return buildOutdentBlockQuote(context);
        }
        if (context.blockType == BlockType::Heading && nextParagraph.isEmpty() &&
            context.blockRange.byteStart >= 0 && context.contentRange.byteStart > context.blockRange.byteStart) {
          return buildConvertHeadingToParagraph(context);
        }
        return buildMergeWithPreviousParagraph(context);
      }
      command.sourceStart = context.contentRange.byteStart + nextOffset - 1;
      command.removedLength = 1;
      command.insertedText.clear();
      nextParagraph.remove(nextOffset - 1, 1);
      --nextOffset;
      command.kind = EditTransaction::Kind::DeleteText;
      command.label = QStringLiteral("Backspace");
      break;
    case Operation::Delete:
      if (nextOffset >= nextParagraph.size()) {
        if (context.node->type() == BlockType::ListItem) {
          Command mergeCmd = buildMergeWithNextListItem(context);
          if (mergeCmd.valid) return mergeCmd;
        }
        if (context.blockType == BlockType::Heading && nextParagraph.isEmpty() &&
            context.blockRange.byteStart >= 0 && context.contentRange.byteStart > context.blockRange.byteStart) {
          return buildRemoveEmptyHeading(context);
        }
        return buildMergeWithNextParagraph(context);
      }
      command.sourceStart = context.contentRange.byteStart + nextOffset;
      command.removedLength = 1;
      command.insertedText.clear();
      nextParagraph.remove(nextOffset, 1);
      command.kind = EditTransaction::Kind::DeleteText;
      command.label = QStringLiteral("Delete");
      break;
    case Operation::Enter:
      if (context.blockType == BlockType::Paragraph) {
        Command pendingBlock = buildConvertPendingToBlock(context);
        if (pendingBlock.valid) {
          return pendingBlock;
        }
      }
      if (context.node->type() == BlockType::LinkDefinition || context.node->type() == BlockType::FootnoteDefinition) {
        const DefinitionBlock definition = context.node->definition();
        const DefinitionFieldRange finalField =
            context.node->type() == BlockType::FootnoteDefinition ? definition.noteRange : definition.titleRange;
        if (finalField.isValid() && context.cursorSourceOffset >= finalField.end) {
          return buildInsertBlockAfter(context);
        }
      }
      if (context.node->type() == BlockType::ListItem) {
        if (context.contentText.trimmed().isEmpty()) {
          return buildExitListItem(context);
        }
        const qsizetype contentOffset = context.plainInlineEditable
            ? qBound<qsizetype>(0, context.cursorTextOffset, context.contentText.size())
            : qBound<qsizetype>(0, context.cursorSourceOffset - context.contentRange.byteStart, context.contentText.size());
        if (contentOffset <= 0) {
          return buildInsertListItemAbove(context);
        }
        return buildSplitListItem(context);
      }
      if (context.visibleText.isEmpty()) {
        if (hasBlockQuoteAncestor(context.node)) {
          return buildOutdentBlockQuoteEmptyParagraph(context);
        }
        return buildInsertBlockAfter(context);
      }
      if (context.cursorTextOffset <= 0) {
        return buildInsertBlockBefore(context);
      }
      if (context.cursorTextOffset >= context.visibleText.size()) {
        return buildInsertBlockAfter(context);
      }
      return buildSplitTextBlock(context, context.cursorSourceOffset - context.contentRange.byteStart);
  }

  command.fallbackSourceOffset = context.contentRange.byteStart + nextOffset;
  command.nodeHints.push_back(LocalEditNodeHint{
      context.node->id(),
      context.blockRange.byteStart >= 0 ? context.blockRange.byteStart : context.contentRange.byteStart,
      context.node->type()});
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildConvertPendingToBlock(const BlockEditContext& context) const {
  // Typora behavior: pressing Enter on a paragraph that is exactly a fence / $$ / \[ opener
  // commits it into a real code or math block (with the caret inside the empty content).
  Command command;
  if (!session_ || !context.node || context.blockType != BlockType::Paragraph) {
    return command;
  }

  const QString content = context.contentText;
  const PendingBlockMarker marker = detectPendingBlockMarker(QStringView(content));
  if (!marker.commitsOnEnter()) {
    return command;
  }

  const QString inserted = marker.opener + QStringLiteral("\n\n") + marker.closer;
  const qsizetype contentStart = context.contentRange.byteStart;

  command.sourceStart = contentStart;
  command.removedLength = context.contentRange.byteEnd - contentStart;
  command.insertedText = inserted;
  command.kind = EditTransaction::Kind::SplitParagraph;
  command.label = marker.targetType == BlockType::CodeFence ? QStringLiteral("Convert to Code Block")
                                                            : QStringLiteral("Convert to Math Block");
  // Caret lands on the blank line inside the new block (opener + "\n").
  command.fallbackSourceOffset = contentStart + marker.opener.size() + 1;
  command.nodeHints.push_back(LocalEditNodeHint{context.node->id(), contentStart, marker.targetType});
  command.structureEdit = true;
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildInsertBlockBefore(const BlockEditContext& context) const {
  Command command;
  const qsizetype insertStart = context.blockRange.byteStart >= 0 ? context.blockRange.byteStart : context.contentRange.byteStart;
  if (!session_ || insertStart < 0 || !context.node) {
    return command;
  }

  command.sourceStart = insertStart;
  command.removedLength = 0;
  const QString separator = paragraphSeparatorFor(context);
  command.insertedText = separator;
  command.kind = EditTransaction::Kind::SplitParagraph;
  command.label = QStringLiteral("Insert Paragraph Before");
  command.preferredCursor = cursorFor(context.node->id(), 0);
  command.fallbackSourceOffset = insertStart + separator.size();
  command.nodeHints.push_back(LocalEditNodeHint{context.node->id(), command.fallbackSourceOffset, context.node->type()});
  command.structureEdit = separator != QStringLiteral("\n\n");
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildInsertBlockAfter(const BlockEditContext& context) const {
  Command command;
  const qsizetype insertEnd = context.blockRange.byteEnd >= 0 ? context.blockRange.byteEnd : context.contentRange.byteEnd;
  if (!session_ || insertEnd < 0 || !context.node) {
    return command;
  }

  command.sourceStart = insertEnd;
  command.removedLength = 0;
  const QString separator = paragraphSeparatorFor(context);
  command.insertedText = separator;
  command.kind = EditTransaction::Kind::SplitParagraph;
  command.label = QStringLiteral("Insert Paragraph After");
  command.fallbackSourceOffset = insertEnd + separator.size();
  command.nodeHints.push_back(LocalEditNodeHint{context.node->id(), context.blockRange.byteStart, context.node->type()});
  command.structureEdit = separator != QStringLiteral("\n\n");
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildSplitTextBlock(
    const BlockEditContext& context,
    qsizetype contentOffset) const {
  Command command;
  if (!session_ || !context.node) {
    return command;
  }

  QString nextContent = context.contentText;
  qsizetype nextOffset = normalizeSplitOffset(nextContent, contentOffset);
  const QString separator = paragraphSeparatorFor(context);
  QString insertion = separator;
  if (context.blockType == BlockType::Heading && context.blockRange.byteStart >= 0 &&
      context.blockRange.byteStart < context.contentRange.byteStart) {
    insertion += session_->markdownText().mid(context.blockRange.byteStart, context.contentRange.byteStart - context.blockRange.byteStart);
  }
  insertion = insertionWithInlineSplit(insertion, nextContent, nextOffset);
  nextContent.insert(nextOffset, insertion);
  nextOffset += insertion.size();

  command.sourceStart = context.contentRange.byteStart;
  command.removedLength = context.contentRange.byteEnd - context.contentRange.byteStart;
  command.insertedText = nextContent;
  command.kind = EditTransaction::Kind::SplitParagraph;
  command.label = QStringLiteral("Split Paragraph");
  command.fallbackSourceOffset = context.contentRange.byteStart + nextOffset;
  command.nodeHints.push_back(LocalEditNodeHint{context.node->id(), context.blockRange.byteStart, context.node->type()});
  command.structureEdit = separator != QStringLiteral("\n\n");
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildMergeWithPreviousParagraph(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_ || !context.node) {
    return command;
  }

  BlockEditContext previous;
  if (!resolver_->previousEditableTextBlock(*context.node, previous)) {
    command.valid = true;
    command.handled = false;
    return command;
  }

  const qsizetype separatorStart = previous.contentRange.byteEnd;
  const bool preserveCurrentHeadingPrefix =
      context.blockType == BlockType::Heading && previous.contentText.isEmpty() && context.blockRange.byteStart >= 0 &&
      context.blockRange.byteStart < context.contentRange.byteStart;
  const qsizetype separatorEnd = preserveCurrentHeadingPrefix ? context.blockRange.byteStart : context.contentRange.byteStart;
  const qsizetype separatorLength = qMax<qsizetype>(0, separatorEnd - separatorStart);
  // Merge joins the two paragraphs by direct concatenation (Typora behaviour). We deliberately
  // do NOT insert a separator space: it is wrong for CJK and other scripts that don't use word
  // spacing, and the paragraphs' own text already carries whatever spacing the author intended.
  command.fallbackSourceOffset =
      preserveCurrentHeadingPrefix ? context.contentRange.byteStart - separatorLength : previous.contentRange.byteEnd;
  command.sourceStart = separatorStart;
  command.removedLength = separatorLength;
  command.insertedText.clear();
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Merge Paragraphs");
  if (previous.contentText.isEmpty() && context.contentText.isEmpty()) {
    command.preferredCursor = cursorFor(context.node->id(), 0);
    command.nodeHints.push_back(LocalEditNodeHint{context.node->id(), command.fallbackSourceOffset, context.node->type()});
    command.preferLaterEmptyAtOffset = true;
  }
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildMergeWithNextParagraph(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_ || !context.node) {
    return command;
  }

  BlockEditContext next;
  if (!resolver_->nextEditableTextBlock(*context.node, next)) {
    command.valid = true;
    command.handled = false;
    return command;
  }

  const qsizetype separatorStart = context.contentRange.byteEnd;
  const bool preserveNextHeadingPrefix =
      next.blockType == BlockType::Heading && context.contentText.isEmpty() && next.blockRange.byteStart >= 0 &&
      next.blockRange.byteStart < next.contentRange.byteStart;
  const qsizetype separatorEnd = preserveNextHeadingPrefix ? next.blockRange.byteStart : next.contentRange.byteStart;
  const qsizetype separatorLength = qMax<qsizetype>(0, separatorEnd - separatorStart);
  // Merge joins the two paragraphs by direct concatenation (Typora behaviour). No separator
  // space is inserted (wrong for CJK / non-space scripts; the text already carries its spacing).
  command.fallbackSourceOffset =
      preserveNextHeadingPrefix ? next.contentRange.byteStart - separatorLength : context.contentRange.byteEnd;
  command.sourceStart = separatorStart;
  command.removedLength = separatorLength;
  command.insertedText.clear();
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Merge Paragraphs");
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildSplitListItem(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_) {
    return command;
  }

  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!resolver_->listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return command;
  }

  const QString markdown = session_->markdownText();
  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const ListLineInfo info = listLineInfoFor(line);
  if (!info.valid) {
    return command;
  }

  QString nextLineContent = context.contentText;
  qsizetype contentOffset = context.plainInlineEditable ? context.cursorTextOffset : context.cursorSourceOffset - context.contentRange.byteStart;
  const qsizetype splitContentOffset = normalizeSplitOffset(nextLineContent, contentOffset);
  const qsizetype splitOffset = context.contentRange.byteStart + splitContentOffset;
  const qsizetype markerColumn = info.markerStart;
  const QString nextMarker = info.ordered
                                 ? QStringLiteral("%1%2 ").arg(info.orderedNumber + 1).arg(info.orderedDelimiter)
                                 : info.marker;
  const QString taskPrefix = info.task ? QStringLiteral("[ ] ") : QString();
  QString insertion = QLatin1Char('\n') + leadingSpaces(markerColumn) + nextMarker + taskPrefix;
  insertion = insertionWithInlineSplit(insertion, nextLineContent, splitContentOffset);
  nextLineContent.insert(splitContentOffset, insertion);
  command.sourceStart = context.contentRange.byteStart;
  command.removedLength = context.contentRange.byteEnd - context.contentRange.byteStart;
  if (info.ordered) {
    const qsizetype runEnd = orderedSiblingRunEnd(markdown, lineEnd < markdown.size() ? lineEnd + 1 : lineEnd, markerColumn);
    const QString tail = markdown.mid(context.contentRange.byteEnd, runEnd - context.contentRange.byteEnd);
    command.removedLength = runEnd - context.contentRange.byteStart;
    command.insertedText = renumberOrderedSiblings(nextLineContent + tail, markerColumn, info.orderedNumber + 1);
  } else {
    command.insertedText = nextLineContent;
  }
  command.kind = EditTransaction::Kind::SplitParagraph;
  command.label = QStringLiteral("Split List Item");
  command.fallbackSourceOffset = splitOffset + insertion.size();
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildExitListItem(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_) {
    return command;
  }

  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!resolver_->listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return command;
  }

  const QString markdown = session_->markdownText();
  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const ListLineInfo info = listLineInfoFor(line);
  if (!info.valid) {
    return command;
  }

  if (info.markerStart >= 2) {
    command.sourceStart = lineStart;
    command.removedLength = 2;
    command.insertedText.clear();
    command.fallbackSourceOffset = qMax<qsizetype>(lineStart, context.contentRange.byteStart - 2);
    command.label = QStringLiteral("Outdent List Item");
  } else {
    command.sourceStart = lineStart;
    command.removedLength = lineEnd - lineStart;
    command.insertedText = QLatin1Char('\n');
    command.fallbackSourceOffset = lineStart;
    command.label = QStringLiteral("Exit List Item");
  }
  command.kind = EditTransaction::Kind::DeleteText;
  command.structureEdit = true;
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildInsertListItemAbove(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_) {
    return command;
  }

  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!resolver_->listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return command;
  }

  const QString line = session_->markdownText().mid(lineStart, lineEnd - lineStart);
  const ListLineInfo info = listLineInfoFor(line);
  if (!info.valid) {
    return command;
  }

  const qsizetype markerColumn = info.markerStart;
  const QString taskPrefix = info.task ? QStringLiteral("[ ] ") : QString();
  const QString insertion = leadingSpaces(markerColumn) + info.marker + taskPrefix + QLatin1Char('\n');

  command.sourceStart = lineStart;
  command.removedLength = 0;
  command.insertedText = insertion;
  command.kind = EditTransaction::Kind::SplitParagraph;
  command.label = QStringLiteral("Insert List Item Above");
  command.fallbackSourceOffset = lineStart + insertion.size() - 1;
  if (info.ordered) {
    const QString markdown = session_->markdownText();
    const qsizetype runEnd = orderedSiblingRunEnd(markdown, lineStart, markerColumn);
    command.removedLength = runEnd - lineStart;
    command.insertedText = insertion + renumberOrderedSiblings(markdown.mid(lineStart, runEnd - lineStart), markerColumn, info.orderedNumber + 1);
  }
  command.structureEdit = true;
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildMergeWithPreviousListItem(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_ || !context.node) {
    return command;
  }

  const MarkdownNode* previous = context.node->previousSibling();
  if (!previous || previous->type() != BlockType::ListItem) {
    return command;
  }

  // Resolve previous item's context to get its line bounds
  MarkdownNode* prevNode = const_cast<MarkdownNode*>(previous);
  BlockEditContext prevContext;
  if (!resolver_->fill(*prevNode, prevContext)) {
    return command;
  }

  qsizetype prevLineStart = -1;
  qsizetype prevContentStart = -1;
  qsizetype prevLineEnd = -1;
  if (!resolver_->listItemLineBounds(prevContext, prevLineStart, prevContentStart, prevLineEnd)) {
    return command;
  }

  // Get current item's line bounds
  qsizetype curLineStart = -1;
  qsizetype curContentStart = -1;
  qsizetype curLineEnd = -1;
  if (!resolver_->listItemLineBounds(context, curLineStart, curContentStart, curLineEnd)) {
    return command;
  }

  const QString& markdown = session_->markdownText();
  const QString curLine = markdown.mid(curLineStart, curLineEnd - curLineStart);
  const ListLineInfo curInfo = listLineInfoFor(curLine);
  if (!curInfo.valid) {
    return command;
  }
  const qsizetype curContentRealStart = curInfo.task ? curLineStart + curInfo.taskContentStart : curContentStart;
  const QString curText = markdown.mid(curContentRealStart, curLineEnd - curContentRealStart);

  const QString separator =
      (!prevContext.contentText.trimmed().isEmpty() && !curText.trimmed().isEmpty()) ? QStringLiteral(" ") : QString();

  command.sourceStart = prevLineEnd;
  command.removedLength = curContentRealStart - prevLineEnd;
  command.insertedText = separator;
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Merge List Items");
  command.fallbackSourceOffset = prevLineEnd + separator.size();
  command.structureEdit = true;
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildMergeWithNextListItem(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_ || !context.node) {
    return command;
  }

  const MarkdownNode* next = context.node->nextSibling();
  if (!next || next->type() != BlockType::ListItem) {
    return command;
  }

  // Get current item's line bounds
  qsizetype curLineStart = -1;
  qsizetype curContentStart = -1;
  qsizetype curLineEnd = -1;
  if (!resolver_->listItemLineBounds(context, curLineStart, curContentStart, curLineEnd)) {
    return command;
  }

  // Resolve next item's context to get its line bounds
  MarkdownNode* nextNode = const_cast<MarkdownNode*>(next);
  BlockEditContext nextContext;
  if (!resolver_->fill(*nextNode, nextContext)) {
    return command;
  }

  qsizetype nextLineStart = -1;
  qsizetype nextContentStart = -1;
  qsizetype nextLineEnd = -1;
  if (!resolver_->listItemLineBounds(nextContext, nextLineStart, nextContentStart, nextLineEnd)) {
    return command;
  }

  const QString& markdown = session_->markdownText();
  const QString nextLine = markdown.mid(nextLineStart, nextLineEnd - nextLineStart);
  const ListLineInfo nextInfo = listLineInfoFor(nextLine);
  if (!nextInfo.valid) {
    return command;
  }
  const qsizetype nextContentRealStart = nextInfo.task ? nextLineStart + nextInfo.taskContentStart : nextContentStart;
  const QString nextText = markdown.mid(nextContentRealStart, nextLineEnd - nextContentRealStart);

  const QString separator =
      (!context.contentText.trimmed().isEmpty() && !nextText.trimmed().isEmpty()) ? QStringLiteral(" ") : QString();

  command.sourceStart = curLineEnd;
  command.removedLength = nextContentRealStart - curLineEnd;
  command.insertedText = separator;
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Merge List Items");
  command.fallbackSourceOffset = curLineEnd + separator.size();
  command.structureEdit = true;
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildOutdentListItem(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_) {
    return command;
  }

  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!resolver_->listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return command;
  }

  const QString& markdown = session_->markdownText();
  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const ListLineInfo info = listLineInfoFor(line);
  if (!info.valid) {
    return command;
  }
  if (info.markerStart >= 2) {
    command.sourceStart = lineStart;
    command.removedLength = 2;
    command.insertedText.clear();
    command.kind = EditTransaction::Kind::DeleteText;
    command.label = QStringLiteral("Outdent List Item");
    command.fallbackSourceOffset = qMax<qsizetype>(lineStart, context.contentRange.byteStart + context.cursorTextOffset - 2);
    command.structureEdit = true;
    command.valid = true;
    command.handled = true;
    return command;
  }

  command.sourceStart = lineStart;
  const qsizetype textStart = info.task ? lineStart + info.taskContentStart : contentStart;
  command.removedLength = textStart - lineStart;
  command.insertedText.clear();
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Exit List Item");
  command.fallbackSourceOffset = lineStart;
  command.structureEdit = true;
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildIndentListItem(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_ || !context.node || context.node->type() != BlockType::ListItem) {
    return command;
  }

  const MarkdownNode* previous = context.node->previousSibling();
  if (!previous || previous->type() != BlockType::ListItem) {
    return command;
  }

  qsizetype lineStart = -1;
  qsizetype contentStart = -1;
  qsizetype lineEnd = -1;
  if (!resolver_->listItemLineBounds(context, lineStart, contentStart, lineEnd)) {
    return command;
  }

  command.sourceStart = lineStart;
  command.removedLength = 0;
  command.insertedText = QStringLiteral("  ");
  command.kind = EditTransaction::Kind::InsertText;
  command.label = QStringLiteral("Indent List Item");
  command.fallbackSourceOffset = context.contentRange.byteStart + context.cursorTextOffset + 2;
  command.structureEdit = true;
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildConvertHeadingToParagraph(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !context.node || context.blockRange.byteStart < 0) {
    return command;
  }

  const qsizetype prefixLength = context.contentRange.byteStart - context.blockRange.byteStart;
  command.sourceStart = context.blockRange.byteStart;
  command.removedLength = prefixLength;
  command.insertedText.clear();
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Convert Heading to Paragraph");
  command.preferredCursor = cursorFor(context.node->id(), 0);
  command.fallbackSourceOffset = context.blockRange.byteStart;
  command.nodeHints.push_back(LocalEditNodeHint{context.node->id(), context.blockRange.byteStart, context.node->type()});
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildRemoveEmptyHeading(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !resolver_ || !context.node || context.blockRange.byteStart < 0) {
    return command;
  }

  BlockEditContext next;
  if (!resolver_->nextEditableTextBlock(*context.node, next)) {
    command.valid = true;
    command.handled = false;
    return command;
  }

  const qsizetype separatorStart = context.blockRange.byteStart;
  const qsizetype separatorEnd = next.contentRange.byteStart >= 0 ? next.contentRange.byteStart : next.blockRange.byteStart;
  const qsizetype separatorLength = qMax<qsizetype>(0, separatorEnd - separatorStart);

  command.sourceStart = separatorStart;
  command.removedLength = separatorLength;
  command.insertedText.clear();
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Delete Empty Heading");
  command.fallbackSourceOffset = separatorStart;
  command.valid = true;
  command.handled = true;
  return command;
}

QString TextBlockCommandBuilder::paragraphSeparatorFor(const BlockEditContext& context) const {
  if (!session_ || !context.node || !hasBlockQuoteAncestor(context.node)) {
    return QStringLiteral("\n\n");
  }

  const QString& markdown = session_->markdownText();
  const qsizetype referenceOffset = context.contentRange.byteStart >= 0 ? context.contentRange.byteStart : context.blockRange.byteStart;
  const qsizetype lineStart = lineStartForOffset(markdown, referenceOffset);
  qsizetype lineEnd = lineStart;
  while (lineEnd < markdown.size() && markdown.at(lineEnd) != QLatin1Char('\n')) {
    ++lineEnd;
  }

  const QString prefix = blockQuotePrefixForLine(markdown.mid(lineStart, lineEnd - lineStart));
  if (prefix.isEmpty()) {
    return QStringLiteral("\n\n");
  }
  QString blankPrefix = prefix;
  if (blankPrefix.endsWith(QLatin1Char(' '))) {
    blankPrefix.chop(1);
  }
  return QStringLiteral("\n") + blankPrefix + QStringLiteral("\n") + prefix;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildOutdentBlockQuote(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !context.node || context.contentRange.byteStart < 0) {
    return command;
  }

  const int depth = blockQuoteDepth(context.node);
  if (depth <= 0) {
    return command;
  }

  const MarkdownNode* quote = context.node->parent();
  if (!quote || quote->type() != BlockType::BlockQuote) {
    return command;
  }

  const QString& markdown = session_->markdownText();

  // Source span whose depth-th ">" marker gets stripped on every line — the source-text
  // equivalent of Typora's tree-level upStream (pop the block out one level).
  //
  // Content paragraph: its own line span; stripping each line drops one nesting level, lazy
  // continuation lines without a marker stay as-is, and a sole-line quote simply disappears.
  //
  // Empty first-child paragraph: the virtual empty is anchored at a single blank quote line,
  // but the *gap* that materialised it (from the quote's first line through the empty's line)
  // is several blank ">" lines. Stripping only the empty's own marker would orphan the
  // preceding ">" and split the quote in two. Instead span the whole leading gap so every
  // blank ">" line becomes an outer-level blank line — the empty pops out cleanly and the
  // remaining quote (the next sibling onward) is untouched, exactly like Typora.
  qsizetype spanStart = -1;
  qsizetype spanEnd = -1;
  if (context.visibleText.isEmpty()) {
    spanStart = sourceOffsetForLineColumn(markdown, quote->sourceRange().lineStart, 1);
    const int emptyLineEnd = context.node->sourceRange().lineEnd;
    spanEnd = emptyLineEnd > 0 ? sourceOffsetForLineEnd(markdown, emptyLineEnd) : spanStart;
  } else {
    spanStart = lineStartForOffset(markdown, context.contentRange.byteStart);
    spanEnd = context.blockRange.lineEnd > 0 ? sourceOffsetForLineEnd(markdown, context.blockRange.lineEnd)
                                             : lineEndForOffset(markdown, context.contentRange.byteStart);
  }
  if (spanStart < 0 || spanEnd < spanStart) {
    return command;
  }

  QStringList lines = markdown.mid(spanStart, spanEnd - spanStart).split(QLatin1Char('\n'));
  if (lines.isEmpty()) {
    return command;
  }
  const BlockQuoteMarkerRange firstMarker = nthBlockQuoteMarkerRange(lines.first(), depth);
  if (!firstMarker.valid) {
    return command;  // first span line must carry the depth-th marker, else bail to merge
  }
  for (QString& line : lines) {
    const BlockQuoteMarkerRange marker = nthBlockQuoteMarkerRange(line, depth);
    if (marker.valid) {
      line.remove(marker.start, marker.end - marker.start);
    }
  }

  command.sourceStart = spanStart;
  command.removedLength = spanEnd - spanStart;
  command.insertedText = lines.join(QLatin1Char('\n'));
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Outdent Quote");
  // Content: the caret stays at the content start, which shifts left by the stripped marker
  // length. Empty: the popped empty lands at the start of the (now outer-level) span.
  command.fallbackSourceOffset = context.visibleText.isEmpty()
                                     ? spanStart
                                     : context.contentRange.byteStart - (firstMarker.end - firstMarker.start);
  command.structureEdit = true;
  command.nodeHints.push_back(LocalEditNodeHint{context.node->id(), command.fallbackSourceOffset, context.node->type()});
  command.valid = true;
  command.handled = true;
  return command;
}

TextBlockCommandBuilder::Command TextBlockCommandBuilder::buildOutdentBlockQuoteEmptyParagraph(const BlockEditContext& context) const {
  Command command;
  if (!session_ || !context.node || context.contentRange.byteStart < 0) {
    return command;
  }

  const int depth = blockQuoteDepth(context.node);
  if (depth <= 0) {
    return command;
  }

  const QString& markdown = session_->markdownText();
  const qsizetype lineStart = lineStartForOffset(markdown, context.contentRange.byteStart);
  qsizetype lineEnd = lineStart;
  while (lineEnd < markdown.size() && markdown.at(lineEnd) != QLatin1Char('\n')) {
    ++lineEnd;
  }

  const QString line = markdown.mid(lineStart, lineEnd - lineStart);
  const BlockQuoteMarkerRange marker = nthBlockQuoteMarkerRange(line, depth);
  if (!marker.valid) {
    return command;
  }

  const QString outerPrefix = line.left(marker.start);
  command.sourceStart = lineStart + marker.start;
  command.removedLength = marker.end - marker.start;
  command.insertedText = QStringLiteral("\n") + outerPrefix;
  command.kind = EditTransaction::Kind::SplitParagraph;
  command.label = QStringLiteral("Outdent Quote Paragraph");
  command.fallbackSourceOffset = command.sourceStart + command.insertedText.size();
  command.structureEdit = true;
  command.valid = true;
  command.handled = true;
  return command;
}

CursorPosition TextBlockCommandBuilder::cursorFor(NodeId blockId, qsizetype offset) const {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = offset;
  return cursor;
}

}  // namespace muffin
