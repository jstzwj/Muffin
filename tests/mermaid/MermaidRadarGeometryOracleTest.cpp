// Radar geometry oracle. SVG numbers captured from Mermaid 11.16.0 are
// compared with the production RadarScene at 0.001 logical px, including the
// complete cubic path rather than only its vertices.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/radar/RadarScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QPointF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

using namespace muffin::mermaid;

namespace {

constexpr double kTolerance = 0.001;

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

double oracleNumber(QString text) {
  text = text.trimmed();
  if (text.endsWith(QLatin1String("px"))) text.chop(2);
  if (text == QLatin1String("NaN"))
    return std::numeric_limits<double>::quiet_NaN();
  if (text == QLatin1String("Infinity") || text == QLatin1String("+Infinity"))
    return std::numeric_limits<double>::infinity();
  if (text == QLatin1String("-Infinity"))
    return -std::numeric_limits<double>::infinity();
  bool ok = false;
  const double value = text.toDouble(&ok);
  require(ok, QStringLiteral("Invalid oracle number: '") + text + QLatin1Char('\''));
  return value;
}

double oracleNumber(const QJsonValue& value) {
  return value.isDouble() ? value.toDouble() : oracleNumber(value.toString());
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
    if (!std::isinf(actual) ||
        std::signbit(actual) != std::signbit(expected))
      errors << path + QStringLiteral(": infinity mismatch");
    return;
  }
  if (!std::isfinite(actual) || std::fabs(actual - expected) > kTolerance)
    errors << path + QStringLiteral(": %1 != %2")
                         .arg(actual, 0, 'g', 17)
                         .arg(expected, 0, 'g', 17);
  // SVG attribute serialization uses String(number), which serializes -0 as
  // "0". The browser oracle therefore cannot distinguish the sign here.
}

void compareNumber(QStringList& errors, double actual,
                   const QJsonValue& expected, const QString& path) {
  compareNumber(errors, actual, oracleNumber(expected), path);
}

QVector<double> numbers(const QString& text) {
  static const QRegularExpression number(
      QStringLiteral(R"((?:-?Infinity|NaN|[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?))"));
  QVector<double> result;
  auto matches = number.globalMatch(text);
  while (matches.hasNext()) result.append(oracleNumber(matches.next().captured()));
  return result;
}

QVector<QPointF> points(const QString& text) {
  const QVector<double> values = numbers(text);
  require(values.size() % 2 == 0,
          QStringLiteral("Odd SVG point coordinate count: ") + text);
  QVector<QPointF> result;
  result.reserve(values.size() / 2);
  for (qsizetype i = 0; i < values.size(); i += 2)
    result.append(QPointF(values.at(i), values.at(i + 1)));
  return result;
}

QRectF viewBox(const QString& text) {
  const QVector<double> values = numbers(text);
  require(values.size() == 4, QStringLiteral("Invalid radar viewBox: ") + text);
  return QRectF(values.at(0), values.at(1), values.at(2), values.at(3));
}

void comparePoint(QStringList& errors, const QPointF& actual,
                  const QPointF& expected, const QString& path) {
  compareNumber(errors, actual.x(), expected.x(), path + QStringLiteral("/x"));
  compareNumber(errors, actual.y(), expected.y(), path + QStringLiteral("/y"));
}

void comparePointArray(QStringList& errors, const QVector<QPointF>& actual,
                       const QVector<QPointF>& expected, const QString& path) {
  if (actual.size() != expected.size()) {
    errors << path + QStringLiteral(": point count %1 != %2")
                         .arg(actual.size())
                         .arg(expected.size());
    return;
  }
  for (qsizetype i = 0; i < actual.size(); ++i)
    comparePoint(errors, actual.at(i), expected.at(i),
                 path + QStringLiteral("/%1").arg(i));
}

void compareColor(QStringList& errors, const QString& actual,
                  const QString& expected, const QString& path) {
  const QColor left = color::toQColor(actual);
  const QColor right = color::toQColor(expected);
  if (!left.isValid() || !right.isValid() || left.rgba() != right.rgba())
    errors << path + QStringLiteral(": '%1' != '%2'").arg(actual, expected);
}

QString anchorName(radar::RadarTextAnchor value) {
  if (value == radar::RadarTextAnchor::Start) return QStringLiteral("start");
  if (value == radar::RadarTextAnchor::End) return QStringLiteral("end");
  return QStringLiteral("middle");
}

