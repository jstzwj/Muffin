#include "render/BlockLayout.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextOption>

namespace muffin {

BlockLayout::BlockLayout(NodeId id) : id_(std::move(id)) {}

NodeId BlockLayout::nodeId() const {
  return id_;
}

BlockType BlockLayout::type() const {
  return type_;
}

void BlockLayout::setType(BlockType type) {
  type_ = type;
}

QRectF BlockLayout::rect() const {
  return rect_;
}

void BlockLayout::setRect(QRectF rect) {
  rect_ = rect;
}

void BlockLayout::translateY(qreal dy) {
  rect_.translate(0, dy);
  for (auto& child : children_) {
    child->translateY(dy);
  }
  for (TableRowLayout& row : tableRows_) {
    row.rect.translate(0, dy);
    for (TableCellLayout& cell : row.cells) {
      cell.rect.translate(0, dy);
    }
  }
}

qreal BlockLayout::height() const {
  return rect_.height();
}

qreal BlockLayout::bottom() const {
  return rect_.bottom();
}

void BlockLayout::setInlineLayout(std::unique_ptr<InlineLayout> layout) {
  inlineLayout_ = std::move(layout);
}

InlineLayout* BlockLayout::inlineLayout() {
  return inlineLayout_.get();
}

const InlineLayout* BlockLayout::inlineLayout() const {
  return inlineLayout_.get();
}

void BlockLayout::setLiteral(QString literal) {
  literal_ = std::move(literal);
}

QString BlockLayout::literal() const {
  return literal_;
}

void BlockLayout::setHeadingLevel(int level) {
  headingLevel_ = level;
}

int BlockLayout::headingLevel() const {
  return headingLevel_;
}

void BlockLayout::setListMarker(QString marker) {
  listMarker_ = std::move(marker);
}

QString BlockLayout::listMarker() const {
  return listMarker_;
}

void BlockLayout::setDepth(int depth) {
  depth_ = depth;
}

int BlockLayout::depth() const {
  return depth_;
}

void BlockLayout::setChildren(std::vector<std::unique_ptr<BlockLayout>> children) {
  children_ = std::move(children);
}

std::vector<std::unique_ptr<BlockLayout>>& BlockLayout::children() {
  return children_;
}

const std::vector<std::unique_ptr<BlockLayout>>& BlockLayout::children() const {
  return children_;
}

void BlockLayout::setTableRows(std::vector<TableRowLayout> rows) {
  tableRows_ = std::move(rows);
}

std::vector<BlockLayout::TableRowLayout>& BlockLayout::tableRows() {
  return tableRows_;
}

const std::vector<BlockLayout::TableRowLayout>& BlockLayout::tableRows() const {
  return tableRows_;
}

void BlockLayout::paint(QPainter& painter, const RenderTheme& theme, qreal scrollY) const {
  paintSelf(painter, theme, scrollY);
  for (const auto& child : children_) {
    child->paint(painter, theme, scrollY);
  }
}

bool BlockLayout::intersects(const QRectF& documentViewport) const {
  return rect_.intersects(documentViewport);
}

void BlockLayout::paintSelf(QPainter& painter, const RenderTheme& theme, qreal scrollY) const {
  const QRectF viewRect = rect_.translated(0, -scrollY);

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      if (inlineLayout_) {
        if (!listMarker_.isEmpty()) {
          painter.save();
          painter.setFont(theme.paragraphFont());
          painter.setPen(theme.textColor());
          const QFontMetricsF metrics(painter.font());
          painter.drawText(QPointF(viewRect.left(), viewRect.top() + metrics.ascent()), listMarker_);
          painter.restore();
          inlineLayout_->paint(painter, QPointF(viewRect.left() + theme.listIndent(), viewRect.top()));
        } else {
          inlineLayout_->paint(painter, viewRect.topLeft());
        }
      }
      break;
    case BlockType::BlockQuote: {
      painter.save();
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme.quoteBorderColor());
      painter.drawRect(QRectF(viewRect.left(), viewRect.top(), 4, viewRect.height()));
      painter.restore();
      break;
    }
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
    case BlockType::MathBlock: {
      painter.save();
      painter.setPen(theme.codeBorderColor());
      painter.setBrush(theme.codeBackgroundColor());
      painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
      painter.setPen(theme.textColor());
      painter.setFont(type_ == BlockType::MathBlock ? theme.mathFont() : theme.codeFont());
      QTextOption option;
      option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
      const QMarginsF padding = theme.codePadding();
      painter.drawText(viewRect.marginsRemoved(padding), literal_, option);
      painter.restore();
      break;
    }
    case BlockType::ThematicBreak: {
      painter.save();
      painter.setPen(QPen(theme.codeBorderColor(), 1));
      const qreal y = viewRect.center().y();
      painter.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
      painter.restore();
      break;
    }
    case BlockType::Table:
      paintTable(painter, theme, scrollY);
      break;
    default:
      break;
  }
}

void BlockLayout::paintTable(QPainter& painter, const RenderTheme& theme, qreal scrollY) const {
  painter.save();
  for (const TableRowLayout& row : tableRows_) {
    const QRectF rowRect = row.rect.translated(0, -scrollY);
    for (const TableCellLayout& cell : row.cells) {
      const QRectF cellRect = cell.rect.translated(0, -scrollY);
      painter.setPen(theme.tableBorderColor());
      painter.setBrush(cell.header ? theme.tableHeaderBackgroundColor() : theme.backgroundColor());
      painter.drawRect(cellRect.adjusted(0.5, 0.5, -0.5, -0.5));
      cell.text.paint(painter, cellRect.marginsRemoved(theme.tableCellPadding()).topLeft());
    }
    Q_UNUSED(rowRect);
  }
  painter.restore();
}

}  // namespace muffin
