#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/treemap/TreemapDiagram.h"
#include "mermaid/treemap/TreemapScene.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue scalar(const QJsonObject &object, const char *key,
                  const QJsonValue &fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct TreemapDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("treemap")}; }
  QString cssClass() const override { return QStringLiteral("treemap"); }

  MermaidRenderEntry render(const MermaidPreprocessResult &pre,
                            const QString &type,
                            const QString &theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject raw =
        pre.config.value(QStringLiteral("treemap")).toObject();

    treemap::TreemapConfig config;
    config.useMaxWidth = scalar(raw, "useMaxWidth", true);
    config.padding = scalar(raw, "padding", 10.0);
    config.diagramPadding = scalar(raw, "diagramPadding", 8.0);
    config.showValues = scalar(raw, "showValues", true);
    config.nodeWidth = scalar(raw, "nodeWidth", 100.0);
    config.nodeHeight = scalar(raw, "nodeHeight", 40.0);
    config.valueFormat = scalar(raw, "valueFormat", QStringLiteral(","));

    treemap::TreemapSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    style.textColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    for (int i = 0; i < 12; ++i) {
      style.cScale[i] = themeVars.cScale[i];
      style.cScalePeer[i] = themeVars.cScalePeer[i];
      style.cScaleLabel[i] = themeVars.cScaleLabel[i];
    }
    const QJsonObject nestedTheme =
        pre.config.value(QStringLiteral("themeVariables")).toObject()
            .value(QStringLiteral("treemap")).toObject();
    if (nestedTheme.value(QStringLiteral("titleColor")).isString())
      style.titleColor = nestedTheme.value(QStringLiteral("titleColor")).toString();
    if (nestedTheme.value(QStringLiteral("titleFontSize")).isString()) {
      const CssLengthContext root = pieCssLengthContext(style.fontFamily, 16.0);
      style.titleFontSize = cssFontSizePx(
          nestedTheme.value(QStringLiteral("titleFontSize")).toString(), root);
    }

    treemap::TreemapData data = treemap::TreemapDiagram::parse(pre.code);
    if (!data.hasTitleDirective && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);
    treemap::TreemapScene scene =
        treemap::buildTreemapScene(data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, data.title, data.accTitle, data.accDescr,
                       themeVars.titleColor, themeVars.fontFamily, 14.0);
    // The renderer owns the visible title inside the SVG at y=15.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.rasterBounds.width()),
                              qRound(scene.rasterBounds.height()));
    entry.scene =
        std::make_shared<const treemap::TreemapScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

} // namespace

const Diagram &treemapDiagramAdapter() {
  static const TreemapDiagramImpl adapter;
  return adapter;
}

} // namespace muffin::mermaid::editor
