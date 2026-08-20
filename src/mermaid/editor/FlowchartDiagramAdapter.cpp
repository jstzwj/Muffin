#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QMap>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

QJsonValue effectiveHtmlLabelsValue(const QJsonObject& config,
                                    const QJsonObject& flowConfig) {
  QJsonValue value = config.value(QStringLiteral("htmlLabels"));
  if (value.isUndefined() || value.isNull())
    value = flowConfig.value(QStringLiteral("htmlLabels"));
  if (value.isUndefined() || value.isNull()) return QJsonValue(true);
  return value;
}

// flowchart behind the Diagram contract. Body is the former renderSource()
// flowchart fallthrough, verbatim.
struct FlowchartDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("flowchart"), QStringLiteral("flowchart-v2"),
            QStringLiteral("flowchart-elk")};
  }
  QString cssClass() const override { return QStringLiteral("flowchart"); }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const QJsonObject flowConfig =
        pre.config.value(QStringLiteral("flowchart")).toObject();
    flowchart::FlowchartParseOptions parseOptions;
    parseOptions.inheritDir =
        flowConfig.value(QStringLiteral("inheritDir")).toBool(false);
    const flowchart::Flowchart chart =
        flowchart::Flowchart::parse(pre.code, parseOptions);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme), themeOverrides(pre.config));
    const flowchart::FlowLook look = flowchart::parseFlowLook(
        pre.config.value(QStringLiteral("look")).toString());
    flowchart::FlowTextOptions textOptions;
    textOptions.fontFamily = firstFontFamily(themeVars.fontFamily);
    textOptions.fontPixelSize = pixelValue(themeVars.fontSize, 16.0);
    textOptions.lineHeight = textOptions.fontPixelSize * 1.5;
    const QJsonValue globalHtmlLabels = pre.config.value(
        QStringLiteral("htmlLabels"));
    const bool effectiveHtmlLabels = evaluateConfigValue(
        effectiveHtmlLabelsValue(pre.config, flowConfig));
    // insertNode uses node.useHtmlLabels || evaluate(config.htmlLabels), while
    // edges and clusters use getEffectiveHtmlLabels(). With only the legacy
    // flowchart alias set to false this deliberately produces mixed output.
    const bool nodeHtmlLabels = effectiveHtmlLabels ||
        evaluateConfigValue(globalHtmlLabels);
    const qreal wrappingWidth = configNumber(
        flowConfig, QStringLiteral("wrappingWidth"), 200.0);
    textOptions.htmlLabels = nodeHtmlLabels;
    textOptions.maximumLineWidth = wrappingWidth;
    const qreal padding = configNumber(flowConfig, QStringLiteral("padding"), 15.0);
    const qreal diagramPadding = configNumber(
        flowConfig, QStringLiteral("diagramPadding"), 8.0);
    textOptions.horizontalPadding = padding * 2.0;
    textOptions.verticalPadding = padding;
    textOptions.look = look;
    const csscascade::FlowchartProjection css = csscascade::resolveFlowchart(
        chart.data(), themeVars,
        pre.config.value(QStringLiteral("themeCSS")).toString());
    QMap<QString, flowchart::FlowTextOptions> perNodeTextOptions;
    for (const flowchart::FlowVertex& vertex : chart.data().vertices) {
      const auto styleIt = css.nodeLabels.constFind(vertex.id);
      if (styleIt == css.nodeLabels.constEnd()) continue;
      flowchart::FlowTextOptions nodeOptions = textOptions;
      nodeOptions.fontFamily = styleIt->fontFamily;
      const CssLengthContext context = pieCssLengthContext(
          nodeOptions.fontFamily, textOptions.fontPixelSize);
      nodeOptions.fontPixelSize = cssFontSizePx(styleIt->fontSize, context);
      nodeOptions.lineHeight = nodeOptions.fontPixelSize * 1.5;
      nodeOptions.fontWeight = cssFontWeightToQt(
          QJsonValue(styleIt->fontWeight), QFont::Normal);
      perNodeTextOptions.insert(vertex.id, nodeOptions);
    }
    QMap<QString, QSizeF> sizes = flowchart::measureFlowchartNodes(
        chart.data(), textOptions, perNodeTextOptions);
    for (const flowchart::FlowVertex& vertex : chart.data().vertices) {
      const auto style = css.nodes.constFind(vertex.id);
      // display:none removes the node box from getBBox, so Dagre sees the
      // 60x30 fallback; visibility:hidden KEEPS the measured box (CSS:
      // visibility preserves layout) and only gates painting.
      if (style != css.nodes.constEnd() &&
          (style->display.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0 ||
           !style->ancestorRenderable))
        sizes.insert(vertex.id, QSizeF(60.0, 30.0));
    }
    flowchart::FlowLayoutOptions layoutOptions;
    layoutOptions.look = look;
    layoutOptions.nodePadding = padding;
    layoutOptions.nodeSpacing = configNumber(flowConfig, QStringLiteral("nodeSpacing"), 50.0);
    layoutOptions.rankSpacing = configNumber(flowConfig, QStringLiteral("rankSpacing"), 50.0);
    // Client-box contract: upstream's viewBox = inkBBox ± padding with NO
    // translate, so the scene must keep dagre's ABSOLUTE margin-anchored
    // coordinates (translateGraph places the node bbox at (8, 8)) — the old
    // first-vertex re-centering moved the origin and broke the fractional
    // viewBox oracle. The wrapper margin is HARDCODED 8 upstream, independent
    // of diagramPadding.
    layoutOptions.preserveDagreCoordinates = true;
    layoutOptions.dagreWrapperMargin = 8.0;
    const QJsonObject subGraphTitleMargin = flowConfig.value(
        QStringLiteral("subGraphTitleMargin")).toObject();
    layoutOptions.subGraphTitleTopMargin = configNumber(
        subGraphTitleMargin, QStringLiteral("top"), 0.0);
    layoutOptions.subGraphTitleBottomMargin = configNumber(
        subGraphTitleMargin, QStringLiteral("bottom"), 0.0);
    const QString curve = flowConfig.value(QStringLiteral("curve")).toString();
    if (!curve.isEmpty()) layoutOptions.curve = curve;
    for (const flowchart::FlowEdge& edge : chart.data().edges) {
      if (!edge.text.isEmpty()) {
        flowchart::FlowTextOptions auxiliaryTextOptions = textOptions;
        auxiliaryTextOptions.htmlLabels = effectiveHtmlLabels;
        const auto style = css.edgeLabels.constFind(edge.id);
        if (style != css.edgeLabels.constEnd()) {
          auxiliaryTextOptions.fontFamily = style->fontFamily;
          const CssLengthContext context = pieCssLengthContext(
              auxiliaryTextOptions.fontFamily, textOptions.fontPixelSize);
          auxiliaryTextOptions.fontPixelSize = cssFontSizePx(
              style->fontSize, context);
          auxiliaryTextOptions.lineHeight = auxiliaryTextOptions.fontPixelSize * 1.5;
          auxiliaryTextOptions.fontWeight = cssFontWeightToQt(
              QJsonValue(style->fontWeight), QFont::Normal);
        }
        const flowchart::FlowEdgeLabelLayout prepared =
            flowchart::layoutFlowchartEdgeLabel(edge, auxiliaryTextOptions);
        layoutOptions.measuredEdgeLabels.insert(edge.id, prepared.size);
        layoutOptions.preparedEdgeLabels.insert(edge.id, prepared);
      }
    }
    for (const flowchart::FlowSubgraph& subgraph : chart.data().subgraphs)
      if (!subgraph.title.isEmpty())
      {
        flowchart::FlowTextOptions auxiliaryTextOptions = textOptions;
        auxiliaryTextOptions.htmlLabels = effectiveHtmlLabels;
        const auto style = css.clusterLabels.constFind(subgraph.id);
        if (style != css.clusterLabels.constEnd()) {
          auxiliaryTextOptions.fontFamily = style->fontFamily;
          const CssLengthContext context = pieCssLengthContext(
              auxiliaryTextOptions.fontFamily, textOptions.fontPixelSize);
          auxiliaryTextOptions.fontPixelSize = cssFontSizePx(
              style->fontSize, context);
          auxiliaryTextOptions.lineHeight = auxiliaryTextOptions.fontPixelSize * 1.5;
          auxiliaryTextOptions.fontWeight = cssFontWeightToQt(
              QJsonValue(style->fontWeight), QFont::Normal);
        }
        layoutOptions.measuredClusterLabels.insert(
            subgraph.id,
            flowchart::measureFlowchartClusterLabel(subgraph,
                                                    auxiliaryTextOptions));
      }
    const flowchart::FlowLayoutResult layout = flowchart::layoutFlowchartNodes(chart.data(), sizes, layoutOptions);
    const quint32 handDrawnSeed = static_cast<quint32>(
        std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, chart.data().title, chart.data().accTitle,
        chart.data().accDescription, themeVars.textColor,
        textOptions.fontFamily, 18.0,
        configNumber(flowConfig, QStringLiteral("titleTopMargin"), 25.0),
        diagramPadding);
    flowscene::FlowSceneTextOptions sceneTextOptions;
    sceneTextOptions.nodeHtmlLabels = nodeHtmlLabels;
    sceneTextOptions.auxiliaryHtmlLabels = effectiveHtmlLabels;
    sceneTextOptions.maximumLineWidth = wrappingWidth;
    sceneTextOptions.css = &css;
    flowscene::FlowScene scene = flowscene::buildFlowScene(
        chart.data(), layout, themeVars, look, handDrawnSeed,
        sceneTextOptions, rawShapeRadius(themeVars));
    // The svg root's viewBox padding (setupGraphViewbox, upstream default
    // 8) — feeds the fractional client-box channel.
    scene.clientPadding = diagramPadding;
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    // Chromium screenshots the fractional SVG client box at the nearest
    // device pixel. Preserve the fractional scene for SVG/viewBox output, but
    // use that replaced-element raster rule for the production PNG viewport.
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene = std::make_shared<const flowscene::FlowScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& flowchartDiagramAdapter() {
  static const FlowchartDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
