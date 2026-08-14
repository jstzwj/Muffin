#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"
#include "mermaid/timeline/TimelineDiagram.h"
#include "mermaid/timeline/TimelineScene.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSize>

#include <cmath>
#include <limits>
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

// Live subset of timeline getStyles (Mermaid 11.16.0). Dead for the timeline
// DOM: `.edge` (no edge elements), `.node-icon-N`, `.section-edge-N`,
// `.edge-depth-N`, `.disabled`, `.section-root`, `.icon-container`; the
// `.eventWrapper { filter: brightness(120%) }` glow is modelled by the
// painter's event brightness instead. Invalid palette slots make upstream
// emit `fill: undefined`, which the CSSOM drops — those declarations are
// skipped while the remaining rule text stays, so later `.lineWrapper line`
// repetitions keep overriding earlier ones exactly like the browser.
QString timelineBaseCss(const timeline::TimelineSceneStyle& style,
                        const QString& reduxFontWeight) {
  const bool redux =
      style.themeName.contains(QStringLiteral("redux"), Qt::CaseSensitive);
  const bool dark =
      redux && style.themeName.contains(QStringLiteral("dark"), Qt::CaseSensitive);
  const bool color =
      redux && style.themeName.contains(QStringLiteral("color"), Qt::CaseSensitive);
  const bool gradient = style.look == QStringLiteral("neo") && style.useGradient &&
                        style.themeName != QStringLiteral("neutral");
  const auto scaleAt = [&style](int index) {
    return index >= 0 && index < style.cScale.size() ? style.cScale.at(index)
                                                    : QString();
  };
  const auto invAt = [&style](int index) {
    return index >= 0 && index < style.cScaleInv.size() ? style.cScaleInv.at(index)
                                                       : QString();
  };
  const auto labelAt = [&style](int index) {
    return index >= 0 && index < style.cScaleLabel.size()
               ? style.cScaleLabel.at(index)
               : QString();
  };
  const auto arrayAt = [&style](const QStringList& values, int index) {
    return index >= 0 && index < values.size() ? values.at(index) : QString();
  };
  QString css;
  for (int i = 0; i < style.themeColorRuleCount; ++i) {
    const QString idx = QString::number(i - 1);
    if (redux) {
      const QString fill = color ? (dark ? style.mainBkg
                                         : arrayAt(style.borderColorArray, i))
                                 : style.mainBkg;
      const QString stroke =
          color ? arrayAt(style.borderColorArray, i) : style.nodeBorder;
      QStringList paintDecls;
      if (!fill.isEmpty()) paintDecls << QStringLiteral("fill: %1").arg(fill);
      if (!stroke.isEmpty())
        paintDecls << QStringLiteral("stroke: %1").arg(stroke);
      if (!style.strokeWidthCss.isEmpty())
        paintDecls << QStringLiteral("stroke-width: %1").arg(style.strokeWidthCss);
      css += QStringLiteral(
                 ".section-%1 rect, .section-%1 path, .section-%1 circle { %2 }\n")
                 .arg(idx, paintDecls.join(QLatin1Char(';')));
      css += QStringLiteral(".section-%1 text { fill: %2; font-weight: %3; }\n")
                 .arg(idx, style.nodeBorder, reduxFontWeight);
      css += QStringLiteral(".section-%1 line { stroke-width: 3px; }\n").arg(idx);
      const QString inv = invAt(i);
      if (!inv.isEmpty())
        css += QStringLiteral(".section-%1 line { stroke: %2; }\n").arg(idx, inv);
      css += QStringLiteral(".lineWrapper line { stroke: %1; stroke-width: %2; }\n")
                 .arg(style.nodeBorder, style.strokeWidthCss);
    } else {
      const QString fill = scaleAt(i);
      if (!fill.isEmpty())
        css += QStringLiteral(
                   ".section-%1 rect, .section-%1 path, .section-%1 circle { fill: %2; }\n")
                   .arg(idx, fill);
      const QString label = labelAt(i);
      if (!label.isEmpty())
        css += QStringLiteral(".section-%1 text { fill: %2; }\n").arg(idx, label);
      css += QStringLiteral(".section-%1 line { stroke-width: 3px; }\n").arg(idx);
      const QString inv = invAt(i);
      if (!inv.isEmpty())
        css += QStringLiteral(".section-%1 line { stroke: %2; }\n").arg(idx, inv);
      if (!label.isEmpty())
        css += QStringLiteral(".lineWrapper line { stroke: %1; }\n").arg(label);
    }
    if (gradient) {
      css += QStringLiteral(
                 ".section-%1[data-look=\"neo\"] rect, "
                 ".section-%1[data-look=\"neo\"] path, "
                 ".section-%1[data-look=\"neo\"] circle { fill: %2; "
                 "stroke: url(#diagram-root-gradient); stroke-width: 2px; }\n"
                 ".section-%1[data-look=\"neo\"] line { "
                 "stroke: url(#diagram-root-gradient); stroke-width: 2px; }\n")
                 .arg(idx, style.mainBkg);
    }
  }
  return css;
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

    // themeCSS: resolve the user sheet against a faithful model of the
    // timeline DOM (two classless scaffold groups, defs/marker, then
    // section/task/event groups interleaved in draw order, title, axis).
    // Base rules come first so the `.section-N` declarations keep overriding
    // presentation attributes exactly as upstream's stylesheet does.
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    timeline::TimelineCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      const QString baseCss = timelineBaseCss(style, fontWeightCss);
      // Mermaid paints `#id { font-family; font-size; fill: textColor }`
      // directly on the svg element; `svg {}` user rules are scoped to
      // `#id svg` and can never reach the root itself.
      ElementStyle rootStyle;
      rootStyle.fill = style.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize =
          QString::number(style.fontSize) + QStringLiteral("px");
      rootStyle.fontWeight = QStringLiteral("400");
      // Empty members inherit the parent's resolved value via project().
      const ElementStyle inheritAll;

      // Pass 1 — node heights are measured through a transient classless
      // <text> probe, so `text {}` font rules move the layout.
      const QHash<QString, ElementStyle> skeleton = csscascade::resolveElements(
          themeCss,
          {ElementInput{QStringLiteral("svg"), {}, QStringLiteral("svg"),
                        QStringLiteral("diagram-root"), {}, {}, rootStyle, {}},
           ElementInput{QStringLiteral("measure"), QStringLiteral("svg"),
                        QStringLiteral("text"), {}, {}, {}, inheritAll, {}}},
          baseCss);
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, style.fontSize);
      const auto fontSizePx = [&familyCtx](const QString& css) {
        return cssFontSizePx(css, familyCtx);
      };
      overrides.active = true;
      const ElementStyle& measured = skeleton.value(QStringLiteral("measure"));
      overrides.measureText.fontFamily = firstFontFamily(
          measured.fontFamily.isEmpty() ? style.fontFamily : measured.fontFamily);
      overrides.measureText.fontSize = fontSizePx(measured.fontSize);
      overrides.measureText.hasBox = measured.hasBox();

      // Pass 2 — the complete element tree, siblings in document order.
      QVector<ElementInput> tree;
      const auto push = [&tree](ElementInput input) {
        tree.append(std::move(input));
      };
      push({QStringLiteral("svg"), {}, QStringLiteral("svg"),
            QStringLiteral("diagram-root"), {}, {}, rootStyle, {}});
      const bool td = data.direction == timeline::TimelineDirection::TopDown;
      // TD lowers the axis group beneath everything, making it the svg's
      // first child; LR appends it after the title.
      if (td) {
        push({QStringLiteral("axis-wrap"), QStringLiteral("svg"),
              QStringLiteral("g"), {}, {QStringLiteral("lineWrapper")}, {},
              inheritAll, {}});
        push({QStringLiteral("axis"), QStringLiteral("axis-wrap"),
              QStringLiteral("line"), {}, {}, {},
              inheritAll, {}, QStringLiteral("stroke:black;stroke-width:4px")});
      }
      push({QStringLiteral("styleEl"), QStringLiteral("svg"),
            QStringLiteral("style"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("scaffold1"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("scaffold2"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("defs"), QStringLiteral("svg"),
            QStringLiteral("defs"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("marker"), QStringLiteral("defs"),
            QStringLiteral("marker"), {}, {}, {}, inheritAll, {}});
      // The arrowhead path carries no fill attribute: it inherits the root.
      push({QStringLiteral("markerPath"), QStringLiteral("marker"),
            QStringLiteral("path"), {}, {}, {}, inheritAll, {}});

      const bool neo = style.look == QStringLiteral("neo");
      const qreal rawLimit = style.rawThemeColorLimit;
      const auto sectionSuffix = [rawLimit](int fullSection) {
        const qreal value =
            std::isfinite(rawLimit) && rawLimit != 0.0
                ? std::fmod(qreal(fullSection), rawLimit) - 1.0
                : std::numeric_limits<qreal>::quiet_NaN();
        return jsNumberToString(value);
      };
      int pathId = 0;
      qsizetype nodeIndex = 0;
      qsizetype connectorIndex = 0;
      const auto nodeKey = [](qsizetype index) {
        return QStringLiteral("node-%1").arg(index);
      };
      const auto pushNode = [&](const QString& key, const QString& parentKey,
                                int sectionNumber) {
        const QString suffix = sectionSuffix(sectionNumber);
        QHash<QString, QString> nodeAttrs;
        if (neo)
          nodeAttrs.insert(QStringLiteral("data-look"), QStringLiteral("neo"));
        push({key + QStringLiteral("-node"), parentKey, QStringLiteral("g"), {},
              {QStringLiteral("timeline-node"),
               QStringLiteral("section-") + suffix},
              nodeAttrs, inheritAll, {}});
        push({key + QStringLiteral("-bkg"), key + QStringLiteral("-node"),
              QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
        QHash<QString, QString> pathAttrs;
        pathAttrs.insert(
            QStringLiteral("id"),
            QStringLiteral("diagram-root-node-%1").arg(pathId++));
        push({key + QStringLiteral("-box"), key + QStringLiteral("-bkg"),
              QStringLiteral("path"), {},
              {QStringLiteral("node-bkg"),
               QStringLiteral("node-undefined")},
              pathAttrs, inheritAll, {}});
        push({key + QStringLiteral("-divider"), key + QStringLiteral("-bkg"),
              QStringLiteral("line"), {},
              {QStringLiteral("node-line-") + suffix}, {}, inheritAll, {}});
        push({key + QStringLiteral("-textg"), key + QStringLiteral("-node"),
              QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
        push({key + QStringLiteral("-text"), key + QStringLiteral("-textg"),
              QStringLiteral("text"), {}, {}, {}, inheritAll, {}});
      };
      const auto pushConnector = [&](const QString& key) {
        push({key + QStringLiteral("-wrap"), QStringLiteral("svg"),
              QStringLiteral("g"), {}, {QStringLiteral("lineWrapper")}, {},
              inheritAll, {}});
        // Classless line: `.lineWrapper line` and structural rules reach it
        // through its parent group.
        push({key, key + QStringLiteral("-wrap"), QStringLiteral("line"), {},
              {}, {}, inheritAll, {},
              QStringLiteral("stroke:black;stroke-width:2px")});
      };

      const bool hasSections = !data.sections.isEmpty();
      const auto tasksFor = [&data](const QString& section) {
        QVector<qsizetype> indices;
        for (qsizetype i = 0; i < data.tasks.size(); ++i)
          if (data.tasks.at(i).section == section) indices.append(i);
        return indices;
      };
      int sectionNumber = 0;
      if (hasSections) {
        for (const QString& section : data.sections) {
          const QVector<qsizetype> selected = tasksFor(section);
          const QString wrapKey =
              QStringLiteral("section-wrap-%1").arg(sectionNumber);
          push({wrapKey, QStringLiteral("svg"), QStringLiteral("g"), {}, {},
                {}, inheritAll, {}});
          pushNode(nodeKey(nodeIndex++), wrapKey, sectionNumber);
          for (const qsizetype taskIndex : selected) {
            const timeline::TimelineTask& task = data.tasks.at(taskIndex);
            const QString taskKey = QStringLiteral("task-%1").arg(taskIndex);
            push({taskKey + QStringLiteral("-wrap"), QStringLiteral("svg"),
                  QStringLiteral("g"), {}, {QStringLiteral("taskWrapper")}, {},
                  inheritAll, {}});
            pushNode(nodeKey(nodeIndex++), taskKey + QStringLiteral("-wrap"),
                     sectionNumber);
            if (td) {
              for (qsizetype e = 0; e < task.events.size(); ++e) {
                const QString eventKey =
                    QStringLiteral("event-%1-%2").arg(taskIndex).arg(e);
                push({eventKey + QStringLiteral("-wrap"),
                      QStringLiteral("svg"), QStringLiteral("g"), {},
                      {QStringLiteral("eventWrapper")}, {}, inheritAll, {}});
                pushNode(nodeKey(nodeIndex++),
                         eventKey + QStringLiteral("-wrap"), sectionNumber);
                pushConnector(QStringLiteral("conn-%1").arg(connectorIndex++));
              }
            } else {
              // LR creates the connector group for every task: `task.events`
              // is an (possibly empty) array, which is truthy in JS.
              pushConnector(QStringLiteral("conn-%1").arg(connectorIndex++));
              for (qsizetype e = 0; e < task.events.size(); ++e) {
                const QString eventKey =
                    QStringLiteral("event-%1-%2").arg(taskIndex).arg(e);
                push({eventKey + QStringLiteral("-wrap"),
                      QStringLiteral("svg"), QStringLiteral("g"), {},
                      {QStringLiteral("eventWrapper")}, {}, inheritAll, {}});
                pushNode(nodeKey(nodeIndex++),
                         eventKey + QStringLiteral("-wrap"), sectionNumber);
              }
            }
          }
          ++sectionNumber;
        }
      } else {
        int taskSection = 0;
        for (qsizetype taskIndex = 0; taskIndex < data.tasks.size();
             ++taskIndex) {
          const timeline::TimelineTask& task = data.tasks.at(taskIndex);
          const QString taskKey = QStringLiteral("task-%1").arg(taskIndex);
          push({taskKey + QStringLiteral("-wrap"), QStringLiteral("svg"),
                QStringLiteral("g"), {}, {QStringLiteral("taskWrapper")}, {},
                inheritAll, {}});
          pushNode(nodeKey(nodeIndex++), taskKey + QStringLiteral("-wrap"),
                   taskSection);
          if (td) {
            for (qsizetype e = 0; e < task.events.size(); ++e) {
              const QString eventKey =
                  QStringLiteral("event-%1-%2").arg(taskIndex).arg(e);
              push({eventKey + QStringLiteral("-wrap"),
                    QStringLiteral("svg"), QStringLiteral("g"), {},
                    {QStringLiteral("eventWrapper")}, {}, inheritAll, {}});
              pushNode(nodeKey(nodeIndex++),
                       eventKey + QStringLiteral("-wrap"), taskSection);
              pushConnector(QStringLiteral("conn-%1").arg(connectorIndex++));
            }
          } else {
            pushConnector(QStringLiteral("conn-%1").arg(connectorIndex++));
            for (qsizetype e = 0; e < task.events.size(); ++e) {
              const QString eventKey =
                  QStringLiteral("event-%1-%2").arg(taskIndex).arg(e);
              push({eventKey + QStringLiteral("-wrap"),
                    QStringLiteral("svg"), QStringLiteral("g"), {},
                    {QStringLiteral("eventWrapper")}, {}, inheritAll, {}});
              pushNode(nodeKey(nodeIndex++),
                       eventKey + QStringLiteral("-wrap"), taskSection);
            }
          }
          if (!config.disableMulticolor) ++taskSection;
        }
      }

      if (!data.title.isEmpty()) {
        const qreal titlePx = timeline::timelineTitleFontSizePx(style);
        push({QStringLiteral("title"), QStringLiteral("svg"),
              QStringLiteral("text"), {}, {}, {}, inheritAll, {},
              QStringLiteral("font-size:%1px;font-weight:bold")
                  .arg(QString::number(titlePx))});
      }
      if (!td) {
        push({QStringLiteral("axis-wrap"), QStringLiteral("svg"),
              QStringLiteral("g"), {}, {QStringLiteral("lineWrapper")}, {},
              inheritAll, {}});
        push({QStringLiteral("axis"), QStringLiteral("axis-wrap"),
              QStringLiteral("line"), {}, {}, {}, inheritAll, {},
              QStringLiteral("stroke:black;stroke-width:4px")});
      }

      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, baseCss);
      const auto convert = [&](const QString& key) {
        timeline::TimelineElementCss out;
        const ElementStyle& resolved = css.value(key);
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        out.color = resolved.color;
        // The engine only resolves font-family through matching rules or the
        // inherit keyword; with no declaration the value stays empty and the
        // element keeps the root font chain.
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = fontSizePx(resolved.fontSize);
        out.fontWeight = resolved.fontWeight;
        out.opacity = cssOpacity(resolved.opacity);
        out.visible = resolved.displayed();
        out.hasBox = resolved.hasBox();
        return out;
      };
      overrides.title = convert(QStringLiteral("title"));
      overrides.axis = convert(QStringLiteral("axis"));
      for (qsizetype i = 0; i < connectorIndex; ++i)
        overrides.connectors.append(
            convert(QStringLiteral("conn-%1").arg(i)));
      for (qsizetype i = 0; i < nodeIndex; ++i) {
        const QString key = nodeKey(i);
        timeline::TimelineCssOverrides::Node resolved;
        resolved.box = convert(key + QStringLiteral("-box"));
        resolved.divider = convert(key + QStringLiteral("-divider"));
        resolved.text = convert(key + QStringLiteral("-text"));
        overrides.nodes.append(std::move(resolved));
      }
    }

    timeline::TimelineScene scene = timeline::buildTimelineScene(
        data, std::move(config), std::move(style),
        themeCssActive ? &overrides : nullptr);
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
