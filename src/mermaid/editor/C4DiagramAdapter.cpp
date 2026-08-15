#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/c4/C4Diagram.h"
#include "mermaid/c4/C4Scene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>

#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue valueOr(const QJsonObject& object, const QString& key,
                   const QJsonValue& fallback) {
  const QJsonValue value = object.value(key);
  return value.isUndefined() || value.isNull() ? fallback : value;
}

qreal numberOr(const QJsonObject& object, const QString& key, qreal fallback) {
  const qreal value = jsNumberValue(valueOr(object, key, fallback));
  return std::isfinite(value) ? value : fallback;
}

QString stringOr(const QJsonObject& object, const QString& key,
                 const QString& fallback) {
  const QJsonValue value = valueOr(object, key, fallback);
  return value.isString() ? value.toString() : value.toVariant().toString();
}

const QStringList& shapeTypes() {
  static const QStringList values = {
      QStringLiteral("person"), QStringLiteral("external_person"),
      QStringLiteral("system"), QStringLiteral("external_system"),
      QStringLiteral("system_db"), QStringLiteral("external_system_db"),
      QStringLiteral("system_queue"), QStringLiteral("external_system_queue"),
      QStringLiteral("container"), QStringLiteral("external_container"),
      QStringLiteral("container_db"), QStringLiteral("external_container_db"),
      QStringLiteral("container_queue"), QStringLiteral("external_container_queue"),
      QStringLiteral("component"), QStringLiteral("external_component"),
      QStringLiteral("component_db"), QStringLiteral("external_component_db"),
      QStringLiteral("component_queue"), QStringLiteral("external_component_queue")};
  return values;
}

void addDefaultColors(c4::C4Config& config) {
  const auto add = [&](const QString& type, const QString& background,
                       const QString& border) {
    config.backgroundColors.insert(type, background);
    config.borderColors.insert(type, border);
  };
  add(QStringLiteral("person"), QStringLiteral("#08427B"), QStringLiteral("#073B6F"));
  add(QStringLiteral("external_person"), QStringLiteral("#686868"), QStringLiteral("#8A8A8A"));
  for (const QString& suffix : {QString(), QStringLiteral("_db"), QStringLiteral("_queue")}) {
    add(QStringLiteral("system") + suffix, QStringLiteral("#1168BD"), QStringLiteral("#3C7FC0"));
    add(QStringLiteral("external_system") + suffix, QStringLiteral("#999999"), QStringLiteral("#8A8A8A"));
    add(QStringLiteral("container") + suffix, QStringLiteral("#438DD5"), QStringLiteral("#3C7FC0"));
    add(QStringLiteral("external_container") + suffix, QStringLiteral("#B3B3B3"), QStringLiteral("#A6A6A6"));
    add(QStringLiteral("component") + suffix, QStringLiteral("#85BBF0"), QStringLiteral("#78A8D8"));
    add(QStringLiteral("external_component") + suffix, QStringLiteral("#CCCCCC"), QStringLiteral("#BFBFBF"));
  }
}

