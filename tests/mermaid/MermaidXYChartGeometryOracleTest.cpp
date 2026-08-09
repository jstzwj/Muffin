// XYChart geometry/config oracle. The fixtures are captured from Mermaid
// 11.16.0 with Noto Sans loaded before layout. Structure and paint attributes
// are exact; coordinates allow a small font-backend tolerance.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/xychart/XYChartScene.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {

// Chrome and Qt both use the bundled Noto face, but their DirectWrite shaping
// APIs quantize advances differently by up to roughly half a pixel.
constexpr double kGeometryTolerance = 0.75;

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

double number(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  QString text = value.toString().trimmed();
  if (text.isEmpty()) return 0.0;
  if (text.endsWith(QLatin1String("px"))) text.chop(2);
  bool ok = false;
  const double result = text.toDouble(&ok);
  require(ok, QStringLiteral("Invalid oracle number: ") + text);
  return result;
}

QVector<double> numbers(const QString& text) {
  static const QRegularExpression re(QStringLiteral(
      R"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)"));
  QVector<double> result;
  auto matches = re.globalMatch(text);
  while (matches.hasNext()) result.append(matches.next().captured().toDouble());
  return result;
}

void compareNumber(QStringList& errors, double actual, double expected,
                   const QString& path, double tolerance = kGeometryTolerance) {
  if (!std::isfinite(actual) || !std::isfinite(expected) ||
      std::fabs(actual - expected) > tolerance)
    errors << QStringLiteral("%1: %2 != %3")
                  .arg(path)
                  .arg(actual, 0, 'g', 17)
                  .arg(expected, 0, 'g', 17);
}

void compareColor(QStringList& errors, const QString& actual,
                  const QString& expected, const QString& path) {
  const QColor left = color::toQColor(actual);
  const QColor right = color::toQColor(expected);
  if (!left.isValid() || !right.isValid() || left.rgba() != right.rgba())
    errors << QStringLiteral("%1: '%2' != '%3'").arg(path, actual, expected);
}

QString anchorName(xychart::XYChartTextAnchor value) {
  if (value == xychart::XYChartTextAnchor::Start) return QStringLiteral("start");
  if (value == xychart::XYChartTextAnchor::End) return QStringLiteral("end");
  return QStringLiteral("middle");
}

QString baselineName(xychart::XYChartBaseline value) {
  if (value == xychart::XYChartBaseline::BeforeEdge)
    return QStringLiteral("text-before-edge");
  if (value == xychart::XYChartBaseline::Hanging)
    return QStringLiteral("hanging");
  if (value == xychart::XYChartBaseline::Auto)
    return QStringLiteral("auto");
  return QStringLiteral("middle");
}

std::shared_ptr<const xychart::XYChartScene> renderScene(
    const QString& source, editor::MermaidRenderEntry* rendered = nullptr) {
  editor::MermaidRenderCache cache;
  editor::MermaidRenderEntry entry =
      cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          QStringLiteral("XYChart render failed: ") + entry.errorMessage);
  const auto scene =
      std::dynamic_pointer_cast<const xychart::XYChartScene>(entry.scene);
  require(bool(scene), QStringLiteral("XYChart entry has wrong scene type"));
  if (rendered) *rendered = std::move(entry);
  return scene;
}

QString sceneAxisPrefix(const xychart::XYChartScene& scene,
                        const QString& oracleAxis) {
  if (scene.config.orientation == xychart::XYChartOrientation::Vertical)
    return oracleAxis == QLatin1String("bottom-axis")
               ? QStringLiteral("x-axis")
               : QStringLiteral("y-axis");
  return oracleAxis == QLatin1String("left-axis")
             ? QStringLiteral("x-axis")
             : QStringLiteral("y-axis");
}

