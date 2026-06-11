#pragma once

#include "document/SourceRange.h"

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

qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column);
qsizetype sourceOffsetForLineEnd(const QString& text, int line);

MarkdownNode* primaryParagraph(MarkdownNode& node);
const MarkdownNode* primaryParagraph(const MarkdownNode& node);

QString listMarkerFor(const QString& line);
ListLineInfo listLineInfoFor(const QString& line);

}  // namespace muffin
