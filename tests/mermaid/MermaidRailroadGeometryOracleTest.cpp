#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/railroad/RailroadScene.h"
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
          qreal tolerance = 0.001) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3")
              .arg(path).arg(actual, 0, 'g', 17).arg(expected, 0, 'g', 17));
}
QRectF viewBox(const QString& text) {
  const QStringList fields = text.split(
      QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  require(fields.size() == 4, QStringLiteral("viewBox fields"));
  return {number(fields[0]), number(fields[1]), number(fields[2]),
          number(fields[3])};
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
  require(a.size() == b.size(), path + QStringLiteral(" token count"));
  for (qsizetype i = 0; i < a.size(); ++i) {
    bool an = false, bn = false;
    const qreal av = a.at(i).toDouble(&an), bv = b.at(i).toDouble(&bn);
    require(an == bn, path + QStringLiteral(" token kind"));
    if (an) near(av, bv, path + QStringLiteral("/%1").arg(i), 0.001);
    else require(a.at(i) == b.at(i), path + QStringLiteral(" command"));
  }
}
QString primitiveTag(railroad::RailroadPrimitiveKind kind) {
  switch (kind) {
    case railroad::RailroadPrimitiveKind::Rect: return QStringLiteral("rect");
    case railroad::RailroadPrimitiveKind::Circle: return QStringLiteral("circle");
    case railroad::RailroadPrimitiveKind::Path: return QStringLiteral("path");
    case railroad::RailroadPrimitiveKind::Text: return QStringLiteral("text");
  }
  return {};
}
void compareMatrix(const railroad::RailroadPrimitive& actual,
                   const QJsonObject& expected, const QString& path) {
  const QJsonArray matrix = expected.value(QStringLiteral("matrix")).toArray();
  require(matrix.size() == 6, path + QStringLiteral(" matrix"));
  near(actual.translation.x(), number(matrix.at(4)), path + QStringLiteral("/tx"));
  near(actual.translation.y(), number(matrix.at(5)), path + QStringLiteral("/ty"));
}
void compareShape(const railroad::RailroadPrimitive& actual,
                  const QJsonObject& expected, const QString& path) {
  const QJsonObject attrs = expected.value(QStringLiteral("attrs")).toObject();
  require(primitiveTag(actual.kind) ==
              expected.value(QStringLiteral("tag")).toString(),
          path + QStringLiteral("/tag"));
  require(actual.cssClass ==
              expected.value(QStringLiteral("className")).toString(),
          path + QStringLiteral("/class"));
  compareMatrix(actual, expected, path);
  if (actual.kind == railroad::RailroadPrimitiveKind::Rect) {
    near(actual.rect.x(), number(attrs.value(QStringLiteral("x"))), path + QStringLiteral("/x"));
    near(actual.rect.y(), number(attrs.value(QStringLiteral("y"))), path + QStringLiteral("/y"));
    near(actual.rect.width(), number(attrs.value(QStringLiteral("width"))), path + QStringLiteral("/width"));
    near(actual.rect.height(), number(attrs.value(QStringLiteral("height"))), path + QStringLiteral("/height"));
    near(actual.rx,
         attrs.contains(QStringLiteral("rx"))
             ? number(attrs.value(QStringLiteral("rx")))
             : 0.0,
         path + QStringLiteral("/rx"));
  } else if (actual.kind == railroad::RailroadPrimitiveKind::Circle) {
    near(actual.rect.center().x(), number(attrs.value(QStringLiteral("cx"))), path + QStringLiteral("/cx"));
    near(actual.rect.center().y(), number(attrs.value(QStringLiteral("cy"))), path + QStringLiteral("/cy"));
    near(actual.rect.width() / 2.0, number(attrs.value(QStringLiteral("r"))), path + QStringLiteral("/r"));
  } else {
    comparePath(actual.pathData, attrs.value(QStringLiteral("d")).toString(),
                path + QStringLiteral("/d"));
  }
  const QJsonObject computed = expected.value(QStringLiteral("computed")).toObject();
  paintEquals(actual.fill, computed.value(QStringLiteral("fill")).toString(),
              path + QStringLiteral("/fill"));
  paintEquals(actual.stroke,
              computed.value(QStringLiteral("stroke")).toString(),
              path + QStringLiteral("/stroke"));
  near(actual.strokeWidth,
       number(computed.value(QStringLiteral("strokeWidth")).toString().chopped(2)),
       path + QStringLiteral("/strokeWidth"));
  const QString dash = computed.value(QStringLiteral("strokeDasharray")).toString();
  require((actual.dash.isEmpty() && dash == QLatin1String("none")) ||
              (!actual.dash.isEmpty() && dash != QLatin1String("none")),
          path + QStringLiteral("/dash"));
}
void compareText(const railroad::RailroadPrimitive& actual,
                 const QJsonObject& expected, const railroad::RailroadScene& scene,
                 const QString& path) {
  require(actual.kind == railroad::RailroadPrimitiveKind::Text,
          path + QStringLiteral("/kind"));
  require(actual.cssClass ==
              expected.value(QStringLiteral("className")).toString(),
          path + QStringLiteral("/class"));
  require(actual.text == expected.value(QStringLiteral("text")).toString(),
          path + QStringLiteral("/text"));
  compareMatrix(actual, expected, path);
  near(actual.position.x(), number(expected.value(QStringLiteral("x"))),
       path + QStringLiteral("/x"));
  near(actual.position.y(), number(expected.value(QStringLiteral("y"))),
       path + QStringLiteral("/y"));
  const QJsonObject computed = expected.value(QStringLiteral("computed")).toObject();
  near(scene.config.fontSize,
       number(computed.value(QStringLiteral("fontSize")).toString().chopped(2)),
       path + QStringLiteral("/fontSize"));
  paintEquals(actual.fill, computed.value(QStringLiteral("fill")).toString(),
              path + QStringLiteral("/fill"));
  require(actual.bold ==
              (computed.value(QStringLiteral("fontWeight")).toString().toInt() >= 600),
          path + QStringLiteral("/fontWeight"));
}

}  // namespace

