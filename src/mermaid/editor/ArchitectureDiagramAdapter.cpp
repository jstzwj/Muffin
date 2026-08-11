#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/architecture/ArchitectureDiagram.h"
#include "mermaid/architecture/ArchitectureScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
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

struct ArchitectureDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("architecture")}; }
  QString cssClass() const override { return QStringLiteral("architecture"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject family =
        pre.config.value(QStringLiteral("architecture")).toObject();

    architecture::ArchitectureConfig config;
    config.useMaxWidth = scalar(family, "useMaxWidth", QJsonValue(true));
    config.padding = scalar(family, "padding", QJsonValue(40.0));
    config.iconSize = scalar(family, "iconSize", QJsonValue(80.0));
    config.fontSize = scalar(family, "fontSize", QJsonValue(16.0));
    config.randomize = scalar(family, "randomize", QJsonValue(false));
    config.nodeSeparation =
        scalar(family, "nodeSeparation", QJsonValue(75.0));
    config.idealEdgeLengthMultiplier =
        scalar(family, "idealEdgeLengthMultiplier", QJsonValue(1.5));
    config.edgeElasticity =
        scalar(family, "edgeElasticity", QJsonValue(0.45));
    config.numIter = scalar(family, "numIter", QJsonValue(2500.0));
    config.seed = scalar(family, "seed", QJsonValue(1.0));

    architecture::ArchitectureSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    style.textColor = themeVars.textColor;
    style.edgeColor = themeVars.archEdgeColor;
    style.arrowColor = themeVars.archEdgeArrowColor;
    style.edgeWidth = themeVars.archEdgeWidth;
    style.groupBorderColor = themeVars.archGroupBorderColor;
    style.groupBorderWidth = themeVars.archGroupBorderWidth;

    architecture::ArchitectureData data =
        architecture::ArchitectureDiagram::parse(pre.code);
    architecture::ArchitectureScene scene = architecture::buildArchitectureScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.textColor, scene.style.fontFamily,
        qreal(jsNumberValue(scene.config.fontSize)));
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene =
        std::make_shared<const architecture::ArchitectureScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& architectureDiagramAdapter() {
  static const ArchitectureDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
