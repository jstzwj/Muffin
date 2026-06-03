#pragma once

#include "document/InlineProjection.h"
#include "document/MarkdownTypes.h"
#include "document/NodeId.h"
#include "document/SourceRange.h"

#include <QString>

namespace muffin {

class DocumentSession;
class MarkdownNode;
class SelectionController;

struct BlockEditContext {
  MarkdownNode* node = nullptr;
  MarkdownNode* editableNode = nullptr;
  NodeId blockId;
  BlockType blockType = BlockType::Unknown;
  SourceRange blockRange;
  SourceRange contentRange;
  qsizetype cursorTextOffset = 0;
  qsizetype cursorSourceOffset = -1;
  QString contentText;
  QString visibleText;
  InlineProjection inlineProjection;
  bool plainInlineEditable = false;
  bool supportsVisibleOffsetMapping = false;
};

class BlockEditContextResolver {
public:
  BlockEditContextResolver(DocumentSession* session, SelectionController* selection);

  bool current(BlockEditContext& context) const;
  bool forBlock(NodeId blockId, BlockEditContext& context) const;
  bool fill(MarkdownNode& displayNode, BlockEditContext& context) const;
  bool selectionContext(BlockEditContext& context, qsizetype& start, qsizetype& end) const;
  bool selectionSourceRange(qsizetype& start, qsizetype& end) const;
  bool blockSourceRange(const MarkdownNode& node, qsizetype& start, qsizetype& end) const;
  bool listItemLineBounds(const BlockEditContext& context, qsizetype& lineStart, qsizetype& contentStart, qsizetype& lineEnd) const;

  QString listMarkerFor(const QString& line) const;
  MarkdownNode* previousEditableTextBlock(const MarkdownNode& node, BlockEditContext& context) const;
  MarkdownNode* nextEditableTextBlock(const MarkdownNode& node, BlockEditContext& context) const;
  MarkdownNode* nodeAtContentSourceOffset(MarkdownNode& node, qsizetype sourceOffset, bool preferLaterEmptyAtOffset = false) const;

  qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column) const;
  qsizetype sourceOffsetForLineEnd(const QString& text, int line) const;

private:
  MarkdownNode* lastEditableDescendant(MarkdownNode& node) const;
  MarkdownNode* firstEditableDescendant(MarkdownNode& node) const;
  MarkdownNode* primaryParagraph(MarkdownNode& node) const;

  DocumentSession* session_ = nullptr;
  SelectionController* selection_ = nullptr;
};

}  // namespace muffin
