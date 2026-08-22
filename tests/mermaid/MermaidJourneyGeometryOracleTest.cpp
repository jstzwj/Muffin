// Journey geometry oracle. Iterates every case captured from Mermaid 11.16.0
// in tests/fixtures/mermaid/journey-geometry.json and compares the native
// scene's deterministic layout fields with the observed SVG at 0.001 px.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/journey/JourneyScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRectF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace muffin::mermaid;

namespace {

constexpr double kTolerance = 0.001;
constexpr double kFontMetricTolerance = 0.02;

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

double oracleNumber(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  const QString text = value.toString().trimmed();
  if (text == QStringLiteral("NaN"))
    return std::numeric_limits<double>::quiet_NaN();
  if (text == QStringLiteral("Infinity") || text == QStringLiteral("+Infinity"))
    return std::numeric_limits<double>::infinity();
  if (text == QStringLiteral("-Infinity"))
    return -std::numeric_limits<double>::infinity();
  bool ok = false;
  const double number = text.toDouble(&ok);
  require(ok, QStringLiteral("Invalid oracle number: '") + text + QLatin1Char('\''));
  return number;
}

double pxNumber(const QJsonValue& value) {
  QString text = value.toString().trimmed();
  if (text.endsWith(QStringLiteral("px"))) text.chop(2);
  return oracleNumber(text);
}

void compareNumber(QStringList& errors, double actual, double expected,
                   const QString& path) {
  if (std::isnan(expected)) {
    if (!std::isnan(actual))
      errors << path + QStringLiteral(": expected NaN, got %1")
                           .arg(actual, 0, 'g', 17);
    return;
  }
  if (std::isinf(expected)) {
    if (!std::isinf(actual) || std::signbit(actual) != std::signbit(expected))
      errors << path + QStringLiteral(": infinity mismatch");
    return;
  }
  if (!std::isfinite(actual) || std::fabs(actual - expected) > kTolerance) {
    errors << path + QStringLiteral(": %1 != %2 (tolerance %3)")
                         .arg(actual, 0, 'g', 17)
                         .arg(expected, 0, 'g', 17)
                         .arg(kTolerance, 0, 'g', 4);
    return;
  }
  if (expected == 0.0 && actual == 0.0 &&
      std::signbit(actual) != std::signbit(expected))
    errors << path + QStringLiteral(": zero sign mismatch");
}

void compareFontMetric(QStringList& errors, double actual, double expected,
                       const QString& path) {
  if (!std::isfinite(actual) || !std::isfinite(expected) ||
      std::fabs(actual - expected) > kFontMetricTolerance) {
    errors << path + QStringLiteral(": %1 != %2 (font tolerance %3)")
                         .arg(actual, 0, 'g', 17)
                         .arg(expected, 0, 'g', 17)
                         .arg(kFontMetricTolerance, 0, 'g', 4);
  }
}

void compareNumber(QStringList& errors, double actual,
                   const QJsonValue& expected, const QString& path) {
  compareNumber(errors, actual, oracleNumber(expected), path);
}

void compareRect(QStringList& errors, const QRectF& actual,
                 const QJsonObject& element, const QString& path) {
  const QJsonObject attrs = element.value(QStringLiteral("attrs")).toObject();
  compareNumber(errors, actual.x(), attrs.value(QStringLiteral("x")),
                path + QStringLiteral("/x"));
  compareNumber(errors, actual.y(), attrs.value(QStringLiteral("y")),
                path + QStringLiteral("/y"));
  compareNumber(errors, actual.width(), attrs.value(QStringLiteral("width")),
                path + QStringLiteral("/width"));
  compareNumber(errors, actual.height(), attrs.value(QStringLiteral("height")),
                path + QStringLiteral("/height"));
}

QRectF viewBoxRect(const QString& value) {
  const QStringList fields = value.trimmed().split(
      QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  require(fields.size() == 4,
          QStringLiteral("Invalid Journey oracle viewBox: ") + value);
  return QRectF(oracleNumber(fields.at(0)), oracleNumber(fields.at(1)),
                oracleNumber(fields.at(2)), oracleNumber(fields.at(3)));
}

void compareColor(QStringList& errors, const QString& actual,
                  const QString& expected, const QString& path) {
  const QColor actualColor = color::toQColor(actual);
  const QColor expectedColor = color::toQColor(expected);
  if (!actualColor.isValid() || !expectedColor.isValid() ||
      actualColor.rgba() != expectedColor.rgba())
    errors << path + QStringLiteral(": color '%1' != '%2'")
                         .arg(actual, expected);
}

int classIndex(const QString& classes, const QString& prefix) {
  const QRegularExpression expression(
      QStringLiteral("(?:^|\\s)%1-(\\d+)(?=\\s|$)")
          .arg(QRegularExpression::escape(prefix)));
  const QRegularExpressionMatch match = expression.match(classes);
  return match.hasMatch() ? match.captured(1).toInt() : -1;
}

QString effectiveTextMode(const QString& configured) {
  if (configured == QStringLiteral("fo")) return QStringLiteral("fo");
  if (configured == QStringLiteral("old")) return QStringLiteral("old");
  return QStringLiteral("tspan");
}

QString oracleLabelText(const QString& nativeText, const QString& mode) {
  if (mode != QStringLiteral("tspan")) return nativeText;
  return nativeText.split(
                       QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                          QRegularExpression::CaseInsensitiveOption),
                       Qt::KeepEmptyParts)
      .join(QLatin1Char('|'));
}

const journey::JourneyActor* findActor(const journey::JourneyScene& scene,
                                       const QString& name) {
  for (const journey::JourneyActor& actor : scene.actors)
    if (actor.name == name) return &actor;
  return nullptr;
}

void compareRoot(QStringList& errors, const journey::JourneyScene& scene,
                 const QJsonObject& expected, const QString& path) {
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject attrs = root.value(QStringLiteral("attrs")).toObject();
  const QJsonObject computed = root.value(QStringLiteral("computed")).toObject();
  const QRectF viewBox = viewBoxRect(attrs.value(QStringLiteral("viewBox")).toString());

  compareNumber(errors, scene.upstreamViewBox.x(), viewBox.x(),
                path + QStringLiteral("/viewBox/x"));
  compareNumber(errors, scene.upstreamViewBox.y(), viewBox.y(),
                path + QStringLiteral("/viewBox/y"));
  compareNumber(errors, scene.upstreamViewBox.width(), viewBox.width(),
                path + QStringLiteral("/viewBox/width"));
  compareNumber(errors, scene.upstreamViewBox.height(), viewBox.height(),
                path + QStringLiteral("/viewBox/height"));
  compareNumber(errors, scene.upstreamRootHeight,
                attrs.value(QStringLiteral("height")),
                path + QStringLiteral("/rootHeight"));
  compareNumber(errors, scene.canvasWidth, viewBox.width(),
                path + QStringLiteral("/canvasWidth"));

  const QRectF bounds = scene.sceneBounds();
  compareNumber(errors, bounds.x(), viewBox.x(), path + QStringLiteral("/bounds/x"));
  compareNumber(errors, bounds.y(), viewBox.y(), path + QStringLiteral("/bounds/y"));
  compareNumber(errors, bounds.width(), viewBox.width(),
                path + QStringLiteral("/bounds/width"));
  compareNumber(errors, bounds.height(), scene.upstreamRootHeight,
                path + QStringLiteral("/bounds/height"));
  const QRectF renderBounds = scene.renderBounds();
  compareNumber(errors, renderBounds.x(), bounds.x(),
                path + QStringLiteral("/renderBounds/x"));
  compareNumber(errors, renderBounds.y(), bounds.y(),
                path + QStringLiteral("/renderBounds/y"));
  compareNumber(errors, renderBounds.width(), bounds.width(),
                path + QStringLiteral("/renderBounds/width"));
  compareNumber(errors, renderBounds.height(), bounds.height(),
                path + QStringLiteral("/renderBounds/height"));

  const bool expectedUseMaxWidth =
      attrs.value(QStringLiteral("width")).toString() == QStringLiteral("100%");
  if (scene.config.useMaxWidth != expectedUseMaxWidth)
    errors << path + QStringLiteral("/useMaxWidth mismatch");
  compareNumber(errors, scene.style.fontSize,
                pxNumber(computed.value(QStringLiteral("font-size"))),
                path + QStringLiteral("/fontSize"));
}

void compareTitle(QStringList& errors, const journey::JourneyScene& scene,
                  const QJsonObject& expected, const QString& path) {
  const QJsonValue titleValue = expected.value(QStringLiteral("title"));
  if (titleValue.isNull()) {
    if (!scene.title.isEmpty()) errors << path + QStringLiteral(": unexpected title");
    return;
  }
  const QJsonObject title = titleValue.toObject();
  const QJsonObject attrs = title.value(QStringLiteral("attrs")).toObject();
  const QJsonObject computed = title.value(QStringLiteral("computed")).toObject();
  if (scene.title != title.value(QStringLiteral("text")).toString())
    errors << path + QStringLiteral(": title text mismatch");
  compareNumber(errors, scene.leftMarginResolved, attrs.value(QStringLiteral("x")),
                path + QStringLiteral("/x"));
  compareNumber(errors, 25.0, attrs.value(QStringLiteral("y")),
                path + QStringLiteral("/y"));
  compareFontMetric(errors, scene.config.titleFontSize,
                    pxNumber(computed.value(QStringLiteral("font-size"))),
                    path + QStringLiteral("/fontSize"));
  compareColor(errors,
               scene.config.titleColor.isEmpty() ? scene.style.textColor
                                                  : scene.config.titleColor,
               computed.value(QStringLiteral("fill")).toString(),
               path + QStringLiteral("/fill"));
}

void compareBottomLine(QStringList& errors, const journey::JourneyScene& scene,
                       const QJsonObject& expected, const QString& path) {
  const QJsonObject line = expected.value(QStringLiteral("bottomLine")).toObject();
  const QJsonObject attrs = line.value(QStringLiteral("attrs")).toObject();
  compareNumber(errors, scene.leftMarginResolved, attrs.value(QStringLiteral("x1")),
                path + QStringLiteral("/x1"));
  compareNumber(errors, scene.config.height * 4.0,
                attrs.value(QStringLiteral("y1")), path + QStringLiteral("/y1"));
  compareNumber(errors, scene.canvasWidth - scene.leftMarginResolved - 4.0,
                attrs.value(QStringLiteral("x2")), path + QStringLiteral("/x2"));
  compareNumber(errors, scene.config.height * 4.0,
                attrs.value(QStringLiteral("y2")), path + QStringLiteral("/y2"));
  compareColor(errors, scene.style.textColor,
               line.value(QStringLiteral("computed"))
                   .toObject()
                   .value(QStringLiteral("stroke"))
                   .toString(),
               path + QStringLiteral("/stroke"));
}

void compareSections(QStringList& errors, const journey::JourneyScene& scene,
                     const QJsonObject& expected, const QString& path) {
  const QJsonArray oracleSections = expected.value(QStringLiteral("sections")).toArray();
  if (scene.sections.size() != oracleSections.size()) {
    errors << path + QStringLiteral(": count %1 != %2")
                         .arg(scene.sections.size())
                         .arg(oracleSections.size());
    return;
  }
  const QString configuredMode = effectiveTextMode(scene.config.textPlacement);
  for (qsizetype index = 0; index < scene.sections.size(); ++index) {
    const journey::JourneySectionGeometry& actual = scene.sections.at(index);
    const QJsonObject oracle = oracleSections.at(index).toObject();
    const QJsonObject rect = oracle.value(QStringLiteral("rect")).toObject();
    const QJsonObject label = oracle.value(QStringLiteral("label")).toObject();
    const QString itemPath = path + QStringLiteral("/%1").arg(index);
    compareRect(errors, actual.rect, rect, itemPath + QStringLiteral("/rect"));
    const QString mode = label.value(QStringLiteral("mode")).toString();
    if (mode != configuredMode)
      errors << itemPath + QStringLiteral("/label: mode '%1' != '%2'")
                               .arg(configuredMode, mode);
    if (oracleLabelText(actual.text, mode) !=
        label.value(QStringLiteral("text")).toString())
      errors << itemPath + QStringLiteral("/label: text mismatch");
    const QJsonObject attrs = rect.value(QStringLiteral("attrs")).toObject();
    const int expectedIndex =
        classIndex(attrs.value(QStringLiteral("class")).toString(),
                   QStringLiteral("section-type"));
    if (actual.colorIndex != expectedIndex)
      errors << itemPath + QStringLiteral("/colorIndex: %1 != %2")
                               .arg(actual.colorIndex)
                               .arg(expectedIndex);
    if (actual.presentationFill != attrs.value(QStringLiteral("fill")).toString())
      errors << itemPath + QStringLiteral("/presentationFill mismatch");
    compareColor(errors, actual.fill,
                 rect.value(QStringLiteral("computed"))
                     .toObject()
                     .value(QStringLiteral("fill"))
                     .toString(),
                 itemPath + QStringLiteral("/fill"));
  }
}

void compareTasks(QStringList& errors, const journey::JourneyScene& scene,
                  const QJsonObject& expected, const QString& path) {
  const QJsonArray oracleTasks = expected.value(QStringLiteral("tasks")).toArray();
  if (scene.tasks.size() != oracleTasks.size()) {
    errors << path + QStringLiteral(": count %1 != %2")
                         .arg(scene.tasks.size())
                         .arg(oracleTasks.size());
    return;
  }
  const QString configuredMode = effectiveTextMode(scene.config.textPlacement);
  qsizetype interactionIndex = 0;
  for (qsizetype index = 0; index < scene.tasks.size(); ++index) {
    const journey::JourneyTaskGeometry& actual = scene.tasks.at(index);
    const QJsonObject oracle = oracleTasks.at(index).toObject();
    const QJsonObject rect = oracle.value(QStringLiteral("rect")).toObject();
    const QJsonObject rectAttrs = rect.value(QStringLiteral("attrs")).toObject();
    const QJsonObject label = oracle.value(QStringLiteral("label")).toObject();
    const QString itemPath = path + QStringLiteral("/%1").arg(index);
    compareRect(errors, actual.rect, rect, itemPath + QStringLiteral("/rect"));
    const QString mode = label.value(QStringLiteral("mode")).toString();
    if (mode != configuredMode)
      errors << itemPath + QStringLiteral("/label: mode '%1' != '%2'")
                               .arg(configuredMode, mode);
    if (oracleLabelText(actual.text, mode) !=
        label.value(QStringLiteral("text")).toString())
      errors << itemPath + QStringLiteral("/label: text mismatch");
    const int expectedIndex =
        classIndex(rectAttrs.value(QStringLiteral("class")).toString(),
                   QStringLiteral("task-type"));
    if (actual.colorIndex != expectedIndex)
      errors << itemPath + QStringLiteral("/colorIndex: %1 != %2")
                               .arg(actual.colorIndex)
                               .arg(expectedIndex);
    if (actual.presentationFill != rectAttrs.value(QStringLiteral("fill")).toString())
      errors << itemPath + QStringLiteral("/presentationFill mismatch");
    compareColor(errors, actual.fill,
                 rect.value(QStringLiteral("computed"))
                     .toObject()
                     .value(QStringLiteral("fill"))
                     .toString(),
                 itemPath + QStringLiteral("/fill"));

    const QJsonObject taskLine = oracle.value(QStringLiteral("taskLine")).toObject();
    const QJsonObject lineAttrs = taskLine.value(QStringLiteral("attrs")).toObject();
    compareNumber(errors, actual.rect.center().x(),
                  lineAttrs.value(QStringLiteral("x1")),
                  itemPath + QStringLiteral("/taskLine/x1"));
    compareNumber(errors, actual.rect.y(), lineAttrs.value(QStringLiteral("y1")),
                  itemPath + QStringLiteral("/taskLine/y1"));
    compareNumber(errors, actual.rect.center().x(),
                  lineAttrs.value(QStringLiteral("x2")),
                  itemPath + QStringLiteral("/taskLine/x2"));
    compareNumber(errors, 450.0, lineAttrs.value(QStringLiteral("y2")),
                  itemPath + QStringLiteral("/taskLine/y2"));
    compareColor(errors, scene.style.textColor,
                 taskLine.value(QStringLiteral("computed"))
                     .toObject()
                     .value(QStringLiteral("stroke"))
                     .toString(),
                 itemPath + QStringLiteral("/taskLine/stroke"));

    const QJsonObject face = oracle.value(QStringLiteral("face")).toObject();
    const QJsonObject faceAttrs = face.value(QStringLiteral("attrs")).toObject();
    compareNumber(errors, actual.faceCenter.x(), faceAttrs.value(QStringLiteral("cx")),
                  itemPath + QStringLiteral("/face/cx"));
    const double faceY = oracleNumber(faceAttrs.value(QStringLiteral("cy")));
    compareNumber(errors, actual.faceCenter.y(), faceY,
                  itemPath + QStringLiteral("/face/cy"));
    const double expectedScore = 5.0 - (faceY - 300.0) / 30.0;
    compareNumber(errors, actual.score, expectedScore,
                  itemPath + QStringLiteral("/score"));
    compareColor(errors, scene.style.faceColor,
                 face.value(QStringLiteral("computed"))
                     .toObject()
                     .value(QStringLiteral("fill"))
                     .toString(),
                 itemPath + QStringLiteral("/face/fill"));

    const QJsonArray people = oracle.value(QStringLiteral("people")).toArray();
    if (actual.people.size() != people.size()) {
      errors << itemPath + QStringLiteral("/people: count %1 != %2")
                               .arg(actual.people.size())
                               .arg(people.size());
      continue;
    }
    for (qsizetype personIndex = 0; personIndex < actual.people.size();
         ++personIndex) {
      const QString person = actual.people.at(personIndex);
      const QJsonObject oraclePerson = people.at(personIndex).toObject();
      const QJsonObject personAttrs =
          oraclePerson.value(QStringLiteral("attrs")).toObject();
      const QString personPath =
          itemPath + QStringLiteral("/people/%1").arg(personIndex);
      if (person != oraclePerson.value(QStringLiteral("title")).toString())
        errors << personPath + QStringLiteral(": title mismatch");
      compareNumber(errors, actual.rect.x() + 14.0 + personIndex * 10.0,
                    personAttrs.value(QStringLiteral("cx")),
                    personPath + QStringLiteral("/cx"));
      compareNumber(errors, actual.rect.y(), personAttrs.value(QStringLiteral("cy")),
                    personPath + QStringLiteral("/cy"));
      const journey::JourneyActor* actor = findActor(scene, person);
      if (!actor) {
        errors << personPath + QStringLiteral(": missing actor");
      } else {
        const int position =
            classIndex(personAttrs.value(QStringLiteral("class")).toString(),
                       QStringLiteral("actor"));
        if (actor->position != position)
          errors << personPath + QStringLiteral("/position mismatch");
        compareColor(errors, actor->color,
                     personAttrs.value(QStringLiteral("fill")).toString(),
                     personPath + QStringLiteral("/fill"));
      }

      if (interactionIndex >= scene.interactions.size()) {
        errors << personPath + QStringLiteral(": missing interaction region");
      } else {
        const InteractionRegion& region = scene.interactions.at(interactionIndex);
        const double cx = oracleNumber(personAttrs.value(QStringLiteral("cx")));
        const double cy = oracleNumber(personAttrs.value(QStringLiteral("cy")));
        compareNumber(errors, region.bounds.x(), cx - 7.0,
                      personPath + QStringLiteral("/interaction/x"));
        compareNumber(errors, region.bounds.y(), cy - 7.0,
                      personPath + QStringLiteral("/interaction/y"));
        compareNumber(errors, region.bounds.width(), 14.0,
                      personPath + QStringLiteral("/interaction/width"));
        compareNumber(errors, region.bounds.height(), 14.0,
                      personPath + QStringLiteral("/interaction/height"));
        if (region.toolTip != person || region.accessibleLabel != person)
          errors << personPath + QStringLiteral(": interaction metadata mismatch");
      }
      ++interactionIndex;
    }
  }
  if (interactionIndex != scene.interactions.size())
    errors << path + QStringLiteral(": unaccounted interaction regions %1 != %2")
                         .arg(scene.interactions.size())
                         .arg(interactionIndex);
}

void compareActors(QStringList& errors, const journey::JourneyScene& scene,
                   const QJsonObject& expected, const QString& path) {
  const QJsonObject legend = expected.value(QStringLiteral("actorLegend")).toObject();
  const QJsonArray circles = legend.value(QStringLiteral("circles")).toArray();
  const QJsonArray texts = legend.value(QStringLiteral("texts")).toArray();
  if (scene.actors.size() != circles.size()) {
    errors << path + QStringLiteral(": actor count %1 != %2")
                         .arg(scene.actors.size())
                         .arg(circles.size());
    return;
  }

  qsizetype textIndex = 0;
  for (qsizetype actorIndex = 0; actorIndex < scene.actors.size(); ++actorIndex) {
    const journey::JourneyActor& actor = scene.actors.at(actorIndex);
    const QJsonObject circle = circles.at(actorIndex).toObject();
    const QJsonObject attrs = circle.value(QStringLiteral("attrs")).toObject();
    const QString actorPath = path + QStringLiteral("/%1").arg(actorIndex);
    compareNumber(errors, 20.0, attrs.value(QStringLiteral("cx")),
                  actorPath + QStringLiteral("/cx"));
    compareNumber(errors, actor.y, attrs.value(QStringLiteral("cy")),
                  actorPath + QStringLiteral("/cy"));
    const int expectedPosition =
        classIndex(attrs.value(QStringLiteral("class")).toString(),
                   QStringLiteral("actor"));
    if (actor.position != expectedPosition)
      errors << actorPath + QStringLiteral("/position mismatch");
    compareColor(errors, actor.color, attrs.value(QStringLiteral("fill")).toString(),
                 actorPath + QStringLiteral("/fill"));

    for (qsizetype lineIndex = 0; lineIndex < actor.lines.size(); ++lineIndex) {
      if (textIndex >= texts.size()) {
        errors << actorPath + QStringLiteral(": missing legend text");
        ++textIndex;
        continue;
      }
      const QJsonObject text = texts.at(textIndex).toObject();
      const QJsonObject textAttrs = text.value(QStringLiteral("attrs")).toObject();
      const QString linePath = actorPath + QStringLiteral("/line%1").arg(lineIndex);
      const QString expectedText = text.value(QStringLiteral("text")).toString();
      if (actor.lines.at(lineIndex) != expectedText)
        errors << linePath + QStringLiteral(": text '%1' != '%2'")
                                 .arg(actor.lines.at(lineIndex), expectedText);
      compareNumber(errors, 40.0, textAttrs.value(QStringLiteral("x")),
                    linePath + QStringLiteral("/x"));
      compareNumber(errors, actor.y + 7.0 + lineIndex * 20.0,
                    textAttrs.value(QStringLiteral("y")),
                    linePath + QStringLiteral("/y"));
      const QJsonArray tspans = text.value(QStringLiteral("tspans")).toArray();
      if (tspans.size() != 1) {
        errors << linePath + QStringLiteral(": expected one tspan");
      } else {
        const QJsonObject tspan = tspans.at(0).toObject();
        if (tspan.value(QStringLiteral("text")).toString() != actor.lines.at(lineIndex))
          errors << linePath + QStringLiteral("/tspan: text '%1' != '%2'")
                                   .arg(actor.lines.at(lineIndex),
                                        tspan.value(QStringLiteral("text")).toString());
        compareNumber(errors, 40.0 + 2.0 * scene.config.boxTextMargin,
                      tspan.value(QStringLiteral("attrs"))
                          .toObject()
                          .value(QStringLiteral("x")),
                      linePath + QStringLiteral("/tspan/x"));
      }
      ++textIndex;
    }
  }
  if (textIndex != texts.size())
    errors << path + QStringLiteral(": legend text count %1 != %2")
                         .arg(textIndex)
                         .arg(texts.size());
}

}  // namespace

