#include "render/BlockLayoutBuilder.h"

#include "document/InlineProjection.h"

#include <QFontMetricsF>
#include <QStringList>
#include <QTextLayout>
#include <QTextOption>

namespace muffin {
namespace {

qreal layoutTextHeight(const QString& text, const QFont& font, qreal width) {
  QTextLayout layout(text.isEmpty() ? QStringLiteral(" ") : text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(option);
  layout.beginLayout();

  qreal height = 0;
  while (true) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(qMax<qreal>(1.0, width));
    line.setPosition(QPointF(0, height));
    height += line.height();
  }
  layout.endLayout();
  return qMax(height, QFontMetricsF(font).height());
}

qreal layoutLiteralHeight(const QString& text, const QFont& font, qreal width) {
  const QStringList lines = text.isEmpty() ? QStringList{QString()} : text.split(QLatin1Char('\n'));
  qreal height = 0;
  for (const QString& line : lines) {
    height += layoutTextHeight(line.isEmpty() ? QStringLiteral(" ") : line, font, width);
  }
  return qMax(height, QFontMetricsF(font).height());
}

QString displayLiteralFor(const MarkdownNode& node) {
  QString literal = node.type() == BlockType::CodeFence ? node.literal() : node.literal().trimmed();
  if (node.type() == BlockType::CodeFence && literal.endsWith(QLatin1Char('\n'))) {
    literal.chop(1);
  }
  return literal;
}

const MarkdownNode* primaryParagraph(const MarkdownNode& node) {
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child.get();
    }
  }
  return nullptr;
}

bool selectionFocusesNode(const SelectionRange& selection, NodeId nodeId) {
  return nodeId.isValid() && selection.focus.blockId == nodeId && selection.focus.text.nodeId == nodeId;
}

QVector<qreal> tableColumnWidths(const MarkdownNode& table, const RenderTheme& theme, qreal width) {
  int columnCount = 0;
  for (const auto& row : table.children()) {
    columnCount = qMax(columnCount, static_cast<int>(row->children().size()));
  }
  QVector<qreal> widths(columnCount, width / qMax(1, columnCount));
  if (columnCount == 0) {
    return widths;
  }

  const QMarginsF padding = theme.tableCellPadding();
  qreal preferredTotal = 0.0;
  for (int column = 0; column < columnCount; ++column) {
    qreal preferred = 0.0;
    for (const auto& row : table.children()) {
      if (column >= static_cast<int>(row->children().size())) {
        continue;
      }
      const MarkdownNode& cell = *row->children().at(static_cast<size_t>(column));
      const QFont font = row->tableRowIsHeader() ? theme.headingFont(6) : theme.paragraphFont();
      preferred = qMax(preferred, QFontMetricsF(font).horizontalAdvance(InlineProjection::plainTextForInlines(cell.inlines())));
    }
    widths[column] = preferred + padding.left() + padding.right();
    preferredTotal += widths[column];
  }

  const qreal minimum = qMax<qreal>(56.0, width * 0.23);
  for (qreal& columnWidth : widths) {
    columnWidth = qMax(columnWidth, minimum);
  }

  preferredTotal = 0.0;
  for (qreal columnWidth : widths) {
    preferredTotal += columnWidth;
  }
  if (preferredTotal <= 0.0) {
    return QVector<qreal>(columnCount, width / columnCount);
  }
  if (preferredTotal < width) {
    widths[columnCount - 1] += width - preferredTotal;
  } else if (preferredTotal > width) {
    const qreal scale = width / preferredTotal;
    for (qreal& columnWidth : widths) {
      columnWidth *= scale;
    }
  }
  return widths;
}

}  // namespace

void BlockLayoutBuilder::setMarkdownText(QString markdownText) {
  markdownText_ = std::move(markdownText);
}