void compareText(QStringList& errors, const xychart::XYChartTextGeometry& actual,
                 const QJsonObject& oracle, const QString& path) {
  const QJsonObject attrs = oracle.value(QStringLiteral("attrs")).toObject();
  const QString transformText = attrs.value(QStringLiteral("transform")).toString();
  if (transformText.isEmpty()) {
    compareNumber(errors, actual.position.x(), number(attrs.value(QStringLiteral("x"))),
                  path + QStringLiteral("/x"));
    compareNumber(errors, actual.position.y(), number(attrs.value(QStringLiteral("y"))),
                  path + QStringLiteral("/y"));
    compareNumber(errors, actual.rotation, 0.0, path + QStringLiteral("/rotation"), 0.001);
  } else {
    const QVector<double> transform = numbers(transformText);
    if (transform.size() != 3) {
      errors << path + QStringLiteral("/transform invalid");
    } else {
      compareNumber(errors, actual.position.x(), transform.at(0), path + "/x");
      compareNumber(errors, actual.position.y(), transform.at(1), path + "/y");
      compareNumber(errors, actual.rotation, transform.at(2), path + "/rotation",
                    0.001);
    }
  }
  if (actual.text != oracle.value(QStringLiteral("text")).toString())
    errors << path + QStringLiteral("/text mismatch");
  compareColor(errors, actual.fill, attrs.value(QStringLiteral("fill")).toString(),
               path + QStringLiteral("/fill"));
  compareNumber(errors, actual.fontSize,
                number(attrs.value(QStringLiteral("font-size"))),
                path + QStringLiteral("/font-size"), 0.001);
  if (anchorName(actual.anchor) !=
      attrs.value(QStringLiteral("text-anchor")).toString())
    errors << path + QStringLiteral("/text-anchor mismatch");
  if (baselineName(actual.baseline) !=
      attrs.value(QStringLiteral("dominant-baseline")).toString())
    errors << path + QStringLiteral("/dominant-baseline mismatch");
}

void comparePath(QStringList& errors, const xychart::XYChartPathGeometry& actual,
                 const QJsonObject& oracle, const QString& path) {
  const QJsonObject attrs = oracle.value(QStringLiteral("attrs")).toObject();
  const QVector<double> left = numbers(actual.path);
  const QVector<double> right = numbers(attrs.value(QStringLiteral("d")).toString());
  if (left.size() != right.size()) {
    errors << QStringLiteral("%1/d coordinate count %2 != %3")
                  .arg(path).arg(left.size()).arg(right.size());
  } else {
    for (qsizetype i = 0; i < left.size(); ++i)
      compareNumber(errors, left.at(i), right.at(i),
                    path + QStringLiteral("/d/%1").arg(i));
  }
  if (attrs.contains(QStringLiteral("fill")) &&
      actual.fill != attrs.value(QStringLiteral("fill")).toString())
    errors << path + QStringLiteral("/fill mismatch");
  compareColor(errors, actual.stroke,
               attrs.value(QStringLiteral("stroke")).toString(),
               path + QStringLiteral("/stroke"));
  compareNumber(errors, actual.strokeWidth,
                number(attrs.value(QStringLiteral("stroke-width"))),
                path + QStringLiteral("/stroke-width"), 0.001);
}

void compareRect(QStringList& errors, const xychart::XYChartRectGeometry& actual,
                 const QJsonObject& oracle, const QString& path) {
  const QJsonObject attrs = oracle.value(QStringLiteral("attrs")).toObject();
  compareNumber(errors, actual.rect.x(),
                number(attrs.value(QStringLiteral("x"))),
                path + QStringLiteral("/x"));
  compareNumber(errors, actual.rect.y(),
                number(attrs.value(QStringLiteral("y"))),
                path + QStringLiteral("/y"));
  compareNumber(errors, actual.rect.width(),
                number(attrs.value(QStringLiteral("width"))),
                path + QStringLiteral("/width"));
  compareNumber(errors, actual.rect.height(),
                number(attrs.value(QStringLiteral("height"))),
                path + QStringLiteral("/height"));
  compareColor(errors, actual.fill, attrs.value(QStringLiteral("fill")).toString(),
               path + QStringLiteral("/fill"));
  compareColor(errors, actual.stroke,
               attrs.value(QStringLiteral("stroke")).toString(),
               path + QStringLiteral("/stroke"));
  compareNumber(errors, actual.strokeWidth,
                number(attrs.value(QStringLiteral("stroke-width"))),
                path + QStringLiteral("/stroke-width"), 0.001);
}

