#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/architecture/ArchitectureDiagram.h"
#include "mermaid/architecture/ArchitectureScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSet>
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

struct ArchitectureDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("architecture")}; }
  QString cssClass() const override { return QStringLiteral("architecture"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject family =
        pre.config.value(QStringLiteral("architecture")).toObject();

    architecture::ArchitectureConfig config;
    config.useMaxWidth = scalar(family, "useMaxWidth", QJsonValue(true));
    config.padding = scalar(family, "padding", QJsonValue(40.0));
    config.iconSize = scalar(family, "iconSize", QJsonValue(80.0));
    config.fontSize = scalar(family, "fontSize", QJsonValue(16.0));
    config.randomize = scalar(family, "randomize", QJsonValue(false));
    config.nodeSeparation =
        scalar(family, "nodeSeparation", QJsonValue(75.0));
    config.idealEdgeLengthMultiplier =
        scalar(family, "idealEdgeLengthMultiplier", QJsonValue(1.5));
    config.edgeElasticity =
        scalar(family, "edgeElasticity", QJsonValue(0.45));
    config.numIter = scalar(family, "numIter", QJsonValue(2500.0));
    config.seed = scalar(family, "seed", QJsonValue(1.0));

    architecture::ArchitectureSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    style.textColor = themeVars.textColor;
    style.edgeColor = themeVars.archEdgeColor;
    style.arrowColor = themeVars.archEdgeArrowColor;
    style.edgeWidth = themeVars.archEdgeWidth;
    style.groupBorderColor = themeVars.archGroupBorderColor;
    style.groupBorderWidth = themeVars.archGroupBorderWidth;

    architecture::ArchitectureData data =
        architecture::ArchitectureDiagram::parse(pre.code);

    // themeCSS: the DOM shape depends only on the parsed data (the fcose
    // layout sizes nodes from config, never the DOM), so the resolveElements
    // tree is built in one pass. The geometry feedback is the final
    // setupGraphViewbox: the root getBBox reads the rendered label sizes, so a
    // CSS font-size on the bare label texts (createText drops the
    // architecture-service-label class on the SVG path) resizes the viewBox.
    // edge/arrow/node-bkg carry only their class — the base sheet below is
    // their sole styling source, exactly like the browser.
    const QString themeCss = pre.config.value(QStringLiteral("themeCSS")).toString();
    architecture::ArchitectureCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      QSet<QString> nodeIds;
      for (const auto& service : data.services)
        nodeIds.insert(service.id);
      for (const auto& junction : data.junctions)
        nodeIds.insert(junction.id);
      // The renderer root svg carries `#id { font-family; font-size; fill:
      // textColor }`; the bare label texts inherit everything through it.
      const qreal rootFontSize = cssFontSizePx(
          themeVars.fontSize,
          pieCssLengthContext(style.fontFamily, 16.0));
      ElementStyle rootStyle;
      rootStyle.fill = style.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize =
          QStringLiteral("%1px").arg(QString::number(rootFontSize));
      rootStyle.fontWeight = QStringLiteral("400");
      ElementStyle inheritAll;

      QVector<ElementInput> tree;
      const auto push = [&tree](ElementInput input) {
        tree.append(std::move(input));
      };
      push({QStringLiteral("svg"), {}, QStringLiteral("svg"),
            QStringLiteral("diagram-root"), {}, {}, rootStyle, {}});
      push({QStringLiteral("styleEl"), QStringLiteral("svg"),
            QStringLiteral("style"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("edges-layer"), QStringLiteral("svg"),
            QStringLiteral("g"), {},
            {QStringLiteral("architecture-edges")}, {}, inheritAll, {}});
      for (int i = 0; i < data.edges.size(); ++i) {
        const auto& edge = data.edges.at(i);
        if (!nodeIds.contains(edge.lhsId) || !nodeIds.contains(edge.rhsId))
          continue;
        const QString key = QStringLiteral("edge-%1").arg(i);
        push({key, QStringLiteral("edges-layer"), QStringLiteral("g"), {}, {},
              {}, inheritAll, {}});
        push({key + QLatin1String("-line"), key, QStringLiteral("path"), {},
              {QStringLiteral("edge")}, {}, inheritAll, {}});
        if (edge.lhsInto || edge.rhsInto)
          push({key + QLatin1String("-arrow"), key, QStringLiteral("polygon"),
                {}, {QStringLiteral("arrow")}, {}, inheritAll, {}});
        if (edge.hasTitle)
          push({key + QLatin1String("-label"), key, QStringLiteral("text"),
                {}, {}, {}, inheritAll, {}});
      }
      push({QStringLiteral("services-layer"), QStringLiteral("svg"),
            QStringLiteral("g"), {},
            {QStringLiteral("architecture-services")}, {}, inheritAll, {}});
      for (int i = 0; i < data.services.size(); ++i) {
        const auto& service = data.services.at(i);
        const QString key = QStringLiteral("service-%1").arg(i);
        push({key, QStringLiteral("services-layer"), QStringLiteral("g"), {},
              {QStringLiteral("architecture-service")}, {}, inheritAll, {}});
        if (service.hasTitle)
          push({key + QLatin1String("-label"), key, QStringLiteral("text"),
                {}, {}, {}, inheritAll, {}});
        if (service.icon.isEmpty() && service.iconText.isEmpty())
          push({key + QLatin1String("-nodebkg"), key, QStringLiteral("path"),
                {}, {QStringLiteral("node-bkg")}, {}, inheritAll, {}});
      }
      for (int j = 0; j < data.junctions.size(); ++j) {
        const QString key = QStringLiteral("junction-%1").arg(j);
        push({key, QStringLiteral("services-layer"), QStringLiteral("g"), {},
              {QStringLiteral("architecture-junction")}, {}, inheritAll, {}});
        push({key + QLatin1String("-rect"), key, QStringLiteral("rect"), {},
              {}, {}, inheritAll, {},
              QStringLiteral("fill-opacity:0")});
      }
      push({QStringLiteral("groups-layer"), QStringLiteral("svg"),
            QStringLiteral("g"), {},
            {QStringLiteral("architecture-groups")}, {}, inheritAll, {}});
      for (int k = 0; k < data.groups.size(); ++k) {
        const auto& group = data.groups.at(k);
        const QString key = QStringLiteral("group-%1").arg(k);
        push({key + QLatin1String("-rect"), QStringLiteral("groups-layer"),
              QStringLiteral("rect"), {}, {QStringLiteral("node-bkg")}, {},
              inheritAll, {}});
        push({key + QLatin1String("-labelcont"), QStringLiteral("groups-layer"),
              QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
        if (group.hasTitle)
          push({key + QLatin1String("-label"),
                key + QLatin1String("-labelcont"), QStringLiteral("text"), {},
                {}, {}, inheritAll, {}});
      }

      // Live architecture getStyles sheet with the resolved theme values —
      // the only styling the class-only edge/arrow/node-bkg elements get.
      const QString baseCss = QStringLiteral(
          ".edge { stroke-width: %1; stroke: %2; fill: none; }\n"
          ".arrow { fill: %3; }\n"
          ".node-bkg { fill: none; stroke: %4; stroke-width: %5; "
          "stroke-dasharray: 8; }\n")
          .arg(style.edgeWidth, style.edgeColor, style.arrowColor,
               style.groupBorderColor, style.groupBorderWidth);
      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, baseCss);
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, rootFontSize);
      const auto convert = [&](const QString& key) {
        architecture::ArchitectureElementCss out;
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
        // Pure channel opacities — the paint layer multiplies the opacity
        // product in once.
        out.fillOpacity = cssOpacity(resolved.fillOpacity);
        out.strokeOpacity = cssOpacity(resolved.strokeOpacity);
        out.visible = resolved.displayed();
        out.hasBox = resolved.hasBox();
        return out;
      };
      overrides.active = true;
      overrides.edgesLayer = convert(QStringLiteral("edges-layer"));
      overrides.servicesLayer = convert(QStringLiteral("services-layer"));
      overrides.groupsLayer = convert(QStringLiteral("groups-layer"));
      for (int i = 0; i < data.edges.size(); ++i) {
        architecture::ArchitectureCssOverrides::Edge slot;
        const auto& edge = data.edges.at(i);
        if (nodeIds.contains(edge.lhsId) && nodeIds.contains(edge.rhsId)) {
          const QString key = QStringLiteral("edge-%1").arg(i);
          slot.line = convert(key + QLatin1String("-line"));
          if (edge.lhsInto || edge.rhsInto)
            slot.arrow = convert(key + QLatin1String("-arrow"));
          if (edge.hasTitle) slot.label = convert(key + QLatin1String("-label"));
        }
        overrides.edges.append(std::move(slot));
      }
      for (int i = 0; i < data.services.size(); ++i) {
        architecture::ArchitectureCssOverrides::Node slot;
        const QString key = QStringLiteral("service-%1").arg(i);
        slot.group = convert(key);
        if (data.services.at(i).hasTitle)
          slot.label = convert(key + QLatin1String("-label"));
        if (data.services.at(i).icon.isEmpty() &&
            data.services.at(i).iconText.isEmpty())
          slot.nodeBkg = convert(key + QLatin1String("-nodebkg"));
        overrides.nodes.append(std::move(slot));
      }
      for (int j = 0; j < data.junctions.size(); ++j) {
        architecture::ArchitectureCssOverrides::Junction slot;
        const QString key = QStringLiteral("junction-%1").arg(j);
        slot.group = convert(key);
        slot.rect = convert(key + QLatin1String("-rect"));
        overrides.junctions.append(std::move(slot));
      }
      for (int k = 0; k < data.groups.size(); ++k) {
        architecture::ArchitectureCssOverrides::Group slot;
        const QString key = QStringLiteral("group-%1").arg(k);
        slot.rect = convert(key + QLatin1String("-rect"));
        if (data.groups.at(k).hasTitle)
          slot.label = convert(key + QLatin1String("-label"));
        overrides.groups.append(std::move(slot));
      }
    }

    architecture::ArchitectureScene scene = architecture::buildArchitectureScene(
        data, std::move(config), std::move(style),
        themeCssActive ? &overrides : nullptr);
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.textColor, scene.style.fontFamily,
        qreal(jsNumberValue(scene.config.fontSize)));
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene =
        std::make_shared<const architecture::ArchitectureScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& architectureDiagramAdapter() {
  static const ArchitectureDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
