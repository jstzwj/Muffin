#pragma once

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
struct StateScene {
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
};

StateScene buildStateScene(const StateLayoutInput& input,
                           const StatePlacementResult& placement,
                           StateSceneStyle style = {});

}  // namespace muffin::mermaid::state
