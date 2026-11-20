#include "render/CollapsedTableBorders.h"

#include <QPainter>
#include <QPainterPath>
#include <tuple>

namespace muffin {
namespace {

int styleRank(const QString& style) {
  // CSS 2.2 §17.6.2.1, in increasing order of precedence.
  static const QStringList order = {"none",   "inset",  "groove", "outset", "ridge",
                                    "dotted", "dashed", "solid",  "double"};
  return order.indexOf(style);
}

ThemeElementStyle rowGroupStyle(const MarkdownNode& row, const RenderTheme& theme) {
  return theme.styleForElement(*theme.cssElementForNode(row)->parent);
}

// Snap the stroke width independently of its position. Otherwise equal CSS widths
// can cover different numbers of device pixels on a fractionally scaled display.
QRectF borderStrip(QPointF start, qreal length, qreal width, qreal before, bool horizontal, const QTransform& device) {
  qreal along = horizontal ? start.x() : start.y();
  qreal across = horizontal ? start.y() : start.x();
  if (device.type() <= QTransform::TxScale && device.m11() > 0 && device.m22() > 0) {
    const qreal sx = horizontal ? device.m11() : device.m22();
    const qreal sy = horizontal ? device.m22() : device.m11();
    const qreal dx = horizontal ? device.dx() : device.dy();
    const qreal dy = horizontal ? device.dy() : device.dx();
    const qreal end = (qRound((along + length) * sx + dx) - dx) / sx;
    along = (qRound(along * sx + dx) - dx) / sx;
    length = end - along;
    const int pixels = qMax(1, qRound(width * sy));
    across = (qRound(across * sy + dy - before * pixels) - dy) / sy;
    width = pixels / sy;
  } else {
    across -= before * width;
  }
  return horizontal ? QRectF(along, across, length, width) : QRectF(across, along, width, length);
}

} // namespace

void CollapsedTableBorders::Edge::consider(const Edge& other) {
  // Hidden vetoes even a wider border. For equal cell/row candidates insertion
  // order breaks the tie: Markdown rows run top to bottom, cells left to right.
  if (style == "hidden" || other.style == "none")
    return;
  if (other.style == "hidden" ||
      std::tuple(other.width, styleRank(other.style), other.origin) > std::tuple(width, styleRank(style), origin))
    *this = other;
}

CollapsedTableBorders::CollapsedTableBorders(const MarkdownNode& table, const RenderTheme& theme,
                                             const ThemeElementBoxStyle& tableBox, int columns)
    : rows_(static_cast<int>(table.children().size())), columns_(columns),
      horizontal_((rows_ + 1) * qsizetype(columns)), vertical_((columns + 1) * qsizetype(rows_)) {
  addBox(tableBox, Origin::Table, 0, rows_, 0, columns_);
  for (int first = 0; first < rows_;) {
    const auto& row = *table.children()[first];
    int end = first + 1;
    while (end < rows_ && table.children()[end]->tableRowIsHeader() == row.tableRowIsHeader())
      ++end;
    const auto group = rowGroupStyle(row, theme);
    rowGroupStyles_.emplace_back(first, group.fingerprint);
    addBox(theme.usedBoxForStyle(group), Origin::RowGroup, first, end, 0, columns_, group.paint.opacity);
    first = end;
  }
  for (int r = 0; r < rows_; ++r) {
    const auto& row = *table.children()[r];
    if (const auto* style = theme.elementStyleForNode(row, QStringLiteral("tr")))
      addBox(theme.usedBoxForStyle(*style), Origin::Row, r, r + 1, 0, columns_, style->paint.opacity);
    const QString key = row.tableRowIsHeader() ? QStringLiteral("th") : QStringLiteral("td");
    for (int c = 0; c < columns_; ++c) {
      const auto* cell = c < static_cast<int>(row.children().size()) ? row.children()[c].get() : nullptr;
      const auto* style = cell ? theme.elementStyleForNode(*cell, key) : theme.elementStyle(key);
      if (style)
        addBox(theme.usedBoxForStyle(*style), Origin::Cell, r, r + 1, c, c + 1, style->paint.opacity);
    }
  }
}

bool CollapsedTableBorders::rowGroupStylesMatch(const MarkdownNode& table, const RenderTheme& theme) const {
  for (const auto& [first, fingerprint] : rowGroupStyles_)
    if (first >= static_cast<int>(table.children().size()) ||
        rowGroupStyle(*table.children()[first], theme).fingerprint != fingerprint)
      return false;
  return true;
}

void CollapsedTableBorders::addBox(const ThemeElementBoxStyle& box, Origin origin, int firstRow, int endRow,
                                   int firstCol, int endCol, qreal opacity) {
  const auto ink = [opacity](QColor color) {
    if (color.isValid())
      color.setAlphaF(color.alphaF() * opacity);
    return color;
  };
  const Edge top{box.borderTopWidth, ink(box.borderTopColor), box.borderTopStyle.toLower(), origin};
  const Edge bottom{box.borderBottomWidth, ink(box.borderBottomColor), box.borderBottomStyle.toLower(), origin};
  for (int c = firstCol; c < endCol; ++c) {
    horizontal_[qsizetype(firstRow) * columns_ + c].consider(top);
    horizontal_[qsizetype(endRow) * columns_ + c].consider(bottom);
  }
  const Edge left{box.borderLeftWidth, ink(box.borderLeftColor), box.borderLeftStyle.toLower(), origin};
  const Edge right{box.borderRightWidth, ink(box.borderRightColor), box.borderRightStyle.toLower(), origin};
  for (int r = firstRow; r < endRow; ++r) {
    vertical_[qsizetype(firstCol) * rows_ + r].consider(left);
    vertical_[qsizetype(endCol) * rows_ + r].consider(right);
  }
}

void CollapsedTableBorders::applyCellInsets(ThemeElementBoxStyle& box, int row, int column) const {
  // Cell rectangles partition the table's border box. A perimeter stroke is
  // entirely inside that partition; neighbours each reserve half a shared stroke.
  box.borderTopWidth = horizontal_[qsizetype(row) * columns_ + column].width * (row == 0 ? 1 : .5);
  box.borderBottomWidth = horizontal_[qsizetype(row + 1) * columns_ + column].width * (row + 1 == rows_ ? 1 : .5);
  box.borderLeftWidth = vertical_[qsizetype(column) * rows_ + row].width * (column == 0 ? 1 : .5);
  box.borderRightWidth = vertical_[qsizetype(column + 1) * rows_ + row].width * (column + 1 == columns_ ? 1 : .5);
}

void CollapsedTableBorders::paint(QPainter& painter, const std::vector<BlockLayout::TableRowLayout>& rows,
                                  QPointF offset, const ThemeElementStyle* tableStyle) const {
  const auto device = painter.deviceTransform();
  const auto stripFor = [&](bool horizontal, int line, int segment, qreal width) {
    const int lines = horizontal ? rows_ : columns_;
    const bool last = line == lines;
    const int cell = last ? line - 1 : line;
    const auto rect = (horizontal ? rows[cell].cells[segment] : rows[segment].cells[cell]).rect.translated(offset);
    const auto start = last ? (horizontal ? rect.bottomLeft() : rect.topRight()) : rect.topLeft();
    return borderStrip(start, horizontal ? rect.width() : rect.height(), width, line == 0 ? 0 : (last ? 1 : .5),
                       horizontal, device);
  };
  const auto colorFor = [&](const Edge& edge, bool horizontal, int line) {
    if (!tableStyle || edge.origin != Origin::Table)
      return edge.color;
    const auto& box = tableStyle->box;
    return horizontal ? (line ? box.borderBottomColor : box.borderTopColor)
                      : (line ? box.borderRightColor : box.borderLeftColor);
  };
  // Vertical segments own the joins. Leave the junction out of the horizontal
  // stroke when the stroke above it can reveal a second coat of ink.
  const auto paintHorizontal = [&](const Edge& edge, int row, int column) {
    const auto strip = stripFor(true, row, column, edge.width);
    QPainterPath cuts;
    for (int c : {column, column + 1}) {
      for (int r = qMax(0, row - 1); r < qMin(rows_, row + 1); ++r) {
        const auto& cross = vertical_[qsizetype(c) * rows_ + r];
        if (cross.width > 0 && colorFor(cross, false, c).alpha() < 255)
          cuts.addRect(stripFor(false, c, r, cross.width));
      }
    }
    if (!cuts.isEmpty()) {
      QPainterPath area;
      area.addRect(strip);
      painter.save();
      painter.setClipPath(area.subtracted(cuts), Qt::IntersectClip);
    }
    paintBorderSide(painter, strip, colorFor(edge, true, row), edge.style, true);
    if (!cuts.isEmpty())
      painter.restore();
  };
  for (int r = 0; r <= rows_; ++r)
    for (int c = 0; c < columns_; ++c) {
      const auto& edge = horizontal_[qsizetype(r) * columns_ + c];
      if (edge.width > 0)
        paintHorizontal(edge, r, c);
    }
  for (int c = 0; c <= columns_; ++c)
    for (int r = 0; r < rows_; ++r) {
      const auto& edge = vertical_[qsizetype(c) * rows_ + r];
      if (edge.width > 0)
        paintBorderSide(painter, stripFor(false, c, r, edge.width), colorFor(edge, false, c), edge.style, false);
    }
}

} // namespace muffin
