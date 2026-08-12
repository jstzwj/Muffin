#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/railroad/RailroadDiagram.h"
#include "mermaid/railroad/RailroadScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QSize>

#include <cmath>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

qreal sanitizedNumber(const QJsonValue& value, qreal fallback,
                      bool positive = false) {
  qreal parsed = std::numeric_limits<qreal>::quiet_NaN();
  if (value.isDouble()) {
    parsed = value.toDouble();
  } else if (value.isString()) {
    static const QRegularExpression prefix(QStringLiteral(
        R"(^[\t\n\v\f\r ]*([+-]?(?:(?:\d+\.?\d*)|(?:\.\d+))(?:[eE][+-]?\d+)?))"));
    const auto match = prefix.match(value.toString());
    if (match.hasMatch()) parsed = match.captured(1).toDouble();
  }
  if (!std::isfinite(parsed) || parsed < 0.0 || (positive && parsed == 0.0))
    return fallback;
  return parsed;
}

QString sanitizedFontFamily(const QJsonValue& value, const QString& fallback) {
  if (!value.isString()) return fallback;
  const QString normalized = value.toString().trimmed();
  static const QRegularExpression allowed(
      QStringLiteral(R"(^[\w "',.\-]+$)"));
  return allowed.match(normalized).hasMatch() ? normalized : fallback;
}

QString sanitizedColor(const QJsonValue& value, const QString& fallback) {
  if (!value.isString()) return fallback;
  const QString normalized = value.toString().trimmed();
  static const QRegularExpression allowed(
      QStringLiteral(
          R"(^#(?:[\da-f]{3,4}|[\da-f]{6}|[\da-f]{8})$|^(?:rgb|rgba|hsl|hsla|hwb|lab|lch|oklab|oklch)\([\d\s%+,./\-]+\)$|^[a-z]+$)"),
      QRegularExpression::CaseInsensitiveOption);
  return allowed.match(normalized).hasMatch() ? normalized : fallback;
}

QString scalarOr(const QString& first, const QString& second,
                 const QString& fallback) {
  return !first.isEmpty() ? first : !second.isEmpty() ? second : fallback;
}

railroad::RailroadConfig railroadConfig(
    const MermaidPreprocessResult& pre,
    const flowtheme::FlowThemeVariables& theme,
    flowtheme::FlowThemeId themeId) {
  railroad::RailroadConfig config;
  const QJsonObject raw = pre.config.value(QStringLiteral("railroad")).toObject();

  config.fontFamily = sanitizedFontFamily(
      QJsonValue(theme.fontFamily), QStringLiteral("monospace"));
  config.fontSize = sanitizedNumber(QJsonValue(theme.fontSize), 14.0, true);
  // Railroad merges getThemeVariables() with the current source config using
  // object spread. Themes in the newer Base/Neo/Redux family omit secondBkg,
  // so the initialized default theme's #ffffde survives that shallow merge.
  const bool themeOmitsSecondBkg =
      themeId == flowtheme::FlowThemeId::Base ||
      themeId == flowtheme::FlowThemeId::Neo ||
      themeId == flowtheme::FlowThemeId::NeoDark ||
      themeId == flowtheme::FlowThemeId::Redux ||
      themeId == flowtheme::FlowThemeId::ReduxDark ||
      themeId == flowtheme::FlowThemeId::ReduxColor ||
      themeId == flowtheme::FlowThemeId::ReduxDarkColor;
  const QString mergedSecondBkg = themeOmitsSecondBkg
      ? QStringLiteral("#ffffde") : theme.secondBkg;
  config.terminalFill = sanitizedColor(
      QJsonValue(scalarOr(mergedSecondBkg, theme.secondaryColor,
                          QStringLiteral("#FFFFC0"))),
      QStringLiteral("#FFFFC0"));
  config.terminalStroke = sanitizedColor(
      QJsonValue(scalarOr(theme.secondaryBorderColor, theme.lineColor,
                          QStringLiteral("#000000"))),
      QStringLiteral("#000000"));
  config.terminalTextColor = sanitizedColor(
      QJsonValue(scalarOr(theme.secondaryTextColor, theme.textColor,
                          QStringLiteral("#000000"))),
      QStringLiteral("#000000"));
  config.nonTerminalFill = sanitizedColor(
      QJsonValue(scalarOr(theme.mainBkg, theme.background,
                          QStringLiteral("#FFFFFF"))),
      QStringLiteral("#FFFFFF"));
  config.nonTerminalStroke = sanitizedColor(
      QJsonValue(scalarOr(theme.primaryBorderColor, theme.lineColor,
                          QStringLiteral("#000000"))),
      QStringLiteral("#000000"));
  config.nonTerminalTextColor = sanitizedColor(
      QJsonValue(scalarOr(theme.primaryTextColor, theme.textColor,
                          QStringLiteral("#000000"))),
      QStringLiteral("#000000"));
  config.lineColor = sanitizedColor(QJsonValue(theme.lineColor),
                                    QStringLiteral("#000000"));
  config.markerFill = config.lineColor;
  config.commentFill = sanitizedColor(
      QJsonValue(scalarOr(theme.labelBackground, theme.tertiaryColor,
                          QStringLiteral("#E8E8E8"))),
      QStringLiteral("#E8E8E8"));
  config.commentStroke = sanitizedColor(
      QJsonValue(scalarOr(theme.tertiaryBorderColor, theme.lineColor,
                          QStringLiteral("#888888"))),
      QStringLiteral("#888888"));
  config.commentTextColor = sanitizedColor(
      QJsonValue(scalarOr(theme.tertiaryTextColor, theme.textColor,
                          QStringLiteral("#666666"))),
      QStringLiteral("#666666"));
  config.specialFill = sanitizedColor(
      QJsonValue(scalarOr(theme.tertiaryColor, theme.secondaryColor,
                          QStringLiteral("#F0E0FF"))),
      QStringLiteral("#F0E0FF"));
  config.specialStroke = sanitizedColor(
      QJsonValue(scalarOr(theme.tertiaryBorderColor,
                          theme.secondaryBorderColor,
                          QStringLiteral("#8800CC"))),
      QStringLiteral("#8800CC"));
  config.ruleNameColor = sanitizedColor(
      QJsonValue(scalarOr(theme.titleColor, theme.textColor,
                          QStringLiteral("#000066"))),
      QStringLiteral("#000066"));

  config.padding = sanitizedNumber(raw.value(QStringLiteral("padding")),
                                   config.padding);
  config.fontSize = sanitizedNumber(raw.value(QStringLiteral("fontSize")),
                                    config.fontSize);
  config.fontFamily = sanitizedFontFamily(
      raw.value(QStringLiteral("fontFamily")), config.fontFamily);
  config.terminalFill = sanitizedColor(
      raw.value(QStringLiteral("terminalFill")), config.terminalFill);
  config.terminalStroke = sanitizedColor(
      raw.value(QStringLiteral("terminalStroke")), config.terminalStroke);
  config.terminalTextColor = sanitizedColor(
      raw.value(QStringLiteral("terminalTextColor")), config.terminalTextColor);
  config.nonTerminalFill = sanitizedColor(
      raw.value(QStringLiteral("nonTerminalFill")), config.nonTerminalFill);
  config.nonTerminalStroke = sanitizedColor(
      raw.value(QStringLiteral("nonTerminalStroke")), config.nonTerminalStroke);
  config.nonTerminalTextColor = sanitizedColor(
      raw.value(QStringLiteral("nonTerminalTextColor")),
      config.nonTerminalTextColor);
  config.lineColor = sanitizedColor(raw.value(QStringLiteral("lineColor")),
                                    config.lineColor);
  config.strokeWidth = sanitizedNumber(raw.value(QStringLiteral("strokeWidth")),
                                       config.strokeWidth);
  config.markerFill = sanitizedColor(raw.value(QStringLiteral("markerFill")),
                                     config.markerFill);
  config.commentFill = sanitizedColor(raw.value(QStringLiteral("commentFill")),
                                      config.commentFill);
  config.commentStroke = sanitizedColor(
      raw.value(QStringLiteral("commentStroke")), config.commentStroke);
  config.commentTextColor = sanitizedColor(
      raw.value(QStringLiteral("commentTextColor")), config.commentTextColor);
  config.specialFill = sanitizedColor(raw.value(QStringLiteral("specialFill")),
                                      config.specialFill);
  config.specialStroke = sanitizedColor(
      raw.value(QStringLiteral("specialStroke")), config.specialStroke);
  config.ruleNameColor = sanitizedColor(
      raw.value(QStringLiteral("ruleNameColor")), config.ruleNameColor);
  // Mermaid's source-entry config sanitizer removes compactMode,
  // vertical/horizontalSeparation, arcRadius, showMarkers, and markerRadius.
  // They remain renderer API fields upstream, but are intentionally inert for
  // %%init/frontmatter input, which is Muffin's exposed configuration surface.
  const QJsonValue maxWidth = raw.value(QStringLiteral("useMaxWidth"));
  config.useMaxWidth = maxWidth.isUndefined() || maxWidth.isNull()
                           ? true
                           : truthyConfigValue(maxWidth);
  return config;
}

class RailroadDiagramImpl final : public Diagram {
public:
  RailroadDiagramImpl(railroad::RailroadDialect dialect, QString id)
      : dialect_(dialect), id_(std::move(id)) {}

  QStringList ids() const override { return {id_}; }
  QString cssClass() const override { return QStringLiteral("railroad"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& themeName) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? themeName : configuredTheme;
    const flowtheme::FlowThemeId themeId = themeIdFromName(effectiveTheme);
    const flowtheme::FlowThemeVariables theme = flowtheme::resolveFlowTheme(
        themeId, themeOverrides(pre.config));
    railroad::RailroadConfig config = railroadConfig(pre, theme, themeId);
    railroad::RailroadData data =
        railroad::RailroadDiagram::parse(pre.code, dialect_);
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);
    railroad::RailroadScene scene =
        railroad::buildRailroadScene(data, std::move(config));

    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        scene.config.ruleNameColor, scene.config.fontFamily,
        scene.config.fontSize);
    // railroadRenderer never paints diagramTitle; the common accessibility
    // title/description still flow through the root SVG metadata.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.config.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    // Browser element screenshots round the CSS-pixel client box rather than
    // always expanding fractional viewBox dimensions.
    entry.naturalSize = scene.rasterBounds.size().toSize();
    entry.scene =
        std::make_shared<const railroad::RailroadScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }

private:
  railroad::RailroadDialect dialect_;
  QString id_;
};

}  // namespace

const Diagram& railroadDiagramAdapter() {
  static const RailroadDiagramImpl impl(railroad::RailroadDialect::Direct,
                                         QStringLiteral("railroad"));
  return impl;
}

const Diagram& railroadEbnfDiagramAdapter() {
  static const RailroadDiagramImpl impl(railroad::RailroadDialect::Ebnf,
                                         QStringLiteral("railroadEbnf"));
  return impl;
}

const Diagram& railroadAbnfDiagramAdapter() {
  static const RailroadDiagramImpl impl(railroad::RailroadDialect::Abnf,
                                         QStringLiteral("railroadAbnf"));
  return impl;
}

const Diagram& railroadPegDiagramAdapter() {
  static const RailroadDiagramImpl impl(railroad::RailroadDialect::Peg,
                                         QStringLiteral("railroadPeg"));
  return impl;
}

}  // namespace muffin::mermaid::editor
