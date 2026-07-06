#pragma once

#include "document/DefinitionBlock.h"
#include "document/MarkdownNode.h"

#include <QString>

namespace muffin {

// Literal blocks edit through dedicated controllers (code/math/HTML/front matter) rather than the
// inline-text model, so the caret is anchored as an offset into the block literal. Shared by the
// render and editor subsystems (previously duplicated verbatim in BlockLayout/InputController/
// BlockEditContext).
inline bool isLiteralBlockType(BlockType type) {
  return type == BlockType::CodeFence || type == BlockType::MathBlock ||
         type == BlockType::HtmlBlock || type == BlockType::FrontMatter;
}

// Inline-editable text blocks (the caret is anchored as a visible/source offset into the block's
// projected inline content). Includes ListItem (whose content is its primaryParagraph) and the
// definition blocks. Shared by the editor subsystem (BlockEditContext, InputController).
inline bool isEditableTextBlock(BlockType type) {
  return type == BlockType::Paragraph || type == BlockType::Heading || type == BlockType::ListItem ||
         type == BlockType::TableCell || type == BlockType::LinkDefinition || type == BlockType::FootnoteDefinition;
}

// A paragraph may begin with up to 3 spaces of CommonMark indentation; the AST start lands after
// the indent, but the editable content begins at the line start. Walk back over up to 3 leading
// spaces to find that line start, and fall back to astStart when the indent does not reach the line
// start — so this never over-selects content sitting after a partial indent.
template <typename Text>
inline qsizetype paragraphContentStartIncludingCommonMarkIndent(const Text& markdown, qsizetype astStart) {
  qsizetype lineStart = astStart;
  while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  qsizetype start = astStart;
  while (start > lineStart && astStart - start < 3 && markdown.at(start - 1) == QLatin1Char(' ')) {
    --start;
  }
  return start == lineStart ? start : astStart;
}

// Selectable length for a link/footnote definition: the span from the marker start to the farthest
// end of the definition's component ranges. Shared by the two selectableTextLength variants (they
// agree on every case except their inline-leaf handling — TableCell vs ListItem — so this isolates
// the one piece of logic that is genuinely identical). Returns 0 when the marker is absent/invalid.
inline qsizetype definitionSelectableLength(const MarkdownNode& node) {
  const DefinitionBlock definition = node.definition();
  if (!definition.markerRange.isValid()) {
    return 0;
  }
  const qsizetype end = definition.sourceRange.isValid()
                            ? definition.sourceRange.end
                            : qMax(definition.markerRange.end,
                                   qMax(definition.destinationRange.end,
                                        qMax(definition.titleRange.end, definition.noteRange.end)));
  return qMax<qsizetype>(0, end - definition.markerRange.start);
}

}  // namespace muffin
