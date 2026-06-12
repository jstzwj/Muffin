#include "parser/CmarkNodeAdapter.h"

#include "document/InlineNode.h"
#include "document/LineStartOffsetCache.h"

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

bool isAutolinkInline(const InlineNode& node, const QString& label) {
  return node.type() == InlineType::Link && node.title().isEmpty() &&
         (label == node.href() || QStringLiteral("http://%1").arg(label) == node.href() ||
          QStringLiteral("mailto:%1").arg(label) == node.href());
}

QString markdownForInlineLabel(const QVector<InlineNode>& inlines) {
  QString label;
  for (const InlineNode& child : inlines) {
    switch (child.type()) {
      case InlineType::Text:
      case InlineType::Code:
      case InlineType::InlineMath:
      case InlineType::HtmlInline:
        label += child.text();
        break;
      case InlineType::SoftBreak:
        label += QLatin1Char('\n');
        break;
      case InlineType::LineBreak:
        label += QStringLiteral("  \n");
        break;
      default:
        label += markdownForInlineLabel(child.children());
        break;
    }
  }
  return label;
}

InlineRange inlineRange(qsizetype start, qsizetype end) {
  return InlineRange{start, end};
}

void setPlainInlineRanges(InlineSourceRanges& ranges, qsizetype start, qsizetype end) {
  ranges.source = inlineRange(start, end);
  ranges.content = ranges.source;
}

void clampInlineRangeEnd(InlineRange& range, qsizetype end) {
  if (!range.isValid() || end < 0) {
    return;
  }
  range.end = qMin(range.end, end);
  if (range.start > range.end) {
    range.start = range.end;
  }
}

void clampInlineRangesEnd(InlineNode& node, qsizetype end) {
  InlineSourceRanges ranges = node.sourceRanges();
  clampInlineRangeEnd(ranges.source, end);
  clampInlineRangeEnd(ranges.content, end);
  clampInlineRangeEnd(ranges.openMarker, end);
  clampInlineRangeEnd(ranges.closeMarker, end);
  node.setSourceRanges(ranges);
}

void clampInlineRangeStart(InlineRange& range, qsizetype start) {
  if (!range.isValid() || start < 0) {
    return;
  }
  range.start = qMax(range.start, start);
  if (range.end < range.start) {
    range.end = range.start;
  }
}

void clampInlineRangesStart(InlineNode& node, qsizetype start) {
  InlineSourceRanges ranges = node.sourceRanges();
  clampInlineRangeStart(ranges.source, start);
  clampInlineRangeStart(ranges.content, start);
  clampInlineRangeStart(ranges.openMarker, start);
  clampInlineRangeStart(ranges.closeMarker, start);
  node.setSourceRanges(ranges);
}

}  // namespace

CmarkNodeAdapter::CmarkNodeAdapter(const LineStartOffsetCache* lineOffsets, QStringView markdown)
    : lineOffsets_(lineOffsets), markdown_(markdown) {}

