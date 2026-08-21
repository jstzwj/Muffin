#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/info/InfoDiagram.h"
#include "mermaid/info/InfoScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

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
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      csscascade::ElementStyle rootFallback;
      rootFallback.fill = themeVars.textColor;
      rootFallback.color = themeVars.textColor;
      rootFallback.fontFamily = themeVars.fontFamily;
      rootFallback.fontSize = themeVars.fontSize;
      rootFallback.fontWeight = QStringLiteral("400");
      csscascade::ElementStyle textFallback = rootFallback;
      textFallback.fontSize = QStringLiteral("32px");
      const auto projected = csscascade::resolveElements(themeCss, {
          {QStringLiteral("svg"), QString(), QStringLiteral("svg"),
           QStringLiteral("diagram-root"), {QStringLiteral("info")}, {},
           rootFallback, QString()},
          {QStringLiteral("style"), QStringLiteral("svg"),
           QStringLiteral("style"), QString(), {}, {}, rootFallback,
           QString()},
          {QStringLiteral("scaffold"), QStringLiteral("svg"),
           QStringLiteral("g"), QString(), {}, {}, rootFallback, QString()},
          {QStringLiteral("group"), QStringLiteral("svg"),
           QStringLiteral("g"), QString(), {}, {}, rootFallback, QString()},
          {QStringLiteral("version"), QStringLiteral("group"),
           QStringLiteral("text"), QString(), {QStringLiteral("version")}, {},
           textFallback, QString()},
      });
      const csscascade::ElementStyle computed =
          projected.value(QStringLiteral("version"), textFallback);
      style.fontFamily = computed.fontFamily;
      const CssLengthContext rootContext =
          pieCssLengthContext(firstFontFamily(style.fontFamily), 16.0);
      style.fontSize = cssFontSizePx(computed.fontSize, rootContext);
      style.fontWeight =
          cssFontWeightToQt(QJsonValue(computed.fontWeight), QFont::Normal);
      style.textColor = computed.fill;
      style.opacity = computed.effectiveOpacity;
      style.textVisible = computed.displayed();
    }
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