int main(int argc, char** argv) {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; Linux
  // (Liberation fallbacks) and macOS (SF/Helvetica) resolve different faces
  // with different metrics. Bundled-font goldens are the eventual closure.
  // One gate covers both railroad-geometry.json and railroad-config.json
  // invocations of this source (GeometryOracle + EdgeParity targets).
  qWarning("skipped on Linux/macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected railroad fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  const QByteArray rawSha =
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  const QString fixtureSha = root.value(QStringLiteral("fixtureSha256")).toString();
  int expectedCount = 0;
  if (fixtureSha == QLatin1String("21704d4baa612b445440b0c82f4ad408617df532524de31d03411ddc2b919b8f")) {
    require(rawSha == QByteArrayLiteral("cf168c3e4ae7b00d0335712e74cb60808ee214041424f283110884a05d332e0e"),
            QStringLiteral("Railroad geometry fixture bytes drifted"));
    expectedCount = 15;
  } else if (fixtureSha == QLatin1String("fc149cfbee72bf46f48e6834080b476f168a96a3f7b9eaa96ec81ad2710ffd06")) {
    require(rawSha == QByteArrayLiteral("9f28b46ac555c4f4eb4aa272873c9881e26e1f5f6509c5818dedd229eebc88a5"),
            QStringLiteral("Railroad config fixture bytes drifted"));
    expectedCount = 36;
  } else {
    fail(QStringLiteral("Unknown Railroad fixture provenance: ") + fixtureSha);
  }
  require(root.value(QStringLiteral("provenance")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Railroad Mermaid version drifted"));

  editor::MermaidRenderCache cache;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QLatin1Char(':') + entry.errorMessage);
    const auto scene =
        std::dynamic_pointer_cast<const railroad::RailroadScene>(entry.scene);
    require(bool(scene), id + QStringLiteral(" scene"));

    const QRectF expectedBounds =
        viewBox(fixture.value(QStringLiteral("root")).toObject()
                    .value(QStringLiteral("viewBox")).toString());
    near(scene->bounds.x(), expectedBounds.x(), id + QStringLiteral("/viewBox/x"));
    near(scene->bounds.y(), expectedBounds.y(), id + QStringLiteral("/viewBox/y"));
    near(scene->bounds.width(), expectedBounds.width(), id + QStringLiteral("/viewBox/w"));
    near(scene->bounds.height(), expectedBounds.height(), id + QStringLiteral("/viewBox/h"));

    QVector<const railroad::RailroadPrimitive*> shapes;
    QVector<const railroad::RailroadPrimitive*> texts;
    for (const auto& primitive : scene->primitives) {
      if (primitive.kind == railroad::RailroadPrimitiveKind::Text)
        texts.append(&primitive);
      else
        shapes.append(&primitive);
    }
    const QJsonArray expectedShapes = fixture.value(QStringLiteral("primitives")).toArray();
    const QJsonArray expectedTexts = fixture.value(QStringLiteral("texts")).toArray();
    require(shapes.size() == expectedShapes.size(),
            QStringLiteral("%1 shape count %2 != %3")
                .arg(id).arg(shapes.size()).arg(expectedShapes.size()));
    require(texts.size() == expectedTexts.size(),
            QStringLiteral("%1 text count %2 != %3")
                .arg(id).arg(texts.size()).arg(expectedTexts.size()));
    for (qsizetype i = 0; i < shapes.size(); ++i)
      compareShape(*shapes.at(i), expectedShapes.at(i).toObject(),
                   id + QStringLiteral("/shapes/%1").arg(i));
    for (qsizetype i = 0; i < texts.size(); ++i)
      compareText(*texts.at(i), expectedTexts.at(i).toObject(), *scene,
                  id + QStringLiteral("/texts/%1").arg(i));

    const QJsonArray expectedRules = fixture.value(QStringLiteral("rules")).toArray();
    require(scene->rules.size() == expectedRules.size(),
            id + QStringLiteral("/rule count"));
    for (qsizetype i = 0; i < scene->rules.size(); ++i) {
      const QJsonArray matrix = expectedRules.at(i).toObject()
                                    .value(QStringLiteral("matrix")).toArray();
      near(scene->rules.at(i).y, number(matrix.at(5)),
           id + QStringLiteral("/rules/%1/y").arg(i));
    }
  }
  require(cases.size() == expectedCount, QStringLiteral("Railroad oracle count"));
  std::printf("MermaidRailroadGeometryOracleTest: %d cases passed\n", expectedCount);
  return 0;
}
