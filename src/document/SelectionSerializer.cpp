#include "document/SelectionSerializer.h"

#include "document/InlineNode.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "parser/CmarkGfmParser.h"

#include <QStringList>

namespace muffin {
namespace {

QString plainTextForInlines(const QVector<InlineNode>& inlines) {
  QString text;
  for (const InlineNode& inlineNode : inlines) {
    switch (inlineNode.type()) {
      case InlineType::Text:
      case InlineType::Code:
      case InlineType::InlineMath:
      case InlineType::HtmlInline:
        text += inlineNode.text();
        break;
      case InlineType::SoftBreak:
        text += QLatin1Char(' ');
        break;
      case InlineType::LineBreak:
        text += QLatin1Char('\n');
        break;
      case InlineType::Image:
        text += inlineNode.alt();
        break;
      default:
        text += plainTextForInlines(inlineNode.children());
        break;
    }
  }
  return text;
}

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
      return plainTextForInlines(node.inlines());
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
  qsizetype mappedStart = -1;
  return isPlainInlineEditable(*editable, context.sourceText) ||
         sourceOffsetForVisibleOffset(editable->inlines(), context.sourceText, 0, mappedStart);
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
  if (!context.editableNode ||
      !sourceOffsetForVisibleOffset(context.editableNode->inlines(), context.sourceText, offset, localSourceOffset)) {
    localSourceOffset = qBound<qsizetype>(0, offset, context.sourceText.size());
  }
  sourceOffset = context.sourceStart + localSourceOffset;
  contextSourceStart = context.sourceStart;
  return true;
}

bool SelectionSerializer::sourceOffsetForVisibleOffset(
    const QVector<InlineNode>& inlines,
    const QString& sourceText,
    qsizetype visibleOffset,
    qsizetype& sourceOffset) const {
  if (visibleOffset <= 0) {
    sourceOffset = 0;
    return true;
  }

  qsizetype searchFrom = 0;
  qsizetype consumed = 0;
  for (const InlineNode& inlineNode : inlines) {
    QString inlineMarkdown;
    switch (inlineNode.type()) {
      case InlineType::Text:
        inlineMarkdown = inlineNode.text();
        break;
      case InlineType::Code:
        inlineMarkdown = QStringLiteral("`%1`").arg(inlineNode.text());
        break;
      case InlineType::InlineMath:
        inlineMarkdown = QStringLiteral("$%1$").arg(inlineNode.text());
        break;
      case InlineType::HtmlInline:
        inlineMarkdown = inlineNode.text();
        break;
      case InlineType::Emphasis:
        inlineMarkdown = QStringLiteral("%1%2%1").arg(inlineNode.marker().isEmpty() ? QStringLiteral("*") : inlineNode.marker(),
                                                       plainTextForInlines(inlineNode.children()));
        break;
      case InlineType::Strong:
        inlineMarkdown = QStringLiteral("%1%2%1").arg(inlineNode.marker().isEmpty() ? QStringLiteral("**") : inlineNode.marker(),
                                                       plainTextForInlines(inlineNode.children()));
        break;
      case InlineType::Strikethrough:
        inlineMarkdown = QStringLiteral("~~%1~~").arg(plainTextForInlines(inlineNode.children()));
        break;
      case InlineType::Link:
        inlineMarkdown = QStringLiteral("[%1](%2%3)").arg(
            plainTextForInlines(inlineNode.children()),
            inlineNode.href(),
            inlineNode.title().isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(inlineNode.title()));
        break;
      case InlineType::Image:
        inlineMarkdown = QStringLiteral("![%1](%2%3)").arg(
            inlineNode.alt(),
            inlineNode.href(),
            inlineNode.title().isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(inlineNode.title()));
        break;
      case InlineType::SoftBreak:
        inlineMarkdown = QStringLiteral("\n");
        break;
      case InlineType::LineBreak:
        inlineMarkdown = QStringLiteral("  \n");
        break;
      default:
        inlineMarkdown = plainTextForInlines(inlineNode.children());
        break;
    }

    const qsizetype sourceStart = sourceText.indexOf(inlineMarkdown, searchFrom);
    if (sourceStart < 0) {
      return false;
    }

    const qsizetype inlineVisibleLength = plainTextForInlines(QVector<InlineNode>{inlineNode}).size();
    if (visibleOffset <= consumed + inlineVisibleLength) {
      const qsizetype insideOffset = visibleOffset - consumed;
      const qsizetype mapped = sourceOffsetWithinInline(inlineNode, inlineMarkdown, insideOffset);
      if (mapped < 0) {
        return false;
      }
      sourceOffset = sourceStart + mapped;
      return true;
    }

    consumed += inlineVisibleLength;
    searchFrom = sourceStart + inlineMarkdown.size();
  }

  if (visibleOffset == consumed) {
    sourceOffset = sourceText.size();
    return true;
  }
  return false;
}

qsizetype SelectionSerializer::sourceOffsetWithinInline(const InlineNode& node, const QString& markdown, qsizetype visibleOffset) const {
  if (visibleOffset < 0) {
    return -1;
  }

  switch (node.type()) {
    case InlineType::Text:
    case InlineType::HtmlInline:
      return qBound<qsizetype>(0, visibleOffset, markdown.size());
    case InlineType::Code:
    case InlineType::InlineMath:
      if (visibleOffset <= 0) {
        return 0;
      }
      if (visibleOffset >= node.text().size()) {
        return markdown.size();
      }
      return qBound<qsizetype>(1, visibleOffset + 1, qMax<qsizetype>(1, markdown.size() - 1));
    case InlineType::Emphasis:
    case InlineType::Strong: {
      const QString marker = node.marker().isEmpty()
                                 ? (node.type() == InlineType::Strong ? QStringLiteral("**") : QStringLiteral("*"))
                                 : node.marker();
      const qsizetype visibleLength = plainTextForInlines(node.children()).size();
      if (visibleOffset <= 0) {
        return 0;
      }
      if (visibleOffset >= visibleLength) {
        return markdown.size();
      }
      return qBound<qsizetype>(marker.size(), marker.size() + visibleOffset, qMax<qsizetype>(marker.size(), markdown.size() - marker.size()));
    }
    case InlineType::Strikethrough:
      if (visibleOffset <= 0) {
        return 0;
      }
      if (visibleOffset >= plainTextForInlines(node.children()).size()) {
        return markdown.size();
      }
      return qBound<qsizetype>(2, 2 + visibleOffset, qMax<qsizetype>(2, markdown.size() - 2));
    case InlineType::Link: {
      const qsizetype visibleLength = plainTextForInlines(node.children()).size();
      if (visibleOffset <= 0) {
        return 0;
      }
      if (visibleOffset >= visibleLength) {
        return markdown.size();
      }
      return qBound<qsizetype>(1, 1 + visibleOffset, qMax<qsizetype>(1, markdown.indexOf(QLatin1Char(']'))));
    }
    case InlineType::Image:
      if (visibleOffset <= 0) {
        return 0;
      }
      if (visibleOffset >= node.alt().size()) {
        return markdown.size();
      }
      return qBound<qsizetype>(2, 2 + visibleOffset, qMax<qsizetype>(2, markdown.indexOf(QLatin1Char(']'))));
    case InlineType::SoftBreak:
      return qBound<qsizetype>(0, visibleOffset, markdown.size());
    case InlineType::LineBreak:
      return visibleOffset <= 0 ? 0 : markdown.size();
    default:
      return qBound<qsizetype>(0, visibleOffset, markdown.size());
  }
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
    return false;
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
    start = anchorStart + (anchorNode == focusNode ? qBound<qsizetype>(0, selection.anchor.text.textOffset, anchorEnd - anchorStart) : 0);
    end = focusEnd;
    if (anchorNode == focusNode) {
      end = anchorStart + qBound<qsizetype>(0, selection.focus.text.textOffset, anchorEnd - anchorStart);
    }
  } else {
    start = focusStart + (anchorNode == focusNode ? qBound<qsizetype>(0, selection.focus.text.textOffset, focusEnd - focusStart) : 0);
    end = anchorEnd;
    if (anchorNode == focusNode) {
      end = focusStart + qBound<qsizetype>(0, selection.anchor.text.textOffset, focusEnd - focusStart);
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