void BlockLayoutBuilder::setSelection(SelectionRange selection) {
  selection_ = selection;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::build(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  switch (node.type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
      return buildParagraphLike(node, theme, x, y, width, depth);
    case BlockType::BlockQuote:
    case BlockType::List:
      return buildContainer(node, theme, x, y, width, depth);
    case BlockType::ListItem:
      return buildListItem(node, theme, x, y, width, depth);
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
    case BlockType::MathBlock:
      return buildLiteralBlock(node, theme, x, y, width, depth);
    case BlockType::Table:
      return buildTable(node, theme, x, y, width, depth);
    case BlockType::ThematicBreak:
      return buildThematicBreak(node, theme, x, y, width, depth);
    case BlockType::Document:
    default:
      return buildContainer(node, theme, x, y, width, depth);
  }
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildParagraphLike(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);
  layout->setHeadingLevel(node.headingLevel());
  layout->setContentSourceStart(sourceContentStartForEditableNode(node));

  auto inlineLayout = std::make_unique<InlineLayout>();
  const QFont font = node.type() == BlockType::Heading ? theme.headingFont(node.headingLevel()) : theme.paragraphFont();
  InlineLayout::BuildOptions options;
  options.projectionState = InlineProjectionState::forSelection(selection_, node.id(), sourceContentStartForEditableNode(node));
  inlineLayout->build(node.inlines(), sourceTextForEditableNode(node), theme, width, font, options);
  qreal height = inlineLayout->height();
  if (node.type() == BlockType::Heading && node.headingLevel() <= 2) {
    height += theme.blockSpacing() * 0.35;
  }
  layout->setRect(QRectF(x, y, width, height));
  layout->setInlineLayout(std::move(inlineLayout));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildContainer(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);

  const qreal childX = node.type() == BlockType::BlockQuote ? x + theme.blockQuoteIndent() : x;
  const qreal childWidth = node.type() == BlockType::BlockQuote ? width - theme.blockQuoteIndent() : width;
  qreal cursorY = y;
  std::vector<std::unique_ptr<BlockLayout>> children;

  for (const auto& child : node.children()) {
    auto childLayout = build(*child, theme, childX, cursorY, childWidth, depth + 1);
    cursorY = childLayout->rect().bottom() + theme.blockSpacing();
    children.push_back(std::move(childLayout));
  }

  const qreal height = children.empty() ? QFontMetricsF(theme.paragraphFont()).height() : qMax<qreal>(0, cursorY - y - theme.blockSpacing());
  layout->setRect(QRectF(x, y, width, height));
  layout->setChildren(std::move(children));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildListItem(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::ListItem);
  layout->setDepth(depth);

  const qreal contentX = x + theme.listIndent();
  const qreal contentWidth = qMax<qreal>(1.0, width - theme.listIndent());
  qreal cursorY = y;

  auto inlineLayout = std::make_unique<InlineLayout>();
  InlineLayout::BuildOptions options;
  QString listSourceText;
  if (const MarkdownNode* paragraph = primaryParagraph(node)) {
    listSourceText = sourceTextForEditableNode(*paragraph);
    layout->setContentSourceStart(sourceContentStartForEditableNode(*paragraph));
    options.projectionState = InlineProjectionState::forSelection(selection_, node.id(), sourceContentStartForEditableNode(*paragraph));
  }
  inlineLayout->build(primaryInlinesForListItem(node), listSourceText, theme, contentWidth, theme.paragraphFont(), options);
  layout->setInlineLayout(std::move(inlineLayout));

  qreal height = layout->inlineLayout() ? layout->inlineLayout()->height() : QFontMetricsF(theme.paragraphFont()).height();
  std::vector<std::unique_ptr<BlockLayout>> children;

  bool skippedPrimaryParagraph = false;
  for (const auto& child : node.children()) {
    if (!skippedPrimaryParagraph && child->type() == BlockType::Paragraph) {
      skippedPrimaryParagraph = true;
      continue;
    }
    cursorY = y + height + theme.blockSpacing();
    auto childLayout = build(*child, theme, contentX, cursorY, contentWidth, depth + 1);
    height = childLayout->rect().bottom() - y;
    children.push_back(std::move(childLayout));
  }

  if (const MarkdownNode* listParent = node.parent()) {
    qsizetype index = 0;
    for (const auto& sibling : listParent->children()) {
      if (sibling.get() == &node) {
        break;
      }
      ++index;
    }
    layout->setListMarker(textForListMarker(*listParent, index));
  } else {
    layout->setListMarker(QStringLiteral("•"));
  }
  layout->setTaskListItem(node.taskChecked(), node.taskChecked());

  layout->setRect(QRectF(x, y, width, height));
  layout->setChildren(std::move(children));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildLiteralBlock(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(node.type());
  layout->setDepth(depth);
  layout->setLiteral(displayLiteralFor(node));
  if (node.type() == BlockType::CodeFence) {
    layout->setCodeLanguage(node.codeLanguage());
    layout->setCodeHighlightSpans(codeHighlighter_.highlight(node.codeLanguage(), layout->literal()));
  }
  const bool editingLiteral = node.type() == BlockType::MathBlock && selectionFocusesNode(selection_, node.id());
  layout->setLiteralEditing(editingLiteral);
  qreal height = textHeight(layout->literal(), node.type() == BlockType::MathBlock ? theme.mathFont() : theme.codeFont(), width, theme.codePadding());
  if (node.type() == BlockType::MathBlock) {
    auto mathLayout = std::make_shared<math::MathLayoutResult>(mathRenderer_.render(layout->literal(), theme, true, width));
    if (mathLayout->valid()) {
      if (!editingLiteral) {
        height = std::ceil(mathLayout->size.height() + theme.codePadding().top() + theme.codePadding().bottom());
      } else {
        const qreal contentWidth = qMax<qreal>(1.0, width - theme.codePadding().left() - theme.codePadding().right());
        const qreal markerLine = qMax<qreal>(14.0, QFontMetricsF(theme.codeFont()).height());
        const qreal sourceHeight = textHeight(layout->literal(), theme.codeFont(), contentWidth, QMarginsF());
        const qreal previewHeight = mathLayout->size.height();
        height = std::ceil(theme.codePadding().top() + markerLine + sourceHeight + markerLine +
                           theme.codePadding().bottom() + theme.codePadding().top() + previewHeight + theme.codePadding().bottom());
      }
      layout->setMathLayout(std::move(mathLayout));
    }
  }
  layout->setRect(QRectF(x, y, width, height));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildTable(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::Table);
  layout->setDepth(depth);

  const int rowCount = static_cast<int>(node.children().size());
  int columnCount = 0;
  for (const auto& row : node.children()) {
    columnCount = qMax(columnCount, static_cast<int>(row->children().size()));
  }

  if (rowCount == 0 || columnCount == 0) {
    layout->setRect(QRectF(x, y, width, QFontMetricsF(theme.paragraphFont()).height()));
    return layout;
  }

  const QVector<qreal> columnWidths = tableColumnWidths(node, theme, width);
  const QMarginsF padding = theme.tableCellPadding();
  const QVector<TableAlignment> alignments = node.tableAlignments();
  std::vector<BlockLayout::TableRowLayout> rows;
  qreal cursorY = y;
  int rowIndex = 0;

  for (const auto& rowNode : node.children()) {
    std::vector<BlockLayout::TableCellLayout> cells;
    qreal rowHeight = 0;
    qreal cellX = x;
    int column = 0;
    for (const auto& cellNode : rowNode->children()) {
      const qreal columnWidth = column < columnWidths.size() ? columnWidths.at(column) : width / columnCount;
      BlockLayout::TableCellLayout cell;
      cell.nodeId = cellNode->id();
      cell.contentSourceStart = sourceContentStartForEditableNode(*cellNode);
      cell.header = rowNode->tableRowIsHeader();
      cell.alternate = rowIndex % 2 == 1;
      cell.alignment = column < alignments.size() ? alignments.at(column) : TableAlignment::None;
      InlineLayout::BuildOptions options;
      if (selection_.focus.text.nodeId == cellNode->id()) {
        options.projectionState = InlineProjectionState::forSelection(selection_, selection_.focus.blockId, sourceContentStartForEditableNode(*cellNode));
      }
      cell.text.build(
          cellNode->inlines(),
          sourceTextForEditableNode(*cellNode),
          theme,
          qMax<qreal>(1.0, columnWidth - padding.left() - padding.right()),
          cell.header ? theme.headingFont(6) : theme.paragraphFont(),
          options);
      rowHeight = qMax(rowHeight, cell.text.height() + padding.top() + padding.bottom());
      cell.rect = QRectF(cellX, cursorY, columnWidth, 0);
      cells.push_back(std::move(cell));
      cellX += columnWidth;
      ++column;
    }
    while (column < columnCount) {
      const qreal columnWidth = column < columnWidths.size() ? columnWidths.at(column) : width / columnCount;
      BlockLayout::TableCellLayout cell;
      cell.nodeId = rowNode->id();
      cell.alternate = rowIndex % 2 == 1;
      cell.alignment = column < alignments.size() ? alignments.at(column) : TableAlignment::None;
      cell.rect = QRectF(cellX, cursorY, columnWidth, 0);
      rowHeight = qMax(rowHeight, QFontMetricsF(theme.paragraphFont()).height() + padding.top() + padding.bottom());
      cells.push_back(std::move(cell));
      cellX += columnWidth;
      ++column;
    }
    for (BlockLayout::TableCellLayout& cell : cells) {
      cell.rect.setHeight(rowHeight);
    }
    BlockLayout::TableRowLayout row;
    row.rect = QRectF(x, cursorY, width, rowHeight);
    row.cells = std::move(cells);
    rows.push_back(std::move(row));
    cursorY += rowHeight;
    ++rowIndex;
  }

  layout->setRect(QRectF(x, y, width, cursorY - y));
  layout->setTableRows(std::move(rows));
  return layout;
}

std::unique_ptr<BlockLayout> BlockLayoutBuilder::buildThematicBreak(
    const MarkdownNode& node,
    const RenderTheme& theme,
    qreal x,
    qreal y,
    qreal width,
    int depth) {
  auto layout = std::make_unique<BlockLayout>(node.id());
  layout->setType(BlockType::ThematicBreak);
  layout->setDepth(depth);
  layout->setRect(QRectF(x, y, width, theme.blockSpacing() * 2.0));
  return layout;
}

QString BlockLayoutBuilder::textForListMarker(const MarkdownNode& listNode, qsizetype index) const {
  if (listNode.listKind() == ListKind::Ordered) {
    return QStringLiteral("%1.").arg(listNode.listStart() + static_cast<int>(index));
  }
  return QStringLiteral("•");
}

QVector<InlineNode> BlockLayoutBuilder::primaryInlinesForListItem(const MarkdownNode& node) const {
  if (!node.inlines().isEmpty()) {
    return node.inlines();
  }
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child->inlines();
    }
  }
  return {};
}

