#include "document/SelectionSerializer.h"

#include "document/InlineNode.h"
#include "document/InlineProjection.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"

#include <QStringList>

namespace muffin {
namespace {

QString plainTextForNode(const MarkdownNode& node) {
  QString text;
  switch (node.type()) {
    case BlockType::Document:
    case BlockType::List:
    case BlockType::ListItem:
    case BlockType::Table:
      for (const auto& child : node.children()) {
        const QString childText = plainTextForNode(*child);
        if (childText.isEmpty()) {
          continue;
        }
        if (!text.isEmpty()) {
          text += QLatin1Char('\n');
        }
        text += childText;
      }
      return text;
    case BlockType::TableRow: {
      QStringList cells;
      for (const auto& cell : node.children()) {
        cells.push_back(plainTextForNode(*cell));
      }
      return cells.join(QLatin1Char('\t'));
    }
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::TableCell:
      return InlineProjection::plainTextForInlines(node.inlines());
    default:
      return node.literal();
  }
}

}  // namespace

SelectionExportResult SelectionSerializer::exportSelection(const SelectionExportRequest& request) const {
  SelectionExportResult result;
  if (!request.document) {
    return result;
  }

  switch (request.format) {
    case SelectionExportFormat::Markdown:
      result.text = exportMarkdown(*request.document, request.selection);
      result.mimeType = QStringLiteral("text/markdown");
      result.mimeData = result.text.toUtf8();
      return result;
    case SelectionExportFormat::PlainText:
      result.text = exportPlainText(*request.document, request.selection);
      result.mimeType = QStringLiteral("text/plain");
      result.mimeData = result.text.toUtf8();
      return result;
    case SelectionExportFormat::Html:
      result.text = exportHtml(*request.document, request.selection);
      result.mimeType = QStringLiteral("text/html");
      result.mimeData = result.text.toUtf8();
      return result;
  }
  return result;
}

QString SelectionSerializer::exportMarkdown(const MarkdownDocument& document, const SelectionRange& selection) const {
  EditableContext context;
  qsizetype start = 0;
  qsizetype end = 0;
  if (selectionContext(document, selection, context, start, end)) {
    const QString markdown = document.markdownText();
    const qsizetype prefixStart = structuredLineStart(markdown, context.sourceStart);
    const qsizetype selectedStart = context.sourceStart + qBound<qsizetype>(0, start, context.sourceText.size());
    const qsizetype selectedEnd = context.sourceStart + qBound<qsizetype>(0, end, context.sourceText.size());
    if (selectedEnd <= selectedStart) {
      return {};
    }
    return markdown.mid(prefixStart, context.sourceStart - prefixStart) + markdown.mid(selectedStart, selectedEnd - selectedStart);
  }

  qsizetype literalAnchorOffset = -1;
  qsizetype literalFocusOffset = -1;
  if (literalCursorSourceOffset(document, selection.anchor, literalAnchorOffset) &&
      literalCursorSourceOffset(document, selection.focus, literalFocusOffset) && literalAnchorOffset != literalFocusOffset) {
    const qsizetype sourceStart = qMin(literalAnchorOffset, literalFocusOffset);
    const qsizetype sourceEnd = qMax(literalAnchorOffset, literalFocusOffset);
    const MarkdownNode* node = document.node(selection.anchor.blockId);
    return (node ? literalMarkdownPrefix(document, *node) : QString()) + document.markdownText().mid(sourceStart, sourceEnd - sourceStart);
  }

  qsizetype anchorOffset = -1;
  qsizetype anchorContextStart = -1;
  qsizetype focusOffset = -1;
  qsizetype focusContextStart = -1;
  if (editableCursorSourceOffset(document, selection.anchor, anchorOffset, anchorContextStart) &&
      editableCursorSourceOffset(document, selection.focus, focusOffset, focusContextStart) && anchorOffset != focusOffset) {
    qsizetype sourceStart = anchorOffset;
    qsizetype sourceEnd = focusOffset;
    qsizetype startContext = anchorContextStart;
    if (sourceStart > sourceEnd) {
      qSwap(sourceStart, sourceEnd);
      startContext = focusContextStart;
    }
    const QString markdown = document.markdownText();
    const qsizetype prefixStart = structuredLineStart(markdown, startContext);
    const QString prefix = prefixStart < startContext ? markdown.mid(prefixStart, startContext - prefixStart) : QString();
    return prefix + markdown.mid(sourceStart, sourceEnd - sourceStart);
  }

  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  if (!selectionSourceRange(document, selection, sourceStart, sourceEnd) &&
      !blockSelectionSourceRange(document, selection, sourceStart, sourceEnd)) {
    return {};
  }
  return document.markdownText().mid(sourceStart, sourceEnd - sourceStart);
}

QString SelectionSerializer::exportPlainText(const MarkdownDocument& document, const SelectionRange& selection) const {
  qsizetype literalAnchorOffset = -1;
  qsizetype literalFocusOffset = -1;
  if (literalCursorSourceOffset(document, selection.anchor, literalAnchorOffset) &&
      literalCursorSourceOffset(document, selection.focus, literalFocusOffset) && literalAnchorOffset != literalFocusOffset) {
    const qsizetype sourceStart = qMin(literalAnchorOffset, literalFocusOffset);
    const qsizetype sourceEnd = qMax(literalAnchorOffset, literalFocusOffset);
    return document.markdownText().mid(sourceStart, sourceEnd - sourceStart);
  }

  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  if (selectionSourceRange(document, selection, sourceStart, sourceEnd) ||
      blockSelectionSourceRange(document, selection, sourceStart, sourceEnd)) {
    return plainTextForMarkdownRange(document.markdownText(), sourceStart, sourceEnd);
  }
  return {};
}

QString SelectionSerializer::exportHtml(const MarkdownDocument& document, const SelectionRange& selection) const {
  const QString markdown = exportMarkdown(document, selection);
  return markdown.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>\n"));
}

bool SelectionSerializer::selectionContext(
    const MarkdownDocument& document,
    const SelectionRange& selection,
    EditableContext& context,
    qsizetype& start,
    qsizetype& end) const {
  if (!selection.isSingleBlock() || selection.isCollapsed()) {
    return false;
  }

  const MarkdownNode* node = document.node(selection.focus.blockId);
  if (!node || !editableContextFor(document, *node, context)) {
    return false;
  }

  start = qBound<qsizetype>(0, selection.startOffset(), context.sourceText.size());
  end = qBound<qsizetype>(0, selection.endOffset(), context.sourceText.size());
  return start < end;
}

bool SelectionSerializer::editableContextFor(const MarkdownDocument& document, const MarkdownNode& displayNode, EditableContext& context) const {
  const MarkdownNode* editable = &displayNode;
  const QString markdown = document.markdownText();
  if (displayNode.type() == BlockType::ListItem) {
    editable = primaryParagraph(displayNode);
    if (!editable) {
      EditableContext markerContext;
      markerContext.node = &displayNode;
      markerContext.sourceStart =
          sourceOffsetForLineColumn(markdown, displayNode.sourceRange().lineStart, qMax(1, displayNode.sourceRange().columnStart));
      qsizetype lineStart = -1;
      qsizetype contentStart = -1;
      qsizetype lineEnd = -1;
      if (markerContext.sourceStart >= 0 && listItemLineBounds(markdown, markerContext, lineStart, contentStart, lineEnd)) {
        context.node = &displayNode;
        context.editableNode = nullptr;
        context.sourceStart = contentStart;
        context.sourceEnd = lineEnd;
        context.sourceText = markdown.mid(contentStart, lineEnd - contentStart);
        return context.sourceText.trimmed().isEmpty();
      }
    }
  }

  if (!editable || (editable->type() != BlockType::Paragraph && editable->type() != BlockType::Heading)) {
    return false;
  }

  const SourceRange range = editable->sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return false;
  }

