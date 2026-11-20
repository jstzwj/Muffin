#pragma once

#include "render/BlockLayout.h"

namespace muffin {

// One winner per grid segment, shared by intrinsic sizing, cell layout and paint.
class CollapsedTableBorders {
public:
  CollapsedTableBorders(const MarkdownNode& table, const RenderTheme& theme, const ThemeElementBoxStyle& tableBox,
                        int columns);

  void applyCellInsets(ThemeElementBoxStyle& box, int row, int column) const;
  void paint(QPainter& painter, const std::vector<BlockLayout::TableRowLayout>& rows, QPointF offset,
             const ThemeElementStyle* tableStyle) const;
  bool rowGroupStylesMatch(const MarkdownNode& table, const RenderTheme& theme) const;

private:
  enum class Origin { Table, RowGroup, Row, Cell };
  struct Edge {
    qreal width = 0;
    QColor color;
    QString style;
    Origin origin = Origin::Table;

    void consider(const Edge& other);
  };

  void addBox(const ThemeElementBoxStyle& box, Origin origin, int firstRow, int endRow, int firstCol, int endCol,
              qreal opacity = 1);
  int rows_, columns_;
  // Indexed by grid line first, position along that line second.
  QVector<Edge> horizontal_, vertical_;
  std::vector<std::pair<int, quint64>> rowGroupStyles_;
};

} // namespace muffin
