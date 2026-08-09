#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/journey/JourneyDiagram.h"
#include "mermaid/journey/JourneyScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

QJsonValue journeyScalar(const QJsonObject& config, QLatin1String key,
                         const QJsonValue& fallback) {
  const QJsonValue value = config.value(key);
  return value.isUndefined() || value.isNull() || value.isArray() || value.isObject()
             ? fallback
             : value;
}

qreal journeyNumber(const QJsonObject& config, QLatin1String key, qreal fallback) {
  return qreal(jsNumberValue(journeyScalar(config, key, QJsonValue(fallback))));
}

QString jsCssString(const QJsonValue& value, const QString& fallback) {
  if (value.isUndefined() || value.isNull()) return fallback;
  if (value.isString()) return value.toString();
  if (value.isBool()) return value.toBool() ? QStringLiteral("true")
                                            : QStringLiteral("false");
  if (value.isDouble()) return jsNumberToString(value.toDouble());
  return value.isArray() ? QString() : QStringLiteral("[object Object]");
}

qreal svgFontSizeFromRaw(const QJsonValue& value,
                         const CssLengthContext& context) {
  if (value.isDouble()) {
    const double number = value.toDouble();
    return std::isfinite(number) && number >= 0.0
               ? std::min<qreal>(number, 10000.0)
               : context.emPx;
  }
  if (!value.isString()) return context.emPx;

  const QString text = value.toString().trimmed();
  static const QRegularExpression bareNumber(
      QStringLiteral(R"(^[+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$)"));
  if (bareNumber.match(text).hasMatch()) {
    bool ok = false;
    const double number = text.toDouble(&ok);
    return ok && std::isfinite(number)
               ? std::min<qreal>(number, 10000.0)
               : context.emPx;
  }
  return cssFontSizePx(text, context);
}

struct JourneyDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("journey")}; }
  QString cssClass() const override { return QStringLiteral("journey"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    journey::JourneyData data = journey::JourneyDiagram::parse(pre.code);
    if (data.title.isEmpty() && !pre.title.isEmpty()) data.title = pre.title;

    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject config = pre.config.value(QStringLiteral("journey")).toObject();

    journey::JourneySceneStyle style;
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    const CssLengthContext htmlRoot = pieCssLengthContext(style.fontFamily, 16.0);
    style.fontSize = cssFontSizePx(themeVars.fontSize, htmlRoot);
    style.textColor = themeVars.textColor;
    for (int i = 0; i < 8; ++i) style.fillTypes.append(themeVars.fillType[i]);

    journey::JourneyConfig journeyConfig;
    const QJsonValue useMaxWidth = config.value(QStringLiteral("useMaxWidth"));
    journeyConfig.useMaxWidth = useMaxWidth.isUndefined() || useMaxWidth.isNull()
                                    ? true
                                    : truthyConfigValue(useMaxWidth);
    journeyConfig.diagramMarginX = journeyNumber(config, QLatin1String("diagramMarginX"), 50.0);
    journeyConfig.diagramMarginY = journeyNumber(config, QLatin1String("diagramMarginY"), 10.0);
    journeyConfig.leftMargin = journeyNumber(config, QLatin1String("leftMargin"), 150.0);
    journeyConfig.maxLabelWidth = journeyNumber(config, QLatin1String("maxLabelWidth"), 360.0);
    journeyConfig.width = journeyNumber(config, QLatin1String("width"), 150.0);
    journeyConfig.height = journeyNumber(config, QLatin1String("height"), 50.0);
    journeyConfig.diagramMarginXRaw = journeyScalar(
        config, QLatin1String("diagramMarginX"), QJsonValue(50.0));
    journeyConfig.diagramMarginYRaw = journeyScalar(
        config, QLatin1String("diagramMarginY"), QJsonValue(10.0));
    journeyConfig.leftMarginRaw = journeyScalar(
        config, QLatin1String("leftMargin"), QJsonValue(150.0));
    journeyConfig.taskMarginRaw = journeyScalar(
        config, QLatin1String("taskMargin"), QJsonValue(50.0));
    const QJsonValue rawWidth = journeyScalar(
        config, QLatin1String("width"), QJsonValue(150.0));
    const QJsonValue rawHeight = journeyScalar(
        config, QLatin1String("height"), QJsonValue(50.0));
    const auto svgNumericAttribute = [](const QJsonValue& value) {
      if (value.isDouble()) return qreal(std::max(0.0, value.toDouble()));
      if (!value.isString()) return qreal(0.0);
      static const QRegularExpression number(
          QStringLiteral(R"(^\s*[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?\s*$)"));
      const qreal parsed = number.match(value.toString()).hasMatch()
                               ? qreal(value.toString().toDouble())
                               : qreal(0.0);
      return std::max<qreal>(0.0, parsed);
    };
    journeyConfig.rectWidth = svgNumericAttribute(rawWidth);
    journeyConfig.rectHeight = svgNumericAttribute(rawHeight);
    journeyConfig.boxTextMargin = journeyNumber(config, QLatin1String("boxTextMargin"), 5.0);
    const QJsonValue taskFontSize = journeyScalar(
        config, QLatin1String("taskFontSize"), QJsonValue(14.0));
    const CssLengthContext taskFontContext =
        pieCssLengthContext(style.fontFamily, style.fontSize);
    journeyConfig.taskFontSize =
        svgFontSizeFromRaw(taskFontSize, taskFontContext);
    journeyConfig.taskFontLineStep = qreal(jsNumberValue(taskFontSize));
    journeyConfig.taskFontFamily = firstFontFamily(jsCssString(
        journeyScalar(config, QLatin1String("taskFontFamily"),
                      QJsonValue(QStringLiteral("\"Open Sans\", sans-serif"))),
        QStringLiteral("\"Open Sans\", sans-serif")));
    journeyConfig.taskMargin = journeyNumber(config, QLatin1String("taskMargin"), 50.0);
    const QJsonValue textPlacement = journeyScalar(
        config, QLatin1String("textPlacement"), QJsonValue(QStringLiteral("fo")));
    journeyConfig.textPlacement = textPlacement.isUndefined() || textPlacement.isNull()
                                      ? QStringLiteral("fo")
                                      : (textPlacement.isString()
                                             ? textPlacement.toString()
                                             : QStringLiteral("__non_string__"));
    journeyConfig.titleColor = jsCssString(
        journeyScalar(config, QLatin1String("titleColor"), QJsonValue(QString())),
        QString());
    journeyConfig.titleFontFamily = firstFontFamily(jsCssString(
        journeyScalar(config, QLatin1String("titleFontFamily"),
                      QJsonValue(QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif"))),
        QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif")));
    const CssLengthContext titleContext =
        pieCssLengthContext(style.fontFamily, style.fontSize);
    journeyConfig.titleFontSize = svgFontSizeFromRaw(
        journeyScalar(config, QLatin1String("titleFontSize"),
                      QJsonValue(QStringLiteral("4ex"))),
        titleContext);

    journey::JourneyScene scene =
        journey::buildJourneyScene(data, std::move(journeyConfig), std::move(style));

    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        scene.config.titleColor.isEmpty() ? scene.style.textColor
                                          : scene.config.titleColor,
        scene.style.fontFamily, scene.style.fontSize, 25.0, 0.0);
    metadata.title = QString();
    metadata.svgUseMaxWidth = scene.config.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const journey::JourneyScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& journeyDiagramAdapter() {
  static const JourneyDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
