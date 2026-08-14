#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"
#include "mermaid/xychart/XYChartDiagram.h"
#include "mermaid/xychart/XYChartScene.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QSize>

#include <algorithm>
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

    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      QVector<ElementInput> elements;
      ElementStyle root;
      root.fill = themeVars.textColor;
      root.stroke = QStringLiteral("none");
      root.strokeWidth = QStringLiteral("1px");
      root.color = QStringLiteral("black");
      root.fontFamily = scene.style.fontFamily;
      root.fontSize = QString::number(scene.style.rootFontSize) +
                      QStringLiteral("px");
      root.fontWeight = QStringLiteral("400");
      elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                       QStringLiteral("diagram-root"), {}, {}, root, {}});
      elements.append({QStringLiteral("main"), QStringLiteral("svg"),
                       QStringLiteral("g"), {}, {QStringLiteral("main")},
                       {}, root, {}});
      ElementStyle background = root;
      background.fill = scene.style.backgroundColor;
      elements.append({QStringLiteral("background"), QStringLiteral("main"),
                       QStringLiteral("rect"), {},
                       {QStringLiteral("background")},
                       {{QStringLiteral("width"), QString::number(scene.bounds.width())},
                        {QStringLiteral("height"), QString::number(scene.bounds.height())},
                        {QStringLiteral("fill"), background.fill}},
                       background, {}});

      QHash<QString, QString> groupKeys;
      const auto domGroupPath = [&](QString path) {
        const bool horizontal =
            scene.config.orientation == xychart::XYChartOrientation::Horizontal;
        if (path == QLatin1String("x-axis") ||
            path.startsWith(QLatin1String("x-axis/")))
          path.replace(0, 6, horizontal ? QStringLiteral("left-axis")
                                        : QStringLiteral("bottom-axis"));
        else if (path == QLatin1String("y-axis") ||
                 path.startsWith(QLatin1String("y-axis/")))
          path.replace(0, 6, horizontal ? QStringLiteral("top-axis")
                                        : QStringLiteral("left-axis"));
        return path;
      };
      const auto ensureGroup = [&](const QString& rawPath) {
        QString parent = QStringLiteral("main");
        QString prefix;
        for (const QString& segment : domGroupPath(rawPath).split(
                 QLatin1Char('/'), Qt::SkipEmptyParts)) {
          prefix += segment;
          const auto existing = groupKeys.constFind(prefix);
          if (existing != groupKeys.cend()) {
            parent = existing.value();
            continue;
          }
          const QString key = QStringLiteral("group-%1").arg(elements.size());
          elements.append({key, parent, QStringLiteral("g"), {}, {segment},
                           {}, root, {}});
          groupKeys.insert(prefix, key);
          parent = key;
        }
        return parent;
      };

      struct Drawable {
        int order = -1;
        char type = 0;
        qsizetype index = 0;
      };
      QVector<Drawable> drawables;
      for (qsizetype i = 0; i < scene.rects.size(); ++i)
        drawables.append({scene.rects.at(i).paintOrder, 'r', i});
      for (qsizetype i = 0; i < scene.paths.size(); ++i)
        drawables.append({scene.paths.at(i).paintOrder, 'p', i});
      for (qsizetype i = 0; i < scene.texts.size(); ++i)
        drawables.append({scene.texts.at(i).paintOrder, 't', i});
      std::stable_sort(drawables.begin(), drawables.end(),
                       [](const Drawable& a, const Drawable& b) {
                         return a.order < b.order;
                       });
      QVector<QString> rectKeys(scene.rects.size());
      QVector<QString> pathKeys(scene.paths.size());
      QVector<QString> textKeys(scene.texts.size());
      for (const Drawable& drawable : drawables) {
        if (drawable.type == 'r') {
          const auto& rect = scene.rects.at(drawable.index);
          ElementStyle value = root;
          value.fill = rect.fill;
          value.stroke = rect.stroke;
          value.strokeWidth = QString::number(rect.strokeWidth) +
                              QStringLiteral("px");
          const QString key = QStringLiteral("rect-%1").arg(drawable.index);
          rectKeys[drawable.index] = key;
          elements.append({key, ensureGroup(rect.group), QStringLiteral("rect"),
                           {}, {},
                           {{QStringLiteral("x"), QString::number(rect.rect.x())},
                            {QStringLiteral("y"), QString::number(rect.rect.y())},
                            {QStringLiteral("width"), QString::number(rect.rect.width())},
                            {QStringLiteral("height"), QString::number(rect.rect.height())},
                            {QStringLiteral("fill"), rect.fill},
                            {QStringLiteral("stroke"), rect.stroke},
                            {QStringLiteral("stroke-width"),
                             QString::number(rect.strokeWidth)}},
                           value, {}});
        } else if (drawable.type == 'p') {
          const auto& path = scene.paths.at(drawable.index);
          ElementStyle value = root;
          value.fill = path.fill;
          value.stroke = path.stroke;
          value.strokeWidth = QString::number(path.strokeWidth) +
                              QStringLiteral("px");
          const QString key = QStringLiteral("path-%1").arg(drawable.index);
          pathKeys[drawable.index] = key;
          elements.append({key, ensureGroup(path.group), QStringLiteral("path"),
                           {}, {},
                           {{QStringLiteral("d"), path.path},
                            {QStringLiteral("fill"), path.fill},
                            {QStringLiteral("stroke"), path.stroke},
                            {QStringLiteral("stroke-width"),
                             QString::number(path.strokeWidth)}},
                           value, {}});
        } else {
          const auto& text = scene.texts.at(drawable.index);
          ElementStyle value = root;
          value.fill = text.fill;
          value.fontSize = QString::number(text.fontSize) + QStringLiteral("px");
          const QString key = QStringLiteral("text-%1").arg(drawable.index);
          textKeys[drawable.index] = key;
          elements.append({key, ensureGroup(text.group), QStringLiteral("text"),
                           {}, {},
                           {{QStringLiteral("fill"), text.fill},
                            {QStringLiteral("font-size"),
                             QString::number(text.fontSize) + QStringLiteral("px")}},
                           value, {}});
        }
      }

      const auto css = csscascade::resolveElements(themeCss, elements);
      const CssLengthContext cssContext =
          pieCssLengthContext(scene.style.fontFamily,
                              scene.style.rootFontSize);
      const qreal diagonal =
          std::hypot(scene.bounds.width(), scene.bounds.height()) /
          std::sqrt(2.0);
      const auto backgroundCss = css.value(QStringLiteral("background"));
      scene.style.backgroundColor = backgroundCss.fill;
      scene.style.backgroundOpacity = backgroundCss.effectiveFillOpacity;
      scene.style.backgroundVisible = backgroundCss.displayed();
      for (qsizetype i = 0; i < scene.rects.size(); ++i) {
        auto& rect = scene.rects[i];
        const auto value = css.value(rectKeys.at(i));
        rect.fill = value.fill;
        rect.stroke = value.stroke;
        rect.strokeWidth = cssStrokeWidthPx(value.strokeWidth, cssContext,
                                            diagonal);
        rect.fillOpacity = value.effectiveFillOpacity;
        rect.strokeOpacity = value.effectiveStrokeOpacity;
        rect.visible = value.displayed();
      }
      for (qsizetype i = 0; i < scene.paths.size(); ++i) {
        auto& path = scene.paths[i];
        const auto value = css.value(pathKeys.at(i));
        path.fill = value.fill;
        path.stroke = value.stroke;
        path.strokeWidth = cssStrokeWidthPx(value.strokeWidth, cssContext,
                                            diagonal);
        path.fillOpacity = value.effectiveFillOpacity;
        path.strokeOpacity = value.effectiveStrokeOpacity;
        path.visible = value.displayed();
      }
      for (qsizetype i = 0; i < scene.texts.size(); ++i) {
        auto& text = scene.texts[i];
        const auto value = css.value(textKeys.at(i));
        text.fill = value.fill;
        text.fontFamily = value.fontFamily;
        text.fontSize = cssFontSizePx(value.fontSize, cssContext);
        text.fontWeight = cssFontWeightToQt(QJsonValue(value.fontWeight),
                                            QFont::Normal);
        text.opacity = value.effectiveFillOpacity;
        text.visible = value.displayed();
      }
    }
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
