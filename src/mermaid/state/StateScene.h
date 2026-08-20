#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/rough/RoughOps.h"
#include "mermaid/theme/MermaidStyleResolve.h"

#include <QRectF>

namespace muffin::mermaid::state {

// themeCSS projection for one DOM element (11.16 state DOM: shapes are
// rects/circles (rect, rectWithTitle, stateStart) or rough double-path pairs
// (note/choice/fork/stateEnd — fill path + stroke path), and the label span
// chain g.label > rect + fo > div > span > p separates shape from text).
// Empty strings / zeros keep the resolved-theme slot (no author declaration
// reached the element).
struct StateElementCss {
  QString fill;
  QString stroke;
  qreal strokeWidthPx = 0.0;   // meaningful only when strokeWidthSet
  // Distinguishes "no author width" from a declared `stroke-width: 0`
  // (which must disable the pen, not fall back to the theme width).
  bool strokeWidthSet = false;
  // display:none (or a hidden ancestor) removes the box: layout collapses
  // (`.node rect { display:none }` -> 0x0 dagre node) and nothing paints.
  bool displayed = true;
  // visibility:hidden/collapse only removes paint — the box (and text
  // metrics) stay, so a hidden `.nodeLabel` keeps the frame visible.
  bool painted = true;
  // Effective element opacity (ancestor chain × own). The paint channel's
  // composite alpha is color.alpha × opacity × the per-channel factor below
  // — exactly the browser's used-value composition for SVG shapes.
  qreal opacity = 1.0;
  // PURE per-channel CSS opacity factors (fill-opacity / stroke-opacity,
  // declared values — the engine's effective{Fill,Stroke}Opacity already
  // fold `opacity` in, so consumers multiply it in exactly once).
  qreal fillOpacity = 1.0;
  qreal strokeOpacity = 1.0;
  // Label channels (span/p elements): color is the CSS color channel, the
  // font pair drives both measurement feedback and painting.
  QString color;
  QString fontFamily;
  qreal fontSize = 0.0;        // 0 = no author font-size
  // p background-color (html edge labels) — "transparent"/alpha 0 paints
  // nothing (CSS initial), any other value replaces the theme slot.
  QString backgroundColor;
};

struct StateSceneStyle {
  QString stateFill = QStringLiteral("#ECECFF");
  QString stateStroke = QStringLiteral("#9370DB");
  QString textColor = QStringLiteral("#333333");
  QString transitionColor = QStringLiteral("#333333");
  QString edgeLabelFill = QStringLiteral("#ECECFF");
  // `.edgeLabel p { background-color: edgeLabelBackground }` — html edge
  // labels paint their background on the <p> (the SVG-label `rect` rule with
  // its 0.5 opacity is dead for state: labels are foreignObjects). The
  // variable's own alpha (default rgba(232,232,232,0.8)) is the whole story.
  QString edgeLabelBackground = QStringLiteral("rgba(232, 232, 232, 0.8)");
  // `.stateLabel text`/`.state-title` (stateLabelColor) and `.edgeLabel .label
  // text` (transitionLabelColor, `|| tertiaryTextColor`) — resolved-theme
  // slots distinct from textColor (defaults agree for every built-in theme).
  QString stateLabelColor;
  QString transitionLabelColor;
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
  // look: "neo" — the barbNeo marker geometry plus the
  // `[data-look="neo"].statediagram-cluster rect` rules (fill mainBkg, rx
  // themeVariables.radius). `radius` of 0 keeps rx 5 via the base rule.
  bool neo = false;
  qreal radius = 0.0;
  // The theme's dropShadow variable, verbatim (e.g.
  // "drop-shadow( 1px 2px 2px rgba(185,185,185,1))"; the redux-dark family
  // carries "url(#drop-shadow)"). The common neo sheet applies it as a
  // `filter:` on `.node rect`, `.node circle`, `.node .outer-path`, and the
  // state sheet on `.statediagram-cluster rect.outer`; the painter parses
  // the string (dx/dy/blur radius/color) once per paint.
  QString shadowCss;

