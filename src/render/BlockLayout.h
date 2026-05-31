#pragma once

#include "document/MarkdownNode.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

namespace muffin {

class BlockLayout {
public:
  struct TableCellLayout {
    QRectF rect;
    InlineLayout text;
    bool header = false;
  };

  struct TableRowLayout {
    QRectF rect;
    std::vector<TableCellLayout> cells;
  };

  explicit BlockLayout(NodeId id = {});

  NodeId nodeId() const;
  BlockType type() const;
  void setType(BlockType type);

  QRectF rect() const;
  void setRect(QRectF rect);
  void translateY(qreal dy);

  qreal height() const;
  qreal bottom() const;

  void setInlineLayout(std::unique_ptr<InlineLayout> layout);
  InlineLayout* inlineLayout();
  const InlineLayout* inlineLayout() const;

  void setLiteral(QString literal);
  QString literal() const;

  void setHeadingLevel(int level);
  int headingLevel() const;

  void setListMarker(QString marker);
  QString listMarker() const;

  void setDepth(int depth);
  int depth() const;

  void setChildren(std::vector<std::unique_ptr<BlockLayout>> children);
  std::vector<std::unique_ptr<BlockLayout>>& children();
  const std::vector<std::unique_ptr<BlockLayout>>& children() const;

  void setTableRows(std::vector<TableRowLayout> rows);
  std::vector<TableRowLayout>& tableRows();
  const std::vector<TableRowLayout>& tableRows() const;

  void paint(QPainter& painter, const RenderTheme& theme, qreal scrollY) const;
  bool intersects(const QRectF& documentViewport) const;

private:
  void paintSelf(QPainter& painter, const RenderTheme& theme, qreal scrollY) const;
  void paintTable(QPainter& painter, const RenderTheme& theme, qreal scrollY) const;

  NodeId id_;
  BlockType type_ = BlockType::Unknown;
  QRectF rect_;
  std::unique_ptr<InlineLayout> inlineLayout_;
  QString literal_;
  int headingLevel_ = 0;
  QString listMarker_;
  int depth_ = 0;
  std::vector<std::unique_ptr<BlockLayout>> children_;
  std::vector<TableRowLayout> tableRows_;
};

}  // namespace muffin
