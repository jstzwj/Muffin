#include "parser/CmarkGfmParser.h"

#include "document/DefinitionBlock.h"
#include "document/InlineNode.h"
#include "document/LineStartOffsetCache.h"
#include "document/PendingBlockMarker.h"
#include "parser/CmarkNodeAdapter.h"
#include "parser/MarkdownSerializer.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QStringList>
#include <QVector>

extern "C" {
#include "cmark-gfm-core-extensions.h"
}

namespace muffin {
namespace {

void annotateSourceOffsets(const LineStartOffsetCache& lineOffsets, QStringView markdown, MarkdownNode& node) {
  SourceRange range = node.sourceRange();
  if (range.lineStart > 0) {
    const qsizetype start = lineOffsets.offsetForLineColumn(range.lineStart, qMax(1, range.columnStart));
    const qsizetype end = lineOffsets.lineEndOffset(range.lineEnd);
    if (start >= 0 && end >= start) {
      range.byteStart = start;
      range.byteEnd = end;
      node.setSourceRange(range);
    }
  }

  // cmark-gfm reports a math block's source range up to the last CONTENT line only, excluding the
  // closing delimiter line (`$$` / `\]`). Every other consumer — block deletion, slice selection,
  // serialization-range replacement — needs the range to span the whole block including the closer,
  // just like a fenced code block. Extend it here, at the single place block byte ranges are
  // resolved, so the range is correct everywhere instead of being patched ad hoc downstream.
  if (node.type() == BlockType::MathBlock && range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
    for (const QString& closer : {QStringLiteral("\n$$"), QStringLiteral("\n\\]")}) {
      if (range.byteEnd + closer.size() <= markdown.size() &&
          markdown.mid(range.byteEnd, closer.size()) == closer) {
        range.byteEnd += closer.size();
        range.lineEnd = lineOffsets.lineForOffset(range.byteEnd);
        node.setSourceRange(range);
        break;
      }
    }
  }

  for (const auto& child : node.children()) {
    annotateSourceOffsets(lineOffsets, markdown, *child);
  }
}

bool isListMarkerAt(const QString& line, int index, int& markerEnd) {
  if (index >= line.size()) {
    return false;
  }
  if ((line.at(index) == QLatin1Char('-') || line.at(index) == QLatin1Char('*') || line.at(index) == QLatin1Char('+')) &&
      index + 1 < line.size() && line.at(index + 1).isSpace()) {
    markerEnd = index + 2;
    return true;
  }

  int digitEnd = index;
  while (digitEnd < line.size() && line.at(digitEnd).isDigit()) {
    ++digitEnd;
  }
  if (digitEnd > index && digitEnd + 1 < line.size() &&
      (line.at(digitEnd) == QLatin1Char('.') || line.at(digitEnd) == QLatin1Char(')')) &&
      line.at(digitEnd + 1).isSpace()) {
    markerEnd = digitEnd + 2;
    return true;
  }
  return false;
}

int containerPrefixEnd(const QString& line) {
  int index = 0;
  bool consumedContainer = false;
  while (true) {
    const int beforeIndent = index;
    int indent = 0;
    while (index < line.size() && indent < 3 && line.at(index) == QLatin1Char(' ')) {
      ++index;
      ++indent;
    }

    if (index < line.size() && line.at(index) == QLatin1Char('>')) {
      ++index;
      if (index < line.size() && line.at(index) == QLatin1Char(' ')) {
        ++index;
      }
      consumedContainer = true;
      continue;
    }

    int markerEnd = -1;
    if (isListMarkerAt(line, index, markerEnd)) {
      index = markerEnd;
      consumedContainer = true;
      continue;
    }

    return consumedContainer ? beforeIndent : 0;
  }
}

int contentStartWithinContainer(const QString& line) {
  const int prefixEnd = containerPrefixEnd(line);
  int index = prefixEnd;
  int indent = 0;
  while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
    ++index;
    ++indent;
  }
  return indent <= 3 ? index : -1;
}

bool hasOnlyTrailingSpaces(const QString& line, int start) {
  for (int i = start; i < line.size(); ++i) {
    if (!line.at(i).isSpace()) {
      return false;
    }
  }
  return true;
}

bool isLegacyMathDelimiterLine(const QString& line, int contentStart, const QString& delimiter) {
  if (contentStart < 0 || contentStart + delimiter.size() > line.size()) {
    return false;
  }
  return line.mid(contentStart, delimiter.size()) == delimiter &&
         hasOnlyTrailingSpaces(line, contentStart + delimiter.size());
}

QString bodyForLegacyMathBlock(QStringView markdown, const SourceRange& range) {
  if (range.byteStart < 0 || range.byteEnd > markdown.size() || range.byteEnd <= range.byteStart) {
    return {};
  }

  const QString text = markdown.toString();
  qsizetype openerLineStart = range.byteStart;
  while (openerLineStart > 0 && text.at(openerLineStart - 1) != QLatin1Char('\n')) {
    --openerLineStart;
  }
  qsizetype openerLineEnd = range.byteStart;
  while (openerLineEnd < text.size() && text.at(openerLineEnd) != QLatin1Char('\n')) {
    ++openerLineEnd;
  }
  if (openerLineEnd >= text.size()) {
    return {};
  }

  const QString openerLine = text.mid(openerLineStart, openerLineEnd - openerLineStart);
  const bool stripContainerPrefixes = contentStartWithinContainer(openerLine) == range.byteStart - openerLineStart;

  QStringList bodyLines;
  qsizetype lineStart = openerLineEnd + 1;
  while (lineStart <= range.byteEnd && lineStart < text.size()) {
    qsizetype lineEnd = lineStart;
    while (lineEnd < text.size() && lineEnd < range.byteEnd && text.at(lineEnd) != QLatin1Char('\n')) {
      ++lineEnd;
    }

    const QString line = text.mid(lineStart, lineEnd - lineStart);
    const int contentStart = contentStartWithinContainer(line);
    if (isLegacyMathDelimiterLine(line, contentStart, QStringLiteral("\\]"))) {
      break;
    }

    bodyLines.append(stripContainerPrefixes && contentStart > 0 ? line.mid(contentStart) : line);

    if (lineEnd >= text.size() || lineEnd >= range.byteEnd) {
      break;
    }
    lineStart = lineEnd + 1;
  }
  return bodyLines.join(QLatin1Char('\n')).trimmed();
}

bool fenceInfoAt(const QString& line, int contentStart, QChar& fenceChar, int& fenceLength) {
  if (contentStart < 0 || contentStart >= line.size()) {
    return false;
  }
  const QChar c = line.at(contentStart);
  if (c != QLatin1Char('`') && c != QLatin1Char('~')) {
    return false;
  }
  int run = 0;
  while (contentStart + run < line.size() && line.at(contentStart + run) == c) {
    ++run;
  }
  if (run < 3) {
    return false;
  }
  fenceChar = c;
  fenceLength = run;
  return true;
}

bool isDollarMathDelimiterLine(const QString& line, int contentStart) {
  if (contentStart < 0 || contentStart + 2 > line.size()) {
    return false;
  }
  if (line.at(contentStart) != QLatin1Char('$') || line.at(contentStart + 1) != QLatin1Char('$')) {
    return false;
  }
  return hasOnlyTrailingSpaces(line, contentStart + 2);
}

enum class MathScanState {
  None,
  Dollar,
  Bracket
};

// Typora-compatible display math: a `\[` line opens and the next matching `\]` line closes a
// LaTeX display-math block. cmark-gfm only parses `$$`, so rewrite the paired delimiters to `$$`.
// The delimiter swaps are byte-for-byte the same length on the same column, so every offset cmark
// reports still maps onto the original text. Lines inside fenced code blocks are left untouched.
// Dollar delimiter lines inside bracket math are temporarily escaped, then the MathBlock literal is
// restored from the original source when delimiters are annotated.
QString legacyMathDelimitersToDollar(QStringView markdown) {
  QStringList lines = markdown.toString().split(QLatin1Char('\n'));
  bool inFence = false;
  MathScanState mathState = MathScanState::None;
  QChar fenceChar;
  int fenceLength = 0;
  for (QString& line : lines) {
    const int contentStart = contentStartWithinContainer(line);

    if (inFence) {
      if (contentStart >= 0 && line.size() > contentStart && line.at(contentStart) == fenceChar) {
        int run = 0;
        while (contentStart + run < line.size() && line.at(contentStart + run) == fenceChar) {
          ++run;
        }
        if (run >= fenceLength && hasOnlyTrailingSpaces(line, contentStart + run)) {
          inFence = false;
          fenceChar = QChar();
          fenceLength = 0;
        }
      }
      continue;
    }

    if (mathState == MathScanState::Dollar) {
      if (isDollarMathDelimiterLine(line, contentStart)) {
        mathState = MathScanState::None;
      }
      continue;
    }

    if (mathState == MathScanState::Bracket) {
      if (isLegacyMathDelimiterLine(line, contentStart, QStringLiteral("\\]"))) {
        line.replace(contentStart, 2, QStringLiteral("$$"));
        mathState = MathScanState::None;
      } else if (isDollarMathDelimiterLine(line, contentStart)) {
        line.replace(contentStart, 2, QStringLiteral("\\$"));
      }
      continue;
    }

    QChar openingFenceChar;
    int openingFenceLength = 0;
    if (fenceInfoAt(line, contentStart, openingFenceChar, openingFenceLength)) {
      inFence = true;
      fenceChar = openingFenceChar;
      fenceLength = openingFenceLength;
      continue;
    }

    if (isDollarMathDelimiterLine(line, contentStart)) {
      mathState = MathScanState::Dollar;
      continue;
    }

    if (isLegacyMathDelimiterLine(line, contentStart, QStringLiteral("\\["))) {
      line.replace(contentStart, 2, QStringLiteral("$$"));
      mathState = MathScanState::Bracket;
    }
  }
  return lines.join(QLatin1Char('\n'));
}

// After source offsets are known, mark each MathBlock whose original opener was `\[` so the
// serializer can re-emit `\[ ... \]` instead of `$$ ... $$`.
void annotateMathDelimiters(QStringView markdown, MarkdownNode& root) {
  const auto visit = [](auto&& self, QStringView md, MarkdownNode& node) -> void {
    if (node.type() == BlockType::MathBlock) {
      const SourceRange range = node.sourceRange();
      if (range.byteStart >= 0 && range.byteStart + 1 < md.size() &&
          md.at(range.byteStart) == QLatin1Char('\\') && md.at(range.byteStart + 1) == QLatin1Char('[')) {
        node.setMathDelimiter(MathDelimiter::Bracket);
        node.setLiteral(bodyForLegacyMathBlock(md, range));
      }
    }
    for (const auto& child : node.children()) {
      self(self, md, *child);
    }
  };
  visit(visit, markdown, root);
}

void annotateDefinitionBlocks(
    MarkdownNode& root,
    const QVector<DefinitionParseResult>& definitions,
    const LineStartOffsetCache& lineOffsets) {
  for (const DefinitionParseResult& parsedDefinition : definitions) {
    const DefinitionBlock& definition = parsedDefinition.definition;
    const auto visit = [&](const auto& self, MarkdownNode& node) -> bool {
      const SourceRange range = node.sourceRange();
      const bool matchesRange = range.byteStart == definition.markerRange.start &&
                                range.byteEnd >= definition.markerRange.end;
      const bool matchesType =
          (definition.kind == DefinitionBlock::Kind::Footnote && node.type() == BlockType::FootnoteDefinition) ||
          (definition.kind == DefinitionBlock::Kind::Link && node.type() == BlockType::LinkDefinition);
      if (matchesRange && matchesType) {
        DefinitionBlock annotated = definition;
        if (node.type() == BlockType::FootnoteDefinition) {
          annotated.sourceRange = {range.byteStart, range.byteEnd};
        }
        node.setDefinition(annotated);
        if (definition.sourceRange.isValid() && node.type() != BlockType::FootnoteDefinition) {
          SourceRange preciseRange = node.sourceRange();
          preciseRange.byteStart = definition.sourceRange.start;
          preciseRange.byteEnd = definition.sourceRange.end;
          preciseRange.lineStart = lineOffsets.lineForOffset(preciseRange.byteStart);
          preciseRange.lineEnd = lineOffsets.lineForOffset(preciseRange.byteEnd);
          const qsizetype lineStart = lineOffsets.offsetForLineColumn(preciseRange.lineStart, 1);
          preciseRange.columnStart = lineStart >= 0 ? static_cast<int>(preciseRange.byteStart - lineStart + 1) : 1;
          preciseRange.columnEnd = lineStart >= 0 ? static_cast<int>(preciseRange.byteEnd - lineStart + 1) : preciseRange.columnStart;
          node.setSourceRange(preciseRange);
        }
        return true;
      }
      for (const auto& child : node.children()) {
        if (self(self, *child)) {
          return true;
        }
      }
      return false;
    };
    visit(visit, root);
  }
}

bool hasDefinitionBlockStartingAt(const MarkdownNode& root, const DefinitionBlock& definition) {
  const BlockType expectedType =
      definition.kind == DefinitionBlock::Kind::Footnote ? BlockType::FootnoteDefinition : BlockType::LinkDefinition;
  for (const auto& child : root.children()) {
    if (child->type() == expectedType && child->sourceRange().byteStart == definition.markerRange.start) {
      return true;
    }
  }
  return false;
}

bool shouldInsertSyntheticDefinition(const DefinitionParseResult& parsedDefinition) {
  return parsedDefinition.classification == DefinitionParseClassification::ValidMarkdownDefinition ||
         parsedDefinition.classification == DefinitionParseClassification::VirtualTemplate;
}

std::unique_ptr<MarkdownNode> createDefinitionNode(const DefinitionBlock& definition, const LineStartOffsetCache& lineOffsets) {
  const BlockType type =
      definition.kind == DefinitionBlock::Kind::Footnote ? BlockType::FootnoteDefinition : BlockType::LinkDefinition;
  auto node = std::make_unique<MarkdownNode>(type);
  node->setDefinition(definition);
  node->setLiteral(definition.kind == DefinitionBlock::Kind::Footnote ? definition.note : definition.destination);

  SourceRange range;
  range.byteStart = definition.sourceRange.isValid() ? definition.sourceRange.start : definition.markerRange.start;
  range.byteEnd = definition.sourceRange.isValid()
                      ? definition.sourceRange.end
                      : qMax(definition.markerRange.end,
                             qMax(definition.destinationRange.end, qMax(definition.titleRange.end, definition.noteRange.end)));
  range.lineStart = lineOffsets.lineForOffset(range.byteStart);
  range.lineEnd = lineOffsets.lineForOffset(range.byteEnd);
  const qsizetype lineStart = lineOffsets.offsetForLineColumn(range.lineStart, 1);
  range.columnStart = lineStart >= 0 ? static_cast<int>(range.byteStart - lineStart + 1) : 1;
  range.columnEnd = lineStart >= 0 ? static_cast<int>(range.byteEnd - lineStart + 1) : range.columnStart;
  node->setSourceRange(range);
  return node;
}

bool isVirtualEmptyParagraph(const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  return node.type() == BlockType::Paragraph && range.byteStart >= 0 && range.byteEnd == range.byteStart;
}

bool isDefinitionSourceParagraph(const MarkdownNode& node, const DefinitionBlock& definition) {
  if (node.type() != BlockType::Paragraph || !definition.sourceRange.isValid()) {
    return false;
  }
  const SourceRange range = node.sourceRange();
  return range.byteStart == definition.sourceRange.start && range.byteEnd == definition.sourceRange.end;
}

void insertMissingDefinitions(MarkdownNode& root, const QVector<DefinitionParseResult>& definitions, const LineStartOffsetCache& lineOffsets) {
  if (root.type() != BlockType::Document) {
    return;
  }
  for (const DefinitionParseResult& parsedDefinition : definitions) {
    if (!shouldInsertSyntheticDefinition(parsedDefinition)) {
      continue;
    }
    const DefinitionBlock& definition = parsedDefinition.definition;
    if (hasDefinitionBlockStartingAt(root, definition)) {
      continue;
    }

    qsizetype insertAt = 0;
    while (insertAt < root.children().size() &&
           root.children().at(static_cast<size_t>(insertAt))->sourceRange().byteStart < definition.markerRange.start) {
      ++insertAt;
    }
    if (insertAt < root.children().size() &&
        (isDefinitionSourceParagraph(*root.children().at(static_cast<size_t>(insertAt)), definition) ||
         (isVirtualEmptyParagraph(*root.children().at(static_cast<size_t>(insertAt))) &&
          root.children().at(static_cast<size_t>(insertAt))->sourceRange().byteStart == definition.markerRange.start))) {
      root.detachChild(insertAt);
    }
    root.insertChild(insertAt, createDefinitionNode(definition, lineOffsets));
  }
}

void insertTrailingEmptyParagraphAfterDefinition(
    QStringView markdown,
    MarkdownNode& root,
    const QVector<DefinitionParseResult>& definitions,
    const LineStartOffsetCache& lineOffsets) {
  if (root.type() != BlockType::Document || definitions.isEmpty()) {
    return;
  }

  const DefinitionBlock* trailingDefinition = nullptr;
  for (const DefinitionParseResult& parsedDefinition : definitions) {
    const DefinitionBlock& definition = parsedDefinition.definition;
    if (!definition.sourceRange.isValid()) {
      continue;
    }
    qsizetype cursor = definition.sourceRange.end;
    while (cursor < markdown.size() && markdown.at(cursor) == QLatin1Char('\r')) {
      ++cursor;
    }
    int newlineCount = 0;
    while (cursor < markdown.size() && markdown.at(cursor) == QLatin1Char('\n')) {
      ++newlineCount;
      ++cursor;
      if (cursor < markdown.size() && markdown.at(cursor) == QLatin1Char('\r')) {
        ++cursor;
      }
    }
    if (cursor == markdown.size() && newlineCount >= 2) {
      trailingDefinition = &definition;
    }
  }
  if (!trailingDefinition) {
    return;
  }

  for (const auto& child : root.children()) {
    if (isVirtualEmptyParagraph(*child) && child->sourceRange().byteStart >= trailingDefinition->sourceRange.end) {
      return;
    }
  }

  const int emptyLine = lineOffsets.lineForOffset(trailingDefinition->sourceRange.end) + 2;
  auto paragraph = std::make_unique<MarkdownNode>(BlockType::Paragraph);
  paragraph->inlines().push_back(InlineNode::text(QString()));

  SourceRange range;
  range.lineStart = emptyLine;
  range.lineEnd = emptyLine;
  range.columnStart = 1;
  range.columnEnd = 1;
  range.byteStart = markdown.size();
  range.byteEnd = markdown.size();
  paragraph->setSourceRange(range);
  root.appendChild(std::move(paragraph));
}

struct TableCellFieldRange {
  qsizetype start = -1;
  qsizetype end = -1;
};

struct FrontMatterScanResult {
  bool found = false;
  FrontMatterFormat format = FrontMatterFormat::None;
  QString literal;
  qsizetype sourceEnd = 0;
  int lineEnd = 0;
};

bool isHorizontalPadding(QChar ch) {
  return ch == QLatin1Char(' ') || ch == QLatin1Char('\t');
}

qsizetype skipBom(QStringView text) {
  return !text.isEmpty() && text.front() == QChar(0xFEFF) ? 1 : 0;
}

qsizetype lineEndOffset(QStringView text, qsizetype lineStart) {
  const qsizetype newline = text.indexOf(QLatin1Char('\n'), lineStart);
  return newline < 0 ? text.size() : newline;
}

qsizetype nextLineStart(QStringView text, qsizetype lineStart) {
  const qsizetype end = lineEndOffset(text, lineStart);
  return end < text.size() ? end + 1 : text.size();
}

QStringView trimmedLine(QStringView text, qsizetype start, qsizetype end) {
  if (end > start && text.at(end - 1) == QLatin1Char('\r')) {
    --end;
  }
  while (start < end && isHorizontalPadding(text.at(start))) {
    ++start;
  }
  while (end > start && isHorizontalPadding(text.at(end - 1))) {
    --end;
  }
  return text.mid(start, end - start);
}

bool lineEquals(QStringView text, qsizetype start, QStringView expected) {
  return trimmedLine(text, start, lineEndOffset(text, start)) == expected;
}

FrontMatterScanResult scanFencedFrontMatter(QStringView markdown, QStringView fence, FrontMatterFormat format) {
  const qsizetype start = skipBom(markdown);
  if (!lineEquals(markdown, start, fence)) {
    return {};
  }

  qsizetype contentStart = nextLineStart(markdown, start);
  qsizetype lineStart = contentStart;
  int lineNumber = 2;
  while (lineStart < markdown.size()) {
    const qsizetype end = lineEndOffset(markdown, lineStart);
    if (trimmedLine(markdown, lineStart, end) == fence) {
      FrontMatterScanResult result;
      result.found = true;
      result.format = format;
      result.literal = markdown.mid(contentStart, qMax<qsizetype>(0, lineStart - contentStart)).toString();
      if (result.literal.endsWith(QLatin1Char('\n'))) {
        result.literal.chop(1);
      }
      if (result.literal.endsWith(QLatin1Char('\r'))) {
        result.literal.chop(1);
      }
      result.sourceEnd = end;
      result.lineEnd = lineNumber;
      return result;
    }
    lineStart = nextLineStart(markdown, lineStart);
    ++lineNumber;
  }
  return {};
}

qsizetype scanJsonObjectEnd(QStringView markdown, qsizetype start) {
  if (start >= markdown.size() || markdown.at(start) != QLatin1Char('{')) {
    return -1;
  }

  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (qsizetype i = start; i < markdown.size(); ++i) {
    const QChar ch = markdown.at(i);
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (ch == QLatin1Char('\\')) {
        escaped = true;
      } else if (ch == QLatin1Char('"')) {
        inString = false;
      }
      continue;
    }

    if (ch == QLatin1Char('"')) {
      inString = true;
    } else if (ch == QLatin1Char('{')) {
      ++depth;
    } else if (ch == QLatin1Char('}')) {
      --depth;
      if (depth == 0) {
        return i + 1;
      }
      if (depth < 0) {
        return -1;
      }
    }
  }
  return -1;
}