  qsizetype sourceStart = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype sourceEnd = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (sourceStart < 0 || sourceEnd < sourceStart) {
    return false;
  }

  if (editable->type() == BlockType::Heading) {
    while (sourceStart < sourceEnd && markdown.at(sourceStart) == QLatin1Char('#')) {
      ++sourceStart;
    }
    if (sourceStart < sourceEnd && markdown.at(sourceStart).isSpace()) {
      ++sourceStart;
    }
  }

  context.node = &displayNode;
  context.editableNode = editable;
  context.sourceStart = sourceStart;
  context.sourceEnd = sourceEnd;
  context.sourceText = markdown.mid(sourceStart, sourceEnd - sourceStart);
  return isPlainInlineEditable(*editable, context.sourceText) || InlineProjection(editable->inlines(), context.sourceText).isValid();
}

bool SelectionSerializer::editableCursorSourceOffset(
    const MarkdownDocument& document,
    const CursorPosition& cursor,
    qsizetype& sourceOffset,
    qsizetype& contextSourceStart) const {
  if (!cursor.isValid()) {
    return false;
  }

  const MarkdownNode* node = document.node(cursor.blockId);
  EditableContext context;
  if (!node || !editableContextFor(document, *node, context)) {
    return false;
  }

  const qsizetype offset = qMax<qsizetype>(0, cursor.text.textOffset);
  qsizetype localSourceOffset = -1;
  InlineProjection projection(context.editableNode ? context.editableNode->inlines() : QVector<InlineNode>(), context.sourceText,
                              cursor.text.sourceOffset >= context.sourceStart ? cursor.text.sourceOffset - context.sourceStart : -1);
  if (cursor.text.sourceOffset >= context.sourceStart && cursor.text.sourceOffset <= context.sourceEnd) {
    localSourceOffset = cursor.text.sourceOffset - context.sourceStart;
  } else if (!projection.sourceOffsetForVisibleOffset(offset, localSourceOffset)) {
    localSourceOffset = qBound<qsizetype>(0, offset, context.sourceText.size());
  }
  sourceOffset = context.sourceStart + localSourceOffset;
  contextSourceStart = context.sourceStart;
  return true;
}

