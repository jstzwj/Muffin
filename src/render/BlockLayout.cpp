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

void BlockLayout::setTaskListItem(bool taskListItem, bool checked) {
  taskListItem_ = taskListItem;
  taskChecked_ = checked;
}

bool BlockLayout::isTaskListItem() const {
  return taskListItem_;
}

bool BlockLayout::taskChecked() const {
  return taskChecked_;
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

HitTestResult BlockLayout::hitTest(QPointF documentPos, const RenderTheme& theme) const {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    const BlockLayout& child = **it;
    if (child.rect().adjusted(-theme.blockSpacing(), -theme.blockSpacing(), theme.blockSpacing(), theme.blockSpacing()).contains(documentPos)) {
      HitTestResult childHit = child.hitTest(documentPos, theme);
      if (childHit.isValid()) {
        return childHit;
      }
    }
  }

  if (!rect_.adjusted(-2, -theme.blockSpacing() * 0.5, 2, theme.blockSpacing() * 0.5).contains(documentPos)) {
    return {};
  }

  if (type_ == BlockType::Table) {
    return hitTable(documentPos, theme);
  }
  return hitSelf(documentPos, theme);
}

QVector<QRectF> BlockLayout::selectionRects(const SelectionRange& selection, const RenderTheme& theme) const {
  QVector<QRectF> rects = selectionRectsSelf(selection, theme);
  for (const auto& child : children_) {
    rects += child->selectionRects(selection, theme);
  }
  return rects;
}

QVector<QRectF> BlockLayout::selectionRectsForOffsets(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const {
  QVector<QRectF> rects = selectionRectsSelfForOffsets(startOffset, endOffset, theme);
  for (const auto& child : children_) {
    rects += child->selectionRectsForOffsets(startOffset, endOffset, theme);
  }
  return rects;
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
          if (taskListItem_) {
            const QRectF box(
                viewRect.left() + 1,
                viewRect.top() + qMax<qreal>(2.0, (metrics.height() - 13.0) / 2.0),
                13,
                13);
            painter.setBrush(theme.backgroundColor());
            painter.setPen(QPen(theme.tableBorderColor(), 1));
            painter.drawRoundedRect(box, 2, 2);
            if (taskChecked_) {
              painter.setPen(QPen(theme.linkColor(), 1.8));
              painter.drawLine(QPointF(box.left() + 3, box.center().y()), QPointF(box.left() + 5.5, box.bottom() - 3));
              painter.drawLine(QPointF(box.left() + 5.5, box.bottom() - 3), QPointF(box.right() - 3, box.top() + 3));
            }
          } else {
            painter.drawText(QPointF(viewRect.left(), viewRect.top() + metrics.ascent()), listMarker_);
          }
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

QVector<QRectF> BlockLayout::selectionRectsSelf(const SelectionRange& selection, const RenderTheme& theme) const {
  QVector<QRectF> rects;
  if (!selection.isSingleBlock() || selection.isCollapsed() || selection.anchor.blockId != id_ || !inlineLayout_) {
    return rects;
  }

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      break;
    default:
      return rects;
  }

  const qreal textLeft = !listMarker_.isEmpty() ? rect_.left() + theme.listIndent() : rect_.left();
  const QPointF origin(textLeft, rect_.top());
  for (QRectF rect : inlineLayout_->selectionRects(selection.startOffset(), selection.endOffset())) {
    rect.translate(origin);
    rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
  }
  return rects;
}

QVector<QRectF> BlockLayout::selectionRectsSelfForOffsets(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const {
  QVector<QRectF> rects;
  if (!inlineLayout_) {
    return rects;
  }

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      break;
    default:
      return rects;
  }

  const qreal textLeft = !listMarker_.isEmpty() ? rect_.left() + theme.listIndent() : rect_.left();
  const QPointF origin(textLeft, rect_.top());
  for (QRectF rect : inlineLayout_->selectionRects(startOffset, endOffset)) {
    rect.translate(origin);
    rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
  }
  return rects;
}

HitTestResult BlockLayout::hitSelf(QPointF documentPos, const RenderTheme& theme) const {
  HitTestResult result;
  result.blockId = id_;
  result.textNodeId = id_;
  result.blockRect = rect_;
  result.zone = HitTestResult::Zone::Block;

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      if (inlineLayout_) {
        const qreal textLeft = !listMarker_.isEmpty() ? rect_.left() + theme.listIndent() : rect_.left();
        const QRectF textRect(textLeft, rect_.top(), qMax<qreal>(1.0, rect_.right() - textLeft), rect_.height());
        if (!listMarker_.isEmpty() && documentPos.x() < textLeft) {
          result.zone = HitTestResult::Zone::Marker;
          result.cursorRect = QRectF(textLeft, rect_.top(), 1.0, rect_.height());
          return result;
        }
        result.zone = HitTestResult::Zone::Text;
        result.textOffset = inlineLayout_->hitTestTextOffset(documentPos - textRect.topLeft());
        result.cursorRect = inlineLayout_->cursorRect(result.textOffset).translated(textRect.topLeft());
      }
      break;
    case BlockType::CodeFence:
      result.zone = HitTestResult::Zone::Code;
      result.cursorRect = QRectF(rect_.marginsRemoved(theme.codePadding()).topLeft(), QSizeF(1.0, rect_.height()));
      break;
    case BlockType::MathBlock:
      result.zone = HitTestResult::Zone::Math;
      result.cursorRect = QRectF(rect_.marginsRemoved(theme.codePadding()).topLeft(), QSizeF(1.0, rect_.height()));
      break;
    case BlockType::HtmlBlock:
      result.zone = HitTestResult::Zone::Html;
      result.cursorRect = QRectF(rect_.marginsRemoved(theme.codePadding()).topLeft(), QSizeF(1.0, rect_.height()));
      break;
    default:
      result.cursorRect = QRectF(rect_.topLeft(), QSizeF(1.0, rect_.height()));
      break;
  }

  return result;
}

