#include "mermaid/journey/JourneyScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/journey/JourneyScenePainter.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::journey {

void JourneyScene::paint(QPainter& painter,
                         const MermaidPaintOptions& options) const {
  paintJourneyScene(*this, painter, options);
}

namespace {

struct JsScalar {
  enum class Kind { Number, String, Boolean } kind = Kind::Number;
  double number = 0.0;
  QString string;
  bool boolean = false;
};

JsScalar jsScalar(const QJsonValue& value) {
  if (value.isString())
    return {JsScalar::Kind::String, 0.0, value.toString(), false};
  if (value.isBool())
    return {JsScalar::Kind::Boolean, 0.0, {}, value.toBool()};
  return {JsScalar::Kind::Number, value.toDouble(), {}, false};
}

JsScalar jsNumberScalar(double value) {
  return {JsScalar::Kind::Number, value, {}, false};
}

double jsScalarNumber(const JsScalar& value) {
  if (value.kind == JsScalar::Kind::Number) return value.number;
  if (value.kind == JsScalar::Kind::Boolean) return value.boolean ? 1.0 : 0.0;
  return editor::jsNumberValue(QJsonValue(value.string));
}

QString jsScalarString(const JsScalar& value) {
  if (value.kind == JsScalar::Kind::String) return value.string;
  if (value.kind == JsScalar::Kind::Boolean)
    return value.boolean ? QStringLiteral("true") : QStringLiteral("false");
  return editor::jsNumberToString(value.number);
}

JsScalar jsAdd(const JsScalar& left, const JsScalar& right) {
  if (left.kind == JsScalar::Kind::String || right.kind == JsScalar::Kind::String)
    return {JsScalar::Kind::String, 0.0,
            jsScalarString(left) + jsScalarString(right), false};
  return jsNumberScalar(jsScalarNumber(left) + jsScalarNumber(right));
}

double svgNumber(const JsScalar& value) {
  if (value.kind == JsScalar::Kind::Number) return value.number;
  if (value.kind == JsScalar::Kind::Boolean) return 0.0;
  static const QRegularExpression number(
      QStringLiteral(R"(^\s*[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?\s*$)"));
  return number.match(value.string).hasMatch() ? value.string.toDouble() : 0.0;
}

const QStringList kActorColors = {
    QStringLiteral("#8FBC8F"), QStringLiteral("#7CFC00"),
    QStringLiteral("#00FFFF"), QStringLiteral("#20B2AA"),
    QStringLiteral("#B0E0E6"), QStringLiteral("#FFFFE0")};

const QStringList kSectionFills = {
    QStringLiteral("#191970"), QStringLiteral("#8B008B"),
    QStringLiteral("#4B0082"), QStringLiteral("#2F4F4F"),
    QStringLiteral("#800000"), QStringLiteral("#8B4513"),
    QStringLiteral("#00008B")};

QStringList wrapActor(const QString& actor, qreal maxLabelWidth,
                      const editor::CssPixelFont& font) {
  if (std::isnan(maxLabelWidth) ||
      font.horizontalAdvance(actor) <= maxLabelWidth)
    return {actor};

  QStringList lines;
  QString current;
  const QStringList words = actor.split(QLatin1Char(' '), Qt::KeepEmptyParts);
  for (const QString& word : words) {
    const QString trial = current.isEmpty() ? word : current + QLatin1Char(' ') + word;
    if (font.horizontalAdvance(trial) <= maxLabelWidth) {
      current = trial;
      continue;
    }
    if (!current.isEmpty()) lines.append(current);
    current = word;
    if (font.horizontalAdvance(word) <= maxLabelWidth) continue;

    QString broken;
    // QString iteration is UTF-16, while JS `for...of` iterates code points.
    // Keep surrogate pairs together so emoji break at the same boundary.
    for (qsizetype i = 0; i < word.size(); ++i) {
      QString scalar(word.at(i));
      if (word.at(i).isHighSurrogate() && i + 1 < word.size() &&
          word.at(i + 1).isLowSurrogate())
        scalar.append(word.at(++i));
      broken += scalar;
      if (font.horizontalAdvance(broken + QLatin1Char('-')) > maxLabelWidth) {
        QString prefix = broken;
        prefix.chop(scalar.size());
        lines.append(prefix + QLatin1Char('-'));
        broken = scalar;
      }
    }
    current = broken;
  }
  if (!current.isEmpty()) lines.append(current);
  return lines;
}

bool isArrayIndexName(const QString& value, quint32* out) {
  if (value.isEmpty() || (value.size() > 1 && value.startsWith(QLatin1Char('0'))))
    return false;
  bool ok = false;
  const qulonglong number = value.toULongLong(&ok, 10);
  if (!ok || number >= 0xffffffffULL || QString::number(number) != value) return false;
  *out = quint32(number);
  return true;
}

QStringList javascriptObjectKeyOrder(QStringList values) {
  struct Indexed { quint32 index; QString value; };
  QVector<Indexed> indices;
  QStringList strings;
  for (const QString& value : values) {
    // Assignment to a plain object's __proto__ setter does not create an own key.
    if (value == QStringLiteral("__proto__")) continue;
    quint32 index = 0;
    if (isArrayIndexName(value, &index)) indices.append({index, value});
    else strings.append(value);
  }
  std::sort(indices.begin(), indices.end(), [](const Indexed& a, const Indexed& b) {
    return a.index < b.index;
  });
  QStringList result;
  for (const Indexed& indexed : indices) result.append(indexed.value);
  result.append(strings);
  return result;
}

QJsonArray rectJson(const QRectF& r) {
  return {r.x(), r.y(), r.width(), r.height()};
}

bool validCssPaint(const QString& value) {
  if (color::isParsableColor(value)) return true;
  const QString keyword = value.trimmed().toLower();
  return keyword == QStringLiteral("none") ||
         keyword == QStringLiteral("currentcolor") ||
         keyword == QStringLiteral("inherit") ||
         keyword == QStringLiteral("initial") ||
         keyword == QStringLiteral("unset") ||
         keyword == QStringLiteral("revert") ||
         keyword == QStringLiteral("revert-layer");
}

}  // namespace

