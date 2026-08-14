#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/kanban/KanbanDiagram.h"
#include "mermaid/kanban/KanbanScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QHash>
#include <QJsonObject>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue nullishValue(const QJsonObject& object, const char* key,
                        const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() ? fallback : value;
}

QString ticketBaseUrl(const QJsonValue& raw) {
  if (raw.isUndefined() || raw.isNull() || !truthyConfigValue(raw))
    return QString();
  if (!raw.isString())
    throw std::runtime_error("config.ticketBaseUrl.replace is not a function");
  return raw.toString();
}

quint32 handDrawnSeed(const QJsonValue& raw) {
  const double value = jsNumberValue(raw);
  if (!std::isfinite(value) || value <= 0.0) return 0;
  constexpr double kMax = double(std::numeric_limits<quint32>::max());
  return static_cast<quint32>(std::min(value, kMax));
}

// Live subset of kanban getStyles (Mermaid 11.16.0). Dead for the kanban
// DOM: `.section-N text` (labels are html spans, not svg text), `.section-N
// line` (the only line lives under g.items), `.edge`, `.section-root`,
// `.node-icon-N`, `.disabled`, `.icon-container`, `.kanban-ticket-link`
// (no anchor without ticketBaseUrl) and the `.kanban-label` block whose
// declarations are either non-paint or non-CSS (`dy`).
QString kanbanBaseCss(const kanban::KanbanSceneStyle& style) {
  const auto scaleAt = [&style](int index) {
    return index >= 0 && index < style.cScale.size() ? style.cScale.at(index)
                                                    : QString();
  };
  const auto invAt = [&style](int index) {
    return index >= 0 && index < style.cScaleInv.size()
               ? style.cScaleInv.at(index)
               : QString();
  };
  const auto adjust = [&style](const QString& color) {
    if (color.isEmpty()) return QString();
    return style.darkMode ? color::darken(color, 10.0)
                          : color::lighten(color, 10.0);
  };
  QString css;
  for (int i = 0; i < style.themeColorRuleCount; ++i) {
    const QString idx = QString::number(i - 1);
    const QString paint = adjust(scaleAt(i));
    if (!paint.isEmpty())
      css += QStringLiteral(
                 ".section-%1 rect, .section-%1 path, .section-%1 circle, "
                 ".section-%1 polygon { fill: %2; stroke: %2; }\n")
                 .arg(idx, paint);
    css += QStringLiteral(".node rect, .node circle, .node ellipse, "
                           ".node polygon, .node path { fill: %1; stroke: %2; "
                           "stroke-width: 1px; }\n")
                 .arg(style.background, style.nodeBorder);
    const QString inv = invAt(i);
    if (!inv.isEmpty())
      css += QStringLiteral(".section-%1 line { stroke-width: 3px; }\n"
                            ".section-%1 line { stroke: %2; }\n")
                 .arg(idx, inv);
  }
  css += QStringLiteral(".cluster-label, .label { color: %1; fill: %1; }\n")
             .arg(style.textColor);
  return css;
}

