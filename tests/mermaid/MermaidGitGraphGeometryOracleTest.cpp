#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/gitgraph/GitGraphScene.h"

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
void require(bool value, const QString& message) {
  if (!value) fail(message);
}
void near(qreal actual, qreal expected, const QString& path,
          qreal tolerance = 0.25) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3 (tol %4)")
              .arg(path).arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17).arg(tolerance));
}
qreal number(const QJsonValue& value) {
  if (value.isDouble()) return value.toDouble();
  bool ok = false;
  const qreal result = value.toString().toDouble(&ok);
  require(ok, QStringLiteral("Invalid oracle number: ") + value.toString());
  return result;
}
QRectF rect(const QJsonObject& object) {
  return QRectF(number(object.value(QStringLiteral("x"))),
                number(object.value(QStringLiteral("y"))),
                number(object.value(QStringLiteral("width"))),
                number(object.value(QStringLiteral("height"))));
}
QRectF viewBox(const QString& value) {
  const QStringList fields = value.split(
      QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  require(fields.size() == 4, QStringLiteral("Invalid viewBox: ") + value);
  return QRectF(number(fields[0]), number(fields[1]), number(fields[2]),
                number(fields[3]));
}

QString oracleRole(const QJsonObject& primitive) {
  const QString tag = primitive.value(QStringLiteral("tag")).toString();
  const QJsonObject attrs = primitive.value(QStringLiteral("attrs")).toObject();
  const QString css = attrs.value(QStringLiteral("class")).toString();
  const QString parent = primitive.value(QStringLiteral("parentClass")).toString();
  if (css.startsWith(QLatin1String("branch "))) return QStringLiteral("branch");
  if (css.startsWith(QLatin1String("branchLabelBkg")))
    return QStringLiteral("branch-label-background");
  if (parent.startsWith(QLatin1String("label branch-label")))
    return QStringLiteral("branch-label");
  if (css.startsWith(QLatin1String("arrow "))) return QStringLiteral("arrow");
  if (css.contains(QLatin1String("commit-highlight-outer")))
    return QStringLiteral("commit-highlight-outer");
  if (css.contains(QLatin1String("commit-highlight-inner")))
    return QStringLiteral("commit-highlight-inner");
  if (css.contains(QLatin1String("commit-merge")))
    return QStringLiteral("commit-merge");
  if (css.contains(QLatin1String("commit-reverse")))
    return QStringLiteral("commit-reverse");
  if (css.contains(QLatin1String("commit-cherry-pick")))
    return tag == QLatin1String("line") ? QStringLiteral("cherry-stem")
                                          : tag == QLatin1String("circle") &&
                                                    attrs.value(QStringLiteral("r")).toString() == QLatin1String("10")
                                                ? QStringLiteral("commit")
                                                : QStringLiteral("cherry-dot");
  if (parent == QLatin1String("commit-bullets")) return QStringLiteral("commit");
  if (css == QLatin1String("commit-label-bkg"))
    return QStringLiteral("commit-label-background");
  if (css == QLatin1String("commit-label")) return QStringLiteral("commit-label");
  if (css == QLatin1String("tag-label-bkg")) return QStringLiteral("tag-background");
  if (css == QLatin1String("tag-hole")) return QStringLiteral("tag-hole");
  if (css == QLatin1String("tag-label")) return QStringLiteral("tag-label");
  if (css == QLatin1String("gitTitleText")) return QStringLiteral("title");
  return QStringLiteral("unknown:") + tag + QLatin1Char(':') + css;
}

QString kindTag(gitgraph::PrimitiveKind kind) {
  switch (kind) {
    case gitgraph::PrimitiveKind::Line: return QStringLiteral("line");
    case gitgraph::PrimitiveKind::Path: return QStringLiteral("path");
    case gitgraph::PrimitiveKind::Circle: return QStringLiteral("circle");
    case gitgraph::PrimitiveKind::Rect: return QStringLiteral("rect");
    case gitgraph::PrimitiveKind::Polygon: return QStringLiteral("polygon");
    case gitgraph::PrimitiveKind::Text: return QStringLiteral("text");
  }
  return {};
}

QVector<QString> pathTokens(const QString& path) {
  static const QRegularExpression token(QStringLiteral(
      R"(([A-Za-z]|[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?))"));
  QVector<QString> result;
  auto matches = token.globalMatch(path);
  while (matches.hasNext()) result.push_back(matches.next().captured());
  return result;
}

void comparePath(const QString& actual, const QString& expected,
                 const QString& path) {
  const QVector<QString> a = pathTokens(actual);
  const QVector<QString> b = pathTokens(expected);
  require(a.size() == b.size(), path + QStringLiteral(" token count"));
  for (qsizetype i = 0; i < a.size(); ++i) {
    bool aNumber = false;
    bool bNumber = false;
    const qreal av = a.at(i).toDouble(&aNumber);
    const qreal bv = b.at(i).toDouble(&bNumber);
    require(aNumber == bNumber, path + QStringLiteral(" token kind"));
    if (aNumber) near(av, bv, path + QStringLiteral("/%1").arg(i), 0.25);
    else require(a.at(i) == b.at(i), path + QStringLiteral(" command"));
  }
}

void comparePrimitive(const gitgraph::GitGraphPrimitive& actual,
                      const QJsonObject& expected, const QString& path) {
  const QJsonObject attrs = expected.value(QStringLiteral("attrs")).toObject();
  require(actual.role == oracleRole(expected),
          path + QStringLiteral(" role %1 != %2")
                     .arg(actual.role, oracleRole(expected)));
  require(kindTag(actual.kind) == expected.value(QStringLiteral("tag")).toString(),
          path + QStringLiteral(" tag"));
  require(actual.text == expected.value(QStringLiteral("text")).toString(),
          path + QStringLiteral(" text"));
  if (actual.kind == gitgraph::PrimitiveKind::Line) {
    near(actual.line.x1(), number(attrs.value(QStringLiteral("x1"))), path + QStringLiteral("/x1"));
    near(actual.line.y1(), number(attrs.value(QStringLiteral("y1"))), path + QStringLiteral("/y1"));
    near(actual.line.x2(), number(attrs.value(QStringLiteral("x2"))), path + QStringLiteral("/x2"));
    near(actual.line.y2(), number(attrs.value(QStringLiteral("y2"))), path + QStringLiteral("/y2"));
  } else if (actual.kind == gitgraph::PrimitiveKind::Rect) {
    near(actual.rect.x(), number(attrs.value(QStringLiteral("x"))), path + QStringLiteral("/x"), 0.75);
    near(actual.rect.y(), number(attrs.value(QStringLiteral("y"))), path + QStringLiteral("/y"), 0.75);
    near(actual.rect.width(), number(attrs.value(QStringLiteral("width"))), path + QStringLiteral("/w"), 0.75);
    near(actual.rect.height(), number(attrs.value(QStringLiteral("height"))), path + QStringLiteral("/h"), 0.75);
  } else if (actual.kind == gitgraph::PrimitiveKind::Circle) {
    near(actual.center.x(), number(attrs.value(QStringLiteral("cx"))), path + QStringLiteral("/cx"));
    near(actual.center.y(), number(attrs.value(QStringLiteral("cy"))), path + QStringLiteral("/cy"));
    near(actual.radius, number(attrs.value(QStringLiteral("r"))), path + QStringLiteral("/r"));
  } else if (actual.kind == gitgraph::PrimitiveKind::Path) {
    comparePath(actual.pathData, attrs.value(QStringLiteral("d")).toString(),
                path + QStringLiteral("/d"));
  } else if (actual.kind == gitgraph::PrimitiveKind::Text) {
    if (!attrs.value(QStringLiteral("x")).isNull())
      near(actual.position.x(), number(attrs.value(QStringLiteral("x"))), path + QStringLiteral("/x"), 0.75);
    if (!attrs.value(QStringLiteral("y")).isNull())
      near(actual.position.y(), number(attrs.value(QStringLiteral("y"))), path + QStringLiteral("/y"), 0.75);
  }
  const QRectF expectedBounds = rect(expected.value(QStringLiteral("globalBBox")).toObject());
  const qreal boundsTolerance =
      actual.kind == gitgraph::PrimitiveKind::Text ? 1.0 : 0.75;
  near(actual.bounds.x(), expectedBounds.x(), path + QStringLiteral("/bbox/x"), boundsTolerance);
  near(actual.bounds.y(), expectedBounds.y(), path + QStringLiteral("/bbox/y"), boundsTolerance);
  near(actual.bounds.width(), expectedBounds.width(), path + QStringLiteral("/bbox/w"), boundsTolerance);
  near(actual.bounds.height(), expectedBounds.height(), path + QStringLiteral("/bbox/h"), boundsTolerance);
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected GitGraph geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("525e9f2ba5107a97ac4c37d1ddde8eb0c619aa489d0c729ed0ac6dcfdaf58e79"),
          QStringLiteral("GitGraph geometry fixture bytes drifted"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("upstream")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("GitGraph geometry version drifted"));

  editor::MermaidRenderCache cache;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": ") + entry.errorMessage);
    const auto scene =
        std::dynamic_pointer_cast<const gitgraph::GitGraphScene>(entry.scene);
    require(bool(scene), id + QStringLiteral(": expected GitGraphScene"));
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QRectF expectedView = viewBox(expected.value(QStringLiteral("root")).toObject()
                                            .value(QStringLiteral("attrs")).toObject()
                                            .value(QStringLiteral("viewBox")).toString());
    if (std::fabs(scene->bounds.width() - expectedView.width()) > 0.5 ||
        std::fabs(scene->bounds.height() - expectedView.height()) > 0.5) {
      std::fprintf(stderr, "%s native viewBox %s\n", qPrintable(id),
                   qPrintable(scene->viewBoxAttribute));
      for (const auto& primitive : scene->primitives) {
        if (primitive.bounds.left() <= scene->contentBounds.left() + 0.01 ||
            primitive.bounds.right() >= scene->contentBounds.right() - 0.01 ||
            primitive.bounds.top() <= scene->contentBounds.top() + 0.01 ||
            primitive.bounds.bottom() >= scene->contentBounds.bottom() - 0.01)
          std::fprintf(stderr, "  native %-26s [%g,%g,%g,%g]\n",
                       qPrintable(primitive.role), primitive.bounds.x(),
                       primitive.bounds.y(), primitive.bounds.width(),
                       primitive.bounds.height());
      }
      const QJsonArray debugPrimitives = expected.value(QStringLiteral("primitives")).toArray();
      QRectF expectedContent;
      bool hasExpectedContent = false;
      for (const QJsonValue& primitiveValue : debugPrimitives) {
        const QJsonObject primitive = primitiveValue.toObject();
        const QRectF bounds = rect(primitive.value(QStringLiteral("globalBBox")).toObject());
        expectedContent = hasExpectedContent ? expectedContent.united(bounds) : bounds;
        hasExpectedContent = true;
      }
      for (const QJsonValue& primitiveValue : debugPrimitives) {
        const QJsonObject primitive = primitiveValue.toObject();
        const QRectF bounds = rect(primitive.value(QStringLiteral("globalBBox")).toObject());
        if (bounds.left() <= expectedContent.left() + 0.01 ||
            bounds.right() >= expectedContent.right() - 0.01 ||
            bounds.top() <= expectedContent.top() + 0.01 ||
            bounds.bottom() >= expectedContent.bottom() - 0.01)
          std::fprintf(stderr, "  oracle %-26s [%g,%g,%g,%g]\n",
                       qPrintable(oracleRole(primitive)), bounds.x(), bounds.y(),
                       bounds.width(), bounds.height());
      }
    }
    near(scene->bounds.x(), expectedView.x(), id + QStringLiteral("/viewBox/x"), 0.75);
    near(scene->bounds.y(), expectedView.y(), id + QStringLiteral("/viewBox/y"), 0.75);
    near(scene->bounds.width(), expectedView.width(), id + QStringLiteral("/viewBox/w"), 0.75);
    near(scene->bounds.height(), expectedView.height(), id + QStringLiteral("/viewBox/h"), 0.75);
    const QJsonArray primitives = expected.value(QStringLiteral("primitives")).toArray();
    require(scene->primitives.size() == primitives.size(),
            id + QStringLiteral(" primitive count %1 != %2")
                     .arg(scene->primitives.size()).arg(primitives.size()));
    for (qsizetype i = 0; i < scene->primitives.size(); ++i)
      comparePrimitive(scene->primitives.at(i), primitives.at(i).toObject(),
                       id + QStringLiteral("/%1").arg(i));
  }
  require(cases.size() == 16, QStringLiteral("GitGraph geometry count"));
  std::puts("MermaidGitGraphGeometryOracleTest: 16 cases passed");
  return 0;
}