template <typename T>
QVector<const T*> inGroup(const QVector<T>& values, const QString& group) {
  QVector<const T*> result;
  for (const T& value : values)
    if (value.group == group) result.append(&value);
  return result;
}

void compareLeafArray(QStringList& errors, const xychart::XYChartScene& scene,
                      const QJsonArray& leaves, const QString& group,
                      const QString& path) {
  if (leaves.isEmpty()) {
    if (!inGroup(scene.texts, group).isEmpty() ||
        !inGroup(scene.paths, group).isEmpty() ||
        !inGroup(scene.rects, group).isEmpty())
      errors << path + QStringLiteral(": expected empty group");
    return;
  }
  QJsonArray textLeaves;
  QJsonArray pathLeaves;
  QJsonArray rectLeaves;
  for (const QJsonValue& leaf : leaves) {
    const QString tag = leaf.toObject().value(QStringLiteral("tag")).toString();
    if (tag == QLatin1String("text")) textLeaves.append(leaf);
    else if (tag == QLatin1String("path")) pathLeaves.append(leaf);
    else if (tag == QLatin1String("rect")) rectLeaves.append(leaf);
    else errors << path + QStringLiteral(" unsupported tag: ") + tag;
  }
  if (!textLeaves.isEmpty()) {
    const auto actual = inGroup(scene.texts, group);
    if (actual.size() != textLeaves.size())
      errors << QStringLiteral("%1 text count %2 != %3").arg(path).arg(actual.size()).arg(textLeaves.size());
    for (qsizetype i = 0; i < std::min(actual.size(), textLeaves.size()); ++i)
      compareText(errors, *actual.at(i), textLeaves.at(i).toObject(),
                  path + QStringLiteral("/%1").arg(i));
  }
  if (!pathLeaves.isEmpty()) {
    const auto actual = inGroup(scene.paths, group);
    if (actual.size() != pathLeaves.size())
      errors << QStringLiteral("%1 path count %2 != %3").arg(path).arg(actual.size()).arg(pathLeaves.size());
    for (qsizetype i = 0; i < std::min(actual.size(), pathLeaves.size()); ++i)
      comparePath(errors, *actual.at(i), pathLeaves.at(i).toObject(),
                  path + QStringLiteral("/%1").arg(i));
  }
  if (!rectLeaves.isEmpty()) {
    const auto actual = inGroup(scene.rects, group);
    if (actual.size() != rectLeaves.size())
      errors << QStringLiteral("%1 rect count %2 != %3").arg(path).arg(actual.size()).arg(rectLeaves.size());
    for (qsizetype i = 0; i < std::min(actual.size(), rectLeaves.size()); ++i)
      compareRect(errors, *actual.at(i), rectLeaves.at(i).toObject(),
                  path + QStringLiteral("/%1").arg(i));
  }
}

void comparePlot(QStringList& errors, const xychart::XYChartScene& scene,
                 const QJsonObject& plot, const QString& id) {
  const QString cls = plot.value(QStringLiteral("class")).toString();
  const QString group = QStringLiteral("plot/") + cls;
  const QJsonArray children = plot.value(QStringLiteral("children")).toArray();
  QJsonArray direct;
  for (const QJsonValue& childValue : children) {
    const QJsonObject child = childValue.toObject();
    if (child.value(QStringLiteral("tag")).toString() == QLatin1String("g")) {
      compareLeafArray(errors, scene,
                       child.value(QStringLiteral("children")).toArray(),
                       group + QLatin1Char('/') +
                           child.value(QStringLiteral("class")).toString(),
                       id + QLatin1Char('/') + cls + QStringLiteral("/labels"));
    } else {
      direct.append(child);
    }
  }
  compareLeafArray(errors, scene, direct, group, id + QLatin1Char('/') + cls);
}

