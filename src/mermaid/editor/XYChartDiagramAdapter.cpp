#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/xychart/XYChartDiagram.h"
#include "mermaid/xychart/XYChartScene.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QSize>

#include <cmath>
#include <memory>
#include <stdexcept>

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

qreal numberValue(const QJsonObject& object, const char* key, qreal fallback) {
  return qreal(jsNumberValue(scalar(object, key, QJsonValue(fallback))));
}

bool boolValue(const QJsonObject& object, const char* key, bool fallback) {
  return truthyConfigValue(scalar(object, key, QJsonValue(fallback)));
}

QString stringValue(const QJsonObject& object, const char* key,
                    const QString& fallback) {
  return scalar(object, key, QJsonValue(fallback)).toString();
}

xychart::XYChartAxisConfig axisConfig(const QJsonObject& object) {
  xychart::XYChartAxisConfig result;
  result.showLabel = boolValue(object, "showLabel", true);
  result.labelFontSize = numberValue(object, "labelFontSize", 14.0);
  result.labelPadding = numberValue(object, "labelPadding", 5.0);
  result.showTitle = boolValue(object, "showTitle", true);
  result.titleFontSize = numberValue(object, "titleFontSize", 16.0);
  result.titlePadding = numberValue(object, "titlePadding", 5.0);
  result.showTick = boolValue(object, "showTick", true);
  result.tickLength = numberValue(object, "tickLength", 5.0);
  result.tickWidth = numberValue(object, "tickWidth", 2.0);
  result.showAxisLine = boolValue(object, "showAxisLine", true);
  result.axisLineWidth = numberValue(object, "axisLineWidth", 2.0);
  result.labelRotation = numberValue(object, "labelRotation", 0.0);
  return result;
}

struct XYChartDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("xychart")}; }
  QString cssClass() const override { return QStringLiteral("xychart"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    xychart::XYChartData data = xychart::XYChartDiagram::parse(pre.code);
    if (!data.hasTitleDirective && !pre.title.isEmpty()) data.title = pre.title;
    if (data.plots.isEmpty())
      throw std::runtime_error(
          "Cannot read properties of undefined (reading 'data')");

    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw = pre.config.value(QStringLiteral("xyChart")).toObject();
    xychart::XYChartConfig config;
    config.width = numberValue(raw, "width", 700.0);
    config.height = numberValue(raw, "height", 500.0);
    config.titleFontSize = numberValue(raw, "titleFontSize", 20.0);
    config.titlePadding = numberValue(raw, "titlePadding", 10.0);
    config.showDataLabel = boolValue(raw, "showDataLabel", false);
    config.showDataLabelOutsideBar =
        boolValue(raw, "showDataLabelOutsideBar", false);
    config.showTitle = boolValue(raw, "showTitle", true);
    config.plotReservedSpacePercent =
        numberValue(raw, "plotReservedSpacePercent", 50.0);
    config.xAxis = axisConfig(raw.value(QStringLiteral("xAxis")).toObject());
    config.yAxis = axisConfig(raw.value(QStringLiteral("yAxis")).toObject());
    const QString configuredOrientation =
        stringValue(raw, "chartOrientation", QStringLiteral("vertical"));
    config.orientation = configuredOrientation == QLatin1String("horizontal")
                             ? xychart::XYChartOrientation::Horizontal
                             : xychart::XYChartOrientation::Vertical;
    if (data.hasOrientationDirective) config.orientation = data.orientation;

    const flowtheme::XYChartThemeVariables& xy = themeVars.xyChart;
    xychart::XYChartSceneStyle style;
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    const CssLengthContext rootContext =
        pieCssLengthContext(style.fontFamily, 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.backgroundColor = xy.backgroundColor;
    style.titleColor = xy.titleColor;
    style.dataLabelColor = xy.dataLabelColor;
    style.xAxisTitleColor = xy.xAxisTitleColor;
    style.xAxisLabelColor = xy.xAxisLabelColor;
    style.xAxisTickColor = xy.xAxisTickColor;
    style.xAxisLineColor = xy.xAxisLineColor;
    style.yAxisTitleColor = xy.yAxisTitleColor;
    style.yAxisLabelColor = xy.yAxisLabelColor;
    style.yAxisTickColor = xy.yAxisTickColor;
    style.yAxisLineColor = xy.yAxisLineColor;
    style.plotColorPalette =
        xy.plotColorPalette.split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (QString& value : style.plotColorPalette) value = value.trimmed();

    xychart::XYChartScene scene =
        xychart::buildXYChartScene(data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        themeVars.textColor, scene.style.fontFamily,
        scene.config.titleFontSize, 25.0, 0.0);
    metadata.title.clear();
    // xychartRenderer hard-codes configureSvgSize(..., true); the config's
    // useMaxWidth field is intentionally inert in Mermaid 11.16.0.
    metadata.svgUseMaxWidth = true;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene =
        std::make_shared<const xychart::XYChartScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& xyChartDiagramAdapter() {
  static const XYChartDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
