#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/journey/JourneyDiagram.h"
#include "mermaid/journey/JourneyScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

QJsonValue journeyScalar(const QJsonObject& config, QLatin1String key,
                         const QJsonValue& fallback) {
  const QJsonValue value = config.value(key);
  return value.isUndefined() || value.isNull() || value.isArray() || value.isObject()
             ? fallback
             : value;
}

qreal journeyNumber(const QJsonObject& config, QLatin1String key, qreal fallback) {
  return qreal(jsNumberValue(journeyScalar(config, key, QJsonValue(fallback))));
}

// Live subset of journeyDiagram getStyles (Mermaid 11.16.0). The .node,
// .arrowheadPath, .edgePath/.flowchart-link/.edgeLabel, .cluster and tooltip
// rules never match a journey DOM element; .label text (fill:#333) is equally
// dead because no <text> lives inside a .label element. The .task-type-N /
// .section-type-N pair is emitted only while fillType0 is truthy and skips
// empty individual values, matching the template's conditional emission.
QString journeyBaseCss(const flowtheme::FlowThemeVariables& themeVars,
                       const QString& fontFamily) {
  QString css = QStringLiteral(
                    ".label { font-family: %1; color: %2; }\n"
                    ".mouth { stroke: #666; }\n"
                    "line { stroke: %2; }\n"
                    ".legend { fill: %2; font-family: %1; }\n"
                    ".face { fill: #FFF8DC; stroke: #999; }\n")
                    .arg(fontFamily, themeVars.textColor);
  if (!themeVars.fillType[0].isEmpty()) {
    for (int i = 0; i < 8; ++i) {
      const QString fill = themeVars.fillType[i];
      if (fill.isEmpty()) continue;
      css += QStringLiteral(".task-type-%1, .section-type-%1 { fill: %2; }\n")
                 .arg(QString::number(i), fill);
    }
  }
  return css;
}

QString jsCssString(const QJsonValue& value, const QString& fallback) {
  if (value.isUndefined() || value.isNull()) return fallback;
  if (value.isString()) return value.toString();
  if (value.isBool()) return value.toBool() ? QStringLiteral("true")
                                            : QStringLiteral("false");
  if (value.isDouble()) return jsNumberToString(value.toDouble());
  return value.isArray() ? QString() : QStringLiteral("[object Object]");
}

qreal svgFontSizeFromRaw(const QJsonValue& value,
                         const CssLengthContext& context) {
  if (value.isDouble()) {
    const double number = value.toDouble();
    return std::isfinite(number) && number >= 0.0
               ? std::min<qreal>(number, 10000.0)
               : context.emPx;
  }
  if (!value.isString()) return context.emPx;

  const QString text = value.toString().trimmed();
  static const QRegularExpression bareNumber(
      QStringLiteral(R"(^[+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$)"));
  if (bareNumber.match(text).hasMatch()) {
    bool ok = false;
    const double number = text.toDouble(&ok);
    return ok && std::isfinite(number)
               ? std::min<qreal>(number, 10000.0)
               : context.emPx;
  }
  return cssFontSizePx(text, context);
}