FrontMatterScanResult scanJsonFrontMatter(QStringView markdown) {
  const qsizetype start = skipBom(markdown);
  if (start >= markdown.size() || markdown.at(start) != QLatin1Char('{')) {
    return {};
  }

  const qsizetype end = scanJsonObjectEnd(markdown, start);
  if (end <= start) {
    return {};
  }
  if (end < markdown.size() && markdown.at(end) != QLatin1Char('\n') && markdown.at(end) != QLatin1Char('\r')) {
    return {};
  }

  const QString literal = markdown.mid(start, end - start).toString();
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(literal.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return {};
  }

  FrontMatterScanResult result;
  result.found = true;
  result.format = FrontMatterFormat::Json;
  result.literal = literal;
  result.sourceEnd = end;
  result.lineEnd = 1;
  for (QChar ch : literal) {
    if (ch == QLatin1Char('\n')) {
      ++result.lineEnd;
    }
  }
  return result;
}

FrontMatterScanResult scanFrontMatter(QStringView markdown) {
  if (FrontMatterScanResult result = scanFencedFrontMatter(markdown, QStringLiteral("---"), FrontMatterFormat::Yaml); result.found) {
    return result;
  }
  if (FrontMatterScanResult result = scanFencedFrontMatter(markdown, QStringLiteral("+++"), FrontMatterFormat::Toml); result.found) {
    return result;
  }
  return scanJsonFrontMatter(markdown);
}