std::unique_ptr<MarkdownNode> CmarkNodeAdapter::convertBlock(cmark_node* node) {
  auto result = std::make_unique<MarkdownNode>(mapBlockType(node));
  result->setSourceRange(readSourceRange(node));
  readBlockMetadata(node, *result);

  if (result->type() == BlockType::Paragraph || result->type() == BlockType::Heading ||
      result->type() == BlockType::TableCell) {
    result->inlines() = convertInlineChildren(node);
  }

  // Compute byte-level source range for block types whose editing code
  // needs byteStart/byteEnd to resolve cell content or inline offsets.
  if (lineOffsets_ && !markdown_.isEmpty()) {
    const SourceRange srcRange = result->sourceRange();
    if (srcRange.lineStart > 0 && result->sourceRange().byteEnd <= result->sourceRange().byteStart) {
      const qsizetype start = lineOffsets_->offsetForLineByteColumn(srcRange.lineStart, qMax(1, srcRange.columnStart));
      const qsizetype end = lineOffsets_->offsetForLineByteColumn(srcRange.lineEnd, qMax(1, srcRange.columnEnd + 1));
      if (start >= 0 && end >= start && end <= markdown_.size()) {
        SourceRange updated = srcRange;
        updated.byteStart = start;
        updated.byteEnd = end;
        result->setSourceRange(updated);
      }
    }
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
  InlineNode result = [this, node]() -> InlineNode {
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
  }();

  annotateInlineSource(node, result);
  return result;
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

void CmarkNodeAdapter::annotateInlineSource(cmark_node* cmarkNode, InlineNode& inlineNode) const {
  if (!lineOffsets_ || markdown_.isEmpty()) {
    return;
  }
  const SourceRange range = readSourceRange(cmarkNode);
  if (range.lineStart <= 0) {
    return;
  }
  const qsizetype start = lineOffsets_->offsetForLineByteColumn(range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = lineOffsets_->offsetForLineByteColumn(range.lineEnd, qMax(1, range.columnEnd + 1));
  if (start < 0 || end < start || end > markdown_.size()) {
    return;
  }

  InlineSourceRanges ranges;
  const InlineType type = inlineNode.type();
  switch (type) {
    case InlineType::Text:
    case InlineType::HtmlInline: {
      qsizetype textStart = start;
      qsizetype textEnd = end;
      // cmark-gfm shifts the start of a text node right past any backtick it scanned as a failed
      // code-span opener, but keeps the (correct) end. The range is then too short for the literal
      // (zero-width when the whole node is a single backtick), and the projection painted the
      // backtick twice — once as the "gap" before the node, once as the node's own text. Re-anchor
      // the start on the end so the literal maps 1:1. Entities are unaffected: their decoded text
      // is shorter than the raw source slice, so the guard below never fires for them.
      const QString literal = inlineNode.text();
      if (!literal.isEmpty() && literal.size() > textEnd - textStart) {
        const qsizetype anchoredStart = textEnd - literal.size();
        if (anchoredStart >= 0 && anchoredStart + literal.size() <= markdown_.size() &&
            markdown_.mid(anchoredStart, literal.size()) == literal) {
          textStart = anchoredStart;
        }
      }
      setPlainInlineRanges(ranges, textStart, textEnd);
      break;
    }
    case InlineType::SoftBreak:
      if (start < markdown_.size() && markdown_.at(start) == QLatin1Char('\n')) {
        setPlainInlineRanges(ranges, start, start + 1);
      }
      break;
    case InlineType::LineBreak:
      if (start + 3 <= markdown_.size() && markdown_.mid(start, 3) == QStringLiteral("  \n")) {
        setPlainInlineRanges(ranges, start, start + 3);
      }
      break;
    case InlineType::Code: {
      qsizetype sourceStart = start;
      qsizetype sourceEnd = end;
      if (sourceStart < sourceEnd && markdown_.at(sourceStart) != QLatin1Char('`')) {
        while (sourceStart > 0 && markdown_.at(sourceStart - 1) == QLatin1Char('`')) {
          --sourceStart;
        }
        while (sourceEnd < markdown_.size() && markdown_.at(sourceEnd) == QLatin1Char('`')) {
          ++sourceEnd;
        }
      }
      const qsizetype openLength = countLeading(markdown_, sourceStart, sourceEnd, QLatin1Char('`'));
      const qsizetype closeLength = countTrailing(markdown_, sourceStart, sourceEnd, QLatin1Char('`'));
      if (openLength > 0 && closeLength >= openLength) {
        ranges.source = inlineRange(sourceStart, sourceEnd);
        ranges.openMarker = inlineRange(sourceStart, sourceStart + openLength);
        ranges.closeMarker = inlineRange(sourceEnd - closeLength, sourceEnd);
        ranges.content = inlineRange(ranges.openMarker.end, ranges.closeMarker.start);
      }
      break;
    }
    case InlineType::InlineMath: {
      qsizetype sourceStart = start;
      qsizetype sourceEnd = end;
      if (sourceStart < sourceEnd && markdown_.at(sourceStart) != QLatin1Char('$')) {
        while (sourceStart > 0 && markdown_.at(sourceStart - 1) == QLatin1Char('$')) {
          --sourceStart;
        }
        while (sourceEnd < markdown_.size() && markdown_.at(sourceEnd) == QLatin1Char('$')) {
          ++sourceEnd;
        }
      }
      const qsizetype openLength = countLeading(markdown_, sourceStart, sourceEnd, QLatin1Char('$'));
      const qsizetype closeLength = countTrailing(markdown_, sourceStart, sourceEnd, QLatin1Char('$'));
      if (openLength > 0 && closeLength >= openLength) {
        ranges.source = inlineRange(sourceStart, sourceEnd);
        ranges.openMarker = inlineRange(sourceStart, sourceStart + openLength);
        ranges.closeMarker = inlineRange(sourceEnd - closeLength, sourceEnd);
        ranges.content = inlineRange(ranges.openMarker.end, ranges.closeMarker.start);
      }
      break;
    }
    case InlineType::Emphasis:
    case InlineType::Strong:
    case InlineType::Strikethrough: {
      const QString marker = inlineNode.marker();
      if (!marker.isEmpty() && end - start >= marker.size() * 2) {
        ranges.source = inlineRange(start, end);
        ranges.openMarker = inlineRange(start, start + marker.size());
        ranges.closeMarker = inlineRange(end - marker.size(), end);
        ranges.content = inlineRange(ranges.openMarker.end, ranges.closeMarker.start);
      } else {
        setPlainInlineRanges(ranges, start, end);
      }
      break;
    }
    case InlineType::Link: {
      // cmark-gfm produces the same CMARK_NODE_LINK for both autolinks
      // (<url>) and regular links ([text](url)). Distinguish them by
      // inspecting the source text: a regular link starts with '['.
      const QString label = markdownForInlineLabel(inlineNode.children());
      const bool isAutolink = isAutolinkInline(inlineNode, label) &&
          !(start < markdown_.size() && markdown_.at(start) == QLatin1Char('['));
      inlineNode.setAutolink(isAutolink);
      if (isAutolink) {
        const qsizetype searchStart = qMax<qsizetype>(0, start);
        const qsizetype labelStart = label.isEmpty() ? qsizetype(-1) : markdown_.indexOf(label, searchStart);
        if (labelStart >= 0 && labelStart <= end &&
            labelStart + label.size() >= start && labelStart + label.size() <= markdown_.size()) {
          setPlainInlineRanges(ranges, labelStart, labelStart + label.size());
        }
      } else {
        ranges.source = inlineRange(start, end);
        ranges.openMarker = inlineRange(start, qMin(end, start + 1));
        const auto source = markdown_.mid(start, end - start);
        const qsizetype labelEnd = source.indexOf(QLatin1Char(']'));
        ranges.content = labelEnd >= 0 ? inlineRange(start + 1, start + labelEnd) : inlineRange(ranges.openMarker.end, ranges.openMarker.end);
      }
      break;
    }
    case InlineType::Image: {
      ranges.source = inlineRange(start, end);
      ranges.openMarker = inlineRange(start, qMin(end, start + 2));
      const auto source = markdown_.mid(start, end - start);
      const qsizetype labelEnd = source.indexOf(QLatin1Char(']'));
      ranges.content = labelEnd >= 0 ? inlineRange(ranges.openMarker.end, start + labelEnd) : inlineRange(ranges.openMarker.end, ranges.openMarker.end);
      break;
    }
    default:
      setPlainInlineRanges(ranges, start, end);
      break;
  }

  if (ranges.source.isValid()) {
    inlineNode.setSourceRanges(ranges);
  }
}

QVector<InlineNode> CmarkNodeAdapter::convertInlineChildren(cmark_node* node) {
  QVector<InlineNode> children;
  for (cmark_node* child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
    if (!isBlockType(cmark_node_get_type(child))) {
      children.push_back(convertInline(child));
    }
  }
  for (qsizetype i = 0; i + 1 < children.size(); ++i) {
    const InlineRange next = children.at(i + 1).sourceRange();
    if (next.isValid()) {
      clampInlineRangesEnd(children[i], next.start);
    }
  }
  for (qsizetype i = 1; i < children.size(); ++i) {
    const InlineRange prev = children.at(i - 1).sourceRange();
    if (prev.isValid()) {
      clampInlineRangesStart(children[i], prev.end);
    }
  }
  return children;
}

void CmarkNodeAdapter::readBlockMetadata(cmark_node* cmarkNode, MarkdownNode& muffinNode) {
  switch (muffinNode.type()) {
    case BlockType::Heading:
      muffinNode.setHeadingLevel(cmark_node_get_heading_level(cmarkNode));
      // cmark-gfm only emits Setext headings for levels 1-2, and a Setext heading
      // always spans two source lines (text + underline) whereas ATX is single-line.
      muffinNode.setSetext(muffinNode.sourceRange().lineEnd > muffinNode.sourceRange().lineStart);
      break;
    case BlockType::List:
      muffinNode.setListKind(cmark_node_get_list_type(cmarkNode) == CMARK_ORDERED_LIST
                                 ? ListKind::Ordered
                                 : ListKind::Bullet);
      muffinNode.setListStart(cmark_node_get_list_start(cmarkNode));
      muffinNode.setListTight(cmark_node_get_list_tight(cmarkNode) != 0);
      break;
    case BlockType::ListItem:
      muffinNode.setTaskChecked(cmark_gfm_extensions_get_tasklist_item_checked(cmarkNode));
      break;
    case BlockType::CodeFence: {
      QString codeLiteral = fromUtf8(cmark_node_get_literal(cmarkNode));
      if (codeLiteral.endsWith(QLatin1Char('\n'))) {
        codeLiteral.chop(1);
      }
      muffinNode.setLiteral(codeLiteral);
      muffinNode.setCodeLanguage(fromUtf8(cmark_node_get_fence_info(cmarkNode)).section(' ', 0, 0));
      break;
    }
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
