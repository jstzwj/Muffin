#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/timeline/TimelineDiagram.h"
#include "mermaid/timeline/TimelineScene.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSize>

#include <cmath>
#include <memory>
#include <stdexcept>
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

qreal numberValue(const QJsonObject& object, const char* key, qreal fallback) {
  return qreal(jsNumberValue(scalar(object, key, QJsonValue(fallback))));
}

bool boolValue(const QJsonObject& object, const char* key, bool fallback) {
  return truthyConfigValue(scalar(object, key, QJsonValue(fallback)));
}

qreal timelineLayoutFontSize(const QJsonValue& raw) {
  if (!raw.isString()) return qreal(jsNumberValue(raw));
  QString value = raw.toString();
  const qsizetype px = value.indexOf(QStringLiteral("px"));
  if (px >= 0) value.remove(px, 2);  // JS String.replace removes the first match.
  return qreal(jsNumberValue(QJsonValue(value)));
}

QString jsArrayString(const QJsonArray& values);

QString jsString(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  if (value.isDouble()) return jsNumberToString(value.toDouble());
  if (value.isBool())
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  if (value.isNull()) return QStringLiteral("null");
  if (value.isArray()) return jsArrayString(value.toArray());
  if (value.isObject()) return QStringLiteral("[object Object]");
  return QStringLiteral("undefined");
}

QString jsArrayString(const QJsonArray& values) {
  QStringList elements;
  elements.reserve(values.size());
  for (const QJsonValue& value : values)
    elements.append(value.isNull() || value.isUndefined()
                        ? QString()
                        : jsString(value));
  return elements.join(QLatin1Char(','));
}

bool stringArray(const QJsonValue& value) {
  if (value.isString()) return true;
  if (!value.isArray()) return false;
  const QJsonArray array = value.toArray();
  if (array.isEmpty()) return false;
  for (const QJsonValue& element : array)
    if (!stringArray(element)) return false;
  return true;
}

bool validCssFontFamily(const QString& expression) {
  const QString value = expression.trimmed();
  if (value.isEmpty()) return false;
  const QString lower = value.toLower();
  if (lower == QLatin1String("inherit") || lower == QLatin1String("initial") ||
      lower == QLatin1String("unset") || lower == QLatin1String("revert") ||
      lower == QLatin1String("revert-layer"))
    return false;
  static const QRegularExpression identifierList(QStringLiteral(
      R"(^\s*(?:(?:"[^"]+"|'[^']+'|-?[_a-zA-Z][-_a-zA-Z0-9]*(?:\s+-?[_a-zA-Z][-_a-zA-Z0-9]*)*)\s*)(?:,\s*(?:(?:"[^"]+"|'[^']+'|-?[_a-zA-Z][-_a-zA-Z0-9]*(?:\s+-?[_a-zA-Z][-_a-zA-Z0-9]*)*)\s*))*$)"));
  return identifierList.match(value).hasMatch();
}

QString timelineFontFamily(const QJsonValue& raw,
                           const QString& themeDefault) {
  if (raw.isUndefined() || raw.isNull()) return themeDefault;
  if (raw.isDouble() || raw.isBool())
    throw std::runtime_error("str is not iterable");
  const QString expression = raw.isArray() && stringArray(raw)
                                 ? jsArrayString(raw.toArray())
                                 : raw.isString() ? raw.toString() : QString();
  return validCssFontFamily(expression) ? expression
                                        : QStringLiteral("Times New Roman");
}

bool falsyReduxMainBkg(const QJsonValue& raw) {
  return raw.isUndefined() || raw.isNull() ||
         (raw.isBool() && !raw.toBool()) ||
         (raw.isDouble() && raw.toDouble() == 0.0) ||
         (raw.isString() && raw.toString().isEmpty());
}

