#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/ishikawa/IshikawaScene.h"
#include "mermaid/theme/MermaidColor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
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
          qreal tolerance = 0.04) {
  require(std::isfinite(actual) &&
              std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3")
              .arg(path)
              .arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17));
}

QVector<qreal> numbers(const QString& value) {
  QVector<qreal> result;
  for (const QString& token :
       value.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    result.append(token.toDouble());
  return result;
}

QColor computedColor(const QString& value) {
  static const QRegularExpression rgb(QStringLiteral(
      R"(^rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?\))"));
  const QRegularExpressionMatch match = rgb.match(value);
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
  const QColor actualColor = computedColor(actual);
  const QColor expectedColor = computedColor(expected);
  require(actualColor.isValid() && expectedColor.isValid() &&
              actualColor.rgba() == expectedColor.rgba(),
          path + QStringLiteral(": ") + actual + QStringLiteral(" != ") +
              expected);
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Ishikawa config fixture"));

  QFile fixtureFile(QString::fromLocal8Bit(argv[1]));
  require(fixtureFile.open(QIODevice::ReadOnly), fixtureFile.errorString());
  const QByteArray bytes = fixtureFile.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "1a9c2094b00b1ef935885cc9407504b4d7f9045caf479ab94ba0542004d8e51a"),
          QStringLiteral("Ishikawa config bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "d6f7b0807f4ae377f42a13765d998df2a5d670de894e92a6e81bdf4a1299e9c6"),
          QStringLiteral("Ishikawa config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 29, QStringLiteral("Expected 29 config cases"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(
        editor::MermaidRenderCache::makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": ") + entry.errorMessage);
    const auto scene =
        std::dynamic_pointer_cast<const ishikawa::IshikawaScene>(entry.scene);
    require(bool(scene), id + QStringLiteral("/scene"));

    const QJsonObject dom = fixture.value(QStringLiteral("expected"))
                                .toObject()
                                .value(QStringLiteral("dom"))
                                .toObject();
    const QJsonObject attrs = dom.value(QStringLiteral("root"))
                                  .toObject()
                                  .value(QStringLiteral("attrs"))
                                  .toObject();
    const QVector<qreal> viewBox =
        numbers(attrs.value(QStringLiteral("viewBox")).toString());
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
    near(scene->bounds.x(), viewBox[0], id + QStringLiteral("/x"));
    near(scene->bounds.y(), viewBox[1], id + QStringLiteral("/y"));
    near(scene->bounds.width(), viewBox[2], id + QStringLiteral("/width"));
    near(scene->bounds.height(), viewBox[3], id + QStringLiteral("/height"));
    require(scene->useMaxWidth ==
                (attrs.value(QStringLiteral("width")).toString() ==
                 QLatin1String("100%")),
            id + QStringLiteral("/useMaxWidth"));

    const QJsonArray lines = dom.value(QStringLiteral("lines")).toArray();
    const QJsonArray paths = dom.value(QStringLiteral("paths")).toArray();
    const QJsonArray texts = dom.value(QStringLiteral("texts")).toArray();
    if (id == QLatin1String("look-hand-drawn")) {
      const bool allRough = std::all_of(
          scene->lines.cbegin(), scene->lines.cend(),
          [](const auto& line) { return line.rough; });
      require(scene->style.look == QLatin1String("handDrawn") && allRough &&
                  lines.isEmpty() && !paths.isEmpty(),
              id + QStringLiteral("/look"));
    } else {
      require(scene->style.look != QLatin1String("handDrawn"),
              id + QStringLiteral("/classic"));
      if (!lines.isEmpty())
        samePaint(scene->style.lineColor,
                  lines.at(0)
                      .toObject()
                      .value(QStringLiteral("computed"))
                      .toObject()
                      .value(QStringLiteral("stroke"))
                      .toString(),
                  id + QStringLiteral("/line"));
      const auto head = std::find_if(
          paths.begin(), paths.end(), [](const QJsonValue& path) {
            return path.toObject()
                       .value(QStringLiteral("attrs"))
                       .toObject()
                       .value(QStringLiteral("class"))
                       .toString() == QLatin1String("ishikawa-head");
          });
      if (head != paths.end())
        samePaint(scene->style.mainBkg,
                  head->toObject()
                      .value(QStringLiteral("computed"))
                      .toObject()
                      .value(QStringLiteral("fill"))
                      .toString(),
                  id + QStringLiteral("/fill"));
    }

    if (!texts.isEmpty())
      samePaint(scene->style.textColor,
                texts.at(0)
                    .toObject()
                    .value(QStringLiteral("computed"))
                    .toObject()
                    .value(QStringLiteral("fill"))
                    .toString(),
                id + QStringLiteral("/text"));
    if (texts.size() > 1) {
      QString fontSize = texts.at(1)
                             .toObject()
                             .value(QStringLiteral("computed"))
                             .toObject()
                             .value(QStringLiteral("fontSize"))
                             .toString();
      near(scene->style.fontSize,
           fontSize.remove(QStringLiteral("px")).toDouble(),
           id + QStringLiteral("/fontSize"), 0.001);
    }
    require(entry.metadata.title.isEmpty() &&
                entry.metadata.accessibleTitle.isEmpty(),
            id + QStringLiteral("/metadata"));
  }

  std::puts("MermaidIshikawaEdgeParityTest: 29/29 passed");
  return 0;
}