struct JourneyDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("journey")}; }
  QString cssClass() const override { return QStringLiteral("journey"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    journey::JourneyData data = journey::JourneyDiagram::parse(pre.code);
    if (data.title.isEmpty() && !pre.title.isEmpty()) data.title = pre.title;

    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject config = pre.config.value(QStringLiteral("journey")).toObject();

    journey::JourneySceneStyle style;
    style.fontFamily = firstFontFamily(themeVars.fontFamily);
    const CssLengthContext htmlRoot = pieCssLengthContext(style.fontFamily, 16.0);
    style.fontSize = cssFontSizePx(themeVars.fontSize, htmlRoot);
    style.textColor = themeVars.textColor;
    for (int i = 0; i < 8; ++i) style.fillTypes.append(themeVars.fillType[i]);

    journey::JourneyConfig journeyConfig;
    const QJsonValue useMaxWidth = config.value(QStringLiteral("useMaxWidth"));
    journeyConfig.useMaxWidth = useMaxWidth.isUndefined() || useMaxWidth.isNull()
                                    ? true
                                    : truthyConfigValue(useMaxWidth);
    journeyConfig.diagramMarginX = journeyNumber(config, QLatin1String("diagramMarginX"), 50.0);
    journeyConfig.diagramMarginY = journeyNumber(config, QLatin1String("diagramMarginY"), 10.0);
    journeyConfig.leftMargin = journeyNumber(config, QLatin1String("leftMargin"), 150.0);
    journeyConfig.maxLabelWidth = journeyNumber(config, QLatin1String("maxLabelWidth"), 360.0);
    journeyConfig.width = journeyNumber(config, QLatin1String("width"), 150.0);
    journeyConfig.height = journeyNumber(config, QLatin1String("height"), 50.0);
    journeyConfig.diagramMarginXRaw = journeyScalar(
        config, QLatin1String("diagramMarginX"), QJsonValue(50.0));
    journeyConfig.diagramMarginYRaw = journeyScalar(
        config, QLatin1String("diagramMarginY"), QJsonValue(10.0));
    journeyConfig.leftMarginRaw = journeyScalar(
        config, QLatin1String("leftMargin"), QJsonValue(150.0));
    journeyConfig.taskMarginRaw = journeyScalar(
        config, QLatin1String("taskMargin"), QJsonValue(50.0));
    const QJsonValue rawWidth = journeyScalar(
        config, QLatin1String("width"), QJsonValue(150.0));
    const QJsonValue rawHeight = journeyScalar(
        config, QLatin1String("height"), QJsonValue(50.0));
    const auto svgNumericAttribute = [](const QJsonValue& value) {
      if (value.isDouble()) return qreal(std::max(0.0, value.toDouble()));
      if (!value.isString()) return qreal(0.0);
      static const QRegularExpression number(
          QStringLiteral(R"(^\s*[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?\s*$)"));
      const qreal parsed = number.match(value.toString()).hasMatch()
                               ? qreal(value.toString().toDouble())
                               : qreal(0.0);
      return std::max<qreal>(0.0, parsed);
    };
    journeyConfig.rectWidth = svgNumericAttribute(rawWidth);
    journeyConfig.rectHeight = svgNumericAttribute(rawHeight);
    journeyConfig.boxTextMargin = journeyNumber(config, QLatin1String("boxTextMargin"), 5.0);
    const QJsonValue taskFontSize = journeyScalar(
        config, QLatin1String("taskFontSize"), QJsonValue(14.0));
    const CssLengthContext taskFontContext =
        pieCssLengthContext(style.fontFamily, style.fontSize);
    journeyConfig.taskFontSize =
        svgFontSizeFromRaw(taskFontSize, taskFontContext);
    journeyConfig.taskFontLineStep = qreal(jsNumberValue(taskFontSize));
    journeyConfig.taskFontFamily = firstFontFamily(jsCssString(
        journeyScalar(config, QLatin1String("taskFontFamily"),
                      QJsonValue(QStringLiteral("\"Open Sans\", sans-serif"))),
        QStringLiteral("\"Open Sans\", sans-serif")));
    journeyConfig.taskMargin = journeyNumber(config, QLatin1String("taskMargin"), 50.0);
    const QJsonValue textPlacement = journeyScalar(
        config, QLatin1String("textPlacement"), QJsonValue(QStringLiteral("fo")));
    journeyConfig.textPlacement = textPlacement.isUndefined() || textPlacement.isNull()
                                      ? QStringLiteral("fo")
                                      : (textPlacement.isString()
                                             ? textPlacement.toString()
                                             : QStringLiteral("__non_string__"));
    journeyConfig.titleColor = jsCssString(
        journeyScalar(config, QLatin1String("titleColor"), QJsonValue(QString())),
        QString());
    journeyConfig.titleFontFamily = firstFontFamily(jsCssString(
        journeyScalar(config, QLatin1String("titleFontFamily"),
                      QJsonValue(QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif"))),
        QStringLiteral("\"trebuchet ms\", verdana, arial, sans-serif")));
    const CssLengthContext titleContext =
        pieCssLengthContext(style.fontFamily, style.fontSize);
    journeyConfig.titleFontSize = svgFontSizeFromRaw(
        journeyScalar(config, QLatin1String("titleFontSize"),
                      QJsonValue(QStringLiteral("4ex"))),
        titleContext);

    // themeCSS: resolve the user sheet against a faithful model of the
    // journey DOM (flat: everything is a child of the svg root, sections and
    // task groups interleaved in draw order). Base rules come first so the
    // `line`/`.legend`/`.face`/`.task-type-N` declarations keep overriding
    // presentation attributes exactly as upstream's stylesheet does.
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    journey::JourneyCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      const QString baseCss = journeyBaseCss(themeVars, style.fontFamily);
      // Mermaid paints `#id { font-family; font-size; fill: textColor }`
      // directly on the svg element. Stylis scopes user `svg {}` rules to
      // `#id svg` — a descendant selector that can never match the root — so
      // this fallback is final for the root unless a rule reaches inside.
      ElementStyle rootStyle;
      rootStyle.fill = style.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize =
          QString::number(style.fontSize) + QStringLiteral("px");
      rootStyle.fontWeight = QStringLiteral("400");
      // Empty members inherit the parent's resolved value via the engine's
      // project() pass; SVG presentation attributes set the concrete base.
      ElementStyle inheritAll;

      const journey::JourneyActorRoster roster =
          journey::journeyActorRoster(data);
      // People circles are drawn only for known actors (the builder filters
      // the same way); the tree must enumerate that filtered order.
      const auto filteredPeople = [&roster](const journey::JourneyTask& task) {
        QStringList people;
        for (const QString& person : task.people)
          if (roster.entryFor(person)) people.append(person);
        return people;
      };

      // Pass 1 — the actor-name wrap is measured with a classless probe
      // <text> that upstream appends hidden and removes again, so `text`
      // rules reach it but `.legend` does not.
      const QHash<QString, ElementStyle> skeleton = csscascade::resolveElements(
          themeCss,
          {ElementInput{QStringLiteral("svg"), {}, QStringLiteral("svg"),
                        QStringLiteral("diagram-root"), {}, {}, rootStyle, {}},
           ElementInput{QStringLiteral("measure"), QStringLiteral("svg"),
                        QStringLiteral("text"), {}, {}, {}, inheritAll, {}}},
          baseCss);
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, style.fontSize);
      const auto fontSizePx = [&familyCtx](const QString& css) {
        return cssFontSizePx(css, familyCtx);
      };
      overrides.active = true;
      const ElementStyle& measured =
          skeleton.value(QStringLiteral("measure"));
      overrides.measureText.fontFamily = firstFontFamily(
          measured.fontFamily.isEmpty() ? style.fontFamily : measured.fontFamily);
      overrides.measureText.fontSize = fontSizePx(measured.fontSize);

      // Line counts come from the same wrap the scene builder performs.
      QVector<QStringList> actorLines;
      actorLines.reserve(roster.display.size());
      for (const journey::JourneyActorRosterEntry& entry : roster.display)
        actorLines.append(journey::wrapJourneyActorLabel(
            entry.name, journeyConfig.maxLabelWidth,
            overrides.measureText.fontSize, overrides.measureText.fontFamily));

      // Pass 2 — the full DOM, siblings in document order (style, scaffold
      // group, defs/marker, legend circles+texts, section/task groups, title,
      // axis). Parents always precede their children in the input vector.
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
      push({QStringLiteral("defs"), QStringLiteral("svg"),
            QStringLiteral("defs"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("marker"), QStringLiteral("defs"),
            QStringLiteral("marker"), {}, {}, {}, inheritAll, {}});
      // The arrowhead path carries no fill attribute: it inherits the root.
      push({QStringLiteral("markerPath"), QStringLiteral("marker"),
            QStringLiteral("path"), {}, {}, {}, inheritAll, {}});

      for (qsizetype i = 0; i < roster.display.size(); ++i) {
        const journey::JourneyActorRosterEntry& entry = roster.display.at(i);
        ElementStyle circleStyle = inheritAll;
        circleStyle.fill = entry.color;
        circleStyle.stroke = QStringLiteral("#000000");
        circleStyle.strokeWidth = QStringLiteral("1px");
        push({QStringLiteral("actor-circle-%1").arg(i),
              QStringLiteral("svg"), QStringLiteral("circle"), {},
              {QStringLiteral("actor-%1").arg(entry.position)}, {},
              circleStyle, {}});
        // One text.legend per wrapped line (upstream drawText per line).
        for (qsizetype line = 0; line < actorLines.at(i).size(); ++line)
          push({QStringLiteral("actor-text-%1-%2").arg(i).arg(line),
                QStringLiteral("svg"), QStringLiteral("text"), {},
                {QStringLiteral("legend")}, {}, inheritAll, {}});
      }

      const bool foPlacement = journeyConfig.textPlacement == QLatin1String("fo");
      const bool oldPlacement =
          journeyConfig.textPlacement == QLatin1String("old");
      // byTspan styles font-size/font-family inline (author origin, above the
      // user sheet unless !important); byFo drops the fill attribute so the
      // fallback text inherits the root fill, while direct tspan placement
      // paints the sectionColours attribute. The inline size is the resolved
      // px equivalent of the raw config value (the CSSOM accepts the unitless
      // number as px for SVG).
      const QString textInline =
          oldPlacement
              ? QString()
              : QStringLiteral("font-size:%1px;font-family:%2")
                    .arg(QString::number(journeyConfig.taskFontSize),
                         journeyConfig.taskFontFamily);
      const QString svgTextFill =
          foPlacement || oldPlacement ? QString()
                                      : QStringLiteral("#ffffff");

      QString lastSection;
      int sectionNumber = 0;
      int colorIndex = 0;
      for (qsizetype i = 0; i < data.tasks.size(); ++i) {
        const journey::JourneyTask& input = data.tasks.at(i);
        if (lastSection != input.section) {
          colorIndex = sectionNumber % 7;
          const QString group = QStringLiteral("section-g-%1").arg(sectionNumber);
          push({group, QStringLiteral("svg"), QStringLiteral("g"), {}, {},
                {}, inheritAll, {}});
          ElementStyle rectStyle = inheritAll;
          rectStyle.fill = journey::journeySectionPresentationFill(colorIndex);
          rectStyle.stroke = QStringLiteral("#666");
          rectStyle.strokeWidth = QStringLiteral("1px");
          const QStringList sectionClasses{
              QStringLiteral("journey-section"),
              QStringLiteral("section-type-%1").arg(colorIndex)};
          push({QStringLiteral("section-rect-%1").arg(sectionNumber), group,
                QStringLiteral("rect"), {}, sectionClasses, {}, rectStyle, {}});
          if (foPlacement) {
            const QString switchKey =
                QStringLiteral("section-switch-%1").arg(sectionNumber);
            push({switchKey, group, QStringLiteral("switch"), {}, {}, {},
                  inheritAll, {}});
            const QString foKey =
                QStringLiteral("section-fo-%1").arg(sectionNumber);
            push({foKey, switchKey, QStringLiteral("foreignobject"), {}, {},
                  {}, inheritAll, {}});
            const QString divKey =
                QStringLiteral("section-div-%1").arg(sectionNumber);
            push({divKey, foKey, QStringLiteral("div"), {}, sectionClasses,
                  {}, inheritAll,
                  QStringLiteral("display:table;height:100%;width:100%")});
            push({QStringLiteral("section-label-%1").arg(sectionNumber), divKey,
                  QStringLiteral("div"), {}, {QStringLiteral("label")}, {},
                  inheritAll,
                  QStringLiteral(
                      "display:table-cell;text-align:center;"
                      "vertical-align:middle")});
          }
          push({QStringLiteral("section-text-%1").arg(sectionNumber), group,
                QStringLiteral("text"), {}, sectionClasses, {}, inheritAll,
                textInline, svgTextFill});
          lastSection = input.section;
          ++sectionNumber;
        }

        const QString group = QStringLiteral("task-g-%1").arg(i);
        push({group, QStringLiteral("svg"), QStringLiteral("g"), {}, {}, {},
              inheritAll, {}});
        ElementStyle lineStyle = inheritAll;
        lineStyle.stroke = QStringLiteral("#666");  // base `line` rule overrides
        lineStyle.strokeWidth = QStringLiteral("1px");
        push({QStringLiteral("task-line-%1").arg(i), group,
              QStringLiteral("line"),
              QStringLiteral("diagram-root-task%1").arg(i),
              {QStringLiteral("task-line")}, {}, lineStyle, {}});
        ElementStyle faceStyle = inheritAll;
        faceStyle.strokeWidth = QStringLiteral("2px");  // .face rule supplies fill/stroke
        push({QStringLiteral("task-face-%1").arg(i), group,
              QStringLiteral("circle"), {}, {QStringLiteral("face")}, {},
              faceStyle, {}});
        const QString faceGroup = QStringLiteral("task-facegroup-%1").arg(i);
        push({faceGroup, group, QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
        ElementStyle eyeStyle = inheritAll;
        eyeStyle.fill = QStringLiteral("#666");
        eyeStyle.stroke = QStringLiteral("#666");
        eyeStyle.strokeWidth = QStringLiteral("2px");
        push({QStringLiteral("task-eye1-%1").arg(i), faceGroup,
              QStringLiteral("circle"), {}, {}, {}, eyeStyle, {}});
        push({QStringLiteral("task-eye2-%1").arg(i), faceGroup,
              QStringLiteral("circle"), {}, {}, {}, eyeStyle, {}});
        ElementStyle mouthStyle = inheritAll;
        mouthStyle.stroke = QStringLiteral("#666");  // .mouth rule (also an attr on the line variant)
        mouthStyle.strokeWidth = QStringLiteral("1px");
        push({QStringLiteral("task-mouth-%1").arg(i), faceGroup,
              input.score > 3.0 || input.score < 3.0 ? QStringLiteral("path")
                                                     : QStringLiteral("line"),
              {}, {QStringLiteral("mouth")}, {}, mouthStyle, {}});
        ElementStyle rectStyle = inheritAll;
        rectStyle.fill = journey::journeySectionPresentationFill(colorIndex);
        rectStyle.stroke = QStringLiteral("#666");
        rectStyle.strokeWidth = QStringLiteral("1px");
        push({QStringLiteral("task-rect-%1").arg(i), group,
              QStringLiteral("rect"), {},
              {QStringLiteral("task"),
               QStringLiteral("task-type-%1").arg(colorIndex)}, {},
              rectStyle, {}});
        qsizetype personIndex = 0;
        for (const QString& person : filteredPeople(input)) {
          const journey::JourneyActorRosterEntry* entry =
              roster.entryFor(person);
          ElementStyle personStyle = inheritAll;
          personStyle.fill = entry->color;
          personStyle.stroke = QStringLiteral("#000000");
          personStyle.strokeWidth = QStringLiteral("1px");
          push({QStringLiteral("task-person-%1-%2").arg(i).arg(personIndex),
                group, QStringLiteral("circle"), {},
                {QStringLiteral("actor-%1").arg(entry->position)}, {},
                personStyle, {}});
          ++personIndex;
        }
        const QStringList taskClasses{QStringLiteral("task")};
        if (foPlacement) {
          const QString switchKey = QStringLiteral("task-switch-%1").arg(i);
          push({switchKey, group, QStringLiteral("switch"), {}, {}, {},
                inheritAll, {}});
          const QString foKey = QStringLiteral("task-fo-%1").arg(i);
          push({foKey, switchKey, QStringLiteral("foreignobject"), {}, {}, {},
                inheritAll, {}});
          const QString divKey = QStringLiteral("task-div-%1").arg(i);
          push({divKey, foKey, QStringLiteral("div"), {}, taskClasses, {},
                inheritAll,
                QStringLiteral("display:table;height:100%;width:100%")});
          push({QStringLiteral("task-label-%1").arg(i), divKey,
                QStringLiteral("div"), {}, {QStringLiteral("label")}, {},
                inheritAll,
                QStringLiteral(
                    "display:table-cell;text-align:center;vertical-align:middle")});
        }
        push({QStringLiteral("task-text-%1").arg(i), group,
              QStringLiteral("text"), {}, taskClasses, {}, inheritAll,
              textInline, svgTextFill});
      }

      if (!data.title.isEmpty()) {
        ElementStyle titleStyle = inheritAll;
        titleStyle.fill = journeyConfig.titleColor;
        titleStyle.fontSize =
            QString::number(journeyConfig.titleFontSize) + QStringLiteral("px");
        titleStyle.fontFamily = journeyConfig.titleFontFamily;
        titleStyle.fontWeight = QStringLiteral("bold");
        push({QStringLiteral("title"), QStringLiteral("svg"),
              QStringLiteral("text"), {}, {}, {}, titleStyle, {}});
      }
      ElementStyle axisStyle = inheritAll;
      axisStyle.stroke = QStringLiteral("#000000");  // base `line` rule overrides
      axisStyle.strokeWidth = QStringLiteral("4px");
      push({QStringLiteral("axis"), QStringLiteral("svg"),
            QStringLiteral("line"), {}, {}, {}, axisStyle, {}});

      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, baseCss);
      const auto convert = [&](const QString& key) {
        journey::JourneyElementCss out;
        const ElementStyle& resolved = css.value(key);
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        out.color = resolved.color;
        // Same engine semantics: an empty resolved font-family means no rule
        // or inherit keyword reached the element — keep the root font chain.
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = fontSizePx(resolved.fontSize);
        out.fontWeight = resolved.fontWeight;
        out.opacity = cssOpacity(resolved.opacity);
        out.visible = resolved.displayed();
        out.hasBox = resolved.hasBox();
        return out;
      };
      overrides.root = convert(QStringLiteral("svg"));
      overrides.title = convert(QStringLiteral("title"));
      overrides.axis = convert(QStringLiteral("axis"));
      for (qsizetype i = 0; i < roster.display.size(); ++i) {
        overrides.actorCircles.append(
            convert(QStringLiteral("actor-circle-%1").arg(i)));
        // Every wrapped line shares the .legend selector surface; structural
        // rules per line are not consumed separately.
        overrides.actorTexts.append(
            convert(QStringLiteral("actor-text-%1-0").arg(i)));
      }
      for (int s = 0; s < sectionNumber; ++s) {
        journey::JourneyCssOverrides::Section resolved;
        resolved.box = convert(QStringLiteral("section-rect-%1").arg(s));
        resolved.label = convert(QStringLiteral("section-label-%1").arg(s));
        resolved.svgText = convert(QStringLiteral("section-text-%1").arg(s));
        overrides.sections.append(std::move(resolved));
      }
      for (qsizetype i = 0; i < data.tasks.size(); ++i) {
        journey::JourneyCssOverrides::Task resolved;
        resolved.box = convert(QStringLiteral("task-rect-%1").arg(i));
        resolved.label = convert(QStringLiteral("task-label-%1").arg(i));
        resolved.svgText = convert(QStringLiteral("task-text-%1").arg(i));
        resolved.line = convert(QStringLiteral("task-line-%1").arg(i));
        resolved.face = convert(QStringLiteral("task-face-%1").arg(i));
        resolved.mouth = convert(QStringLiteral("task-mouth-%1").arg(i));
        const QStringList people = filteredPeople(data.tasks.at(i));
        for (qsizetype personIndex = 0; personIndex < people.size();
             ++personIndex)
          resolved.people.append(convert(
              QStringLiteral("task-person-%1-%2").arg(i).arg(personIndex)));
        overrides.tasks.append(std::move(resolved));
      }
    }

    journey::JourneyScene scene = journey::buildJourneyScene(
        data, std::move(journeyConfig), std::move(style),
        themeCssActive ? &overrides : nullptr);

    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), data.accTitle, data.accDescr,
        scene.config.titleColor.isEmpty() ? scene.style.textColor
                                          : scene.config.titleColor,
        scene.style.fontFamily, scene.style.fontSize, 25.0, 0.0);
    metadata.title = QString();
    metadata.svgUseMaxWidth = scene.config.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()),
                              qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const journey::JourneyScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& journeyDiagramAdapter() {
  static const JourneyDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
