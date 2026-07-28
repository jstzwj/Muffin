#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QRectF>

namespace muffin::mermaid::state {

struct StateSceneStyle {
  QString stateFill = QStringLiteral("#ECECFF");
  QString stateStroke = QStringLiteral("#9370DB");
  QString textColor = QStringLiteral("#333333");
  QString transitionColor = QStringLiteral("#333333");
  QString edgeLabelFill = QStringLiteral("#ECECFF");
  QString compositeFill = QStringLiteral("#ffffde");
  QString compositeStroke = QStringLiteral("#aaaa33");
  QString noteFill = QStringLiteral("#fff5ad");
  QString noteStroke = QStringLiteral("#aaaa33");
  QString noteTextColor = QStringLiteral("#000000");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  qreal lineHeight = 24.0;
  qreal strokeWidth = 1.0;
};

struct StateSceneNode {
  QString id;
  QString shape;
  QString label;
  QStringList descriptions;
  QString cssClasses;
  QStringList styles;
  QString fill;
  QString stroke;
  QString textColor;
  qreal strokeWidth = 1.0;
  QRectF bounds;
  bool group = false;
  flowchart::FlowLabelDocument labelDocument;
  QVector<flowchart::FlowLabelDocument> descriptionDocuments;
};
struct StateSceneEdge {
  QString id;
  QString start;
  QString end;
  QString label;
  QString markerEnd;
  QString classes;
  QVector<QPointF> points;
  QVector<QVector<QPointF>> segments;
  std::optional<QPointF> labelPosition;
  QString path;
  flowchart::FlowLabelDocument labelDocument;
  QSizeF labelSize;
  QRectF pathBounds;
  QRectF labelBounds;
};
struct StateScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QString role = QStringLiteral("graphics-document document");
  QString ariaRoleDescription = QStringLiteral("stateDiagram");
  QString arrowMarkerId = QStringLiteral("barbEnd");
  QSizeF arrowMarkerSize = QSizeF(20.0, 14.0);
  QPointF arrowMarkerRef = QPointF(19.0, 7.0);
  QString arrowMarkerOrient = QStringLiteral("auto");
  QRectF bounds;
  QVector<StateSceneNode> clusters;
  QVector<StateSceneEdge> edges;
  QVector<StateSceneNode> nodes;
  StateSceneStyle style;
  // handDrawn (rough) look — gated in the painter, only set when the diagram
  // config requests `look: handDrawn`. Default rendering is unaffected.
  bool handDrawn = false;
  quint32 handDrawnSeed = 0;
};

StateScene buildStateScene(const StateLayoutInput& input,
                           const StatePlacementResult& placement,
                           StateSceneStyle style = {});

}  // namespace muffin::mermaid::state
