#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/block/BlockDiagram.h"
#include "mermaid/block/BlockScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/theme/MermaidCssCascade.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

#include <functional>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue blockScalar(const QJsonObject& object, const char* key,
                       const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct BlockDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("block"), QStringLiteral("block-beta")};
  }
  QString cssClass() const override { return QStringLiteral("block"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw =
        pre.config.value(QStringLiteral("block")).toObject();
    block::BlockConfig config;
    config.padding = blockScalar(raw, "padding", 8.0);
    config.useMaxWidth = blockScalar(raw, "useMaxWidth", true);
    const QJsonValue htmlLabels =
        pre.config.value(QStringLiteral("htmlLabels"));
    config.htmlLabels = htmlLabels.isUndefined() || htmlLabels.isNull()
                            ? true
                            : truthyConfigValue(htmlLabels);
    const QJsonValue look = pre.config.value(QStringLiteral("look"));
    config.look = look.isString() ? look.toString() : QStringLiteral("classic");
    config.handDrawnSeed = quint32(jsNumberValue(
        pre.config.value(QStringLiteral("handDrawnSeed"))));
    config.svgId = QStringLiteral("block-native");

    block::BlockData data = block::BlockDiagram::parse(pre.code);
    csscascade::FlowchartProjection measurementCss;
    csscascade::FlowchartProjection paintCss;
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    const csscascade::FlowchartProjection* cssPtr = nullptr;
    if (!themeCss.trimmed().isEmpty()) {
      flowchart::FlowchartData projectedData;
      projectedData.direction = QStringLiteral("TB");
      std::function<void(const block::BlockNode&)> appendNode =
          [&](const block::BlockNode& node) {
            if (node.id != QLatin1String("root") &&
                node.type != QLatin1String("space")) {
              flowchart::FlowVertex vertex;
              vertex.id = node.id;
              vertex.domId = config.svgId + QLatin1Char('-') + node.id;
              vertex.text = node.label;
              vertex.type = QStringLiteral("rect");
              vertex.styles = node.styles;
              vertex.classes = node.classes;
              projectedData.vertices.append(std::move(vertex));
            }
            for (const block::BlockNode& child : node.children)
              appendNode(child);
          };
      appendNode(data.root);
      for (const block::BlockEdge& source : data.edges) {
        flowchart::FlowEdge edge;
        edge.id = source.id;
        edge.start = source.start;
        edge.end = source.end;
        edge.text = source.label;
        projectedData.edges.append(std::move(edge));
      }
      for (const block::BlockClass& source : data.classes) {
        flowchart::FlowClass value;
        value.id = source.id;
        value.styles = source.styles;
        value.textStyles = source.textStyles;
        projectedData.classes.append(std::move(value));
      }
      paintCss = csscascade::resolveFlowchart(projectedData, themeVars, themeCss);
      // calculateBlockSizes inserts one node into the temporary `.block`
      // group and removes it before inserting the next. Structural selectors
      // therefore see every node as :first-child during measurement, while
      // the final paint DOM contains all nodes together.
      for (const flowchart::FlowVertex& vertex : projectedData.vertices) {
        flowchart::FlowchartData singleton;
        singleton.direction = projectedData.direction;
        singleton.vertices.append(vertex);
        singleton.classes = projectedData.classes;
        const auto measured =
            csscascade::resolveFlowchart(singleton, themeVars, themeCss);
        measurementCss.nodes.insert(vertex.id,
                                    measured.nodes.value(vertex.id));
        measurementCss.nodeLabels.insert(
            vertex.id, measured.nodeLabels.value(vertex.id));
      }
      cssPtr = &measurementCss;
    }
    block::BlockScene scene = block::buildBlockScene(
        data, std::move(config), themeVars, cssPtr,
        themeCss.trimmed().isEmpty() ? nullptr : &paintCss);

    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), themeVars.textColor,
        themeVars.fontFamily, cssFontSizePx(
            themeVars.fontSize,
            pieCssLengthContext(firstFontFamily(themeVars.fontFamily), 16.0)));
    // Block has no commonDb title/accessibility productions. Mermaid 11.16.0
    // ignores frontmatter metadata for this family.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.rasterBounds.width()),
                              qCeil(scene.rasterBounds.height()));
    entry.scene =
        std::make_shared<const block::BlockScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& blockDiagramAdapter() {
  static const BlockDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
