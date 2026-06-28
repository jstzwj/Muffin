#pragma once

#include "document/MarkdownNode.h"
#include "document/SourceRange.h"
#include "document/MarkdownTypes.h"

#include <QString>

namespace muffin {

struct ListLineInfo {
  bool valid = false;
  bool ordered = false;
  bool task = false;
  bool taskChecked = false;
  qsizetype markerStart = -1;
  qsizetype markerEnd = -1;
  qsizetype contentStart = -1;
  qsizetype taskMarkerStart = -1;
  qsizetype taskMarkerEnd = -1;
  qsizetype taskContentStart = -1;
  QString marker;
  QChar orderedDelimiter = QLatin1Char('.');
  int orderedNumber = 0;
};

// Full byte span of a block, snapping indented-code starts back to their line start. Templated so
// it reads the document text from either a QString (command/edit path) or a PieceTable (zero-copy);
// body only uses size() and lineStartOffset(), both of which work on either.
template <typename Text>
SourceRange fullBlockSourceRange(const MarkdownNode& node, const Text& markdown) {
  SourceRange range = node.sourceRange();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > markdown.size()) {
    return range;
  }
  // cmark reports indented-code content starting past its 4-space indent, so extend the
  // range back to the line start so serialization (which re-applies the indent) replaces the
  // whole block rather than double-indenting the first line.
  if (node.type() == BlockType::CodeFence && node.isIndentedCode() && range.byteStart > 0) {
    range.byteStart = lineStartOffset(markdown, range.byteStart);
  }
  return range;
}

QString mathOpeningDelimiter(const MarkdownNode& node);
QString mathClosingDelimiter(const MarkdownNode& node);
QString mathOpeningDelimiter(MathDelimiter delimiter);
QString mathClosingDelimiter(MathDelimiter delimiter);
bool isMathClosingLine(const MarkdownNode& node, QStringView line);

// Line/column -> byte-offset helpers, templated so they read the document text from either a
// QString (parse/command/edit path) or a PieceTable (render builder path) -- both expose
// indexOf(QChar, from) and size(). Definitions live here (not SourceRangeUtil.cpp) because they
// must be visible at every instantiation site.
template <typename Text>
qsizetype sourceOffsetForLineColumn(const Text& text, int line, int column) {
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

template <typename Text>
qsizetype sourceOffsetForLineEnd(const Text& text, int line) {
  if (line <= 0) {
    return -1;
  }
  const qsizetype offset = sourceOffsetForLineColumn(text, line, 1);
  if (offset < 0) {
    return -1;
  }
  const qsizetype newline = text.indexOf(QLatin1Char('\n'), offset);
  return newline < 0 ? text.size() : newline;
}

// Byte offset of the first character on the line that contains `offset`. cmark reports indented
// code content starting past its 4-space indent, so a range or re-parse slice rooted at that
// content offset would lose the indentation and collapse back to paragraphs; snapping to the line
// boundary keeps block-leading whitespace inside the range/slice.
// Templated so it reads the document text from either a QString (parse path) or a PieceTable
// (edit path) — both expose at()/size() and are indexed by QChar.
template <typename Text>
qsizetype lineStartOffset(const Text& text, qsizetype offset) {
  const qsizetype bounded = qBound<qsizetype>(0, offset, text.size());
  for (qsizetype i = bounded; i > 0; --i) {
    if (text.at(i - 1) == QLatin1Char('\n')) {
      return i;
    }
  }
  return 0;
}

// Plain byte span of a block's first..last line: start = column 1 of the first line, end = end of
// the last line. Unlike fullBlockSourceRange this does NOT extend past a math block's closing
// "$$"/"\]" — callers that want an insertion *point* right after the block (rather than a
// whole-block replacement range) use this so the inserted paragraph lands after the last line
// instead of swallowing the closing delimiter. Returns byteStart < 0 when the line span is unusable.
// Templated so it reads the document text from either a QString or a PieceTable (zero-copy).
template <typename Text>
SourceRange blockLineSpan(const MarkdownNode& node, const Text& markdown) {
  SourceRange span;
  span.byteStart = -1;
  span.byteEnd = -1;
  const SourceRange range = node.sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return span;
  }
  span.byteStart = sourceOffsetForLineColumn(markdown, range.lineStart, 1);
  span.byteEnd = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (span.byteStart < 0 || span.byteEnd < span.byteStart) {
    span.byteStart = -1;
    span.byteEnd = -1;
  }
  return span;
}

// Past-the-end byte offset of a heading's editable content. For a Setext heading
// the content is the text line only; the `===`/`---` underline line belongs to the
// block's construct span (kept intact in sourceRange for structural consumers) and
// must not leak into the rendered/edited content. ATX headings fall through to the
// usual byte/line end.
// Templated so it reads the document text from either a QString (edit/command path) or a
// PieceTable (render builder path) -- both expose the indexOf/size that sourceOffsetForLineEnd uses.
template <typename Text>
qsizetype headingContentEndOffset(const MarkdownNode& node, const Text& markdown) {
  const SourceRange range = node.sourceRange();
  if (node.setext() && range.lineStart > 0) {
    // Setext: editable content is the text line only; stop before the underline line.
    return sourceOffsetForLineEnd(markdown, range.lineStart);
  }
  return range.byteEnd > range.byteStart ? range.byteEnd : sourceOffsetForLineEnd(markdown, range.lineEnd);
}

MarkdownNode* primaryParagraph(MarkdownNode& node);
const MarkdownNode* primaryParagraph(const MarkdownNode& node);

QString listMarkerFor(const QString& line);
ListLineInfo listLineInfoFor(const QString& line);

}  // namespace muffin