using muffin::shiftInlineSourcePositions;

void annotateTableCellInlineSourceRanges(QVector<InlineNode>& inlines, const QString& content, qsizetype sourceBase) {
  qsizetype searchFrom = 0;
  for (InlineNode& inlineNode : inlines) {
    const QString markdown = MarkdownSerializer().serializeInline(inlineNode);
    const qsizetype start = markdown.isEmpty() ? searchFrom : content.indexOf(markdown, searchFrom);
    if (start < 0) {
      continue;
    }
    const qsizetype end = start + markdown.size();
    InlineSourceRanges ranges;
    ranges.source = {sourceBase + start, sourceBase + end};
    ranges.content = ranges.source;

    const QString marker = inlineNode.marker();
    if (!marker.isEmpty() && end - start >= marker.size() * 2 &&
        (inlineNode.type() == InlineType::Emphasis || inlineNode.type() == InlineType::Strong ||
         inlineNode.type() == InlineType::Strikethrough)) {
      ranges.openMarker = {sourceBase + start, sourceBase + start + marker.size()};
      ranges.closeMarker = {sourceBase + end - marker.size(), sourceBase + end};
      ranges.content = {ranges.openMarker.end, ranges.closeMarker.start};
      inlineNode.setSourceRanges(ranges);
      annotateTableCellInlineSourceRanges(inlineNode.children(), content, sourceBase);
    } else if (inlineNode.type() == InlineType::Code || inlineNode.type() == InlineType::InlineMath) {
      const QChar delim = (inlineNode.type() == InlineType::Code) ? QLatin1Char('`') : QLatin1Char('$');
      const qsizetype openLen = countLeading(content, start, end, delim);
      const qsizetype closeLen = countTrailing(content, start, end, delim);
      if (openLen > 0 && closeLen >= openLen) {
        ranges.openMarker = {sourceBase + start, sourceBase + start + openLen};
        ranges.closeMarker = {sourceBase + end - closeLen, sourceBase + end};
        ranges.content = {ranges.openMarker.end, ranges.closeMarker.start};
        inlineNode.setSourceRanges(ranges);
      }
    } else if (inlineNode.type() == InlineType::Link) {
      ranges.openMarker = {sourceBase + start, sourceBase + start + 1};
      const qsizetype labelEndInSlice = content.mid(start, end - start).indexOf(QLatin1Char(']'));
      const qsizetype labelEnd = labelEndInSlice >= 0 ? start + labelEndInSlice : qsizetype(-1);
      ranges.content = labelEnd >= 0 ? InlineRange{sourceBase + start + 1, sourceBase + labelEnd}
                                     : InlineRange{ranges.openMarker.end, ranges.openMarker.end};
      inlineNode.setSourceRanges(ranges);
      annotateTableCellInlineSourceRanges(inlineNode.children(), content, sourceBase);
    } else if (inlineNode.type() == InlineType::Image) {
      ranges.openMarker = {sourceBase + start, sourceBase + start + 2};
      const qsizetype labelEndInSlice = content.mid(start, end - start).indexOf(QLatin1Char(']'));
      const qsizetype labelEnd = labelEndInSlice >= 0 ? start + labelEndInSlice : qsizetype(-1);
      ranges.content = labelEnd >= 0 ? InlineRange{sourceBase + start + 2, sourceBase + labelEnd}
                                     : InlineRange{ranges.openMarker.end, ranges.openMarker.end};
      inlineNode.setSourceRanges(ranges);
    } else {
      inlineNode.setSourceRanges(ranges);
    }
    searchFrom = end;
  }
}

