#pragma once

#include "Export.h"
#include <QString>

namespace muffin {
class MarkdownNode;
class PieceTable;
// A literal block can be valid at EOF without a closing delimiter. Structural
// operations must terminate it before introducing another sibling. The suffix
// belongs at the end of its last physical line, before separator newlines.
MUFFIN_CORE_EXPORT QString blockClosingSuffix(const PieceTable& text, const MarkdownNode& node);
}
