#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/radar/RadarDiagram.h"
#include "mermaid/radar/RadarScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QSize>
#include <QString>

#include <cmath>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

QJsonValue scalarConfig(const QJsonObject& object, QLatin1String key,
                        const QJsonValue& fallback) {
  const QJsonValue value = object.value(key);
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

QString jsString(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  if (value.isDouble()) return jsNumberToString(value.toDouble());
  if (value.isBool())
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  if (value.isNull()) return QStringLiteral("null");
  return value.isArray() ? QString() : QStringLiteral("[object Object]");
}

// Radar's frame formulas use JavaScript `+`, not an all-numeric helper. A
// string config value therefore changes only the additions that touch it:
// width:"320" gives radius=160 and centerX=210, but totalWidth is the string
// concatenation "320"+50+50 -> 3205050. Preserve that observable quirk.
struct JsAddValue {
  bool string = false;
  QString text;
  double number = 0.0;
};

JsAddValue jsAddValue(const QJsonValue& value) {
  if (value.isString()) return {true, value.toString(), 0.0};
  return {false, QString(), jsNumberValue(value)};
}

QString jsAddString(const JsAddValue& value) {
  return value.string ? value.text : jsNumberToString(value.number);
}

JsAddValue jsAdd(const JsAddValue& left, const JsAddValue& right) {
  if (left.string || right.string)
    return {true, jsAddString(left) + jsAddString(right), 0.0};
  return {false, QString(), left.number + right.number};
}

qreal jsNumeric(const JsAddValue& value) {
  return qreal(value.string ? jsNumberValue(QJsonValue(value.text))
                            : value.number);
}

struct RadarDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("radar")}; }
  QString cssClass() const override { return QStringLiteral("radar"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    radar::RadarData data = radar::RadarDiagram::parse(pre.code);
    // populateCommonDb only applies a truthy final AST title. An empty source
    // title (including one that follows a non-empty title) therefore leaves
    // the frontmatter title in place.
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = pre.title;

    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject rawConfig =
        pre.config.value(QStringLiteral("radar")).toObject();
    const QJsonValue rawWidth =
        scalarConfig(rawConfig, QLatin1String("width"), QJsonValue(600.0));
    const QJsonValue rawHeight =
        scalarConfig(rawConfig, QLatin1String("height"), QJsonValue(600.0));
    const QJsonValue rawTop =
        scalarConfig(rawConfig, QLatin1String("marginTop"), QJsonValue(50.0));
    const QJsonValue rawRight =
        scalarConfig(rawConfig, QLatin1String("marginRight"), QJsonValue(50.0));
    const QJsonValue rawBottom =
        scalarConfig(rawConfig, QLatin1String("marginBottom"), QJsonValue(50.0));
    const QJsonValue rawLeft =
        scalarConfig(rawConfig, QLatin1String("marginLeft"), QJsonValue(50.0));

    radar::RadarConfig config;
    config.width = qreal(jsNumberValue(rawWidth));
    config.height = qreal(jsNumberValue(rawHeight));
    config.marginTop = qreal(jsNumberValue(rawTop));
    config.marginRight = qreal(jsNumberValue(rawRight));
    config.marginBottom = qreal(jsNumberValue(rawBottom));
    config.marginLeft = qreal(jsNumberValue(rawLeft));
    config.axisScaleFactor = qreal(jsNumberValue(scalarConfig(
        rawConfig, QLatin1String("axisScaleFactor"), QJsonValue(1.0))));
    config.axisLabelFactor = qreal(jsNumberValue(scalarConfig(
        rawConfig, QLatin1String("axisLabelFactor"), QJsonValue(1.05))));
    config.curveTension = qreal(jsNumberValue(scalarConfig(
        rawConfig, QLatin1String("curveTension"), QJsonValue(0.17))));
    const QJsonValue rawUseMaxWidth = scalarConfig(
        rawConfig, QLatin1String("useMaxWidth"), QJsonValue(true));
    config.useMaxWidth = truthyConfigValue(rawUseMaxWidth);

    const JsAddValue width = jsAddValue(rawWidth);
    const JsAddValue height = jsAddValue(rawHeight);
    const JsAddValue top = jsAddValue(rawTop);
    const JsAddValue right = jsAddValue(rawRight);
    const JsAddValue bottom = jsAddValue(rawBottom);
    const JsAddValue left = jsAddValue(rawLeft);
    const JsAddValue halfWidth{false, QString(), config.width / 2.0};
    const JsAddValue halfHeight{false, QString(), config.height / 2.0};
    config.totalWidth = jsNumeric(jsAdd(jsAdd(width, left), right));
    config.totalHeight = jsNumeric(jsAdd(jsAdd(height, top), bottom));
    config.centerX = jsNumeric(jsAdd(left, halfWidth));
    config.centerY = jsNumeric(jsAdd(top, halfHeight));
    config.legendX = jsNumeric(jsAdd(halfWidth, right)) * 3.0 / 4.0;
    config.legendY = -jsNumeric(jsAdd(halfHeight, top)) * 3.0 / 4.0;

    radar::RadarSceneStyle style;
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    style.textColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    const CssLengthContext rootContext =
        pieCssLengthContext(style.fontFamily, 16.0);
    const qreal rootFontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    const CssLengthContext lengthContext =
        pieCssLengthContext(style.fontFamily, rootFontSize);
    style.titleFontSize = rootFontSize;
    for (const QString& color : themeVars.cScale) style.palette.append(color);
    style.themeColorLimit =
        jsThemeColorLimit(pre.config).value_or(themeVars.themeColorLimit);

    // buildRadarStyleOptions clean-merges the nested themeVariables.radar over
    // the theme defaults. The similarly named config.radar style keys are kept
    // by source sanitization but are not passed to styles(), hence intentionally
    // remain inert here.
    const QJsonObject radarTheme =
        pre.config.value(QStringLiteral("themeVariables"))
            .toObject()
            .value(QStringLiteral("radar"))
            .toObject();
    const QJsonValue axisColor = scalarConfig(
        radarTheme, QLatin1String("axisColor"), QJsonValue(themeVars.lineColor));
    const QJsonValue axisStroke = scalarConfig(
        radarTheme, QLatin1String("axisStrokeWidth"), QJsonValue(2.0));
    const QJsonValue axisLabelSize = scalarConfig(
        radarTheme, QLatin1String("axisLabelFontSize"), QJsonValue(12.0));
    const QJsonValue curveOpacity = scalarConfig(
        radarTheme, QLatin1String("curveOpacity"), QJsonValue(0.5));
    const QJsonValue curveStroke = scalarConfig(
        radarTheme, QLatin1String("curveStrokeWidth"), QJsonValue(2.0));
    const QJsonValue graticuleColor = scalarConfig(
        radarTheme, QLatin1String("graticuleColor"),
        QJsonValue(QStringLiteral("#DEDEDE")));
    const QJsonValue graticuleStroke = scalarConfig(
        radarTheme, QLatin1String("graticuleStrokeWidth"), QJsonValue(1.0));
    const QJsonValue graticuleOpacity = scalarConfig(
        radarTheme, QLatin1String("graticuleOpacity"), QJsonValue(0.3));
    const QJsonValue legendSize = scalarConfig(
        radarTheme, QLatin1String("legendFontSize"), QJsonValue(12.0));

    const qreal diagonal =
        std::sqrt(*config.totalWidth * *config.totalWidth +
                  *config.totalHeight * *config.totalHeight) /
        std::sqrt(2.0);
    style.axisColor = jsString(axisColor);
    style.axisLabelColor = style.axisColor;
    style.axisStrokeWidth =
        cssStrokeWidthPx(jsString(axisStroke), lengthContext, diagonal);
    style.axisLabelFontSize =
        cssFontSizePx(jsString(axisLabelSize) + QStringLiteral("px"),
                      lengthContext);
    style.curveOpacity = cssOpacity(jsString(curveOpacity));
    style.curveStrokeWidth =
        cssStrokeWidthPx(jsString(curveStroke), lengthContext, diagonal);
    style.graticuleColor = jsString(graticuleColor);
    style.graticuleStrokeWidth =
        cssStrokeWidthPx(jsString(graticuleStroke), lengthContext, diagonal);
    style.graticuleOpacity = cssOpacity(jsString(graticuleOpacity));
    style.legendFontSize =
        cssFontSizePx(jsString(legendSize) + QStringLiteral("px"),
                      lengthContext);

    radar::RadarScene scene =
        radar::buildRadarScene(data, std::move(config), std::move(style));

    // Radar draws its title at the top of the fixed viewBox. Suppress the
    // shared external title band while retaining the accessibility metadata.
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        scene.style.textColor, scene.style.fontFamily,
        scene.style.titleFontSize, 25.0, 0.0);
    metadata.title = QString();
    metadata.svgUseMaxWidth = scene.config.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const radar::RadarScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& radarDiagramAdapter() {
  static const RadarDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
