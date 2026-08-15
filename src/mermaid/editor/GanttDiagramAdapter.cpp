#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/gantt/GanttDiagram.h"
#include "mermaid/gantt/GanttScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <memory>

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

qreal number(const QJsonObject& object, const char* key, qreal fallback) {
  return qreal(jsNumberValue(scalar(object, key, QJsonValue(fallback))));
}

bool boolean(const QJsonObject& object, const char* key, bool fallback) {
  return truthyConfigValue(scalar(object, key, QJsonValue(fallback)));
}

QString string(const QJsonObject& object, const char* key,
               const QString& fallback = {}) {
  const QJsonValue value = scalar(object, key, QJsonValue(fallback));
  return value.isString() ? value.toString() : fallback;
}

// Live subset of gantt getStyles (Mermaid 11.16.0), in sheet order so
// same-specificity conflicts resolve exactly as upstream (user themeCSS is
// appended after and wins those ties). Dead rules are dropped: the
// `.grid .tick { stroke }` declaration lands on the tick <g> where nothing
// paints, `.milestone { transform }` is geometry the builder owns, and
// `.mermaid-main-font` matches no element.
QString ganttBaseCss(const gantt::GanttSceneStyle& style) {
  const QString family = style.fontFamily;
  QString css = QStringLiteral(
                     ".exclude-range { fill: %1; }\n"
                     ".section { stroke: none; opacity: 0.2; }\n"
                     ".section0 { fill: %2; }\n"
                     ".section2 { fill: %3; }\n"
                     ".section1, .section3 { fill: %4; opacity: 0.2; }\n"
                     ".sectionTitle0 { fill: %5; }\n"
                     ".sectionTitle1 { fill: %5; }\n"
                     ".sectionTitle2 { fill: %5; }\n"
                     ".sectionTitle3 { fill: %5; }\n"
                     ".sectionTitle { text-anchor: start; font-family: %6; }\n"
                     ".grid .tick { stroke: %7; opacity: 0.8; }\n"
                     ".grid .tick text { font-family: %6; fill: %8; }\n"
                     ".grid path { stroke-width: 0; }\n"
                     ".today { fill: none; stroke: %9; stroke-width: 2px; }\n"
                     ".task { stroke-width: 2; }\n"
                     ".taskText { text-anchor: middle; font-family: %6; }\n"
                     ".taskTextOutsideRight { fill: %10; text-anchor: start; font-family: %6; }\n"
                     ".taskTextOutsideLeft { fill: %10; text-anchor: end; }\n")
                    .arg(style.excludeBkgColor, style.sectionBkgColor,
                         style.sectionBkgColor2, style.altSectionBkgColor,
                         style.titleColor, family, style.gridColor,
                         style.textColor, style.todayLineColor,
                         style.taskTextDarkColor);
  css += QStringLiteral(
             ".taskText0, .taskText1, .taskText2, .taskText3 { fill: %1; }\n"
             ".task0, .task1, .task2, .task3 { fill: %2; stroke: %3; }\n"
             ".taskTextOutside0, .taskTextOutside1, .taskTextOutside2, .taskTextOutside3 { fill: %4; }\n"
             ".active0, .active1, .active2, .active3 { fill: %5; stroke: %6; }\n"
             ".activeText0, .activeText1, .activeText2, .activeText3 { fill: %7 !important; }\n"
             ".done0, .done1, .done2, .done3 { stroke: %8; fill: %9; stroke-width: 2; }\n"
             ".doneText0, .doneText1, .doneText2, .doneText3 { fill: %7 !important; }\n")
             .arg(style.taskTextColor, style.taskBkgColor, style.taskBorderColor,
                  style.taskTextOutsideColor, style.activeTaskBkgColor,
                  style.activeTaskBorderColor, style.taskTextDarkColor,
                  style.doneTaskBorderColor, style.doneTaskBkgColor);
  // Done-task text outside the bar sits against the diagram background, not
  // the done-task bar, so it uses the outside/contrast color.
  for (int i = 0; i < 4; ++i) {
    css += QStringLiteral(".doneText%1.taskTextOutsideLeft, .doneText%1.taskTextOutsideRight { fill: %2 !important; }\n")
               .arg(QString::number(i), style.taskTextOutsideColor);
  }
  css += QStringLiteral(
             ".crit0, .crit1, .crit2, .crit3 { stroke: %1; fill: %2; stroke-width: 2; }\n"
             ".activeCrit0, .activeCrit1, .activeCrit2, .activeCrit3 { stroke: %1; fill: %3; stroke-width: 2; }\n"
             ".doneCrit0, .doneCrit1, .doneCrit2, .doneCrit3 { stroke: %1; fill: %4; stroke-width: 2; }\n"
             ".milestoneText { font-style: italic; }\n"
             ".doneCritText0, .doneCritText1, .doneCritText2, .doneCritText3 { fill: %5 !important; }\n")
             .arg(style.critBorderColor, style.critBkgColor,
                  style.activeTaskBkgColor, style.doneTaskBkgColor,
                  style.taskTextDarkColor);
  for (int i = 0; i < 4; ++i) {
    css += QStringLiteral(".doneCritText%1.taskTextOutsideLeft, .doneCritText%1.taskTextOutsideRight { fill: %2 !important; }\n")
               .arg(QString::number(i), style.taskTextOutsideColor);
  }
  css += QStringLiteral(
             ".vert { stroke: %1; }\n"
             ".vertText { font-size: 15px; text-anchor: middle; fill: %1 !important; }\n"
             ".activeCritText0, .activeCritText1, .activeCritText2, .activeCritText3 { fill: %2 !important; }\n"
             ".titleText { text-anchor: middle; font-size: 18px; fill: %3; font-family: %4; }\n")
             .arg(style.vertLineColor, style.taskTextDarkColor,
                  style.titleColor.isEmpty() ? style.textColor : style.titleColor,
                  family);
  // Clickable task links (only emitted when the source declares click
  // statements).
  css += QStringLiteral(
             ".task.clickable { cursor: pointer; }\n"
             ".taskText.clickable { cursor: pointer; fill: %1 !important; font-weight: bold; }\n"
             ".taskTextOutsideLeft.clickable { cursor: pointer; fill: %1 !important; font-weight: bold; }\n"
             ".taskTextOutsideRight.clickable { cursor: pointer; fill: %1 !important; font-weight: bold; }\n")
             .arg(style.taskTextClickableColor);
  return css;
}

