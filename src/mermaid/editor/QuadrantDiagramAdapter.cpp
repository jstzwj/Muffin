#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/quadrant/QuadrantDiagram.h"
#include "mermaid/quadrant/QuadrantScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>
#include <QString>

#include <memory>

namespace muffin::mermaid::editor {
namespace {

struct QuadrantDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("quadrantChart")}; }
  QString cssClass() const override { return QStringLiteral("quadrantChart"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    quadrant::QuadrantData data = quadrant::QuadrantDiagram::parse(pre.code);
    // Effective title: the in-source `quadrantChart title` wins; otherwise the
    // frontmatter title is the diagram title. Resolved BEFORE buildQuadrantScene
    // so titleSpace is reserved and the in-scene title is placed correctly.
    if (data.title.isEmpty() && !pre.title.isEmpty()) data.title = pre.title;
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    quadrant::QuadrantSceneStyle style;
    // Consume the fully-resolved quadrant themeVariables (FlowTheme derived every
    // fill / text fill / point fill / axis+title text / border per the active
    // theme, including the upstream-invalid quadrantPointFill "hsl(..., NaN%)"
    // string emitted verbatim). No default/dark special-casing remains; the
    // struct defaults below are dead safety fallbacks.
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    style.quadrant1Fill = themeVars.quadrant[0];
    style.quadrant2Fill = themeVars.quadrant[1];
    style.quadrant3Fill = themeVars.quadrant[2];
    style.quadrant4Fill = themeVars.quadrant[3];
    style.quadrant1TextFill = themeVars.quadrantText[0];
    style.quadrant2TextFill = themeVars.quadrantText[1];
    style.quadrant3TextFill = themeVars.quadrantText[2];
    style.quadrant4TextFill = themeVars.quadrantText[3];
    style.quadrantPointFill = themeVars.quadrantPointFill;
    style.quadrantPointTextFill = themeVars.quadrantPointTextFill;
    style.quadrantXAxisTextFill = themeVars.quadrantXAxisTextFill;
    style.quadrantYAxisTextFill = themeVars.quadrantYAxisTextFill;
    style.quadrantInternalBorderStrokeFill = themeVars.quadrantInternalBorderStrokeFill;
    style.quadrantExternalBorderStrokeFill = themeVars.quadrantExternalBorderStrokeFill;
    style.quadrantTitleFill = themeVars.quadrantTitleFill;

    const QJsonObject qcfg = pre.config.value(QStringLiteral("quadrantChart")).toObject();
    quadrant::QuadrantScene scene = quadrant::buildQuadrantScene(data, qcfg, std::move(style));

    // The quadrant title is drawn INSIDE the viewBox (mermaid places it at
    // y=titlePadding). Clear metadata.title so the shared image path does not add
    // a second title band (renderMetadata would otherwise pull in pre.title).
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, QString(), data.accTitle, data.accDescr,
                       scene.style.quadrantTitleFill, scene.style.fontFamily, 20.0, 10.0, 0.0);
    metadata.title = QString();
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const quadrant::QuadrantScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& quadrantDiagramAdapter() {
  static const QuadrantDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