void shiftSourceRanges(MarkdownNode& node, qsizetype delta, int lineDelta) {
  SourceRange range = node.sourceRange();
  if (range.byteStart >= 0 && range.byteEnd >= range.byteStart) {
    range.byteStart += delta;
    range.byteEnd += delta;
    if (range.lineStart > 0) {
      range.lineStart += lineDelta;
    }
    if (range.lineEnd > 0) {
      range.lineEnd += lineDelta;
    }
    node.setSourceRange(range);
  }
  DefinitionBlock definition = node.definition();
  if (definition.isValid()) {
    auto shiftField = [delta](DefinitionFieldRange& field) {
      if (field.isValid()) {
        field.start += delta;
        field.end += delta;
      }
    };
    shiftField(definition.labelRange);
    shiftField(definition.destinationRange);
    shiftField(definition.titleRange);
    shiftField(definition.noteRange);
    shiftField(definition.markerRange);
    shiftField(definition.sourceRange);
    node.setDefinition(definition);
  }
  shiftInlineSourcePositions(node.inlines(), delta);
  for (const auto& child : node.children()) {
    shiftSourceRanges(*child, delta, lineDelta);
  }
}

QVector<TableCellFieldRange> tableRowFieldRanges(QStringView rowText, qsizetype rowStartOffset) {
  QVector<qsizetype> separators;
  bool escaped = false;
  for (qsizetype i = 0; i < rowText.size(); ++i) {
    const QChar ch = rowText.at(i);
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == QLatin1Char('\\')) {
      escaped = true;
      continue;
    }
    if (ch == QLatin1Char('|')) {
      separators.push_back(rowStartOffset + i);
    }
  }

  QVector<TableCellFieldRange> ranges;
  if (separators.isEmpty()) {
    return ranges;
  }
  if (separators.first() != rowStartOffset) {
    separators.prepend(rowStartOffset - 1);
  }
  const qsizetype rowEndOffset = rowStartOffset + rowText.size();
  if (separators.last() != rowEndOffset - 1) {
    separators.push_back(rowEndOffset);
  }

  ranges.reserve(qMax(0, separators.size() - 1));
  for (qsizetype i = 0; i + 1 < separators.size(); ++i) {
    qsizetype start = separators.at(i) + 1;
    qsizetype end = separators.at(i + 1);
    while (start < end && isHorizontalPadding(rowText.at(start - rowStartOffset))) {
      ++start;
    }
    while (end > start && isHorizontalPadding(rowText.at(end - rowStartOffset - 1))) {
      --end;
    }
    ranges.push_back({start, end});
  }
  return ranges;
}

