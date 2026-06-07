#pragma once

#include "document/SourceRange.h"

#include <QString>

namespace muffin {

class MarkdownNode;

SourceRange fullBlockSourceRange(const MarkdownNode& node, const QString& markdown);

}  // namespace muffin
