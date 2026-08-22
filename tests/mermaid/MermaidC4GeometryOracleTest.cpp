#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/c4/C4Scene.h"
#include "mermaid/editor/MermaidRenderCache.h"
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
void require(bool value, const QString& message) { if (!value) fail(message); }
qreal number(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  bool ok = false;
  const qreal result = value.toString().toDouble(&ok);
  require(ok, QStringLiteral("invalid number ") + value.toString());
  return result;
}
void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 0.01) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual,0,'g',17).arg(expected,0,'g',17));
}
QRectF viewBox(const QString& text) {
  const QStringList fields = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  require(fields.size() == 4, QStringLiteral("viewBox fields"));
  return {number(fields[0]), number(fields[1]), number(fields[2]), number(fields[3])};
}
QString tag(c4::C4PrimitiveKind kind) {
  switch (kind) {
    case c4::C4PrimitiveKind::Rect: return QStringLiteral("rect");
    case c4::C4PrimitiveKind::Path: return QStringLiteral("path");
    case c4::C4PrimitiveKind::Line: return QStringLiteral("line");
    case c4::C4PrimitiveKind::Text: return QStringLiteral("text");
    case c4::C4PrimitiveKind::Image: return QStringLiteral("image");
  }
  return {};
}
void paintEquals(const QString& actual, const QString& expected,
                 const QString& path) {
  if (actual.compare(expected, Qt::CaseInsensitive) == 0) return;
  const QColor a = color::toQColor(actual);
  const QColor b = color::toQColor(expected);
  require(a.isValid() && b.isValid() && a.rgba() == b.rgba(),
          QStringLiteral("%1: %2 != %3").arg(path, actual, expected));
}
QVector<QString> pathTokens(const QString& path) {
  static const QRegularExpression token(QStringLiteral(
      R"(([A-Za-z]|[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?))"));
  QVector<QString> result;
  auto matches = token.globalMatch(path);
  while (matches.hasNext()) result.append(matches.next().captured());
  return result;
}
void comparePath(const QString& actual, const QString& expected,
                 const QString& path) {
  const auto a = pathTokens(actual), b = pathTokens(expected);
  require(a.size() == b.size(), path + QStringLiteral(" tokens"));
  for (qsizetype i = 0; i < a.size(); ++i) {
    bool an = false, bn = false;
    const qreal av = a.at(i).toDouble(&an), bv = b.at(i).toDouble(&bn);
    require(an == bn, path + QStringLiteral(" kind"));
    if (an) near(av, bv, path + QStringLiteral("/%1").arg(i), 0.000001);
    else require(a.at(i) == b.at(i), path + QStringLiteral(" command"));
  }
}
void comparePrimitive(const c4::C4Primitive& actual, const QJsonObject& expected,
                      const QString& path) {
  const QJsonObject attrs = expected.value(QStringLiteral("attrs")).toObject();
  require(tag(actual.kind) == expected.value(QStringLiteral("tag")).toString(), path + QStringLiteral(" tag"));
  const QString expectedText = expected.value(QStringLiteral("text")).toString();
  require(actual.text == expectedText,
          QStringLiteral("%1 text: %2 != %3")
              .arg(path, actual.text, expectedText));
  if (actual.kind == c4::C4PrimitiveKind::Rect || actual.kind == c4::C4PrimitiveKind::Image) {
    near(actual.rect.x(), number(attrs.value(QStringLiteral("x"))), path + QStringLiteral("/x"));
    near(actual.rect.y(), number(attrs.value(QStringLiteral("y"))), path + QStringLiteral("/y"));
    near(actual.rect.width(), number(attrs.value(QStringLiteral("width"))), path + QStringLiteral("/w"));
    near(actual.rect.height(), number(attrs.value(QStringLiteral("height"))), path + QStringLiteral("/h"));
  } else if (actual.kind == c4::C4PrimitiveKind::Line) {
    near(actual.line.x1(), number(attrs.value(QStringLiteral("x1"))), path + QStringLiteral("/x1"), 1e-8);
    near(actual.line.y1(), number(attrs.value(QStringLiteral("y1"))), path + QStringLiteral("/y1"), 1e-8);
    near(actual.line.x2(), number(attrs.value(QStringLiteral("x2"))), path + QStringLiteral("/x2"), 1e-8);
    near(actual.line.y2(), number(attrs.value(QStringLiteral("y2"))), path + QStringLiteral("/y2"), 1e-8);
  } else if (actual.kind == c4::C4PrimitiveKind::Path) {
    comparePath(actual.pathData, attrs.value(QStringLiteral("d")).toString(), path + QStringLiteral("/d"));
  } else if (actual.kind == c4::C4PrimitiveKind::Text) {
    near(actual.position.x(), number(attrs.value(QStringLiteral("x"))), path + QStringLiteral("/x"), 0.01);
    near(actual.position.y(), number(attrs.value(QStringLiteral("y"))), path + QStringLiteral("/y"), 0.01);
    const QJsonObject computed = expected.value(QStringLiteral("computed")).toObject();
    near(actual.fontSize, number(computed.value(QStringLiteral("fontSize")).toString().chopped(2)), path + QStringLiteral("/fontSize"));
    QString actualFamily = actual.fontFamily;
    QString expectedFamily =
        computed.value(QStringLiteral("fontFamily")).toString();
    actualFamily.remove(QLatin1Char('"'));
    expectedFamily.remove(QLatin1Char('"'));
    require(actualFamily.trimmed() == expectedFamily.trimmed(),
            path + QStringLiteral("/fontFamily"));
    paintEquals(actual.fill, computed.value(QStringLiteral("fill")).toString(), path + QStringLiteral("/fill"));
  }
  if (actual.kind != c4::C4PrimitiveKind::Text && actual.kind != c4::C4PrimitiveKind::Image) {
    const QJsonObject computed = expected.value(QStringLiteral("computed")).toObject();
    paintEquals(actual.fill, computed.value(QStringLiteral("fill")).toString(), path + QStringLiteral("/fill"));
    paintEquals(actual.stroke, computed.value(QStringLiteral("stroke")).toString(), path + QStringLiteral("/stroke"));
    near(actual.strokeWidth, number(computed.value(QStringLiteral("strokeWidth")).toString().chopped(2)), path + QStringLiteral("/strokeWidth"));
  }
}
}

