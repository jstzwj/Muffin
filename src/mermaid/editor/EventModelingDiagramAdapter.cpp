#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/eventmodeling/EventModelingDiagram.h"
#include "mermaid/eventmodeling/EventModelingScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue eventModelingScalar(const QJsonObject& object, const char* key,
                               const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct EventModelingDiagramImpl final : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("eventmodeling")};
  }
  QString cssClass() const override {
    return QStringLiteral("eventmodeling");
  }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw =
        pre.config.value(QStringLiteral("eventmodeling")).toObject();
    eventmodeling::EventModelingConfig config;
    config.useMaxWidth =
        eventModelingScalar(raw, "useMaxWidth", QJsonValue(true));
    config.padding = eventModelingScalar(raw, "padding", QJsonValue(30.0));
    config.rowHeight =
        eventModelingScalar(raw, "rowHeight", QJsonValue(32.0));

    const eventmodeling::EventModelingData data =
        eventmodeling::EventModelingDiagram::parse(pre.code);

    eventmodeling::EventModelingSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(style.fontFamily, 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.uiFill = themeVars.emUiFill;
    style.uiStroke = themeVars.emUiStroke;
    style.processorFill = themeVars.emProcessorFill;
    style.processorStroke = themeVars.emProcessorStroke;
    style.readModelFill = themeVars.emReadModelFill;
    style.readModelStroke = themeVars.emReadModelStroke;
    style.commandFill = themeVars.emCommandFill;
    style.commandStroke = themeVars.emCommandStroke;
    style.eventFill = themeVars.emEventFill;
    style.eventStroke = themeVars.emEventStroke;
    style.swimlaneFill = themeVars.emSwimlaneBackgroundOdd;
    style.swimlaneStroke = themeVars.emSwimlaneBackgroundStroke;
    style.arrowhead = themeVars.emArrowhead;
    style.relationStroke = themeVars.emRelationStroke;

    eventmodeling::EventModelingScene scene =
        eventmodeling::buildEventModelingScene(
            data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), scene.style.textColor,
        scene.style.fontFamily, scene.style.rootFontSize);
    // The registered Event Modeling grammar does not expose common metadata.
    // Frontmatter and metadata-looking statements therefore never create a
    // visible title strip or accessible SVG title/description elements.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene = std::make_shared<const eventmodeling::EventModelingScene>(
        std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& eventModelingDiagramAdapter() {
  static const EventModelingDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
