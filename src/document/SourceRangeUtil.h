#pragma once

#include "document/SourceRange.h"
#include "document/MarkdownTypes.h"

#include <QString>

namespace muffin {

class MarkdownNode;

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

SourceRange fullBlockSourceRange(const MarkdownNode& node, const QString& markdown);
QString mathOpeningDelimiter(const MarkdownNode& node);
QString mathClosingDelimiter(const MarkdownNode& node);
QString mathOpeningDelimiter(MathDelimiter delimiter);
QString mathClosingDelimiter(MathDelimiter delimiter);
bool isMathClosingLine(const MarkdownNode& node, QStringView line);

qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column);
qsizetype sourceOffsetForLineEnd(const QString& text, int line);

// Byte offset of the first character on the line that contains `offset`. cmark reports indented
// code content starting past its 4-space indent, so a range or re-parse slice rooted at that
// content offset would lose the indentation and collapse back to paragraphs; snapping to the line
// boundary keeps block-leading whitespace inside the range/slice.
qsizetype lineStartOffset(const QString& text, qsizetype offset);

// Plain byte span of a block's first..last line: start = column 1 of the first line, end = end of
// the last line. Unlike fullBlockSourceRange this does NOT extend past a math block's closing
// "$$"/"\]" — callers that want an insertion *point* right after the block (rather than a
// whole-block replacement range) use this so the inserted paragraph lands after the last line
// instead of swallowing the closing delimiter. Returns byteStart < 0 when the line span is unusable.
SourceRange blockLineSpan(const MarkdownNode& node, const QString& markdown);

// Past-the-end byte offset of a heading's editable content. For a Setext heading
// the content is the text line only; the `===`/`---` underline line belongs to the
// block's construct span (kept intact in sourceRange for structural consumers) and
// must not leak into the rendered/edited content. ATX headings fall through to the
// usual byte/line end.
qsizetype headingContentEndOffset(const MarkdownNode& node, const QString& markdown);

MarkdownNode* primaryParagraph(MarkdownNode& node);
const MarkdownNode* primaryParagraph(const MarkdownNode& node);

QString listMarkerFor(const QString& line);
ListLineInfo listLineInfoFor(const QString& line);

}  // namespace muffin
