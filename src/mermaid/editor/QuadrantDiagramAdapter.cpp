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
    // Default-theme values are the struct defaults (captured live from 11.16.0).
    // Dark theme overrides (captured via the style probe).
    if (effectiveTheme == QStringLiteral("dark")) {
      style.quadrant1Fill = QStringLiteral("#1f2020");
      style.quadrant2Fill = QStringLiteral("#242525");
      style.quadrant3Fill = QStringLiteral("#292a2a");
      style.quadrant4Fill = QStringLiteral("#2e2f2f");
      style.quadrant1TextFill = QStringLiteral("#e0dfdf");
      style.quadrant2TextFill = QStringLiteral("#dbdada");
      style.quadrant3TextFill = QStringLiteral("#d6d5d5");
      style.quadrant4TextFill = QStringLiteral("#d1d0d0");
      style.quadrantPointFill = QStringLiteral("hsl(180, 1.5873015873%, NaN%)");
      style.quadrantPointTextFill = QStringLiteral("#e0dfdf");
      style.quadrantXAxisTextFill = QStringLiteral("#e0dfdf");
      style.quadrantYAxisTextFill = QStringLiteral("#e0dfdf");
      style.quadrantInternalBorderStrokeFill = QStringLiteral("#CCCCCC");
      style.quadrantExternalBorderStrokeFill = QStringLiteral("#CCCCCC");
      style.quadrantTitleFill = QStringLiteral("#e0dfdf");
    }

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
