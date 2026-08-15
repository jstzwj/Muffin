#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/wardley/WardleyDiagram.h"
#include "mermaid/wardley/WardleyScene.h"

#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

struct WardleyDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("wardley")}; }
  QString cssClass() const override { return QStringLiteral("wardley"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    // Mermaid 11.16 source-entry sanitization removes the entire
    // `wardley-beta` config object. Diagram-local `size` remains live in the
    // parser, while renderer config stays at these upstream defaults.
    wardley::WardleyConfig config;

    wardley::WardleySceneStyle style;
    // Wardley's stylesheet never declares font-family. SVG text therefore
    // inherits Chromium's initial serif family, independently of the source
    // fontFamily setting (the browser oracle resolves it to Times New Roman).
    style.fontFamily = QStringLiteral("Times New Roman");
    style.backgroundColor = themeVars.wardley.backgroundColor;
    style.axisColor = themeVars.wardley.axisColor;
    style.axisTextColor = themeVars.wardley.axisTextColor;
    style.gridColor = themeVars.wardley.gridColor;
    style.componentFill = themeVars.wardley.componentFill;
    style.componentStroke = themeVars.wardley.componentStroke;
    style.componentLabelColor = themeVars.wardley.componentLabelColor;
    style.linkStroke = themeVars.wardley.linkStroke;
    style.evolutionStroke = themeVars.wardley.evolutionStroke;
    // These renderer attributes are not overridden by source-entry styles:
    // Mermaid's config sanitizer drops the three wardley.annotation* keys.
    style.annotationStroke = style.axisColor;
    style.annotationTextColor = style.axisTextColor;
    style.annotationFill = QStringLiteral("white");

    const wardley::WardleyData data =
        wardley::WardleyDiagram::parse(pre.code);
    wardley::WardleyScene scene = wardley::buildWardleyScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.axisTextColor, scene.style.fontFamily,
        scene.config.axisFontSize);
    // The renderer draws the visible title at padding / 2. Frontmatter title
    // is intentionally ignored by this family, matching its DB.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene =
        std::make_shared<const wardley::WardleyScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& wardleyDiagramAdapter() {
  static const WardleyDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
