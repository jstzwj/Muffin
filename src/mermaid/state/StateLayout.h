#pragma once

#include "mermaid/state/StateDiagram.h"

#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <optional>

namespace muffin::mermaid::state {

struct StateLayoutNodeInput {
  QString id;
  QString shape;
  QJsonValue label;
  QString parentId;
  QString direction;
  bool explicitDirection = false;
  bool isGroup = false;
  QString cssClasses;
  QStringList cssStyles;
  // classDef (matching cssClasses) + inline `style` cascade, merged last-wins —
  // the exact declarations compileStyles (chunk-BNCO5QFQ.mjs) feeds the renderer.
  // NOT serialized by stateLayoutInputToJson: the upstream-comparable layout
  // contract keeps cssStyles as inline-only; the merged cascade is render state.
  QStringList styles;
  QString position;
  QJsonValue description = QJsonValue::Null;
};
struct StateLayoutEdgeInput {
  QString id;
  QString start;
  QString end;
  QJsonValue label = QJsonValue::Null;
  QString arrowhead = QStringLiteral("normal");
  QString arrowTypeEnd = QStringLiteral("arrow_barb");
  QString style = QStringLiteral("fill:none");
  QString labelStyle;
  QString thickness = QStringLiteral("normal");
  QString classes = QStringLiteral("transition");
  QStringList linkStyles;  // linkStyle declarations (key:value), applied at paint
};
struct StateLayoutInput {
  QString direction = QStringLiteral("TB");
  QVector<StateLayoutNodeInput> nodes;
  QVector<StateLayoutEdgeInput> edges;
};

struct StateLayoutMeasurements {
  QMap<QString, QSizeF> nodes;
  QMap<QString, QSizeF> paintedNodes;
  QMap<QString, QSizeF> edgeLabels;
};
struct StatePlacementNode {
  QString id;
  QPointF center;
  QSizeF size;
  QSizeF paintedSize;
  int rank = 0;
};
struct StatePlacementEdge {
  QString id;
  QString path;
  QVector<QPointF> points;
  QVector<QVector<QPointF>> segments;
  std::optional<QPointF> labelPosition;
};
struct StatePlacementCluster {
  QString id;
  QPointF center;
  QSizeF size;
};
struct StatePlacementResult {
  QVector<StatePlacementNode> nodes;
  QVector<StatePlacementEdge> edges;
  QVector<StatePlacementCluster> clusters;
};

StateLayoutInput buildStateLayoutInput(const StateDiagramData& data,
                                       QString look = QStringLiteral("classic"));
QJsonObject stateLayoutInputToJson(const StateLayoutInput& input);
StateLayoutMeasurements measureStateLayoutInput(
    const StateLayoutInput& input, QString fontFamily = QStringLiteral("Noto Sans"),
    qreal fontSize = 16.0);
StatePlacementResult layoutStateDiagramDagre(
    const StateLayoutInput& input, const StateLayoutMeasurements& measurements,
    qreal nodeSpacing = 50.0, qreal rankSpacing = 50.0);

}  // namespace muffin::mermaid::state