void compareAxis(QStringList& errors, const xychart::XYChartScene& scene,
                 const QJsonObject& axis, const QString& id) {
  if (axis.isEmpty()) return;
  const QString cls = axis.value(QStringLiteral("class")).toString();
  const QString prefix = sceneAxisPrefix(scene, cls);
  for (const QJsonValue& groupValue : axis.value(QStringLiteral("children")).toArray()) {
    const QJsonObject group = groupValue.toObject();
    const QString suffix = group.value(QStringLiteral("class")).toString();
    compareLeafArray(errors, scene,
                     group.value(QStringLiteral("children")).toArray(),
                     prefix + QLatin1Char('/') + suffix,
                     id + QLatin1Char('/') + cls + QLatin1Char('/') + suffix);
  }
}

void compareFullCase(QStringList& errors, const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene(fixture.value(QStringLiteral("source")).toString(), &entry);
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject attrs = expected.value(QStringLiteral("root")).toObject()
                                .value(QStringLiteral("attrs")).toObject();
  const QVector<double> viewBox = numbers(attrs.value(QStringLiteral("viewBox")).toString());
  if (viewBox.size() == 4) {
    compareNumber(errors, scene->bounds.x(), viewBox.at(0),
                  id + QStringLiteral("/bounds/x"), 0.001);
    compareNumber(errors, scene->bounds.y(), viewBox.at(1),
                  id + QStringLiteral("/bounds/y"), 0.001);
    compareNumber(errors, scene->bounds.width(), viewBox.at(2),
                  id + QStringLiteral("/bounds/width"), 0.001);
    compareNumber(errors, scene->bounds.height(), viewBox.at(3),
                  id + QStringLiteral("/bounds/height"), 0.001);
  } else {
    errors << id + QStringLiteral("/viewBox invalid");
  }
  if (!entry.metadata.svgUseMaxWidth)
    errors << id + QStringLiteral("/useMaxWidth must be hard-coded true");
  compareColor(errors, scene->style.backgroundColor,
               expected.value(QStringLiteral("background")).toObject()
                   .value(QStringLiteral("attrs")).toObject()
                   .value(QStringLiteral("fill")).toString(),
               id + QStringLiteral("/background"));

  const QJsonObject title = expected.value(QStringLiteral("title")).toObject();
  compareLeafArray(errors, *scene,
                   title.value(QStringLiteral("children")).toArray(),
                   QStringLiteral("chart-title"), id + QStringLiteral("/title"));
  for (const QJsonValue& plot : expected.value(QStringLiteral("plots")).toArray())
    comparePlot(errors, *scene, plot.toObject(), id);
  compareAxis(errors, *scene, expected.value(QStringLiteral("bottomAxis")).toObject(), id);
  compareAxis(errors, *scene, expected.value(QStringLiteral("leftAxis")).toObject(), id);
  compareAxis(errors, *scene, expected.value(QStringLiteral("topAxis")).toObject(), id);
  compareAxis(errors, *scene, expected.value(QStringLiteral("rightAxis")).toObject(), id);
}

void compareCompactLeaves(QStringList& errors,
                          const xychart::XYChartScene& scene,
                          const QJsonArray& leaves, const QString& group,
                          const QString& path) {
  if (leaves.isEmpty()) return;
  const auto compareOne = [&](const QJsonObject& leaf, bool last,
                              const QString& suffix) {
    const QString tag = leaf.value(QStringLiteral("tag")).toString();
    if (tag == QLatin1String("text")) {
      const auto actual = inGroup(scene.texts, group);
      if (actual.isEmpty()) errors << path + QStringLiteral(" text missing");
      else compareText(errors, *(last ? actual.back() : actual.front()), leaf,
                       path + suffix);
    } else if (tag == QLatin1String("path")) {
      const auto actual = inGroup(scene.paths, group);
      if (actual.isEmpty()) errors << path + QStringLiteral(" path missing");
      else comparePath(errors, *(last ? actual.back() : actual.front()), leaf,
                       path + suffix);
    } else if (tag == QLatin1String("rect")) {
      const auto actual = inGroup(scene.rects, group);
      if (actual.isEmpty()) errors << path + QStringLiteral(" rect missing");
      else compareRect(errors, *(last ? actual.back() : actual.front()), leaf,
                       path + suffix);
    } else {
      errors << path + QStringLiteral(" unsupported tag: ") + tag;
    }
  };
  compareOne(leaves.first().toObject(), false, QStringLiteral("/first"));
  if (leaves.size() > 1)
    compareOne(leaves.last().toObject(), true, QStringLiteral("/last"));
}