HitTestResult BlockLayout::hitTable(QPointF documentPos, const RenderTheme& theme) const {
  HitTestResult result;
  result.blockId = id_;
  result.textNodeId = id_;
  result.blockRect = rect_;
  result.zone = HitTestResult::Zone::Block;

  int rowIndex = 0;
  for (const TableRowLayout& row : tableRows_) {
    if (!row.rect.contains(documentPos)) {
      ++rowIndex;
      continue;
    }
    int columnIndex = 0;
    for (const TableCellLayout& cell : row.cells) {
      if (cell.rect.contains(documentPos)) {
        result.zone = HitTestResult::Zone::TableCell;
        result.textNodeId = cell.nodeId.isValid() ? cell.nodeId : id_;
        result.tableRow = rowIndex;
        result.tableColumn = columnIndex;
        result.textOffset = cell.text.hitTestTextOffset(documentPos - cell.rect.marginsRemoved(theme.tableCellPadding()).topLeft());
        result.cursorRect = cell.text.cursorRect(result.textOffset).translated(cell.rect.marginsRemoved(theme.tableCellPadding()).topLeft());
        return result;
      }
      ++columnIndex;
    }
    ++rowIndex;
  }

  result.cursorRect = QRectF(rect_.topLeft(), QSizeF(1.0, rect_.height()));
  return result;
}

void BlockLayout::paintTable(QPainter& painter, const RenderTheme& theme, qreal scrollY) const {
  painter.save();
  for (const TableRowLayout& row : tableRows_) {
    const QRectF rowRect = row.rect.translated(0, -scrollY);
    for (const TableCellLayout& cell : row.cells) {
      const QRectF cellRect = cell.rect.translated(0, -scrollY);
      painter.setPen(theme.tableBorderColor());
      painter.setBrush(cell.header ? theme.tableHeaderBackgroundColor() : (cell.alternate ? theme.tableAlternateBackgroundColor() : theme.backgroundColor()));
      painter.drawRect(cellRect.adjusted(0.5, 0.5, -0.5, -0.5));
      QRectF contentRect = cellRect.marginsRemoved(theme.tableCellPadding());
      qreal textX = contentRect.left();
      if (cell.alignment == TableAlignment::Right) {
        textX = contentRect.right() - cell.text.size().width();
      } else if (cell.alignment == TableAlignment::Center) {
        textX = contentRect.left() + (contentRect.width() - cell.text.size().width()) / 2.0;
      }
      cell.text.paint(painter, QPointF(qMax(contentRect.left(), textX), contentRect.top()));
    }
    Q_UNUSED(rowRect);
  }
  painter.restore();
}

}  // namespace muffin