QString BlockLayoutBuilder::sourceTextForEditableNode(const MarkdownNode& node) const {
  const qsizetype start = sourceContentStartForEditableNode(node);
  const qsizetype end = sourceContentEndForEditableNode(node);
  if (start < 0 || end < start) {
    return {};
  }
  return markdownText_.mid(start, end - start);
}

qsizetype BlockLayoutBuilder::sourceContentStartForEditableNode(const MarkdownNode& node) const {
  const SourceRange range = node.sourceRange();
  qsizetype start = node.type() == BlockType::TableCell && range.byteEnd >= range.byteStart
                     ? range.byteStart
                     : sourceOffsetForLineColumn(range.lineStart, qMax(1, range.columnStart));
  const qsizetype end = sourceContentEndForEditableNode(node);
  if (start < 0 || end < start) {
    return -1;
  }
  if (node.type() == BlockType::Heading) {
    while (start < end && markdownText_.at(start) == QLatin1Char('#')) {
      ++start;
    }
    if (start < end && markdownText_.at(start).isSpace()) {
      ++start;
    }
  }
  return start;
}

qsizetype BlockLayoutBuilder::sourceContentEndForEditableNode(const MarkdownNode& node) const {
  const SourceRange range = node.sourceRange();
  qsizetype end = node.type() == BlockType::TableCell && range.byteEnd >= range.byteStart
                    ? range.byteEnd
                    : sourceOffsetForLineEnd(range.lineEnd);
  const qsizetype start = sourceOffsetForLineColumn(range.lineStart, qMax(1, range.columnStart));
  if (start < 0 || end < start) {
    return -1;
  }
  if (node.type() == BlockType::TableCell) {
    while (end > start && markdownText_.at(end - 1).isSpace()) {
      --end;
    }
  }
  return end;
}