void compareCompactAxis(QStringList& errors, const xychart::XYChartScene& scene,
                        const QJsonObject& axis, const QString& oracleClass,
                        const QString& id) {
  const QString prefix = sceneAxisPrefix(scene, oracleClass);
  struct Part { const char* key; const char* suffix; const char* countKey; };
  constexpr Part parts[] = {{"axisLines","axis-line",nullptr},
                            {"labels","label","labelCount"},
                            {"ticks","ticks","tickCount"},
                            {"titles","title",nullptr}};
  for (const Part& part : parts) {
    const QJsonArray leaves = axis.value(QLatin1String(part.key)).toArray();
    const QString suffix = oracleClass == QLatin1String("left-axis") &&
                                   QLatin1String(part.suffix) == QLatin1String("axis-line")
                               ? QStringLiteral("axisl-line")
                               : QLatin1String(part.suffix);
    const QString group = prefix + QLatin1Char('/') + suffix;
    if (part.countKey) {
      const int expectedCount = axis.value(QLatin1String(part.countKey)).toInt();
      int actualCount = 0;
      if (QLatin1String(part.suffix) == QLatin1String("label"))
        actualCount = inGroup(scene.texts, group).size();
      else
        actualCount = inGroup(scene.paths, group).size();
      if (actualCount != expectedCount)
        errors << QStringLiteral("%1/%2 count %3 != %4")
                      .arg(id, QLatin1String(part.suffix))
                      .arg(actualCount).arg(expectedCount);
    }
    // Compact fixtures intentionally retain only first/last leaves. Compare
    // those against first/last production primitives without weakening counts.
    if (leaves.isEmpty()) continue;
    if (QLatin1String(part.suffix) == QLatin1String("label") ||
        QLatin1String(part.suffix) == QLatin1String("title")) {
      const auto actual = inGroup(scene.texts, group);
      if (actual.isEmpty()) {
        errors << id + QLatin1Char('/') + QLatin1String(part.suffix) +
                      QStringLiteral(" missing");
        continue;
      }
      compareText(errors, *actual.front(), leaves.first().toObject(),
                  id + QLatin1Char('/') + QLatin1String(part.suffix) +
                      QStringLiteral("/first"));
      if (leaves.size() > 1)
        compareText(errors, *actual.back(), leaves.last().toObject(),
                    id + QLatin1Char('/') + QLatin1String(part.suffix) +
                        QStringLiteral("/last"));
    } else {
      const auto actual = inGroup(scene.paths, group);
      if (actual.isEmpty()) {
        errors << id + QLatin1Char('/') + QLatin1String(part.suffix) +
                      QStringLiteral(" missing");
        continue;
      }
      comparePath(errors, *actual.front(), leaves.first().toObject(),
                  id + QLatin1Char('/') + QLatin1String(part.suffix) +
                      QStringLiteral("/first"));
      if (leaves.size() > 1)
        comparePath(errors, *actual.back(), leaves.last().toObject(),
                    id + QLatin1Char('/') + QLatin1String(part.suffix) +
                        QStringLiteral("/last"));
    }
  }
}

