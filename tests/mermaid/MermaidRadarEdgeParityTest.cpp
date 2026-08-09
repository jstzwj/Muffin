// Radar source-config, theme, degenerate-geometry, color-limit, and painter
// parity. The 76-case config table is a direct Mermaid 11.16.0 source-entry
// oracle; focused cases below cover behavior that does not fit that table.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/radar/RadarScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QPainter>
#include <QPointF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
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
  require(ok, QStringLiteral("Invalid Radar config oracle number: ") + text);
  return value;
}

double oracleNumber(const QJsonValue& value) {
  return value.isDouble() ? value.toDouble() : oracleNumber(value.toString());
}

void compareNumber(QStringList& errors, double actual, double expected,
                   const QString& path) {
  if (std::isnan(expected)) {
    if (!std::isnan(actual)) errors << path + QStringLiteral(": expected NaN");
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
          QStringLiteral("Odd Radar point coordinate count"));
  QVector<QPointF> result;
  for (qsizetype i = 0; i < values.size(); i += 2)
    result.append(QPointF(values.at(i), values.at(i + 1)));
  return result;
}

void comparePoint(QStringList& errors, const QPointF& actual,
                  const QPointF& expected, const QString& path) {
  compareNumber(errors, actual.x(), expected.x(), path + QStringLiteral("/x"));
  compareNumber(errors, actual.y(), expected.y(), path + QStringLiteral("/y"));
}

void comparePoints(QStringList& errors, const QVector<QPointF>& actual,
                   const QVector<QPointF>& expected, const QString& path) {
  if (actual.size() != expected.size()) {
    errors << path + QStringLiteral("/count mismatch");
    return;
  }
  for (qsizetype i = 0; i < actual.size(); ++i)
    comparePoint(errors, actual.at(i), expected.at(i),
                 path + QStringLiteral("/%1").arg(i));
}

std::shared_ptr<const radar::RadarScene> renderScene(
    const QString& source, editor::MermaidRenderEntry* output = nullptr) {
  editor::MermaidRenderCache cache;
  const editor::MermaidRenderEntry entry =
      cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          QStringLiteral("Radar source did not render: ") + entry.errorMessage);
  const auto scene =
      std::dynamic_pointer_cast<const radar::RadarScene>(entry.scene);
  require(bool(scene), QStringLiteral("Radar scene has the wrong type"));
  if (output) *output = entry;
  return scene;
}

QImage paintScene(const radar::RadarScene& scene) {
  const QRectF bounds = scene.renderBounds();
  require(bounds.width() > 0.0 && bounds.height() > 0.0 &&
              bounds.width() <= 4096.0 && bounds.height() <= 4096.0,
          QStringLiteral("Refusing an unbounded Radar edge-test image"));
  QImage image(qCeil(bounds.width()), qCeil(bounds.height()),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.translate(-bounds.left(), -bounds.top());
  scene.paint(painter, MermaidPaintOptions{});
  painter.end();
  return image;
}

int nearColorPixels(const QImage& image, const QColor& expected,
                    int tolerance = 10) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QColor actual = image.pixelColor(x, y);
      if (actual.alpha() < 24) continue;
      count += std::abs(actual.red() - expected.red()) <= tolerance &&
               std::abs(actual.green() - expected.green()) <= tolerance &&
               std::abs(actual.blue() - expected.blue()) <= tolerance;
    }
  }
  return count;
}

void comparePaint(QStringList& errors, const QString& raw,
                  color::SvgPaintKind kind, const QColor& inherited,
                  const QString& expected, const QString& path) {
  const color::SvgPaint actual = color::resolveSvgPaint(raw, kind, inherited);
  if (expected == QLatin1String("none")) {
    if (!actual.none) errors << path + QStringLiteral(": expected none");
    return;
  }
  const QColor oracle = color::toQColor(expected);
  if (actual.none || !oracle.isValid() || actual.color.rgba() != oracle.rgba())
    errors << path + QStringLiteral(": paint mismatch");
}

QJsonValue withoutNullMembers(const QJsonValue& value) {
  if (value.isObject()) {
    QJsonObject result;
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
      if (!it.value().isNull()) result.insert(it.key(), withoutNullMembers(it.value()));
    }
    return result;
  }
  if (value.isArray()) {
    QJsonArray result;
    for (const QJsonValue& item : value.toArray())
      result.append(withoutNullMembers(item));
    return result;
  }
  return value;
}