void annotateTableCellSourceRanges(QStringView markdown, const LineStartOffsetCache& lineOffsets, MarkdownNode& node) {
  if (node.type() == BlockType::TableRow) {
    const SourceRange rowRange = node.sourceRange();
    const qsizetype rowStart = lineOffsets.offsetForLineColumn(rowRange.lineStart, 1);
    const qsizetype rowEnd = lineOffsets.lineEndOffset(rowRange.lineStart);
    if (rowStart >= 0 && rowEnd >= rowStart) {
      const QVector<TableCellFieldRange> fields = tableRowFieldRanges(markdown.mid(rowStart, rowEnd - rowStart), rowStart);
      int column = 0;
      for (const auto& child : node.children()) {
        if (child->type() == BlockType::TableCell && column < fields.size()) {
          SourceRange range = child->sourceRange();
          range.lineStart = rowRange.lineStart;
          range.lineEnd = rowRange.lineStart;
          range.byteStart = fields.at(column).start;
          range.byteEnd = fields.at(column).end;
          range.columnStart = static_cast<int>(range.byteStart - rowStart + 1);
          range.columnEnd = static_cast<int>(range.byteEnd - rowStart);
          child->setSourceRange(range);
          annotateTableCellInlineSourceRanges(
              child->inlines(),
              markdown.mid(range.byteStart, range.byteEnd - range.byteStart).toString(),
              range.byteStart);
        }
        ++column;
      }
    }
  }

  for (const auto& child : node.children()) {
    annotateTableCellSourceRanges(markdown, lineOffsets, *child);
  }
}

}  // namespace

