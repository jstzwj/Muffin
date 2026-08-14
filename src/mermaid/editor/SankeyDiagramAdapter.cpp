#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/sankey/SankeyDiagram.h"
#include "mermaid/sankey/SankeyScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QSize>

#include <cmath>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue scalar(const QJsonObject &object, const char *key,
                  const QJsonValue &fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct SankeyDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("sankey")}; }
  QString cssClass() const override { return QStringLiteral("sankey"); }

  MermaidRenderEntry render(const MermaidPreprocessResult &pre,
                            const QString &type,
                            const QString &theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject raw =
        pre.config.value(QStringLiteral("sankey")).toObject();

    sankey::SankeyConfig config;
    config.width = scalar(raw, "width", 600.0);
    config.height = scalar(raw, "height", 400.0);
    config.useMaxWidth = scalar(raw, "useMaxWidth", true);
    config.linkColor = scalar(raw, "linkColor", QStringLiteral("gradient"));
    config.nodeAlignment =
        scalar(raw, "nodeAlignment", QStringLiteral("justify"));
    config.showValues = scalar(raw, "showValues", true);
    config.prefix = scalar(raw, "prefix", QString());
    config.suffix = scalar(raw, "suffix", QString());
    config.nodeWidth = scalar(raw, "nodeWidth", 10.0);
    config.nodePadding = scalar(raw, "nodePadding", 12.0);
    config.labelStyle = scalar(raw, "labelStyle", QStringLiteral("legacy"));
    if (raw.value(QStringLiteral("nodeColors")).isObject()) {
      static const QRegularExpression safeColor(
          QStringLiteral(
              R"(^(?:#[\da-f]{3,8}|rgb\([\d\s%,.]+\)|hsl\([\d\s%,.]+\)|[a-z]+)$)"),
          QRegularExpression::CaseInsensitiveOption);
      const QJsonObject colors =
          raw.value(QStringLiteral("nodeColors")).toObject();
      for (auto it = colors.begin(); it != colors.end(); ++it)
        if (it.value().isString() &&
            safeColor.match(it.value().toString()).hasMatch())
          config.nodeColors.insert(it.key(), it.value());
    }

    sankey::SankeySceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    style.textColor = themeVars.textColor;
    style.mainBkg = themeVars.mainBkg;
    style.background = themeVars.background;

    const sankey::SankeyData data = sankey::SankeyDiagram::parse(pre.code);
    sankey::SankeyScene scene =
        sankey::buildSankeyScene(data, std::move(config), std::move(style));

    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      QVector<ElementInput> elements;
      ElementStyle rootStyle;
      rootStyle.fill = scene.style.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = scene.style.fontFamily;
      rootStyle.fontSize = QStringLiteral("16px");
      rootStyle.fontWeight = QStringLiteral("400");
      elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                       QStringLiteral("diagram-root"), {}, {}, rootStyle, {}});
      elements.append({QStringLiteral("nodes"), QStringLiteral("svg"),
                       QStringLiteral("g"), {}, {QStringLiteral("nodes")},
                       {}, rootStyle, {}});
      QVector<QString> nodeKeys(scene.nodes.size());
      for (qsizetype i = 0; i < scene.nodes.size(); ++i) {
        const auto &node = scene.nodes.at(i);
        const QString group = QStringLiteral("node-group-%1").arg(i);
        elements.append({group, QStringLiteral("nodes"), QStringLiteral("g"),
                         QStringLiteral("node-%1").arg(i),
                         {QStringLiteral("node")}, {}, rootStyle, {}});
        ElementStyle rect = rootStyle;
        rect.fill = node.color;
        rect.stroke = node.stroke;
        rect.strokeWidth = QString::number(node.strokeWidth) +
                           QStringLiteral("px");
        const QString key = QStringLiteral("node-rect-%1").arg(i);
        nodeKeys[i] = key;
        elements.append({key, group, QStringLiteral("rect"), {}, {},
                         {{QStringLiteral("fill"), node.color}}, rect, {}});
      }
      ElementStyle labelsStyle = rootStyle;
      labelsStyle.fontSize = QStringLiteral("14px");
      elements.append({QStringLiteral("labels"), QStringLiteral("svg"),
                       QStringLiteral("g"), {},
                       {QStringLiteral("node-labels")},
                       {{QStringLiteral("font-size"), QStringLiteral("14")}},
                       labelsStyle, {}});
      QVector<QString> labelKeys(scene.labels.size());
      for (qsizetype i = 0; i < scene.labels.size(); ++i) {
        const auto &label = scene.labels.at(i);
        ElementStyle text = labelsStyle;
        text.fill = label.fill;
        text.stroke = label.stroke;
        text.strokeWidth = QString::number(label.backgroundLayer
                                               ? label.strokeWidth
                                               : 1.0) +
                           QStringLiteral("px");
        const QString key = QStringLiteral("label-%1").arg(i);
        labelKeys[i] = key;
        QStringList classes;
        if (label.backgroundLayer)
          classes.append(QStringLiteral("sankey-label-bg"));
        else if (label.outlined)
          classes.append(QStringLiteral("sankey-label-fg"));
        elements.append({key, QStringLiteral("labels"), QStringLiteral("text"),
                         {}, classes, {}, text, {}});
      }
      ElementStyle linksStyle = rootStyle;
      linksStyle.fill = QStringLiteral("none");
      linksStyle.strokeOpacity = QStringLiteral("0.5");
      elements.append({QStringLiteral("links"), QStringLiteral("svg"),
                       QStringLiteral("g"), {}, {QStringLiteral("links")},
                       {{QStringLiteral("fill"), QStringLiteral("none")},
                        {QStringLiteral("stroke-opacity"), QStringLiteral("0.5")}},
                       linksStyle, {}});
      QVector<QString> linkKeys(scene.links.size());
      QVector<QString> linkGroupKeys(scene.links.size());
      for (qsizetype i = 0; i < scene.links.size(); ++i) {
        const QString group = QStringLiteral("link-group-%1").arg(i);
        linkGroupKeys[i] = group;
        ElementStyle linkGroup = linksStyle;
        linkGroup.mixBlendMode = QStringLiteral("multiply");
        elements.append({group, QStringLiteral("links"), QStringLiteral("g"),
                         {}, {QStringLiteral("link")}, {}, linkGroup,
                         QStringLiteral("mix-blend-mode:multiply")});
        const auto &link = scene.links.at(i);
        ElementStyle path = linkGroup;
        path.stroke = link.stroke == QLatin1String("gradient")
                          ? QStringLiteral("url(#gradient)")
                          : link.stroke;
        path.strokeWidth = QString::number(link.width) + QStringLiteral("px");
        const QString key = QStringLiteral("link-path-%1").arg(i);
        linkKeys[i] = key;
        elements.append({key, group, QStringLiteral("path"), {}, {},
                         {{QStringLiteral("stroke"), path.stroke},
                          {QStringLiteral("stroke-width"),
                           QString::number(link.width)}},
                         path, {}});
      }

      const QString builtInCss = QStringLiteral(
          ".node-labels{font-family:%1;}"
          ".sankey-label-bg{stroke:%2;stroke-width:4px;}"
          ".sankey-label-fg{fill:%3;}"
          ".link{fill:none;stroke-opacity:.5;mix-blend-mode:multiply;}")
          .arg(scene.style.fontFamily,
               scene.style.mainBkg.isEmpty() ? scene.style.background
                                             : scene.style.mainBkg,
               scene.style.textColor);
      const auto css = csscascade::resolveElements(themeCss, elements,
                                                    builtInCss);
      const CssLengthContext context = pieCssLengthContext(
          firstFontFamily(scene.style.fontFamily), 16.0);
      const qreal diagonal = std::hypot(scene.configuredWidth,
                                        scene.configuredHeight) /
                             std::sqrt(2.0);
      for (qsizetype i = 0; i < scene.nodes.size(); ++i) {
        auto &node = scene.nodes[i];
        const auto value = css.value(nodeKeys.at(i));
        node.color = value.fill;
        node.stroke = value.stroke;
        node.strokeWidth = cssStrokeWidthPx(value.strokeWidth, context,
                                            diagonal);
        node.opacity = value.effectiveFillOpacity;
        node.strokeOpacity = value.effectiveStrokeOpacity;
        node.visible = value.displayed();
        node.hasBox = value.hasBox();
      }
      for (qsizetype i = 0; i < scene.labels.size(); ++i) {
        auto &label = scene.labels[i];
        const auto value = css.value(labelKeys.at(i));
        label.fill = value.fill;
        label.stroke = value.stroke;
        label.strokeWidth = cssStrokeWidthPx(value.strokeWidth, context,
                                             diagonal);
        label.fontFamily = value.fontFamily;
        label.fontSize = cssFontSizePx(value.fontSize, context);
        label.fontWeight = cssFontWeightToQt(QJsonValue(value.fontWeight),
                                             QFont::Normal);
        label.opacity = label.backgroundLayer ? value.effectiveStrokeOpacity
                                              : value.effectiveFillOpacity;
        label.visible = value.displayed();
        label.hasBox = value.hasBox();
      }
      for (qsizetype i = 0; i < scene.links.size(); ++i) {
        auto &link = scene.links[i];
        const auto value = css.value(linkKeys.at(i));
        link.stroke = value.stroke.startsWith(QLatin1String("url("),
                                              Qt::CaseInsensitive)
                          ? QStringLiteral("gradient")
                          : value.stroke;
        link.width = cssStrokeWidthPx(value.strokeWidth, context, diagonal);
        link.opacity = value.effectiveStrokeOpacity;
        link.visible = value.displayed();
        link.hasBox = value.hasBox();
        link.mixBlendMode = css.value(linkGroupKeys.at(i)).mixBlendMode;
      }
      sankey::refreshSankeyBounds(scene);
    }
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, QString(), QString(), QString(),
                       themeVars.textColor, themeVars.fontFamily, 16.0);
    // Sankey's parser exposes commonDb methods but has no metadata grammar; its
    // renderer does not project frontmatter title/accessibility nodes.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.rasterBounds.width()),
                              qRound(scene.rasterBounds.height()));
    entry.scene = std::make_shared<const sankey::SankeyScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

} // namespace

const Diagram &sankeyDiagramAdapter() {
  static const SankeyDiagramImpl adapter;
  return adapter;
}

} // namespace muffin::mermaid::editor
