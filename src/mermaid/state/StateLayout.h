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
};
struct StateLayoutInput {
  QString direction = QStringLiteral("TB");
  QVector<StateLayoutNodeInput> nodes;
  QVector<StateLayoutEdgeInput> edges;
};

// Per-element themeCSS feedback for measurement: label font (empty/0 keeps
// the shared font) and the `.node rect { display:none }` box removal.
struct StateMeasureCss {
  QString fontFamily;
  qreal fontSize = 0.0;
  bool shapeHidden = false;
  // display:none on the label's <p> collapses the LABEL BOX (the fo renders
  // nothing): a node shrinks to its padding-only rect (16x16, browser-
  // verified), a cluster title band to zero, and an edge label reserves no
  // space. visibility keeps the box — paint-only.
  bool labelHidden = false;
  // rectWithTitle description rows are their OWN <p> (the second fo): their
  // computed font measures the rows independently of the title's.
  QString descFontFamily;
  qreal descFontSize = 0.0;
  // display:none on the DESC p collapses the description block in the LAYOUT:
  // the second fo's div measures 0x0 and a 0x0 foreignObject is EXCLUDED from
  // label.getBBox(), so the titled node's dagre box is the TITLE alone
  // (browser: 64.921875x65 -> x32; vertical add-on drops 17 -> 8 — the 9px
  // title-rows gap only exists while the rows render). The divider line
  // STILL paints (its own element, still inside the title-only box).
  bool descHidden = false;
};

struct StateLayoutMeasurements {
  QMap<QString, QSizeF> nodes;
  QMap<QString, QSizeF> paintedNodes;
  QMap<QString, QSizeF> edgeLabels;
  QMap<QString, QSizeF> clusterLabels;
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
  QSizeF logicalSize;
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
    qreal fontSize = 16.0, bool handDrawn = false,
    quint32 handDrawnSeed = 0,
    // shapeHidden: the legacy global `.node rect { display:none }` fold —
    // plain nodes measure as the label bbox alone (no shape padding). The
    // per-node css hash refines it element-by-element.
    bool shapeHidden = false,
    const QHash<QString, StateMeasureCss>* nodeCss = nullptr,
    const QHash<QString, StateMeasureCss>* edgeLabelCss = nullptr);
StatePlacementResult layoutStateDiagramDagre(
    const StateLayoutInput& input, const StateLayoutMeasurements& measurements,
    qreal nodeSpacing = 50.0, qreal rankSpacing = 50.0,
    bool handDrawn = false, quint32 handDrawnSeed = 0);

}  // namespace muffin::mermaid::state