struct KanbanDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("kanban")}; }
  QString cssClass() const override { return QStringLiteral("kanban"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject kanbanConfig =
        pre.config.value(QStringLiteral("kanban")).toObject();
    const QJsonObject mindmapConfig =
        pre.config.value(QStringLiteral("mindmap")).toObject();
    const QJsonValue look = pre.config.value(QStringLiteral("look"));

    kanban::KanbanParseConfig parseConfig;
    parseConfig.mindmapPadding =
        nullishValue(mindmapConfig, "padding", QJsonValue(10.0));
    parseConfig.mindmapMaxNodeWidth =
        nullishValue(mindmapConfig, "maxNodeWidth", QJsonValue(200.0));
    parseConfig.look =
        look.isString() ? look.toString() : QStringLiteral("classic");
    const kanban::KanbanData data =
        kanban::KanbanDiagram::parse(pre.code, parseConfig);

    kanban::KanbanConfig config;
    config.sectionWidth =
        nullishValue(kanbanConfig, "sectionWidth", QJsonValue(200.0));
    // Mermaid 11.16.0's Kanban renderer accidentally reads both values from
    // mindmap, not from the same-named Kanban configuration fields.
    config.padding =
        nullishValue(mindmapConfig, "padding", QJsonValue(10.0));
    config.useMaxWidth =
        nullishValue(mindmapConfig, "useMaxWidth", QJsonValue(true));
    const QJsonValue htmlLabels =
        pre.config.value(QStringLiteral("htmlLabels"));
    config.htmlLabels = htmlLabels.isUndefined() || htmlLabels.isNull()
                            ? true
                            : truthyConfigValue(htmlLabels);
    const QJsonValue markdownAutoWrap =
        pre.config.value(QStringLiteral("markdownAutoWrap"));
    config.markdownAutoWrap =
        markdownAutoWrap.isUndefined() || markdownAutoWrap.isNull()
            ? true
            : truthyConfigValue(markdownAutoWrap);
    config.ticketBaseUrl = ticketBaseUrl(
        kanbanConfig.value(QStringLiteral("ticketBaseUrl")));
    config.handDrawnSeed = handDrawnSeed(
        nullishValue(pre.config, "handDrawnSeed", QJsonValue(0.0)));

    const QJsonObject rawThemeVariables =
        pre.config.value(QStringLiteral("themeVariables")).toObject();
    kanban::KanbanSceneStyle style;
    style.themeName = effectiveTheme.isEmpty() ? QStringLiteral("default")
                                                : effectiveTheme;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(firstFontFamily(style.fontFamily), 16.0);
    style.fontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.background = themeVars.background;
    style.nodeBorder = themeVars.nodeBorder;
    style.shadowColor = themeVars.shadowColor;
    style.shadowOpacity = themeVars.shadowOpacity;
    style.shadowOffsetX = themeVars.shadowOffsetX;
    style.shadowOffsetY = themeVars.shadowOffsetY;
    const QJsonValue rawDropShadow =
        rawThemeVariables.value(QStringLiteral("dropShadow"));
    // Through Mermaid's source-entry sanitizer, any explicit override has a
    // used value of `none`: literal none, invalid filters, and even a custom
    // drop-shadow() all disable the built-in neo filter. Only absent/null
    // preserves the theme default.
    style.dropShadowEnabled =
        rawDropShadow.isUndefined() || rawDropShadow.isNull();
    style.darkMode = truthyConfigValue(
        rawThemeVariables.value(QStringLiteral("darkMode")));
    const QJsonValue rawThemeColorLimit =
        rawThemeVariables.value(QStringLiteral("THEME_COLOR_LIMIT"));
    style.rawThemeColorLimit =
        rawThemeColorLimit.isUndefined() || rawThemeColorLimit.isNull()
            ? qreal(themeVars.themeColorLimit)
            : qreal(jsNumberValue(rawThemeColorLimit));
    style.themeColorRuleCount =
        jsThemeColorLimit(pre.config).value_or(themeVars.themeColorLimit);
    for (const QString& color : themeVars.cScale) style.cScale.append(color);
    for (const QString& color : themeVars.cScaleInv)
      style.cScaleInv.append(color);
    for (const QString& color : themeVars.cScaleLabel)
      style.cScaleLabel.append(color);

    // The renderer visits items column by column (per-section parentId
    // filter over the flattened node list), so the override collection must
    // enumerate the same order.
    QVector<const kanban::KanbanNode*> itemNodes;
    for (const kanban::KanbanNode& section : data.sections)
      for (const kanban::KanbanNode& node : data.nodes)
        if (!node.isGroup && node.parentId == section.id)
          itemNodes.append(&node);

    // themeCSS: resolve the user sheet against a faithful model of the
    // kanban DOM (scaffold group, g.sections clusters with html labels,
    // g.items node cards whose label rects share the `.node rect` surface).
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    kanban::KanbanCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      const QString baseCss = kanbanBaseCss(style);
      // Mermaid paints `#id { font-family; font-size; fill: textColor }` on
      // the svg root; `svg {}` user rules are scoped to `#id svg` and never
      // reach it.
      ElementStyle rootStyle;
      rootStyle.fill = style.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize =
          QString::number(style.fontSize) + QStringLiteral("px");
      rootStyle.fontWeight = QStringLiteral("400");
      const ElementStyle inheritAll;

      overrides.active = true;
      QVector<ElementInput> tree;
      const auto push = [&tree](ElementInput input) {
        tree.append(std::move(input));
      };
      push({QStringLiteral("svg"), {}, QStringLiteral("svg"),
            QStringLiteral("diagram-root"), {}, {}, rootStyle, {}});
      push({QStringLiteral("styleEl"), QStringLiteral("svg"),
            QStringLiteral("style"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("scaffold"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("sections"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {QStringLiteral("sections")}, {},
            inheritAll, {}});

      for (int i = 0; i < data.sections.size(); ++i) {
        const kanban::KanbanNode& section = data.sections.at(i);
        const QString clusterKey =
            QStringLiteral("sec-%1-cluster").arg(i);
        push({clusterKey, QStringLiteral("sections"), QStringLiteral("g"),
              QStringLiteral("diagram-root-") + section.id,
              {QStringLiteral("cluster"), QStringLiteral("undefined"),
               QStringLiteral("section-%1").arg(i + 1)},
              {}, inheritAll, {}});
        push({QStringLiteral("sec-%1-box").arg(i), clusterKey,
              QStringLiteral("rect"), {}, {}, {}, inheritAll, {}});
        const QString labelKey = QStringLiteral("sec-%1-label").arg(i);
        push({QStringLiteral("sec-%1-cl").arg(i), clusterKey,
              QStringLiteral("g"), {}, {QStringLiteral("cluster-label")}, {},
              inheritAll, {}});
        push({QStringLiteral("sec-%1-fo").arg(i),
              QStringLiteral("sec-%1-cl").arg(i),
              QStringLiteral("foreignobject"), {}, {}, {}, inheritAll, {}});
        push({QStringLiteral("sec-%1-div").arg(i),
              QStringLiteral("sec-%1-fo").arg(i), QStringLiteral("div"), {},
              {}, {}, inheritAll,
              QStringLiteral(
                  "display:table-cell;white-space:nowrap;line-height:1.5")});
        push({labelKey, QStringLiteral("sec-%1-div").arg(i),
              QStringLiteral("span"), {}, {QStringLiteral("nodeLabel")}, {},
              inheritAll, {}});
        if (!section.label.isEmpty())
          push({QStringLiteral("sec-%1-p").arg(i), labelKey,
                QStringLiteral("p"), {}, {}, {}, inheritAll, {}});
      }

      push({QStringLiteral("items"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {QStringLiteral("items")}, {},
            inheritAll, {}});
      const auto priorityColorOf = [](const QJsonValue& value) {
        if (!value.isString()) return QString();
        const QString v = value.toString();
        if (v == QLatin1String("Very High")) return QStringLiteral("red");
        if (v == QLatin1String("High")) return QStringLiteral("orange");
        if (v == QLatin1String("Low")) return QStringLiteral("blue");
        if (v == QLatin1String("Very Low")) return QStringLiteral("lightblue");
        return QString();
      };
      int itemIndex = 0;
      for (const kanban::KanbanNode* node : itemNodes) {
        const QString nodeKey = QStringLiteral("item-%1-node").arg(itemIndex);
        push({nodeKey, QStringLiteral("items"), QStringLiteral("g"), {},
              {QStringLiteral("node"), QStringLiteral("undefined")}, {},
              inheritAll, {}});
        push({QStringLiteral("item-%1-box").arg(itemIndex), nodeKey,
              QStringLiteral("rect"), {},
              {QStringLiteral("basic"), QStringLiteral("label-container"),
               QStringLiteral("__APA__")},
              {}, inheritAll, {}});
        const QString labelTexts[3] = {node->label, node->ticket,
                                       node->assigned};
        for (int k = 0; k < 3; ++k) {
          const QString groupKey =
              QStringLiteral("item-%1-label-%2").arg(itemIndex).arg(k);
          push({groupKey, nodeKey, QStringLiteral("g"), {},
                {QStringLiteral("label")}, {}, inheritAll,
                QStringLiteral("text-align:left !important")});
          push({QStringLiteral("item-%1-bkg-%2").arg(itemIndex).arg(k),
                groupKey, QStringLiteral("rect"), {}, {}, {}, inheritAll, {}});
          push({QStringLiteral("item-%1-fo-%2").arg(itemIndex).arg(k),
                groupKey, QStringLiteral("foreignobject"), {}, {}, {},
                inheritAll, {}});
          push({QStringLiteral("item-%1-div-%2").arg(itemIndex).arg(k),
                QStringLiteral("item-%1-fo-%2").arg(itemIndex).arg(k),
                QStringLiteral("div"), {}, {}, {}, inheritAll,
                QStringLiteral(
                    "text-align:center;display:table-cell;white-space:nowrap")});
          QStringList spanClasses{QStringLiteral("nodeLabel")};
          if (k == 0)
            spanClasses.append(QStringLiteral("markdown-node-label"));
          const QString spanKey =
              QStringLiteral("item-%1-span-%2").arg(itemIndex).arg(k);
          push({spanKey, QStringLiteral("item-%1-div-%2").arg(itemIndex).arg(k),
                QStringLiteral("span"), {}, spanClasses, {}, inheritAll,
                k == 0 ? QStringLiteral("text-align:left !important")
                       : QString()});
          if (!labelTexts[k].isEmpty())
            push({QStringLiteral("item-%1-p-%2").arg(itemIndex).arg(k), spanKey,
                  QStringLiteral("p"), {}, {}, {}, inheritAll, {}});
        }
        const QString priority = priorityColorOf(node->priority);
        if (!priority.isEmpty())
          push({QStringLiteral("item-%1-priority").arg(itemIndex), nodeKey,
                QStringLiteral("line"), {}, {}, {}, inheritAll, {},
                QStringLiteral("stroke:%1;stroke-width:4px").arg(priority)});
        ++itemIndex;
      }

      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, baseCss);
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, style.fontSize);
      const auto fontSizePx = [&familyCtx](const QString& value) {
        return cssFontSizePx(value, familyCtx);
      };
      const auto convert = [&](const QString& key) {
        kanban::KanbanElementCss out;
        const ElementStyle& resolved = css.value(key);
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        out.color = resolved.color;
        // The engine only resolves font-family through matching rules or the
        // inherit keyword; an empty value keeps the root font chain.
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = fontSizePx(resolved.fontSize);
        out.fontWeight = resolved.fontWeight;
        out.opacity = cssOpacity(resolved.opacity);
        out.visible = resolved.displayed();
        out.hasBox = resolved.hasBox();
        return out;
      };
      for (int i = 0; i < data.sections.size(); ++i) {
        kanban::KanbanCssOverrides::Section resolved;
        resolved.cluster = convert(QStringLiteral("sec-%1-cluster").arg(i));
        resolved.box = convert(QStringLiteral("sec-%1-box").arg(i));
        resolved.label = convert(QStringLiteral("sec-%1-label").arg(i));
        overrides.sections.append(std::move(resolved));
      }
      itemIndex = 0;
      for (const kanban::KanbanNode* node : itemNodes) {
        kanban::KanbanCssOverrides::Item resolved;
        resolved.node = convert(QStringLiteral("item-%1-node").arg(itemIndex));
        resolved.box = convert(QStringLiteral("item-%1-box").arg(itemIndex));
        resolved.priority =
            convert(QStringLiteral("item-%1-priority").arg(itemIndex));
        for (int k = 0; k < 3; ++k) {
          kanban::KanbanCssOverrides::ItemLabel label;
          label.group = convert(QStringLiteral("item-%1-label-%2")
                                    .arg(itemIndex)
                                    .arg(k));
          label.bkg = convert(QStringLiteral("item-%1-bkg-%2")
                                  .arg(itemIndex)
                                  .arg(k));
          label.span = convert(QStringLiteral("item-%1-span-%2")
                                   .arg(itemIndex)
                                   .arg(k));
          resolved.labels.append(std::move(label));
        }
        overrides.items.append(std::move(resolved));
        ++itemIndex;
      }
    }

    kanban::KanbanScene scene = kanban::buildKanbanScene(
        data, std::move(config), std::move(style),
        themeCssActive ? &overrides : nullptr);
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), scene.style.textColor,
        scene.style.fontFamily, scene.style.fontSize);
    // Kanban has no commonDb title or accessibility directives. Upstream
    // ignores a frontmatter title completely, so it must not create the shared
    // 40px title strip or an ARIA title/description relationship.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene =
        std::make_shared<const kanban::KanbanScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& kanbanDiagramAdapter() {
  static const KanbanDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
