#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/ishikawa/IshikawaScene.h"
#include "mermaid/rough/RoughOps.h"

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
          qreal tolerance = 0.025) {
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
                 const QString& path, bool position = true,
                 qreal tolerance = 0.025) {
  if (position) {
    near(actual.x(), expected.value(QStringLiteral("x")).toDouble(),
         path + "/x", tolerance);
    near(actual.y(), expected.value(QStringLiteral("y")).toDouble(),
         path + "/y", tolerance);
  }
  near(actual.width(), expected.value(QStringLiteral("width")).toDouble(),
       path + "/width", tolerance);
  near(actual.height(), expected.value(QStringLiteral("height")).toDouble(),
       path + "/height", tolerance);
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Ishikawa geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("31a2bb064c882b34630a8ffdd36f09affe7ba71b2eeae3d4d0234edc4caded06"),
          QStringLiteral("Ishikawa geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("2951868b2a27d2b8c04407b4aa7d9a05bcb4cb78701f3b12b359993114eeff1f"),
          QStringLiteral("Ishikawa geometry provenance changed"));
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
    const auto scene = std::dynamic_pointer_cast<const ishikawa::IshikawaScene>(entry.scene);
    require(bool(scene), id + QStringLiteral(": expected IshikawaScene"));
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject rootAttrs = expected.value(QStringLiteral("root")).toObject()
                                      .value(QStringLiteral("attrs")).toObject();
    const QVector<qreal> viewBox = numbers(rootAttrs.value(QStringLiteral("viewBox")).toString());
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
    near(scene->bounds.x(), viewBox[0], id + "/viewBox/x");
    near(scene->bounds.y(), viewBox[1], id + "/viewBox/y");
    near(scene->bounds.width(), viewBox[2], id + "/viewBox/width");
    near(scene->bounds.height(), viewBox[3], id + "/viewBox/height");
    compareRect(scene->contentBounds,
                expected.value(QStringLiteral("graph")).toObject()
                    .value(QStringLiteral("bbox")).toObject(),
                id + QStringLiteral("/content"));
    require(scene->useMaxWidth ==
                (rootAttrs.value(QStringLiteral("width")).toString() == QLatin1String("100%")),
            id + QStringLiteral("/useMaxWidth"));

    const QJsonArray lines = expected.value(QStringLiteral("lines")).toArray();
    const QJsonArray rects = expected.value(QStringLiteral("rects")).toArray();
    const bool handDrawn = !scene->lines.isEmpty() && scene->lines.first().rough;
    if (!handDrawn) {
      require(scene->lines.size() == lines.size(), id + QStringLiteral("/line count"));
      for (qsizetype i = 0; i < lines.size(); ++i) {
        const auto& actual = scene->lines.at(i);
        const QJsonObject attrs = lines.at(i).toObject().value(QStringLiteral("attrs")).toObject();
        require(actual.className == attrs.value(QStringLiteral("class")).toString(),
                QStringLiteral("%1/line/%2/class").arg(id).arg(i));
        near(actual.line.x1(), attrs.value(QStringLiteral("x1")).toString().toDouble(),
             QStringLiteral("%1/line/%2/x1").arg(id).arg(i),
             actual.className == QLatin1String("ishikawa-spine") ? 0.02 : 0.001);
        near(actual.line.y1(), attrs.value(QStringLiteral("y1")).toString().toDouble(),
             QStringLiteral("%1/line/%2/y1").arg(id).arg(i), 0.001);
        near(actual.line.x2(), attrs.value(QStringLiteral("x2")).toString().toDouble(),
             QStringLiteral("%1/line/%2/x2").arg(id).arg(i), 0.001);
        near(actual.line.y2(), attrs.value(QStringLiteral("y2")).toString().toDouble(),
             QStringLiteral("%1/line/%2/y2").arg(id).arg(i), 0.001);
      }
      require(scene->rects.size() == rects.size(), id + QStringLiteral("/rect count"));
      for (qsizetype i = 0; i < rects.size(); ++i)
        compareRect(scene->rects.at(i).rect,
                    rects.at(i).toObject().value(QStringLiteral("bbox")).toObject(),
                    QStringLiteral("%1/rect/%2").arg(id).arg(i), true, 0.04);
    } else {
      require(lines.isEmpty() && rects.isEmpty(), id + QStringLiteral("/rough DOM types"));
      QVector<QRectF> roughPaths;
      qreal spineY = 0.0;
      for (const auto& line : scene->lines)
        if (line.className == QLatin1String("ishikawa-spine"))
          spineY = line.line.y1();
      for (const auto& paintEntry : scene->paintOrder) {
        const rough::Drawable* drawable = nullptr;
        bool headLocalCoordinates = false;
        if (paintEntry.kind == ishikawa::IshikawaPrimitiveKind::Line)
          drawable = &scene->lines.at(paintEntry.index).roughDrawable;
        else if (paintEntry.kind == ishikawa::IshikawaPrimitiveKind::Path) {
          const auto& path = scene->paths.at(paintEntry.index);
          drawable = &path.roughDrawable;
          headLocalCoordinates = path.className == QLatin1String("ishikawa-head");
        } else if (paintEntry.kind == ishikawa::IshikawaPrimitiveKind::Rect)
          drawable = &scene->rects.at(paintEntry.index).roughDrawable;
        if (!drawable) continue;
        for (const auto& set : drawable->sets) {
          QRectF pathBounds = rough::tightBounds(set);
          if (headLocalCoordinates) pathBounds.translate(0.0, -spineY);
          roughPaths.append(pathBounds);
        }
      }
      const QJsonArray paths = expected.value(QStringLiteral("paths")).toArray();
      require(roughPaths.size() == paths.size(), id + QStringLiteral("/path count"));
      for (qsizetype i = 0; i < paths.size(); ++i)
        compareRect(roughPaths.at(i),
                    paths.at(i).toObject().value(QStringLiteral("bbox")).toObject(),
                    QStringLiteral("%1/path/%2").arg(id).arg(i), true, 0.03);
    }
    const QJsonArray texts = expected.value(QStringLiteral("texts")).toArray();
    require(scene->texts.size() == texts.size(), id + QStringLiteral("/text count"));
    for (qsizetype i = 0; i < texts.size(); ++i) {
      const QJsonObject text = texts.at(i).toObject();
      require(scene->texts.at(i).source == text.value(QStringLiteral("value")).toString(),
              QStringLiteral("%1/text/%2/value").arg(id).arg(i));
      compareRect(scene->texts.at(i).bounds,
                  text.value(QStringLiteral("bbox")).toObject(),
                  QStringLiteral("%1/text/%2").arg(id).arg(i), i != 0,
                  0.04);
    }
    require(entry.metadata.title.isEmpty() && entry.metadata.accessibleTitle.isEmpty() &&
                entry.metadata.accessibleDescription.isEmpty(),
            id + QStringLiteral("/metadata"));
  }
  std::puts("MermaidIshikawaGeometryOracleTest: 10/10 passed");
  return 0;
}
