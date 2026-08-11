#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/sankey/SankeyDiagram.h"
#include "mermaid/sankey/SankeyScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QRegularExpression>
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

struct SankeyDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("sankey")}; }
  QString cssClass() const override { return QStringLiteral("sankey"); }

  MermaidRenderEntry render(const MermaidPreprocessResult &pre,
                            const QString &type,
                            const QString &theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject raw =
        pre.config.value(QStringLiteral("sankey")).toObject();

    sankey::SankeyConfig config;
    config.width = scalar(raw, "width", 600.0);
    config.height = scalar(raw, "height", 400.0);
    config.useMaxWidth = scalar(raw, "useMaxWidth", true);
    config.linkColor = scalar(raw, "linkColor", QStringLiteral("gradient"));
    config.nodeAlignment =
        scalar(raw, "nodeAlignment", QStringLiteral("justify"));
    config.showValues = scalar(raw, "showValues", true);
    config.prefix = scalar(raw, "prefix", QString());
    config.suffix = scalar(raw, "suffix", QString());
    config.nodeWidth = scalar(raw, "nodeWidth", 10.0);
    config.nodePadding = scalar(raw, "nodePadding", 12.0);
    config.labelStyle = scalar(raw, "labelStyle", QStringLiteral("legacy"));
    if (raw.value(QStringLiteral("nodeColors")).isObject()) {
      static const QRegularExpression safeColor(
          QStringLiteral(
              R"(^(?:#[\da-f]{3,8}|rgb\([\d\s%,.]+\)|hsl\([\d\s%,.]+\)|[a-z]+)$)"),
          QRegularExpression::CaseInsensitiveOption);
      const QJsonObject colors =
          raw.value(QStringLiteral("nodeColors")).toObject();
      for (auto it = colors.begin(); it != colors.end(); ++it)
        if (it.value().isString() &&
            safeColor.match(it.value().toString()).hasMatch())
          config.nodeColors.insert(it.key(), it.value());
    }

    sankey::SankeySceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    style.textColor = themeVars.textColor;
    style.mainBkg = themeVars.mainBkg;
    style.background = themeVars.background;

    const sankey::SankeyData data = sankey::SankeyDiagram::parse(pre.code);
    sankey::SankeyScene scene =
        sankey::buildSankeyScene(data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, QString(), QString(), QString(),
                       themeVars.textColor, themeVars.fontFamily, 16.0);
    // Sankey's parser exposes commonDb methods but has no metadata grammar; its
    // renderer does not project frontmatter title/accessibility nodes.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.rasterBounds.width()),
                              qRound(scene.rasterBounds.height()));
    entry.scene = std::make_shared<const sankey::SankeyScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

} // namespace

const Diagram &sankeyDiagramAdapter() {
  static const SankeyDiagramImpl adapter;
  return adapter;
}

} // namespace muffin::mermaid::editor
