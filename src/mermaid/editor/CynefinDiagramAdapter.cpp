#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/cynefin/CynefinDiagram.h"
#include "mermaid/cynefin/CynefinScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue scalar(const QJsonObject &object, const char *key,
                  const QJsonValue &fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct CynefinDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("cynefin")}; }
  QString cssClass() const override { return QStringLiteral("cynefin"); }

  MermaidRenderEntry render(const MermaidPreprocessResult &pre,
                            const QString &type,
                            const QString &theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject raw = pre.config.value(QStringLiteral("cynefin")).toObject();

    cynefin::CynefinConfig config;
    config.useMaxWidth = scalar(raw, "useMaxWidth", true);
    config.width = scalar(raw, "width", 800.0);
    config.height = scalar(raw, "height", 600.0);
    config.padding = scalar(raw, "padding", 40.0);
    config.showDomainDescriptions = scalar(raw, "showDomainDescriptions", true);
    config.boundaryAmplitude = scalar(raw, "boundaryAmplitude", 8.0);
    config.seed = scalar(raw, "seed", 0.0);

    cynefin::CynefinSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext html = pieCssLengthContext(style.fontFamily, 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, html);
    style.domainFontSize = themeVars.cynefin.domainFontSize.toDouble();
    style.itemFontSize = themeVars.cynefin.itemFontSize.toDouble();
    style.boundaryColor = themeVars.cynefin.boundaryColor;
    style.boundaryWidth = themeVars.cynefin.boundaryWidth;
    style.cliffColor = themeVars.cynefin.cliffColor;
    style.cliffWidth = themeVars.cynefin.cliffWidth;
    style.arrowColor = themeVars.cynefin.arrowColor;
    style.arrowWidth = themeVars.cynefin.arrowWidth;
    style.complexBg = themeVars.cynefin.complexBg;
    style.complicatedBg = themeVars.cynefin.complicatedBg;
    style.chaoticBg = themeVars.cynefin.chaoticBg;
    style.clearBg = themeVars.cynefin.clearBg;
    style.confusionBg = themeVars.cynefin.confusionBg;
    style.textColor = themeVars.cynefin.textColor;
    style.labelColor = themeVars.cynefin.labelColor;
    const QJsonObject rawTheme =
        pre.config.value(QStringLiteral("themeVariables")).toObject()
            .value(QStringLiteral("cynefin")).toObject();
    if (rawTheme.contains(QStringLiteral("domainFontSize")))
      style.domainFontSize = scalar(rawTheme, "domainFontSize",
                                    themeVars.cynefin.domainFontSize);
    if (rawTheme.contains(QStringLiteral("itemFontSize")))
      style.itemFontSize = scalar(rawTheme, "itemFontSize",
                                  themeVars.cynefin.itemFontSize);

    cynefin::CynefinData data = cynefin::CynefinDiagram::parse(pre.code);
    if (!data.hasTitleDirective && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);
    cynefin::CynefinScene scene =
        cynefin::buildCynefinScene(data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, data.title, data.accTitle, data.accDescr,
                       themeVars.cynefin.labelColor, themeVars.fontFamily,
                       scene.title.fontSize > 0.0 ? scene.title.fontSize : 18.0);
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene = std::make_shared<const cynefin::CynefinScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

} // namespace

const Diagram &cynefinDiagramAdapter() {
  static const CynefinDiagramImpl adapter;
  return adapter;
}

} // namespace muffin::mermaid::editor
