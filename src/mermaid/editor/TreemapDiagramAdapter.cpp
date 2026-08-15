#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"
#include "mermaid/treemap/TreemapDiagram.h"
#include "mermaid/treemap/TreemapScene.h"

#include <QJsonObject>
#include <QSize>

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

struct TreemapDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("treemap")}; }
  QString cssClass() const override { return QStringLiteral("treemap"); }

  MermaidRenderEntry render(const MermaidPreprocessResult &pre,
                            const QString &type,
                            const QString &theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject raw =
        pre.config.value(QStringLiteral("treemap")).toObject();

    treemap::TreemapConfig config;
    config.useMaxWidth = scalar(raw, "useMaxWidth", true);
    config.padding = scalar(raw, "padding", 10.0);
    config.diagramPadding = scalar(raw, "diagramPadding", 8.0);
    config.showValues = scalar(raw, "showValues", true);
    config.nodeWidth = scalar(raw, "nodeWidth", 100.0);
    config.nodeHeight = scalar(raw, "nodeHeight", 40.0);
    config.valueFormat = scalar(raw, "valueFormat", QStringLiteral(","));

    treemap::TreemapSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    style.textColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    for (int i = 0; i < 12; ++i) {
      style.cScale[i] = themeVars.cScale[i];
      style.cScalePeer[i] = themeVars.cScalePeer[i];
      style.cScaleLabel[i] = themeVars.cScaleLabel[i];
    }
    const QJsonObject nestedTheme =
        pre.config.value(QStringLiteral("themeVariables")).toObject()
            .value(QStringLiteral("treemap")).toObject();
    if (nestedTheme.value(QStringLiteral("titleColor")).isString())
      style.titleColor = nestedTheme.value(QStringLiteral("titleColor")).toString();
    if (nestedTheme.value(QStringLiteral("titleFontSize")).isString()) {
      const CssLengthContext root = pieCssLengthContext(style.fontFamily, 16.0);
      style.titleFontSize = cssFontSizePx(
          nestedTheme.value(QStringLiteral("titleFontSize")).toString(), root);
    }

    treemap::TreemapData data = treemap::TreemapDiagram::parse(pre.code);
    if (!data.hasTitleDirective && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);

    // themeCSS: the shrink loops write inline font sizes that beat
    // non-important rules, so a probe build supplies the final inline state
    // and the emission order; only the title keeps a live base rule, and the
    // final svg.getBBox reacts to display:none plus the title font-size.
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    treemap::TreemapCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      const treemap::TreemapScene probe =
          treemap::buildTreemapScene(data, config, style);
      // setupViewPortForSVG stamps class="flowchart" on the svg root; the
      // `#id { font-family; font-size; fill: textColor }` root rule carries
      // the inherited chain.
      ElementStyle rootStyle;
      rootStyle.fill = style.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize = QStringLiteral("16px");
      rootStyle.fontWeight = QStringLiteral("400");
      ElementStyle inheritAll;

      QVector<ElementInput> tree;
      const auto push = [&tree](ElementInput input) {
        tree.append(std::move(input));
      };
      push({QStringLiteral("svg"), {}, QStringLiteral("svg"),
            QStringLiteral("diagram-root"), {QStringLiteral("flowchart")},
            {}, rootStyle, {}});
      push({QStringLiteral("styleEl"), QStringLiteral("svg"),
            QStringLiteral("style"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("scaffold"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      if (!probe.title.text.isEmpty()) {
        push({QStringLiteral("title"), QStringLiteral("svg"),
              QStringLiteral("text"), {}, {QStringLiteral("treemapTitle")},
              {}, inheritAll, {},
              QStringLiteral("text-anchor:middle;dominant-baseline:middle")});
      }
      push({QStringLiteral("container"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {QStringLiteral("treemapContainer")}, {},
            inheritAll, {}});
      for (qsizetype section = 0; section < probe.sections.size(); ++section) {
        const auto& source = probe.sections.at(section);
        const QString key = QStringLiteral("section-%1").arg(section);
        const bool hidden = source.depth == 0;
        push({key, QStringLiteral("container"), QStringLiteral("g"), {},
              {QStringLiteral("treemapSection")}, {}, inheritAll, {}});
        push({key + QLatin1String("-header"), key, QStringLiteral("rect"), {},
              {QStringLiteral("treemapSectionHeader")}, {}, inheritAll,
              hidden ? QStringLiteral("display:none") : QString(),
              QStringLiteral("fill:none;fill-opacity:0.6;stroke-width:0.6")});
        // The clipPath and its rect are never rendered; skipped.
        push({key + QLatin1String("-rect"), key, QStringLiteral("rect"), {},
              {QStringLiteral("treemapSection"),
               QStringLiteral("section%1").arg(section)},
              {}, inheritAll,
              hidden ? QStringLiteral("display:none") : QString(),
              QStringLiteral("fill:%1;fill-opacity:0.6;stroke:%2;"
                             "stroke-width:2;stroke-opacity:0.4")
                  .arg(source.fill, source.stroke)});
        push({key + QLatin1String("-label"), key, QStringLiteral("text"), {},
              {QStringLiteral("treemapSectionLabel")}, {}, inheritAll,
              hidden
                  ? QStringLiteral("display:none")
                  : QStringLiteral(
                        "dominant-baseline:middle;font-size:12px;fill:%1;"
                        "white-space:nowrap;overflow:hidden;"
                        "text-overflow:ellipsis")
                        .arg(source.label.fill),
              QStringLiteral("font-weight:bold")});
        if (!source.value.text.isEmpty()) {
          push({key + QLatin1String("-value"), key, QStringLiteral("text"), {},
                {QStringLiteral("treemapSectionValue")}, {}, inheritAll,
                hidden
                    ? QStringLiteral("display:none")
                    : QStringLiteral(
                          "text-anchor:end;dominant-baseline:middle;"
                          "font-size:10px;fill:%1;white-space:nowrap;"
                          "overflow:hidden;text-overflow:ellipsis")
                          .arg(source.value.fill),
                QStringLiteral("font-style:italic")});
        }
      }
      for (qsizetype leaf = 0; leaf < probe.leaves.size(); ++leaf) {
        const auto& source = probe.leaves.at(leaf);
        const QString key = QStringLiteral("leaf-%1").arg(leaf);
        QStringList groupClasses{QStringLiteral("treemapNode"),
                                  QStringLiteral("treemapLeafGroup"),
                                  QStringLiteral("leaf%1x").arg(leaf)};
        if (!source.classSelector.isEmpty())
          groupClasses.append(source.classSelector);
        push({key, QStringLiteral("container"), QStringLiteral("g"), {},
              groupClasses, {}, inheritAll, {}});
        push({key + QLatin1String("-rect"), key, QStringLiteral("rect"), {},
              {QStringLiteral("treemapLeaf")}, {}, inheritAll, {},
              QStringLiteral("fill:%1;fill-opacity:0.3;stroke:%2;"
                             "stroke-width:3")
                  .arg(source.fill, source.stroke)});
        QString labelInline =
            QStringLiteral("text-anchor:middle;dominant-baseline:middle;"
                           "font-size:%1px;fill:%2")
                .arg(QString::number(source.label.fontSize),
                     source.label.fill);
        if (!source.label.visible)
          labelInline += QLatin1String(";display:none");
        push({key + QLatin1String("-label"), key, QStringLiteral("text"), {},
              {QStringLiteral("treemapLabel")}, {}, inheritAll, labelInline});
        if (!source.value.text.isEmpty()) {
          // The shrink loop stamps the final value font inline and may hide
          // the text outright.
          QString valueInline =
              QStringLiteral("text-anchor:middle;dominant-baseline:hanging;"
                             "font-size:%1px;fill:%2")
                  .arg(QString::number(source.value.fontSize),
                       source.value.fill);
          if (!source.value.visible)
            valueInline += QLatin1String(";display:none");
          push({key + QLatin1String("-value"), key, QStringLiteral("text"),
                {}, {QStringLiteral("treemapValue")}, {}, inheritAll,
                valueInline});
        }
      }

      // Live subset of treemap getStyles: .treemapNode.section/.leaf match
      // nothing (section groups carry treemapSection; leaf groups carry
      // treemapNode but no section/leaf token), and the label/value rules
      // lose to the shrink loops' inline styles. Only .treemapTitle lives.
      const QString baseCss =
          QStringLiteral(
              ".treemapNode.section { stroke: black; stroke-width: 1; fill: #efefef; }\n"
              ".treemapNode.leaf { stroke: black; stroke-width: 1; fill: #efefef; }\n"
              ".treemapLabel { fill: %1; font-size: 12px; }\n"
              ".treemapValue { fill: %1; font-size: 10px; }\n"
              ".treemapTitle { fill: %2; font-size: %3px; }\n")
              .arg(style.textColor, style.titleColor,
                   QString::number(style.titleFontSize));
      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, baseCss);
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, 16.0);
      const auto convert = [&](const QString& key) {
        treemap::TreemapElementCss out;
        const ElementStyle& resolved = css.value(key);
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = cssFontSizePx(resolved.fontSize, familyCtx);
        out.fontWeight = resolved.fontWeight;
        out.fontStyle = resolved.fontStyle;
        out.opacity = resolved.effectiveOpacity;
        out.fillOpacity = resolved.effectiveFillOpacity;
        out.strokeOpacity = resolved.effectiveStrokeOpacity;
        out.visible = resolved.displayed();
        out.hasBox = resolved.hasBox();
        out.measures = resolved.display.compare(QStringLiteral("none"),
                                                Qt::CaseInsensitive) != 0;
        return out;
      };
      overrides.active = true;
      if (!probe.title.text.isEmpty())
        overrides.title = convert(QStringLiteral("title"));
      for (qsizetype section = 0; section < probe.sections.size(); ++section) {
        treemap::TreemapCssOverrides::Section slot;
        const QString key = QStringLiteral("section-%1").arg(section);
        slot.group = convert(key);
        slot.header = convert(key + QLatin1String("-header"));
        slot.rect = convert(key + QLatin1String("-rect"));
        slot.label = convert(key + QLatin1String("-label"));
        slot.value = convert(key + QLatin1String("-value"));
        overrides.sections.append(std::move(slot));
      }
      for (qsizetype leaf = 0; leaf < probe.leaves.size(); ++leaf) {
        treemap::TreemapCssOverrides::Leaf slot;
        const QString key = QStringLiteral("leaf-%1").arg(leaf);
        slot.group = convert(key);
        slot.rect = convert(key + QLatin1String("-rect"));
        slot.label = convert(key + QLatin1String("-label"));
        slot.value = convert(key + QLatin1String("-value"));
        overrides.leaves.append(std::move(slot));
      }
    }

    treemap::TreemapScene scene = treemap::buildTreemapScene(
        data, std::move(config), std::move(style),
        themeCssActive ? &overrides : nullptr);
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, data.title, data.accTitle, data.accDescr,
                       themeVars.titleColor, themeVars.fontFamily, 14.0);
    // The renderer owns the visible title inside the SVG at y=15.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.rasterBounds.width()),
                              qRound(scene.rasterBounds.height()));
    entry.scene =
        std::make_shared<const treemap::TreemapScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

} // namespace

const Diagram &treemapDiagramAdapter() {
  static const TreemapDiagramImpl adapter;
  return adapter;
}

} // namespace muffin::mermaid::editor
