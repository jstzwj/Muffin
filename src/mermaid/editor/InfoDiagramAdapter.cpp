#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/info/InfoDiagram.h"
#include "mermaid/info/InfoScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QSize>

#include <memory>

namespace muffin::mermaid::editor {
namespace {

struct InfoDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("info")}; }
  QString cssClass() const override { return QStringLiteral("info"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const info::InfoData data = info::InfoDiagram::parse(pre.code);
    Q_UNUSED(data);

    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars =
        flowtheme::resolveFlowTheme(
            themeIdFromName(configuredTheme.isEmpty() ? theme
                                                       : configuredTheme),
            themeOverrides(pre.config));
    info::InfoSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    style.textColor = themeVars.textColor;
    info::InfoScene scene = info::buildInfoScene(std::move(style));

    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), themeVars.textColor,
        themeVars.fontFamily, 32.0);
    // Info parses title/accessibility terminals into an AST but its diagram
    // parser intentionally discards that AST. The renderer therefore emits no
    // shared title/description and frontmatter is equally inert.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgEmitViewBox = false;
    metadata.svgUseMaxWidth = true;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(400, 150);
    entry.scene = std::make_shared<const info::InfoScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& infoDiagramAdapter() {
  static const InfoDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
