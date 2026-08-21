#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QVector>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

// themeCSS resolution against the REAL 11.16 state DOM (render() emits
// g.root > [g.clusters, g.edgePaths, g.edgeLabels, g.nodes]; clusters carry
// rect.outer/rect.inner/rect.divider plus a cluster-label span; nodes carry
// shape-specific rect/polygon/circle/path children plus a label span; edges
// are path.transition and their labels fo/div/span/p stacks). Structural
// selectors (nth-of-type, sibling, id suffix) therefore resolve exactly like
// the browser instead of one folded virtual node. Keys are
// "<kind>!<semantic id>!<slot>".
struct StateCssModel {
  QHash<QString, csscascade::ElementStyle> nodeShapes;
  QHash<QString, csscascade::ElementStyle> nodeShapeStrokes;
  QHash<QString, csscascade::ElementStyle> nodeInnerFills;
  QHash<QString, csscascade::ElementStyle> nodeInnerStrokes;
  QHash<QString, csscascade::ElementStyle> nodeLabels;
  // The label <p> slots — the text wrapper INSIDE the span: the text's used
  // font/color and its own display/visibility channels live here.
  QHash<QString, csscascade::ElementStyle> nodeLabelPs;
  QHash<QString, csscascade::ElementStyle> nodeDescriptions;
  QHash<QString, csscascade::ElementStyle> nodeDescriptionPs;
  QHash<QString, csscascade::ElementStyle> nodeDividers;
  QHash<QString, csscascade::ElementStyle> clusterOuters;
  QHash<QString, csscascade::ElementStyle> clusterDividers;
  QHash<QString, csscascade::ElementStyle> clusterInners;
  QHash<QString, csscascade::ElementStyle> clusterLabels;
  QHash<QString, csscascade::ElementStyle> clusterLabelPs;
  QHash<QString, csscascade::ElementStyle> edgePaths;
  QHash<QString, csscascade::ElementStyle> edgeLabelSpans;
  QHash<QString, csscascade::ElementStyle> edgeLabelBackgrounds;
  // The referenced marker's path element (classic barbEnd / neo -margin
  // clone) — the raster arrowhead color channel.
  csscascade::ElementStyle markerPath;
};

state::StateElementCss elementCss(const csscascade::ElementStyle& style) {
  state::StateElementCss css;
  css.fill = style.fill;
  css.stroke = style.stroke;
  css.strokeWidthPx = cssStrokeWidthPx(style.strokeWidth, {}, 0.0);
  // The resolved width (inline `style` participates in the cascade) is
  // authoritative — including a declared 0, which disables the pen.
  css.strokeWidthSet = true;
  // display:none (or a hidden ancestor) removes the box; visibility only
  // removes paint — two separate channels for layout vs painting.
  css.displayed = style.ancestorRenderable &&
      style.display.compare(QLatin1String("none"), Qt::CaseInsensitive) != 0;
  css.painted = style.visibility.compare(QLatin1String("hidden"),
                                         Qt::CaseInsensitive) != 0 &&
      style.visibility.compare(QLatin1String("collapse"),
                               Qt::CaseInsensitive) != 0;
  // Opacity model (browser-exact): `opacity` carries the EFFECTIVE element
  // opacity (ancestor chain folded), while fill-/stroke-opacity keep the
  // PURE per-channel factors. The paint channel's composite alpha is
  // color.alpha × opacity × channel — the engine's effectiveFillOpacity /
  // effectiveStrokeOpacity already fold `opacity` in, so storing them here
  // AND multiplying by css.opacity in the painter would square the element
  // opacity (opacity:0.2 rendered 0.04). The SVG projection serializes the
  // three factors separately (opacity + fill-opacity), which composes to
  // the same product.
  css.opacity = style.effectiveOpacity;
  css.fillOpacity = cssOpacity(style.fillOpacity);
  css.strokeOpacity = cssOpacity(style.strokeOpacity);
  css.color = style.color;
  css.fontFamily = firstFontFamily(style.fontFamily);
  css.fontSize = cssFontSizePx(style.fontSize, {});
  css.backgroundColor = style.backgroundColor;
  return css;
}

