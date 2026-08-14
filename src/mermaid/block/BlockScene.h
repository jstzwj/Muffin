#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/block/BlockDiagram.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonValue>
#include <QRectF>

namespace muffin::mermaid::block {

struct BlockConfig {
  QJsonValue useMaxWidth = true;
  QJsonValue padding = 8.0;
  bool htmlLabels = true;
  QString look;
  quint32 handDrawnSeed = 0;
  QString svgId = QStringLiteral("block-native");
};

struct BlockNodeGeometry {
  QString id;
  QString type;
  QString label;
  QPointF center;
  QSizeF layoutSize;
  QSizeF paintSize;
  QRectF bounds;
};

struct BlockEdgeGeometry {
  QString id;
  QString start;
  QString end;
  QString path;
  QVector<QPointF> points;
  QRectF labelBounds;
};

struct BlockScene final : MermaidScene {
  QRectF bounds;
  QRectF rasterBounds;
  QRectF contentBounds;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  QString fontFamily;
  flowscene::FlowScene flow;
  QVector<BlockNodeGeometry> nodes;
  QVector<BlockEdgeGeometry> edges;

  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return rasterBounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options = {}) const override;
  QJsonObject toJsonObject() const override;
  SvgMarkerProjection svgMarkerProjection() const override {
    return flow.svgMarkerProjection();
  }
};

BlockScene buildBlockScene(const BlockData& data, BlockConfig config,
                           const flowtheme::FlowThemeVariables& theme,
                           const csscascade::FlowchartProjection* measurementCss = nullptr,
                           const csscascade::FlowchartProjection* paintCss = nullptr);

}  // namespace muffin::mermaid::block
