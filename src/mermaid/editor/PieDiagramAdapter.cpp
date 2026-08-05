#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/pie/PieDiagram.h"
#include "mermaid/pie/PieScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QFont>
#include <QFontMetrics>
#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

// mermaid 11.16.0 pie section palette (theme pie1..pie12), captured live from a
// 13-slice render in each theme via scripts/probe_mermaid_pie_palette.mjs. The
// default-theme list reproduces the frozen pie-geometry.json fill values
// byte-for-byte; the dark-theme list leaves slot 12 unset (mermaid's dark theme
// only generates pie1..pie11), reproduced as an empty string => no fill.
QStringList defaultPiePalette() {
  return {
      QStringLiteral("#ECECFF"),
      QStringLiteral("#ffffde"),
      QStringLiteral("hsl(80, 100%, 56.2745098039%)"),
      QStringLiteral("hsl(240, 100%, 86.2745098039%)"),
      QStringLiteral("hsl(60, 100%, 63.5294117647%)"),
      QStringLiteral("hsl(80, 100%, 76.2745098039%)"),
      QStringLiteral("hsl(300, 100%, 76.2745098039%)"),
      QStringLiteral("hsl(180, 100%, 56.2745098039%)"),
      QStringLiteral("hsl(0, 100%, 56.2745098039%)"),
      QStringLiteral("hsl(300, 100%, 56.2745098039%)"),
      QStringLiteral("hsl(150, 100%, 56.2745098039%)"),
      QStringLiteral("hsl(0, 100%, 66.2745098039%)"),
  };
}
QStringList darkPiePalette() {
  return {
      QStringLiteral("#0b0000"), QStringLiteral("#4d1037"), QStringLiteral("#3f5258"),
      QStringLiteral("#4f2f1b"), QStringLiteral("#6e0a0a"), QStringLiteral("#3b0048"),
      QStringLiteral("#995a01"), QStringLiteral("#154706"), QStringLiteral("#161722"),
      QStringLiteral("#00296f"), QStringLiteral("#01629c"), QString(),
  };
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
    config.textPosition = configNumber(pieConfig, QStringLiteral("textPosition"), 0.75);
    config.donutHole = configNumber(pieConfig, QStringLiteral("donutHole"), 0.0);
    config.legendPosition =
        pieConfig.value(QStringLiteral("legendPosition")).toString(QStringLiteral("right"));
    config.highlightSlice = pieConfig.value(QStringLiteral("highlightSlice")).toString();
    config.useMaxWidth = pieConfig.value(QStringLiteral("useMaxWidth")).toBool(true);

    pie::PieSceneStyle style;
    style.palette = (effectiveTheme == QStringLiteral("dark")) ? darkPiePalette()
                                                                : defaultPiePalette();
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    style.titleColor = themeVars.titleColor.isEmpty() ? QStringLiteral("#333333")
                                                      : themeVars.titleColor;
    style.sectionTextColor = themeVars.primaryTextColor;
    style.legendTextColor = themeVars.primaryTextColor;

    pie::PieScene scene = pie::buildPieScene(data, config, std::move(style));
    // Frontmatter title (`---\ntitle: X\n---`) is the diagram title when there is
    // no in-source `pie title`; the inline title wins when both are present
    // (mermaid's getDiagramTitle = last-set: frontmatter during preprocess, the
    // inline `title` token during parse). The pie renderer draws it in-scene.
    if (scene.title.isEmpty() && !pre.title.isEmpty()) scene.title = pre.title;

    // Measure legend text advance with the resolved font so the canvas width
    // (font-coupled) and the painted legend block agree. Mirrors mermaid's
    // chartAndLegendWidth = pieWidth + margin + rect + spacing + longestTextWidth.
    QFont legendFont(scene.style.fontFamily);
    legendFont.setPixelSize(qRound(scene.style.legendFontSize));
    const QFontMetrics fm(legendFont);
    qreal longest = 0.0;
    for (const pie::PieLegendEntry& e : scene.legends)
      longest = std::max(longest, qreal(fm.horizontalAdvance(e.text)));
    scene.longestLegendWidth = longest;
    // Upstream switch default is "right": only top/bottom/center are non-right.
    const QString& lpos = config.legendPosition;
    const bool legendHorizontal = lpos == QStringLiteral("top") || lpos == QStringLiteral("bottom");
    const bool legendCenter = lpos == QStringLiteral("center");
    if (legendHorizontal) {
      scene.totalWidth = scene.pieWidth + scene.margin;  // legend stacks above/below
    } else {
      // right (default), left, center, or unknown -> side legend block widens canvas
      scene.totalWidth =
          scene.pieWidth + scene.margin + scene.legendRectSize + scene.legendSpacing + longest;
    }
    scene.totalHeight = legendHorizontal ? scene.height + scene.legends.size() * scene.legendHeight
                                         : scene.height;
    (void)legendCenter;
    scene.bounds = QRectF(0.0, 0.0, scene.totalWidth, scene.totalHeight);

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