  // themeCSS `.node rect { display:none }` legacy global fold (per-node css
  // refines it): every plain state node drops its shape box.
  bool shapeVisible = true;};

// A click link: upstream wraps the rendered node's <g> in an <a xlink:href>
// after layout (with a title tooltip on the anchor and the node).
struct StateSceneLink {
  QString stateId;
  QString url;
  QString tooltip;
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
  // themeCSS `.node rect { display:none }` removes the shape box: the node
  // keeps its text (group bbox = label) and the painter skips the rect.
  bool shapeVisible = true;
  // Measured title-label height (rectWithTitle/cluster placement mirrors the
  // upstream titleBox: divider at titleHeight + padding/2, description block
  // at titleHeight + padding/2 + 5, label group 3px below the rect top).
  qreal titleHeight = 0.0;
  StateElementCss shapeCss;   // shape element (rect / circle / rough fill path)
  StateElementCss shapeStrokeCss;  // rough stroke path (note/choice/fork/end)
  StateElementCss labelCss;   // span.nodeLabel
  // The label's <p> (INSIDE the span — the text wrapper): the text's used
  // font/color resolve here (it folds the span chain by inheritance), and its
  // display/visibility hide the text exactly like the fo's content.
  StateElementCss labelTextCss;
  // rectWithTitle description foreignObject (the second fo after g.label):
  // display/visibility gate the description rows' paint.
  StateElementCss descriptionCss;
  // The description rows' own <p>: its font/color are the rows' used style —
  // the titled node also MEASURES the rows with it (it grows when larger).
  StateElementCss descriptionTextCss;
  // rectWithTitle's line.divider (own DOM element with own computed style —
  // `.statediagram-state .divider { stroke: stateBorder }` base).
  StateElementCss dividerCss;
  // stateEnd inner dot (fill/stroke paths of the rc.circle pair).
  StateElementCss innerCss;
  StateElementCss innerStrokeCss;
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
  // Resolved edge paint. State transitions carry no linkStyle/classDef
  // channel upstream (the grammar has no linkStyle production and edges get
  // no compiled class styles), so these are theme/themeCSS slots only —
  // empty when nothing overrides; the painter falls back to scene.style.
  QString stroke;
  QString strokeWidth;
  QString strokeDasharray;
  StateElementCss shapeCss;   // path.transition (+ .note-edge)
  StateElementCss labelCss;   // span.edgeLabel
  // The label <p> INSIDE the span (span > p > text): the html label paints
  // its background on the p, and the p's OWN opacity/display/visibility gate
  // the whole label — the span's slots do not fold a descendant.
  StateElementCss labelBackgroundCss;
  QString labelBackground;    // p background-color (CSS spelling)
  rough::Drawable roughDrawable;
};
struct StateScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  SvgMarkerProjection svgMarkerProjection() const override;
  // Chromium element screenshots snap the fractional client box to the
  // NEAREST device pixel — the production raster follows the same rule as
  // the state pixel oracle (qCeil inflated every fractional box by 1px).
  bool roundRasterExtentToNearestPixel() const override { return true; }
  // The exported root carries the exact fractional getBBox-derived viewBox
  // (e.g. a 76.96875-tall diagram), not the raster-rounded canvas ints.
  QRectF svgClientViewBox() const override { return bounds; }
  const QVector<InteractionRegion>& interactionRegions() const override {
    return interactionRegions_;
  }

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
  QVector<InteractionRegion> interactionRegions_;  // click links (precomputed)
  // The referenced marker path (classic barbEnd / neo -margin clone): its
  // computed fill/stroke is the raster arrowhead color (theme
  // transitionColor via `defs [id$="-barbEnd"]`, refined by themeCSS).
  StateElementCss markerCss;
  StateSceneStyle style;
  // handDrawn (rough) look — gated in the painter, only set when the diagram
  // config requests `look: handDrawn`. Default rendering is unaffected.
  bool handDrawn = false;
  quint32 handDrawnSeed = 0;
};

StateScene buildStateScene(const StateLayoutInput& input,
                           const StatePlacementResult& placement,
                           StateSceneStyle style = {},
                           const style::ThemeDefaults& themeDefaults = {},
                           bool handDrawn = false,
                           quint32 handDrawnSeed = 0,
                           const QVector<StateSceneLink>& links = {},
                           // themeCSS label-font feedback for the scene's own
                           // text metrics (titleHeight, description rows,
                           // edge-label boxes): ids of nodes AND clusters in
                           // the first hash, edge ids in the second. Empty
                           // entries keep the scene font.
                           const QHash<QString, StateMeasureCss>* labelCssFonts = nullptr,
                           const QHash<QString, StateMeasureCss>* edgeLabelCssFonts = nullptr);

}  // namespace muffin::mermaid::state
