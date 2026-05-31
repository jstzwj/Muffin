#include "parser/CmarkNodeAdapter.h"

#include <QString>

extern "C" {
#include "cmark-gfm-core-extensions.h"
#include "strikethrough.h"
#include "table.h"
#include "math_extension.h"
}

namespace muffin {
namespace {

QString fromUtf8(const char* value) {
  return value ? QString::fromUtf8(value) : QString();
}

TableAlignment tableAlignmentFromCmark(uint8_t value) {
  switch (value) {
    case 'l':
      return TableAlignment::Left;
    case 'c':
      return TableAlignment::Center;
    case 'r':
      return TableAlignment::Right;
    default:
      return TableAlignment::None;
  }
}

bool isBlockType(cmark_node_type type) {
  return (type & CMARK_NODE_TYPE_MASK) == CMARK_NODE_TYPE_BLOCK;
}

}  // namespace

std::unique_ptr<MarkdownNode> CmarkNodeAdapter::convertBlock(cmark_node* node) {
  auto result = std::make_unique<MarkdownNode>(mapBlockType(node));
  result->setSourceRange(readSourceRange(node));
  readBlockMetadata(node, *result);

  if (result->type() == BlockType::Paragraph || result->type() == BlockType::Heading ||
      result->type() == BlockType::TableCell) {
    result->inlines() = convertInlineChildren(node);
  }

  for (cmark_node* child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
    const auto type = cmark_node_get_type(child);
    if (isBlockType(type)) {
      result->appendChild(convertBlock(child));
    }
  }

  return result;
}

InlineNode CmarkNodeAdapter::convertInline(cmark_node* node) {
  switch (mapInlineType(node)) {
    case InlineType::Text:
      return InlineNode::text(fromUtf8(cmark_node_get_literal(node)));
    case InlineType::SoftBreak:
      return InlineNode::softBreak();
    case InlineType::LineBreak:
      return InlineNode::lineBreak();
    case InlineType::Code:
      return InlineNode::code(fromUtf8(cmark_node_get_literal(node)));
    case InlineType::HtmlInline: {
      InlineNode inlineNode(InlineType::HtmlInline);
      inlineNode.setText(fromUtf8(cmark_node_get_literal(node)));
      return inlineNode;
    }
    case InlineType::Emphasis:
      return InlineNode::emphasis(QStringLiteral("*"), convertInlineChildren(node));
    case InlineType::Strong:
      return InlineNode::strong(QStringLiteral("**"), convertInlineChildren(node));
    case InlineType::Strikethrough:
      return InlineNode::strikethrough(QStringLiteral("~~"), convertInlineChildren(node));
    case InlineType::InlineMath:
      return InlineNode::inlineMath(fromUtf8(cmark_node_get_string_content(node)));
    case InlineType::Link:
      return InlineNode::link(
          fromUtf8(cmark_node_get_url(node)),
          fromUtf8(cmark_node_get_title(node)),
          convertInlineChildren(node));
    case InlineType::Image: {
      const auto children = convertInlineChildren(node);
      QString alt;
      for (const auto& child : children) {
        alt += child.text();
      }
      return InlineNode::image(
          fromUtf8(cmark_node_get_url(node)),
          alt,
          fromUtf8(cmark_node_get_title(node)));
    }
    default: {
      InlineNode unknown(InlineType::Unknown);
      unknown.setText(fromUtf8(cmark_node_get_literal(node)));
      return unknown;
    }
  }
}

BlockType CmarkNodeAdapter::mapBlockType(cmark_node* node) const {
  const auto type = cmark_node_get_type(node);
  if (type == CMARK_NODE_DOCUMENT) return BlockType::Document;
  if (type == CMARK_NODE_PARAGRAPH) return BlockType::Paragraph;
  if (type == CMARK_NODE_HEADING) return BlockType::Heading;
  if (type == CMARK_NODE_BLOCK_QUOTE) return BlockType::BlockQuote;
  if (type == CMARK_NODE_LIST) return BlockType::List;
  if (type == CMARK_NODE_ITEM) return BlockType::ListItem;
  if (type == CMARK_NODE_THEMATIC_BREAK) return BlockType::ThematicBreak;
  if (type == CMARK_NODE_CODE_BLOCK) return BlockType::CodeFence;
  if (type == CMARK_NODE_HTML_BLOCK) return BlockType::HtmlBlock;
  if (type == CMARK_NODE_FOOTNOTE_DEFINITION) return BlockType::FootnoteDefinition;
  if (type == CMARK_NODE_TABLE) return BlockType::Table;
  if (type == CMARK_NODE_TABLE_ROW) return BlockType::TableRow;
  if (type == CMARK_NODE_TABLE_CELL) return BlockType::TableCell;
  if (type == CMARK_NODE_MATH_BLOCK) return BlockType::MathBlock;
  return BlockType::Unknown;
}

InlineType CmarkNodeAdapter::mapInlineType(cmark_node* node) const {
  const auto type = cmark_node_get_type(node);
  if (type == CMARK_NODE_TEXT) return InlineType::Text;
  if (type == CMARK_NODE_SOFTBREAK) return InlineType::SoftBreak;
  if (type == CMARK_NODE_LINEBREAK) return InlineType::LineBreak;
  if (type == CMARK_NODE_CODE) return InlineType::Code;
  if (type == CMARK_NODE_HTML_INLINE) return InlineType::HtmlInline;
  if (type == CMARK_NODE_EMPH) return InlineType::Emphasis;
  if (type == CMARK_NODE_STRONG) return InlineType::Strong;
  if (type == CMARK_NODE_LINK) return InlineType::Link;
  if (type == CMARK_NODE_IMAGE) return InlineType::Image;
  if (type == CMARK_NODE_STRIKETHROUGH) return InlineType::Strikethrough;
  if (type == CMARK_NODE_INLINE_MATH) return InlineType::InlineMath;
  return InlineType::Unknown;
}

SourceRange CmarkNodeAdapter::readSourceRange(cmark_node* node) const {
  SourceRange range;
  range.lineStart = cmark_node_get_start_line(node);
  range.lineEnd = cmark_node_get_end_line(node);
  range.columnStart = cmark_node_get_start_column(node);
  range.columnEnd = cmark_node_get_end_column(node);
  return range;
}

QVector<InlineNode> CmarkNodeAdapter::convertInlineChildren(cmark_node* node) {
  QVector<InlineNode> children;
  for (cmark_node* child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
    if (!isBlockType(cmark_node_get_type(child))) {
      children.push_back(convertInline(child));
    }
  }
  return children;
}

void CmarkNodeAdapter::readBlockMetadata(cmark_node* cmarkNode, MarkdownNode& muffinNode) {
  switch (muffinNode.type()) {
    case BlockType::Heading:
      muffinNode.setHeadingLevel(cmark_node_get_heading_level(cmarkNode));
      break;
    case BlockType::List:
      muffinNode.setListKind(cmark_node_get_list_type(cmarkNode) == CMARK_ORDERED_LIST
                                 ? ListKind::Ordered
                                 : ListKind::Bullet);
      muffinNode.setListStart(cmark_node_get_list_start(cmarkNode));
      muffinNode.setListTight(cmark_node_get_list_tight(cmarkNode) != 0);
      break;
    case BlockType::CodeFence:
      muffinNode.setLiteral(fromUtf8(cmark_node_get_literal(cmarkNode)));
      muffinNode.setCodeLanguage(fromUtf8(cmark_node_get_fence_info(cmarkNode)).section(' ', 0, 0));
      break;
    case BlockType::HtmlBlock:
      muffinNode.setLiteral(fromUtf8(cmark_node_get_literal(cmarkNode)));
      break;
    case BlockType::MathBlock:
      muffinNode.setLiteral(fromUtf8(cmark_node_get_string_content(cmarkNode)).trimmed());
      break;
    case BlockType::Table:
      readTableMetadata(cmarkNode, muffinNode);
      break;
    case BlockType::TableRow:
      muffinNode.setTableRowIsHeader(cmark_gfm_extensions_get_table_row_is_header(cmarkNode) != 0);
      break;
    default:
      break;
  }
}

void CmarkNodeAdapter::readTableMetadata(cmark_node* cmarkNode, MarkdownNode& muffinNode) {
  const auto columns = cmark_gfm_extensions_get_table_columns(cmarkNode);
  const auto* alignments = cmark_gfm_extensions_get_table_alignments(cmarkNode);
  QVector<TableAlignment> converted;
  converted.reserve(columns);
  for (uint16_t i = 0; i < columns; ++i) {
    converted.push_back(tableAlignmentFromCmark(alignments ? alignments[i] : 0));
  }
  muffinNode.setTableAlignments(std::move(converted));
}

}  // namespace muffin
