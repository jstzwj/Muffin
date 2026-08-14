#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/classdiagram/ClassDiagram.h"
#include "mermaid/classdiagram/ClassLayout.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QVector>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

// classDiagram behind the Diagram contract. Body is the former renderSource()
// class branch, verbatim.
struct ClassDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("class"), QStringLiteral("classDiagram")};
  }
  QString cssClass() const override { return QStringLiteral("classDiagram"); }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
      const classdiagram::ClassDiagram diagram =
          classdiagram::ClassDiagram::parse(pre.code);
      const QString configuredTheme = themeFromConfig(pre.config);
      const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
          themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
          themeOverrides(pre.config));
      const QJsonObject classConfig = pre.config.value(QStringLiteral("class")).toObject();
      classdiagram::ClassLayoutOptions options;
      options.padding = configNumber(classConfig, QStringLiteral("padding"), 12.0);
      options.hierarchicalNamespaces =
          classConfig.value(QStringLiteral("hierarchicalNamespaces")).toBool(true);
      options.hideEmptyMembersBox =
          classConfig.value(QStringLiteral("hideEmptyMembersBox")).toBool(false);
      options.htmlLabels = pre.config.value(QStringLiteral("htmlLabels")).toBool(true);
      options.look = pre.config.value(QStringLiteral("look")).toString(QStringLiteral("classic"));
      const bool handDrawn = options.look == QLatin1String("handDrawn");
      const quint32 handDrawnSeed = static_cast<quint32>(
          std::max(0.0, configNumber(
              pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
      const classdiagram::ClassLayoutInput input =
          classdiagram::buildClassLayoutInput(diagram.data(), options);
      classdiagram::ClassLabelMeasureOptions measureOptions;
      measureOptions.fontFamily = themeVars.fontFamily;
      measureOptions.fontPixelSize = pixelValue(themeVars.fontSize, 16.0);
      measureOptions.lineHeight = measureOptions.fontPixelSize * 1.5;
      measureOptions.htmlLabels = options.htmlLabels;
      csscascade::ElementStyle rootFallback;
      rootFallback.fill = themeVars.textColor;
      rootFallback.stroke = QStringLiteral("none");
      rootFallback.strokeWidth = QStringLiteral("1px");
      rootFallback.color = QStringLiteral("black");
      rootFallback.fontFamily = measureOptions.fontFamily;
      rootFallback.fontSize = QString::number(measureOptions.fontPixelSize) +
                              QStringLiteral("px");
      rootFallback.fontWeight = QStringLiteral("400");
      csscascade::ElementStyle labelFallback = rootFallback;
      labelFallback.color = themeVars.primaryTextColor;
      labelFallback.fontWeight = QStringLiteral("700");
      csscascade::ElementStyle labelStyle = labelFallback;
      const QString themeCss =
          pre.config.value(QStringLiteral("themeCSS")).toString();
      if (!themeCss.trimmed().isEmpty()) {
        const auto css = csscascade::resolveElements(themeCss, {
          {QStringLiteral("svg"), {}, QStringLiteral("svg"),
           QStringLiteral("diagram-root"), {QStringLiteral("classDiagram")}, {},
           rootFallback, {}},
          {QStringLiteral("root"), QStringLiteral("svg"), QStringLiteral("g"),
           {}, {QStringLiteral("root")}, {}, rootFallback, {}},
          {QStringLiteral("node"), QStringLiteral("root"), QStringLiteral("g"),
           {}, {QStringLiteral("node")}, {}, rootFallback, {}},
          {QStringLiteral("label"), QStringLiteral("node"), QStringLiteral("span"),
           {}, {QStringLiteral("nodeLabel")}, {}, labelFallback, {}}
        });
        labelStyle = css.value(QStringLiteral("label"), labelFallback);
        measureOptions.fontFamily = labelStyle.fontFamily;
        measureOptions.fontPixelSize = cssFontSizePx(labelStyle.fontSize, {});
        measureOptions.lineHeight = measureOptions.fontPixelSize * 1.5;
      }
      const classdiagram::ClassLayoutMeasurements labelMeasurements =
          classdiagram::measureClassLayoutLabels(input, measureOptions);
      const QVector<classdiagram::ClassBoxGeometry> boxes =
          classdiagram::layoutClassBoxes(input, labelMeasurements, options);
      const classdiagram::ClassDagreMeasurements dagreMeasurements =
          classdiagram::measureClassDagreInput(
              input, boxes, measureOptions, handDrawn, handDrawnSeed);
      const classdiagram::ClassPlacementResult placement =
          classdiagram::layoutClassDiagramDagre(
              input, dagreMeasurements, handDrawn, handDrawnSeed);
      classdiagram::ClassSceneStyle style;
      style.classFill = themeVars.mainBkg;
      style.classStroke = themeVars.border1;
      style.textColor = labelStyle.color;
      style.lineColor = themeVars.lineColor;
      style.edgeLabelFill = themeVars.mainBkg;
      style.clusterFill = themeVars.secondaryColor;
      style.clusterStroke = themeVars.border2;
      style.titleColor = themeVars.titleColor;
      style.strokeWidth = themeVars.strokeWidth;
      if (configuredTheme.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        style.noteFill = QStringLiteral("#474949");
        style.noteStroke = QStringLiteral("#2f2f2f");
        style.noteTextColor = color::invert(themeVars.secondaryColor);
      }
      style.fontFamily = measureOptions.fontFamily;
      style.fontSize = measureOptions.fontPixelSize;
      style.lineHeight = measureOptions.lineHeight;
      MermaidRenderMetadata metadata = renderMetadata(
          pre, type, diagram.data().title, diagram.data().accTitle,
          diagram.data().accDescription, style.textColor, style.fontFamily,
          18.0, configNumber(classConfig, QStringLiteral("titleTopMargin"),
                             25.0), 8.0);
      QVector<style::ClassDef> classStyleDefs;
      for (auto it = diagram.data().classDefs.constBegin();
           it != diagram.data().classDefs.constEnd(); ++it)
        classStyleDefs.append({it.key(), it.value()});
      style::ThemeDefaults classTheme;
      classTheme.mainBkg = themeVars.mainBkg;
      classTheme.nodeBorder = themeVars.border1;
      classTheme.lineColor = themeVars.lineColor;
      classTheme.strokeWidth = themeVars.strokeWidth;
      classTheme.textColor = themeVars.primaryTextColor;
      classTheme.fontFamily = style.fontFamily;
      classTheme.fontSize = QString::number(style.fontSize) + QStringLiteral("px");
      classdiagram::ClassScene scene = classdiagram::buildClassScene(
          input, boxes, labelMeasurements, placement, std::move(style),
          classStyleDefs, classTheme, handDrawn, handDrawnSeed);
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      entry.naturalSize = QSize(qRound(scene.bounds.width()),
                                qRound(scene.bounds.height()));
      entry.scene = std::make_shared<const classdiagram::ClassScene>(std::move(scene));
      finalizeReadyEntry(entry, std::move(metadata));
      return entry;
  }
};

}  // namespace

const Diagram& classDiagramAdapter() {
  static const ClassDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
