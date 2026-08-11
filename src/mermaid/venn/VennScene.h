#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/rough/RoughOps.h"
#include "mermaid/venn/VennDiagram.h"
#include "mermaid/venn/VennLayout.h"

#include <QJsonValue>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace muffin::mermaid::venn {

struct VennConfig {
  QJsonValue width = 800.0;
  QJsonValue height = 450.0;
  QJsonValue padding = 8.0;
  QJsonValue useMaxWidth = true;
  QJsonValue useDebugLayout = false;
  QJsonValue handDrawnSeed = 0.0;
};

struct VennSceneStyle {
  QString look = QStringLiteral("classic");
  QString fontFamily = QStringLiteral("Noto Sans");
  QString background = QStringLiteral("#f4f4f4");
  QString primaryColor = QStringLiteral("#ECECFF");
  QString primaryTextColor = QStringLiteral("#333");
  QString textColor = QStringLiteral("#333");
  QString titleColor = QStringLiteral("#333");
  QString vennTitleTextColor = QStringLiteral("#333");
  QString vennSetTextColor = QStringLiteral("#333");
  QStringList colors;
};

struct VennTextGeometry {
  QString cssClass;
  QString source;
  QStringList lines;
  QPointF position;
  qreal fontSize = 16.0;
  qreal firstDyEm = 0.0;
  qreal lineHeightEm = 1.1;
  QString fill;
  bool middle = true;
};

struct VennAreaGeometry {
  VennSubset data;
  QString key;
  QString cssClass;
  QString rawPath;
  QPainterPath path;
  QVector<layout::Circle> circles;
  QPointF textCenter;
  VennTextGeometry label;
  QString fill;
  QString stroke;
  qreal fillOpacity = 0.0;
  qreal strokeOpacity = 1.0;
  qreal strokeWidth = 1.0;
  bool circle = false;
  bool rough = false;
  rough::Drawable roughDrawable;
};

struct VennTextNodeGeometry {
  QString areaKey;
  QString id;
  QString source;
  QRectF box;
  QString color;
  qreal fontSize = 16.0;
};

struct VennDebugCircle {
  QPointF center;
  qreal radius = 0.0;
  qreal strokeWidth = 1.0;
};

struct VennDebugCell {
  QRectF rect;
  qreal strokeWidth = 1.0;
};

struct VennScene final : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return rasterBounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds{0.0, 0.0, 800.0, 450.0};
  QRectF rasterBounds{0.0, 0.0, 800.0, 450.0};
  QString viewBoxAttribute = QStringLiteral("0 0 800 450");
  bool useMaxWidth = true;
  bool useDebugLayout = false;
  qreal scale = 0.5;
  qreal titleHeight = 0.0;
  QString title;
  QString accTitle;
  QString accDescr;
  VennConfig config;
  VennSceneStyle style;
  VennTextGeometry titleText;
  QVector<VennAreaGeometry> areas;
  QVector<VennTextNodeGeometry> textNodes;
  QVector<VennDebugCircle> debugCircles;
  QVector<VennDebugCell> debugCells;
};

VennScene buildVennScene(const VennData& data, VennConfig config,
                         VennSceneStyle style);

}  // namespace muffin::mermaid::venn
