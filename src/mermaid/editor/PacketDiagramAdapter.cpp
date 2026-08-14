#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/packet/PacketDiagram.h"
#include "mermaid/packet/PacketScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue packetScalar(const QJsonObject& object, const char* key,
                        const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct PacketDiagramImpl : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("packet"), QStringLiteral("packet-beta")};
  }
  QString cssClass() const override { return QStringLiteral("packet"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw =
        pre.config.value(QStringLiteral("packet")).toObject();
    packet::PacketConfig config;
    config.rowHeight = packetScalar(raw, "rowHeight", 32.0);
    config.bitWidth = packetScalar(raw, "bitWidth", 32.0);
    config.bitsPerRow = packetScalar(raw, "bitsPerRow", 32.0);
    config.showBits = packetScalar(raw, "showBits", true);
    config.paddingX = packetScalar(raw, "paddingX", 5.0);
    config.paddingY = packetScalar(raw, "paddingY", 5.0);
    config.useMaxWidth = packetScalar(raw, "useMaxWidth", true);

    packet::PacketData data =
        packet::PacketDiagram::parse(pre.code, config.bitsPerRow);
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);

    packet::PacketSceneStyle style;
    // Keep the complete CSS fallback list: Packet's painter maps it to
    // QFont::setFamilies(), matching the upstream SVG font-family cascade.
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        packet::packetCssLengthContext(style.fontFamily, 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.inheritedColor = themeVars.textColor;
    style.byteFontSize = themeVars.packet.byteFontSize;
    style.startByteColor = themeVars.packet.startByteColor;
    style.endByteColor = themeVars.packet.endByteColor;
    style.labelColor = themeVars.packet.labelColor;
    style.labelFontSize = themeVars.packet.labelFontSize;
    style.titleColor = themeVars.packet.titleColor;
    style.titleFontSize = themeVars.packet.titleFontSize;
    style.blockStrokeColor = themeVars.packet.blockStrokeColor;
    style.blockStrokeWidth = themeVars.packet.blockStrokeWidth;
    style.blockFillColor = themeVars.packet.blockFillColor;

    // Apply themeCSS via the cascade ONLY when it is present. Muffin preserves
    // the raw themeVariables paint declarations (none/inherit/empty) on the scene
    // for the painter to resolve at paint time (resolveSvgPaint with the
    // inherited fallback); the cascade resolves CSS-wide keywords against the
    // parent, which would destroy that preservation when there is no themeCSS
    // override. The style fields above are already seeded from themeVars, so with
    // no themeCSS they are left untouched. Mirrors the requirement adapter's
    // `if (!themeCss.trimmed().isEmpty())` gate. (probed vs mermaid 11.16.0)
    const QString themeCss = pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      csscascade::ElementStyle rootFallback;
      rootFallback.fill = style.inheritedColor;
      rootFallback.stroke = QStringLiteral("none");
      rootFallback.strokeWidth = QStringLiteral("1px");
      rootFallback.color = QStringLiteral("black");
      rootFallback.fontFamily = style.fontFamily;
      rootFallback.fontSize = QString::number(style.rootFontSize) +
                              QStringLiteral("px");
      csscascade::ElementStyle blockFallback = rootFallback;
      blockFallback.fill = style.blockFillColor;
      blockFallback.stroke = style.blockStrokeColor;
      blockFallback.strokeWidth = style.blockStrokeWidth;
      csscascade::ElementStyle labelFallback = rootFallback;
      labelFallback.fill = style.labelColor;
      labelFallback.fontSize = style.labelFontSize;
      csscascade::ElementStyle byteFallback = rootFallback;
      byteFallback.fill = style.startByteColor;
      byteFallback.fontSize = style.byteFontSize;
      csscascade::ElementStyle titleFallback = rootFallback;
      titleFallback.fill = style.titleColor;
      titleFallback.fontSize = style.titleFontSize;
      const auto css = csscascade::resolveElements(themeCss, {
        {QStringLiteral("svg"), {}, QStringLiteral("svg"),
         QStringLiteral("diagram-root"), {QStringLiteral("packet")}, {},
         rootFallback, {}},
        {QStringLiteral("root"), QStringLiteral("svg"), QStringLiteral("g"),
         {}, {QStringLiteral("root")}, {}, rootFallback, {}},
        {QStringLiteral("block"), QStringLiteral("root"), QStringLiteral("rect"),
         {}, {}, {}, blockFallback, {}},
        {QStringLiteral("label"), QStringLiteral("root"), QStringLiteral("text"),
         {}, {QStringLiteral("packetLabel")}, {}, labelFallback, {}},
        {QStringLiteral("byteStart"), QStringLiteral("root"), QStringLiteral("text"),
         {}, {QStringLiteral("packetByte"), QStringLiteral("start")}, {},
         byteFallback, {}},
        {QStringLiteral("byteEnd"), QStringLiteral("root"), QStringLiteral("text"),
         {}, {QStringLiteral("packetByte"), QStringLiteral("end")}, {},
         byteFallback, {}},
        {QStringLiteral("title"), QStringLiteral("root"), QStringLiteral("text"),
         {}, {QStringLiteral("packetTitle")}, {}, titleFallback, {}}
      });
      const auto block = css.value(QStringLiteral("block"), blockFallback);
      const auto label = css.value(QStringLiteral("label"), labelFallback);
      const auto byteStart = css.value(QStringLiteral("byteStart"), byteFallback);
      const auto byteEnd = css.value(QStringLiteral("byteEnd"), byteFallback);
      const auto titleStyle = css.value(QStringLiteral("title"), titleFallback);
      style.blockFillColor = block.fill;
      style.blockStrokeColor = block.stroke;
      style.blockStrokeWidth = block.strokeWidth;
      style.labelColor = label.fill;
      style.labelFontSize = label.fontSize;
      style.startByteColor = byteStart.fill;
      style.endByteColor = byteEnd.fill;
      style.byteFontSize = byteStart.fontSize;
      style.titleColor = titleStyle.fill;
      style.titleFontSize = titleStyle.fontSize;
    }

    packet::PacketScene scene = packet::buildPacketScene(
        data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        scene.style.titleColor, scene.style.fontFamily,
        scene.titleText.fontSize);
    // Packet renders its inline/frontmatter title inside the family viewBox.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene =
        std::make_shared<const packet::PacketScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& packetDiagramAdapter() {
  static const PacketDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
