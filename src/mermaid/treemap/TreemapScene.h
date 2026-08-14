#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/treemap/TreemapDiagram.h"

#include <QJsonValue>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::treemap {

struct TreemapConfig {
  QJsonValue useMaxWidth = true;
  QJsonValue padding = 10.0;
  QJsonValue diagramPadding = 8.0;
  QJsonValue showValues = true;
  QJsonValue nodeWidth = 100.0;
  QJsonValue nodeHeight = 40.0;
  QJsonValue valueFormat = QStringLiteral(",");
};

struct TreemapSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QString textColor = QStringLiteral("#333");
  QString titleColor = QStringLiteral("#333");
  qreal titleFontSize = 14.0;
  QString cScale[12];
  QString cScalePeer[12];
  QString cScaleLabel[12];
};

enum class TreemapTextBaseline { Middle, Hanging };

// themeCSS overlay for one treemap DOM element. Label and value font sizes
// are inline styles written by the upstream shrink loops, so non-important
// themeCSS never moves them — only the final svg.getBBox (viewBox) and the
// .treemapTitle base rule (font-size feeds the title ink box) create
// geometry feedback. `hasBox` carries the display-only gate for that bbox.
struct TreemapElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString fontFamily;
  qreal fontSize = -1.0;
  QString fontWeight;
  QString fontStyle;
  qreal opacity = -1.0;
  qreal fillOpacity = -1.0;
  qreal strokeOpacity = -1.0;
  bool visible = true;
  bool hasBox = true;
  bool measures = true;
};

// Slots are indexed by emission order (sections then leaves, matching the
// builder's emission which mirrors upstream's branch/leaf node order).
struct TreemapCssOverrides {
  struct Section {
    TreemapElementCss group;
    TreemapElementCss header;
    TreemapElementCss rect;
    TreemapElementCss label;
    TreemapElementCss value;
  };
  struct Leaf {
    TreemapElementCss group;
    TreemapElementCss rect;
    TreemapElementCss label;
    TreemapElementCss value;
  };
  bool active = false;
  QVector<Section> sections;
  QVector<Leaf> leaves;
  TreemapElementCss title;
};

struct TreemapTextGeometry {
  QString role;
  QString text;
  QPointF position;
  QRectF bounds;
  QRectF clip;
  qreal fontSize = 12.0;
  bool bold = false;
  bool italic = false;
  bool visible = true;
  QString anchor = QStringLiteral("start");
  TreemapTextBaseline baseline = TreemapTextBaseline::Middle;
  QString fill;
  TreemapElementCss css;
};

struct TreemapSectionGeometry {
  int node = -1;
  int depth = 0;
  QRectF rect;
  QString fill;
  QString stroke;
  qreal fillOpacity = 0.6;
  qreal strokeOpacity = 0.4;
  qreal strokeWidth = 2.0;
  QString classSelector;
  TreemapTextGeometry label;
  TreemapTextGeometry value;
  TreemapElementCss groupCss;
  TreemapElementCss headerCss;
  TreemapElementCss rectCss;
};

struct TreemapLeafGeometry {
  int node = -1;
  QRectF rect;
  QRectF clip;
  QString fill;
  QString stroke;
  qreal fillOpacity = 0.3;
  qreal strokeWidth = 3.0;
  QString classSelector;
  TreemapTextGeometry label;
  TreemapTextGeometry value;
  TreemapElementCss groupCss;
  TreemapElementCss rectCss;
};

struct TreemapScene final : MermaidScene {
  QRectF bounds;
  QRectF rasterBounds;
  QRectF contentBounds;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  qreal configuredWidth = 1000.0;
  qreal configuredHeight = 400.0;
  TreemapSceneStyle style;
  TreemapTextGeometry title;
  QVector<TreemapSectionGeometry> sections;
  QVector<TreemapLeafGeometry> leaves;

  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return rasterBounds; }
  void paint(QPainter &painter,
             const MermaidPaintOptions &options = {}) const override;
  QJsonObject toJsonObject() const override;
};

TreemapScene buildTreemapScene(const TreemapData &data, TreemapConfig config,
                               TreemapSceneStyle style,
                               const TreemapCssOverrides *css = nullptr);

} // namespace muffin::mermaid::treemap
