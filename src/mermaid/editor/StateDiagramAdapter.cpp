#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QVector>

#include <algorithm>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

// stateDiagram behind the Diagram contract. Body is the former renderSource()
// state branch, verbatim.
struct StateDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("state"), QStringLiteral("stateDiagram")};
  }
  QString cssClass() const override { return QStringLiteral("stateDiagram"); }
  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
      const state::StateDiagram diagram = state::StateDiagram::parse(pre.code);
      const QString configuredTheme = themeFromConfig(pre.config);
      const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
          themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
          themeOverrides(pre.config));
      const QString look = pre.config.value(QStringLiteral("look"))
          .toString(QStringLiteral("classic"));
      const bool handDrawn = look == QLatin1String("handDrawn");
      const quint32 handDrawnSeed = static_cast<quint32>(
          std::max(0.0, configNumber(
              pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
      const QJsonObject stateConfig =
          pre.config.value(QStringLiteral("state")).toObject();
      const state::StateLayoutInput input =
          state::buildStateLayoutInput(diagram.data(), look);
      state::StateSceneStyle style;
      // `.node rect` rule: fill = stateBkg || mainBkg, stroke = stateBorder ||
      // nodeBorder (NOT border1 — redux-dark's border1 #ccc differs from its
      // stateBorder/nodeBorder #FFFFFF). stateBkg is derived to mainBkg by
      // every built-in theme's updateColors.
      style.stateFill = !themeVars.stateBkg.isEmpty() ? themeVars.stateBkg
                                                      : themeVars.mainBkg;
      style.stateStroke = !themeVars.stateBorder.isEmpty()
                              ? themeVars.stateBorder : themeVars.nodeBorder;
      style.textColor = themeVars.primaryTextColor;
      style.transitionColor = themeVars.lineColor;
      style.specialStateColor = themeVars.specialStateColor;
      style.endInnerFill = !themeVars.stateBorder.isEmpty()
                               ? themeVars.stateBorder : themeVars.nodeBorder;
      style.edgeLabelFill = themeVars.mainBkg;
      style.compositeFill = themeVars.compositeBackground;
      style.compositeAltFill = themeVars.altBackground;
      style.compositeTitleFill = themeVars.compositeTitleBackground;
      style.compositeStroke = themeVars.nodeBorder;
      style.fontFamily = MermaidFontRegistry::cssFamilyStack();
      style.fontSize = pixelValue(themeVars.fontSize, 16.0);
      style.lineHeight = style.fontSize * 1.5;
      style.strokeWidth = themeVars.strokeWidth;
      csscascade::ElementStyle rootFallback;
      rootFallback.fill = themeVars.textColor;
      rootFallback.stroke = QStringLiteral("none");
      rootFallback.strokeWidth = QStringLiteral("1px");
      rootFallback.color = QStringLiteral("black");
      rootFallback.fontFamily = style.fontFamily;
      rootFallback.fontSize = QString::number(style.fontSize) + QStringLiteral("px");
      csscascade::ElementStyle nodeFallback = rootFallback;
      nodeFallback.fill = style.stateFill;
      nodeFallback.stroke = style.stateStroke;
      nodeFallback.strokeWidth = QString::number(style.strokeWidth) + QStringLiteral("px");
      csscascade::ElementStyle labelFallback = rootFallback;
      labelFallback.color = style.textColor;
      const auto css = csscascade::resolveElements(
          pre.config.value(QStringLiteral("themeCSS")).toString(), {
            {QStringLiteral("svg"), {}, QStringLiteral("svg"),
             QStringLiteral("diagram-root"), {QStringLiteral("stateDiagram")}, {},
             rootFallback, {}},
            {QStringLiteral("root"), QStringLiteral("svg"), QStringLiteral("g"),
             {}, {QStringLiteral("root")}, {}, rootFallback, {}},
            {QStringLiteral("node"), QStringLiteral("root"), QStringLiteral("g"),
             {}, {QStringLiteral("node")}, {}, rootFallback, {}},
            {QStringLiteral("shape"), QStringLiteral("node"), QStringLiteral("rect"),
             {}, {}, {}, nodeFallback, {}},
            {QStringLiteral("label"), QStringLiteral("node"), QStringLiteral("span"),
             {}, {QStringLiteral("nodeLabel")}, {}, labelFallback, {}}
          });
      const auto nodeStyle = css.value(QStringLiteral("shape"), nodeFallback);
      const auto labelStyle = css.value(QStringLiteral("label"), labelFallback);
      const bool shapeHidden = !nodeStyle.hasBox();
      style.shapeVisible = !shapeHidden;
      style.stateFill = nodeStyle.fill;
      style.stateStroke = nodeStyle.stroke;
      style.strokeWidth = cssStrokeWidthPx(nodeStyle.strokeWidth, {}, 0.0);
      style.textColor = labelStyle.color;
      style.fontFamily = firstFontFamily(labelStyle.fontFamily);
      style.fontSize = cssFontSizePx(labelStyle.fontSize, {});
      style.lineHeight = style.fontSize * 1.5;
      if (configuredTheme.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        style.noteFill = QStringLiteral("#474949");
        style.noteStroke = QStringLiteral("#2f2f2f");
        style.noteTextColor = QStringLiteral("#ffffff");
      }
      const state::StateLayoutMeasurements measurements = state::measureStateLayoutInput(
          input, style.fontFamily, style.fontSize, handDrawn, handDrawnSeed,
          shapeHidden);
      const state::StatePlacementResult placement =
          state::layoutStateDiagramDagre(
              input, measurements,
              configNumber(stateConfig, QStringLiteral("nodeSpacing"), 50.0),
              configNumber(stateConfig, QStringLiteral("rankSpacing"), 50.0),
              handDrawn, handDrawnSeed);
      MermaidRenderMetadata metadata = renderMetadata(
          pre, type, {}, diagram.data().accTitle,
          diagram.data().accDescription, style.textColor, style.fontFamily,
          18.0, configNumber(stateConfig, QStringLiteral("titleTopMargin"),
                             25.0), 0.0);
      QVector<style::ClassDef> stateStyleDefs;
      for (const state::StateStyleClass& cls : diagram.data().styleClasses)
        stateStyleDefs.append({cls.id, cls.styles + cls.textStyles});
      style::ThemeDefaults stateTheme;
      stateTheme.mainBkg = themeVars.mainBkg;
      stateTheme.nodeBorder = themeVars.border1;
      stateTheme.lineColor = themeVars.lineColor;
      stateTheme.strokeWidth = themeVars.strokeWidth;
      stateTheme.textColor = themeVars.primaryTextColor;
      stateTheme.fontFamily = style.fontFamily;
      stateTheme.fontSize = QString::number(style.fontSize) + QStringLiteral("px");
      state::StateScene scene = state::buildStateScene(
          input, placement, std::move(style), stateStyleDefs, stateTheme,
          handDrawn, handDrawnSeed);
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      // naturalSize = the rounded client size (upstream's fractional viewBox
      // width is the svg's used CSS width; round, matching the Flowchart
      // convention — ceil inflated a 26.40625-wide diagram to 27).
      entry.naturalSize = QSize(qRound(scene.bounds.width()), qRound(scene.bounds.height()));
      entry.scene = std::make_shared<const state::StateScene>(std::move(scene));
      finalizeReadyEntry(entry, std::move(metadata));
      return entry;
  }
};

}  // namespace

const Diagram& stateDiagramAdapter() {
  static const StateDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
