#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/erdiagram/ErDiagram.h"
#include "mermaid/erdiagram/ErLayout.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/erdiagram/ErStyles.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QSize>
#include <QString>
#include <QVector>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

// Whether a configuration value is there: absent and null both leave a key
// unset to Mermaid.
bool isSet(const QJsonValue& value) {
  return !value.isUndefined() && !value.isNull();
}

// erDiagram behind the Diagram contract.
struct ErDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("er")}; }
  QString cssClass() const override { return QStringLiteral("erDiagram"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const er::ErDiagram diagram = er::ErDiagram::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeId themeId = themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(themeId, themeOverrides(pre.config));
    const QJsonObject erConfig = pre.config.value(QStringLiteral("er")).toObject();
    const er::ErLayoutInput input = er::buildErLayoutInput(diagram.data());

    // erDB compiles every entity's classDefs and inline style; the stylesheet
    // cascade then reads them as the inline styles erBox writes.
    er::ErStyleInput styleInput;
    styleInput.theme = themeVars;
    styleInput.fontFamily = themeVars.fontFamily;
    const QString look = pre.config.value(QStringLiteral("look")).toString();
    styleInput.look = look.isEmpty() ? QStringLiteral("classic") : look;
    styleInput.themeCss = pre.config.value(QStringLiteral("themeCSS")).toString();
    for (auto it = diagram.data().classDefs.cbegin(); it != diagram.data().classDefs.cend(); ++it)
      styleInput.classDefs.append({it.key(), it.value()});
    styleInput.colorTheme = flowtheme::isColorTheme(themeId);
    styleInput.themeColorLimit = jsThemeColorLimit(pre.config).value_or(themeVars.themeColorLimit);
    styleInput.nodeStylesReachEveryPath = flowtheme::isReduxTheme(themeId);
    // The neo look strokes every path of a node with the theme's gradient
    // where it has one, and the shadow filter the redux themes name is
    // white over a theme whose name contains "dark".
    styleInput.useGradient = themeVars.useGradient;
    styleInput.gradientStart = themeVars.gradientStart;
    styleInput.gradientStop = themeVars.gradientStop;
    styleInput.darkTheme = flowtheme::isDarkTheme(themeId);
    // Under the handDrawn look rough.js draws the shapes from this seed;
    // Mermaid's default, 0, makes it draw at random.
    styleInput.handDrawnSeed =
        static_cast<quint32>(std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
    // Mermaid's `htmlLabels`, as its two readers take it. erBox reads the root
    // key raw: unset, it scales its paddings by 1.25, though createText then
    // draws an HTML label, as it does for a true key. A false key draws SVG
    // text. A relationship label reads that key too, and where it is unset,
    // the flowchart one Mermaid once shared with every diagram.
    const QJsonValue rootHtmlLabels = pre.config.value(QStringLiteral("htmlLabels"));
    const QJsonValue relationshipHtmlLabels =
        isSet(rootHtmlLabels)
            ? rootHtmlLabels
            : pre.config.value(QStringLiteral("flowchart")).toObject().value(QStringLiteral("htmlLabels"));
    styleInput.htmlLabels = !isSet(rootHtmlLabels) || truthyConfigValue(rootHtmlLabels);
    styleInput.relationshipHtmlLabels = !isSet(relationshipHtmlLabels) || truthyConfigValue(relationshipHtmlLabels);

    // The document's style: what paints its elements, and how its texts are
    // set, through its stylesheet, the classDefs and themeCSS.
    er::ErDocumentStyle document = er::resolveErDocumentStyle(input, styleInput);
    er::ErTextOptions text = document.text;
    text.scalePaddings = !isSet(rootHtmlLabels) || !truthyConfigValue(rootHtmlLabels);

    er::ErMeasureOptions measure;
    measure.siteStyle = document.text.table;
    measure.minEntityWidth = configNumber(erConfig, QStringLiteral("minEntityWidth"), 100.0);
    measure.diagramPadding = configNumber(erConfig, QStringLiteral("diagramPadding"), 20.0);
    measure.entityPadding = configNumber(erConfig, QStringLiteral("entityPadding"), 15.0);
    measure.wrappingWidth =
        configNumber(pre.config.value(QStringLiteral("flowchart")).toObject(), QStringLiteral("wrappingWidth"), 200.0);
    // The handDrawn look sizes an entity by the box of the shape rough.js
    // draws for it.
    for (auto it = document.rough.cbegin(); it != document.rough.cend(); ++it)
      measure.rough.insert(it.key(), it.value().table);
    const er::ErLayoutMeasurements measurements = er::measureErLayoutInput(input, text, measure);
    // erRenderer: `conf.nodeSpacing || 140`, `conf.rankSpacing || 80`.
    const auto spacing = [&](const char* key, qreal fallback) {
      const qreal value = configNumber(erConfig, QLatin1String(key), fallback);
      return value == 0.0 ? fallback : value;
    };
    const er::ErPlacementResult placement =
        er::layoutErDiagramDagre(input, measurements, spacing("nodeSpacing", 140.0), spacing("rankSpacing", 80.0));
    // The neo look dashes each line by the length of its path, which exists
    // only now.
    er::resolveErLineDashes(document, input, placement, styleInput);

    // What the stylesheet paints with where the document's style says no
    // more: the theme's own colors, with the strokes the look leaves to the
    // theme's stroke width.
    er::ErSceneStyle style;
    style.text = document.text.table;
    style.relationshipText = document.text.relationship;
    style.nodeFill = themeVars.mainBkg;
    style.nodeBorder = themeVars.nodeBorder;
    style.rowOddFill = themeVars.rowOdd;
    style.rowEvenFill = themeVars.rowEven;
    style.lineColor = themeVars.lineColor;
    style.neo = document.neo;
    style.gradient = document.gradient;
    style.handDrawn = document.handDrawn;
    style.handDrawnSeed = styleInput.handDrawnSeed;
    style.background = themeVars.background;
    style.rough = document.rough;
    if (document.neo)
      style.boxStrokeWidth = style.tableStrokeWidth = style.lineStrokeWidth = themeVars.strokeWidth;
    // `.labelBkg`: the color themes name a background, the others fade their
    // tertiary color to half, as every theme does under the neo look. Under
    // htmlLabels:false the label is an SVG text over `.edgeLabel .label rect`
    // instead.
    const QString labelBackground =
        flowtheme::isColorTheme(themeId) && !themeVars.erEdgeLabelBackground.isEmpty() && !document.neo
            ? themeVars.erEdgeLabelBackground
            : color::rgba(themeVars.tertiaryColor, 0.5);
    style.labelBackground = color::toQColor(labelBackground);
    style.svgLabelBackground = color::toQColor(themeVars.edgeLabelBackground);
    // `.marker`: a stroke of the line's color, 1px, or the theme's stroke
    // width under the neo look, and a white ring, which the neo look fills
    // with the background.
    style.marker.strokeWidth = document.neo ? themeVars.strokeWidth : 1.0;
    style.marker.stroke = themeVars.lineColor;
    style.marker.ringFill = document.neo ? themeVars.mainBkg : QStringLiteral("white");
    style.marker.ringStroke = themeVars.lineColor;
    style.htmlLabels = text.htmlLabels;
    style.relationshipHtmlLabels = text.relationshipHtmlLabels;
    style.scalePaddings = text.scalePaddings;
    er::ErScene scene = er::buildErScene(input, placement, measurements, std::move(style), document.paint);

    // An er diagram's title comes from its front matter alone; the grammar has
    // no title statement.
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), diagram.data().accTitle, diagram.data().accDescription, themeVars.primaryTextColor,
        scene.style.text.fontFamily, 18.0, configNumber(erConfig, QStringLiteral("titleTopMargin"), 25.0), 8.0);
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    // Upstream's viewBox is fractional, and the browser sizes the SVG element
    // as erRasterExtent says: the image is made from that extent.
    const QSize raster = er::erRasterExtent(scene.bounds, 8.0);
    scene.rasterBounds = QRectF(scene.bounds.topLeft(), QSizeF(raster.width() - 16, raster.height() - 16));
    entry.naturalSize = QSize(qRound(scene.rasterBounds.width()), qRound(scene.rasterBounds.height()));
    entry.scene = std::make_shared<const er::ErScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

} // namespace

const Diagram& erDiagramAdapter() {
  static const ErDiagramImpl impl;
  return impl;
}

} // namespace muffin::mermaid::editor
