#include "parser/CmarkGfmParser.h"

#include "document/InlineNode.h"
#include "document/LineStartOffsetCache.h"
#include "parser/CmarkNodeAdapter.h"

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

void annotateSourceOffsets(const LineStartOffsetCache& lineOffsets, MarkdownNode& node) {
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

  for (const auto& child : node.children()) {
    annotateSourceOffsets(lineOffsets, *child);
  }
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
  const QByteArray utf8 = markdownToParse.toString().toUtf8();
  cmark_parser* parser = cmark_parser_new(CMARK_OPT_DEFAULT | CMARK_OPT_FOOTNOTES);
  attachExtensions(parser, options);
  cmark_parser_feed(parser, utf8.constData(), static_cast<size_t>(utf8.size()));
  cmark_node* document = cmark_parser_finish(parser);

  CmarkNodeAdapter adapter;
  ParseResult result;
  result.root = adapter.convertBlock(document);
  const LineStartOffsetCache lineOffsets(markdownToParse);
  insertVirtualEmptyParagraphs(markdownToParse, *result.root);
  annotateSourceOffsets(lineOffsets, *result.root);
  annotateTableCellSourceRanges(markdownToParse, lineOffsets, *result.root);

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
  const int trailingLines = totalLines - previousEndLine;
  const int trailingEmptyCount = qMax(0, trailingLines / 2);
  for (int i = 0; i < trailingEmptyCount; ++i) {
    const int emptyLine = previousEndLine + 2 + i * 2;
    const int lineIndex = emptyLine - 1;
    if (lineIndex >= 0 && lineIndex < lines.size() && lines.at(lineIndex).trimmed().isEmpty()) {
      root.appendChild(createVirtualEmptyParagraph(emptyLine));
    }
  }
}

std::unique_ptr<MarkdownNode> CmarkGfmParser::createVirtualEmptyParagraph(int line) const {
  auto paragraph = std::make_unique<MarkdownNode>(BlockType::Paragraph);
  paragraph->inlines().push_back(InlineNode::text(QString()));

  SourceRange range;
  range.lineStart = line;
  range.lineEnd = line;
  range.columnStart = 1;
  range.columnEnd = 1;
  range.byteStart = 0;
  range.byteEnd = 0;
  paragraph->setSourceRange(range);
  return paragraph;
}

}  // namespace muffin
