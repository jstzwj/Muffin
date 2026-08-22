#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/eventmodeling/EventModelingScene.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
          qreal tolerance = 0.001) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17));
}
QVector<qreal> numbers(const QString& text) {
  QVector<qreal> result;
  for (const QString& token : text.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    result.append(token.toDouble());
  return result;
}
void compareRect(const QRectF& actual, const QJsonObject& expected,
                 const QString& path, qreal tolerance = 0.001) {
  near(actual.x(), expected.value(QStringLiteral("x")).toDouble(), path + "/x", tolerance);
  near(actual.y(), expected.value(QStringLiteral("y")).toDouble(), path + "/y", tolerance);
  near(actual.width(), expected.value(QStringLiteral("width")).toDouble(), path + "/width", tolerance);
  near(actual.height(), expected.value(QStringLiteral("height")).toDouble(), path + "/height", tolerance);
}
}  // namespace

int main(int argc, char** argv) {
#if defined(Q_OS_LINUX)
  // The fixture goldens embed the Windows golden host's font stack; Linux
  // (Liberation fallbacks) resolves different faces with different metrics.
  // Bundled-font goldens are the eventual closure.
  qWarning("skipped on Linux: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Event Modeling geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("405543c150876f8382030b801b87b870d0a14eea8c94f6249c89fd4d3fd55834"),
          QStringLiteral("Event Modeling geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("9d2fb27a449f4aae08e132f4e5b82659c802e8af8fb0943cf54a6fc020eb5852"),
          QStringLiteral("Event Modeling geometry provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 10, QStringLiteral("Expected ten geometry cases"));
  editor::MermaidRenderCache cache;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": ") + entry.errorMessage);
    const auto scene = std::dynamic_pointer_cast<const eventmodeling::EventModelingScene>(entry.scene);
    require(bool(scene), id + QStringLiteral(": expected EventModelingScene"));
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject rootAttrs = expected.value(QStringLiteral("root")).toObject()
                                      .value(QStringLiteral("attrs")).toObject();
    const QVector<qreal> viewBox = numbers(rootAttrs.value(QStringLiteral("viewBox")).toString());
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
    near(scene->bounds.x(), viewBox[0], id + "/viewBox/x");
    near(scene->bounds.y(), viewBox[1], id + "/viewBox/y");
    near(scene->bounds.width(), viewBox[2], id + "/viewBox/width", 0.02);
    near(scene->bounds.height(), viewBox[3], id + "/viewBox/height", 0.02);
    require(scene->useMaxWidth == (rootAttrs.value(QStringLiteral("width")).toString() == QLatin1String("100%")),
            id + QStringLiteral("/useMaxWidth"));

    const QJsonArray lanes = expected.value(QStringLiteral("swimlanes")).toArray();
    require(scene->swimlanes.size() == lanes.size(), id + QStringLiteral("/lane count"));
    for (qsizetype i = 0; i < lanes.size(); ++i) {
      const auto& actual = scene->swimlanes.at(i);
      const QJsonObject expectedLane = lanes.at(i).toObject();
      compareRect(actual.rect, expectedLane.value(QStringLiteral("rect")).toObject()
                                      .value(QStringLiteral("bbox")).toObject(),
                  QStringLiteral("%1/lane/%2").arg(id).arg(i), 0.02);
      require(actual.label == expectedLane.value(QStringLiteral("label")).toObject()
                                  .value(QStringLiteral("value")).toString(),
              QStringLiteral("%1/lane/%2/label").arg(id).arg(i));
    }
    const QJsonArray boxes = expected.value(QStringLiteral("boxes")).toArray();
    require(scene->boxes.size() == boxes.size(), id + QStringLiteral("/box count"));
    for (qsizetype i = 0; i < boxes.size(); ++i) {
      const auto& actual = scene->boxes.at(i);
      const QJsonObject expectedBox = boxes.at(i).toObject();
      compareRect(actual.rect, expectedBox.value(QStringLiteral("rect")).toObject()
                                      .value(QStringLiteral("bbox")).toObject(),
                  QStringLiteral("%1/box/%2").arg(id).arg(i), 0.02);
      compareRect(actual.foreignObjectRect,
                  expectedBox.value(QStringLiteral("foreignObject")).toObject()
                      .value(QStringLiteral("bbox")).toObject(),
                  QStringLiteral("%1/box/%2/foreignObject").arg(id).arg(i), 0.02);
      require(actual.contentHtml == expectedBox.value(QStringLiteral("span")).toObject()
                                        .value(QStringLiteral("html")).toString(),
              QStringLiteral("%1/box/%2/html [%3] != [%4]")
                  .arg(id).arg(i).arg(actual.contentHtml,
                      expectedBox.value(QStringLiteral("span")).toObject()
                          .value(QStringLiteral("html")).toString()));
    }
    const QJsonArray relations = expected.value(QStringLiteral("relations")).toArray();
    require(scene->relations.size() == relations.size(), id + QStringLiteral("/relation count"));
    for (qsizetype i = 0; i < relations.size(); ++i) {
      require(scene->relations.at(i).pathData ==
                  relations.at(i).toObject().value(QStringLiteral("attrs")).toObject()
                      .value(QStringLiteral("d")).toString(),
              QStringLiteral("%1/relation/%2/path").arg(id).arg(i));
    }
    require(entry.metadata.title.isEmpty() &&
                entry.metadata.accessibleTitle.isEmpty() &&
                entry.metadata.accessibleDescription.isEmpty(),
            id + QStringLiteral("/metadata"));
  }
  std::puts("MermaidEventModelingGeometryOracleTest: 10/10 passed");
  return 0;
}
