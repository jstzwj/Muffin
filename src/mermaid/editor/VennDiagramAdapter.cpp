#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/venn/VennDiagram.h"
#include "mermaid/venn/VennScene.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <stdexcept>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue scalar(const QJsonObject& object, const char* key,
                  const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct VennDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("venn")}; }
  QString cssClass() const override { return QStringLiteral("venn"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw = pre.config.value(QStringLiteral("venn")).toObject();
    venn::VennConfig config;
    config.width = scalar(raw, "width", QJsonValue(800.0));
    config.height = scalar(raw, "height", QJsonValue(450.0));
    config.padding = scalar(raw, "padding", QJsonValue(8.0));
    config.useMaxWidth = scalar(raw, "useMaxWidth", QJsonValue(true));
    config.useDebugLayout =
        scalar(raw, "useDebugLayout", QJsonValue(false));
    config.handDrawnSeed =
        scalar(pre.config, "handDrawnSeed", QJsonValue(0.0));

    venn::VennSceneStyle style;
    const QJsonValue look = pre.config.value(QStringLiteral("look"));
    style.look = look.isString() ? look.toString() : QStringLiteral("classic");
    style.fontFamily = themeVars.fontFamily;
    style.background = themeVars.background;
    style.primaryColor = themeVars.primaryColor;
    style.primaryTextColor = themeVars.primaryTextColor;
    style.textColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    style.vennTitleTextColor = themeVars.vennTitleTextColor;
    style.vennSetTextColor = themeVars.vennSetTextColor;
    for (const QString& color : themeVars.venn) style.colors.append(color);

    venn::VennData data = venn::VennDiagram::parse(pre.code);
    if (!data.hasTitleDirective && !pre.title.isEmpty()) data.title = pre.title;
    if (data.subsets.isEmpty())
      throw std::runtime_error(
          "Cannot read properties of undefined (reading 'set')");

    venn::VennScene scene = venn::buildVennScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.vennTitleTextColor, scene.style.fontFamily,
        32.0 * scene.scale);
    // Venn owns its visual title and its renderer does not project commonDb
    // title/accessibility fields into SVG metadata in Mermaid 11.16.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.rasterBounds.width()),
                              qRound(scene.rasterBounds.height()));
    entry.scene = std::make_shared<const venn::VennScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& vennDiagramAdapter() {
  static const VennDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
