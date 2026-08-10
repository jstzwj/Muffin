#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/eventmodeling/EventModelingScene.h"
#include "mermaid/theme/MermaidColor.h"

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
          qreal tolerance = 0.02) {
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
QColor computedColor(const QString& value) {
  static const QRegularExpression rgb(
      QStringLiteral(R"(^rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?)"));
  const auto match = rgb.match(value);
  if (match.hasMatch()) {
    QColor result(match.captured(1).toInt(), match.captured(2).toInt(),
                  match.captured(3).toInt());
    if (!match.captured(4).isEmpty()) result.setAlphaF(match.captured(4).toDouble());
    return result;
  }
  return color::toQColor(value);
}
void sameColor(const QString& actual, const QString& expected,
               const QString& path) {
  const QColor a = computedColor(actual);
  const QColor b = computedColor(expected);
  require(a.isValid() && b.isValid() && a.rgba() == b.rgba(),
          path + QStringLiteral(": ") + actual + QStringLiteral(" != ") + expected);
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Event Modeling config fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("fea6d4c2d054d46b9c4f421cfe3467ff9d64b5bca592f334180a59066068f42f"),
          QStringLiteral("Event Modeling config fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("c77a63c40041ffd3e234eacd6ab2c5eda4b1060061e153d744f7517b74ed0736"),
          QStringLiteral("Event Modeling config provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 32, QStringLiteral("Expected 32 config cases"));
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
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject()
                                     .value(QStringLiteral("dom")).toObject();
    const QJsonObject rootAttrs = expected.value(QStringLiteral("root")).toObject()
                                      .value(QStringLiteral("attrs")).toObject();
    const QVector<qreal> viewBox = numbers(rootAttrs.value(QStringLiteral("viewBox")).toString());
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
    near(scene->bounds.x(), viewBox[0], id + QStringLiteral("/x"));
    near(scene->bounds.y(), viewBox[1], id + QStringLiteral("/y"));
    near(scene->bounds.width(), viewBox[2], id + QStringLiteral("/width"), 1.01);
    near(scene->bounds.height(), viewBox[3], id + QStringLiteral("/height"));
    require(scene->useMaxWidth ==
                (rootAttrs.value(QStringLiteral("width")).toString() == QLatin1String("100%")),
            id + QStringLiteral("/useMaxWidth"));

    const QJsonArray lanes = expected.value(QStringLiteral("swimlanes")).toArray();
    require(scene->swimlanes.size() == lanes.size(), id + QStringLiteral("/lanes"));
    if (!lanes.isEmpty()) {
      const QJsonObject computed = lanes.at(0).toObject()
                                       .value(QStringLiteral("rect")).toObject()
                                       .value(QStringLiteral("computed")).toObject();
      sameColor(scene->style.swimlaneFill,
                computed.value(QStringLiteral("fill")).toString(), id + "/lane/fill");
      sameColor(scene->style.swimlaneStroke,
                computed.value(QStringLiteral("stroke")).toString(), id + "/lane/stroke");
    }
    const QJsonArray boxes = expected.value(QStringLiteral("boxes")).toArray();
    require(scene->boxes.size() == boxes.size(), id + QStringLiteral("/boxes"));
    for (qsizetype i = 0; i < boxes.size(); ++i) {
      const QJsonObject computed = boxes.at(i).toObject()
                                       .value(QStringLiteral("rect")).toObject()
                                       .value(QStringLiteral("computed")).toObject();
      sameColor(scene->boxes.at(i).fill,
                computed.value(QStringLiteral("fill")).toString(),
                QStringLiteral("%1/box/%2/fill").arg(id).arg(i));
      sameColor(scene->boxes.at(i).stroke,
                computed.value(QStringLiteral("stroke")).toString(),
                QStringLiteral("%1/box/%2/stroke").arg(id).arg(i));
    }
    const QJsonArray relations = expected.value(QStringLiteral("relations")).toArray();
    if (!relations.isEmpty()) {
      const QString expectedStroke = relations.at(0).toObject()
                                         .value(QStringLiteral("computed")).toObject()
                                         .value(QStringLiteral("stroke")).toString();
      sameColor(scene->style.relationStroke, expectedStroke, id + "/relation");
    }
    const QString markerFill = expected.value(QStringLiteral("marker")).toObject()
                                   .value(QStringLiteral("polygon")).toObject()
                                   .value(QStringLiteral("computed")).toObject()
                                   .value(QStringLiteral("fill")).toString();
    sameColor(scene->style.arrowhead, markerFill, id + "/arrowhead");
  }
  std::puts("MermaidEventModelingEdgeParityTest: 32/32 passed");
  return 0;
}
