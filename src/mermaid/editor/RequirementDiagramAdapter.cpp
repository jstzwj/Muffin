#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/requirement/RequirementDiagram.h"
#include "mermaid/requirement/RequirementLayout.h"
#include "mermaid/requirement/RequirementScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>
#include <QString>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

// requirementDiagram behind the Diagram contract. Requirement routes through
// the shared dagre pipeline (no custom layout). The upstream renderer reads
// config.state for spacing (nodeSpacing/rankSpacing/titleTopMargin/useMaxWidth),
// so this adapter mirrors that: it never reads config.requirement (all
// requirement-specific fields are upstream-inert per the config-matrix finding).
struct RequirementDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("requirement")}; }
  QString cssClass() const override { return QStringLiteral("requirementDiagram"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const requirement::RequirementDiagram diagram =
        requirement::RequirementDiagram::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
        themeOverrides(pre.config));
    const requirement::RequirementLayoutInput input =
        requirement::buildRequirementLayoutInput(diagram.data());
    const QString fontFamily = firstFontFamily(themeVars.fontFamily);
    const qreal fontSize = pixelValue(themeVars.fontSize, 16.0);
    // Requirement reads config.state for spacing (RequirementDB.getData →
    // conf = config.state → nodeSpacing/rankSpacing). titleTopMargin likewise.
    const QJsonObject stateConfig = pre.config.value(QStringLiteral("state")).toObject();
    const qreal nodeSpacing = configNumber(stateConfig, QStringLiteral("nodeSpacing"), 50.0);
    const qreal rankSpacing = configNumber(stateConfig, QStringLiteral("rankSpacing"), 50.0);
    const requirement::RequirementLayoutMeasurements measurements =
        requirement::measureRequirementLayoutInput(input, fontFamily, fontSize, QFont::Normal);
    const requirement::RequirementPlacementResult placement =
        requirement::layoutRequirementDiagramDagre(input, measurements, nodeSpacing, rankSpacing,
                                                   fontFamily, fontSize);
    requirement::RequirementSceneStyle style;
    style.boxFill = themeVars.mainBkg;
    style.boxStroke = themeVars.border1;
    style.textColor = themeVars.primaryTextColor;
    style.dividerColor = themeVars.nodeBorder.isEmpty() ? themeVars.border1 : themeVars.nodeBorder;
    style.lineColor = themeVars.lineColor;
    style.edgeLabelFill = themeVars.edgeLabelBackground.isEmpty()
                              ? themeVars.mainBkg : themeVars.edgeLabelBackground;
    style.edgeLabelColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    // requirementBox default border size is 1.3 (userNodeOverrides:
    // strokeWidth = stylesMap["stroke-width"]?.replace("px","") || 1.3) — it is
    // NOT the theme's generic strokeWidth (which defaults to 1), so hardcode the
    // family default. foregroundFallback = the svg-inherited color used when an
    // inline fill/color is invalid (#333 default / #ccc dark = theme textColor).
    style.foregroundFallback = themeVars.textColor;
    style.strokeWidth = 1.3;
    style.fontFamily = fontFamily;
    style.fontSize = fontSize;
    style.lineHeight = fontSize * 1.5;
    // SVG-root base font-weight (the weight bolder/lighter resolve against at the
    // first cascade layer). mermaid's theme does not expose a font-weight variable,
    // so it is the browser default 400 (Normal).
    style.fontWeight = QFont::Normal;
    // Commit 4 colorIndex palette (empty for the 9 standard themes; populated by
    // redux-color / redux-dark-color). The scene cycles node colors by insertion
    // order only when borderColorArray is non-empty.
    style.borderColorArray = themeVars.borderColorArray;
    style.bkgColorArray = themeVars.bkgColorArray;
    // NOTE: upstream ignores user-supplied borderColorArray/bkgColorArray via the
    // %%{init}%% SOURCE entry (only the external mermaid.initialize() API honors
    // them — verified G:/github/req-probe/step4-source-entry-report.json). The
    // built-in redux-color/redux-dark-color palette above is the only source-path
    // palette; custom arrays are not a %%{init}%% parity feature.
    // genColor emits palette rules for color-0..color-(THEME_COLOR_LIMIT-1). The
    // limit defaults to 12 and is user-configurable via themeVariables
    // .THEME_COLOR_LIMIT (top-level init.THEME_COLOR_LIMIT is upstream-ignored).
    // Read the RAW config value with JS Number()+ceil semantics (2.5->3, true->1,
    // false/"abc"->0, null/absent->keep default); fall back to the theme default.
    style.themeColorLimit = jsThemeColorLimit(pre.config).value_or(themeVars.themeColorLimit);
    // No inline `title` token in requirementDiagram grammar — pass empty so
    // renderMetadata falls back to the frontmatter title (pre.title).
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), diagram.data().accTitle,
        diagram.data().accDescription, style.titleColor, style.fontFamily, 18.0,
        configNumber(stateConfig, QStringLiteral("titleTopMargin"), 25.0), 8.0);
    requirement::RequirementScene scene =
        requirement::buildRequirementScene(input, measurements, placement, std::move(style));
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const requirement::RequirementScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& requirementDiagramAdapter() {
  static const RequirementDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
