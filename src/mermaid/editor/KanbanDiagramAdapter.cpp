#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/kanban/KanbanDiagram.h"
#include "mermaid/kanban/KanbanScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue nullishValue(const QJsonObject& object, const char* key,
                        const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() ? fallback : value;
}

QString ticketBaseUrl(const QJsonValue& raw) {
  if (raw.isUndefined() || raw.isNull() || !truthyConfigValue(raw))
    return QString();
  if (!raw.isString())
    throw std::runtime_error("config.ticketBaseUrl.replace is not a function");
  return raw.toString();
}

quint32 handDrawnSeed(const QJsonValue& raw) {
  const double value = jsNumberValue(raw);
  if (!std::isfinite(value) || value <= 0.0) return 0;
  constexpr double kMax = double(std::numeric_limits<quint32>::max());
  return static_cast<quint32>(std::min(value, kMax));
}

struct KanbanDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("kanban")}; }
  QString cssClass() const override { return QStringLiteral("kanban"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject kanbanConfig =
        pre.config.value(QStringLiteral("kanban")).toObject();
    const QJsonObject mindmapConfig =
        pre.config.value(QStringLiteral("mindmap")).toObject();
    const QJsonValue look = pre.config.value(QStringLiteral("look"));

    kanban::KanbanParseConfig parseConfig;
    parseConfig.mindmapPadding =
        nullishValue(mindmapConfig, "padding", QJsonValue(10.0));
    parseConfig.mindmapMaxNodeWidth =
        nullishValue(mindmapConfig, "maxNodeWidth", QJsonValue(200.0));
    parseConfig.look =
        look.isString() ? look.toString() : QStringLiteral("classic");
    const kanban::KanbanData data =
        kanban::KanbanDiagram::parse(pre.code, parseConfig);

    kanban::KanbanConfig config;
    config.sectionWidth =
        nullishValue(kanbanConfig, "sectionWidth", QJsonValue(200.0));
    // Mermaid 11.16.0's Kanban renderer accidentally reads both values from
    // mindmap, not from the same-named Kanban configuration fields.
    config.padding =
        nullishValue(mindmapConfig, "padding", QJsonValue(10.0));
    config.useMaxWidth =
        nullishValue(mindmapConfig, "useMaxWidth", QJsonValue(true));
    const QJsonValue htmlLabels =
        pre.config.value(QStringLiteral("htmlLabels"));
    config.htmlLabels = htmlLabels.isUndefined() || htmlLabels.isNull()
                            ? true
                            : truthyConfigValue(htmlLabels);
    const QJsonValue markdownAutoWrap =
        pre.config.value(QStringLiteral("markdownAutoWrap"));
    config.markdownAutoWrap =
        markdownAutoWrap.isUndefined() || markdownAutoWrap.isNull()
            ? true
            : truthyConfigValue(markdownAutoWrap);
    config.ticketBaseUrl = ticketBaseUrl(
        kanbanConfig.value(QStringLiteral("ticketBaseUrl")));
    config.handDrawnSeed = handDrawnSeed(
        nullishValue(pre.config, "handDrawnSeed", QJsonValue(0.0)));

    const QJsonObject rawThemeVariables =
        pre.config.value(QStringLiteral("themeVariables")).toObject();
    kanban::KanbanSceneStyle style;
    style.themeName = effectiveTheme.isEmpty() ? QStringLiteral("default")
                                                : effectiveTheme;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(firstFontFamily(style.fontFamily), 16.0);
    style.fontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.background = themeVars.background;
    style.nodeBorder = themeVars.nodeBorder;
    style.shadowColor = themeVars.shadowColor;
    style.shadowOpacity = themeVars.shadowOpacity;
    style.shadowOffsetX = themeVars.shadowOffsetX;
    style.shadowOffsetY = themeVars.shadowOffsetY;
    const QJsonValue rawDropShadow =
        rawThemeVariables.value(QStringLiteral("dropShadow"));
    // Through Mermaid's source-entry sanitizer, any explicit override has a
    // used value of `none`: literal none, invalid filters, and even a custom
    // drop-shadow() all disable the built-in neo filter. Only absent/null
    // preserves the theme default.
    style.dropShadowEnabled =
        rawDropShadow.isUndefined() || rawDropShadow.isNull();
    style.darkMode = truthyConfigValue(
        rawThemeVariables.value(QStringLiteral("darkMode")));
    const QJsonValue rawThemeColorLimit =
        rawThemeVariables.value(QStringLiteral("THEME_COLOR_LIMIT"));
    style.rawThemeColorLimit =
        rawThemeColorLimit.isUndefined() || rawThemeColorLimit.isNull()
            ? qreal(themeVars.themeColorLimit)
            : qreal(jsNumberValue(rawThemeColorLimit));
    style.themeColorRuleCount =
        jsThemeColorLimit(pre.config).value_or(themeVars.themeColorLimit);
    for (const QString& color : themeVars.cScale) style.cScale.append(color);
    for (const QString& color : themeVars.cScaleInv)
      style.cScaleInv.append(color);
    for (const QString& color : themeVars.cScaleLabel)
      style.cScaleLabel.append(color);

    kanban::KanbanScene scene = kanban::buildKanbanScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), scene.style.textColor,
        scene.style.fontFamily, scene.style.fontSize);
    // Kanban has no commonDb title or accessibility directives. Upstream
    // ignores a frontmatter title completely, so it must not create the shared
    // 40px title strip or an ARIA title/description relationship.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene =
        std::make_shared<const kanban::KanbanScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& kanbanDiagramAdapter() {
  static const KanbanDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