void compareCompactCase(QStringList& errors, const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  editor::MermaidRenderEntry entry;
  const auto scene = renderScene(fixture.value(QStringLiteral("source")).toString(), &entry);
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QVector<double> viewBox = numbers(
      root.value(QStringLiteral("attrs")).toObject()
          .value(QStringLiteral("viewBox")).toString());
  if (viewBox.size() == 4) {
    compareNumber(errors, scene->bounds.width(), viewBox.at(2),
                  id + QStringLiteral("/width"), 0.001);
    compareNumber(errors, scene->bounds.height(), viewBox.at(3),
                  id + QStringLiteral("/height"), 0.001);
  } else {
    errors << id + QStringLiteral("/viewBox invalid");
  }
  if (!entry.metadata.svgUseMaxWidth)
    errors << id + QStringLiteral("/useMaxWidth drifted");
  compareColor(errors, scene->style.backgroundColor,
               expected.value(QStringLiteral("background")).toObject()
                   .value(QStringLiteral("attrs")).toObject()
                   .value(QStringLiteral("fill")).toString(),
               id + QStringLiteral("/background"));

  compareLeafArray(errors, *scene, expected.value(QStringLiteral("title")).toArray(),
                   QStringLiteral("chart-title"), id + QStringLiteral("/title"));
  for (const QJsonValue& plotValue : expected.value(QStringLiteral("plots")).toArray()) {
    const QJsonObject plot = plotValue.toObject();
    const QString group = QStringLiteral("plot/") + plot.value(QStringLiteral("class")).toString();
    const int expectedCount = plot.value(QStringLiteral("childCount")).toInt();
    const int actualCount = inGroup(scene->rects, group).size() +
                            inGroup(scene->paths, group).size() +
                            inGroup(scene->texts, group).size() +
                            (!inGroup(scene->texts,
                                     group + QStringLiteral("/labels")).isEmpty());
    if (actualCount != expectedCount)
      errors << QStringLiteral("%1/%2 child count %3 != %4")
                    .arg(id, group).arg(actualCount).arg(expectedCount);
    QJsonArray direct;
    for (const QJsonValue& childValue :
         plot.value(QStringLiteral("children")).toArray()) {
      const QJsonObject child = childValue.toObject();
      if (child.value(QStringLiteral("tag")).toString() == QLatin1String("g")) {
        compareCompactLeaves(
            errors, *scene, child.value(QStringLiteral("children")).toArray(),
            group + QLatin1Char('/') +
                child.value(QStringLiteral("class")).toString(),
            id + QLatin1Char('/') + group + QStringLiteral("/labels"));
      } else {
        direct.append(child);
      }
    }
    compareCompactLeaves(errors, *scene, direct, group,
                         id + QLatin1Char('/') + group);
  }
  const QJsonObject bottom = expected.value(QStringLiteral("bottomAxis")).toObject();
  const QJsonObject left = expected.value(QStringLiteral("leftAxis")).toObject();
  const QJsonObject top = expected.value(QStringLiteral("topAxis")).toObject();
  if (!bottom.isEmpty()) compareCompactAxis(errors, *scene, bottom, "bottom-axis", id);
  if (!left.isEmpty()) compareCompactAxis(errors, *scene, left, "left-axis", id);
  if (!top.isEmpty()) compareCompactAxis(errors, *scene, top, "top-axis", id);
}

QJsonObject loadFixture(const QString& path, const QString& label) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), label + QStringLiteral(": ") + file.errorString());
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError,
          label + QStringLiteral(": ") + error.errorString());
  return document.object();
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2,
          QStringLiteral("Expected XYChart geometry fixture path"));

  const QString geometryPath = QString::fromLocal8Bit(argv[1]);
  const QJsonObject geometry = loadFixture(geometryPath, QStringLiteral("geometry"));
  const QString configPath = QFileInfo(geometryPath).dir().filePath(
      QStringLiteral("xychart-config.json"));
  const QJsonObject config =
      loadFixture(configPath, QStringLiteral("config"));
  require(geometry.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("751143dd70961c3cf94a43de7dad40c129b77e7454e384693e5348a9982dfc64") &&
              config.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("efcc25f4a7fc4d975872a5db951948a28ba7546f95549ab38a18c04c95f0809d"),
          QStringLiteral("XYChart fixture provenance drifted"));

  QStringList errors;
  const QJsonArray geometryCases = geometry.value(QStringLiteral("cases")).toArray();
  const QJsonArray configCases = config.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : geometryCases)
    compareFullCase(errors, value.toObject());
  for (const QJsonValue& value : configCases)
    compareCompactCase(errors, value.toObject());
  require(geometryCases.size() == 16,
          QStringLiteral("XYChart geometry table was not fully visited"));
  require(configCases.size() == 55,
          QStringLiteral("XYChart config table was not fully visited"));
  if (!errors.isEmpty()) fail(errors.join(QLatin1Char('\n')));

  std::puts("MermaidXYChartGeometryOracleTest: 16 geometry + 55 config cases passed");
  return 0;
}
