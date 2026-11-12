#include "projection/SelectionSerializer.h"

#include "document/InlineNode.h"
#include "document/BlockPredicates.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "document/ListItemSource.h"
#include "parser/CmarkGfmParser.h"
#include "projection/InlineProjection.h"
#include "projection/MarkdownHtmlSerializer.h"

#include <QStringList>
#include <algorithm>

namespace muffin {
namespace {

QString plainTextForNode(const MarkdownNode& node) {
  QString text;
  switch (node.type()) {
  case BlockType::Document:
  case BlockType::BlockQuote:
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

} // namespace

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
  if (selection.coversDocument(document.markdownText().size()))
    return document.markdownText().toString();
  qsizetype literalAnchorOffset = -1;
  qsizetype literalFocusOffset = -1;
  if (literalCursorSourceOffset(document, selection.anchor, literalAnchorOffset) &&
      literalCursorSourceOffset(document, selection.focus, literalFocusOffset) &&
      literalAnchorOffset != literalFocusOffset) {
    const qsizetype sourceStart = qMin(literalAnchorOffset, literalFocusOffset);
    const qsizetype sourceEnd = qMax(literalAnchorOffset, literalFocusOffset);
    const MarkdownNode* node = document.node(selection.anchor.blockId);
    // Emit the fence prefix (```cpp / $$ etc.) ONLY when the selection spans the ENTIRE literal
    // content — then the copied markdown needs the opener to stay a valid fenced block. A partial
    // in-block selection copies just the highlighted code text (no ```cpp), matching what the user
    // selected. literalCursorSourceOffset clamps offsets to [contentStart, contentEnd], so the
    // whole-content test is exact-equality. Cross-block selections don't reach this path.
    QString prefix;
    if (node) {
      qsizetype contentStart = -1;
      qsizetype contentEnd = -1;
      if (literalContentSourceRange(document, *node, contentStart, contentEnd) && sourceStart <= contentStart &&
          sourceEnd >= contentEnd) {
        prefix = literalMarkdownPrefix(document, *node);
      }
    }
    return prefix + document.markdownText().mid(sourceStart, sourceEnd - sourceStart);
  }

  qsizetype anchorOffset = -1;
  qsizetype anchorContextStart = -1;
  qsizetype focusOffset = -1;
  qsizetype focusContextStart = -1;
  EditableContext firstContext, lastContext;
  if (editableCursorSourceOffset(document, selection.anchor, anchorOffset, anchorContextStart, &firstContext) &&
      editableCursorSourceOffset(document, selection.focus, focusOffset, focusContextStart, &lastContext) &&
      anchorOffset != focusOffset) {
    qsizetype sourceStart = anchorOffset;
    qsizetype sourceEnd = focusOffset;
    if (sourceStart > sourceEnd) {
      qSwap(sourceStart, sourceEnd);
      qSwap(firstContext, lastContext);
    }
    const PieceTable& markdown = document.markdownText();
    // Block syntax belongs to a complete editable block, not to every text
    // fragment inside it. Apply the same coverage rule to both selection edges,
    // regardless of direction or whether the selection crosses blocks.
    const auto complete = [&](const EditableContext& context) {
      return context.editableNode && context.editableNode->type() != BlockType::TableCell &&
             sourceStart <= context.sourceStart && sourceEnd >= context.sourceEnd;
    };
    const bool wholeFirst = complete(firstContext);
    const bool wholeLast = complete(lastContext);
    const qsizetype exportStart =
        wholeFirst ? structuredLineStart(document, firstContext.node->id(), firstContext.sourceStart) : sourceStart;
    const auto constructEnd = [&](const EditableContext& context) {
      qsizetype end = context.sourceEnd;
      if (context.editableNode && context.editableNode->type() == BlockType::Heading &&
          context.editableNode->setext()) {
        end = blockLineSpan(*context.editableNode, markdown).byteEnd;
        // cmark's Setext construct may include the following blank-line separator.
        while (end > context.sourceEnd && (markdown.at(end - 1) == '\n' || markdown.at(end - 1) == '\r'))
          --end;
      }
      return end;
    };
    const qsizetype exportEnd = wholeLast ? qMax(sourceEnd, constructEnd(lastContext)) : sourceEnd;
    // Only the two edge blocks may be partial. Interior blocks and their exact
    // Markdown remain untouched; remove structural syntax from partial edges.
    QVector<InlineRange> omitted;
    if (!wholeFirst && firstContext.editableNode && firstContext.editableNode->type() == BlockType::Heading &&
        firstContext.editableNode->setext() && sourceEnd > firstContext.sourceEnd)
      omitted.push_back({firstContext.sourceEnd, constructEnd(firstContext)});
    if (!wholeLast && firstContext.node != lastContext.node && lastContext.editableNode &&
        lastContext.editableNode->type() != BlockType::TableCell) {
      const auto prefix = qMax(exportStart, lineStartOffset(markdown, lastContext.sourceStart));
      if (prefix < lastContext.sourceStart)
        omitted.push_back({prefix, lastContext.sourceStart});
    }
    const auto omitContinuationPrefixes = [&](const EditableContext& context) {
      if (!context.editableNode)
        return;
      const auto base = context.sourceStart - context.editableNode->topLevelBlock()->sourceRange().byteStart;
      for (const auto range : blockquoteContinuationPrefixes(*context.editableNode, context.sourceText, base)) {
        const InlineRange clipped{qMax(exportStart, context.sourceStart + range.start),
                                  qMin(exportEnd, context.sourceStart + range.end)};
        if (clipped.start < clipped.end)
          omitted.push_back(clipped);
      }
    };
    if (!wholeFirst)
      omitContinuationPrefixes(firstContext);
    if (!wholeLast && firstContext.node != lastContext.node)
      omitContinuationPrefixes(lastContext);
    std::sort(omitted.begin(), omitted.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    QString result;
    qsizetype position = exportStart;
    for (const auto& range : omitted) {
      result += markdown.mid(position, qMax<qsizetype>(0, range.start - position));
      position = qMax(position, qMin(exportEnd, range.end));
    }
    return result + markdown.mid(position, exportEnd - position);
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
  if (selection.coversDocument(document.markdownText().size()))
    return plainTextForMarkdownRange(document.markdownText().toString(), 0, document.markdownText().size());
  qsizetype literalAnchorOffset = -1;
  qsizetype literalFocusOffset = -1;
  if (literalCursorSourceOffset(document, selection.anchor, literalAnchorOffset) &&
      literalCursorSourceOffset(document, selection.focus, literalFocusOffset) &&
      literalAnchorOffset != literalFocusOffset) {
    const qsizetype sourceStart = qMin(literalAnchorOffset, literalFocusOffset);
    const qsizetype sourceEnd = qMax(literalAnchorOffset, literalFocusOffset);
    return document.markdownText().mid(sourceStart, sourceEnd - sourceStart);
  }

  const QString markdown = exportMarkdown(document, selection);
  return plainTextForMarkdownRange(markdown, 0, markdown.size());
}

QString SelectionSerializer::exportHtml(const MarkdownDocument& document, const SelectionRange& selection) const {
  const QString markdown = exportMarkdown(document, selection);
  return renderMarkdownToHtml(markdown);
}

QString SelectionSerializer::renderMarkdownToHtml(const QString& markdown) {
  if (markdown.isEmpty()) {
    return {};
  }
  // Parse through the editor's own CmarkGfmParser (runs splitDelimInlines + annotateAlertKinds +
  // all post-parse passes) and serialize the annotated tree to HTML. This makes export fidelity a
  // structural property of the editor's parse tree rather than a second cmark instance to keep in
  // sync — highlight / subscript / superscript / GitHub Alerts / emoji now survive export.
  return MarkdownHtmlSerializer::serializeSource(markdown);
}

bool SelectionSerializer::editableContextFor(const MarkdownDocument& document, const MarkdownNode& displayNode,
                                             EditableContext& context) const {
  if (displayNode.type() == BlockType::Paragraph && displayNode.parent() &&
      displayNode.parent()->type() == BlockType::ListItem && primaryParagraph(*displayNode.parent()) == &displayNode) {
    return editableContextFor(document, *displayNode.parent(), context);
  }
  const MarkdownNode* editable = &displayNode;
  const PieceTable& markdown = document.markdownText();
  ListItemSource listSource;
  if (displayNode.type() == BlockType::ListItem) {
    listSource = listItemSource(markdown, displayNode);
    if (!listSource.isValid()) {
      return false;
    }
    editable = listSource.paragraph;
    if (!editable) {
      context.node = &displayNode;
      context.editableNode = nullptr;
      context.sourceStart = listSource.content.start;
      context.sourceEnd = listSource.content.end;
      context.sourceText.clear();
      return true;
    }
  }

  if (!editable || (editable->type() != BlockType::Paragraph && editable->type() != BlockType::Heading &&
                    editable->type() != BlockType::TableCell)) {
    return false;
  }

  const SourceRange range = editable->sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return false;
  }

  qsizetype sourceStart = editable->type() == BlockType::TableCell && range.byteEnd >= range.byteStart
                              ? range.byteStart
                              : sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  qsizetype sourceEnd = editable->type() == BlockType::Heading ? headingContentEndOffset(*editable, markdown)
                        : editable->type() == BlockType::TableCell && range.byteEnd >= range.byteStart
                            ? range.byteEnd
                            : sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (sourceStart < 0 || sourceEnd < sourceStart) {
    return false;
  }

  if (listSource.isValid()) {
    sourceStart = listSource.content.start;
    sourceEnd = listSource.content.end;
  } else if (editable->type() == BlockType::Heading) {
    while (sourceStart < sourceEnd && markdown.at(sourceStart) == QLatin1Char('#')) {
      ++sourceStart;
    }
    if (sourceStart < sourceEnd && markdown.at(sourceStart).isSpace()) {
      ++sourceStart;
    }
  } else if (editable->type() == BlockType::Paragraph) {
    sourceStart = paragraphContentStartIncludingCommonMarkIndent(markdown, sourceStart);
  }

  context.node = &displayNode;
  context.editableNode = editable;
  context.sourceStart = sourceStart;
  context.sourceEnd = sourceEnd;
  context.sourceText = markdown.mid(sourceStart, sourceEnd - sourceStart);
  // Inline ranges are relative to the owning top-level block; the projection's sourceBase must be
  // the content offset within the block (sourceStart - top-level byteStart), not the absolute start.
  const qsizetype topLevelByteStart = editable->topLevelBlock()->sourceRange().byteStart;
  return isPlainInlineEditable(*editable, context.sourceText) ||
         InlineProjection(editable->inlines(), context.sourceText, {}, sourceStart - topLevelByteStart).isValid();
}

bool SelectionSerializer::editableCursorSourceOffset(const MarkdownDocument& document, const CursorPosition& cursor,
                                                     qsizetype& sourceOffset, qsizetype& contextSourceStart,
                                                     EditableContext* resolvedContext) const {
  if (!cursor.isValid()) {
    return false;
  }

  const MarkdownNode* node = document.node(cursor.text.nodeId.isValid() ? cursor.text.nodeId : cursor.blockId);
  EditableContext context;
  if (!node || !editableContextFor(document, *node, context)) {
    return false;
  }

  const qsizetype offset = qMax<qsizetype>(0, cursor.text.textOffset);
  qsizetype localSourceOffset = -1;
  CursorPosition contextCursor = cursor;
  contextCursor.text.textOffset = offset;
  InlineProjectionState projectionState =
      InlineProjectionState::forCursor(contextCursor, cursor.blockId, context.sourceStart);
  InlineProjection projection(context.editableNode ? context.editableNode->inlines() : QVector<InlineNode>(),
                              context.sourceText, projectionState,
                              context.sourceStart - node->topLevelBlock()->sourceRange().byteStart);
  if (cursor.text.sourceOffset >= context.sourceStart && cursor.text.sourceOffset <= context.sourceEnd) {
    localSourceOffset = cursor.text.sourceOffset - context.sourceStart;
  } else if (!projection.sourceOffsetForVisibleOffset(offset, localSourceOffset)) {
    localSourceOffset = qBound<qsizetype>(0, offset, context.sourceText.size());
  }
  sourceOffset = context.sourceStart + localSourceOffset;
  contextSourceStart = context.sourceStart;
  if (resolvedContext)
    *resolvedContext = std::move(context);
  return true;
}

bool SelectionSerializer::selectionSourceRange(const MarkdownDocument& document, const SelectionRange& selection,
                                               qsizetype& start, qsizetype& end) const {
  if (selection.isCollapsed()) {
    return false;
  }
  if (selection.coversDocument(document.markdownText().size())) {
    start = 0;
    end = document.markdownText().size();
    return true;
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

bool SelectionSerializer::blockSelectionSourceRange(const MarkdownDocument& document, const SelectionRange& selection,
                                                    qsizetype& start, qsizetype& end) const {
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
  if (!blockSourceRange(document, *anchorNode, anchorStart, anchorEnd) ||
      !blockSourceRange(document, *focusNode, focusStart, focusEnd)) {
    return false;
  }

  if (anchorStart < focusStart ||
      (anchorStart == focusStart && selection.anchor.text.textOffset <= selection.focus.text.textOffset)) {
    qsizetype anchorCursorOffset = -1;
    start = anchorNode == focusNode && literalCursorSourceOffset(document, selection.anchor, anchorCursorOffset)
                ? anchorCursorOffset
                : anchorStart + (anchorNode == focusNode
                                     ? qBound<qsizetype>(0, selection.anchor.text.textOffset, anchorEnd - anchorStart)
                                     : 0);
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
                : focusStart + (anchorNode == focusNode
                                    ? qBound<qsizetype>(0, selection.focus.text.textOffset, focusEnd - focusStart)
                                    : 0);
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

bool SelectionSerializer::blockSourceRange(const MarkdownDocument& document, const MarkdownNode& node, qsizetype& start,
                                           qsizetype& end) const {
  const SourceRange range = node.sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return false;
  }
  const PieceTable& markdown = document.markdownText();
  start = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  end = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (end >= 0 && range.lineEnd > range.lineStart && end < markdown.size()) {
    end = qMin<qsizetype>(markdown.size(), end);
  }
  return start >= 0 && end >= start;
}

bool SelectionSerializer::literalCursorSourceOffset(const MarkdownDocument& document, const CursorPosition& cursor,
                                                    qsizetype& sourceOffset) const {
  if (!cursor.isValid()) {
    return false;
  }
  const MarkdownNode* node = document.node(cursor.blockId);
  if (!node || (node->type() != BlockType::FrontMatter && node->type() != BlockType::CodeFence &&
                node->type() != BlockType::HtmlBlock && node->type() != BlockType::MathBlock)) {
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

bool SelectionSerializer::literalContentSourceRange(const MarkdownDocument& document, const MarkdownNode& node,
                                                    qsizetype& start, qsizetype& end) const {
  if (node.type() != BlockType::FrontMatter && node.type() != BlockType::CodeFence &&
      node.type() != BlockType::HtmlBlock && node.type() != BlockType::MathBlock) {
    return false;
  }
  qsizetype blockStart = -1;
  qsizetype blockEnd = -1;
  if (!blockSourceRange(document, node, blockStart, blockEnd)) {
    return false;
  }
  const PieceTable& markdown = document.markdownText();
  if (node.type() == BlockType::CodeFence || node.type() == BlockType::MathBlock ||
      (node.type() == BlockType::FrontMatter && node.frontMatterFormat() != FrontMatterFormat::Json)) {
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
        (node.type() == BlockType::MathBlock && isMathClosingLine(node, closingLine)) ||
        (node.type() == BlockType::FrontMatter &&
         ((node.frontMatterFormat() == FrontMatterFormat::Yaml && closingLine == QStringLiteral("---")) ||
          (node.frontMatterFormat() == FrontMatterFormat::Toml && closingLine == QStringLiteral("+++"))))) {
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
  if (!blockSourceRange(document, node, blockStart, blockEnd) ||
      !literalContentSourceRange(document, node, contentStart, contentEnd) || contentStart <= blockStart) {
    return {};
  }
  return document.markdownText().mid(blockStart, contentStart - blockStart);
}

qsizetype SelectionSerializer::structuredLineStart(const MarkdownDocument& document, NodeId block,
                                                   qsizetype contextSourceStart) const {
  if (contextSourceStart < 0) {
    return contextSourceStart;
  }

  const PieceTable& markdown = document.markdownText();
  if (const auto* node = document.node(block); node && node->type() == BlockType::ListItem) {
    const auto source = listItemSource(markdown, *node);
    if (source.isValid()) {
      const QString prefix = markdown.mid(source.firstLine.start, source.item.start - source.firstLine.start);
      return prefix.trimmed().isEmpty() ? source.firstLine.start : source.item.start;
    }
  }
  qsizetype prefixStart = qMin(contextSourceStart, markdown.size());
  while (prefixStart > 0 && markdown.at(prefixStart - 1) != QLatin1Char('\n')) {
    --prefixStart;
  }

  const QString prefix = markdown.mid(prefixStart, contextSourceStart - prefixStart);
  return prefix.trimmed().isEmpty() ? contextSourceStart : prefixStart;
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

QString SelectionSerializer::plainTextForMarkdownRange(const QString& markdownText, qsizetype start,
                                                       qsizetype end) const {
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

} // namespace muffin
