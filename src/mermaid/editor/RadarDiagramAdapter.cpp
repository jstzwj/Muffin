#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/radar/RadarDiagram.h"
#include "mermaid/radar/RadarScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

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

    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      QVector<ElementInput> elements;
      ElementStyle root;
      root.fill = scene.style.textColor;
      root.stroke = QStringLiteral("none");
      root.strokeWidth = QStringLiteral("1px");
      root.color = QStringLiteral("black");
      root.fontFamily = scene.style.fontFamily;
      root.fontSize = QString::number(rootFontSize) + QStringLiteral("px");
      root.fontWeight = QStringLiteral("400");
      elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                       QStringLiteral("diagram-root"), {QStringLiteral("radar")},
                       {}, root, {}});
      elements.append({QStringLiteral("frame"), QStringLiteral("svg"),
                       QStringLiteral("g"), {}, {}, {}, root, {}});
      for (qsizetype i = 0; i < scene.graticules.size(); ++i) {
        const auto& ring = scene.graticules.at(i);
        ElementStyle value = root;
        value.fill = ring.fill;
        value.stroke = ring.stroke;
        value.strokeWidth = QString::number(ring.strokeWidth) + QStringLiteral("px");
        value.fillOpacity = QString::number(ring.fillOpacity);
        elements.append({QStringLiteral("graticule-%1").arg(i),
                         QStringLiteral("frame"),
                         ring.circle ? QStringLiteral("circle")
                                     : QStringLiteral("polygon"),
                         {}, {QStringLiteral("radarGraticule")}, {}, value, {}});
      }
      for (qsizetype i = 0; i < scene.axes.size(); ++i) {
        const auto& axis = scene.axes.at(i);
        ElementStyle line = root;
        line.stroke = axis.lineStroke;
        line.strokeWidth = QString::number(axis.lineStrokeWidth) + QStringLiteral("px");
        elements.append({QStringLiteral("axis-line-%1").arg(i),
                         QStringLiteral("frame"), QStringLiteral("line"), {},
                         {QStringLiteral("radarAxisLine")}, {}, line, {}});
        ElementStyle label = root;
        label.color = axis.labelColor;
        label.fontSize = QString::number(axis.labelFontSize) + QStringLiteral("px");
        elements.append({QStringLiteral("axis-label-%1").arg(i),
                         QStringLiteral("frame"), QStringLiteral("text"), {},
                         {QStringLiteral("radarAxisLabel")}, {}, label, {}});
      }
      for (qsizetype i = 0; i < scene.curves.size(); ++i) {
        const auto& curve = scene.curves.at(i);
        ElementStyle value = root;
        value.fill = curve.fill;
        value.stroke = curve.stroke;
        value.strokeWidth = QString::number(curve.strokeWidth) + QStringLiteral("px");
        value.color = curve.elementColor;
        value.fillOpacity = QString::number(curve.fillOpacity);
        value.strokeOpacity = QString::number(curve.strokeOpacity);
        elements.append({QStringLiteral("curve-%1").arg(i),
                         QStringLiteral("frame"),
                         curve.polygon ? QStringLiteral("polygon")
                                       : QStringLiteral("path"),
                         {}, {QStringLiteral("radarCurve-%1").arg(curve.colorIndex)},
                         {}, value, {}});
      }
      for (qsizetype i = 0; i < scene.legends.size(); ++i) {
        const auto& legend = scene.legends.at(i);
        const QString group = QStringLiteral("legend-%1").arg(i);
        elements.append({group, QStringLiteral("frame"), QStringLiteral("g"),
                         {}, {}, {}, root, {}});
        ElementStyle box = root;
        box.fill = legend.boxFill;
        box.stroke = legend.boxStroke;
        box.strokeWidth = QString::number(legend.boxStrokeWidth) + QStringLiteral("px");
        box.color = legend.boxColor;
        box.fillOpacity = QString::number(legend.boxFillOpacity);
        box.strokeOpacity = QString::number(legend.boxStrokeOpacity);
        elements.append({group + QStringLiteral("-box"), group,
                         QStringLiteral("rect"), {},
                         {QStringLiteral("radarLegendBox-%1").arg(legend.colorIndex)},
                         {}, box, {}});
        ElementStyle text = root;
        text.fill = legend.textFill;
        text.color = legend.textColor;
        text.fontSize = QString::number(legend.textFontSize) + QStringLiteral("px");
        elements.append({group + QStringLiteral("-text"), group,
                         QStringLiteral("text"), {},
                         {QStringLiteral("radarLegendText")}, {}, text, {}});
      }
      ElementStyle title = root;
      title.color = scene.titleColor;
      title.fontSize = QString::number(scene.titleFontSize) + QStringLiteral("px");
      elements.append({QStringLiteral("title"), QStringLiteral("frame"),
                       QStringLiteral("text"), {},
                       {QStringLiteral("radarTitle")}, {}, title, {}});

      const auto css = csscascade::resolveElements(themeCss, elements);
      const CssLengthContext cssContext = pieCssLengthContext(
          firstFontFamily(scene.style.fontFamily), rootFontSize);
      const qreal cssDiagonal =
          std::hypot(scene.bounds.width(), scene.bounds.height()) / std::sqrt(2.0);
      const auto font = [&](const ElementStyle& value, QString& family,
                            qreal& size, QFont::Weight& weight) {
        family = value.fontFamily;
        size = cssFontSizePx(value.fontSize, cssContext);
        weight = cssFontWeightToQt(QJsonValue(value.fontWeight), QFont::Normal);
      };
      for (qsizetype i = 0; i < scene.graticules.size(); ++i) {
        auto& ring = scene.graticules[i];
        const auto value = css.value(QStringLiteral("graticule-%1").arg(i));
        ring.fill = value.fill;
        ring.stroke = value.stroke;
        ring.color = value.color;
        ring.strokeWidth = cssStrokeWidthPx(value.strokeWidth, cssContext, cssDiagonal);
        ring.fillOpacity = value.effectiveFillOpacity;
        ring.strokeOpacity = value.effectiveStrokeOpacity;
        ring.visible = value.displayed();
      }
      for (qsizetype i = 0; i < scene.axes.size(); ++i) {
        auto& axis = scene.axes[i];
        const auto line = css.value(QStringLiteral("axis-line-%1").arg(i));
        axis.lineStroke = line.stroke;
        axis.lineColor = line.color;
        axis.lineStrokeWidth = cssStrokeWidthPx(line.strokeWidth, cssContext, cssDiagonal);
        axis.lineOpacity = line.effectiveStrokeOpacity;
        axis.lineVisible = line.displayed();
        const auto label = css.value(QStringLiteral("axis-label-%1").arg(i));
        axis.labelFill = label.fill;
        axis.labelColor = label.color;
        font(label, axis.labelFontFamily, axis.labelFontSize, axis.labelFontWeight);
        axis.labelOpacity = label.effectiveOpacity;
        axis.labelVisible = label.displayed();
      }
      for (qsizetype i = 0; i < scene.curves.size(); ++i) {
        auto& curve = scene.curves[i];
        const auto value = css.value(QStringLiteral("curve-%1").arg(i));
        curve.fill = value.fill;
        curve.stroke = value.stroke;
        curve.elementColor = value.color;
        curve.strokeWidth = cssStrokeWidthPx(value.strokeWidth, cssContext, cssDiagonal);
        curve.fillOpacity = value.effectiveFillOpacity;
        curve.strokeOpacity = value.effectiveStrokeOpacity;
        curve.visible = value.displayed();
      }
      for (qsizetype i = 0; i < scene.legends.size(); ++i) {
        auto& legend = scene.legends[i];
        const QString group = QStringLiteral("legend-%1").arg(i);
        const auto box = css.value(group + QStringLiteral("-box"));
        legend.boxFill = box.fill;
        legend.boxStroke = box.stroke;
        legend.boxColor = box.color;
        legend.boxStrokeWidth = cssStrokeWidthPx(box.strokeWidth, cssContext, cssDiagonal);
        legend.boxFillOpacity = box.effectiveFillOpacity;
        legend.boxStrokeOpacity = box.effectiveStrokeOpacity;
        legend.boxVisible = box.displayed();
        const auto text = css.value(group + QStringLiteral("-text"));
        legend.textFill = text.fill;
        legend.textColor = text.color;
        font(text, legend.textFontFamily, legend.textFontSize,
             legend.textFontWeight);
        legend.textOpacity = text.effectiveOpacity;
        legend.textVisible = text.displayed();
      }
      const auto titleValue = css.value(QStringLiteral("title"));
      scene.titleFill = titleValue.fill;
      scene.titleColor = titleValue.color;
      font(titleValue, scene.titleFontFamily, scene.titleFontSize,
           scene.titleFontWeight);
      scene.titleOpacity = titleValue.effectiveOpacity;
      scene.titleVisible = titleValue.displayed();
    }

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
