#pragma once

#include "document/SourceRange.h"

#include <QString>

namespace muffin {

class MarkdownNode;

SourceRange fullBlockSourceRange(const MarkdownNode& node, const QString& markdown);

qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column);
qsizetype sourceOffsetForLineEnd(const QString& text, int line);

MarkdownNode* primaryParagraph(MarkdownNode& node);
const MarkdownNode* primaryParagraph(const MarkdownNode& node);

QString listMarkerFor(const QString& line);

}  // namespace muffin
