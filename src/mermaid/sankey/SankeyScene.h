#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/sankey/SankeyDiagram.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QPainterPath>
#include <QRectF>
#include <QStringList>
#include <QVector>

namespace muffin::mermaid::sankey {

struct SankeyConfig {
  QJsonValue width = 600.0;
  QJsonValue height = 400.0;
  QJsonValue useMaxWidth = true;
  QJsonValue linkColor = QStringLiteral("gradient");
  QJsonValue nodeAlignment = QStringLiteral("justify");
  QJsonValue showValues = true;
  QJsonValue prefix = QString();
  QJsonValue suffix = QString();
  QJsonValue nodeWidth = 10.0;
  QJsonValue nodePadding = 12.0;
  QJsonValue labelStyle = QStringLiteral("legacy");
  QJsonObject nodeColors;
};

struct SankeySceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  QString textColor = QStringLiteral("#333");
  QString mainBkg = QStringLiteral("#fff");
  QString background = QStringLiteral("#fff");
};

struct SankeyNodeGeometry {
  QString id;
  int index = 0;
  int depth = 0;
  int height = 0;
  int layer = 0;
  qreal value = 0.0;
  qreal x0 = 0.0;
  qreal x1 = 0.0;
  qreal y0 = 0.0;
  qreal y1 = 0.0;
  QString color;
};

struct SankeyLinkGeometry {
  int index = 0;
  int source = 0;
  int target = 0;
  qreal value = 0.0;
  qreal width = 0.0;
  qreal y0 = 0.0;
  qreal y1 = 0.0;
  QPainterPath path;
  QString pathData;
  QString stroke;
  QString sourceColor;
  QString targetColor;
};

struct SankeyLabelGeometry {
  int node = 0;
  QString text;
  QPointF position;
  QString anchor;
  qreal dyEm = 0.0;
  QRectF bounds;
  bool outlined = false;
  bool backgroundLayer = false;
  QString fill;
  QString stroke;
  qreal strokeWidth = 0.0;
};

struct SankeyScene final : MermaidScene {
  QRectF bounds;
  QRectF rasterBounds;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  qreal configuredWidth = 600.0;
  qreal configuredHeight = 400.0;
  bool outlinedLabels = false;
  SankeySceneStyle style;
  QVector<SankeyNodeGeometry> nodes;
  QVector<SankeyLinkGeometry> links;
  QVector<SankeyLabelGeometry> labels;

  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return rasterBounds; }
  void paint(QPainter &painter,
             const MermaidPaintOptions &options = {}) const override;
  QJsonObject toJsonObject() const override;
};

SankeyScene buildSankeyScene(const SankeyData &data, SankeyConfig config,
                             SankeySceneStyle style);

} // namespace muffin::mermaid::sankey
