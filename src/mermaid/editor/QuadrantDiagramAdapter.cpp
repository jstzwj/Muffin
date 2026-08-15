#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/quadrant/QuadrantDiagram.h"
#include "mermaid/quadrant/QuadrantScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>
#include <QString>

#include <memory>

namespace muffin::mermaid::editor {
namespace {

struct QuadrantDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("quadrantChart")}; }
  QString cssClass() const override { return QStringLiteral("quadrantChart"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    quadrant::QuadrantData data = quadrant::QuadrantDiagram::parse(pre.code);
    // Effective title: the in-source `quadrantChart title` wins; otherwise the
    // frontmatter title is the diagram title. Resolved BEFORE buildQuadrantScene
    // so titleSpace is reserved and the in-scene title is placed correctly.
    if (!data.hasTitleDirective && !pre.title.isEmpty()) data.title = pre.title;
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    quadrant::QuadrantSceneStyle style;
    // Consume the fully-resolved quadrant themeVariables (FlowTheme derived every
    // fill / text fill / point fill / axis+title text / border per the active
    // theme, including the upstream-invalid quadrantPointFill "hsl(..., NaN%)"
    // string emitted verbatim). No default/dark special-casing remains; the
    // struct defaults below are dead safety fallbacks.
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    style.inheritedFontSize = cssFontSizePx(
        themeVars.fontSize, pieCssLengthContext(style.fontFamily, 16.0));
    style.inheritedColor = themeVars.textColor;
    style.quadrant1Fill = themeVars.quadrant[0];
    style.quadrant2Fill = themeVars.quadrant[1];
    style.quadrant3Fill = themeVars.quadrant[2];
    style.quadrant4Fill = themeVars.quadrant[3];
    style.quadrant1TextFill = themeVars.quadrantText[0];
    style.quadrant2TextFill = themeVars.quadrantText[1];
    style.quadrant3TextFill = themeVars.quadrantText[2];
    style.quadrant4TextFill = themeVars.quadrantText[3];
    style.quadrantPointFill = themeVars.quadrantPointFill;
    style.quadrantPointTextFill = themeVars.quadrantPointTextFill;
    style.quadrantXAxisTextFill = themeVars.quadrantXAxisTextFill;
    style.quadrantYAxisTextFill = themeVars.quadrantYAxisTextFill;
    style.quadrantInternalBorderStrokeFill = themeVars.quadrantInternalBorderStrokeFill;
    style.quadrantExternalBorderStrokeFill = themeVars.quadrantExternalBorderStrokeFill;
    style.quadrantTitleFill = themeVars.quadrantTitleFill;

    const QJsonObject qcfg = pre.config.value(QStringLiteral("quadrantChart")).toObject();
    quadrant::QuadrantScene scene = quadrant::buildQuadrantScene(data, qcfg, std::move(style));

    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      QVector<ElementInput> elements;
      ElementStyle root;
      root.fill = scene.style.inheritedColor;
      root.stroke = QStringLiteral("none");
      root.strokeWidth = QStringLiteral("1px");
      root.color = scene.style.inheritedColor;
      root.fontFamily = scene.style.fontFamily;
      root.fontSize = QString::number(scene.style.inheritedFontSize) +
                      QStringLiteral("px");
      root.fontWeight = QStringLiteral("400");
      elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                       QStringLiteral("diagram-root"),
                       {QStringLiteral("quadrantChart")}, {}, root, {}});
      elements.append({QStringLiteral("main"), QStringLiteral("svg"),
                       QStringLiteral("g"), {}, {QStringLiteral("main")}, {},
                       root, {}});
      for (const QString& group : {QStringLiteral("quadrants"),
                                   QStringLiteral("border"),
                                   QStringLiteral("data-points"),
                                   QStringLiteral("labels"),
                                   QStringLiteral("title")})
        elements.append({group, QStringLiteral("main"), QStringLiteral("g"),
                         {}, {group}, {}, root, {}});

      for (qsizetype i = 0; i < scene.quadrants.size(); ++i) {
        const auto& q = scene.quadrants.at(i);
        const QString group = QStringLiteral("quadrant-%1").arg(i);
        ElementStyle groupFallback = root;
        elements.append({group, QStringLiteral("quadrants"), QStringLiteral("g"),
                         {}, {QStringLiteral("quadrant")}, {}, groupFallback, {}});
        ElementStyle rect = root;
        rect.fill = q.fill;
        elements.append({group + QStringLiteral("-rect"), group,
                         QStringLiteral("rect"), {}, {}, {}, rect, {}});
        ElementStyle text = root;
        text.fill = q.textFill;
        text.fontSize = QString::number(q.textFontSize) + QStringLiteral("px");
        elements.append({group + QStringLiteral("-text"), group,
                         QStringLiteral("text"), {}, {}, {}, text, {}});
      }
      for (qsizetype i = 0; i < scene.borders.size(); ++i) {
        const auto& b = scene.borders.at(i);
        ElementStyle line = root;
        line.stroke = b.strokeFill;
        line.strokeWidth = QString::number(b.strokeWidth) + QStringLiteral("px");
        elements.append({QStringLiteral("border-%1").arg(i),
                         QStringLiteral("border"), QStringLiteral("line"), {}, {}, {},
                         line, {}});
      }
      for (qsizetype i = 0; i < scene.axisLabels.size(); ++i) {
        const auto& a = scene.axisLabels.at(i);
        const QString group = QStringLiteral("label-%1").arg(i);
        elements.append({group, QStringLiteral("labels"), QStringLiteral("g"), {},
                         {QStringLiteral("label")}, {}, root, {}});
        ElementStyle text = root;
        text.fill = a.fill;
        text.fontSize = QString::number(a.fontSize) + QStringLiteral("px");
        elements.append({group + QStringLiteral("-text"), group,
                         QStringLiteral("text"), {}, {}, {}, text, {}});
      }
      for (qsizetype i = 0; i < scene.points.size(); ++i) {
        const auto& p = scene.points.at(i);
        const QString group = QStringLiteral("point-%1").arg(i);
        elements.append({group, QStringLiteral("data-points"), QStringLiteral("g"), {},
                         {QStringLiteral("data-point")}, {}, root, {}});
        ElementStyle circle = root;
        circle.fill = p.fill;
        circle.stroke = p.stroke;
        circle.strokeWidth = QString::number(p.strokeWidth) + QStringLiteral("px");
        elements.append({group + QStringLiteral("-circle"), group,
                         QStringLiteral("circle"), {}, {}, {}, circle, {}});
        ElementStyle text = root;
        text.fill = p.textFill;
        text.fontSize = QString::number(p.textFontSize) + QStringLiteral("px");
        elements.append({group + QStringLiteral("-text"), group,
                         QStringLiteral("text"), {}, {}, {}, text, {}});
      }
      if (!scene.titleText.isEmpty()) {
        ElementStyle title = root;
        title.fill = scene.titleFill;
        title.fontSize = QString::number(scene.titleFontSizeCfg) + QStringLiteral("px");
        elements.append({QStringLiteral("title-text"), QStringLiteral("title"),
                         QStringLiteral("text"), {}, {}, {}, title, {}});
      }

      const auto css = csscascade::resolveElements(themeCss, elements);
      const CssLengthContext rootContext = pieCssLengthContext(
          firstFontFamily(scene.style.fontFamily), scene.style.inheritedFontSize);
      const auto applyText = [&](const ElementStyle& computed, QString& fill,
                                 QString& family, qreal& size,
                                 QFont::Weight& weight, qreal& opacity,
                                 bool& visible) {
        fill = computed.fill;
        family = computed.fontFamily;
        size = cssFontSizePx(computed.fontSize, rootContext);
        weight = cssFontWeightToQt(QJsonValue(computed.fontWeight), QFont::Normal);
        opacity = computed.effectiveOpacity;
        visible = computed.displayed();
      };
      for (qsizetype i = 0; i < scene.quadrants.size(); ++i) {
        auto& q = scene.quadrants[i];
        const QString key = QStringLiteral("quadrant-%1").arg(i);
        const auto shape = css.value(key + QStringLiteral("-rect"));
        q.fill = shape.fill;
        q.shapeOpacity = shape.effectiveOpacity;
        q.shapeVisible = shape.displayed();
        applyText(css.value(key + QStringLiteral("-text")), q.textFill,
                  q.textFontFamily, q.textFontSize, q.textFontWeight,
                  q.textOpacity, q.textVisible);
      }
      for (qsizetype i = 0; i < scene.borders.size(); ++i) {
        auto& b = scene.borders[i];
        const auto line = css.value(QStringLiteral("border-%1").arg(i));
        b.strokeFill = line.stroke;
        b.strokeWidth = cssStrokeWidthPx(line.strokeWidth, rootContext,
                                         std::hypot(scene.bounds.width(), scene.bounds.height()) /
                                             std::sqrt(2.0));
        b.opacity = line.effectiveOpacity;
        b.visible = line.displayed();
      }
      for (qsizetype i = 0; i < scene.axisLabels.size(); ++i) {
        auto& a = scene.axisLabels[i];
        applyText(css.value(QStringLiteral("label-%1-text").arg(i)), a.fill,
                  a.fontFamily, a.fontSize, a.fontWeight, a.opacity, a.visible);
      }
      for (qsizetype i = 0; i < scene.points.size(); ++i) {
        auto& p = scene.points[i];
        const QString key = QStringLiteral("point-%1").arg(i);
        const auto shape = css.value(key + QStringLiteral("-circle"));
        p.fill = shape.fill;
        p.stroke = shape.stroke;
        p.strokeWidth = cssStrokeWidthPx(
            shape.strokeWidth, rootContext,
            std::hypot(scene.bounds.width(), scene.bounds.height()) / std::sqrt(2.0));
        p.shapeOpacity = shape.effectiveOpacity;
        p.shapeVisible = shape.displayed();
        applyText(css.value(key + QStringLiteral("-text")), p.textFill,
                  p.textFontFamily, p.textFontSize, p.textFontWeight,
                  p.textOpacity, p.textVisible);
      }
      if (!scene.titleText.isEmpty())
        applyText(css.value(QStringLiteral("title-text")), scene.titleFill,
                  scene.titleFontFamily, scene.titleFontSizeCfg,
                  scene.titleFontWeight, scene.titleOpacity, scene.titleVisible);
    }

    // The quadrant title is drawn INSIDE the viewBox (mermaid places it at
    // y=titlePadding). Clear metadata.title so the shared image path does not add
    // a second title band (renderMetadata would otherwise pull in pre.title).
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, QString(), data.accTitle, data.accDescr,
                       scene.style.quadrantTitleFill, scene.style.fontFamily, 20.0, 10.0, 0.0);
    metadata.title = QString();
    const QJsonValue rawUseMaxWidth = qcfg.value(QStringLiteral("useMaxWidth"));
    metadata.svgUseMaxWidth = rawUseMaxWidth.isUndefined() || rawUseMaxWidth.isNull()
                                  ? true
                                  : truthyConfigValue(rawUseMaxWidth);
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const quadrant::QuadrantScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& quadrantDiagramAdapter() {
  static const QuadrantDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