StateCssModel resolveStateCss(const state::StateLayoutInput& input,
                              const flowtheme::FlowThemeVariables& themeVars,
                              const QString& look, const QString& themeCss,
                              bool hasTitle, const QString& fontFamily,
                              qreal fontSize,
                              const QHash<QString, state::StateSceneLink>& anchors) {
  using csscascade::ElementInput;
  using csscascade::ElementStyle;
  const bool neo = look == QLatin1String("neo");
  // Live subset of state getStyles() (11.16). Dead rules are omitted with
  // their reason: `.state-note` (the class is statediagram-note),
  // `.stateLabel text`/`.edgeLabel .label text`/`g.stateGroup text` (labels
  // are foreignObjects), `.edgeLabel .label rect` (that rect only exists in
  // the SVG-label branch), `.statediagram .edgeLabel` (the scoped selector
  // needs .statediagram INSIDE the svg root), `.node .fork-join` and
  // `.node circle.state-end` (fork/end shapes draw rough <path>s),
  // `.node polygon` (11.16 state has no polygons — choice is a rough path
  // pair), and the rx rules (geometry, not paint).
  const QString px = QString::number(themeVars.strokeWidth) + QStringLiteral("px");
  const QString stateStroke = !themeVars.stateBorder.isEmpty()
      ? themeVars.stateBorder : themeVars.nodeBorder;
  const QString stateBkg = !themeVars.stateBkg.isEmpty()
      ? themeVars.stateBkg : themeVars.mainBkg;
  const QString labelColor = themeVars.stateLabelColor.isEmpty()
      ? themeVars.primaryTextColor : themeVars.stateLabelColor;
  const QString transitionLabel = themeVars.transitionLabelColor.isEmpty()
      ? themeVars.tertiaryTextColor : themeVars.transitionLabelColor;
  const QString altBackground = themeVars.altBackground.isEmpty()
      ? QStringLiteral("#efefef") : themeVars.altBackground;
  // NOTE: markers %1..%16 must stay dense and in order — QString::arg's
  // sequential substitution fills the LOWEST marker per argument, so an
  // unused marker shifts every later value by one.
  const QString builtInCss = QStringLiteral(
      "defs [id$=\"-barbEnd\"] { fill: %1; stroke: %1; }"
      ".transition { stroke: %2; stroke-width: %3; fill: none; }"
      ".node rect { fill: %4; stroke: %5; stroke-width: %3; }"
      ".node circle.state-start { fill: %6; stroke: %6; }"
      ".statediagram-cluster rect { fill: %7; stroke: %5; stroke-width: %3; }"
      ".statediagram-cluster.statediagram-cluster .inner { fill: %8; }"
      ".statediagram-cluster.statediagram-cluster-alt .inner { fill: %9; }"
      ".statediagram-state .divider { stroke: %5; }"
      ".statediagram-state rect.divider { fill: %9; }"
      ".statediagram-note rect { fill: %10; stroke: %11; stroke-width: 1px; }"
      ".cluster-label, .nodeLabel { color: %12; }"
      ".statediagram-note .nodeLabel { color: %13; }"
      ".edgeLabel p { background-color: %14; }"
      ".label div .edgeLabel { color: %15; }"
      ".statediagramTitleText { fill: %16; }")
      .arg(themeVars.transitionColor,
           themeVars.transitionColor, px, stateBkg, stateStroke,
           themeVars.specialStateColor,
           !neo ? themeVars.compositeTitleBackground : themeVars.mainBkg,
           themeVars.compositeBackground, altBackground,
           themeVars.noteBkgColor, themeVars.noteBorderColor,
           labelColor, themeVars.noteTextColor,
           themeVars.edgeLabelBackground, transitionLabel,
           themeVars.textColor);
  // The neo rules: the state sheet's cluster rules come AFTER the divider
  // rule, so their equal-specificity fill wins the cascade (divider grey ->
  // mainBkg; the higher-specificity `.inner` rules keep compositeBackground).
  // The common sheet's look rules additionally restyle every node shape:
  // rects/circles/paths stroke nodeBorder (no gradient for the built-in
  // themes that reach here — useGradient is resolved false) and pick up the
  // theme dropShadow filter (paint-side in the painter).
  const QString neoCss = neo ? QStringLiteral(
      "[data-look=\"neo\"].statediagram-cluster rect { fill: %1; stroke: %2; }"
      "[data-look=\"neo\"].node rect, [data-look=\"neo\"].node circle { "
      "stroke: %3; }"
      "[data-look=\"neo\"].node path { stroke: %3; stroke-width: %4; }")
          .arg(themeVars.mainBkg, stateStroke, themeVars.nodeBorder, px)
      : QString();

  ElementStyle rootFallback;
  rootFallback.fill = themeVars.textColor;
  rootFallback.stroke = QStringLiteral("none");
  rootFallback.strokeWidth = QStringLiteral("1px");
  rootFallback.color = QStringLiteral("black");
  rootFallback.fontFamily = fontFamily;
  rootFallback.fontSize = QString::number(fontSize) + QStringLiteral("px");

  QVector<ElementInput> elements;
  elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                   QStringLiteral("diagram-root"),
                   {QStringLiteral("stateDiagram")}, {}, rootFallback, {}});
  // Real 11.16 skeleton (browser-verified): svg > [style, g (CLASSLESS
  // wrapper) > [defs (one PER marker), g.root > [clusters/edgePaths/
  // edgeLabels/nodes]], defs (drop-shadow filters), text.title]. The
  // <style> element is the FIRST root child (createUserStyles injects it
  // before the content), the wrapper carries the marker defs before g.root,
  // and the theme's two feDropShadow filter defs sit at the svg root after
  // the wrapper.
  elements.append({QStringLiteral("style"), QStringLiteral("svg"),
                   QStringLiteral("style"), {}, {}, {}, rootFallback, {}});
  elements.append({QStringLiteral("wrapper"), QStringLiteral("svg"),
                   QStringLiteral("g"), {}, {}, {}, rootFallback, {}});
  {
    // The renderer emits one defs per barb marker — the classic
    // `…-barbEnd` only (referenced by classic edges), plus its `-margin`
    // clone under neo (referenced by neo edges). Both paths carry the NEO
    // barb geometry under the neo look.
    const auto appendMarker = [&](const QString& key, const QString& id,
                                  const QString& presentation) {
      ElementStyle pathFallback = rootFallback;
      pathFallback.fill = themeVars.transitionColor;
      pathFallback.stroke = themeVars.transitionColor;
      pathFallback.strokeWidth = QStringLiteral("1px");
      elements.append({QStringLiteral("marker!") + key +
                           QStringLiteral("-defs"),
                       QStringLiteral("wrapper"), QStringLiteral("defs"), {},
                       {}, {}, rootFallback, {}});
      elements.append({QStringLiteral("marker!") + key +
                           QStringLiteral("-marker"),
                       QStringLiteral("marker!") + key +
                           QStringLiteral("-defs"),
                       QStringLiteral("marker"), id, {}, {}, rootFallback, {}});
      elements.append({QStringLiteral("marker!") + key,
                       QStringLiteral("marker!") + key +
                           QStringLiteral("-marker"),
                       QStringLiteral("path"), {}, {}, {}, pathFallback, {},
                       presentation});
    };
    appendMarker(QStringLiteral("classic"),
                 QStringLiteral("x_stateDiagram-barbEnd"), QString());
    if (neo)
      appendMarker(QStringLiteral("margin"),
                   QStringLiteral("x_stateDiagram-barbEnd-margin"),
                   QStringLiteral("fill:%1").arg(themeVars.transitionColor));
  }
  elements.append({QStringLiteral("root"), QStringLiteral("wrapper"),
                   QStringLiteral("g"), {}, {QStringLiteral("root")}, {},
                   rootFallback, {}});

  // Label span stack: g.label > [rect (the 0x0 svg-background rect — ONLY
  // on markdown labels: plain rect and note nodes carry it, cluster /
  // rectWithTitle / edge labels do not)] + foreignObject >
  // div(.labelBkg for edge labels) > span.nodeLabel > p (the text wrapper —
  // `.nodeLabel p` matches it).
  const auto appendLabel = [&](const QString& parent, const QString& key,
                               const QString& spanClass,
                               const QStringList& labelGroupClasses,
                               const ElementStyle& fallback,
                               const QStringList& extraSpanClasses = {},
                               bool divLabelBkg = false,
                               bool backgroundRect = true) {
    elements.append({key + QStringLiteral("-lg"), parent,
                     QStringLiteral("g"), {}, labelGroupClasses, {}, fallback, {}});
    if (backgroundRect)
      elements.append({key + QStringLiteral("-rect"),
                       key + QStringLiteral("-lg"), QStringLiteral("rect"), {},
                       {}, {}, fallback, {}});
    elements.append({key + QStringLiteral("-fo"),
                     key + QStringLiteral("-lg"),
                     QStringLiteral("foreignObject"), {}, {}, {}, fallback, {}});
    elements.append({key + QStringLiteral("-div"),
                     key + QStringLiteral("-fo"), QStringLiteral("div"), {},
                     divLabelBkg ? QStringList{QStringLiteral("labelBkg")}
                                 : QStringList{},
                     {}, fallback, {}});
    QStringList spanClassList{spanClass};
    spanClassList += extraSpanClasses;
    elements.append({key, key + QStringLiteral("-div"),
                     QStringLiteral("span"), {}, spanClassList, {}, fallback, {}});
    elements.append({key + QStringLiteral("-p"), key,
                     QStringLiteral("p"), {}, {}, {}, fallback, {}});
  };

  // ---- g.clusters ----
  elements.append({QStringLiteral("clusters"), QStringLiteral("root"),
                   QStringLiteral("g"), {}, {QStringLiteral("clusters")}, {},
                   rootFallback, {}});
  for (const state::StateLayoutNodeInput& node : input.nodes) {
    if (!node.isGroup) continue;
    const QString base = QStringLiteral("cluster!") + node.id + QLatin1Char('!');
    const bool divider = node.shape == QLatin1String("divider");
    const bool noteGroup = node.shape == QStringLiteral("noteGroup");
    QStringList classes = node.cssClasses.split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    if (noteGroup) classes = {QStringLiteral("note-cluster")};
    elements.append({base + QStringLiteral("g"), QStringLiteral("clusters"),
                     QStringLiteral("g"), QStringLiteral("state-%1").arg(node.id),
                     classes,
                     {{QStringLiteral("data-look"), look},
                      {QStringLiteral("data-id"), node.id}},
                     rootFallback, {}});
    ElementStyle rectFallback = rootFallback;
    rectFallback.fill = themeVars.compositeTitleBackground;
    rectFallback.stroke = stateStroke;
    rectFallback.strokeWidth = px;
    if (noteGroup) {
      // g.note-cluster > rect (fill:none presentation; nothing paints).
      elements.append({base + QStringLiteral("rect"),
                       base + QStringLiteral("g"), QStringLiteral("rect"), {},
                       {}, {}, rootFallback, {},
                       QStringLiteral("fill:none")});
      continue;
    }
    if (divider) {
      elements.append({base + QStringLiteral("inner-g"),
                       base + QStringLiteral("g"), QStringLiteral("g"), {}, {},
                       {}, rootFallback, {}});
      elements.append({base + QStringLiteral("divider"),
                       base + QStringLiteral("inner-g"), QStringLiteral("rect"),
                       {}, {QStringLiteral("divider")},
                       {{QStringLiteral("data-look"), look}}, rectFallback, {}});
      continue;
    }
    // composite: g > rect.outer + g.cluster-label(span) + rect.inner
    elements.append({base + QStringLiteral("outer-g"),
                     base + QStringLiteral("g"), QStringLiteral("g"), {}, {},
                     {}, rootFallback, {}});
    elements.append({base + QStringLiteral("outer"),
                     base + QStringLiteral("outer-g"), QStringLiteral("rect"),
                     {}, {QStringLiteral("outer")},
                     {{QStringLiteral("data-look"), look}}, rectFallback, {}});
    ElementStyle labelFallback = rootFallback;
    labelFallback.color = labelColor;
    appendLabel(base + QStringLiteral("g"),
                base + QStringLiteral("label"),
                QStringLiteral("nodeLabel"),
                {QStringLiteral("cluster-label")}, labelFallback, {},
                false, /*backgroundRect=*/false);
    elements.append({base + QStringLiteral("inner"),
                     base + QStringLiteral("g"), QStringLiteral("rect"), {},
                     {QStringLiteral("inner")}, {}, rectFallback, {}});
  }

  // ---- g.edgePaths / g.edgeLabels ----
  elements.append({QStringLiteral("edgePaths"), QStringLiteral("root"),
                   QStringLiteral("g"), {}, {QStringLiteral("edgePaths")}, {},
                   rootFallback, {}});
  elements.append({QStringLiteral("edgeLabels"), QStringLiteral("root"),
                   QStringLiteral("g"), {}, {QStringLiteral("edgeLabels")}, {},
                   rootFallback, {}});
  for (const state::StateLayoutEdgeInput& edge : input.edges) {
    const QString base = QStringLiteral("edge!") + edge.id + QLatin1Char('!');
    ElementStyle pathFallback = rootFallback;
    pathFallback.fill = QStringLiteral("none");
    pathFallback.stroke = themeVars.transitionColor;
    pathFallback.strokeWidth = px;
    QStringList classes{QStringLiteral("edge-thickness-normal"),
                        QStringLiteral("edge-pattern-solid"),
                        QStringLiteral("transition")};
    if (edge.classes.contains(QLatin1String("note-edge")))
      classes.append(QStringLiteral("note-edge"));
    elements.append({base + QStringLiteral("path"),
                     QStringLiteral("edgePaths"), QStringLiteral("path"),
                     QStringLiteral("state-") + edge.id, classes,
                     {{QStringLiteral("data-look"), look}}, pathFallback, {},
                     QStringLiteral("fill:none")});
    if (edge.label.isString() && !edge.label.toString().isEmpty()) {
      elements.append({base + QStringLiteral("label-g"),
                       QStringLiteral("edgeLabels"), QStringLiteral("g"), {},
                       {QStringLiteral("edgeLabel")}, {}, rootFallback, {}});
      ElementStyle spanFallback = rootFallback;
      spanFallback.color = transitionLabel;
      appendLabel(base + QStringLiteral("label-g"),
                  base + QStringLiteral("label"),
                  QStringLiteral("edgeLabel"),
                  {QStringLiteral("label")}, spanFallback, {}, true,
                  /*backgroundRect=*/false);
    }
  }

  // ---- g.nodes ----
  elements.append({QStringLiteral("nodes"), QStringLiteral("root"),
                   QStringLiteral("g"), {}, {QStringLiteral("nodes")}, {},
                   rootFallback, {}});
  for (const state::StateLayoutNodeInput& node : input.nodes) {
    if (node.isGroup) continue;
    const QString base = QStringLiteral("node!") + node.id + QLatin1Char('!');
    const QString shape = node.shape.isEmpty()
        ? QStringLiteral("rect") : node.shape;
    // Under handDrawn the node class token is normally replaced by
    // "rough-node", and plain rects become a hachure + outline path pair.
    const bool roughLook = look == QLatin1String("handDrawn");
    // rectWithTitle is the handDrawn exception: it keeps the `node` class
    // while its rect and divider become classless rough path groups.
    const bool roughTitled = roughLook &&
        shape == QLatin1String("rectWithTitle");
    QStringList classes = node.cssClasses.split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    classes.prepend(roughLook && !roughTitled
                        ? QStringLiteral("rough-node")
                        : QStringLiteral("node"));
    if (shape == QLatin1String("stateStart") ||
        shape == QLatin1String("stateEnd"))
      classes = {roughLook ? QStringLiteral("rough-node")
                           : QStringLiteral("node"),
                 QStringLiteral("default")};
    // Click-wrapped nodes render as g.nodes > a > g.node (upstream wraps
    // the node's <g> in an <a> AFTER layout) — descendant and structural
    // selectors must resolve through the anchor. The anchor carries
    // xlink:href + title (attribute selectors like a[title="tip"] match),
    // and the wrapped g.node also receives the title attribute.
    QString parent = QStringLiteral("nodes");
    const auto anchor = anchors.constFind(node.id);
    const bool anchored = anchor != anchors.constEnd();
    if (anchored) {
      elements.append({base + QStringLiteral("a"), parent,
                       QStringLiteral("a"), {}, {},
                       {{QStringLiteral("xlink:href"), anchor.value().url},
                        {QStringLiteral("title"), anchor.value().tooltip}},
                       rootFallback, {}});
      parent = base + QStringLiteral("a");
    }
    QHash<QString, QString> nodeAttributes{
        {QStringLiteral("data-look"), look}};
    if (anchored && !anchor.value().tooltip.isEmpty())
      nodeAttributes.insert(QStringLiteral("title"), anchor.value().tooltip);
    elements.append({base + QStringLiteral("g"), parent,
                     QStringLiteral("g"), QStringLiteral("state-%1").arg(node.id),
                     classes, nodeAttributes, rootFallback, {}});
    ElementStyle shapeFallback = rootFallback;
    shapeFallback.fill = stateBkg;
    shapeFallback.stroke = stateStroke;
    shapeFallback.strokeWidth = px;
    // The classDef/inline `style` cascade rides the shape element as an
    // inline author style (upstream writes nodeStyles straight onto it).
    const QString inlineStyle = node.styles.join(QLatin1Char(';'));
    const auto nodeStyleValue = [&](QLatin1StringView key,
                                    const QString& fallback) {
      QString value = fallback;
      for (const QString& declaration : node.styles) {
        const int colon = declaration.indexOf(QLatin1Char(':'));
        if (colon < 0 || declaration.left(colon).trimmed() != key) continue;
        value = declaration.mid(colon + 1).trimmed();
        value.remove(QStringLiteral("!important"), Qt::CaseInsensitive);
        value = value.trimmed();
      }
      return value;
    };
    // Rough-drawn shapes (note/choice/fork/stateEnd) are rough.js output at
    // roughness 0: a FILL path (fill attr, stroke none) plus a separate
    // STROKE path (stroke attr at userNodeOverrides' 1.3px default, fill
    // none) — independent DOM elements for themeCSS.
    const auto appendRoughShape = [&](const QString& groupClasses,
                                      const QString& fillPresentation,
                                      const QString& strokePresentation,
                                      qreal strokePx,
                                      const ElementStyle& fillFallback,
                                      const ElementStyle& strokeFallback) {
      elements.append({base + QStringLiteral("shape-g"),
                       base + QStringLiteral("g"), QStringLiteral("g"), {},
                       groupClasses.isEmpty() ? QStringList{}
                                              : QStringList{groupClasses},
                       {}, rootFallback, {}});
      ElementStyle fillStyle = fillFallback;
      fillStyle.stroke = QStringLiteral("none");
      fillStyle.strokeWidth = QStringLiteral("0px");
      QString usedFillPresentation = fillPresentation;
      if (look == QLatin1String("handDrawn")) {
        // Under the handDrawn look the fill path paints the HACHURE via
        // its stroke (fill:none;stroke:<fill>;width 4 — browser-verified
        // for note/fork/choice/end and the rough rects).
        fillStyle.stroke = fillFallback.fill;
        fillStyle.strokeWidth = QStringLiteral("4px");
        usedFillPresentation =
            QStringLiteral("fill:none;stroke:%1;stroke-width:4px")
                .arg(fillFallback.fill);
      }
      elements.append({base + QStringLiteral("shape"),
                       base + QStringLiteral("shape-g"), QStringLiteral("path"),
                       {}, {}, {}, fillStyle, {}, usedFillPresentation});
      ElementStyle strokeStyle = strokeFallback;
      strokeStyle.fill = QStringLiteral("none");
      strokeStyle.strokeWidth =
          QString::number(strokePx) + QStringLiteral("px");
      elements.append({base + QStringLiteral("shape-stroke"),
                       base + QStringLiteral("shape-g"), QStringLiteral("path"),
                       {}, {}, {}, strokeStyle, {}, strokePresentation});
    };
    if (shape == QLatin1String("stateStart")) {
      ElementStyle circleFallback = rootFallback;
      circleFallback.fill = themeVars.specialStateColor;
      circleFallback.stroke = themeVars.specialStateColor;
      elements.append({base + QStringLiteral("shape"), base + QStringLiteral("g"),
                       QStringLiteral("circle"), {},
                       {QStringLiteral("state-start")}, {}, circleFallback, {}});
    } else if (shape == QLatin1String("stateEnd")) {
      // g.outer-path > rc.circle pair: ring (fill mainBkg, stroke lineColor
      // 2px) with the inner dot (fill+stroke `stateBorder ?? nodeBorder`
      // 2px, r = 5/14 of the 14px circle) inserted into the same group.
      ElementStyle ringFill = shapeFallback;
      ElementStyle ringStroke = rootFallback;
      ringStroke.stroke = themeVars.lineColor;
      ElementStyle dot = rootFallback;
      dot.fill = stateStroke;
      dot.stroke = stateStroke;
      appendRoughShape(QStringLiteral("outer-path"), QString(),
                       QStringLiteral("stroke:%1;stroke-width:2px")
                           .arg(themeVars.lineColor),
                       2.0, ringFill, ringStroke);
      ElementStyle dotFill = dot;
      dotFill.stroke = QStringLiteral("none");
      ElementStyle dotStroke = dot;
      dotStroke.fill = QStringLiteral("none");
      // The inner dot is a nested classless g INSIDE g.outer-path
      // (browser-verified: outer-path > [ring fill path, ring stroke path,
      // g > [dot fill path, dot stroke path]]).
      elements.append({base + QStringLiteral("inner-g"),
                       base + QStringLiteral("shape-g"), QStringLiteral("g"), {},
                       {}, {}, rootFallback, {}});
      elements.append({base + QStringLiteral("inner"),
                       base + QStringLiteral("inner-g"), QStringLiteral("path"),
                       {}, {}, {}, dotFill, {},
                       QStringLiteral("fill:%1").arg(stateStroke)});
      elements.append({base + QStringLiteral("inner-stroke"),
                       base + QStringLiteral("inner-g"), QStringLiteral("path"),
                       {}, {}, {}, dotStroke, {},
                       QStringLiteral("fill:none;stroke:%1;stroke-width:2px")
                           .arg(stateStroke)});
    } else if (shape == QLatin1String("fork") ||
               shape == QLatin1String("join")) {
      shapeFallback.fill = themeVars.lineColor;
      shapeFallback.stroke = themeVars.lineColor;
      appendRoughShape(QString(), QString(),
                       QStringLiteral("stroke:%1").arg(themeVars.lineColor),
                       1.3, shapeFallback, shapeFallback);
    } else if (shape == QLatin1String("choice")) {
      appendRoughShape(QString(), QString(),
                       QStringLiteral("stroke:%1").arg(themeVars.nodeBorder),
                       1.3, shapeFallback, shapeFallback);
    } else if (shape == QLatin1String("note")) {
      shapeFallback.fill = themeVars.noteBkgColor;
      shapeFallback.stroke = themeVars.noteBorderColor;
      appendRoughShape(QStringLiteral("basic label-container outer-path"),
                       QStringLiteral("fill:%1").arg(themeVars.noteBkgColor),
                       QStringLiteral("stroke:%1;stroke-width:1.3px")
                           .arg(themeVars.noteBorderColor),
                       1.3, shapeFallback, shapeFallback);
      appendLabel(base + QStringLiteral("g"), base + QStringLiteral("label"),
                  QStringLiteral("nodeLabel"),
                  {QStringLiteral("label"), QStringLiteral("noteLabel")},
                  rootFallback,
                  {QStringLiteral("markdown-node-label")});
    } else if (roughTitled) {
      // Browser DOM (11.16): g.node > [classless g with hachure+outline,
      // classless g with divider path, empty classless g, g.label with two
      // foreignObjects]. There is no rect/line and no rough-node class.
      ElementStyle titledFillFallback = shapeFallback;
      titledFillFallback.fill = nodeStyleValue(QLatin1String("fill"),
                                               shapeFallback.fill);
      ElementStyle titledStrokeFallback = shapeFallback;
      titledStrokeFallback.stroke = nodeStyleValue(QLatin1String("stroke"),
                                                    shapeFallback.stroke);
      const QString titledStrokeWidth = nodeStyleValue(
          QLatin1String("stroke-width"), QStringLiteral("1.3px"));
      const qreal titledStrokePx = cssStrokeWidthPx(titledStrokeWidth, {}, 1.3);
      appendRoughShape(QString(), QString(),
                       QStringLiteral("fill:none;stroke:%1;stroke-width:%2")
                           .arg(titledStrokeFallback.stroke,
                                titledStrokeWidth),
                       titledStrokePx, titledFillFallback,
                       titledStrokeFallback);
      elements.append({base + QStringLiteral("divider-g"),
                       base + QStringLiteral("g"), QStringLiteral("g"), {},
                       {}, {}, rootFallback, {}});
      ElementStyle dividerFallback = titledStrokeFallback;
      dividerFallback.fill = QStringLiteral("none");
      dividerFallback.strokeWidth = titledStrokeWidth;
      elements.append({base + QStringLiteral("divider-line"),
                       base + QStringLiteral("divider-g"),
                       QStringLiteral("path"), {}, {}, {}, dividerFallback,
                       {}, QStringLiteral("fill:none;stroke:%1;stroke-width:%2")
                               .arg(titledStrokeFallback.stroke,
                                    titledStrokeWidth)});
      elements.append({base + QStringLiteral("label-placeholder"),
                       base + QStringLiteral("g"), QStringLiteral("g"), {},
                       {}, {}, rootFallback, {}});
      ElementStyle labelFallback = rootFallback;
      labelFallback.color = labelColor;
      appendLabel(base + QStringLiteral("g"), base + QStringLiteral("label"),
                  QStringLiteral("nodeLabel"),
                  {QStringLiteral("label")}, labelFallback, {}, false,
                  /*backgroundRect=*/false);
      if (node.description.isArray() && !node.description.toArray().isEmpty()) {
        elements.append({base + QStringLiteral("desc"),
                         base + QStringLiteral("label-lg"),
                         QStringLiteral("foreignObject"), {}, {}, {},
                         labelFallback, {}});
        elements.append({base + QStringLiteral("desc-div"),
                         base + QStringLiteral("desc"), QStringLiteral("div"),
                         {}, {}, {}, labelFallback, {}});
        elements.append({base + QStringLiteral("desc-span"),
                         base + QStringLiteral("desc-div"),
                         QStringLiteral("span"), {},
                         {QStringLiteral("nodeLabel")}, {}, labelFallback, {}});
        elements.append({base + QStringLiteral("desc-p"),
                         base + QStringLiteral("desc-span"),
                         QStringLiteral("p"), {}, {}, {}, labelFallback, {}});
      }
    } else if (roughLook && shape == QLatin1String("rect")) {
      // handDrawn plain rect: the rough pair replaces the rect element —
      // the hachure fill path carries fill:none;stroke:<fill>;width 4 and
      // the outline path stroke:<border> at 1.3px. The user inline style
      // rides the group (fill/stroke inherit to both paths).
      elements.append({base + QStringLiteral("shape-g"),
                       base + QStringLiteral("g"), QStringLiteral("g"), {},
                       {QStringLiteral("basic"),
                        QStringLiteral("label-container")}, {},
                       rootFallback, inlineStyle});
      ElementStyle fillPath = shapeFallback;
      fillPath.fill = QStringLiteral("none");
      fillPath.stroke = stateBkg;
      fillPath.strokeWidth = QStringLiteral("4px");
      ElementStyle strokePath = shapeFallback;
      strokePath.fill = QStringLiteral("none");
      strokePath.strokeWidth = QStringLiteral("1.3px");
      elements.append({base + QStringLiteral("shape"),
                       base + QStringLiteral("shape-g"), QStringLiteral("path"),
                       {}, {}, {}, fillPath, {},
                       QStringLiteral("fill:none;stroke:%1;stroke-width:4px")
                           .arg(stateBkg)});
      elements.append({base + QStringLiteral("shape-stroke"),
                       base + QStringLiteral("shape-g"), QStringLiteral("path"),
                       {}, {}, {}, strokePath, {},
                       QStringLiteral("fill:none;stroke:%1;stroke-width:1.3px")
                           .arg(stateStroke)});
      ElementStyle labelFallback = rootFallback;
      labelFallback.color = labelColor;
      appendLabel(base + QStringLiteral("g"), base + QStringLiteral("label"),
                  QStringLiteral("nodeLabel"),
                  {QStringLiteral("label")}, labelFallback,
                  {QStringLiteral("markdown-node-label")});
    } else {
      // rect / rectWithTitle (browser-verified): the plain rect is
      // rect.basic.label-container directly under g.node; the TITLED
      // variant wraps rect.outer.title-state + line.divider in a classless
      // intermediate g. The label group is g.label > fo > div >
      // span.nodeLabel > p (NO 0x0 background rect, NO markdown class for
      // the titled variant), and the description rows are a SECOND
      // foreignObject inside g.label with the SAME fo > div > span > p
      // chain (one p per row).
      QStringList rectClasses{QStringLiteral("basic"),
                              QStringLiteral("label-container")};
      if (shape == QLatin1String("rectWithTitle"))
        rectClasses = {QStringLiteral("outer"), QStringLiteral("title-state")};
      QString shapeParent = base + QStringLiteral("g");
      if (shape == QLatin1String("rectWithTitle")) {
        elements.append({base + QStringLiteral("shape-g"),
                         base + QStringLiteral("g"), QStringLiteral("g"), {}, {},
                         {}, rootFallback, {}});
        shapeParent = base + QStringLiteral("shape-g");
      }
      elements.append({base + QStringLiteral("shape"),
                       shapeParent, QStringLiteral("rect"), {},
                       rectClasses, {}, shapeFallback, inlineStyle});
      if (shape == QLatin1String("rectWithTitle"))
        elements.append({base + QStringLiteral("divider-line"),
                         shapeParent, QStringLiteral("line"), {},
                         {QStringLiteral("divider")}, {}, shapeFallback, {}});
      ElementStyle labelFallback = rootFallback;
      labelFallback.color = labelColor;
      appendLabel(base + QStringLiteral("g"), base + QStringLiteral("label"),
                  QStringLiteral("nodeLabel"),
                  {QStringLiteral("label")}, labelFallback,
                  shape == QLatin1String("rectWithTitle")
                      ? QStringList{}
                      : QStringList{QStringLiteral("markdown-node-label")},
                  false,
                  /*backgroundRect=*/shape != QLatin1String("rectWithTitle"));
      if (shape == QLatin1String("rectWithTitle") &&
          node.description.isArray() && !node.description.toArray().isEmpty()) {
        // rectWithTitle's description is a SECOND foreignObject inside
        // g.label (createLabel_default appends it beside the title fo).
        // Upstream joins the rows first — description.join("<br/>") fed to
        // ONE createLabel (chunk-ZGVPDNZ5.mjs) — so the chain is
        // fo > div > span.nodeLabel > a SINGLE p carrying <br/> rows, not
        // one p per row.
        elements.append({base + QStringLiteral("desc"),
                         base + QStringLiteral("label-lg"),
                         QStringLiteral("foreignObject"), {}, {}, {},
                         labelFallback, {}});
        elements.append({base + QStringLiteral("desc-div"),
                         base + QStringLiteral("desc"), QStringLiteral("div"),
                         {}, {}, {}, labelFallback, {}});
        elements.append({base + QStringLiteral("desc-span"),
                         base + QStringLiteral("desc-div"),
                         QStringLiteral("span"), {},
                         {QStringLiteral("nodeLabel")}, {}, labelFallback, {}});
        elements.append({base + QStringLiteral("desc-p"),
                         base + QStringLiteral("desc-span"),
                         QStringLiteral("p"), {}, {}, {}, labelFallback, {}});
      }
    }
  }

  // Root-level filter defs AFTER the wrapper g (document order): the
  // theme's feDropShadow pair — `<id>-drop-shadow` and
  // `<id>-drop-shadow-small`. They carry no styling consumers but complete
  // the real DOM (defs:nth-of-type / svg > defs selectors).
  elements.append({QStringLiteral("filter-defs"), QStringLiteral("svg"),
                   QStringLiteral("defs"), {}, {}, {}, rootFallback, {}});
  elements.append({QStringLiteral("filter-shadow"),
                   QStringLiteral("filter-defs"), QStringLiteral("filter"),
                   QStringLiteral("x-drop-shadow"), {}, {}, rootFallback, {}});
  elements.append({QStringLiteral("filter-defs-2"), QStringLiteral("svg"),
                   QStringLiteral("defs"), {}, {}, {}, rootFallback, {}});
  elements.append({QStringLiteral("filter-shadow-small"),
                   QStringLiteral("filter-defs-2"), QStringLiteral("filter"),
                   QStringLiteral("x-drop-shadow-small"), {}, {},
                   rootFallback, {}});

  if (hasTitle) {
    ElementStyle titleFallback = rootFallback;
    titleFallback.fill = themeVars.textColor;
    elements.append({QStringLiteral("title"), QStringLiteral("svg"),
                     QStringLiteral("text"), {},
                     {QStringLiteral("statediagramTitleText")}, {},
                     titleFallback, {}});
  }

  const QHash<QString, ElementStyle> resolved = csscascade::resolveElements(
      themeCss, elements, builtInCss + neoCss);

  StateCssModel model;
  for (auto it = resolved.constBegin(); it != resolved.constEnd(); ++it) {
    const QString& key = it.key();
    // Marker path elements use 2-part keys (no slot).
    if (key == QStringLiteral("marker!") +
            QLatin1String(neo ? "margin" : "classic")) {
      model.markerPath = it.value();
      continue;
    }
    const int first = key.indexOf(QLatin1Char('!'));
    const int last = key.lastIndexOf(QLatin1Char('!'));
    if (first < 0 || last <= first) continue;
    const QString kind = key.left(first);
    const QString id = key.mid(first + 1, last - first - 1);
    const QString slot = key.mid(last + 1);
    if (kind == QLatin1String("node")) {
      if (slot == QLatin1String("shape")) model.nodeShapes.insert(id, it.value());
      else if (slot == QLatin1String("shape-stroke"))
        model.nodeShapeStrokes.insert(id, it.value());
      else if (slot == QLatin1String("inner"))
        model.nodeInnerFills.insert(id, it.value());
      else if (slot == QLatin1String("inner-stroke"))
        model.nodeInnerStrokes.insert(id, it.value());
      else if (slot == QLatin1String("label")) model.nodeLabels.insert(id, it.value());
      else if (slot == QLatin1String("label-p"))
        model.nodeLabelPs.insert(id, it.value());
      else if (slot == QLatin1String("desc"))
        model.nodeDescriptions.insert(id, it.value());
      else if (slot == QLatin1String("desc-p"))
        model.nodeDescriptionPs.insert(id, it.value());
      else if (slot == QLatin1String("divider-line"))
        model.nodeDividers.insert(id, it.value());
    } else if (kind == QLatin1String("cluster")) {
      if (slot == QLatin1String("outer")) model.clusterOuters.insert(id, it.value());
      else if (slot == QLatin1String("divider"))
        model.clusterDividers.insert(id, it.value());
      else if (slot == QLatin1String("inner"))
        model.clusterInners.insert(id, it.value());
      else if (slot == QLatin1String("label"))
        model.clusterLabels.insert(id, it.value());
      else if (slot == QLatin1String("label-p"))
        model.clusterLabelPs.insert(id, it.value());
    } else if (kind == QLatin1String("edge")) {
      if (slot == QLatin1String("path")) model.edgePaths.insert(id, it.value());
      else if (slot == QLatin1String("label"))
        model.edgeLabelSpans.insert(id, it.value());
      else if (slot == QStringLiteral("label-p"))
        model.edgeLabelBackgrounds.insert(id, it.value());
    }
  }
  return model;
}