QString baselineName(radar::RadarBaseline value) {
  if (value == radar::RadarBaseline::Auto) return QStringLiteral("auto");
  if (value == radar::RadarBaseline::Hanging) return QStringLiteral("hanging");
  return QStringLiteral("central");
}

std::shared_ptr<const radar::RadarScene> renderScene(
    const QString& source, editor::MermaidRenderEntry& entry) {
  editor::MermaidRenderCache cache;
  entry = cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          QStringLiteral("Radar render failed: ") + entry.errorMessage);
  const auto scene =
      std::dynamic_pointer_cast<const radar::RadarScene>(entry.scene);
  require(bool(scene), QStringLiteral("Radar entry has the wrong scene type"));
  return scene;
}

void compareCase(const radar::RadarScene& scene,
                 const editor::MermaidRenderEntry& entry,
                 const QJsonObject& expected, const QString& id,
                 QStringList& errors) {
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QRectF oracleBounds = viewBox(root.value(QStringLiteral("viewBox")).toString());
  const QRectF bounds = scene.sceneBounds();
  compareNumber(errors, bounds.x(), oracleBounds.x(), id + QStringLiteral("/bounds/x"));
  compareNumber(errors, bounds.y(), oracleBounds.y(), id + QStringLiteral("/bounds/y"));
  compareNumber(errors, bounds.width(), oracleBounds.width(),
                id + QStringLiteral("/bounds/width"));
  compareNumber(errors, bounds.height(), oracleBounds.height(),
                id + QStringLiteral("/bounds/height"));
  const bool useMaxWidth = root.value(QStringLiteral("width")).toString() ==
                           QLatin1String("100%");
  if (entry.metadata.svgUseMaxWidth != useMaxWidth)
    errors << id + QStringLiteral("/root/useMaxWidth mismatch");

  const QVector<double> frame = numbers(
      expected.value(QStringLiteral("frameTransform")).toString());
  if (frame.size() != 2) {
    errors << id + QStringLiteral("/frameTransform invalid");
  } else {
    comparePoint(errors, scene.center, QPointF(frame.at(0), frame.at(1)),
                 id + QStringLiteral("/center"));
  }

  const QJsonArray rings = expected.value(QStringLiteral("graticules")).toArray();
  if (scene.graticules.size() != rings.size())
    errors << id + QStringLiteral("/graticules count mismatch");
  const qsizetype ringCount = std::min(scene.graticules.size(), rings.size());
  for (qsizetype i = 0; i < ringCount; ++i) {
    const radar::RadarGraticuleGeometry& actual = scene.graticules.at(i);
    const QJsonObject oracle = rings.at(i).toObject();
    const QJsonObject attrs = oracle.value(QStringLiteral("attributes")).toObject();
    const QString path = id + QStringLiteral("/graticules/%1").arg(i);
    const bool circle = oracle.value(QStringLiteral("tag")).toString() ==
                        QLatin1String("circle");
    if (actual.circle != circle) errors << path + QStringLiteral("/shape mismatch");
    if (circle)
      compareNumber(errors, actual.radius, attrs.value(QStringLiteral("r")),
                    path + QStringLiteral("/radius"));
    else
      comparePointArray(errors, actual.points,
                        points(attrs.value(QStringLiteral("points")).toString()),
                        path + QStringLiteral("/points"));
  }
  if (!rings.isEmpty()) {
    const QJsonObject style = rings.first().toObject()
                                  .value(QStringLiteral("style")).toObject();
    compareColor(errors, scene.style.graticuleColor,
                 style.value(QStringLiteral("fill")).toString(),
                 id + QStringLiteral("/style/graticuleColor"));
    compareNumber(errors, scene.style.graticuleOpacity,
                  style.value(QStringLiteral("fill-opacity")),
                  id + QStringLiteral("/style/graticuleOpacity"));
    compareNumber(errors, scene.style.graticuleStrokeWidth,
                  style.value(QStringLiteral("stroke-width")),
                  id + QStringLiteral("/style/graticuleStrokeWidth"));
  }

  const QJsonArray axes = expected.value(QStringLiteral("axes")).toArray();
  const QJsonArray labels = expected.value(QStringLiteral("axisLabels")).toArray();
  if (scene.axes.size() != axes.size() || scene.axes.size() != labels.size())
    errors << id + QStringLiteral("/axes count mismatch");
  const qsizetype axisCount =
      std::min(scene.axes.size(), std::min(axes.size(), labels.size()));
  for (qsizetype i = 0; i < axisCount; ++i) {
    const radar::RadarAxisGeometry& actual = scene.axes.at(i);
    const QJsonObject axisAttrs = axes.at(i).toObject()
                                      .value(QStringLiteral("attributes")).toObject();
    const QJsonObject label = labels.at(i).toObject();
    const QJsonObject labelAttrs =
        label.value(QStringLiteral("attributes")).toObject();
    const QString path = id + QStringLiteral("/axes/%1").arg(i);
    comparePoint(errors, actual.end,
                 QPointF(oracleNumber(axisAttrs.value(QStringLiteral("x2"))),
                         oracleNumber(axisAttrs.value(QStringLiteral("y2")))),
                 path + QStringLiteral("/end"));
    comparePoint(errors, actual.labelPosition,
                 QPointF(oracleNumber(labelAttrs.value(QStringLiteral("x"))),
                         oracleNumber(labelAttrs.value(QStringLiteral("y")))),
                 path + QStringLiteral("/labelPosition"));
    if (actual.label != label.value(QStringLiteral("text")).toString())
      errors << path + QStringLiteral("/label text mismatch");
    if (anchorName(actual.textAnchor) !=
        labelAttrs.value(QStringLiteral("text-anchor")).toString())
      errors << path + QStringLiteral("/textAnchor mismatch");
    if (baselineName(actual.baseline) !=
        labelAttrs.value(QStringLiteral("dominant-baseline")).toString())
      errors << path + QStringLiteral("/baseline mismatch");
  }
  if (!axes.isEmpty()) {
    const QJsonObject axisStyle = axes.first().toObject()
                                      .value(QStringLiteral("style")).toObject();
    const QJsonObject labelStyle = labels.first().toObject()
                                        .value(QStringLiteral("style")).toObject();
    compareColor(errors, scene.style.axisColor,
                 axisStyle.value(QStringLiteral("stroke")).toString(),
                 id + QStringLiteral("/style/axisColor"));
    compareNumber(errors, scene.style.axisStrokeWidth,
                  axisStyle.value(QStringLiteral("stroke-width")),
                  id + QStringLiteral("/style/axisStrokeWidth"));
    compareNumber(errors, scene.style.axisLabelFontSize,
                  labelStyle.value(QStringLiteral("font-size")),
                  id + QStringLiteral("/style/axisLabelFontSize"));
  }

  const QJsonArray curves = expected.value(QStringLiteral("curves")).toArray();
  if (scene.curves.size() != curves.size())
    errors << id + QStringLiteral("/curves count mismatch");
  const qsizetype curveCount = std::min(scene.curves.size(), curves.size());
  for (qsizetype i = 0; i < curveCount; ++i) {
    const radar::RadarCurveGeometry& actual = scene.curves.at(i);
    const QJsonObject oracle = curves.at(i).toObject();
    const QJsonObject attrs = oracle.value(QStringLiteral("attributes")).toObject();
    const QJsonObject style = oracle.value(QStringLiteral("style")).toObject();
    const QString path = id + QStringLiteral("/curves/%1").arg(i);
    const bool polygon = oracle.value(QStringLiteral("tag")).toString() ==
                         QLatin1String("polygon");
    if (actual.polygon != polygon) errors << path + QStringLiteral("/shape mismatch");
    if (polygon) {
      comparePointArray(errors, actual.points,
                        points(attrs.value(QStringLiteral("points")).toString()),
                        path + QStringLiteral("/points"));
    } else {
      const QVector<double> pathNumbers =
          numbers(attrs.value(QStringLiteral("d")).toString());
      QVector<QPointF> flattened;
      if (!actual.points.isEmpty()) flattened.append(actual.points.front());
      for (const radar::RadarCubicSegment& segment : actual.cubics) {
        flattened.append(segment.control1);
        flattened.append(segment.control2);
        flattened.append(segment.end);
      }
      comparePointArray(errors, flattened,
                        [&] {
                          QVector<QPointF> result;
                          for (qsizetype n = 0; n + 1 < pathNumbers.size(); n += 2)
                            result.append(QPointF(pathNumbers.at(n), pathNumbers.at(n + 1)));
                          return result;
                        }(),
                        path + QStringLiteral("/path"));
    }
    compareColor(errors, actual.color,
                 style.value(QStringLiteral("fill")).toString(),
                 path + QStringLiteral("/color"));
    compareNumber(errors, scene.style.curveOpacity,
                  style.value(QStringLiteral("fill-opacity")),
                  path + QStringLiteral("/opacity"));
    compareNumber(errors, scene.style.curveStrokeWidth,
                  style.value(QStringLiteral("stroke-width")),
                  path + QStringLiteral("/strokeWidth"));
  }

  const QJsonArray boxes = expected.value(QStringLiteral("legendBoxes")).toArray();
  const QJsonArray legendTexts =
      expected.value(QStringLiteral("legendTexts")).toArray();
  if (scene.legends.size() != boxes.size() || scene.legends.size() != legendTexts.size())
    errors << id + QStringLiteral("/legends count mismatch");
  const qsizetype legendCount =
      std::min(scene.legends.size(), std::min(boxes.size(), legendTexts.size()));
  for (qsizetype i = 0; i < legendCount; ++i) {
    const radar::RadarLegendGeometry& actual = scene.legends.at(i);
    const QJsonObject box = boxes.at(i).toObject();
    const QJsonObject text = legendTexts.at(i).toObject();
    const QVector<double> translation =
        numbers(text.value(QStringLiteral("parentTransform")).toString());
    const QString path = id + QStringLiteral("/legends/%1").arg(i);
    if (translation.size() == 2)
      comparePoint(errors, actual.position,
                   QPointF(translation.at(0), translation.at(1)),
                   path + QStringLiteral("/position"));
    else
      errors << path + QStringLiteral("/parentTransform invalid");
    if (actual.text != text.value(QStringLiteral("text")).toString())
      errors << path + QStringLiteral("/text mismatch");
    const QJsonObject attrs = box.value(QStringLiteral("attributes")).toObject();
    compareNumber(errors, 12.0, attrs.value(QStringLiteral("width")),
                  path + QStringLiteral("/boxWidth"));
    compareNumber(errors, 12.0, attrs.value(QStringLiteral("height")),
                  path + QStringLiteral("/boxHeight"));
    const QJsonObject boxStyle = box.value(QStringLiteral("style")).toObject();
    compareColor(errors, actual.color,
                 boxStyle.value(QStringLiteral("fill")).toString(),
                 path + QStringLiteral("/color"));
    compareNumber(errors, scene.style.legendFontSize,
                  text.value(QStringLiteral("style")).toObject()
                      .value(QStringLiteral("font-size")),
                  path + QStringLiteral("/fontSize"));
  }

  const QJsonObject title = expected.value(QStringLiteral("title")).toObject();
  const QJsonObject titleAttrs =
      title.value(QStringLiteral("attributes")).toObject();
  if (scene.title != title.value(QStringLiteral("text")).toString())
    errors << id + QStringLiteral("/title/text mismatch");
  compareNumber(errors, 0.0, titleAttrs.value(QStringLiteral("x")),
                id + QStringLiteral("/title/x"));
  compareNumber(errors, -scene.config.height / 2.0 - scene.config.marginTop,
                titleAttrs.value(QStringLiteral("y")),
                id + QStringLiteral("/title/y"));
  compareNumber(errors, scene.style.titleFontSize,
                title.value(QStringLiteral("style")).toObject()
                    .value(QStringLiteral("font-size")),
                id + QStringLiteral("/title/fontSize"));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected radar geometry fixture path"));

  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QJsonParseError jsonError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &jsonError);
  require(jsonError.error == QJsonParseError::NoError,
          QStringLiteral("Radar geometry JSON: ") + jsonError.errorString());
  const QJsonObject root = document.object();
  require(root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("0a6983e68547c76e2f23d3b3afd6eae24ff096e24a1b4b9aa44815a24b9ece48"),
          QStringLiteral("Radar geometry fixture provenance drifted"));

  QStringList errors;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    editor::MermaidRenderEntry entry;
    const auto scene = renderScene(
        fixture.value(QStringLiteral("source")).toString(), entry);
    compareCase(*scene, entry,
                fixture.value(QStringLiteral("expected")).toObject(),
                fixture.value(QStringLiteral("id")).toString(), errors);
  }

  require(cases.size() == 11,
          QStringLiteral("Radar geometry fixture was not fully visited"));
  if (!errors.isEmpty()) fail(errors.join(QLatin1Char('\n')));
  std::puts("MermaidRadarGeometryOracleTest: 11 cases passed at 0.001 px");
  return 0;
}
