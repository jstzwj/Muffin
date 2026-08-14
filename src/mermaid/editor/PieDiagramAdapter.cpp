#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/pie/PieDiagram.h"
#include "mermaid/pie/PieScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

qreal chromiumTextAdvance(const QString& text, const QString& family,
                          qreal pixelSize, const QString& weight) {
  flowchart::FlowLabelDocument document;
  document.text = text;
  document.baseWeight = cssFontWeightToQt(QJsonValue(weight), QFont::Normal);
  return flowchart::measureChromiumInlineLayoutWidth(
      document, family, pixelSize);
}

struct PieDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("pie")}; }
  QString cssClass() const override { return QStringLiteral("pieDiagram"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const pie::PieData data = pie::PieDiagram::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    // Resolve the 4 live pie config fields from pre.config.pie over the defaults.
    const QJsonObject pieConfig = pre.config.value(QStringLiteral("pie")).toObject();
    pie::PieConfig config;
    const auto jsConfigNumber = [&pieConfig](QLatin1String key, qreal fallback) {
      const QJsonValue value = pieConfig.value(key);
      return value.isUndefined() || value.isNull() ? fallback : qreal(jsNumberValue(value));
    };
    config.textPosition = jsConfigNumber(QLatin1String("textPosition"), 0.75);
    config.donutHole = jsConfigNumber(QLatin1String("donutHole"), 0.0);
    config.legendPosition =
        pieConfig.value(QStringLiteral("legendPosition")).toString(QStringLiteral("right"));
    const QJsonValue rawHighlight = pieConfig.value(QStringLiteral("highlightSlice"));
    config.highlightSliceIsString = rawHighlight.isUndefined() || rawHighlight.isNull() ||
                                    rawHighlight.isString();
    config.highlightSlice = rawHighlight.isString() ? rawHighlight.toString() : QString();
    const QJsonValue rawUseMaxWidth = pieConfig.value(QStringLiteral("useMaxWidth"));
    config.useMaxWidth = rawUseMaxWidth.isUndefined() || rawUseMaxWidth.isNull()
                             ? true
                             : truthyConfigValue(rawUseMaxWidth);

    pie::PieSceneStyle style;
    // Consume the fully-resolved pie themeVariables (FlowTheme already derived
    // pie1..pie12 + the scalars per the active theme, honoring THEME_COLOR_LIMIT
    // and any source-entry overrides). The palette cycles by GLOBAL section
    // index; an empty entry (dark pie12 at TCL<=12) is "no fill attribute" and is
    // preserved verbatim. The pie*TextColor keys are the family-correct source
    // (the model derives them from taskTextDarkColor / mainContrastColor).
    for (int i = 0; i < 12; ++i) style.palette.append(themeVars.pie[i]);
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    style.titleFontFamily = style.fontFamily;
    style.sectionFontFamily = style.fontFamily;
    style.legendFontFamily = style.fontFamily;
    style.inheritedColor = themeVars.textColor;
    // Resolve the SVG ROOT font-size (themeVariables.fontSize) against the
    // browser <html> root (16px) FIRST: em/% here are relative to 16. "2em"->32,
    // neo's "14px"->14, invalid/absent -> 16 (inherits the document root). The
    // pie text elements inherit this root, so it is the em basis AND the
    // invalid/bare/negative fallback for every per-element font-size below
    // (cssFontSizePx returns ctx.emPx for those). Probed: "2em"+pieTitleTextSize
    // "200%" -> 64; neo root 14 -> invalid/bare title sizes inherit 14, not 16.
    const CssLengthContext rootCtx = pieCssLengthContext(style.fontFamily, 16.0);
    const qreal rootFs = cssFontSizePx(themeVars.fontSize, rootCtx);
    const CssLengthContext lengthCtx = pieCssLengthContext(style.fontFamily, rootFs);
    style.outerStrokeColor = themeVars.pieOuterStrokeColor;
    style.sliceStrokeColor = themeVars.pieStrokeColor;
    // The outer-ring RADIUS uses parseFontSize's numeric prefix (upstream
    // pieDiagram:157). parseFontSize branches on the JSON type (number verbatim
    // vs string parseInt), so read the RAW override.
    const QJsonValue rawOsw =
        pre.config.value(QStringLiteral("themeVariables")).toObject().value(QStringLiteral("pieOuterStrokeWidth"));
    style.outerStrokeWidthGeom = parseFontSizeNumber(rawOsw, themeVars.pieOuterStrokeWidth);
    style.pieOpacity = cssOpacity(themeVars.pieOpacity);
    if (!themeVars.pieTitleTextColor.isEmpty()) style.titleColor = themeVars.pieTitleTextColor;
    if (!themeVars.pieSectionTextColor.isEmpty()) style.sectionTextColor = themeVars.pieSectionTextColor;
    if (!themeVars.pieLegendTextColor.isEmpty()) style.legendTextColor = themeVars.pieLegendTextColor;
    style.titleFontSize = cssFontSizePx(themeVars.pieTitleTextSize, lengthCtx);
    style.sectionFontSize = cssFontSizePx(themeVars.pieSectionTextSize, lengthCtx);
    style.legendFontSize = cssFontSizePx(themeVars.pieLegendTextSize, lengthCtx);
    QString sliceStrokeWidthCss = themeVars.pieStrokeWidth;
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    // Layout-width gates: visibility:hidden keeps the text box (upstream still
    // reads getComputedTextLength), only display:none drops it from the canvas.
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    bool legendHasBox = true;
    bool titleHasBox = true;
    if (themeCssActive) {
      csscascade::ElementStyle rootFallback;
      rootFallback.fill = style.inheritedColor;
      rootFallback.stroke = QStringLiteral("none");
      rootFallback.strokeWidth = QStringLiteral("1px");
      rootFallback.color = QStringLiteral("black");
      rootFallback.fontFamily = style.fontFamily;
      rootFallback.fontSize = QString::number(rootFs) + QStringLiteral("px");
      csscascade::ElementStyle sliceFallback = rootFallback;
      sliceFallback.fill = style.palette.value(0);
      sliceFallback.stroke = style.sliceStrokeColor;
      sliceFallback.strokeWidth = themeVars.pieStrokeWidth;
      sliceFallback.opacity = QString::number(style.pieOpacity);
      csscascade::ElementStyle sliceTextFallback = rootFallback;
      sliceTextFallback.fill = style.sectionTextColor;
      sliceTextFallback.fontSize = QString::number(style.sectionFontSize) +
                                   QStringLiteral("px");
      csscascade::ElementStyle titleTextFallback = rootFallback;
      titleTextFallback.fill = style.titleColor;
      titleTextFallback.fontSize = QString::number(style.titleFontSize) +
                                   QStringLiteral("px");
      csscascade::ElementStyle legendTextFallback = rootFallback;
      legendTextFallback.fill = style.legendTextColor;
      legendTextFallback.fontSize = QString::number(style.legendFontSize) +
                                    QStringLiteral("px");
      const auto css = csscascade::resolveElements(themeCss, {
        {QStringLiteral("svg"), {}, QStringLiteral("svg"),
         QStringLiteral("diagram-root"), {QStringLiteral("pie")}, {},
         rootFallback, {}},
        {QStringLiteral("root"), QStringLiteral("svg"), QStringLiteral("g"),
         {}, {QStringLiteral("root")}, {}, rootFallback, {}},
        {QStringLiteral("slicePath"), QStringLiteral("root"), QStringLiteral("path"),
         {}, {QStringLiteral("pieCircle")}, {}, sliceFallback, {}},
        {QStringLiteral("sliceText"), QStringLiteral("root"), QStringLiteral("text"),
         {}, {QStringLiteral("slice")}, {}, sliceTextFallback, {}},
        {QStringLiteral("titleText"), QStringLiteral("root"), QStringLiteral("text"),
         {}, {QStringLiteral("pieTitleText")}, {}, titleTextFallback, {}},
        {QStringLiteral("legendText"), QStringLiteral("root"), QStringLiteral("text"),
         {}, {}, {}, legendTextFallback, {}}
      });
      const auto sliceStyle = css.value(QStringLiteral("slicePath"), sliceFallback);
      const auto sliceTextStyle = css.value(QStringLiteral("sliceText"), sliceTextFallback);
      const auto titleTextStyle = css.value(QStringLiteral("titleText"), titleTextFallback);
      const auto legendTextStyle = css.value(QStringLiteral("legendText"), legendTextFallback);
      for (QString& paletteColor : style.palette) paletteColor = sliceStyle.fill;
      style.sliceStrokeColor = sliceStyle.stroke;
      sliceStrokeWidthCss = sliceStyle.strokeWidth;
      style.pieOpacity = cssOpacity(sliceStyle.opacity);
      style.sectionTextColor = sliceTextStyle.fill;
      style.sectionFontSize = cssFontSizePx(sliceTextStyle.fontSize, lengthCtx);
      style.sectionFontFamily = firstFontFamily(sliceTextStyle.fontFamily);
      style.sectionFontWeight = sliceTextStyle.fontWeight;
      style.sectionTextVisible = sliceTextStyle.displayed();
      style.sliceVisible = sliceTextStyle.hasBox();
      style.titleColor = titleTextStyle.fill;
      style.titleFontSize = cssFontSizePx(titleTextStyle.fontSize, lengthCtx);
      style.titleFontFamily = firstFontFamily(titleTextStyle.fontFamily);
      style.titleFontWeight = titleTextStyle.fontWeight;
      style.titleVisible = titleTextStyle.displayed();
      style.legendTextColor = legendTextStyle.fill;
      style.legendFontSize = cssFontSizePx(legendTextStyle.fontSize, lengthCtx);
      style.legendFontFamily = firstFontFamily(legendTextStyle.fontFamily);
      style.legendFontWeight = legendTextStyle.fontWeight;
      style.legendTextVisible = legendTextStyle.displayed();
      legendHasBox = legendTextStyle.hasBox();
      titleHasBox = titleTextStyle.hasBox();
    }
    // slice/outerStrokeWidth (paint) are resolved AFTER the canvas bounds are
    // known -- stroke-width % is relative to the SVG normalized diagonal.

    pie::PieScene scene = pie::buildPieScene(data, config, std::move(style));
    // Frontmatter title (`---\ntitle: X\n---`) is the diagram title when there is
    // no in-source `pie title`; the inline title wins when both are present
    // (mermaid's getDiagramTitle = last-set: frontmatter during preprocess, the
    // inline `title` token during parse). The pie renderer draws it in-scene.
    if (scene.title.isEmpty() && !pre.title.isEmpty()) scene.title = pre.title;

    // Measure legend text advance with the resolved font so the canvas width
    // (font-coupled) and the painted legend block agree. Mirrors mermaid's
    // chartAndLegendWidth = pieWidth + margin + rect + spacing + longestTextWidth.
    // A 0 legend font-size paints no legend text (font-size:0 -> invisible), so
    // skip measuring (avoids a setPixelSize(0) warning) and contribute 0 width.
    // visibility:hidden KEEPS the box (getComputedTextLength still reports the
    // advance), so the layout gate is display-only; display:none drops it.
    const bool legendBoxSuppressed = themeCssActive && !legendHasBox;
    qreal longest = scene.legends.isEmpty()
                        ? -std::numeric_limits<qreal>::infinity()
                        : 0.0;
    if (!legendBoxSuppressed && scene.style.legendFontSize > 0.0) {
      for (const pie::PieLegendEntry& e : scene.legends)
        longest = std::max(longest, chromiumTextAdvance(
            e.text, scene.style.legendFontFamily, scene.style.legendFontSize,
            scene.style.legendFontWeight));
    }
    scene.longestLegendWidth = longest;
    // Upstream switch default is "right": top/bottom stack vertically, center
    // overlays the legend without widening, and every other value uses a side
    // legend block.
    const QString& lpos = config.legendPosition;
    const bool legendHorizontal = lpos == QStringLiteral("top") || lpos == QStringLiteral("bottom");
    const bool legendCenter = lpos == QStringLiteral("center");
    if (legendHorizontal || legendCenter) {
      scene.totalWidth = scene.pieWidth + scene.margin;  // legend stacks above/below
    } else {
      // right (default), left, or unknown -> side legend block widens canvas.
      scene.totalWidth =
          scene.pieWidth + scene.margin + scene.legendRectSize + scene.legendSpacing + longest;
    }
    scene.totalHeight = legendHorizontal ? scene.height + scene.legends.size() * scene.legendHeight
                                         : scene.height;
    // Upstream title-driven SVG viewBox expansion (pieRenderer draw() lines
    // 280-286): the title <text> is centered at group-local x=0 (= pieWidth/2 =
    // 225 SVG px), and when its rendered width exceeds the chart extent the
    // viewBox grows. Upstream's titleWidth is getBoundingClientRect().width read
    // BEFORE the viewBox is set -- which (no viewBox scaling yet) is the title's
    // natural advance width in the resolved title font; measure it the same way
    // the legend text width is measured. The title paints nothing when it is
    // empty or its font-size is <= 0 (painter gate), so it then contributes 0.
    qreal titleWidth = 0.0;
    const bool titleBoxSuppressed = themeCssActive && !titleHasBox;
    if (!scene.title.isEmpty() && !titleBoxSuppressed &&
        scene.style.titleFontSize > 0.0) {
      titleWidth = chromiumTextAdvance(
          scene.title, scene.style.titleFontFamily, scene.style.titleFontSize,
          scene.style.titleFontWeight);
    }
    const qreal titleLeft = scene.pieWidth / 2.0 - titleWidth / 2.0;
    const qreal titleRight = scene.pieWidth / 2.0 + titleWidth / 2.0;
    const qreal viewBoxX = std::min(0.0, titleLeft);
    const qreal viewBoxRight = std::max(scene.totalWidth, titleRight);
    scene.bounds = QRectF(viewBoxX, 0.0, viewBoxRight - viewBoxX, scene.totalHeight);
    scene.titleWidth = titleWidth;  // reused by the painter to size the title rect

    // Resolve the stroke-width PAINT values now that the SVG viewport (bounds)
    // is known: stroke-width % is relative to the normalized diagonal
    // sqrt(w^2+h^2)/sqrt(2) of the SVG viewport (probed; deferred to paint, so
    // getComputedStyle leaves it as "%"). Non-% values ignore the diagonal.
    const qreal diagonal =
        std::sqrt(scene.bounds.width() * scene.bounds.width() +
                  scene.bounds.height() * scene.bounds.height()) /
        std::sqrt(2.0);
    scene.style.sliceStrokeWidth = cssStrokeWidthPx(
        sliceStrokeWidthCss, lengthCtx, diagonal);
    scene.style.outerStrokeWidth = cssStrokeWidthPx(themeVars.pieOuterStrokeWidth, lengthCtx, diagonal);

    // The pie title is part of the chart (mermaid draws pieTitleText INSIDE the
    // viewBox at y=25, not above it). Pass an empty diagramTitle so the image
    // path does not reserve title space or paint a second title; the painter
    // draws data.title in-scene. Force metadata.title empty too (renderMetadata
    // would otherwise pull in a frontmatter title and add a second title band).
    // Frontmatter-vs-inline title precedence is covered separately.
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, QString(), data.accTitle, data.accDescr,
                       scene.style.titleColor, scene.style.fontFamily, scene.style.titleFontSize,
                       25.0, 0.0);
    metadata.title = QString();
    metadata.svgUseMaxWidth = config.useMaxWidth;
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const pie::PieScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& pieDiagramAdapter() {
  static const PieDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
