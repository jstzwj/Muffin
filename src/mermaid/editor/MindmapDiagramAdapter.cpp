#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/mindmap/MindmapDiagram.h"
#include "mermaid/mindmap/MindmapScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue nullishValue(const QJsonObject& object, const char* key,
                        const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() ? fallback : value;
}

quint32 handDrawnSeed(const QJsonValue& raw) {
  const double value = jsNumberValue(raw);
  if (!std::isfinite(value) || value <= 0.0) return 0;
  constexpr double kMax = double(std::numeric_limits<quint32>::max());
  return static_cast<quint32>(std::min(value, kMax));
}

QString canonicalMindmapTheme(const QString& candidate) {
  static const QStringList kThemes = {
      QStringLiteral("base"),             QStringLiteral("dark"),
      QStringLiteral("default"),          QStringLiteral("forest"),
      QStringLiteral("neutral"),          QStringLiteral("neo"),
      QStringLiteral("neo-dark"),         QStringLiteral("redux"),
      QStringLiteral("redux-dark"),       QStringLiteral("redux-color"),
      QStringLiteral("redux-dark-color")};
  // Mermaid's source config theme names are case-sensitive. Unknown spelling,
  // including Redux/REDUX, is sanitized back to the default theme.
  return kThemes.contains(candidate) ? candidate : QStringLiteral("default");
}

bool effectiveHtmlLabels(const QJsonValue& raw) {
  if (raw.isUndefined() || raw.isNull()) return true;
  if (raw.isBool() && !raw.toBool()) return false;
  QString text;
  if (raw.isString()) text = raw.toString();
  else if (raw.isDouble()) text = jsNumberToString(raw.toDouble());
  else text = raw.toVariant().toString();
  const QString normalized = text.trimmed().toLower();
  return normalized != QLatin1String("false") &&
      normalized != QLatin1String("null") &&
      normalized != QLatin1String("0");
}

struct MindmapDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("mindmap")}; }
  QString cssClass() const override { return QStringLiteral("mindmap"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = canonicalMindmapTheme(
        configuredTheme.isEmpty() ? theme : configuredTheme);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject family =
        pre.config.value(QStringLiteral("mindmap")).toObject();
    const QJsonValue rawLook = pre.config.value(QStringLiteral("look"));
    const QString look = rawLook.isString() ? rawLook.toString()
                                             : QStringLiteral("classic");
    const QJsonValue rawLayout = pre.config.value(QStringLiteral("layout"));

    mindmap::MindmapParseConfig parseConfig;
    parseConfig.padding = nullishValue(family, "padding", QJsonValue(10.0));
    parseConfig.maxNodeWidth =
        nullishValue(family, "maxNodeWidth", QJsonValue(200.0));
    parseConfig.useMaxWidth =
        nullishValue(family, "useMaxWidth", QJsonValue(true));
    parseConfig.look = look;
    parseConfig.theme = effectiveTheme;
    parseConfig.userDefinedLayout =
        !rawLayout.isUndefined() && !rawLayout.isNull();
    if (parseConfig.userDefinedLayout) parseConfig.layout = rawLayout.toString();
    const mindmap::MindmapData data =
        mindmap::MindmapDiagram::parse(pre.code, parseConfig);

    mindmap::MindmapConfig config;
    config.padding = parseConfig.padding;
    config.maxNodeWidth = parseConfig.maxNodeWidth;
    config.useMaxWidth = parseConfig.useMaxWidth;
    const QJsonValue rawHtmlLabels =
        pre.config.value(QStringLiteral("htmlLabels"));
    config.htmlLabels = effectiveHtmlLabels(rawHtmlLabels);
    const QJsonValue rawAutoWrap =
        pre.config.value(QStringLiteral("markdownAutoWrap"));
    // handle-markdown-text.ts tests strict identity with false. Numeric zero
    // and the string "false" therefore keep auto wrapping enabled.
    config.markdownAutoWrap = !(rawAutoWrap.isBool() && !rawAutoWrap.toBool());
    config.handDrawnSeed = handDrawnSeed(
        nullishValue(pre.config, "handDrawnSeed", QJsonValue(0.0)));

    const QJsonObject rawThemeVariables =
        pre.config.value(QStringLiteral("themeVariables")).toObject();
    mindmap::MindmapSceneStyle style;
    style.themeName = parseConfig.theme;
    style.look = look;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(style.fontFamily, 16.0);
    style.fontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.mainBkg = themeVars.mainBkg;
    style.rootFill = themeVars.git0;
    style.rootTextColor = themeVars.gitBranchLabel0;
    style.nodeBorder = themeVars.nodeBorder;
    style.lineColor = themeVars.lineColor;
    style.strokeWidth = themeVars.strokeWidth;
    style.gradientStart = themeVars.gradientStart;
    style.gradientStop = themeVars.gradientStop;
    const QJsonValue rawUseGradient =
        rawThemeVariables.value(QStringLiteral("useGradient"));
    style.useGradient = rawUseGradient.isUndefined() || rawUseGradient.isNull()
                            ? themeVars.useGradient
                            : truthyConfigValue(rawUseGradient);
    const QJsonValue rawDropShadow =
        rawThemeVariables.value(QStringLiteral("dropShadow"));
    // Mermaid source sanitization/CSS used-value handling disables the filter
    // for every explicit source value (none, invalid, or custom). Only a
    // missing/null override preserves the built-in theme filter.
    style.dropShadow = rawDropShadow.isUndefined() || rawDropShadow.isNull()
                           ? themeVars.dropShadow
                           : QStringLiteral("none");
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

    mindmap::MindmapScene scene = mindmap::buildMindmapScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), scene.style.textColor,
        scene.style.fontFamily, scene.style.fontSize);
    // Mindmap has no commonDb title/accessibility grammar. Mermaid 11.16.0
    // ignores frontmatter titles for this family, including SVG ARIA linkage.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene =
        std::make_shared<const mindmap::MindmapScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& mindmapDiagramAdapter() {
  static const MindmapDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