void compareConfigCase(QStringList& errors, const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const QString source = fixture.value(QStringLiteral("source")).toString();
  const MermaidPreprocessResult pre = preprocessDiagram(source);
  // Muffin retains JSON null until the family adapter applies its fallback;
  // Mermaid's source-entry snapshot removes the member earlier. Compare the
  // effective sanitized trees while retaining strict string/bool/array/object
  // distinctions for every non-null value.
  if (withoutNullMembers(pre.config).toObject() !=
      fixture.value(QStringLiteral("sanitizedConfig")).toObject())
    errors << id + QStringLiteral("/sanitizedConfig mismatch");

  editor::MermaidRenderEntry entry;
  const auto scene = renderScene(source, &entry);
  const QJsonObject expected =
      fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QVector<double> box =
      numbers(root.value(QStringLiteral("viewBox")).toString());
  if (box.size() != 4) {
    errors << id + QStringLiteral("/viewBox invalid");
  } else {
    compareNumber(errors, scene->bounds.x(), box.at(0), id + QStringLiteral("/bounds/x"));
    compareNumber(errors, scene->bounds.y(), box.at(1), id + QStringLiteral("/bounds/y"));
    compareNumber(errors, scene->bounds.width(), box.at(2), id + QStringLiteral("/bounds/w"));
    compareNumber(errors, scene->bounds.height(), box.at(3), id + QStringLiteral("/bounds/h"));
  }
  const bool useMax = root.value(QStringLiteral("width")).toString() ==
                      QLatin1String("100%");
  if (entry.metadata.svgUseMaxWidth != useMax)
    errors << id + QStringLiteral("/useMaxWidth mismatch");

  const QVector<double> frame =
      numbers(expected.value(QStringLiteral("frameTransform")).toString());
  if (frame.size() == 2)
    comparePoint(errors, scene->center, QPointF(frame.at(0), frame.at(1)),
                 id + QStringLiteral("/center"));
  else
    errors << id + QStringLiteral("/frameTransform invalid");

  const QJsonObject ring = expected.value(QStringLiteral("graticule")).toObject();
  const QJsonObject ringAttrs =
      ring.value(QStringLiteral("attributes")).toObject();
  if (scene->graticules.isEmpty()) {
    errors << id + QStringLiteral("/missing graticule");
  } else if (ring.value(QStringLiteral("tag")).toString() == QLatin1String("circle")) {
    compareNumber(errors, scene->graticules.front().radius,
                  ringAttrs.value(QStringLiteral("r")),
                  id + QStringLiteral("/graticule/r"));
  } else {
    comparePoints(errors, scene->graticules.front().points,
                  points(ringAttrs.value(QStringLiteral("points")).toString()),
                  id + QStringLiteral("/graticule/points"));
  }

  const QJsonObject axis = expected.value(QStringLiteral("axis")).toObject();
  const QJsonObject axisAttrs =
      axis.value(QStringLiteral("attributes")).toObject();
  const QJsonObject label = expected.value(QStringLiteral("axisLabel")).toObject();
  const QJsonObject labelAttrs =
      label.value(QStringLiteral("attributes")).toObject();
  if (scene->axes.isEmpty()) {
    errors << id + QStringLiteral("/missing axis");
  } else {
    comparePoint(errors, scene->axes.front().end,
                 QPointF(oracleNumber(axisAttrs.value(QStringLiteral("x2"))),
                         oracleNumber(axisAttrs.value(QStringLiteral("y2")))),
                 id + QStringLiteral("/axis/end"));
    comparePoint(errors, scene->axes.front().labelPosition,
                 QPointF(oracleNumber(labelAttrs.value(QStringLiteral("x"))),
                         oracleNumber(labelAttrs.value(QStringLiteral("y")))),
                 id + QStringLiteral("/axis/label"));
  }

  const QJsonObject curve = expected.value(QStringLiteral("curve")).toObject();
  const QJsonObject curveAttrs =
      curve.value(QStringLiteral("attributes")).toObject();
  if (scene->curves.isEmpty()) {
    errors << id + QStringLiteral("/missing curve");
  } else {
    comparePoints(errors, scene->curves.front().points,
                  points(curveAttrs.value(QStringLiteral("points")).toString()),
                  id + QStringLiteral("/curve/points"));
  }

  const QColor inherited = color::toQColor(scene->style.textColor);
  const QJsonObject ringStyle = ring.value(QStringLiteral("style")).toObject();
  comparePaint(errors, scene->style.graticuleColor,
               color::SvgPaintKind::Fill, inherited,
               ringStyle.value(QStringLiteral("fill")).toString(),
               id + QStringLiteral("/style/graticuleFill"));
  comparePaint(errors, scene->style.graticuleColor,
               color::SvgPaintKind::Stroke, inherited,
               ringStyle.value(QStringLiteral("stroke")).toString(),
               id + QStringLiteral("/style/graticuleStroke"));
  compareNumber(errors, scene->style.graticuleOpacity,
                ringStyle.value(QStringLiteral("fill-opacity")),
                id + QStringLiteral("/style/graticuleOpacity"));
  compareNumber(errors, scene->style.graticuleStrokeWidth,
                ringStyle.value(QStringLiteral("stroke-width")),
                id + QStringLiteral("/style/graticuleStrokeWidth"));

  const QJsonObject axisStyle = axis.value(QStringLiteral("style")).toObject();
  comparePaint(errors, scene->style.axisColor, color::SvgPaintKind::Stroke,
               inherited, axisStyle.value(QStringLiteral("stroke")).toString(),
               id + QStringLiteral("/style/axisStroke"));
  compareNumber(errors, scene->style.axisStrokeWidth,
                axisStyle.value(QStringLiteral("stroke-width")),
                id + QStringLiteral("/style/axisStrokeWidth"));
  compareNumber(errors, scene->style.axisLabelFontSize,
                label.value(QStringLiteral("style")).toObject()
                    .value(QStringLiteral("font-size")),
                id + QStringLiteral("/style/axisLabelFontSize"));

  const QJsonObject curveStyle = curve.value(QStringLiteral("style")).toObject();
  if (!scene->curves.isEmpty()) {
    comparePaint(errors, scene->curves.front().color,
                 color::SvgPaintKind::Fill, inherited,
                 curveStyle.value(QStringLiteral("fill")).toString(),
                 id + QStringLiteral("/style/curveFill"));
  }
  compareNumber(errors, scene->style.curveOpacity,
                curveStyle.value(QStringLiteral("fill-opacity")),
                id + QStringLiteral("/style/curveOpacity"));
  compareNumber(errors, scene->style.curveStrokeWidth,
                curveStyle.value(QStringLiteral("stroke-width")),
                id + QStringLiteral("/style/curveStrokeWidth"));
  compareNumber(errors, scene->style.legendFontSize,
                expected.value(QStringLiteral("legendText")).toObject()
                    .value(QStringLiteral("style")).toObject()
                    .value(QStringLiteral("font-size")),
                id + QStringLiteral("/style/legendFontSize"));
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected radar config fixture path"));

  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QJsonParseError jsonError;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &jsonError);
  require(jsonError.error == QJsonParseError::NoError,
          QStringLiteral("Radar config JSON: ") + jsonError.errorString());
  const QJsonObject root = document.object();
  require(root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("6828d4b767ddc35f0fb5e601ddeb2e0b552bf81b71cb5c8872bd5b040095dab5"),
          QStringLiteral("Radar config fixture provenance drifted"));

  QStringList errors;
  const QJsonArray configCases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& caseValue : configCases)
    compareConfigCase(errors, caseValue.toObject());
  require(configCases.size() == 76,
          QStringLiteral("Radar config table was not fully visited"));
  if (!errors.isEmpty()) fail(errors.join(QLatin1Char('\n')));

  // Fractional ticks use the JavaScript `i < ticks` loop: 2.5 emits 3 rings,
  // including one outside the nominal radius.
  const auto fractional = renderScene(QStringLiteral(
      "radar-beta\naxis A,B,C\ncurve C {1,2,3}\nticks 2.5"));
  require(fractional->graticules.size() == 3 &&
              std::fabs(fractional->graticules.back().radius - 360.0) <
                  kTolerance,
          QStringLiteral("Fractional Radar ticks lost JS loop semantics"));

  // A mismatched numeric curve is omitted, but its legend item remains.
  const auto mismatch = renderScene(QStringLiteral(
      "radar-beta\naxis A,B,C\ncurve TooFew {1,2}"));
  require(mismatch->curves.isEmpty() && mismatch->legends.size() == 1 &&
              mismatch->legends.front().text == QLatin1String("TooFew"),
          QStringLiteral("Mismatched Radar curve/legend behavior drifted"));

  // max == min deliberately yields NaN SVG points; the scene retains those
  // numbers for structural parity and the painter skips the invalid path.
  const auto equalRange = renderScene(QStringLiteral(
      "radar-beta\naxis A,B,C\ncurve C {5,5,5}\nmin 5\nmax 5\n"
      "graticule polygon"));
  require(equalRange->curves.size() == 1 &&
              std::all_of(equalRange->curves.front().points.cbegin(),
                          equalRange->curves.front().points.cend(),
                          [](const QPointF& point) {
                            return std::isnan(point.x()) && std::isnan(point.y());
                          }),
          QStringLiteral("Equal Radar min/max must retain NaN curve geometry"));
  require(!paintScene(*equalRange).isNull(),
          QStringLiteral("Degenerate Radar scene must remain paintable"));

  const auto empty = renderScene(QStringLiteral("radar-beta"));
  require(empty->axes.isEmpty() && empty->curves.isEmpty() &&
              empty->legends.isEmpty() && empty->graticules.size() == 5,
          QStringLiteral("Empty Radar scene contract drifted"));

  // THEME_COLOR_LIMIT controls generated curve/legend CSS classes. Curves at
  // or beyond the limit retain SVG presentation defaults instead of cycling.
  QString tclSource = QStringLiteral(
      "%%{init: {\"themeVariables\": {\"THEME_COLOR_LIMIT\": 1, "
      "\"cScale0\": \"#ff0000\", \"cScale1\": \"#00ff00\"}}}%%\n"
      "radar-beta\naxis A,B,C\ngraticule polygon\n");
  for (int i = 0; i < 14; ++i)
    tclSource += QStringLiteral("curve C%1 {1,2,3}\n").arg(i);
  const auto limited = renderScene(tclSource);
  require(limited->curves.size() == 14 && limited->legends.size() == 14 &&
              limited->curves.at(0).classGenerated &&
              !limited->curves.at(1).classGenerated &&
              !limited->curves.at(13).classGenerated &&
              limited->legends.at(0).classGenerated &&
              !limited->legends.at(1).classGenerated,
          QStringLiteral("Radar THEME_COLOR_LIMIT class boundary drifted"));
  const auto paintedPalette = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"THEME_COLOR_LIMIT\": 1, "
      "\"cScale0\": \"#ff0000\"}}}%%\n"
      "radar-beta\naxis A,B,C\ncurve C {1,2,3}\ngraticule polygon"));
  const int paintedRed = nearColorPixels(
      paintScene(*paintedPalette), QColor(QStringLiteral("#ff0000")));
  require(paintedRed > 50,
          QStringLiteral("Generated Radar palette class did not reach paint "
                         "(scene=%1, pixels=%2)")
              .arg(paintedPalette->curves.front().color)
              .arg(paintedRed));

  // ThemeVariables.radar is live, while identically named radar config keys
  // are sanitizer-preserved but style-inert. Verify actual QPainter output.
  const QString themedSource = QStringLiteral(
      "%%{init: {\"themeVariables\": {\"cScale0\": \"#0000ff\", "
      "\"radar\": {\"axisColor\": \"#ff0000\", \"axisStrokeWidth\": 7, "
      "\"graticuleColor\": \"#00ff00\", \"graticuleOpacity\": 1, "
      "\"curveOpacity\": 1, \"curveStrokeWidth\": 8}}}}%%\n"
      "radar-beta\naxis A,B,C\ncurve C {1,2,3}\ngraticule polygon");
  const auto themed = renderScene(themedSource);
  const QImage themedImage = paintScene(*themed);
  require(nearColorPixels(themedImage, QColor(QStringLiteral("#ff0000"))) > 100 &&
              nearColorPixels(themedImage, QColor(QStringLiteral("#00ff00"))) > 100 &&
              nearColorPixels(themedImage, QColor(QStringLiteral("#0000ff"))) > 100,
          QStringLiteral("Radar theme colors did not reach actual paint"));

  // SVG text uses white-space:normal. Preserve the parser's raw strings, but
  // collapse and trim them at paint time for labels and legends.
  const auto compactWhitespace = renderScene(QStringLiteral(
      "radar-beta\naxis X[\"A B\"]\ncurve C[\"L Q\"] {1}"));
  const auto expandedWhitespace = renderScene(QStringLiteral(
      "radar-beta\naxis X[\"  A  B  \"]\ncurve C[\"L\t Q\"] {1}"));
  require(paintScene(*compactWhitespace) == paintScene(*expandedWhitespace),
          QStringLiteral("Radar SVG white-space normalization drifted"));
  const QString nbsp(1, QChar(0x00a0));
  const auto plainWhitespace = renderScene(QStringLiteral(
      "radar-beta\naxis X[\"A\"]\ncurve C {1}\nshowLegend false"));
  const auto nbspWhitespace = renderScene(
      QStringLiteral("radar-beta\naxis X[\"A") + nbsp +
      QStringLiteral("\"]\ncurve C {1}\nshowLegend false"));
  require(paintScene(*plainWhitespace) != paintScene(*nbspWhitespace),
          QStringLiteral("Radar SVG white-space incorrectly removed NBSP"));

  // Root SVG fill remains an inherited paint value. A none root hides an
  // unclassed curve; currentColor resolves against each element's CSS color.
  const QString nonePrefix = QStringLiteral(
      "%%{init: {\"themeVariables\": {\"textColor\": \"none\", "
      "\"THEME_COLOR_LIMIT\": 0}}}%%\nradar-beta\naxis A,B,C\n"
      "showLegend false\ngraticule polygon\n");
  const auto noneWithoutCurve = renderScene(nonePrefix);
  const auto noneWithCurve = renderScene(
      nonePrefix + QStringLiteral("curve C {1,2,3}\n"));
  require(paintScene(*noneWithoutCurve) == paintScene(*noneWithCurve),
          QStringLiteral("Radar root fill:none did not hide an unclassed curve"));

  const auto currentColor = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"textColor\": \"currentColor\", "
      "\"titleColor\": \"#ff0000\", \"radar\": {"
      "\"axisColor\": \"#00ff00\", \"axisStrokeWidth\": 0, "
      "\"axisLabelFontSize\": 24}}}}%%\nradar-beta\n"
      "title Colored\naxis A,B,C\nshowLegend false"));
  const QImage currentColorImage = paintScene(*currentColor);
  require(nearColorPixels(currentColorImage,
                          QColor(QStringLiteral("#ff0000"))) > 10 &&
              nearColorPixels(currentColorImage,
                              QColor(QStringLiteral("#00ff00"))) > 10,
          QStringLiteral("Radar root currentColor did not use element CSS color"));

  const QString circleBase = QStringLiteral(
      "radar-beta\naxis A,B,C,D\ncurve C {1,2,3,4}");
  const auto tension0 = renderScene(
      QStringLiteral("%%{init: {\"radar\": {\"curveTension\": 0}}}%%\n") +
      circleBase);
  const auto tension5 = renderScene(
      QStringLiteral("%%{init: {\"radar\": {\"curveTension\": 0.5}}}%%\n") +
      circleBase);
  require(!tension0->curves.isEmpty() && !tension5->curves.isEmpty() &&
              tension0->curves.front().cubics.front().control1 !=
                  tension5->curves.front().cubics.front().control1 &&
              paintScene(*tension0) != paintScene(*tension5),
          QStringLiteral("Radar curveTension did not affect cubic geometry/paint"));

  // Default/dark/redux-color all resolve through the shared theme model.
  const auto defaultTheme = renderScene(circleBase);
  const auto darkTheme = renderScene(
      QStringLiteral("%%{init: {\"theme\": \"dark\"}}%%\n") + circleBase);
  const auto reduxTheme = renderScene(
      QStringLiteral("%%{init: {\"theme\": \"redux-color\"}}%%\n") +
      circleBase);
  require(!defaultTheme->style.palette.isEmpty() &&
              !darkTheme->style.palette.isEmpty() &&
              !reduxTheme->style.palette.isEmpty() &&
              defaultTheme->style.palette.front() != darkTheme->style.palette.front() &&
              reduxTheme->style.palette.front() != darkTheme->style.palette.front(),
          QStringLiteral("Radar theme selection did not reach the palette"));

  std::puts("MermaidRadarEdgeParityTest: config/theme/degenerate/TCL/paint passed");
  return 0;
}
