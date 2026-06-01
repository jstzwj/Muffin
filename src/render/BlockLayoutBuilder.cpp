#include "render/BlockLayoutBuilder.h"

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
  option.setFlags(QTextOption::ShowTabsAndSpaces);
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

}  // namespace

void BlockLayoutBuilder::setActiveCursor(CursorPosition cursor) {
  activeCursor_ = cursor;
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

  auto inlineLayout = std::make_unique<InlineLayout>();
  const QFont font = node.type() == BlockType::Heading ? theme.headingFont(node.headingLevel()) : theme.paragraphFont();
  InlineLayout::BuildOptions options;
  if (activeCursor_.blockId == node.id()) {
    options.activeTextOffset = activeCursor_.text.textOffset;
  }
  inlineLayout->build(node.inlines(), theme, width, font, options);
  const qreal height = inlineLayout->height();
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
  if (activeCursor_.blockId == node.id()) {
    options.activeTextOffset = activeCursor_.text.textOffset;
  }
  inlineLayout->build(primaryInlinesForListItem(node), theme, contentWidth, theme.paragraphFont(), options);
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
  const qreal height = textHeight(layout->literal(), node.type() == BlockType::MathBlock ? theme.mathFont() : theme.codeFont(), width, theme.codePadding());
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

  const qreal columnWidth = width / columnCount;
  const QMarginsF padding = theme.tableCellPadding();
  const QVector<TableAlignment> alignments = node.tableAlignments();
  std::vector<BlockLayout::TableRowLayout> rows;
  qreal cursorY = y;
  int rowIndex = 0;

  for (const auto& rowNode : node.children()) {
    std::vector<BlockLayout::TableCellLayout> cells;
    qreal rowHeight = 0;
    int column = 0;
    for (const auto& cellNode : rowNode->children()) {
      BlockLayout::TableCellLayout cell;
      cell.nodeId = cellNode->id();
      cell.header = rowNode->tableRowIsHeader();
      cell.alternate = rowIndex % 2 == 1;
      cell.alignment = column < alignments.size() ? alignments.at(column) : TableAlignment::None;
      InlineLayout::BuildOptions options;
      if (activeCursor_.text.nodeId == cellNode->id()) {
        options.activeTextOffset = activeCursor_.text.textOffset;
      }
      cell.text.build(
          cellNode->inlines(),
          theme,
          qMax<qreal>(1.0, columnWidth - padding.left() - padding.right()),
          cell.header ? theme.headingFont(6) : theme.paragraphFont(),
          options);
      rowHeight = qMax(rowHeight, cell.text.height() + padding.top() + padding.bottom());
      cell.rect = QRectF(x + column * columnWidth, cursorY, columnWidth, 0);
      cells.push_back(std::move(cell));
      ++column;
    }
    while (column < columnCount) {
      BlockLayout::TableCellLayout cell;
      cell.nodeId = rowNode->id();
      cell.alternate = rowIndex % 2 == 1;
      cell.alignment = column < alignments.size() ? alignments.at(column) : TableAlignment::None;
      cell.rect = QRectF(x + column * columnWidth, cursorY, columnWidth, 0);
      rowHeight = qMax(rowHeight, QFontMetricsF(theme.paragraphFont()).height() + padding.top() + padding.bottom());
      cells.push_back(std::move(cell));
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

qreal BlockLayoutBuilder::textHeight(const QString& text, const QFont& font, qreal width, const QMarginsF& padding) const {
  const qreal innerWidth = qMax<qreal>(1.0, width - padding.left() - padding.right());
  return std::ceil(layoutLiteralHeight(text, font, innerWidth) + padding.top() + padding.bottom() + 2.0);
}

}  // namespace muffin
