#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/block/BlockDiagram.h"
#include "mermaid/block/BlockScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

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
    block::BlockScene scene = block::buildBlockScene(
        data, std::move(config), themeVars);

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