int main(int argc, char** argv) {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; Linux
  // (Liberation fallbacks) and macOS (SF/Helvetica) resolve different faces
  // with different metrics. Bundled-font goldens are the eventual closure.
  // One gate covers both c4-geometry.json and c4-config.json invocations of
  // this source (GeometryOracle + EdgeParity targets).
  qWarning("skipped on Linux/macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected C4 geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  const QByteArray rawSha =
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("provenance")).toObject().value(QStringLiteral("version")).toString() == QLatin1String("11.16.0"), QStringLiteral("C4 version"));
  const QString fixtureSha =
      root.value(QStringLiteral("fixtureSha256")).toString();
  int expectedCount = 0;
  if (fixtureSha == QLatin1String(
                        "bcbcc7874e5e97d20b567cf2522e76c28158a64f0256e44d3e96f438e4597f81")) {
    require(rawSha == QByteArrayLiteral(
                          "3f2c8f43fc85212d56b2d3dc9bdc5c83ab2a1f298283f7a2c6ad3fe539899bfe"),
            QStringLiteral("C4 geometry fixture bytes drifted"));
    expectedCount = 13;
  } else if (fixtureSha == QLatin1String(
                               "f2d31edace4f93886ed29e52ec3ba1ffe48122e6a5b805862c9a509032945128")) {
    require(rawSha == QByteArrayLiteral(
                          "f0c641dd82c98533a1cf720ca2bb18b3800894609b6d2ba0fadf98fbc5d3b44e"),
            QStringLiteral("C4 config fixture bytes drifted"));
    expectedCount = 31;
  } else {
    fail(QStringLiteral("Unknown C4 fixture provenance: ") + fixtureSha);
  }
  editor::MermaidRenderCache cache;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    if (fixture.contains(QStringLiteral("error"))) {
      require(entry.status == editor::MermaidRenderStatus::Error,
              id + QStringLiteral(" should fail like Mermaid"));
      continue;
    }
    require(entry.status == editor::MermaidRenderStatus::Ready, id + QLatin1Char(':') + entry.errorMessage);
    const auto scene = std::dynamic_pointer_cast<const c4::C4Scene>(entry.scene);
    require(bool(scene), id + QStringLiteral(" scene"));
    const QRectF expectedView = viewBox(fixture.value(QStringLiteral("root")).toObject().value(QStringLiteral("attrs")).toObject().value(QStringLiteral("viewBox")).toString());
    const QJsonArray primitives = fixture.value(QStringLiteral("primitives")).toArray();
    require(scene->primitives.size() == primitives.size(), QStringLiteral("%1 primitive count %2 != %3").arg(id).arg(scene->primitives.size()).arg(primitives.size()));
    for (qsizetype i = 0; i < scene->primitives.size(); ++i)
      comparePrimitive(scene->primitives.at(i), primitives.at(i).toObject(), id + QStringLiteral("/%1").arg(i));
    near(scene->bounds.x(), expectedView.x(), id + QStringLiteral("/viewBox/x"));
    near(scene->bounds.y(), expectedView.y(), id + QStringLiteral("/viewBox/y"));
    near(scene->bounds.width(), expectedView.width(), id + QStringLiteral("/viewBox/w"));
    near(scene->bounds.height(), expectedView.height(), id + QStringLiteral("/viewBox/h"));
  }
  require(cases.size() == expectedCount, QStringLiteral("C4 oracle count"));
  std::printf("MermaidC4GeometryOracleTest: %d cases passed\n", expectedCount);
}