qsizetype BlockLayoutBuilder::sourceOffsetForLineColumn(int line, int column) const {
  if (line <= 0 || column <= 0) {
    return -1;
  }
  int currentLine = 1;
  qsizetype offset = 0;
  while (currentLine < line && offset < markdownText_.size()) {
    if (markdownText_.at(offset) == QLatin1Char('\n')) {
      ++currentLine;
    }
    ++offset;
  }
  if (currentLine != line) {
    return -1;
  }
  return qMin(offset + column - 1, markdownText_.size());
}

qsizetype BlockLayoutBuilder::sourceOffsetForLineEnd(int line) const {
  if (line <= 0) {
    return -1;
  }
  int currentLine = 1;
  qsizetype offset = 0;
  while (offset < markdownText_.size()) {
    if (currentLine == line && markdownText_.at(offset) == QLatin1Char('\n')) {
      return offset;
    }
    if (markdownText_.at(offset) == QLatin1Char('\n')) {
      ++currentLine;
    }
    ++offset;
  }
  return currentLine == line ? markdownText_.size() : -1;
}

qreal BlockLayoutBuilder::textHeight(const QString& text, const QFont& font, qreal width, const QMarginsF& padding) const {
  const qreal innerWidth = qMax<qreal>(1.0, width - padding.left() - padding.right());
  return std::ceil(layoutLiteralHeight(text, font, innerWidth) + padding.top() + padding.bottom() + 2.0);
}

}  // namespace muffin
