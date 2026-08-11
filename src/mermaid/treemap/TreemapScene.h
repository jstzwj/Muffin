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
  QString textColor = QStringLiteral("#333");
  QString titleColor = QStringLiteral("#333");
  qreal titleFontSize = 14.0;
  QString cScale[12];
  QString cScalePeer[12];
  QString cScaleLabel[12];
};

enum class TreemapTextBaseline { Middle, Hanging };

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
                               TreemapSceneStyle style);

} // namespace muffin::mermaid::treemap
