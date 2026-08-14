#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"
#include "mermaid/treeview/TreeViewDiagram.h"
#include "mermaid/treeview/TreeViewScene.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QSize>

#include <algorithm>
#include <cmath>
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

QFont::Style cssFontStyle(const QString& value) {
  const QString lower = value.trimmed().toLower();
  if (lower == QLatin1String("italic")) return QFont::StyleItalic;
  if (lower.startsWith(QLatin1String("oblique"))) return QFont::StyleOblique;
  return QFont::StyleNormal;
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
        data, config, style);
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      QVector<ElementInput> elements;
      ElementStyle rootStyle;
      rootStyle.fill = style.rootTextColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize = QString::number(style.rootFontSize) +
                           QStringLiteral("px");
      rootStyle.fontWeight = QStringLiteral("400");
      rootStyle.fontStyle = QStringLiteral("normal");
      elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                       QStringLiteral("diagram-root"),
                       {QStringLiteral("treeView")}, {}, rootStyle, {}});
      elements.append({QStringLiteral("defs"), QStringLiteral("svg"),
                       QStringLiteral("defs"), {}, {}, {}, rootStyle, {}});
      elements.append({QStringLiteral("tree"), QStringLiteral("svg"),
                       QStringLiteral("g"), {},
                       {QStringLiteral("tree-view")}, {}, rootStyle, {}});

      QHash<int, QString> groupKeys;
      QHash<int, QString> labelKeys;
      QHash<int, QString> descriptionKeys;
      QHash<int, QString> highlightKeys;
      QVector<QString> lineKeys(scene.lines.size());
      struct PaintElement { bool node = false; int index = -1; int order = -1; };
      QVector<PaintElement> paintElements;
      for (qsizetype i = 0; i < scene.nodes.size(); ++i)
        paintElements.append({true, int(i), scene.nodes.at(i).groupPaintOrder});
      for (qsizetype i = 0; i < scene.lines.size(); ++i)
        paintElements.append({false, int(i), scene.lines.at(i).paintOrder});
      std::stable_sort(paintElements.begin(), paintElements.end(),
                       [](const PaintElement& left, const PaintElement& right) {
                         return left.order < right.order;
                       });

      for (const PaintElement& item : paintElements) {
        if (!item.node) {
          const auto& line = scene.lines.at(item.index);
          ElementStyle fallback = rootStyle;
          fallback.fill = QStringLiteral("black");
          fallback.stroke = style.lineColor;
          fallback.strokeWidth = line.strokeWidthAttribute;
          const QString key = QStringLiteral("line-%1").arg(item.index);
          lineKeys[item.index] = key;
          elements.append({key, QStringLiteral("tree"), QStringLiteral("line"),
                           {}, {QStringLiteral("treeView-node-line")},
                           {{QStringLiteral("stroke-width"),
                             line.strokeWidthAttribute}}, fallback, {}});
          continue;
        }

        const auto& node = scene.nodes.at(item.index);
        const QString group = QStringLiteral("node-group-%1").arg(node.id);
        groupKeys.insert(node.id, group);
        elements.append({group, QStringLiteral("tree"), QStringLiteral("g"),
                         {}, {}, {}, rootStyle, {}});
        if (node.highlighted) {
          ElementStyle fallback = rootStyle;
          fallback.fill = style.highlightBg;
          fallback.stroke = style.highlightStroke;
          fallback.strokeWidth = QStringLiteral("1px");
          const QString key = QStringLiteral("highlight-%1").arg(node.id);
          highlightKeys.insert(node.id, key);
          elements.append({key, group, QStringLiteral("rect"), {},
                           {QStringLiteral("treeView-highlight-bg")}, {},
                           fallback, {}});
        }
        if (node.iconReserved) {
          ElementStyle fallback = rootStyle;
          fallback.color = style.iconColor;
          elements.append({QStringLiteral("icon-%1").arg(node.id), group,
                           QStringLiteral("use"), {},
                           {QStringLiteral("treeView-node-icon")}, {},
                           fallback, {}});
        }
        ElementStyle fallback = rootStyle;
        fallback.fill = style.labelColor;
        fallback.fontSize = style.labelFontSize;
        fallback.fontWeight = node.label.bold ? QStringLiteral("700")
                                              : QStringLiteral("400");
        QStringList classes = node.label.cssClass.split(
            QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
        const QString key = QStringLiteral("label-%1").arg(node.id);
        labelKeys.insert(node.id, key);
        elements.append({key, group, QStringLiteral("text"), {}, classes, {},
                         fallback, {}});
      }

      // Descriptions are appended only after the complete tree and all lines
      // have been created. Keeping them last here preserves :first-child and
      // sibling-selector behavior inside every node group.
      for (const auto& node : scene.nodes) {
        if (!node.hasDescription) continue;
        ElementStyle fallback = rootStyle;
        fallback.fill = style.descriptionColor;
        fallback.fontSize = style.labelFontSize;
        fallback.fontStyle = QStringLiteral("italic");
        const QString key = QStringLiteral("description-%1").arg(node.id);
        descriptionKeys.insert(node.id, key);
        elements.append({key, groupKeys.value(node.id), QStringLiteral("text"),
                         {}, {QStringLiteral("treeView-node-description")}, {},
                         fallback, {}});
      }

      const QString builtInCss = QStringLiteral(
          ".treeView-node-label{font-size:%1;fill:%2;white-space:pre;}"
          ".treeView-node-dir{font-weight:bold;}"
          ".treeView-node-line{stroke:%3;}"
          ".treeView-node-icon{color:%4;}"
          ".treeView-node-description{font-size:%1;fill:%5;"
          "font-style:italic;white-space:pre;}"
          ".treeView-highlight-bg{fill:%6;stroke:%7;stroke-width:1;}")
          .arg(style.labelFontSize, style.labelColor, style.lineColor,
               style.iconColor, style.descriptionColor, style.highlightBg,
               style.highlightStroke);
      const auto css = csscascade::resolveElements(themeCss, elements,
                                                    builtInCss);
      QHash<QString, QString> parentKeys;
      for (const ElementInput& element : elements)
        parentKeys.insert(element.key, element.parentKey);
      const CssLengthContext rootContext =
          pieCssLengthContext(style.fontFamily, style.rootFontSize);
      const qreal diagonal = std::hypot(scene.totalWidth, scene.totalHeight) /
                             std::sqrt(2.0);
      auto resolveText = [&](const QString& key, int nodeId,
                             QHash<int, treeview::TreeViewResolvedTextStyle>& out) {
        const auto value = css.value(key);
        const QString parentKey = parentKeys.value(key);
        const auto parent = css.value(parentKey);
        const qreal parentSize = cssFontSizePx(parent.fontSize, rootContext);
        const CssLengthContext context = pieCssLengthContext(
            value.fontFamily, parentSize);
        treeview::TreeViewResolvedTextStyle resolved;
        resolved.fontFamily = value.fontFamily;
        resolved.fontSize = cssFontSizePx(value.fontSize, context);
        resolved.fontWeight = cssFontWeightToQt(QJsonValue(value.fontWeight),
                                                QFont::Normal);
        resolved.fontStyle = cssFontStyle(value.fontStyle);
        resolved.fill = value.fill;
        resolved.opacity = value.effectiveFillOpacity;
        resolved.visible = value.displayed();
        resolved.hasBox = value.hasBox();
        out.insert(nodeId, std::move(resolved));
      };
      for (const auto& node : scene.nodes) {
        resolveText(labelKeys.value(node.id), node.id, style.labelStyles);
        if (descriptionKeys.contains(node.id))
          resolveText(descriptionKeys.value(node.id), node.id,
                      style.descriptionStyles);
        if (highlightKeys.contains(node.id)) {
          const auto value = css.value(highlightKeys.value(node.id));
          treeview::TreeViewResolvedShapeStyle resolved;
          resolved.fill = value.fill;
          resolved.stroke = value.stroke;
          resolved.strokeWidth = cssStrokeWidthPx(value.strokeWidth,
                                                  rootContext, diagonal);
          resolved.fillOpacity = value.effectiveFillOpacity;
          resolved.strokeOpacity = value.effectiveStrokeOpacity;
          resolved.visible = value.displayed();
          style.highlightStyles.insert(node.id, std::move(resolved));
        }
      }
      for (qsizetype i = 0; i < scene.lines.size(); ++i) {
        const auto value = css.value(lineKeys.at(i));
        treeview::TreeViewResolvedShapeStyle resolved;
        resolved.stroke = value.stroke;
        resolved.strokeWidth = cssStrokeWidthPx(value.strokeWidth,
                                                rootContext, diagonal);
        resolved.strokeOpacity = value.effectiveStrokeOpacity;
        resolved.visible = value.displayed();
        style.lineStyles.append(std::move(resolved));
      }
      scene = treeview::buildTreeViewScene(data, std::move(config),
                                           std::move(style));
    }
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