c4::C4Config c4Config(const QJsonObject& raw) {
  c4::C4Config config;
  config.useMaxWidth = truthyConfigValue(valueOr(raw, QStringLiteral("useMaxWidth"), true));
  config.diagramMarginX = numberOr(raw, QStringLiteral("diagramMarginX"), 50.0);
  config.diagramMarginY = numberOr(raw, QStringLiteral("diagramMarginY"), 10.0);
  config.c4ShapeMargin = numberOr(raw, QStringLiteral("c4ShapeMargin"), 50.0);
  config.c4ShapePadding = numberOr(raw, QStringLiteral("c4ShapePadding"), 20.0);
  config.width = numberOr(raw, QStringLiteral("width"), 216.0);
  config.height = numberOr(raw, QStringLiteral("height"), 60.0);
  config.boxMargin = numberOr(raw, QStringLiteral("boxMargin"), 10.0);
  config.c4ShapeInRow = qMax(1, qRound(numberOr(raw, QStringLiteral("c4ShapeInRow"), 4.0)));
  config.nextLinePaddingX = numberOr(raw, QStringLiteral("nextLinePaddingX"), 0.0);
  config.c4BoundaryInRow = qMax(1, qRound(numberOr(raw, QStringLiteral("c4BoundaryInRow"), 2.0)));
  config.wrap = truthyConfigValue(valueOr(raw, QStringLiteral("wrap"), true));
  config.wrapPadding = numberOr(raw, QStringLiteral("wrapPadding"), 10.0);
  addDefaultColors(config);
  for (const QString& type : shapeTypes()) {
    c4::C4Font font;
    font.family = stringOr(raw, type + QStringLiteral("FontFamily"),
                           QStringLiteral("\"Open Sans\", sans-serif"));
    font.size = numberOr(raw, type + QStringLiteral("FontSize"), 14.0);
    font.weight = stringOr(raw, type + QStringLiteral("FontWeight"),
                           QStringLiteral("normal"));
    config.fonts.insert(type, font);
    config.backgroundColors[type] = stringOr(
        raw, type + QStringLiteral("_bg_color"), config.backgroundColors.value(type));
    config.borderColors[type] = stringOr(
        raw, type + QStringLiteral("_border_color"), config.borderColors.value(type));
  }
  c4::C4Font boundary;
  boundary.family = stringOr(raw, QStringLiteral("boundaryFontFamily"),
                             QStringLiteral("\"Open Sans\", sans-serif"));
  boundary.size = numberOr(raw, QStringLiteral("boundaryFontSize"), 14.0);
  boundary.weight = stringOr(raw, QStringLiteral("boundaryFontWeight"),
                             QStringLiteral("normal"));
  config.fonts.insert(QStringLiteral("boundary"), boundary);
  c4::C4Font message;
  message.family = stringOr(raw, QStringLiteral("messageFontFamily"),
                            QStringLiteral("\"Open Sans\", sans-serif"));
  message.size = numberOr(raw, QStringLiteral("messageFontSize"), 12.0);
  message.weight = stringOr(raw, QStringLiteral("messageFontWeight"),
                            QStringLiteral("normal"));
  config.fonts.insert(QStringLiteral("message"), message);
  return config;
}

// c4's getStyles is a single `.person { stroke; fill }` rule. The renderer
// hardcodes class "person-man" on every shape group, so the selector matches
// nothing in the DOM — dead upstream and dead here. The personBorder /
// personBkg themeVariables pair behind it derives like every other theme
// (primaryBorderColor / mainBkg) and is wired here so the sheet matches the
// active theme instead of pinned default-theme literals.
QString c4BaseCss(const flowtheme::FlowThemeVariables& themeVars) {
  return QStringLiteral(".person { stroke: %1; fill: %2; }\n")
      .arg(themeVars.personBorder, themeVars.personBkg);
}

// The inline style byTspan paints on every label line: only !important
// themeCSS declarations can beat it. Presentation attributes (fill etc.)
// sit below every author rule.
QString c4TextInlineStyle(const c4::C4Font& font, qreal sizeAdjust,
                          const QString& weightOverride) {
  const QString weight = !weightOverride.isEmpty() ? weightOverride
                                                   : font.weight;
  return QStringLiteral(
             "text-anchor: middle; font-size: %1px; font-weight: %2; "
             "font-family: %3")
      .arg(QString::number(font.size + sizeAdjust), weight, font.family);
}

