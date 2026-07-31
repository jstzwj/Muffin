#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/erdiagram/ErDiagram.h"
#include "mermaid/erdiagram/ErLayout.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QVector>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

// erDiagram behind the Diagram contract. Body is the former renderSource()
// er branch, verbatim.
struct ErDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("er")}; }
  QString cssClass() const override { return QStringLiteral("erDiagram"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const er::ErDiagram diagram = er::ErDiagram::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
        themeOverrides(pre.config));
    const er::ErLayoutInput input = er::buildErLayoutInput(diagram.data());
    const QString fontFamily = firstFontFamily(themeVars.fontFamily);
    const qreal fontSize = pixelValue(themeVars.fontSize, 16.0);
    const QJsonObject erConfig = pre.config.value(QStringLiteral("er")).toObject();
    const er::ErLayoutMeasurements measurements = er::measureErLayoutInput(
        input, fontFamily, fontSize,
        configNumber(erConfig, QStringLiteral("minEntityWidth"), 100.0),
        configNumber(erConfig, QStringLiteral("diagramPadding"), 20.0),
        configNumber(erConfig, QStringLiteral("entityPadding"), 15.0));
    const er::ErPlacementResult placement = er::layoutErDiagramDagre(
        input, measurements,
        configNumber(erConfig, QStringLiteral("nodeSpacing"), 140.0),
        configNumber(erConfig, QStringLiteral("rankSpacing"), 80.0));
    er::ErSceneStyle style;
    style.entityFill = themeVars.mainBkg;
    style.entityStroke = themeVars.border1;
    style.entityTitle1 = themeVars.primaryTextColor;
    style.attributeColor = themeVars.primaryTextColor;
    style.relationshipColor = themeVars.lineColor;
    style.relationshipLabelColor = themeVars.textColor;
    style.labelBackground = themeVars.mainBkg;
    style.strokeWidth = themeVars.strokeWidth;
    style.fontFamily = fontFamily;
    style.fontSize = fontSize;
    style.lineHeight = fontSize * 1.5;
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, diagram.data().title, diagram.data().accTitle,
        diagram.data().accDescription, style.entityTitle1, style.fontFamily,
        18.0, configNumber(erConfig, QStringLiteral("titleTopMargin"), 25.0), 8.0);
    QVector<style::ClassDef> erStyleDefs;
    for (auto it = diagram.data().classDefs.constBegin();
         it != diagram.data().classDefs.constEnd(); ++it)
      erStyleDefs.append({it.key(), it.value()});
    style::ThemeDefaults erTheme;
    erTheme.mainBkg = themeVars.mainBkg;
    erTheme.nodeBorder = themeVars.border1;
    erTheme.lineColor = themeVars.lineColor;
    erTheme.strokeWidth = themeVars.strokeWidth;
    erTheme.textColor = themeVars.primaryTextColor;
    erTheme.fontFamily = style.fontFamily;
    erTheme.fontSize = QString::number(style.fontSize) + QStringLiteral("px");
    er::ErScene scene = er::buildErScene(input, placement, std::move(style),
                                         erStyleDefs, erTheme);
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const er::ErScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& erDiagramAdapter() {
  static const ErDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
