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
