#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/gantt/GanttDiagram.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <memory>

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

qreal number(const QJsonObject& object, const char* key, qreal fallback) {
  return qreal(jsNumberValue(scalar(object, key, QJsonValue(fallback))));
}

bool boolean(const QJsonObject& object, const char* key, bool fallback) {
  return truthyConfigValue(scalar(object, key, QJsonValue(fallback)));
}

QString string(const QJsonObject& object, const char* key,
               const QString& fallback = {}) {
  const QJsonValue value = scalar(object, key, QJsonValue(fallback));
  return value.isString() ? value.toString() : fallback;
}

struct GanttDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("gantt")}; }
  QString cssClass() const override { return QStringLiteral("gantt"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    gantt::GanttData data = gantt::GanttDiagram::parse(pre.code);
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);

    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme
                                                              : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject raw = pre.config.value(QStringLiteral("gantt")).toObject();

    gantt::GanttConfig config;
    config.useMaxWidth = boolean(raw, "useMaxWidth", true);
    config.useWidth = number(raw, "useWidth", 1200.0);
    config.titleTopMargin = number(raw, "titleTopMargin", 25.0);
    config.barHeight = number(raw, "barHeight", 20.0);
    config.barGap = number(raw, "barGap", 4.0);
    config.topPadding = number(raw, "topPadding", 50.0);
    config.rightPadding = number(raw, "rightPadding", 75.0);
    config.leftPadding = number(raw, "leftPadding", 75.0);
    config.gridLineStartPadding = number(raw, "gridLineStartPadding", 35.0);
    config.fontSize = number(raw, "fontSize", 11.0);
    config.sectionFontSize = number(raw, "sectionFontSize", 11.0);
    config.numberSectionStyles = std::max(1, int(number(raw, "numberSectionStyles", 4.0)));
    config.axisFormat = string(raw, "axisFormat", QStringLiteral("%Y-%m-%d"));
    config.tickInterval = string(raw, "tickInterval");
    config.topAxis = boolean(raw, "topAxis", false);
    config.displayMode = string(raw, "displayMode");
    config.weekday = string(raw, "weekday", QStringLiteral("sunday"));

    gantt::GanttSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(firstFontFamily(style.fontFamily), 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    style.sectionBkgColor = themeVars.sectionBkgColor;
    style.altSectionBkgColor = themeVars.altSectionBkgColor;
    style.sectionBkgColor2 = themeVars.sectionBkgColor2;
    style.excludeBkgColor = themeVars.excludeBkgColor;
    style.taskBorderColor = themeVars.taskBorderColor;
    style.taskBkgColor = themeVars.taskBkgColor;
    style.taskTextColor = themeVars.taskTextColor;
    style.taskTextDarkColor = themeVars.taskTextDarkColor;
    style.taskTextOutsideColor = themeVars.taskTextOutsideColor;
    style.taskTextClickableColor = themeVars.taskTextClickableColor;
    style.activeTaskBorderColor = themeVars.activeTaskBorderColor;
    style.activeTaskBkgColor = themeVars.activeTaskBkgColor;
    style.gridColor = themeVars.gridColor;
    style.doneTaskBkgColor = themeVars.doneTaskBkgColor;
    style.doneTaskBorderColor = themeVars.doneTaskBorderColor;
    style.critBorderColor = themeVars.critBorderColor;
    style.critBkgColor = themeVars.critBkgColor;
    style.todayLineColor = themeVars.todayLineColor;
    style.vertLineColor = themeVars.vertLineColor;

    gantt::GanttScene scene = gantt::buildGanttScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        themeVars.titleColor, scene.style.fontFamily, scene.style.rootFontSize);
    // Gantt owns its visible diagram title; the shared metadata title is only
    // used for SVG accessibility and must not add another 40px title strip.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.config.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(std::max(1, qRound(scene.bounds.width())),
                              std::max(1, qRound(scene.bounds.height())));
    entry.scene = std::make_shared<const gantt::GanttScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& ganttDiagramAdapter() {
  static const GanttDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
