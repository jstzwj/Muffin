#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/theme/FlowTheme.h"

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

// flowchart behind the Diagram contract. Body is the former renderSource()
// flowchart fallthrough, verbatim.
struct FlowchartDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("flowchart"), QStringLiteral("flowchart-v2")};
  }
  QString cssClass() const override { return QStringLiteral("flowchart"); }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const flowchart::Flowchart chart = flowchart::Flowchart::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme), themeOverrides(pre.config));
    const QJsonObject flowConfig = pre.config.value(QStringLiteral("flowchart")).toObject();
    const flowchart::FlowLook look = flowchart::parseFlowLook(
        pre.config.value(QStringLiteral("look")).toString());
    flowchart::FlowTextOptions textOptions;
    textOptions.fontFamily = firstFontFamily(themeVars.fontFamily);
    textOptions.fontPixelSize = pixelValue(themeVars.fontSize, 16.0);
    textOptions.lineHeight = textOptions.fontPixelSize * 1.5;
    const qreal padding = configNumber(flowConfig, QStringLiteral("padding"), 15.0);
    const qreal diagramPadding = configNumber(
        flowConfig, QStringLiteral("diagramPadding"), 8.0);
    textOptions.horizontalPadding = padding * 2.0;
    textOptions.verticalPadding = padding;
    textOptions.look = look;
    const QMap<QString, QSizeF> sizes = flowchart::measureFlowchartNodes(chart.data(), textOptions);
    flowchart::FlowLayoutOptions layoutOptions;
    layoutOptions.look = look;
    layoutOptions.nodePadding = padding;
    layoutOptions.nodeSpacing = configNumber(flowConfig, QStringLiteral("nodeSpacing"), 50.0);
    layoutOptions.rankSpacing = configNumber(flowConfig, QStringLiteral("rankSpacing"), 50.0);
    const QString curve = flowConfig.value(QStringLiteral("curve")).toString();
    if (!curve.isEmpty()) layoutOptions.curve = curve;
    for (const flowchart::FlowEdge& edge : chart.data().edges) {
      if (!edge.text.isEmpty()) {
        const flowchart::FlowEdgeLabelLayout prepared =
            flowchart::layoutFlowchartEdgeLabel(edge, textOptions);
        layoutOptions.measuredEdgeLabels.insert(edge.id, prepared.size);
        layoutOptions.preparedEdgeLabels.insert(edge.id, prepared);
      }
    }
    for (const flowchart::FlowSubgraph& subgraph : chart.data().subgraphs)
      if (!subgraph.title.isEmpty())
        layoutOptions.measuredClusterLabels.insert(
            subgraph.id,
            flowchart::measureFlowchartClusterLabel(subgraph, textOptions));
    const flowchart::FlowLayoutResult layout = flowchart::layoutFlowchartNodes(chart.data(), sizes, layoutOptions);
    const quint32 handDrawnSeed = static_cast<quint32>(
        std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, chart.data().title, chart.data().accTitle,
        chart.data().accDescription, themeVars.textColor,
        textOptions.fontFamily, 18.0,
        configNumber(flowConfig, QStringLiteral("titleTopMargin"), 25.0),
        diagramPadding);
    flowscene::FlowScene scene = flowscene::buildFlowScene(
        chart.data(), layout, themeVars, look, handDrawnSeed);
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
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
