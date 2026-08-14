#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/eventmodeling/EventModelingDiagram.h"
#include "mermaid/eventmodeling/EventModelingScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue eventModelingScalar(const QJsonObject& object, const char* key,
                               const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct EventModelingDiagramImpl final : Diagram {
  QStringList ids() const override {
    return {QStringLiteral("eventmodeling")};
  }
  QString cssClass() const override {
    return QStringLiteral("eventmodeling");
  }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw =
        pre.config.value(QStringLiteral("eventmodeling")).toObject();
    eventmodeling::EventModelingConfig config;
    config.useMaxWidth =
        eventModelingScalar(raw, "useMaxWidth", QJsonValue(true));
    config.padding = eventModelingScalar(raw, "padding", QJsonValue(30.0));
    config.rowHeight =
        eventModelingScalar(raw, "rowHeight", QJsonValue(32.0));

    const eventmodeling::EventModelingData data =
        eventmodeling::EventModelingDiagram::parse(pre.code);

    eventmodeling::EventModelingSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(style.fontFamily, 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.uiFill = themeVars.emUiFill;
    style.uiStroke = themeVars.emUiStroke;
    style.processorFill = themeVars.emProcessorFill;
    style.processorStroke = themeVars.emProcessorStroke;
    style.readModelFill = themeVars.emReadModelFill;
    style.readModelStroke = themeVars.emReadModelStroke;
    style.commandFill = themeVars.emCommandFill;
    style.commandStroke = themeVars.emCommandStroke;
    style.eventFill = themeVars.emEventFill;
    style.eventStroke = themeVars.emEventStroke;
    style.swimlaneFill = themeVars.emSwimlaneBackgroundOdd;
    style.swimlaneStroke = themeVars.emSwimlaneBackgroundStroke;
    style.arrowhead = themeVars.emArrowhead;
    style.relationStroke = themeVars.emRelationStroke;

    // themeCSS: eventmodeling ships no base stylesheet, so the user sheet is
    // the only author origin besides presentation attributes and the
    // foreignObject inline styles. Layout never reads the DOM
    // (calculateTextDimensions uses hardcoded config fonts), so a single
    // resolve pass against a faithful DOM model covers everything.
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    eventmodeling::EventModelingCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      // A probe build supplies the emission order and the presentation
      // values (frame-type fills, lane backgrounds) without duplicating the
      // builder's lane/box bookkeeping.
      const eventmodeling::EventModelingScene probe =
          eventmodeling::buildEventModelingScene(data, config, style);
      // Mermaid paints `#id { font-family; font-size; fill: textColor }` on
      // the svg root; nothing sets `color`, so the foreignObject spans start
      // at the initial black.
      ElementStyle rootStyle;
      rootStyle.fill = style.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize =
          QString::number(style.rootFontSize) + QStringLiteral("px");
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
      push({QStringLiteral("scaffold"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      for (qsizetype lane = 0; lane < probe.swimlanes.size(); ++lane) {
        const QString laneKey = QStringLiteral("lane-%1").arg(lane);
        push({laneKey, QStringLiteral("svg"), QStringLiteral("g"), {},
              {QStringLiteral("em-swimlane")}, {}, inheritAll, {}});
        push({laneKey + QLatin1String("-rect"), laneKey, QStringLiteral("rect"),
              {}, {},
              {}, inheritAll, {},
              QStringLiteral("fill:%1;stroke:%2")
                  .arg(style.swimlaneFill, style.swimlaneStroke)});
        // d3 stamps the lane title's font-weight attribute only.
        push({laneKey + QLatin1String("-text"), laneKey, QStringLiteral("text"),
              {}, {}, {}, inheritAll, {},
              QStringLiteral("font-weight:700")});
      }
      for (qsizetype box = 0; box < probe.boxes.size(); ++box) {
        const QString boxKey = QStringLiteral("box-%1").arg(box);
        push({boxKey, QStringLiteral("svg"), QStringLiteral("g"), {},
              {QStringLiteral("em-box")}, {}, inheritAll, {}});
        push({boxKey + QLatin1String("-rect"), boxKey, QStringLiteral("rect"),
              {}, {}, {}, inheritAll, {},
              QStringLiteral("fill:%1;stroke:%2")
                  .arg(probe.boxes.at(box).fill, probe.boxes.at(box).stroke)});
        push({boxKey + QLatin1String("-fo"), boxKey,
              QStringLiteral("foreignobject"), {}, {}, {}, inheritAll, {}});
        push({boxKey + QLatin1String("-div"), boxKey + QLatin1String("-fo"),
              QStringLiteral("div"), {}, {}, {}, inheritAll,
              QStringLiteral("display:table;height:100%;width:100%")});
        push({boxKey + QLatin1String("-span"), boxKey + QLatin1String("-div"),
              QStringLiteral("span"), {}, {}, {}, inheritAll,
              QStringLiteral(
                  "display:table-cell;text-align:center;"
                  "vertical-align:middle")});
      }
      for (qsizetype relation = 0; relation < probe.relations.size();
           ++relation) {
        push({QStringLiteral("relation-%1").arg(relation),
              QStringLiteral("svg"), QStringLiteral("path"), {},
              {QStringLiteral("em-relation")}, {}, inheritAll, {},
              QStringLiteral("fill:none;stroke:%1;stroke-width:1")
                  .arg(style.relationStroke)});
      }
      push({QStringLiteral("defs"), QStringLiteral("svg"),
            QStringLiteral("defs"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("marker"), QStringLiteral("defs"),
            QStringLiteral("marker"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("polygon"), QStringLiteral("marker"),
            QStringLiteral("polygon"), {}, {}, {}, inheritAll, {},
            QStringLiteral("fill:%1").arg(style.arrowhead)});

      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, QString());
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, style.rootFontSize);
      const auto convert = [&](const QString& key) {
        eventmodeling::EventModelingElementCss out;
        const ElementStyle& resolved = css.value(key);
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        out.color = resolved.color;
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = cssFontSizePx(resolved.fontSize, familyCtx);
        out.fontWeight = resolved.fontWeight;
        out.fontStyle = resolved.fontStyle;
        out.opacity = resolved.effectiveOpacity;
        out.visible = resolved.displayed();
        out.measures = resolved.display.compare(
                            QStringLiteral("none"), Qt::CaseInsensitive) != 0;
        return out;
      };
      overrides.active = true;
      for (qsizetype lane = 0; lane < probe.swimlanes.size(); ++lane) {
        eventmodeling::EventModelingCssOverrides::Swimlane slot;
        const QString laneKey = QStringLiteral("lane-%1").arg(lane);
        slot.rect = convert(laneKey + QLatin1String("-rect"));
        slot.text = convert(laneKey + QLatin1String("-text"));
        overrides.swimlanes.append(std::move(slot));
      }
      for (qsizetype box = 0; box < probe.boxes.size(); ++box) {
        eventmodeling::EventModelingCssOverrides::Box slot;
        const QString boxKey = QStringLiteral("box-%1").arg(box);
        slot.rect = convert(boxKey + QLatin1String("-rect"));
        slot.label = convert(boxKey + QLatin1String("-span"));
        overrides.boxes.append(std::move(slot));
      }
      for (qsizetype relation = 0; relation < probe.relations.size();
           ++relation)
        overrides.relations.append(
            convert(QStringLiteral("relation-%1").arg(relation)));
      overrides.marker = convert(QStringLiteral("polygon"));
    }

    eventmodeling::EventModelingScene scene =
        eventmodeling::buildEventModelingScene(
            data, std::move(config), std::move(style),
            themeCssActive ? &overrides : nullptr);
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), scene.style.textColor,
        scene.style.fontFamily, scene.style.rootFontSize);
    // The registered Event Modeling grammar does not expose common metadata.
    // Frontmatter and metadata-looking statements therefore never create a
    // visible title strip or accessible SVG title/description elements.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene = std::make_shared<const eventmodeling::EventModelingScene>(
        std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& eventModelingDiagramAdapter() {
  static const EventModelingDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
