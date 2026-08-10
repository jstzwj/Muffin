#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/mindmap/MindmapDiagram.h"
#include "mermaid/rough/RoughOps.h"

#include <QJsonValue>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::mindmap {

struct MindmapConfig {
  QJsonValue padding = 10.0;
  QJsonValue maxNodeWidth = 200.0;
  QJsonValue useMaxWidth = true;
  bool htmlLabels = true;
  bool markdownAutoWrap = true;
  quint32 handDrawnSeed = 7;
};

struct MindmapSceneStyle {
  QString themeName = QStringLiteral("default");
  QString look = QStringLiteral("classic");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  QString textColor = QStringLiteral("#333");
  QString mainBkg = QStringLiteral("#ECECFF");
  QString rootFill = QStringLiteral("#0000ff");
  QString rootTextColor = QStringLiteral("#333");
  QString nodeBorder = QStringLiteral("#9370DB");
  QString lineColor = QStringLiteral("#333");
  QString gradientStart = QStringLiteral("#0042eb");
  QString gradientStop = QStringLiteral("#eb0042");
  QString dropShadow = QStringLiteral("drop-shadow(2px 2px 0 rgba(0,0,0,.06))");
  qreal strokeWidth = 2.0;
  bool useGradient = true;
  qreal rawThemeColorLimit = 12.0;
  int themeColorRuleCount = 12;
  QVector<QString> cScale;
  QVector<QString> cScaleInv;
  QVector<QString> cScaleLabel;
};

struct MindmapLabelGeometry {
  QString source;
  flowchart::FlowLabelDocument document;
  // Mermaid sizes the node handler from an inline-layout box even when the
  // final SVG <text> ink box is narrower. CoSE must consume this box.
  QRectF layoutBounds;
  QRectF bounds;
  QString fill;
};

struct MindmapAnchorGeometry {
  QString href;
  QString label;
  QRectF bounds;
};

struct MindmapNodeGeometry {
  int id = -1;
  QString nodeId;
  int level = 0;
  int section = -1;
  QString shape;
  QString look;
  QPointF center;
  // The generic CoSE renderer measures the complete inserted node <g>, not
  // just the shape handler's own path. SVG labels can extend beyond an
  // explicitly-shaped node because most handlers leave their label at x=0.
  QRectF layoutBounds;
  QRectF localBounds;
  QRectF paintedBounds;
  QPainterPath shapePath;
  rough::Drawable roughDrawable;
  bool handDrawn = false;
  bool dropShadow = false;
  bool gradient = false;
  QString fill;
  QString stroke;
  qreal strokeWidth = 1.0;
  bool bottomLine = false;
  QString bottomLineStroke;
  qreal bottomLineWidth = 3.0;
  QVector<MindmapAnchorGeometry> anchors;
  MindmapLabelGeometry label;
};

struct MindmapEdgeGeometry {
  QString id;
  int start = -1;
  int end = -1;
  int depth = 0;
  int section = -1;
  QVector<QPointF> points;
  QString path;
  rough::Drawable roughDrawable;
  bool handDrawn = false;
  QRectF bounds;
  QString stroke;
  qreal strokeWidth = 1.0;
};

struct MindmapScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override {
    return QRectF(bounds.topLeft(), QSizeF(qRound(bounds.width()),
                                           qRound(bounds.height())));
  }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  const QVector<InteractionRegion>& interactionRegions() const override {
    return interactions;
  }

  QRectF bounds;
  QRectF contentBounds;
  bool useMaxWidth = true;
  QString effectiveLayout = QStringLiteral("cose-bilkent");
  MindmapConfig config;
  MindmapSceneStyle style;
  QVector<MindmapNodeGeometry> nodes;
  QVector<MindmapEdgeGeometry> edges;
  QVector<InteractionRegion> interactions;
};

MindmapScene buildMindmapScene(const MindmapData& data, MindmapConfig config,
                               MindmapSceneStyle style);

}  // namespace muffin::mermaid::mindmap
