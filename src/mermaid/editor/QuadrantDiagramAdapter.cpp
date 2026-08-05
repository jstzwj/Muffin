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
    const quadrant::QuadrantData data = quadrant::QuadrantDiagram::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    quadrant::QuadrantSceneStyle style;
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    // Default-theme quadrant fills mirror the frozen geometry oracle.
    // (Dark-theme fills differ; the pixel golden is the visual-parity target.)

    const QJsonObject qcfg = pre.config.value(QStringLiteral("quadrantChart")).toObject();
    quadrant::QuadrantScene scene = quadrant::buildQuadrantScene(data, qcfg, std::move(style));

    // The quadrant title is drawn INSIDE the viewBox (mermaid places it at
    // y=titlePadding), so do not reserve title space in the image path.
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, QString(), data.accTitle, data.accDescr,
                       scene.style.quadrantTitleFill, scene.style.fontFamily, 20.0, 10.0, 0.0);
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