bool SelectionSerializer::selectionSourceRange(
    const MarkdownDocument& document,
    const SelectionRange& selection,
    qsizetype& start,
    qsizetype& end) const {
  if (selection.isCollapsed()) {
    return false;
  }

  qsizetype anchorOffset = -1;
  qsizetype anchorContextStart = -1;
  qsizetype focusOffset = -1;
  qsizetype focusContextStart = -1;
  if (!editableCursorSourceOffset(document, selection.anchor, anchorOffset, anchorContextStart) ||
      !editableCursorSourceOffset(document, selection.focus, focusOffset, focusContextStart)) {
    return literalCursorSourceOffset(document, selection.anchor, anchorOffset) &&
           literalCursorSourceOffset(document, selection.focus, focusOffset) && anchorOffset != focusOffset
               ? (start = qMin(anchorOffset, focusOffset), end = qMax(anchorOffset, focusOffset), true)
               : false;
  }

  start = qMin(anchorOffset, focusOffset);
  end = qMax(anchorOffset, focusOffset);
  return start < end;
}

bool SelectionSerializer::blockSelectionSourceRange(
    const MarkdownDocument& document,
    const SelectionRange& selection,
    qsizetype& start,
    qsizetype& end) const {
  if (selection.isCollapsed()) {
    return false;
  }

  const MarkdownNode* anchorNode = document.node(selection.anchor.blockId);
  const MarkdownNode* focusNode = document.node(selection.focus.blockId);
  if (!anchorNode || !focusNode) {
    return false;
  }

  qsizetype anchorStart = -1;
  qsizetype anchorEnd = -1;
  qsizetype focusStart = -1;
  qsizetype focusEnd = -1;
  if (!blockSourceRange(document, *anchorNode, anchorStart, anchorEnd) || !blockSourceRange(document, *focusNode, focusStart, focusEnd)) {
    return false;
  }

  if (anchorStart < focusStart ||
      (anchorStart == focusStart && selection.anchor.text.textOffset <= selection.focus.text.textOffset)) {
    qsizetype anchorCursorOffset = -1;
    start = anchorNode == focusNode && literalCursorSourceOffset(document, selection.anchor, anchorCursorOffset)
                ? anchorCursorOffset
                : anchorStart + (anchorNode == focusNode ? qBound<qsizetype>(0, selection.anchor.text.textOffset, anchorEnd - anchorStart) : 0);
    end = focusEnd;
    if (anchorNode == focusNode) {
      qsizetype focusCursorOffset = -1;
      end = literalCursorSourceOffset(document, selection.focus, focusCursorOffset)
                ? focusCursorOffset
                : anchorStart + qBound<qsizetype>(0, selection.focus.text.textOffset, anchorEnd - anchorStart);
    }
  } else {
    qsizetype focusCursorOffset = -1;
    start = anchorNode == focusNode && literalCursorSourceOffset(document, selection.focus, focusCursorOffset)
                ? focusCursorOffset
                : focusStart + (anchorNode == focusNode ? qBound<qsizetype>(0, selection.focus.text.textOffset, focusEnd - focusStart) : 0);
    end = anchorEnd;
    if (anchorNode == focusNode) {
      qsizetype anchorCursorOffset = -1;
      end = literalCursorSourceOffset(document, selection.anchor, anchorCursorOffset)
                ? anchorCursorOffset
                : focusStart + qBound<qsizetype>(0, selection.anchor.text.textOffset, focusEnd - focusStart);
    }
  }
  return start < end;
}

