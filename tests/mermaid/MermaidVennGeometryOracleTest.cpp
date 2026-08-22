#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/venn/VennScene.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 1e-6) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3")
              .arg(path)
              .arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17));
}

QVector<qreal> numbers(const QString& text) {
  static const QRegularExpression expression(
      QStringLiteral(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)"));
  QVector<qreal> result;
  auto matches = expression.globalMatch(text);
  while (matches.hasNext()) result.append(matches.next().captured().toDouble());
  return result;
}

QColor computedColor(const QString& value) {
  static const QRegularExpression rgb(QStringLiteral(
      R"(^rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\))"));
  const auto match = rgb.match(value);
  if (!match.hasMatch()) return color::toQColor(value);
  QColor result(match.captured(1).toInt(), match.captured(2).toInt(),
                match.captured(3).toInt());
  if (!match.captured(4).isEmpty())
    result.setAlphaF(match.captured(4).toDouble());
  return result;
}

void samePaint(const QString& actual, const QString& expected,
               const QString& path) {
  if (expected == QLatin1String("none")) {
    require(actual.isEmpty() || actual == QLatin1String("none"),
            path + QStringLiteral(": expected none"));
    return;
  }
  const QColor left = computedColor(actual);
  const QColor right = computedColor(expected);
  require(left.isValid() && right.isValid() && left.rgba() == right.rgba(),
          path + QStringLiteral(": ") + actual + QStringLiteral(" != ") +
              expected);
}

void comparePathNumbers(const QString& actual, const QString& expected,
                        const QString& path) {
  const QVector<qreal> left = numbers(actual);
  const QVector<qreal> right = numbers(expected);
  require(left.size() == right.size(), path + QStringLiteral("/number count"));
  for (qsizetype i = 0; i < left.size(); ++i)
    near(left.at(i), right.at(i), QStringLiteral("%1/%2").arg(path).arg(i),
         1e-7);
}

qreal px(QString value) {
  value.remove(QStringLiteral("px"));
  return value.toDouble();
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
  require(argc == 2, QStringLiteral("Expected Venn geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "898a3845d1786af23b3a137f494a2bef75b9444d699239d4d0a282427be3c5ff"),
          QStringLiteral("Venn geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "b15e69bd29441a18d2da2c81df95243196b5db6d56937f569abc27bd419fe6ec"),
          QStringLiteral("Venn geometry provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 15, QStringLiteral("Expected 15 geometry cases"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source),
                                     source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": ") + entry.errorMessage);
    const auto scene =
        std::dynamic_pointer_cast<const venn::VennScene>(entry.scene);
    require(bool(scene), id + QStringLiteral(": expected VennScene"));
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject rootAttrs = expected.value(QStringLiteral("root")).toObject()
                                      .value(QStringLiteral("attrs")).toObject();
    const QVector<qreal> viewBox = numbers(rootAttrs.value(QStringLiteral("viewBox")).toString());
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
    near(scene->bounds.x(), viewBox.at(0), id + QStringLiteral("/viewBox/x"));
    near(scene->bounds.y(), viewBox.at(1), id + QStringLiteral("/viewBox/y"));
    near(scene->bounds.width(), viewBox.at(2), id + QStringLiteral("/viewBox/w"));
    near(scene->bounds.height(), viewBox.at(3), id + QStringLiteral("/viewBox/h"));
    require(scene->viewBoxAttribute == rootAttrs.value(QStringLiteral("viewBox")).toString(),
            id + QStringLiteral("/raw viewBox"));
    require(scene->useMaxWidth ==
                (rootAttrs.value(QStringLiteral("width")).toString() == QLatin1String("100%")),
            id + QStringLiteral("/useMaxWidth"));

    const QJsonArray areas = expected.value(QStringLiteral("areas")).toArray();
    require(scene->areas.size() == areas.size(), id + QStringLiteral("/area count"));
    for (qsizetype i = 0; i < areas.size(); ++i) {
      const auto& actual = scene->areas.at(i);
      const QJsonObject area = areas.at(i).toObject();
      const QJsonObject attrs = area.value(QStringLiteral("attrs")).toObject();
      const QString path = QStringLiteral("%1/area/%2").arg(id).arg(i);
      require(actual.cssClass == attrs.value(QStringLiteral("class")).toString(),
              path + QStringLiteral("/class"));
      require(actual.key.split(QLatin1Char('|')).join(QLatin1Char('_')) ==
                  attrs.value(QStringLiteral("data-venn-sets")).toString(),
              path + QStringLiteral("/sets"));
      const QJsonObject expectedPath = area.value(QStringLiteral("path")).toObject();
      const QJsonArray roughPaths = area.value(QStringLiteral("roughPaths")).toArray();
      if (expectedPath.isEmpty()) {
        require(actual.rough &&
                    actual.roughDrawable.sets.size() == roughPaths.size(),
                path + QStringLiteral("/rough path count"));
        continue;
      }
      comparePathNumbers(actual.rawPath,
                         expectedPath.value(QStringLiteral("attrs")).toObject()
                             .value(QStringLiteral("d")).toString(),
                         path + QStringLiteral("/path"));
      const QJsonObject pathComputed = expectedPath.value(QStringLiteral("computed")).toObject();
      const qreal expectedFillOpacity =
          pathComputed.value(QStringLiteral("fillOpacity")).toString().toDouble();
      if (expectedFillOpacity > 0.0)
        samePaint(actual.fill, pathComputed.value(QStringLiteral("fill")).toString(),
                  path + QStringLiteral("/fill"));
      samePaint(actual.stroke, pathComputed.value(QStringLiteral("stroke")).toString(),
                path + QStringLiteral("/stroke"));
      near(actual.fillOpacity, expectedFillOpacity,
           path + QStringLiteral("/fillOpacity"));
      near(actual.strokeOpacity,
           pathComputed.value(QStringLiteral("strokeOpacity")).toString().toDouble(),
           path + QStringLiteral("/strokeOpacity"));
      near(actual.strokeWidth,
           px(pathComputed.value(QStringLiteral("strokeWidth")).toString()),
           path + QStringLiteral("/strokeWidth"));

      const QJsonObject text = area.value(QStringLiteral("textElement")).toObject();
      const QJsonObject textAttrs = text.value(QStringLiteral("attrs")).toObject();
      require(actual.label.source == text.value(QStringLiteral("text")).toString(),
              path + QStringLiteral("/label"));
      near(actual.label.position.x(), textAttrs.value(QStringLiteral("x")).toString().toDouble(),
           path + QStringLiteral("/label/x"));
      near(actual.label.position.y(), textAttrs.value(QStringLiteral("y")).toString().toDouble(),
           path + QStringLiteral("/label/y"));
      near(actual.label.fontSize,
           px(text.value(QStringLiteral("computed")).toObject()
                  .value(QStringLiteral("fontSize")).toString()),
           path + QStringLiteral("/label/fontSize"));
      samePaint(actual.label.fill,
                text.value(QStringLiteral("computed")).toObject()
                    .value(QStringLiteral("fill")).toString(),
                path + QStringLiteral("/label/fill"));
    }

    const QJsonArray foreign = expected.value(QStringLiteral("foreignObjects")).toArray();
    require(scene->textNodes.size() == foreign.size(), id + QStringLiteral("/text node count"));
    for (qsizetype i = 0; i < foreign.size(); ++i) {
      const auto& node = scene->textNodes.at(i);
      const QJsonObject object = foreign.at(i).toObject();
      const QJsonObject attrs = object.value(QStringLiteral("attrs")).toObject();
      const QString path = QStringLiteral("%1/text/%2").arg(id).arg(i);
      require(node.source == object.value(QStringLiteral("text")).toString(),
              path + QStringLiteral("/source actual=") + node.source +
                  QStringLiteral(" expected=") +
                  object.value(QStringLiteral("text")).toString());
      near(node.box.x(), attrs.value(QStringLiteral("x")).toString().toDouble(), path + "/x");
      near(node.box.y(), attrs.value(QStringLiteral("y")).toString().toDouble(), path + "/y");
      near(node.box.width(), attrs.value(QStringLiteral("width")).toString().toDouble(), path + "/w");
      near(node.box.height(), attrs.value(QStringLiteral("height")).toString().toDouble(), path + "/h");
      samePaint(node.color,
                object.value(QStringLiteral("span")).toObject()
                    .value(QStringLiteral("computed")).toObject()
                    .value(QStringLiteral("color")).toString(),
                path + QStringLiteral("/color"));
    }

    const QJsonArray debugCircles = expected.value(QStringLiteral("debugCircles")).toArray();
    require(scene->debugCircles.size() == debugCircles.size(), id + QStringLiteral("/debug circles"));
    for (qsizetype i = 0; i < debugCircles.size(); ++i) {
      const QJsonObject attrs = debugCircles.at(i).toObject().value(QStringLiteral("attrs")).toObject();
      near(scene->debugCircles.at(i).center.x(), attrs.value(QStringLiteral("cx")).toString().toDouble(), id + "/debug/cx");
      near(scene->debugCircles.at(i).center.y(), attrs.value(QStringLiteral("cy")).toString().toDouble(), id + "/debug/cy");
      near(scene->debugCircles.at(i).radius, attrs.value(QStringLiteral("r")).toString().toDouble(), id + "/debug/r");
    }
    const QJsonArray debugCells = expected.value(QStringLiteral("debugCells")).toArray();
    require(scene->debugCells.size() == debugCells.size(), id + QStringLiteral("/debug cells"));
    for (qsizetype i = 0; i < debugCells.size(); ++i) {
      const QJsonObject attrs = debugCells.at(i).toObject().value(QStringLiteral("attrs")).toObject();
      const QRectF rect = scene->debugCells.at(i).rect;
      near(rect.x(), attrs.value(QStringLiteral("x")).toString().toDouble(), id + "/debug/x");
      near(rect.y(), attrs.value(QStringLiteral("y")).toString().toDouble(), id + "/debug/y");
      near(rect.width(), attrs.value(QStringLiteral("width")).toString().toDouble(), id + "/debug/w");
      near(rect.height(), attrs.value(QStringLiteral("height")).toString().toDouble(), id + "/debug/h");
    }
    require(entry.metadata.title.isEmpty() && entry.metadata.accessibleTitle.isEmpty() &&
                entry.metadata.accessibleDescription.isEmpty() &&
                !entry.metadata.svgEmitAccessibleTitle,
            id + QStringLiteral("/metadata"));
  }
  std::puts("MermaidVennGeometryOracleTest: 15/15 passed");
  return 0;
}
