#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/gitgraph/GitGraphDiagram.h"
#include "mermaid/gitgraph/GitGraphScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

#include <cmath>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue gitGraphValue(const QJsonObject& object, const char* key,
                         const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() ? fallback : value;
}

qreal finiteNumber(const QJsonValue& value, qreal fallback) {
  const qreal number = jsNumberValue(value);
  return std::isfinite(number) ? number : fallback;
}

struct GitGraphDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("gitGraph")}; }
  QString cssClass() const override { return QStringLiteral("gitGraph"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeId themeId = themeIdFromName(effectiveTheme);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeId, themeOverrides(pre.config));

    const QJsonObject family =
        pre.config.value(QStringLiteral("gitGraph")).toObject();
    gitgraph::GitGraphParseConfig parseConfig;
    const QJsonValue mainName =
        gitGraphValue(family, "mainBranchName", QStringLiteral("main"));
    parseConfig.mainBranchName = mainName.isString()
                                     ? mainName.toString()
                                     : QStringLiteral("main");
    parseConfig.mainBranchOrder = finiteNumber(
        gitGraphValue(family, "mainBranchOrder", 0.0), 0.0);

    gitgraph::GitGraphData data =
        gitgraph::GitGraphDiagram::parse(pre.code, parseConfig);
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);

    gitgraph::GitGraphConfig config;
    config.useMaxWidth = truthyConfigValue(
        gitGraphValue(family, "useMaxWidth", true));
    config.titleTopMargin = finiteNumber(
        gitGraphValue(family, "titleTopMargin", 25.0), 25.0);
    config.diagramPadding = finiteNumber(
        gitGraphValue(family, "diagramPadding", 8.0), 8.0);
    config.showCommitLabel = truthyConfigValue(
        gitGraphValue(family, "showCommitLabel", true));
    config.showBranches = truthyConfigValue(
        gitGraphValue(family, "showBranches", true));
    config.rotateCommitLabel = truthyConfigValue(
        gitGraphValue(family, "rotateCommitLabel", true));
    config.parallelCommits = truthyConfigValue(
        gitGraphValue(family, "parallelCommits", false));

    gitgraph::GitGraphSceneStyle style;
    style.themeName = effectiveTheme;
    const QJsonValue look = pre.config.value(QStringLiteral("look"));
    style.look = look.isString() ? look.toString() : QStringLiteral("classic");
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(style.fontFamily, 16.0);
    style.fontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.lineColor = themeVars.lineColor;
    style.commitLineColor = themeVars.commitLineColor;
    const QJsonObject rawThemeVariables =
        pre.config.value(QStringLiteral("themeVariables")).toObject();
    if (rawThemeVariables.contains(QStringLiteral("commitLineColor")))
      style.commitLineColor = flowtheme::resolveFlowTheme(themeId, {}).commitLineColor;
    style.nodeBorder = themeVars.nodeBorder;
    style.mainBkg = themeVars.mainBkg;
    style.primaryColor = themeVars.primaryColor;
    style.commitLabelColor = themeVars.commitLabelColor;
    style.commitLabelBackground = themeVars.commitLabelBackground;
    style.commitLabelFontSize = cssFontSizePx(
        themeVars.commitLabelFontSize, rootContext);
    style.tagLabelColor = themeVars.tagLabelColor;
    style.tagLabelBackground = themeVars.tagLabelBackground;
    style.tagLabelBorder = themeVars.tagLabelBorder;
    style.tagLabelFontSize = cssFontSizePx(
        themeVars.tagLabelFontSize, rootContext);
    style.strokeWidth = themeVars.strokeWidth;
    style.useGradient = themeVars.useGradient;
    style.gradientStart = themeVars.gradientStart;
    style.gradientStop = themeVars.gradientStop;
    for (const QString& value : themeVars.git) style.gitColors.push_back(value);
    for (const QString& value : themeVars.gitInv)
      style.gitInvColors.push_back(value);
    for (const QString& value : themeVars.gitBranchLabel)
      style.branchLabelColors.push_back(value);
    for (const QString& value : themeVars.borderColorArray)
      style.borderColors.push_back(value);

    gitgraph::GitGraphScene scene = gitgraph::buildGitGraphScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.textColor, scene.style.fontFamily, 16.0);
    // GitGraph draws the visible title in its own coordinate system.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene =
        std::make_shared<const gitgraph::GitGraphScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& gitGraphDiagramAdapter() {
  static const GitGraphDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