CmarkGfmParser::CmarkGfmParser() {
  ensureExtensionsRegistered();
}

ParseResult CmarkGfmParser::parseDocument(QStringView markdown, const ParseOptions& options) {
  QElapsedTimer timer;
  timer.start();

  const FrontMatterScanResult frontMatter = options.enableFrontMatter ? scanFrontMatter(markdown) : FrontMatterScanResult{};
  qsizetype markdownStart = 0;
  std::unique_ptr<MarkdownNode> frontMatterNode;
  if (frontMatter.found) {
    markdownStart = frontMatter.sourceEnd;
    if (markdownStart < markdown.size() && markdown.at(markdownStart) == QLatin1Char('\r')) {
      ++markdownStart;
    }
    if (markdownStart < markdown.size() && markdown.at(markdownStart) == QLatin1Char('\n')) {
      ++markdownStart;
    }

    frontMatterNode = std::make_unique<MarkdownNode>(BlockType::FrontMatter);
    frontMatterNode->setFrontMatterFormat(frontMatter.format);
    frontMatterNode->setLiteral(frontMatter.literal);
    SourceRange range;
    range.byteStart = 0;
    range.byteEnd = frontMatter.sourceEnd;
    range.lineStart = 1;
    range.lineEnd = frontMatter.lineEnd;
    range.columnStart = 1;
    range.columnEnd = 1;
    frontMatterNode->setSourceRange(range);
  }

  const QStringView markdownToParse = markdown.mid(markdownStart);
  const QByteArray utf8 = legacyMathDelimitersToDollar(markdownToParse).toUtf8();
  const QVector<DefinitionParseResult> definitions = scanDefinitionBlocks(markdownToParse);
  cmark_parser* parser = cmark_parser_new(CMARK_OPT_DEFAULT | CMARK_OPT_FOOTNOTES);
  attachExtensions(parser, options);
  cmark_parser_feed(parser, utf8.constData(), static_cast<size_t>(utf8.size()));
  cmark_node* document = cmark_parser_finish(parser);

  const LineStartOffsetCache lineOffsets(markdownToParse);
  CmarkNodeAdapter adapter(&lineOffsets, markdownToParse);
  ParseResult result;
  result.root = adapter.convertBlock(document);
  insertVirtualEmptyParagraphs(markdownToParse, *result.root);
  annotateSourceOffsets(lineOffsets, markdownToParse, *result.root);
  annotateMathDelimiters(markdownToParse, *result.root);
  insertVirtualEmptyParagraphsInBlockQuotes(markdownToParse, *result.root);
  insertMissingDefinitions(*result.root, definitions, lineOffsets);
  annotateDefinitionBlocks(*result.root, definitions, lineOffsets);
  insertTrailingEmptyParagraphAfterDefinition(markdownToParse, *result.root, definitions, lineOffsets);
  annotateTableCellSourceRanges(markdownToParse, lineOffsets, *result.root);
  // cmark turns a lone `*`/`-`/`+`/`1.` (trailing newline satisfies its bullet check) into an empty
  // list item the editor can't edit (its list-marker validation requires a space). Demote those to
  // paragraphs at parse time so load and edit paths agree. Done last, on the pre-front-matter tree.
  demotePendingListMarkers(*result.root, markdownToParse.toString());

  if (frontMatterNode) {
    const int lineDelta = frontMatter.lineEnd;
    for (const auto& child : result.root->children()) {
      shiftSourceRanges(*child, markdownStart, lineDelta);
    }
    result.root->insertChild(0, std::move(frontMatterNode));
  }

  result.elapsedMs = timer.elapsed();

  cmark_node_free(document);
  cmark_parser_free(parser);
  return result;
}

