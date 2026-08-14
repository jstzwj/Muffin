#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/rough/RoughOps.h"
#include "mermaid/theme/MermaidStyleResolve.h"

#include <QRectF>

namespace muffin::mermaid::state {

struct StateSceneStyle {
  QString stateFill = QStringLiteral("#ECECFF");
  QString stateStroke = QStringLiteral("#9370DB");
  QString textColor = QStringLiteral("#333333");
  QString transitionColor = QStringLiteral("#333333");
  QString edgeLabelFill = QStringLiteral("#ECECFF");
  QString compositeFill = QStringLiteral("white");
  QString compositeAltFill = QStringLiteral("#f0f0f0");
  QString compositeTitleFill = QStringLiteral("#ECECFF");
  QString compositeStroke = QStringLiteral("#9370DB");
  QString noteFill = QStringLiteral("#fff5ad");
  QString noteStroke = QStringLiteral("#aaaa33");
  QString noteTextColor = QStringLiteral("#000000");
  // 11.16 rendering-util shapes: the start circle's `.node circle.state-start`
  // fill/stroke (specialStateColor) and the end-state inner dot
  // (`stateBorder ?? nodeBorder`). The end ring keeps transitionColor stroke
  // and takes the node fill (userNodeOverrides' mainBkg default).
  QString specialStateColor;
  QString endInnerFill;
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  qreal lineHeight = 24.0;
  qreal strokeWidth = 1.0;

  // themeCSS `.node rect { display:none }` — the modeled single rect shape is
  // removed from every plain state node (label-only bbox, no painted rect).
  bool shapeVisible = true;};

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
  // themeCSS `.node rect { display:none }` removes the shape box: the node
  // keeps its text (group bbox = label) and the painter skips the rect.
  bool shapeVisible = true;
  QRectF bounds;
  QRectF innerBounds;
  QString innerFill;
  bool group = false;
  flowchart::FlowLabelDocument labelDocument;
  QVector<flowchart::FlowLabelDocument> descriptionDocuments;
  QVector<rough::Drawable> roughDrawables;
  QRectF paintedBounds;
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
  // Resolved edge paint (linkStyle / edge classDef via MermaidStyleResolve);
  // empty when no linkStyle applies — the painter falls back to scene.style.
  QString stroke;
  QString strokeWidth;
  QString strokeDasharray;
  rough::Drawable roughDrawable;
};
struct StateScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  SvgMarkerProjection svgMarkerProjection() const override;

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
                           StateSceneStyle style = {},
                           const QVector<style::ClassDef>& classDefs = {},
                           const style::ThemeDefaults& themeDefaults = {},
                           bool handDrawn = false,
                           quint32 handDrawnSeed = 0);

}  // namespace muffin::mermaid::state