const JourneyActorRosterEntry* JourneyActorRoster::entryFor(
    const QString& name) const {
  if (hasPrototype && name == prototype.name) return &prototype;
  for (const JourneyActorRosterEntry& entry : display)
    if (entry.name == name) return &entry;
  return nullptr;
}

JourneyActorRoster journeyActorRoster(const JourneyData& data) {
  JourneyActorRoster roster;
  QStringList actorNames;
  for (const JourneyTask& task : data.tasks)
    for (const QString& person : task.people)
      if (!actorNames.contains(person)) actorNames.append(person);
  std::sort(actorNames.begin(), actorNames.end());
  const QStringList sortedActorNames = actorNames;
  const int prototypePosition =
      sortedActorNames.indexOf(QStringLiteral("__proto__"));
  if (prototypePosition >= 0) {
    roster.hasPrototype = true;
    roster.prototype.name = QStringLiteral("__proto__");
    roster.prototype.position = prototypePosition;
    roster.prototype.color =
        kActorColors.at(prototypePosition % kActorColors.size());
  }
  const QStringList displayNames =
      javascriptObjectKeyOrder(actorNames);
  for (const QString& name : displayNames) {
    JourneyActorRosterEntry entry;
    entry.name = name;
    entry.position = int(sortedActorNames.indexOf(name));
    entry.color = kActorColors.at(entry.position % kActorColors.size());
    roster.display.append(std::move(entry));
  }
  return roster;
}

QStringList wrapJourneyActorLabel(const QString& actor, qreal maxLabelWidth,
                                  qreal fontPixelSize,
                                  const QString& fontFamily) {
  return wrapActor(actor, maxLabelWidth,
                   editor::makeUnhintedCssPixelFont(fontFamily, fontPixelSize));
}

QString journeySectionPresentationFill(int colorIndex) {
  return kSectionFills.at(colorIndex % kSectionFills.size());
}