bool SelectionSerializer::blockSourceRange(const MarkdownDocument& document, const MarkdownNode& node, qsizetype& start, qsizetype& end) const {
  const SourceRange range = node.sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return false;
  }
  const QString markdown = document.markdownText();
  start = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  end = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (end >= 0 && range.lineEnd > range.lineStart && end < markdown.size()) {
    end = qMin<qsizetype>(markdown.size(), end);
  }
  return start >= 0 && end >= start;
}

bool SelectionSerializer::literalCursorSourceOffset(const MarkdownDocument& document, const CursorPosition& cursor, qsizetype& sourceOffset) const {
  if (!cursor.isValid()) {
    return false;
  }
  const MarkdownNode* node = document.node(cursor.blockId);
  if (!node || (node->type() != BlockType::CodeFence && node->type() != BlockType::HtmlBlock && node->type() != BlockType::MathBlock)) {
    return false;
  }

  qsizetype contentStart = -1;
  qsizetype contentEnd = -1;
  if (!literalContentSourceRange(document, *node, contentStart, contentEnd)) {
    return false;
  }
  if (cursor.text.sourceOffset >= contentStart && cursor.text.sourceOffset <= contentEnd) {
    sourceOffset = cursor.text.sourceOffset;
  } else {
    sourceOffset = contentStart + qBound<qsizetype>(0, cursor.text.textOffset, contentEnd - contentStart);
  }
  return true;
}

bool SelectionSerializer::literalContentSourceRange(const MarkdownDocument& document, const MarkdownNode& node, qsizetype& start, qsizetype& end) const {
  if (node.type() != BlockType::CodeFence && node.type() != BlockType::HtmlBlock && node.type() != BlockType::MathBlock) {
    return false;
  }
  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  if (!blockSourceRange(document, node, blockStart, blockEnd)) {
    return false;
  }
  const QString markdown = document.markdownText();
  if (node.type() == BlockType::CodeFence || node.type() == BlockType::MathBlock) {
    const qsizetype firstNewline = markdown.indexOf(QLatin1Char('\n'), blockStart);
    if (firstNewline < 0 || firstNewline >= blockEnd) {
      return false;
    }
    start = firstNewline + 1;
    end = qMax(start, blockEnd);
    if (end > start && markdown.at(end - 1) == QLatin1Char('\n')) {
      --end;
    }
    qsizetype closingStart = end;
    while (closingStart > start && markdown.at(closingStart - 1) != QLatin1Char('\n')) {
      --closingStart;
    }
    const QString closingLine = markdown.mid(closingStart, end - closingStart).trimmed();
    if ((node.type() == BlockType::CodeFence && closingLine.startsWith(QStringLiteral("```"))) ||
        (node.type() == BlockType::MathBlock && closingLine == QStringLiteral("$$"))) {
      end = qMax(start, closingStart - 1);
    }
    return end >= start;
  }

  start = blockStart;
  end = blockEnd;
  return true;
}

