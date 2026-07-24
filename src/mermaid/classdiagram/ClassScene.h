#pragma once

#include "mermaid/classdiagram/ClassLayout.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QMap>
#include <QRectF>

namespace muffin::mermaid::classdiagram {

struct ClassSceneStyle {
  QString classFill = QStringLiteral("#ECECFF");
  QString classStroke = QStringLiteral("#9370DB");
  QString textColor = QStringLiteral("#131300");
  QString lineColor = QStringLiteral("#333333");
  QString edgeLabelFill = QStringLiteral("#ECECFF");
  QString noteFill = QStringLiteral("#fff5ad");
  QString noteStroke = QStringLiteral("#aaaa33");
  QString noteTextColor = QStringLiteral("#000000");
  QString clusterFill = QStringLiteral("#ffffde");
  QString clusterStroke = QStringLiteral("#aaaa33");
  QString titleColor = QStringLiteral("#333333");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  qreal lineHeight = 24.0;
  qreal strokeWidth = 1.0;
};

struct ClassSceneLabel {
  QString text;
  QString cssStyle;
  QPointF center;
  QSizeF size;
  bool svgText = false;
  flowchart::FlowLabelDocument document;
};

struct ClassSceneNode {
  QString id;
  QString shape;
  QString cssClasses;
  QPointF center;
  QSizeF size;
  QRectF localOuter;
  QVector<QRectF> localDividers;
  QVector<ClassSceneLabel> annotationLabels;
  QVector<ClassSceneLabel> nameLabels;
  QVector<ClassSceneLabel> memberLabels;
  QVector<ClassSceneLabel> methodLabels;
  QStringList cssStyles;
  QStringList styles;
  QString fill;
  QString stroke;
  QString textColor;
  qreal strokeWidth = 1.0;
};

struct ClassSceneTerminalLabel {
  QString text;
  QPointF center;
  QSizeF size;
  flowchart::FlowLabelDocument document;
};

struct ClassSceneEdge {
  QString id;
  QString pattern;
  QString classes;
  QString markerStart;
  QString markerEnd;
  QVector<QPointF> points;
  QVector<QPointF> renderedPoints;
  QVector<QVector<QPointF>> segments;
  QVector<QVector<QPointF>> renderedSegments;
  QString path;
  QStringList paths;
  QString label;
  std::optional<QPointF> labelPosition;
  flowchart::FlowLabelDocument labelDocument;
  QSizeF labelSize;
  QRectF pathBounds;
  QRectF labelBounds;
  std::optional<ClassSceneTerminalLabel> startLabelRight;
  std::optional<ClassSceneTerminalLabel> endLabelLeft;
  QStringList style;
  QStringList labelStyle;
};

struct ClassSceneCluster {
  QString id;
  QString label;
  QRectF bounds;
  ClassSceneLabel titleLabel;
};

struct ClassMarkerChild {
  QString tag;
  QString path;
  QString points;
  qreal cx = 0.0;
  qreal cy = 0.0;
  qreal radius = 0.0;
  QString fill;
  QString style;
  QString strokeWidth;
};

struct ClassMarkerDefinition {
  QString type;
  QString suffix;
  QString cssClass;
  qreal refX = 0.0;
  qreal refY = 0.0;
  qreal markerWidth = 0.0;
  qreal markerHeight = 0.0;
  QString markerUnits;
  QString orient = QStringLiteral("auto");
  QString viewBox;
  ClassMarkerChild child;
};

struct ClassScene {
  QRectF bounds;
  QVector<ClassSceneCluster> clusters;
  QVector<ClassSceneEdge> edges;
  QVector<ClassSceneNode> nodes;
  QVector<ClassMarkerDefinition> markers;
  ClassSceneStyle style;
  // handDrawn (rough) look — gated in the painter, only set when the diagram
  // config requests `look: handDrawn`. Default rendering is unaffected.
  bool handDrawn = false;
  quint32 handDrawnSeed = 0;
};

QVector<ClassMarkerDefinition> classMarkerDefinitions();

ClassScene buildClassScene(const ClassLayoutInput& input,
                           const QVector<ClassBoxGeometry>& boxes,
                           const ClassLayoutMeasurements& measurements,
                           const ClassPlacementResult& placement,
                           ClassSceneStyle style = {});

}  // namespace muffin::mermaid::classdiagram