struct C4DiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("c4")}; }
  QString cssClass() const override { return QStringLiteral("c4"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    c4::C4Data data = c4::C4Diagram::parse(
        pre.code,
        truthyConfigValue(valueOr(pre.config, QStringLiteral("wrap"), false)));
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);
    c4::C4Config config = c4Config(pre.config.value(QStringLiteral("c4")).toObject());
    c4::C4SceneStyle style;
    style.rootFontFamily = themeVars.fontFamily;
    style.rootFontSize = cssFontSizePx(
        themeVars.fontSize, pieCssLengthContext(style.rootFontFamily, 16.0));
    style.rootFontWeight = themeVars.fontWeight;
    style.rootTextColor = themeVars.textColor;

    // themeCSS: resolve the user sheet against a faithful model of the c4
    // DOM (empty scaffold group, three icon defs, the draw recursion —
    // global shapes, then per boundary: nested shapes, nested boundaries,
    // own group, with the synthetic global boundary never drawing itself —
    // four marker defs, the relations group and the title). c4 measures
    // through the config fonts, so the resolved values only ever repaint.
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    c4::C4CssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      // Mermaid paints `#id { font-family; font-size; fill: textColor }`
      // directly on the svg element; `svg {}` user rules are scoped to
      // `#id svg` and never reach the root itself.
      ElementStyle rootStyle;
      rootStyle.fill = style.rootTextColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.rootFontFamily;
      rootStyle.fontSize =
          QString::number(style.rootFontSize) + QStringLiteral("px");
      rootStyle.fontWeight = QStringLiteral("400");
      // Empty members inherit the parent's resolved value via project().
      const ElementStyle inheritAll;

      QVector<ElementInput> tree;
      const auto push = [&tree](ElementInput input) {
        tree.append(std::move(input));
      };
      push({QStringLiteral("svg"), {}, QStringLiteral("svg"),
            QStringLiteral("diagram-root"), {}, {}, rootStyle, {}});
      push({QStringLiteral("styleEl"), QStringLiteral("svg"),
            QStringLiteral("style"), {}, {}, {}, inheritAll, {}});
      // Mermaid's render loop leaves an empty scaffold group first; every
      // structural :nth-of-type index counts it.
      push({QStringLiteral("scaffold"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      // Icon symbols: defined for every c4 render, never referenced.
      const QStringList icons{QStringLiteral("computer"),
                              QStringLiteral("database"),
                              QStringLiteral("clock")};
      for (int i = 0; i < icons.size(); ++i) {
        const QString defsKey = QStringLiteral("defs-ic%1").arg(i);
        const QString symKey = QStringLiteral("sym%1").arg(i);
        push({defsKey, QStringLiteral("svg"), QStringLiteral("defs"), {}, {},
              {}, inheritAll, {}});
        push({symKey, defsKey, QStringLiteral("symbol"), {}, {}, {}, inheritAll,
              {}});
        push({QStringLiteral("symp%1").arg(i), symKey, QStringLiteral("path"),
              {}, {}, {}, inheritAll, {}});
      }

      const auto font = [&config](const QString& type) {
        return config.fonts.value(type);
      };
      const c4::C4Font boundaryFont = font(QStringLiteral("boundary"));
      const c4::C4Font messageFont = font(QStringLiteral("message"));

      // Shapes: `g.person-man` groups appended straight to the svg root.
      const auto emitShape = [&](qsizetype index) {
        const c4::C4Element& shape = data.shapes.at(index);
        const QString type = shape.typeC4Shape;
        const c4::C4Font shapeFont = font(type);
        const QString fontColor =
            shape.fontColor.value_or(QStringLiteral("#FFFFFF"));
        const QString fill =
            shape.backgroundColor.value_or(config.backgroundColors.value(
                type, QStringLiteral("#1168BD")));
        const QString stroke =
            shape.borderColor.value_or(config.borderColors.value(
                type, QStringLiteral("#3C7FC0")));
        const QString grp = QStringLiteral("sh%1-grp").arg(index);
        push({grp, QStringLiteral("svg"), QStringLiteral("g"), {},
              {QStringLiteral("person-man")}, {}, inheritAll, {}});
        const bool database = type.endsWith(QLatin1String("_db"));
        const bool queue = type.endsWith(QLatin1String("_queue"));
        if (!database && !queue) {
          push({QStringLiteral("sh%1-body").arg(index), grp,
                QStringLiteral("rect"), {}, {},
                {}, inheritAll, {},
                QStringLiteral("fill:%1;stroke:%2;stroke-width:0.5px")
                    .arg(fill, stroke)});
        } else {
          push({QStringLiteral("sh%1-body").arg(index), grp,
                QStringLiteral("path"), {}, {}, {}, inheritAll, {},
                QStringLiteral("fill:%1;stroke:%2;stroke-width:0.5px")
                    .arg(fill, stroke)});
          push({QStringLiteral("sh%1-lip").arg(index), grp,
                QStringLiteral("path"), {}, {}, {}, inheritAll, {},
                QStringLiteral("fill:none;stroke:%1;stroke-width:0.5px")
                    .arg(stroke)});
        }
        // Stereotype `<<type>>`: fill/font-family/font-size/font-style all
        // ride as presentation attributes (the textLength squeeze stays).
        push({QStringLiteral("sh%1-ster").arg(index), grp,
              QStringLiteral("text"), {}, {}, {}, inheritAll, {},
              QStringLiteral(
                  "fill:%1;font-family:%2;font-size:%3px;font-style:italic")
                  .arg(fontColor, shapeFont.family,
                       QString::number(shapeFont.size - 2.0))});
        if (type == QLatin1String("person") ||
            type == QLatin1String("external_person")) {
          push({QStringLiteral("sh%1-img").arg(index), grp,
                QStringLiteral("image"), {}, {}, {}, inheritAll, {}});
        }
        const QString labelWeight = QStringLiteral("bold");
        push({QStringLiteral("sh%1-label").arg(index), grp,
              QStringLiteral("text"), {}, {}, {}, inheritAll,
              c4TextInlineStyle(shapeFont, 2.0, labelWeight),
              QStringLiteral("fill:%1").arg(fontColor)});
        if (!shape.technology.isEmpty() || !shape.type.isEmpty()) {
          push({QStringLiteral("sh%1-tech").arg(index), grp,
                QStringLiteral("text"), {}, {}, {}, inheritAll,
                c4TextInlineStyle(shapeFont, 0.0, {}),
                QStringLiteral("fill:%1;font-style:italic").arg(fontColor)});
        }
        if (!shape.description.isEmpty()) {
          // Upstream measures and draws the description with personFont.
          const c4::C4Font personFont = font(QStringLiteral("person"));
          push({QStringLiteral("sh%1-descr").arg(index), grp,
                QStringLiteral("text"), {}, {}, {}, inheritAll,
                c4TextInlineStyle(personFont, 0.0, {}),
                QStringLiteral("fill:%1").arg(fontColor)});
        }
      };

      // Boundaries: plain `g` groups; the dashed rect and the #444444 texts.
      const auto emitBoundary = [&](qsizetype index) {
        const c4::C4Element& boundary = data.boundaries.at(index);
        const QString grp = QStringLiteral("bd%1-grp").arg(index);
        push({grp, QStringLiteral("svg"), QStringLiteral("g"), {}, {}, {},
              inheritAll, {}});
        const QString fill =
            boundary.backgroundColor.value_or(QStringLiteral("none"));
        const QString stroke =
            boundary.borderColor.value_or(QStringLiteral("#444444"));
        push({QStringLiteral("bd%1-body").arg(index), grp,
              QStringLiteral("rect"), {}, {}, {}, inheritAll, {},
              QStringLiteral("fill:%1;stroke:%2;stroke-width:1px")
                  .arg(fill, stroke)});
        push({QStringLiteral("bd%1-label").arg(index), grp,
              QStringLiteral("text"), {}, {}, {}, inheritAll,
              c4TextInlineStyle(boundaryFont, 2.0,
                                QStringLiteral("bold")),
              QStringLiteral("fill:#444444")});
        if (!boundary.type.isEmpty()) {
          push({QStringLiteral("bd%1-type").arg(index), grp,
                QStringLiteral("text"), {}, {}, {}, inheritAll,
                c4TextInlineStyle(boundaryFont, 0.0, {}),
                QStringLiteral("fill:#444444")});
        }
        if (!boundary.description.isEmpty()) {
          push({QStringLiteral("bd%1-descr").arg(index), grp,
                QStringLiteral("text"), {}, {}, {}, inheritAll,
                c4TextInlineStyle(boundaryFont, -2.0, {}),
                QStringLiteral("fill:#444444")});
        }
      };

      // The upstream draw recursion: for every boundary of `parent`, first
      // its shapes, then the nested boundaries, then its own group.
      std::function<void(const QString&)> visit = [&](const QString& parent) {
        for (qsizetype i = 0; i < data.boundaries.size(); ++i) {
          const c4::C4Element& boundary = data.boundaries.at(i);
          if (boundary.parentBoundary != parent) continue;
          for (qsizetype s = 0; s < data.shapes.size(); ++s)
            if (data.shapes.at(s).parentBoundary == boundary.alias)
              emitShape(s);
          visit(boundary.alias);
          if (boundary.alias != QLatin1String("global")) emitBoundary(i);
        }
      };
      visit(QString());

      // Marker defs: arrowhead, arrowend, crosshead (two paths) and
      // filled-head. Only arrowhead/arrowend are ever referenced; the
      // crosshead paths carry explicit black fills.
      struct MarkerSpec {
        const char* key;
        const char* presentation;
        const char* inlineStyle;
      };
      const MarkerSpec markerSpecs[] = {
          {"mk-arrowhead", "", ""},
          {"mk-arrowend", "", ""},
          {"mk-cross1",
           "fill:black;stroke:#000000;stroke-width:1px",
           "stroke-dasharray: 0, 0"},
          {"mk-cross2",
           "fill:none;stroke:#000000;stroke-width:1px",
           "stroke-dasharray: 0, 0"},
          {"mk-filled", "", ""},
      };
      for (int m = 0; m < 5; ++m) {
        const QString defsKey = QStringLiteral("defs-m%1").arg(m);
        push({defsKey, QStringLiteral("svg"), QStringLiteral("defs"), {}, {},
              {}, inheritAll, {}});
        push({QStringLiteral("mkd%1").arg(m), defsKey, QStringLiteral("marker"),
              {}, {}, {}, inheritAll, {}});
        push({QString::fromLatin1(markerSpecs[m].key),
              QStringLiteral("mkd%1").arg(m), QStringLiteral("path"), {}, {},
              {}, inheritAll,
              QString::fromLatin1(markerSpecs[m].inlineStyle),
              QString::fromLatin1(markerSpecs[m].presentation)});
      }

      // Relations: the first is a straight line, the rest quadratic paths.
      push({QStringLiteral("rels-g"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      for (qsizetype r = 0; r < data.relations.size(); ++r) {
        const c4::C4Relation& relation = data.relations.at(r);
        const QString color =
            relation.lineColor.value_or(QStringLiteral("#444444"));
        const QString textColor =
            relation.textColor.value_or(QStringLiteral("#444444"));
        const QString base = QStringLiteral("rl%1").arg(r);
        if (r == 0) {
          push({base + QStringLiteral("-body"), QStringLiteral("rels-g"),
                QStringLiteral("line"), {}, {}, {}, inheritAll,
                QStringLiteral("fill: none"),
                QStringLiteral("stroke:%1;stroke-width:1px").arg(color)});
        } else {
          push({base + QStringLiteral("-body"), QStringLiteral("rels-g"),
                QStringLiteral("path"), {}, {}, {}, inheritAll, {},
                QStringLiteral("fill:none;stroke:%1;stroke-width:1px")
                    .arg(color)});
        }
        push({base + QStringLiteral("-label"), QStringLiteral("rels-g"),
              QStringLiteral("text"), {}, {}, {}, inheritAll,
              c4TextInlineStyle(messageFont, 0.0, {}),
              QStringLiteral("fill:%1").arg(textColor)});
        if (!relation.technology.isEmpty()) {
          push({base + QStringLiteral("-tech"), QStringLiteral("rels-g"),
                QStringLiteral("text"), {}, {}, {}, inheritAll,
                c4TextInlineStyle(messageFont, 0.0, {}),
                QStringLiteral("fill:%1;font-style:italic").arg(textColor)});
        }
      }
      if (!data.title.isEmpty()) {
        push({QStringLiteral("title"), QStringLiteral("svg"),
              QStringLiteral("text"), {}, {}, {}, inheritAll, {}});
      }

      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, c4BaseCss(themeVars));
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.rootFontFamily, style.rootFontSize);
      const auto convert = [&](const ElementStyle& resolved) {
        c4::C4ElementCss out;
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        // The engine only resolves font-family through matching rules or
        // the inherit keyword; with no declaration the value stays empty
        // and the element keeps the root font chain.
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = cssFontSizePx(resolved.fontSize, familyCtx);
        out.fontWeight = resolved.fontWeight;
        out.fontStyle = resolved.fontStyle;
        out.opacity = resolved.effectiveOpacity;
        out.visible = resolved.displayed();
        out.hasBox = resolved.hasBox();
        out.measures =
            resolved.display.compare(QStringLiteral("none"),
                                      Qt::CaseInsensitive) != 0;
        return out;
      };
      overrides.active = true;
      overrides.shapes.resize(data.shapes.size());
      overrides.boundaries.resize(data.boundaries.size());
      overrides.relations.resize(data.relations.size());
      const ElementStyle none;
      const auto at = [&css, &none](const QString& key) -> const ElementStyle& {
        const auto it = css.constFind(key);
        return it == css.constEnd() ? none : it.value();
      };
      for (qsizetype i = 0; i < data.shapes.size(); ++i) {
        c4::C4CssOverrides::Shape& slot = overrides.shapes[i];
        const QString base = QStringLiteral("sh%1").arg(i);
        slot.group = convert(at(base + QStringLiteral("-grp")));
        slot.body = convert(at(base + QStringLiteral("-body")));
        slot.detail = convert(at(base + QStringLiteral("-lip")));
        slot.stereotype = convert(at(base + QStringLiteral("-ster")));
        slot.image = convert(at(base + QStringLiteral("-img")));
        slot.label = convert(at(base + QStringLiteral("-label")));
        slot.technology = convert(at(base + QStringLiteral("-tech")));
        slot.description = convert(at(base + QStringLiteral("-descr")));
      }
      for (qsizetype i = 0; i < data.boundaries.size(); ++i) {
        c4::C4CssOverrides::Boundary& slot = overrides.boundaries[i];
        const QString base = QStringLiteral("bd%1").arg(i);
        slot.group = convert(at(base + QStringLiteral("-grp")));
        slot.body = convert(at(base + QStringLiteral("-body")));
        slot.label = convert(at(base + QStringLiteral("-label")));
        slot.type = convert(at(base + QStringLiteral("-type")));
        slot.description = convert(at(base + QStringLiteral("-descr")));
      }
      for (qsizetype i = 0; i < data.relations.size(); ++i) {
        c4::C4CssOverrides::Relation& slot = overrides.relations[i];
        const QString base = QStringLiteral("rl%1").arg(i);
        slot.group = convert(at(base + QStringLiteral("-grp")));
        slot.body = convert(at(base + QStringLiteral("-body")));
        slot.label = convert(at(base + QStringLiteral("-label")));
        slot.technology = convert(at(base + QStringLiteral("-tech")));
      }
      overrides.title = convert(at(QStringLiteral("title")));
      overrides.markers = convert(at(QStringLiteral("mk-arrowhead")));
    }
    c4::C4Scene scene = c4::buildC4Scene(
        data, std::move(config), std::move(style),
        themeCssActive ? &overrides : nullptr);
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.rootTextColor, scene.style.rootFontFamily,
        scene.style.rootFontSize);
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;
    // C4 11.16's accTitle grammar action overwrites the visible diagram title
    // instead of populating commonDb. Its SVG therefore has no accessible
    // <title>/aria-labelledby fallback, while accDescr still emits normally.
    metadata.svgEmitAccessibleTitle = !data.accTitle.isEmpty();
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()), qRound(scene.bounds.height()));
    entry.scene = std::make_shared<const c4::C4Scene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& c4DiagramAdapter() {
  static const C4DiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