JourneyScene buildJourneyScene(const JourneyData& data, JourneyConfig config,
                               JourneySceneStyle style,
                               const JourneyCssOverrides* css) {
  JourneyScene scene;
  scene.config = std::move(config);
  scene.style = std::move(style);
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;
  if (css && css->active) {
    scene.rootCss = css->root;
    scene.titleCss = css->title;
    scene.axisCss = css->axis;
  }

  const JourneyActorRoster roster = journeyActorRoster(data);
  scene.hasPrototypeActor = roster.hasPrototype;
  scene.prototypeActor.name = roster.prototype.name;
  scene.prototypeActor.position = roster.prototype.position;
  scene.prototypeActor.color = roster.prototype.color;
  QStringList actorNames;
  for (const JourneyActorRosterEntry& entry : roster.display)
    actorNames.append(entry.name);

  // The svg root's own cascade (#id { font-family; font-size; fill }) feeds
  // everything that inherits from it. The actor-name wrap measurement uses a
  // classless probe <text> that upstream appends hidden and removes again, so
  // its font is resolved separately from the drawn .legend text.
  const QString rootFamily = css && !css->root.fontFamily.isEmpty()
                                 ? css->root.fontFamily
                                 : scene.style.fontFamily;
  const qreal rootSize = css && css->root.fontSize >= 0.0
                             ? css->root.fontSize
                             : scene.style.fontSize;
  const QString measureFamily = css && !css->measureText.fontFamily.isEmpty()
                                    ? css->measureText.fontFamily
                                    : rootFamily;
  const qreal measureSize = css && css->measureText.fontSize >= 0.0
                                ? css->measureText.fontSize
                                : rootSize;
  const editor::CssPixelFont wrapFont =
      editor::makeUnhintedCssPixelFont(measureFamily, measureSize);
  qreal maxWidth = 0.0;
  qreal actorY = 60.0;
  for (qsizetype i = 0; i < actorNames.size(); ++i) {
    JourneyActor actor;
    actor.name = actorNames.at(i);
    const JourneyActorRosterEntry& entry = roster.display.at(i);
    actor.position = entry.position;
    actor.color = entry.color;
    actor.y = actorY;
    if (css && css->active) {
      actor.circle = css->actorCircles.value(i);
      actor.text = css->actorTexts.value(i);
    }
    actor.lines = wrapActor(actor.name, scene.config.maxLabelWidth, wrapFont);
    const QString legendFamily = !actor.text.fontFamily.isEmpty()
                                     ? actor.text.fontFamily
                                     : scene.style.fontFamily;
    const qreal legendSize = actor.text.fontSize >= 0.0
                                 ? actor.text.fontSize
                                 : scene.style.fontSize;
    const editor::CssPixelFont legendFont =
        editor::makeUnhintedCssPixelFont(legendFamily, legendSize);
    for (const QString& line : actor.lines) {
      // The drawn .legend text drives the legend block width upstream; a
      // display:none text reports a zero rect while visibility:hidden keeps
      // its advance.
      const qreal width = actor.text.hasBox
                              ? legendFont.horizontalAdvance(line)
                              : 0.0;
      actor.maxLineWidth = std::max(actor.maxLineWidth, width);
      if (width > maxWidth && width > scene.config.leftMargin - width)
        maxWidth = width;
    }
    actorY += std::max<qreal>(20.0, actor.lines.size() * 20.0);
    scene.actors.append(std::move(actor));
  }

  const JsScalar leftMarginValue = jsAdd(
      jsScalar(scene.config.leftMarginRaw), jsNumberScalar(maxWidth));
  scene.leftMarginResolved = svgNumber(leftMarginValue);
  qreal startX = 0.0;
  qreal stopX = scene.leftMarginResolved;
  qreal startY = 0.0;
  qreal stopY = scene.actors.size() * 50.0;
  const JsScalar taskYValue = jsAdd(
      jsNumberScalar(scene.config.height * 2.0),
      jsScalar(scene.config.diagramMarginYRaw));
  const qreal taskY = svgNumber(jsAdd(jsNumberScalar(0.0), taskYValue));
  QString lastSection;
  int sectionNumber = 0;
  QString currentPresentationFill = QStringLiteral("#CCC");
  int currentColorIndex = 0;
  QString currentFill = scene.style.fillTypes.value(0);
  bool currentCssFillActive = !currentFill.isEmpty() &&
                              validCssPaint(currentFill);
  if (!currentCssFillActive) currentFill = currentPresentationFill;

  for (qsizetype i = 0; i < data.tasks.size(); ++i) {
    const JourneyTask& input = data.tasks.at(i);
    if (lastSection != input.section) {
      currentColorIndex = sectionNumber % 7;
      currentPresentationFill = kSectionFills.at(currentColorIndex);
      // Journey emits all eight CSS rules only when fillType0 is truthy. An
      // empty individual fill declaration is invalid and falls back to the
      // rect's presentation attribute.
      const QString cssFill = scene.style.fillTypes.value(currentColorIndex);
      currentCssFillActive = !scene.style.fillTypes.value(0).isEmpty() &&
                             validCssPaint(cssFill);
      currentFill = currentCssFillActive
                        ? cssFill
                        : currentPresentationFill;
      qsizetype count = 0;
      for (qsizetype j = i; j < data.tasks.size() &&
                           data.tasks.at(j).section == input.section; ++j)
        ++count;
      JourneySectionGeometry section;
      const JsScalar sectionXValue = jsAdd(
          jsNumberScalar(i * scene.config.taskMargin + i * scene.config.width),
          leftMarginValue);
      const qreal sectionX = svgNumber(sectionXValue);
      section.rect = QRectF(sectionX,
                            50.0,
                            scene.config.width * count +
                                scene.config.diagramMarginX * (count - 1),
                            scene.config.rectHeight);
      section.oldTextAnchor = QPointF(
          svgNumber(jsAdd(sectionXValue,
                          jsNumberScalar(scene.config.width * count / 2.0 +
                                         scene.config.diagramMarginX *
                                             (count - 1) / 2.0))),
          50.0 + scene.config.height / 2.0 + 5.0);
      section.tspanTextAnchor = QPointF(
          section.oldTextAnchor.x(), 50.0 + scene.config.height / 2.0);
      section.text = input.section;
      section.presentationFill = currentPresentationFill;
      section.fill = currentFill;
      section.cssFillActive = currentCssFillActive;
      section.colorIndex = currentColorIndex;
      if (css && css->active) {
        const JourneyCssOverrides::Section& resolved =
            css->sections.value(sectionNumber);
        section.box = resolved.box;
        section.label = resolved.label;
        section.svgText = resolved.svgText;
        // The resolved string IS the cascade outcome (base .task-type rules
        // included), so it always participates as a CSS value.
        if (!resolved.box.fill.isEmpty()) {
          section.fill = resolved.box.fill;
          section.cssFillActive = true;
        }
      }
      scene.sections.append(std::move(section));
      lastSection = input.section;
      ++sectionNumber;
    }

    JourneyTaskGeometry task;
    const JsScalar taskXValue = jsAdd(
        jsNumberScalar(i * scene.config.taskMargin + i * scene.config.width),
        leftMarginValue);
    const qreal taskX = svgNumber(taskXValue);
    task.rect = QRectF(taskX, taskY, scene.config.rectWidth,
                       scene.config.rectHeight);
    task.oldTextAnchor = QPointF(
        svgNumber(jsAdd(taskXValue, jsNumberScalar(scene.config.width / 2.0))),
        svgNumber(jsAdd(
            jsAdd(jsAdd(jsNumberScalar(0.0), taskYValue),
                  jsNumberScalar(scene.config.height / 2.0)),
            jsNumberScalar(5.0))));
    task.tspanTextAnchor = QPointF(
        task.oldTextAnchor.x(),
        svgNumber(jsAdd(jsAdd(jsNumberScalar(0.0), taskYValue),
                        jsNumberScalar(scene.config.height / 2.0))));
    task.text = input.task;
    task.section = input.section;
    task.presentationFill = currentPresentationFill;
    task.fill = currentFill;
    task.cssFillActive = currentCssFillActive;
    task.colorIndex = currentColorIndex;
    task.score = input.score;
    task.faceCenter = QPointF(
        svgNumber(jsAdd(taskXValue,
                        jsNumberScalar(scene.config.width / 2.0))),
                              300.0 + (5.0 - input.score) * 30.0);
    if (css && css->active) {
      const JourneyCssOverrides::Task& resolved = css->tasks.value(i);
      task.box = resolved.box;
      task.label = resolved.label;
      task.svgText = resolved.svgText;
      task.line = resolved.line;
      task.face = resolved.face;
      task.mouth = resolved.mouth;
      if (!resolved.box.fill.isEmpty()) {
        task.fill = resolved.box.fill;
        task.cssFillActive = true;
      }
    }
    qsizetype personIndex = 0;
    for (const QString& person : input.people)
      if (person == QStringLiteral("__proto__") || actorNames.contains(person)) {
        task.people.append(person);
        if (css && css->active)
          task.peopleCircles.append(
              css->tasks.value(i).people.value(personIndex));
        ++personIndex;
      }
    JsScalar actorXValue = jsAdd(taskXValue, jsNumberScalar(14.0));
    for (const QString& person : task.people) {
      const qreal actorX = svgNumber(actorXValue);
      task.actorCenters.append(actorX);
      InteractionRegion region;
      region.bounds = QRectF(actorX - 7.0, taskY - 7.0, 14.0, 14.0);
      region.toolTip = person;
      region.accessibleLabel = person;
      scene.interactions.append(std::move(region));
      actorXValue = jsAdd(actorXValue, jsNumberScalar(10.0));
    }
    scene.tasks.append(std::move(task));

    const qreal bx2 = svgNumber(jsAdd(
        jsAdd(taskXValue, jsScalar(scene.config.diagramMarginXRaw)),
        jsScalar(scene.config.taskMarginRaw)));
    startX = std::min(startX, std::min(taskX, bx2));
    stopX = std::max(stopX, std::max(taskX, bx2));
    startY = std::min(startY, std::min(taskY, 450.0));
    stopY = std::max(stopY, std::max(taskY, 450.0));
  }

  scene.baseHeight = stopY - startY + 2.0 * scene.config.diagramMarginY;
  scene.canvasWidth = svgNumber(jsAdd(
      jsAdd(leftMarginValue, jsNumberScalar(stopX)),
      jsNumberScalar(2.0 * scene.config.diagramMarginX)));
  const qreal titleExtra = scene.title.isEmpty() ? 0.0 : 70.0;
  scene.upstreamViewBox = QRectF(startX, -25.0, scene.canvasWidth,
                                 scene.baseHeight + titleExtra);
  scene.upstreamRootHeight = scene.baseHeight + titleExtra + 25.0;
  scene.bounds = QRectF(startX, -25.0, scene.canvasWidth,
                        scene.upstreamRootHeight);
  return scene;
}