ParseResult CmarkGfmParser::parseBlock(QStringView markdown, BlockType, const ParseOptions& options) {
  return parseDocument(markdown, options);
}

void CmarkGfmParser::ensureExtensionsRegistered() {
  cmark_gfm_core_extensions_ensure_registered();
}

void CmarkGfmParser::attachExtensions(cmark_parser* parser, const ParseOptions& options) {
  const auto attach = [parser](const char* name) {
    if (cmark_syntax_extension* extension = cmark_find_syntax_extension(name)) {
      cmark_parser_attach_syntax_extension(parser, extension);
    }
  };

  if (options.enableTable) attach("table");
  if (options.enableStrikethrough) attach("strikethrough");
  if (options.enableAutolink) attach("autolink");
  if (options.enableTaskList) attach("tasklist");
  attach("math");
}

void CmarkGfmParser::insertVirtualEmptyParagraphs(QStringView markdown, MarkdownNode& root) const {
  if (root.type() != BlockType::Document) {
    return;
  }

  const QString text = markdown.toString();
  const QStringList lines = text.split(QLatin1Char('\n'));
  qsizetype childIndex = 0;
  int previousEndLine = 0;

  if (root.children().empty()) {
    if (markdown.isEmpty()) {
      root.appendChild(createVirtualEmptyParagraph(1));
      return;
    }
    const int emptyCount = lines.size() / 2;
    for (int i = 0; i < emptyCount; ++i) {
      const int emptyLine = 1 + i * 2;
      if (emptyLine - 1 < lines.size() && lines.at(emptyLine - 1).trimmed().isEmpty()) {
        root.appendChild(createVirtualEmptyParagraph(emptyLine));
      }
    }
    return;
  }

  while (childIndex < root.children().size()) {
    MarkdownNode* child = root.children().at(static_cast<size_t>(childIndex)).get();
    const SourceRange range = child->sourceRange();
    const int startLine = range.lineStart;
    const int blankLines = startLine > 0 ? startLine - previousEndLine - 1 : 0;
    const int emptyCount = qMax(0, blankLines / 2);
    const int firstEmptyLine = previousEndLine == 0 ? 1 : previousEndLine + 2;
    for (int i = 0; i < emptyCount; ++i) {
      const int emptyLine = firstEmptyLine + i * 2;
      const int lineIndex = emptyLine - 1;
      if (lineIndex >= 0 && lineIndex < lines.size() && lines.at(lineIndex).trimmed().isEmpty()) {
        root.insertChild(childIndex, createVirtualEmptyParagraph(emptyLine));
        ++childIndex;
      }
    }

    previousEndLine = qMax(previousEndLine, range.lineEnd);
    ++childIndex;
  }

  const int totalLines = lines.size();
  // Container blocks (Lists, BlockQuotes) absorb trailing blank lines into
  // their lineEnd, so previousEndLine may overestimate where the content ends.
  // Use the actual last non-blank line for trailing blank-line counting.
  int lastContentLine = 0;
  for (int i = lines.size() - 1; i >= 0; --i) {
    if (!lines.at(i).trimmed().isEmpty()) {
      lastContentLine = i + 1;  // 1-indexed
      break;
    }
  }
  const int trailingLines = totalLines - lastContentLine;
  const int trailingEmptyCount = qMax(0, trailingLines / 2);
  for (int i = 0; i < trailingEmptyCount; ++i) {
    const int emptyLine = lastContentLine + 2 + i * 2;
    const int lineIndex = emptyLine - 1;
    if (lineIndex >= 0 && lineIndex < lines.size() && lines.at(lineIndex).trimmed().isEmpty()) {
      root.appendChild(createVirtualEmptyParagraph(emptyLine));
    }
  }
}