// stateDiagram behind the Diagram contract. Body is the former renderSource()
// state branch, verbatim.
struct StateDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("state"), QStringLiteral("stateDiagram")};
  }
  QString cssClass() const override { return QStringLiteral("stateDiagram"); }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
      const state::StateDiagram diagram = state::StateDiagram::parse(pre.code);
      const QString configuredTheme = themeFromConfig(pre.config);
      const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
          themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
          themeOverrides(pre.config));
      const QString look = pre.config.value(QStringLiteral("look"))
          .toString(QStringLiteral("classic"));
      const bool handDrawn = look == QLatin1String("handDrawn");
      const quint32 handDrawnSeed = static_cast<quint32>(
          std::max(0.0, configNumber(
              pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
      const QJsonObject stateConfig =
          pre.config.value(QStringLiteral("state")).toObject();
      const state::StateLayoutInput input =
          state::buildStateLayoutInput(diagram.data(), look);
      state::StateSceneStyle style;
      // `.node rect` rule: fill = stateBkg || mainBkg, stroke = stateBorder ||
      // nodeBorder (NOT border1 — redux-dark's border1 #ccc differs from its
      // stateBorder/nodeBorder #FFFFFF). stateBkg is derived to mainBkg by
      // every built-in theme's updateColors.
      style.stateFill = !themeVars.stateBkg.isEmpty() ? themeVars.stateBkg
                                                      : themeVars.mainBkg;
      style.stateStroke = !themeVars.stateBorder.isEmpty()
                              ? themeVars.stateBorder : themeVars.nodeBorder;
      style.textColor = themeVars.primaryTextColor;
      // state/styles.js: `.transition` stroke + the barbEnd markers take
      // transitionColor (`|| lineColor`); the html edge-label background is
      // the <p>'s edgeLabelBackground; `.stateLabel text`/`.state-title` take
      // stateLabelColor — whose `|| stateBkg || primaryTextColor` chain
      // equals primaryTextColor unless overridden. All consume the resolved
      // themeVariables, so user overrides propagate like 11.16.
      style.transitionColor = themeVars.transitionColor;
      style.edgeLabelBackground = themeVars.edgeLabelBackground;
      style.stateLabelColor = themeVars.stateLabelColor;
      style.transitionLabelColor = themeVars.transitionLabelColor;
      style.specialStateColor = themeVars.specialStateColor;
      style.endInnerFill = !themeVars.stateBorder.isEmpty()
                               ? themeVars.stateBorder : themeVars.nodeBorder;
      style.compositeFill = themeVars.compositeBackground;
      style.compositeAltFill = themeVars.altBackground;
      style.compositeTitleFill = themeVars.compositeTitleBackground;
      style.compositeStroke = themeVars.nodeBorder;
      style.fontFamily = MermaidFontRegistry::cssFamilyStack();
      style.fontSize = pixelValue(themeVars.fontSize, 16.0);
      style.lineHeight = style.fontSize * 1.5;
      style.strokeWidth = themeVars.strokeWidth;
      style.neo = look == QLatin1String("neo");
      style.radius = themeVars.radius.toDouble();
      // The theme's dropShadow string feeds the neo-look filter (parsed in
      // the painter). The redux-dark family carries the url(#drop-shadow)
      // FORM reference: the renderer's defs filter is feDropShadow dx=4
      // dy=4 stdDeviation=0 flood-opacity 0.06 flood-color black (white for
      // dark themes) — synthesize the equivalent flat drop-shadow() from
      // the modeled shadow fields so the painter handles one grammar.
      if (themeVars.dropShadow.trimmed().startsWith(QLatin1String("url("))) {
        const QColor shadowColor = QColor(themeVars.shadowColor);
        style.shadowCss = QStringLiteral("drop-shadow(%1px %2px 0px rgba(%3,%4,%5,%6))")
            .arg(themeVars.shadowOffsetX).arg(themeVars.shadowOffsetY)
            .arg(shadowColor.red()).arg(shadowColor.green())
            .arg(shadowColor.blue()).arg(themeVars.shadowOpacity);
      } else {
        style.shadowCss = themeVars.dropShadow;
      }

      QVector<state::StateSceneLink> links;
      for (const state::StateLink& link : diagram.data().links) {
        const QString stateId = link.id.isObject()
            ? link.id.toObject().value(QStringLiteral("id")).toString()
            : link.id.toString();
        // upstream draw() strips surrounding quotes from click urls/tooltips
        // (linkInfo.url.replace(/^"+|"+$/g, "")).
        const auto stripQuotes = [](QString value) {
          while (value.size() >= 2 && value.front() == QLatin1Char('"') &&
                 value.back() == QLatin1Char('"'))
            value = value.mid(1, value.size() - 2);
          return value;
        };
        links.append({stateId, stripQuotes(link.url), stripQuotes(link.tooltip)});
      }

      MermaidRenderMetadata metadata = renderMetadata(
          pre, type, {}, diagram.data().accTitle,
          diagram.data().accDescription, style.textColor, style.fontFamily,
          18.0, configNumber(stateConfig, QStringLiteral("titleTopMargin"),
                             25.0), 0.0,
          // State folds its 8px viewbox padding into scene.bounds, so the
          // title band takes the padding via titleBandPadding (upstream:
          // viewBox = title∪content bbox, padded 8 all around).
          8.0);

      // themeCSS against the real DOM — resolved once, feeding measurement
      // (label fonts + rect display:none) and the scene's per-element slots.
      QHash<QString, state::StateSceneLink> anchors;
      for (const state::StateSceneLink& link : links)
        anchors.insert(link.stateId, link);
      const StateCssModel css = resolveStateCss(
          input, themeVars, look,
          pre.config.value(QStringLiteral("themeCSS")).toString(),
          metadata.hasVisibleTitle(), style.fontFamily, style.fontSize,
          anchors);

      // Global measurement font: the first node label's computed font (all
      // labels share the sheet, so uniform rules move every node together).
      // The p slot is the text's own used style (it folds the span chain).
      QString measureFamily = style.fontFamily;
      qreal measureSize = style.fontSize;
      for (const state::StateLayoutNodeInput& node : input.nodes) {
        if (node.isGroup) continue;
        const auto label = css.nodeLabelPs.constFind(node.id);
        if (label == css.nodeLabelPs.constEnd()) continue;
        measureFamily = firstFontFamily(label->fontFamily);
        measureSize = cssFontSizePx(label->fontSize, {});
        break;
      }

      QHash<QString, state::StateMeasureCss> nodeMeasureCss;
      // The label's <p> carries the TEXT: its computed font (folding the
      // span's rules through inheritance) is what the fo renders with, so
      // BOTH the label-box measurement and the painted glyphs read the p
      // slot. display:none on the p collapses the label box itself — a node
      // shrinks to its padding-only rect (16x16, browser-verified), the
      // cluster title band to zero, the edge label to no reserved space.
      const auto labelHidden = [](const csscascade::ElementStyle* style) {
        return style && (!style->ancestorRenderable ||
                         style->display.compare(QLatin1String("none"),
                                                Qt::CaseInsensitive) == 0);
      };
      for (const state::StateLayoutNodeInput& node : input.nodes) {
        state::StateMeasureCss entry;
        // Group nodes measure their cluster-label p (composite titles);
        // regular nodes their nodeLabel p. Both feed dagre sizing.
        const auto& labelPs = node.isGroup ? css.clusterLabelPs : css.nodeLabelPs;
        const auto labelP = labelPs.constFind(node.id);
        if (labelP != labelPs.constEnd()) {
          entry.fontFamily = firstFontFamily(labelP->fontFamily);
          entry.fontSize = cssFontSizePx(labelP->fontSize, {});
          entry.labelHidden = labelHidden(&labelP.value());
        } else {
          const auto label = node.isGroup
              ? css.clusterLabels.constFind(node.id)
              : css.nodeLabels.constFind(node.id);
          if (label != (node.isGroup ? css.clusterLabels : css.nodeLabels)
                              .constEnd()) {
            entry.fontFamily = firstFontFamily(label->fontFamily);
            entry.fontSize = cssFontSizePx(label->fontSize, {});
          }
        }
        if (!node.isGroup) {
          const auto shape = css.nodeShapes.constFind(node.id);
          // DISPLAY-ONLY: `display:none` collapses the dagre box (the
          // drawState temp group measures 0x0); `visibility:hidden` keeps
          // the box and only removes paint — ElementStyle::displayed()
          // folds both, which would wrongly collapse hidden-but-spacing
          // shapes.
          if (shape != css.nodeShapes.constEnd())
            entry.shapeHidden =
                !shape->ancestorRenderable ||
                shape->display.compare(QLatin1String("none"),
                                       Qt::CaseInsensitive) == 0;
          // rectWithTitle description rows are their OWN p (the second fo):
          // the titled box measures them with their own computed font, and
          // the p's display:none collapses the rows out of the dagre box.
          const auto descP = css.nodeDescriptionPs.constFind(node.id);
          if (descP != css.nodeDescriptionPs.constEnd()) {
            entry.descFontFamily = firstFontFamily(descP->fontFamily);
            entry.descFontSize = cssFontSizePx(descP->fontSize, {});
            entry.descHidden = labelHidden(&descP.value());
          }
        }
        nodeMeasureCss.insert(node.id, entry);
      }
      QHash<QString, state::StateMeasureCss> edgeMeasureCss;
      for (const state::StateLayoutEdgeInput& edge : input.edges) {
        // The edge label's p (same slot that carries the background): its
        // font sizes the label chip, display:none removes the reserved
        // space (the edge paths shrink — not just the paint).
        const auto background = css.edgeLabelBackgrounds.constFind(edge.id);
        const auto label = css.edgeLabelSpans.constFind(edge.id);
        if (background == css.edgeLabelBackgrounds.constEnd() &&
            label == css.edgeLabelSpans.constEnd())
          continue;
        state::StateMeasureCss entry;
        const csscascade::ElementStyle& source =
            background != css.edgeLabelBackgrounds.constEnd()
                ? background.value() : label.value();
        entry.fontFamily = firstFontFamily(source.fontFamily);
        entry.fontSize = cssFontSizePx(source.fontSize, {});
        entry.labelHidden = labelHidden(&source);
        edgeMeasureCss.insert(edge.id, entry);
      }

      const state::StateLayoutMeasurements measurements = state::measureStateLayoutInput(
          input, measureFamily, measureSize, handDrawn, handDrawnSeed, false,
          &nodeMeasureCss, &edgeMeasureCss);
      const state::StatePlacementResult placement =
          state::layoutStateDiagramDagre(
              input, measurements,
              configNumber(stateConfig, QStringLiteral("nodeSpacing"), 50.0),
              configNumber(stateConfig, QStringLiteral("rankSpacing"), 50.0),
              handDrawn, handDrawnSeed);
      style::ThemeDefaults stateTheme;
      stateTheme.mainBkg = themeVars.mainBkg;
      stateTheme.nodeBorder = themeVars.border1;
      stateTheme.lineColor = themeVars.lineColor;
      stateTheme.strokeWidth = themeVars.strokeWidth;
      stateTheme.textColor = themeVars.primaryTextColor;
      stateTheme.fontFamily = style.fontFamily;
      stateTheme.fontSize = QString::number(style.fontSize) + QStringLiteral("px");
      state::StateScene scene = state::buildStateScene(
          input, placement, std::move(style), stateTheme,
          handDrawn, handDrawnSeed, links, &nodeMeasureCss, &edgeMeasureCss);
      // Stamp the resolved per-element CSS onto the immutable scene.
      scene.markerCss = elementCss(css.markerPath);
      for (state::StateSceneNode& node : scene.nodes) {
        const auto shape = css.nodeShapes.constFind(node.id);
        if (shape != css.nodeShapes.constEnd()) node.shapeCss = elementCss(*shape);
        const auto shapeStroke = css.nodeShapeStrokes.constFind(node.id);
        if (shapeStroke != css.nodeShapeStrokes.constEnd())
          node.shapeStrokeCss = elementCss(*shapeStroke);
        const auto innerFill = css.nodeInnerFills.constFind(node.id);
        if (innerFill != css.nodeInnerFills.constEnd())
          node.innerCss = elementCss(*innerFill);
        const auto innerStroke = css.nodeInnerStrokes.constFind(node.id);
        if (innerStroke != css.nodeInnerStrokes.constEnd())
          node.innerStrokeCss = elementCss(*innerStroke);
        const auto label = css.nodeLabels.constFind(node.id);
        if (label != css.nodeLabels.constEnd()) node.labelCss = elementCss(*label);
        const auto labelP = css.nodeLabelPs.constFind(node.id);
        if (labelP != css.nodeLabelPs.constEnd())
          node.labelTextCss = elementCss(*labelP);
        const auto description = css.nodeDescriptions.constFind(node.id);
        if (description != css.nodeDescriptions.constEnd())
          node.descriptionCss = elementCss(*description);
        const auto descriptionP = css.nodeDescriptionPs.constFind(node.id);
        if (descriptionP != css.nodeDescriptionPs.constEnd())
          node.descriptionTextCss = elementCss(*descriptionP);
        const auto divider = css.nodeDividers.constFind(node.id);
        if (divider != css.nodeDividers.constEnd())
          node.dividerCss = elementCss(*divider);
        node.shapeVisible = node.shapeVisible && node.shapeCss.displayed;
      }
      for (state::StateSceneNode& cluster : scene.clusters) {
        const bool divider = cluster.shape == QLatin1String("divider");
        const auto outer = (divider ? css.clusterDividers : css.clusterOuters)
                               .constFind(cluster.id);
        if (outer != (divider ? css.clusterDividers : css.clusterOuters)
                          .constEnd())
          cluster.shapeCss = elementCss(*outer);
        const auto inner = css.clusterInners.constFind(cluster.id);
        if (inner != css.clusterInners.constEnd() && cluster.innerBounds.isValid())
          cluster.innerCss = elementCss(*inner);
        const auto label = css.clusterLabels.constFind(cluster.id);
        if (label != css.clusterLabels.constEnd())
          cluster.labelCss = elementCss(*label);
        const auto labelP = css.clusterLabelPs.constFind(cluster.id);
        if (labelP != css.clusterLabelPs.constEnd())
          cluster.labelTextCss = elementCss(*labelP);
      }
      for (state::StateSceneEdge& edge : scene.edges) {
        const auto path = css.edgePaths.constFind(edge.id);
        if (path != css.edgePaths.constEnd()) {
          edge.shapeCss = elementCss(*path);
          if (!path->stroke.isEmpty())
            edge.stroke = path->stroke;
          edge.strokeWidth = path->strokeWidth;
        }
        const auto label = css.edgeLabelSpans.constFind(edge.id);
        if (label != css.edgeLabelSpans.constEnd()) edge.labelCss = elementCss(*label);
        // The p background is written verbatim — "transparent" (CSS initial)
        // clears the theme background; the painter decides via alpha. The p's
        // own element channels ride along: it sits INSIDE the span, so
        // `.edgeLabel p { opacity:.5 }` dims the background AND text without
        // touching the span's computed values.
        const auto background = css.edgeLabelBackgrounds.constFind(edge.id);
        if (background != css.edgeLabelBackgrounds.constEnd()) {
          edge.labelBackground = background->backgroundColor;
          edge.labelBackgroundCss = elementCss(*background);
        }
      }
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      // naturalSize = the rounded client size (upstream's fractional viewBox
      // width is the svg's used CSS width; round, matching the Flowchart
      // convention — ceil inflated a 26.40625-wide diagram to 27).
      entry.naturalSize = QSize(qRound(scene.bounds.width()), qRound(scene.bounds.height()));
      entry.scene = std::make_shared<const state::StateScene>(std::move(scene));
      finalizeReadyEntry(entry, std::move(metadata));
      return entry;
  }
};

}  // namespace

const Diagram& stateDiagramAdapter() {
  static const StateDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