QJsonObject JourneyScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("bounds")] = rectJson(bounds);
  root[QStringLiteral("viewBox")] = rectJson(upstreamViewBox);
  root[QStringLiteral("rootHeight")] = upstreamRootHeight;
  root[QStringLiteral("leftMargin")] = leftMarginResolved;
  root[QStringLiteral("title")] = title;

  QJsonArray actorArray;
  for (const JourneyActor& actor : actors) {
    QJsonObject value;
    value[QStringLiteral("name")] = actor.name;
    value[QStringLiteral("position")] = actor.position;
    value[QStringLiteral("color")] = actor.color;
    value[QStringLiteral("y")] = actor.y;
    value[QStringLiteral("lines")] = QJsonArray::fromStringList(actor.lines);
    actorArray.append(value);
  }
  root[QStringLiteral("actors")] = actorArray;

  QJsonArray sectionArray;
  for (const JourneySectionGeometry& section : sections) {
    QJsonObject value;
    value[QStringLiteral("rect")] = rectJson(section.rect);
    value[QStringLiteral("text")] = section.text;
    value[QStringLiteral("presentationFill")] = section.presentationFill;
    value[QStringLiteral("fill")] = section.fill;
    value[QStringLiteral("cssFillActive")] = section.cssFillActive;
    value[QStringLiteral("colorIndex")] = section.colorIndex;
    sectionArray.append(value);
  }
  root[QStringLiteral("sections")] = sectionArray;

  QJsonArray taskArray;
  for (const JourneyTaskGeometry& task : tasks) {
    QJsonObject value;
    value[QStringLiteral("rect")] = rectJson(task.rect);
    value[QStringLiteral("text")] = task.text;
    value[QStringLiteral("section")] = task.section;
    value[QStringLiteral("presentationFill")] = task.presentationFill;
    value[QStringLiteral("fill")] = task.fill;
    value[QStringLiteral("cssFillActive")] = task.cssFillActive;
    value[QStringLiteral("colorIndex")] = task.colorIndex;
    value[QStringLiteral("score")] = std::isfinite(task.score)
                                           ? QJsonValue(task.score)
                                           : QJsonValue(editor::jsNumberToString(task.score));
    value[QStringLiteral("face")] = QJsonArray{task.faceCenter.x(), task.faceCenter.y()};
    value[QStringLiteral("people")] = QJsonArray::fromStringList(task.people);
    taskArray.append(value);
  }
  root[QStringLiteral("tasks")] = taskArray;
  return root;
}

}  // namespace muffin::mermaid::journey