QString SelectionSerializer::literalMarkdownPrefix(const MarkdownDocument& document, const MarkdownNode& node) const {
  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  qsizetype contentStart = -1;
  qsizetype contentEnd = -1;
  if (!blockSourceRange(document, node, blockStart, blockEnd) || !literalContentSourceRange(document, node, contentStart, contentEnd) ||
      contentStart <= blockStart) {
    return {};
  }
  return document.markdownText().mid(blockStart, contentStart - blockStart);
}

qsizetype SelectionSerializer::structuredLineStart(const QString& markdown, qsizetype contextSourceStart) const {
  if (contextSourceStart < 0) {
    return contextSourceStart;
  }

  qsizetype prefixStart = qMin(contextSourceStart, markdown.size());
  while (prefixStart > 0 && markdown.at(prefixStart - 1) != QLatin1Char('\n')) {
    --prefixStart;
  }

  const QString prefix = markdown.mid(prefixStart, contextSourceStart - prefixStart);
  return prefix.trimmed().isEmpty() ? contextSourceStart : prefixStart;
}

qsizetype SelectionSerializer::sourceOffsetForLineColumn(const QString& text, int line, int column) const {
  if (line <= 0 || column <= 0) {
    return -1;
  }
  qsizetype offset = 0;
  for (int currentLine = 1; currentLine < line; ++currentLine) {
    const qsizetype newline = text.indexOf(QLatin1Char('\n'), offset);
    if (newline < 0) {
      return -1;
    }
    offset = newline + 1;
  }
  return qMin<qsizetype>(text.size(), offset + column - 1);
}

qsizetype SelectionSerializer::sourceOffsetForLineEnd(const QString& text, int line) const {
  if (line <= 0) {
    return -1;
  }
  qsizetype offset = sourceOffsetForLineColumn(text, line, 1);
  if (offset < 0) {
    return -1;
  }
  const qsizetype newline = text.indexOf(QLatin1Char('\n'), offset);
  return newline < 0 ? text.size() : newline;
}

const MarkdownNode* SelectionSerializer::primaryParagraph(const MarkdownNode& node) const {
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child.get();
    }
  }
  return nullptr;
}

QString SelectionSerializer::listMarkerFor(const QString& line) const {
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

bool SelectionSerializer::listItemLineBounds(
    const QString& markdown,
    const EditableContext& context,
    qsizetype& lineStart,
    qsizetype& contentStart,
    qsizetype& lineEnd) const {
  if (!context.node || context.node->type() != BlockType::ListItem) {
    return false;
  }

  lineStart = context.sourceStart;
  while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  lineEnd = context.sourceStart;
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

bool SelectionSerializer::isPlainInlineEditable(const MarkdownNode& node, const QString& sourceText) const {
  QString plain;
  for (const InlineNode& inlineNode : node.inlines()) {
    switch (inlineNode.type()) {
      case InlineType::Text:
        plain += inlineNode.text();
        break;
      case InlineType::SoftBreak:
        plain += QLatin1Char('\n');
        break;
      case InlineType::LineBreak:
        plain += QStringLiteral("  \n");
        break;
      default:
        return false;
    }
  }

  return plain == sourceText;
}

QString SelectionSerializer::plainTextForMarkdownRange(const QString& markdownText, qsizetype start, qsizetype end) const {
  if (start < 0 || end <= start) {
    return {};
  }

  const QString markdown = markdownText.mid(start, end - start);
  ParseResult result = CmarkGfmParser().parseDocument(QStringView(markdown), ParseOptions());
  if (!result.root) {
    return markdown;
  }
  QString text = plainTextForNode(*result.root);
  while (text.endsWith(QLatin1Char('\n'))) {
    text.chop(1);
  }
  return text;
}

}  // namespace muffin
