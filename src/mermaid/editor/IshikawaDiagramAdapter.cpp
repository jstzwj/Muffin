#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/ishikawa/IshikawaDiagram.h"
#include "mermaid/ishikawa/IshikawaScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue scalar(const QJsonObject& object, const char* key,
                  const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct IshikawaDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("ishikawa")}; }
  QString cssClass() const override { return QStringLiteral("ishikawa"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw =
        pre.config.value(QStringLiteral("ishikawa")).toObject();
    ishikawa::IshikawaConfig config;
    config.useMaxWidth = scalar(raw, "useMaxWidth", QJsonValue(true));
    config.diagramPadding = scalar(raw, "diagramPadding", QJsonValue(20.0));
    config.handDrawnSeed =
        scalar(pre.config, "handDrawnSeed", QJsonValue(0.0));

    ishikawa::IshikawaSceneStyle style;
    const QJsonValue look = pre.config.value(QStringLiteral("look"));
    style.look = look.isString() ? look.toString() : QStringLiteral("classic");
    style.fontFamily = themeVars.fontFamily;
    style.fontSize = cssFontSizePx(
        themeVars.fontSize,
        pieCssLengthContext(firstFontFamily(style.fontFamily), 16.0));
    style.lineColor = themeVars.lineColor;
    style.mainBkg = themeVars.mainBkg;
    style.textColor = themeVars.textColor;

    const ishikawa::IshikawaData data =
        ishikawa::IshikawaDiagram::parse(pre.code);
    ishikawa::IshikawaScene scene = ishikawa::buildIshikawaScene(
        data, std::move(config), std::move(style));

    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), scene.style.textColor,
        scene.style.fontFamily, scene.style.fontSize);
    // Ishikawa's root is drawn inside the fish head. Mermaid 11.16 does not
    // project it, frontmatter, or metadata-looking nodes into SVG title/ARIA.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene =
        std::make_shared<const ishikawa::IshikawaScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& ishikawaDiagramAdapter() {
  static const IshikawaDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