void CmarkGfmParser::insertVirtualEmptyParagraphsInBlockQuotes(QStringView markdown, MarkdownNode& root) const {
  const QString text = markdown.toString();
  const QStringList lines = text.split(QLatin1Char('\n'));

  const auto quoteContentOffset = [](QStringView line, int depth) -> int {
    int index = 0;
    for (int currentDepth = 0; currentDepth < depth; ++currentDepth) {
      while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
        ++index;
      }
      if (index >= line.size() || line.at(index) != QLatin1Char('>')) {
        return -1;
      }
      ++index;
      if (index < line.size() && line.at(index) == QLatin1Char(' ')) {
        ++index;
      }
    }
    return index;
  };

  const auto lineStartOffset = [&text](int line) -> qsizetype {
    qsizetype offset = 0;
    for (int currentLine = 1; currentLine < line; ++currentLine) {
      const qsizetype newline = text.indexOf(QLatin1Char('\n'), offset);
      if (newline < 0) {
        return -1;
      }
      offset = newline + 1;
    }
    return offset;
  };

  const auto isEmptyQuoteLine = [&](int line, int depth, int& contentColumn, qsizetype& contentOffset) {
    const int lineIndex = line - 1;
    if (lineIndex < 0 || lineIndex >= lines.size()) {
      return false;
    }
    const QString& sourceLine = lines.at(lineIndex);
    const int contentIndex = quoteContentOffset(sourceLine, depth);
    if (contentIndex < 0 || !QStringView(sourceLine).mid(contentIndex).trimmed().isEmpty()) {
      return false;
    }
    const qsizetype startOffset = lineStartOffset(line);
    if (startOffset < 0) {
      return false;
    }
    contentColumn = contentIndex + 1;
    contentOffset = startOffset + contentIndex;
    return true;
  };

  const auto quoteDepth = [](const MarkdownNode& node) {
    int depth = 0;
    for (const MarkdownNode* current = &node; current; current = current->parent()) {
      if (current->type() == BlockType::BlockQuote) {
        ++depth;
      }
    }
    return depth;
  };

  const auto visit = [&](const auto& self, MarkdownNode& node) -> void {
    for (const auto& child : node.children()) {
      self(self, *child);
    }
    if (node.type() != BlockType::BlockQuote || node.children().empty()) {
      return;
    }

    const int depth = quoteDepth(node);
    qsizetype childIndex = 0;
    int previousEndLine = node.sourceRange().lineStart - 1;
    while (childIndex < static_cast<qsizetype>(node.children().size())) {
      MarkdownNode* child = node.children().at(static_cast<size_t>(childIndex)).get();
      const SourceRange range = child->sourceRange();
      const int startLine = range.lineStart;
      const int blankLines = startLine > 0 ? startLine - previousEndLine - 1 : 0;
      const int emptyCount = qMax(0, blankLines / 2);
      const int firstEmptyLine = previousEndLine + 2;
      for (int i = 0; i < emptyCount; ++i) {
        const int emptyLine = firstEmptyLine + i * 2;
        int contentColumn = 1;
        qsizetype contentOffset = -1;
        if (isEmptyQuoteLine(emptyLine, depth, contentColumn, contentOffset)) {
          node.insertChild(childIndex, createVirtualEmptyParagraph(emptyLine, contentColumn, contentOffset));
          ++childIndex;
        }
      }

      previousEndLine = qMax(previousEndLine, range.lineEnd);
      ++childIndex;
    }

    const int trailingLines = node.sourceRange().lineEnd - previousEndLine;
    const int trailingEmptyCount = qMax(0, trailingLines / 2);
    for (int i = 0; i < trailingEmptyCount; ++i) {
      const int emptyLine = previousEndLine + 2 + i * 2;
      int contentColumn = 1;
      qsizetype contentOffset = -1;
      if (isEmptyQuoteLine(emptyLine, depth, contentColumn, contentOffset)) {
        node.appendChild(createVirtualEmptyParagraph(emptyLine, contentColumn, contentOffset));
      }
    }
  };

  visit(visit, root);
}

std::unique_ptr<MarkdownNode> CmarkGfmParser::createVirtualEmptyParagraph(int line) const {
  return createVirtualEmptyParagraph(line, 1, 0);
}

std::unique_ptr<MarkdownNode> CmarkGfmParser::createVirtualEmptyParagraph(int line, int column, qsizetype sourceOffset) const {
  auto paragraph = std::make_unique<MarkdownNode>(BlockType::Paragraph);
  paragraph->inlines().push_back(InlineNode::text(QString()));

  SourceRange range;
  range.lineStart = line;
  range.lineEnd = line;
  range.columnStart = column;
  range.columnEnd = column;
  range.byteStart = sourceOffset;
  range.byteEnd = sourceOffset;
  paragraph->setSourceRange(range);
  return paragraph;
}

}  // namespace muffin