void validateReduxMainBkg(const QString& theme, const QJsonValue& raw) {
  if (!theme.contains(QStringLiteral("redux"), Qt::CaseSensitive) ||
      falsyReduxMainBkg(raw))
    return;
  if (raw.isArray() || raw.isObject())
    throw std::runtime_error(
        "Cannot read properties of undefined (reading 'is')");
  if (raw.isBool())
    throw std::runtime_error("Cannot create property 'l' on boolean 'true'");
  if (raw.isDouble())
    throw std::runtime_error(
        (QStringLiteral("Cannot create property 'l' on number '") +
         jsNumberToString(raw.toDouble()) + QStringLiteral("'"))
            .toStdString());
  if (!raw.isString() || !color::isParsableColor(raw.toString()))
    throw std::runtime_error(
        (QStringLiteral("Unsupported color format: \"") + jsString(raw) +
         QStringLiteral("\""))
            .toStdString());
}

struct TimelineDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("timeline")}; }
  QString cssClass() const override { return QStringLiteral("timeline"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QJsonValue rawTheme = pre.config.value(QStringLiteral("theme"));
    if (rawTheme.isDouble() || rawTheme.isBool())
      throw std::runtime_error("theme?.includes is not a function");
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const QJsonObject rawThemeVariables =
        pre.config.value(QStringLiteral("themeVariables")).toObject();
    const QJsonValue rawMainBkg =
        rawThemeVariables.value(QStringLiteral("mainBkg"));
    validateReduxMainBkg(effectiveTheme, rawMainBkg);
    QJsonObject themeConfig = pre.config;
    if (effectiveTheme.contains(QStringLiteral("redux"), Qt::CaseSensitive) &&
        !rawMainBkg.isUndefined() && falsyReduxMainBkg(rawMainBkg)) {
      QJsonObject variables = rawThemeVariables;
      variables.remove(QStringLiteral("mainBkg"));
      themeConfig.insert(QStringLiteral("themeVariables"), variables);
    }
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(themeConfig));

    const QJsonObject raw =
        pre.config.value(QStringLiteral("timeline")).toObject();
    timeline::TimelineConfig config;
    config.useMaxWidth = boolValue(raw, "useMaxWidth", true);
    config.leftMarginRaw = scalar(raw, "leftMargin", QJsonValue(150.0));
    config.leftMargin = qreal(jsNumberValue(config.leftMarginRaw));
    const QJsonValue rawPadding = raw.value(QStringLiteral("padding"));
    config.padding = rawPadding.isUndefined() || rawPadding.isNull()
                         ? 50.0
                         : qreal(jsNumberValue(rawPadding));
    config.invalidPadding = !std::isfinite(config.padding);
    if (config.invalidPadding) config.padding = 0.0;
    config.disableMulticolor = boolValue(raw, "disableMulticolor", false);

    timeline::TimelineSceneStyle style;
    style.themeName = effectiveTheme.isEmpty() ? QStringLiteral("default")
                                                : effectiveTheme;
    const QJsonValue look = pre.config.value(QStringLiteral("look"));
    style.look = look.isString() ? look.toString() : QStringLiteral("classic");
    const QJsonValue rawTimelineFontFamily =
        rawThemeVariables.value(QStringLiteral("fontFamily"));
    // Timeline's root stylesheet reads themeVariables.fontFamily. A separate
    // top-level fontFamily may coexist in the source config; it must not
    // overwrite an explicit nested value (the real 11.16 source oracle uses
    // exactly that pair to prove the precedence).
    style.fontFamily = timelineFontFamily(rawTimelineFontFamily,
                                          themeVars.fontFamily);
    const CssLengthContext rootContext =
        pieCssLengthContext(firstFontFamily(style.fontFamily), 16.0);
    const QJsonValue rawFontSize =
        rawThemeVariables.value(QStringLiteral("fontSize"));
    const QString fontSizeCss = rawFontSize.isUndefined() || rawFontSize.isNull()
                                    ? themeVars.fontSize
                                    : jsString(rawFontSize);
    style.fontSize = cssFontSizePx(fontSizeCss, rootContext);
    style.layoutFontSize = timelineLayoutFontSize(
        scalar(pre.config, "fontSize", QJsonValue(16.0)));
    // The Redux timeline stylesheet explicitly applies themeVariables.fontWeight
    // to node labels. Classic and Neo renderers leave the SVG default (400).
    const QJsonValue rawFontWeight =
        rawThemeVariables.value(QStringLiteral("fontWeight"));
    const QString fontWeightCss =
        rawFontWeight.isUndefined() || rawFontWeight.isNull()
            ? themeVars.fontWeight
            : jsString(rawFontWeight);
    style.nodeFontWeight =
        style.themeName.contains(QStringLiteral("redux"), Qt::CaseSensitive)
            ? cssFontWeightToQt(QJsonValue(fontWeightCss), QFont::Normal)
            : QFont::Normal;
    const QJsonValue rawTextColor =
        rawThemeVariables.value(QStringLiteral("textColor"));
    style.textColor = rawTextColor.isUndefined() || rawTextColor.isNull()
                          ? themeVars.textColor
                          : jsString(rawTextColor);
    style.mainBkg = falsyReduxMainBkg(rawMainBkg)
                        ? themeVars.mainBkg
                        : jsString(rawMainBkg);
    const QJsonValue rawNodeBorder =
        rawThemeVariables.value(QStringLiteral("nodeBorder"));
    style.nodeBorder = rawNodeBorder.isUndefined() || rawNodeBorder.isNull()
                           ? themeVars.nodeBorder
                           : jsString(rawNodeBorder);
    style.tertiaryColor = themeVars.tertiaryColor;
    style.clusterBorder = themeVars.clusterBorder;
    style.strokeWidth = themeVars.strokeWidth;
    const QJsonValue rawStrokeWidth =
        rawThemeVariables.value(QStringLiteral("strokeWidth"));
    if (rawStrokeWidth.isUndefined() || rawStrokeWidth.isNull())
      style.strokeWidthCss = jsNumberToString(themeVars.strokeWidth);
    else
      style.strokeWidthCss = jsString(rawStrokeWidth);
    const QJsonValue rawUseGradient =
        rawThemeVariables.value(QStringLiteral("useGradient"));
    style.useGradient = rawUseGradient.isUndefined() || rawUseGradient.isNull()
                            ? themeVars.useGradient
                            : truthyConfigValue(rawUseGradient);
    const QJsonValue rawGradientStart =
        rawThemeVariables.value(QStringLiteral("gradientStart"));
    const QJsonValue rawGradientStop =
        rawThemeVariables.value(QStringLiteral("gradientStop"));
    style.gradientStart =
        rawGradientStart.isUndefined() || rawGradientStart.isNull()
            ? themeVars.gradientStart
            : jsString(rawGradientStart);
    style.gradientStop =
        rawGradientStop.isUndefined() || rawGradientStop.isNull()
            ? themeVars.gradientStop
            : jsString(rawGradientStop);
    const QJsonValue rawThemeColorLimit =
        rawThemeVariables.value(QStringLiteral("THEME_COLOR_LIMIT"));
    style.rawThemeColorLimit =
        rawThemeColorLimit.isUndefined() || rawThemeColorLimit.isNull()
            ? qreal(themeVars.themeColorLimit)
            : qreal(jsNumberValue(rawThemeColorLimit));
    style.themeColorRuleCount =
        jsThemeColorLimit(pre.config).value_or(themeVars.themeColorLimit);
    style.themeColorLimit = style.themeColorRuleCount;
    for (const QString& color : themeVars.cScale) style.cScale.append(color);
    for (const QString& color : themeVars.cScaleInv)
      style.cScaleInv.append(color);
    for (const QString& color : themeVars.cScaleLabel)
      style.cScaleLabel.append(color);
    style.borderColorArray = themeVars.borderColorArray;
    style.bkgColorArray = themeVars.bkgColorArray;

    timeline::TimelineData data = timeline::TimelineDiagram::parse(pre.code);
    timeline::TimelineScene scene = timeline::buildTimelineScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        themeVars.titleColor, scene.style.fontFamily, scene.style.fontSize);
    // Timeline owns its inline title inside the scene. Unlike families backed
    // by commonDb, Mermaid 11.16.0 does not fall back to a frontmatter title.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.config.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene =
        std::make_shared<const timeline::TimelineScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& timelineDiagramAdapter() {
  static const TimelineDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
