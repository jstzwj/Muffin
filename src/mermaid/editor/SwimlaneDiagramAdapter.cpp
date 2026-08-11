#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/flowchart/SwimlaneLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/FlowTheme.h"

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
    const flowchart::Flowchart chart = flowchart::Flowchart::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
        themeOverrides(pre.config));
    const QJsonObject flowConfig =
        pre.config.value(QStringLiteral("flowchart")).toObject();
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

    const quint32 handDrawnSeed = static_cast<quint32>(std::max(
        0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
    const QMap<QString, QSizeF> renderedSizes =
        flowchart::measureFlowchartNodes(chart.data(), textOptions);
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
      const flowchart::FlowEdgeLabelLayout prepared =
          flowchart::layoutFlowchartEdgeLabel(edge, textOptions);
      swimlaneOptions.preparedEdgeLabels.insert(edge.id, prepared);
    }
    for (const flowchart::FlowSubgraph& subgraph : chart.data().subgraphs) {
      if (subgraph.title.isEmpty()) continue;
      swimlaneOptions.measuredClusterLabels.insert(
          subgraph.id,
          flowchart::measureFlowchartClusterLabel(subgraph, textOptions));
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
    flowscene::FlowScene scene = flowscene::buildFlowScene(
        chart.data(), layout, themeVars, look, handDrawnSeed);
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
