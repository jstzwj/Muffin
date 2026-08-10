#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/treeview/TreeViewDiagram.h"
#include "mermaid/treeview/TreeViewScene.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue treeViewScalar(const QJsonObject& object, const char* key,
                          const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

QString cssValue(const QJsonObject& object, const char* key,
                 const QString& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isString()) return value.toString();
  if (value.isDouble()) return jsNumberToString(value.toDouble());
  if (value.isBool())
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  return fallback;
}

struct TreeViewDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("treeView")}; }
  QString cssClass() const override { return QStringLiteral("treeView"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw =
        pre.config.value(QStringLiteral("treeView")).toObject();
    treeview::TreeViewConfig config;
    config.useMaxWidth = treeViewScalar(raw, "useMaxWidth", true);
    config.rowIndent = treeViewScalar(raw, "rowIndent", 10.0);
    config.paddingX = treeViewScalar(raw, "paddingX", 5.0);
    config.paddingY = treeViewScalar(raw, "paddingY", 5.0);
    config.lineThickness = treeViewScalar(raw, "lineThickness", 1.0);
    config.showIcons = treeViewScalar(raw, "showIcons", false);
    config.defaultIconPack = raw.value(QStringLiteral("defaultIconPack")).toString();
    config.filenameIcons = raw.value(QStringLiteral("filenameIcons")).toObject();
    config.extensionIcons = raw.value(QStringLiteral("extensionIcons")).toObject();

    const treeview::TreeViewData data =
        treeview::TreeViewDiagram::parse(pre.code);
    const QJsonObject themeVariables =
        pre.config.value(QStringLiteral("themeVariables")).toObject();
    const QJsonObject rawStyle =
        themeVariables.value(QStringLiteral("treeView")).toObject();
    treeview::TreeViewSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(style.fontFamily, 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.rootTextColor = themeVars.textColor;
    style.labelFontSize =
        cssValue(rawStyle, "labelFontSize", QStringLiteral("16px"));
    style.labelColor =
        cssValue(rawStyle, "labelColor", QStringLiteral("black"));
    style.lineColor =
        cssValue(rawStyle, "lineColor", QStringLiteral("black"));
    // Mermaid's source-entry config sanitizer admits only the three generic
    // style keys above. The remaining TreeView CSS variables are initialize-
    // only and therefore retain renderer defaults for source/frontmatter.

    treeview::TreeViewScene scene = treeview::buildTreeViewScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        scene.style.labelColor, scene.style.fontFamily,
        scene.nodes.isEmpty() ? scene.style.rootFontSize
                              : scene.nodes.front().label.fontSize);
    // The renderer never consumes getDiagramTitle(), and frontmatter title is
    // likewise absent from the SVG. accTitle/accDescr still drive SVG ARIA.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize =
        QSize(qRound(scene.totalWidth), qRound(scene.totalHeight));
    entry.scene =
        std::make_shared<const treeview::TreeViewScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& treeViewDiagramAdapter() {
  static const TreeViewDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