int main(int argc, char** argv) {
#if defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; macOS
  // (SF/Helvetica) resolves different faces with different metrics.
  // Bundled-font goldens are the eventual closure.
  qWarning("skipped on macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Journey geometry fixture path"));

  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QJsonParseError jsonError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &jsonError);
  require(jsonError.error == QJsonParseError::NoError,
          QStringLiteral("Journey geometry JSON: ") + jsonError.errorString());
  const QJsonObject root = document.object();
  require(root.value(QStringLiteral("upstream"))
                  .toObject()
                  .value(QStringLiteral("version"))
                  .toString() == QStringLiteral("11.16.0"),
          QStringLiteral("Journey geometry: Mermaid version drifted"));
  require(root.value(QStringLiteral("oracle")).toString().contains(
              QStringLiteral("Journey renderer SVG")),
          QStringLiteral("Journey geometry: oracle contract drifted"));

  int visited = 0;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    ++visited;
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();

    editor::MermaidRenderCache cache;
    const editor::MermaidRenderEntry entry =
        cache.getSync(cache.makeKey(source), source);
    const auto* scene = dynamic_cast<const journey::JourneyScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && scene != nullptr,
            id + QStringLiteral(": native Journey render failed: ") +
                entry.errorMessage);

    QStringList errors;
    compareRoot(errors, *scene, expected, id + QStringLiteral("/root"));
    compareTitle(errors, *scene, expected, id + QStringLiteral("/title"));
    compareBottomLine(errors, *scene, expected,
                      id + QStringLiteral("/bottomLine"));
    compareSections(errors, *scene, expected, id + QStringLiteral("/sections"));
    compareTasks(errors, *scene, expected, id + QStringLiteral("/tasks"));
    compareActors(errors, *scene, expected, id + QStringLiteral("/actors"));

    if (!errors.isEmpty()) {
      for (const QString& error : errors)
        std::fprintf(stderr, "%s\n", qPrintable(error));
      fail(id + QStringLiteral(": Journey geometry parity regression"));
    }
  }

  require(visited == root.value(QStringLiteral("cases")).toArray().size() &&
              visited > 0,
          QStringLiteral("Journey geometry fixture was not fully visited"));
  return 0;
}
