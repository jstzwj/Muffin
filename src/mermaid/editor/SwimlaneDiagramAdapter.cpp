#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/flowchart/SwimlaneLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

bool swimlaneBoolean(const QJsonObject& config, const char* key, bool fallback) {
  const QJsonValue value = config.value(QString::fromLatin1(key));
  return value.isUndefined() || value.isNull() ? fallback : truthyConfigValue(value);
}

struct SwimlaneDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("swimlane")}; }
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
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
        themeOverrides(pre.config));
    const QJsonObject swimlaneConfig =
        pre.config.value(QStringLiteral("swimlane")).toObject();
    const flowchart::FlowLook look = flowchart::parseFlowLook(
        pre.config.value(QStringLiteral("look")).toString());

    flowchart::FlowTextOptions textOptions;
    textOptions.fontFamily = firstFontFamily(themeVars.fontFamily);
    textOptions.fontPixelSize = pixelValue(themeVars.fontSize, 16.0);
    textOptions.lineHeight = textOptions.fontPixelSize * 1.5;
    const qreal padding = configNumber(flowConfig, QStringLiteral("padding"), 15.0);
    const qreal diagramPadding =
        configNumber(flowConfig, QStringLiteral("diagramPadding"), 8.0);
    textOptions.horizontalPadding = padding * 2.0;
    textOptions.verticalPadding = padding;
    textOptions.look = look;
    textOptions.chromiumInlineWidth = true;
    textOptions.chromiumSvgTerminalPhase = true;
    const QJsonValue rawHtmlLabels =
        pre.config.value(QStringLiteral("htmlLabels"));
    textOptions.htmlLabels = rawHtmlLabels.isUndefined() || rawHtmlLabels.isNull()
        ? true : truthyConfigValue(rawHtmlLabels);

    const csscascade::FlowchartProjection css = csscascade::resolveFlowchart(
        chart.data(), themeVars,
        pre.config.value(QStringLiteral("themeCSS")).toString(), true,
        flowchart::flowLookName(look), textOptions.htmlLabels);
    QMap<QString, flowchart::FlowTextOptions> perNodeTextOptions;
    for (const flowchart::FlowVertex& vertex : chart.data().vertices) {
      const auto style = css.nodeLabels.constFind(vertex.id);
      if (style == css.nodeLabels.constEnd()) continue;
      flowchart::FlowTextOptions nodeOptions = textOptions;
      nodeOptions.fontFamily = style->fontFamily;
      const CssLengthContext context = pieCssLengthContext(
          nodeOptions.fontFamily, textOptions.fontPixelSize);
      nodeOptions.fontPixelSize = cssFontSizePx(style->fontSize, context);
      nodeOptions.lineHeight = nodeOptions.fontPixelSize * 1.5;
      nodeOptions.fontWeight = cssFontWeightToQt(
          QJsonValue(style->fontWeight), QFont::Normal);
      perNodeTextOptions.insert(vertex.id, nodeOptions);
    }

    const quint32 handDrawnSeed = static_cast<quint32>(std::max(
        0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
    QMap<QString, QSizeF> renderedSizes =
        flowchart::measureFlowchartNodes(chart.data(), textOptions,
                                         perNodeTextOptions);
    for (const flowchart::FlowVertex& vertex : chart.data().vertices) {
      const auto style = css.nodes.constFind(vertex.id);
      if (style != css.nodes.constEnd() && !style->displayed())
        renderedSizes.insert(vertex.id, QSizeF(60.0, 30.0));
    }
    const QMap<QString, QSizeF> sizes = look == flowchart::FlowLook::HandDrawn
        ? flowchart::measureSwimlaneHandDrawnNodes(
              chart.data(), renderedSizes, handDrawnSeed)
        : renderedSizes;
    flowchart::SwimlaneLayoutOptions swimlaneOptions;
    swimlaneOptions.look = look;
    swimlaneOptions.renderedNodeSizes = renderedSizes;
    swimlaneOptions.nodePadding = padding;
    swimlaneOptions.labelLineHeight = textOptions.lineHeight;
    swimlaneOptions.nodeSpacing =
        configNumber(flowConfig, QStringLiteral("nodeSpacing"), 50.0);
    swimlaneOptions.rankSpacing =
        configNumber(flowConfig, QStringLiteral("rankSpacing"), 50.0);
    swimlaneOptions.ignoreCrossLaneEdges =
        swimlaneBoolean(swimlaneConfig, "ignoreCrossLaneEdges", true);
    swimlaneOptions.optimizeRanksByCrossings =
        swimlaneBoolean(swimlaneConfig, "optimizeRanksByCrossings", true);
    swimlaneOptions.automaticLaneOrdering =
        swimlaneBoolean(swimlaneConfig, "automaticLaneOrdering", false);
    const QString curve = flowConfig.value(QStringLiteral("curve")).toString();
    if (!curve.isEmpty()) swimlaneOptions.curve = curve;
    const QJsonValue rawLineHops =
        swimlaneConfig.value(QStringLiteral("lineHops"));
    if (rawLineHops.isBool() && !rawLineHops.toBool())
      swimlaneOptions.lineHops = QStringLiteral("false");
    else if (rawLineHops.isString() && !rawLineHops.toString().isEmpty())
      swimlaneOptions.lineHops = rawLineHops.toString();
    for (const flowchart::FlowEdge& edge : chart.data().edges) {
      if (edge.text.isEmpty()) continue;
      flowchart::FlowTextOptions labelOptions = textOptions;
      labelOptions.edgeLabelRectNode = true;
      const auto style = css.edgeLabels.constFind(edge.id);
      if (style != css.edgeLabels.constEnd()) {
        labelOptions.fontFamily = style->fontFamily;
        const CssLengthContext context = pieCssLengthContext(
            labelOptions.fontFamily, textOptions.fontPixelSize);
        labelOptions.fontPixelSize = cssFontSizePx(style->fontSize, context);
        labelOptions.lineHeight = labelOptions.fontPixelSize * 1.5;
        labelOptions.fontWeight = cssFontWeightToQt(
            QJsonValue(style->fontWeight), QFont::Normal);
      }
      const flowchart::FlowEdgeLabelLayout prepared =
          flowchart::layoutFlowchartEdgeLabel(edge, labelOptions);
      swimlaneOptions.preparedEdgeLabels.insert(edge.id, prepared);
    }
    for (const flowchart::FlowSubgraph& subgraph : chart.data().subgraphs) {
      if (subgraph.title.isEmpty()) continue;
      flowchart::FlowTextOptions labelOptions = textOptions;
      const auto style = css.clusterLabels.constFind(subgraph.id);
      if (style != css.clusterLabels.constEnd()) {
        labelOptions.fontFamily = style->fontFamily;
        const CssLengthContext context = pieCssLengthContext(
            labelOptions.fontFamily, textOptions.fontPixelSize);
        labelOptions.fontPixelSize = cssFontSizePx(style->fontSize, context);
        labelOptions.lineHeight = labelOptions.fontPixelSize * 1.5;
        labelOptions.fontWeight = cssFontWeightToQt(
            QJsonValue(style->fontWeight), QFont::Normal);
      }
      swimlaneOptions.measuredClusterLabels.insert(
          subgraph.id,
          flowchart::measureFlowchartClusterLabel(subgraph, labelOptions));
    }

    flowchart::FlowLayoutResult layout;
    const QString requestedLayout =
        pre.config.value(QStringLiteral("layout")).toString();
    if (requestedLayout == QLatin1String("dagre")) {
      flowchart::FlowLayoutOptions dagreOptions;
      dagreOptions.look = look;
      dagreOptions.nodePadding = padding;
      dagreOptions.nodeSpacing = swimlaneOptions.nodeSpacing;
      dagreOptions.rankSpacing = swimlaneOptions.rankSpacing;
      dagreOptions.curve = swimlaneOptions.curve;
      dagreOptions.preserveDagreCoordinates = true;
      dagreOptions.dagreWrapperMargin = 8.0;
      dagreOptions.diagramPadding = diagramPadding;
      dagreOptions.preparedEdgeLabels = swimlaneOptions.preparedEdgeLabels;
      for (auto it = swimlaneOptions.preparedEdgeLabels.cbegin();
           it != swimlaneOptions.preparedEdgeLabels.cend(); ++it)
        dagreOptions.measuredEdgeLabels.insert(it.key(), it->size);
      dagreOptions.measuredClusterLabels =
          swimlaneOptions.measuredClusterLabels;
      layout = flowchart::layoutFlowchartNodes(
          chart.data(), renderedSizes, dagreOptions);
    } else {
      layout = flowchart::layoutSwimlaneNodes(
          chart.data(), sizes, swimlaneOptions);
    }
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, chart.data().title, chart.data().accTitle,
        chart.data().accDescription, themeVars.textColor,
        textOptions.fontFamily, 18.0,
        configNumber(flowConfig, QStringLiteral("titleTopMargin"), 25.0),
        diagramPadding);
    metadata.svgUseMaxWidth =
        swimlaneBoolean(flowConfig, "useMaxWidth", true);
    flowscene::FlowSceneTextOptions sceneTextOptions;
    sceneTextOptions.nodeHtmlLabels = textOptions.htmlLabels;
    sceneTextOptions.auxiliaryHtmlLabels = textOptions.htmlLabels;
    sceneTextOptions.css = &css;
    flowscene::FlowScene scene = flowscene::buildFlowScene(
        chart.data(), layout, themeVars, look, handDrawnSeed,
        sceneTextOptions, rawShapeRadius(themeVars));
    scene.markerDiagramType = QStringLiteral("swimlane");
    // The svg root's viewBox padding (setupGraphViewbox, upstream default
    // 8) — feeds the fractional client-box channel like flowchart.
    scene.clientPadding = diagramPadding;
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene =
        std::make_shared<const flowscene::FlowScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& swimlaneDiagramAdapter() {
  static const SwimlaneDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
