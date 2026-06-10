#include "editor/TextBlockCommandBuilder.h"

#include "document/MarkdownNode.h"
#include "editor/InlineSplit.h"

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
          return buildOutdentListItem(context);
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
      if (context.node->type() == BlockType::LinkDefinition || context.node->type() == BlockType::FootnoteDefinition) {
        const DefinitionBlock definition = context.node->definition();
        const DefinitionFieldRange finalField =
            context.node->type() == BlockType::FootnoteDefinition ? definition.noteRange : definition.titleRange;
        if (finalField.isValid() && context.cursorSourceOffset >= finalField.end) {
          return buildInsertBlockAfter(context);
        }
      }
      if (context.node->type() == BlockType::ListItem) {
        return context.contentText.trimmed().isEmpty() ? buildExitListItem(context) : buildSplitListItem(context);
      }
      if (context.visibleText.isEmpty()) {
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
  const QString separator = previous.contentText.isEmpty() || context.contentText.isEmpty() ? QString() : QStringLiteral(" ");
  command.fallbackSourceOffset =
      preserveCurrentHeadingPrefix ? context.contentRange.byteStart - separatorLength + separator.size() : previous.contentRange.byteEnd;
  command.sourceStart = separatorStart;
  command.removedLength = separatorLength;
  command.insertedText = separator;
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
  const QString separator = context.contentText.isEmpty() || next.contentText.isEmpty() ? QString() : QStringLiteral(" ");
  command.fallbackSourceOffset =
      preserveNextHeadingPrefix ? next.contentRange.byteStart - separatorLength + separator.size() : context.contentRange.byteEnd;
  command.sourceStart = separatorStart;
  command.removedLength = separatorLength;
  command.insertedText = separator;
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
  const QString marker = listMarkerFor(line);
  if (marker.isEmpty()) {
    return command;
  }

  QString nextLineContent = context.contentText;
  qsizetype contentOffset = context.plainInlineEditable ? context.cursorTextOffset : context.cursorSourceOffset - context.contentRange.byteStart;
  const qsizetype splitContentOffset = normalizeSplitOffset(nextLineContent, contentOffset);
  const qsizetype splitOffset = context.contentRange.byteStart + splitContentOffset;
  const qsizetype markerColumn = qMax<qsizetype>(0, contentStart - lineStart - marker.size());
  QString nextMarker = marker;
  qsizetype digitCount = 0;
  while (digitCount < marker.size() && marker.at(digitCount).isDigit()) {
    ++digitCount;
  }
  if (digitCount > 0) {
    const int nextNumber = marker.left(digitCount).toInt() + 1;
    nextMarker = QStringLiteral("%1. ").arg(nextNumber);
  }
  QString insertion = QLatin1Char('\n') + QString(markerColumn, QLatin1Char(' ')) + nextMarker;
  insertion = insertionWithInlineSplit(insertion, nextLineContent, splitContentOffset);
  nextLineContent.insert(splitContentOffset, insertion);
  command.sourceStart = context.contentRange.byteStart;
  command.removedLength = context.contentRange.byteEnd - context.contentRange.byteStart;
  command.insertedText = nextLineContent;
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

  command.sourceStart = lineStart;
  command.removedLength = lineEnd - lineStart;
  command.insertedText = QLatin1Char('\n');
  command.kind = EditTransaction::Kind::DeleteText;
  command.label = QStringLiteral("Exit List Item");
  command.fallbackSourceOffset = lineStart;
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
  const qsizetype indent =
      qMax<qsizetype>(0, contentStart - lineStart - listMarkerFor(markdown.mid(lineStart, lineEnd - lineStart)).size());
  if (indent >= 2) {
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
  command.removedLength = contentStart - lineStart;
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

CursorPosition TextBlockCommandBuilder::cursorFor(NodeId blockId, qsizetype offset) const {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = offset;
  return cursor;
}

}  // namespace muffin