struct GanttDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("gantt")}; }
  QString cssClass() const override { return QStringLiteral("gantt"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    gantt::GanttData data = gantt::GanttDiagram::parse(pre.code);
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);

    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme
                                                              : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject raw = pre.config.value(QStringLiteral("gantt")).toObject();

    gantt::GanttConfig config;
    config.useMaxWidth = boolean(raw, "useMaxWidth", true);
    config.useWidth = number(raw, "useWidth", 1200.0);
    config.titleTopMargin = number(raw, "titleTopMargin", 25.0);
    config.barHeight = number(raw, "barHeight", 20.0);
    config.barGap = number(raw, "barGap", 4.0);
    config.topPadding = number(raw, "topPadding", 50.0);
    config.rightPadding = number(raw, "rightPadding", 75.0);
    config.leftPadding = number(raw, "leftPadding", 75.0);
    config.gridLineStartPadding = number(raw, "gridLineStartPadding", 35.0);
    config.fontSize = number(raw, "fontSize", 11.0);
    config.sectionFontSize = number(raw, "sectionFontSize", 11.0);
    config.numberSectionStyles = std::max(1, int(number(raw, "numberSectionStyles", 4.0)));
    config.axisFormat = string(raw, "axisFormat", QStringLiteral("%Y-%m-%d"));
    config.tickInterval = string(raw, "tickInterval");
    config.topAxis = boolean(raw, "topAxis", false);
    config.displayMode = string(raw, "displayMode");
    config.weekday = string(raw, "weekday", QStringLiteral("sunday"));

    gantt::GanttSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext rootContext =
        pieCssLengthContext(firstFontFamily(style.fontFamily), 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, rootContext);
    style.textColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    style.sectionBkgColor = themeVars.sectionBkgColor;
    style.altSectionBkgColor = themeVars.altSectionBkgColor;
    style.sectionBkgColor2 = themeVars.sectionBkgColor2;
    style.excludeBkgColor = themeVars.excludeBkgColor;
    style.taskBorderColor = themeVars.taskBorderColor;
    style.taskBkgColor = themeVars.taskBkgColor;
    style.taskTextColor = themeVars.taskTextColor;
    style.taskTextDarkColor = themeVars.taskTextDarkColor;
    style.taskTextOutsideColor = themeVars.taskTextOutsideColor;
    style.taskTextClickableColor = themeVars.taskTextClickableColor;
    style.activeTaskBorderColor = themeVars.activeTaskBorderColor;
    style.activeTaskBkgColor = themeVars.activeTaskBkgColor;
    style.gridColor = themeVars.gridColor;
    style.doneTaskBkgColor = themeVars.doneTaskBkgColor;
    style.doneTaskBorderColor = themeVars.doneTaskBorderColor;
    style.critBorderColor = themeVars.critBorderColor;
    style.critBkgColor = themeVars.critBkgColor;
    style.todayLineColor = themeVars.todayLineColor;
    style.vertLineColor = themeVars.vertLineColor;

    // themeCSS: resolve the user sheet against a faithful model of the gantt
    // DOM (svg > style, scaffold g, excludes g, g.grid with the d3 axis,
    // section-rect g, task g — all rects before all texts —, section-title g,
    // today g, title text). Base rules come first so user rules only win
    // through importance or sheet order, exactly like upstream.
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    gantt::GanttCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      const gantt::GanttPreparedLayout layout =
          gantt::ganttPrepareLayout(data, config);
      const QString baseCss = ganttBaseCss(style);
      // Mermaid paints `#id { font-family; font-size; fill: textColor }` on
      // the svg root; user `svg {}` rules are stylis-scoped to `#id svg` and
      // never reach it. Nothing upstream sets `color`, so it stays initial
      // black — the value currentColor resolves to on the axis strokes.
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

      // Pass 1 — the task-label measurement runs on a classless probe
      // <text> carrying only the font-size presentation attribute, so tag
      // and ancestor selectors feed it while .taskText rules cannot.
      const QHash<QString, ElementStyle> skeleton = csscascade::resolveElements(
          themeCss,
          {ElementInput{QStringLiteral("svg"), {}, QStringLiteral("svg"),
                        QStringLiteral("diagram-root"), {}, {}, rootStyle, {}},
           ElementInput{QStringLiteral("measure"), QStringLiteral("svg"),
                        QStringLiteral("text"), {}, {}, {}, inheritAll, {},
                        QStringLiteral("font-size:%1px")
                            .arg(QString::number(config.fontSize))}},
          baseCss);
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, style.rootFontSize);
      const auto fontSizePx = [&familyCtx](const QString& css) {
        return cssFontSizePx(css, familyCtx);
      };
      overrides.active = true;
      const ElementStyle& measured =
          skeleton.value(QStringLiteral("measure"));
      overrides.measureText.fontFamily = firstFontFamily(
          measured.fontFamily.isEmpty() ? style.fontFamily : measured.fontFamily);
      overrides.measureText.fontSize = fontSizePx(measured.fontSize);

      // Pass 2 — the full DOM, siblings in document order. Parents always
      // precede their children in the input vector.
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

      if (!data.excludes.isEmpty() || !data.includes.isEmpty()) {
        push({QStringLiteral("excludes"), QStringLiteral("svg"),
              QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
        for (qsizetype run = 0; run < layout.excludeRuns.size(); ++run)
          push({QStringLiteral("exclude-%1").arg(run),
                QStringLiteral("excludes"), QStringLiteral("rect"), {},
                {QStringLiteral("exclude-range")}, {}, inheritAll, {}});
      }
      // d3 stamps fill/font-size/font-family/text-anchor presentation
      // attributes on the axis <g> itself; they inherit down to anything the
      // per-element attributes do not already pin.
      push({QStringLiteral("grid"), QStringLiteral("svg"), QStringLiteral("g"),
            {}, {QStringLiteral("grid")}, {}, inheritAll, {},
            QStringLiteral("fill:none;font-size:10px;"
                           "font-family:sans-serif;text-anchor:middle")});
      push({QStringLiteral("domain"), QStringLiteral("grid"),
            QStringLiteral("path"), {}, {QStringLiteral("domain")}, {},
            inheritAll, {}, QStringLiteral("stroke:currentColor")});
      for (int axis = 0;
           axis < (data.topAxis || config.topAxis ? 2 : 1); ++axis) {
        for (qsizetype tick = 0; tick < layout.ticks.size(); ++tick) {
          const QString tickKey =
              QStringLiteral("tick-%1-%2").arg(axis).arg(tick);
          push({tickKey, QStringLiteral("grid"), QStringLiteral("g"), {},
                {QStringLiteral("tick")}, {}, inheritAll, {},
                QStringLiteral("opacity:1")});
          push({tickKey + QLatin1String("-line"), tickKey,
                QStringLiteral("line"), {}, {}, {}, inheritAll, {},
                QStringLiteral("stroke:currentColor")});
          push({tickKey + QLatin1String("-text"), tickKey,
                QStringLiteral("text"), {}, {}, {}, inheritAll,
                QStringLiteral("text-anchor:middle"),
                QStringLiteral("fill:#000;stroke:none;font-size:10px")});
        }
      }
      push({QStringLiteral("sections"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      for (qsizetype row = 0; row < layout.uniqueOrders.size(); ++row) {
        const auto found = std::find_if(
            layout.tasks.cbegin(), layout.tasks.cend(),
            [&layout, row](const gantt::GanttTask& task) {
              return !task.vert && task.order == layout.uniqueOrders.at(row);
            });
        const int category = int(std::max<qsizetype>(
            0, layout.categories.indexOf(found->type)));
        push({QStringLiteral("section-%1").arg(row), QStringLiteral("sections"),
              QStringLiteral("rect"), {},
              {QStringLiteral("section"),
               QStringLiteral("section%1").arg(
                   category % std::max(1, config.numberSectionStyles))},
              {}, inheritAll, {}});
      }
      push({QStringLiteral("tasks"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      // All task rects precede all task texts (upstream appends the rect
      // pass before the text pass), so structural selectors see the same
      // sibling order as the browser.
      for (qsizetype index = 0; index < layout.tasks.size(); ++index) {
        const gantt::GanttTask& task = layout.tasks.at(index);
        int secNum = 0;
        for (int i = 0; i < layout.categories.size(); ++i)
          if (task.type == layout.categories.at(i))
            secNum = i % std::max(1, config.numberSectionStyles);
        push({QStringLiteral("task-%1-rect").arg(index),
              QStringLiteral("tasks"), QStringLiteral("rect"),
              QStringLiteral("diagram-root-%1").arg(task.id),
              gantt::ganttTaskRectClass(task, secNum)
                  .split(QLatin1Char(' '), Qt::SkipEmptyParts),
              {}, inheritAll, {}});
      }
      for (qsizetype index = 0; index < layout.tasks.size(); ++index) {
        const gantt::GanttTask& task = layout.tasks.at(index);
        int secNum = 0;
        for (int i = 0; i < layout.categories.size(); ++i)
          if (task.type == layout.categories.at(i))
            secNum = i % std::max(1, config.numberSectionStyles);
        const gantt::GanttTaskTextPlacement placement =
            gantt::ganttTaskTextPlacement(task, layout, config,
                                          overrides.measureText.fontFamily,
                                          overrides.measureText.fontSize);
        const QString classes = gantt::ganttTaskTextClass(
            task, secNum, placement.textWidth, placement.outside,
            placement.outsideLeft);
        push({QStringLiteral("task-%1-text").arg(index),
              QStringLiteral("tasks"), QStringLiteral("text"),
              QStringLiteral("diagram-root-%1-text").arg(task.id),
              classes.split(QLatin1Char(' '), Qt::SkipEmptyParts), {},
              inheritAll, {},
              QStringLiteral("font-size:%1px")
                  .arg(QString::number(config.fontSize))});
      }
      push({QStringLiteral("sectionTitles"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      for (int category = 0; category < layout.categories.size(); ++category) {
        push({QStringLiteral("sectionTitle-%1").arg(category),
              QStringLiteral("sectionTitles"), QStringLiteral("text"), {},
              {QStringLiteral("sectionTitle"),
               QStringLiteral("sectionTitle%1").arg(
                   category % std::max(1, config.numberSectionStyles))},
              {}, inheritAll, {},
              QStringLiteral("font-size:%1px")
                  .arg(QString::number(config.sectionFontSize))});
      }
      if (data.todayMarker != QLatin1String("off")) {
        push({QStringLiteral("todayG"), QStringLiteral("svg"),
              QStringLiteral("g"), {}, {QStringLiteral("today")}, {},
              inheritAll, {}});
        push({QStringLiteral("todayLine"), QStringLiteral("todayG"),
              QStringLiteral("line"), {}, {QStringLiteral("today")}, {},
              inheritAll, {}});
      }
      push({QStringLiteral("titleText"), QStringLiteral("svg"),
            QStringLiteral("text"), {}, {QStringLiteral("titleText")}, {},
            inheritAll, {}});

      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, baseCss);
      const auto convert = [&](const QString& key) {
        gantt::GanttElementCss out;
        const ElementStyle& resolved = css.value(key);
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        // Empty resolved font-family means no rule or inherit keyword
        // reached the element — keep the root font chain.
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = fontSizePx(resolved.fontSize);
        out.fontWeight = resolved.fontWeight;
        out.fontStyle = resolved.fontStyle;
        out.textAnchor = resolved.textAnchor;
        out.opacity = resolved.effectiveOpacity;
        out.visible = resolved.displayed();
        out.measures = resolved.display.compare(
                            QStringLiteral("none"), Qt::CaseInsensitive) != 0;
        return out;
      };
      overrides.gridDomain = convert(QStringLiteral("domain"));
      overrides.today = convert(QStringLiteral("todayLine"));
      for (qsizetype run = 0; run < layout.excludeRuns.size(); ++run)
        overrides.excludes.append(
            convert(QStringLiteral("exclude-%1").arg(run)));
      for (int axis = 0;
           axis < (data.topAxis || config.topAxis ? 2 : 1); ++axis) {
        for (qsizetype tick = 0; tick < layout.ticks.size(); ++tick) {
          const QString tickKey =
              QStringLiteral("tick-%1-%2").arg(axis).arg(tick);
          gantt::GanttCssOverrides::Tick resolved;
          resolved.line = convert(tickKey + QLatin1String("-line"));
          resolved.text = convert(tickKey + QLatin1String("-text"));
          overrides.ticks.append(std::move(resolved));
        }
      }
      for (qsizetype row = 0; row < layout.uniqueOrders.size(); ++row)
        overrides.sections.append(
            convert(QStringLiteral("section-%1").arg(row)));
      for (qsizetype index = 0; index < layout.tasks.size(); ++index) {
        gantt::GanttCssOverrides::Task resolved;
        resolved.rect = convert(QStringLiteral("task-%1-rect").arg(index));
        resolved.text = convert(QStringLiteral("task-%1-text").arg(index));
        overrides.tasks.append(std::move(resolved));
      }
      for (int category = 0; category < layout.categories.size(); ++category)
        overrides.sectionTitles.append(
            convert(QStringLiteral("sectionTitle-%1").arg(category)));
      overrides.title = convert(QStringLiteral("titleText"));
    }

    gantt::GanttScene scene = gantt::buildGanttScene(
        data, std::move(config), std::move(style),
        themeCssActive ? &overrides : nullptr);
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        themeVars.titleColor, scene.style.fontFamily, scene.style.rootFontSize);
    // Gantt owns its visible diagram title; the shared metadata title is only
    // used for SVG accessibility and must not add another 40px title strip.
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.config.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(std::max(1, qRound(scene.bounds.width())),
                              std::max(1, qRound(scene.bounds.height())));
    entry.scene = std::make_shared<const gantt::GanttScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& ganttDiagramAdapter() {
  static const GanttDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
