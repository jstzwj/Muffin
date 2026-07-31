#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/state/StateDiagram.h"
#include "mermaid/state/StateLayout.h"
#include "mermaid/state/StateScene.h"
#include "mermaid/theme/FlowTheme.h"

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
      const QJsonObject stateConfig =
          pre.config.value(QStringLiteral("state")).toObject();
      const state::StateLayoutInput input =
          state::buildStateLayoutInput(diagram.data(), look);
      state::StateSceneStyle style;
      style.stateFill = themeVars.mainBkg;
      style.stateStroke = themeVars.border1;
      style.textColor = themeVars.primaryTextColor;
      style.transitionColor = themeVars.lineColor;
      style.edgeLabelFill = themeVars.mainBkg;
      style.compositeFill = themeVars.clusterBkg;
      style.compositeStroke = themeVars.clusterBorder;
      style.fontFamily = MermaidFontRegistry::cssFamilyStack();
      style.fontSize = pixelValue(themeVars.fontSize, 16.0);
      style.lineHeight = style.fontSize * 1.5;
      style.strokeWidth = themeVars.strokeWidth;
      if (configuredTheme.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        style.noteFill = QStringLiteral("#474949");
        style.noteStroke = QStringLiteral("#2f2f2f");
        style.noteTextColor = QStringLiteral("#ffffff");
      }
      const state::StateLayoutMeasurements measurements = state::measureStateLayoutInput(
          input, style.fontFamily, style.fontSize);
      const state::StatePlacementResult placement =
          state::layoutStateDiagramDagre(
              input, measurements,
              configNumber(stateConfig, QStringLiteral("nodeSpacing"), 50.0),
              configNumber(stateConfig, QStringLiteral("rankSpacing"), 50.0));
      MermaidRenderMetadata metadata = renderMetadata(
          pre, type, {}, diagram.data().accTitle,
          diagram.data().accDescription, style.textColor, style.fontFamily,
          18.0, configNumber(stateConfig, QStringLiteral("titleTopMargin"),
                             25.0), 8.0);
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
          input, placement, std::move(style), stateStyleDefs, stateTheme);
      scene.handDrawn = look.compare(
          QStringLiteral("handDrawn"), Qt::CaseInsensitive) == 0;
      scene.handDrawnSeed = static_cast<quint32>(
          std::max(0.0, configNumber(pre.config, QStringLiteral("handDrawnSeed"), 0.0)));
      MermaidRenderEntry entry;
      entry.status = MermaidRenderStatus::Ready;
      entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
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
