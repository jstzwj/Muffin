#include "parser/CmarkNodeAdapter.h"

#include "document/InlineNode.h"
#include "document/LineStartOffsetCache.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QString>

extern "C" {
#include "cmark-gfm-core-extensions.h"
#include "strikethrough.h"
#include "table.h"
#include "math_extension.h"
}

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(adapterPerf, "muffin.perf", QtWarningMsg)

// Aggregate convertBlock breakdown counters (single-threaded parse). Enabled only while
// muffin.perf debug is on, so the fromUtf8/annotate hot paths pay just a branch in normal builds.
qint64 g_fromUtf8Ns = 0;
qint64 g_annotateNs = 0;
qint64 g_inlineChildrenNs = 0;
qint64 g_blockLocalNs = 0;
bool g_adapterPerf = false;

struct AccumGuard {
  qint64& bucket;
  QElapsedTimer timer;
  explicit AccumGuard(qint64& b) : bucket(b) {
    if (g_adapterPerf) {
      timer.start();
    }
  }
  ~AccumGuard() {
    if (g_adapterPerf) {
      bucket += timer.nsecsElapsed();
    }
  }
};

QString fromUtf8(const char* value) {
  AccumGuard guard(g_fromUtf8Ns);
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

// Index (relative to `source`) of the ']' that closes the bracketed construct whose opener already
// occupies source[0..scanFrom). Scanning starts at `scanFrom`, so a nested opener — the '[' of an
// image label inside a link [![alt](img)](url) — is balanced against its OWN ']' and skipped. A naive
// first-']' search would otherwise stop at the nested image's ']' and truncate the link's label range,
// which in turn made the image child fall out of the link's content bounds and render as alt text.
// Returns -1 when no matching closer is found.
qsizetype matchingBracketEnd(QStringView source, qsizetype scanFrom) {
  int depth = 0;
  for (qsizetype i = scanFrom; i < source.size(); ++i) {
    const QChar ch = source.at(i);
    if (ch == QLatin1Char('[')) {
      ++depth;
    } else if (ch == QLatin1Char(']')) {
      if (depth == 0) {
        return i;
      }
      --depth;
    }
  }
  return -1;
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
  std::unique_ptr<MarkdownNode> result;
  {
    AccumGuard g(g_blockLocalNs);
    result = std::make_unique<MarkdownNode>(mapBlockType(node));
    result->setSourceRange(readSourceRange(node));
    readBlockMetadata(node, *result);
  }

  if (result->type() == BlockType::Paragraph || result->type() == BlockType::Heading ||
      result->type() == BlockType::TableCell) {
    result->inlines() = convertInlineChildren(node);
  }

  // Compute byte-level source range for block types whose editing code
  // needs byteStart/byteEnd to resolve cell content or inline offsets.
  {
    AccumGuard g(g_blockLocalNs);
    if (lineOffsets_ && !markdown_.isEmpty()) {
      const SourceRange srcRange = result->sourceRange();
      if (srcRange.lineStart > 0 && result->sourceRange().byteEnd <= result->sourceRange().byteStart) {
        const qsizetype start = lineOffsets_->offsetForLineByteColumn(markdown_, srcRange.lineStart, qMax(1, srcRange.columnStart));
        const qsizetype end = lineOffsets_->offsetForLineByteColumn(markdown_, srcRange.lineEnd, qMax(1, srcRange.columnEnd + 1));
        if (start >= 0 && end >= start && end <= markdown_.size()) {
          SourceRange updated = srcRange;
          updated.byteStart = start;
          updated.byteEnd = end;
          result->setSourceRange(updated);
        }
      }
    }
  }

  // Iterate block children, converting each then freeing its cmark subtree immediately. cmark is
  // per-node malloc (NOT arena), so cmark_node_free releases real memory now. Without this the
  // whole cmark tree overlaps the Muffin tree being built until function return — the dominant
  // ~800MB slice of the open-time peak on large files. Save `next` BEFORE freeing: cmark_node_free
  // unlinks the node (mutating the sibling/parent chain). A block's inline children were read in
  // Phase 2 above and ride inside its subtree, freed here with it.
  cmark_node* child = cmark_node_first_child(node);
  while (child) {
    cmark_node* next = cmark_node_next(child);
    const cmark_node_type type = cmark_node_get_type(child);
    if (isBlockType(type)) {
      result->appendChild(convertBlock(child));
      cmark_node_free(child);
    }
    child = next;
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
    case InlineType::FootnoteReference: {
      // cmark resolves footnotes at parse time (process_footnotes): the reference's
      // literal becomes the resolved ordinal ("1"), and parent_footnote_def points at
      // the definition (whose literal is still the bare label).
      const QString ordinal = fromUtf8(cmark_node_get_literal(node));
      cmark_node* def = cmark_node_parent_footnote_def(node);
      const QString label = def ? fromUtf8(cmark_node_get_literal(def)) : ordinal;
      return InlineNode::footnoteReference(label, ordinal);
    }
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
  if (type == CMARK_NODE_FOOTNOTE_REFERENCE) return InlineType::FootnoteReference;
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
  AccumGuard guard(g_annotateNs);
  if (!lineOffsets_ || markdown_.isEmpty()) {
    return;
  }
  const SourceRange range = readSourceRange(cmarkNode);
  if (range.lineStart <= 0) {
    return;
  }
  const qsizetype start = lineOffsets_->offsetForLineByteColumn(markdown_, range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = lineOffsets_->offsetForLineByteColumn(markdown_, range.lineEnd, qMax(1, range.columnEnd + 1));
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
    case InlineType::FootnoteReference: {
      // cmark's range covers the whole `[^label]` token; the projection renders the
      // resolved ordinal but reveals this raw source when the caret is inside.
      setPlainInlineRanges(ranges, start, end);
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
        // Depth-aware: skip the ']' of a nested image label so the link's OWN ']' bounds the label.
        const qsizetype labelEnd = matchingBracketEnd(source, ranges.openMarker.end - start);
        ranges.content = labelEnd >= 0 ? inlineRange(start + 1, start + labelEnd) : inlineRange(ranges.openMarker.end, ranges.openMarker.end);
      }
      break;
    }
    case InlineType::Image: {
      ranges.source = inlineRange(start, end);
      ranges.openMarker = inlineRange(start, qMin(end, start + 2));
      const auto source = markdown_.mid(start, end - start);
      const qsizetype labelEnd = matchingBracketEnd(source, ranges.openMarker.end - start);
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
  AccumGuard guard(g_inlineChildrenNs);
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
      // The tasklist extension reports type string "tasklist" for genuine task
      // items and "item" for plain bullets; the checked flag alone is false for
      // both an unchecked task item and a regular item, so it cannot tell them
      // apart. Capture task-item identity here so the serializer can round-trip
      // the checkbox faithfully.
      muffinNode.setTaskItem(
          QString::fromUtf8(cmark_node_get_type_string(cmarkNode)) == QLatin1String("tasklist"));
      muffinNode.setTaskChecked(cmark_gfm_extensions_get_tasklist_item_checked(cmarkNode));
      break;
    case BlockType::CodeFence: {
      QString codeLiteral = fromUtf8(cmark_node_get_literal(cmarkNode));
      if (codeLiteral.endsWith(QLatin1Char('\n'))) {
        codeLiteral.chop(1);
      }
      muffinNode.setLiteral(codeLiteral);
      muffinNode.setCodeLanguage(fromUtf8(cmark_node_get_fence_info(cmarkNode)).section(' ', 0, 0));
      // cmark reports both fenced and indented code as CMARK_NODE_CODE_BLOCK, but it records the
      // original form in the node's authoritative `fenced` flag (set during parsing). That is the
      // only reliable signal: inspecting the source line breaks for a fence nested inside a
      // list/block-quote, whose opening fence is itself indented (column 1 is a space) yet the
      // block is genuinely fenced. Round-trip indented code as indented, fenced as fenced.
      int fenceLength = 0;
      int fenceOffset = 0;
      char fenceChar = 0;
      const bool isFenced = cmark_node_get_fenced(cmarkNode, &fenceLength, &fenceOffset, &fenceChar) != 0;
      muffinNode.setIndentedCode(!isFenced);
      break;
    }
    case BlockType::HtmlBlock:
      muffinNode.setLiteral(fromUtf8(cmark_node_get_literal(cmarkNode)));
      break;
    case BlockType::MathBlock: {
      // cmark wraps the math body in newlines (opener/closer live on their own lines):
      //   "$$\nx\n$$" -> string_content = "\nx\n"
      // Treat it like a fenced code block: strip exactly one leading and one trailing
      // newline so the literal is the user-editable body. Internal newlines/blank lines
      // are preserved (cmark keeps them), which lets the user break a long formula across
      // lines and keeps caret offsets mapping 1:1 to the source.
      QString mathLiteral = fromUtf8(cmark_node_get_string_content(cmarkNode));
      if (mathLiteral.startsWith(QLatin1Char('\n'))) {
        mathLiteral.remove(0, 1);
      }
      if (mathLiteral.endsWith(QLatin1Char('\n'))) {
        mathLiteral.chop(1);
      }
      muffinNode.setLiteral(mathLiteral);
      break;
    }
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

void CmarkNodeAdapter::setPerfEnabled(bool enabled) {
  g_adapterPerf = enabled;
}

void CmarkNodeAdapter::resetPerfCounters() {
  g_fromUtf8Ns = 0;
  g_annotateNs = 0;
  g_inlineChildrenNs = 0;
  g_blockLocalNs = 0;
}

void CmarkNodeAdapter::dumpConvertBreakdown() {
  if (!adapterPerf().isDebugEnabled()) {
    return;
  }
  qCDebug(adapterPerf).nospace() << "adapter.fromUtf8 " << g_fromUtf8Ns / 1000000.0 << " ms";
  qCDebug(adapterPerf).nospace() << "adapter.annotateInline " << g_annotateNs / 1000000.0 << " ms";
  qCDebug(adapterPerf).nospace() << "adapter.inlineChildren " << g_inlineChildrenNs / 1000000.0 << " ms (incl fromUtf8+annotate)";
  qCDebug(adapterPerf).nospace() << "adapter.blockLocal " << g_blockLocalNs / 1000000.0 << " ms (construct+meta+byterange)";
}

}  // namespace muffin
