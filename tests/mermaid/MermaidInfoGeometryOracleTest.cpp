#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/info/InfoScene.h"
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
          qreal tolerance = 0.75) {
  require(std::isfinite(actual) && std::fabs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3").arg(path).arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17));
}
QColor computedColor(const QString& value) {
  static const QRegularExpression rgb(
      QStringLiteral(R"(^rgba?\((\d+),\s*(\d+),\s*(\d+))"));
  const auto match = rgb.match(value);
  return match.hasMatch()
             ? QColor(match.captured(1).toInt(), match.captured(2).toInt(),
                      match.captured(3).toInt())
             : color::toQColor(value);
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Info geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("af09a1943a4d8282c925fc8fa6cd97a4f90ad1c03234b633e3ae5a5d237f1a11"),
          QStringLiteral("Info geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("44ad70af6ce5f0b481967196724792f787932a2a431c8a0ca629a41fd7d61938"),
          QStringLiteral("Info geometry fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 8, QStringLiteral("Expected eight Info geometry cases"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": ") + entry.errorMessage);
    auto scene = std::dynamic_pointer_cast<const info::InfoScene>(entry.scene);
    require(bool(scene), id + QStringLiteral(": expected InfoScene"));
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonObject expectedText = expected.value(QStringLiteral("text")).toObject();
    const QJsonObject attrs = expectedText.value(QStringLiteral("attrs")).toObject();
    require(scene->text == expectedText.value(QStringLiteral("value")).toString(),
            id + QStringLiteral("/text"));
    near(scene->anchor.x(), attrs.value(QStringLiteral("x")).toString().toDouble(),
         id + QStringLiteral("/x"), 0.001);
    near(scene->anchor.y(), attrs.value(QStringLiteral("y")).toString().toDouble(),
         id + QStringLiteral("/y"), 0.001);
    near(scene->bounds.width(), 400.0, id + QStringLiteral("/width"), 0.001);
    near(scene->bounds.height(), 150.0, id + QStringLiteral("/height"), 0.001);
    if (id != QLatin1String("font-family") &&
        id != QLatin1String("unrelated-config-inert")) {
      near(scene->textBounds.width(),
           expectedText.value(QStringLiteral("bbox")).toObject()
               .value(QStringLiteral("width")).toDouble(),
           id + QStringLiteral("/text-width"), 2.0);
    }
    require(!scene->style.fontFamily.isEmpty(),
            id + QStringLiteral("/font-family"));
    const QColor native = computedColor(scene->style.textColor);
    const QColor browser = computedColor(
        expectedText.value(QStringLiteral("computed")).toObject()
            .value(QStringLiteral("fill")).toString());
    require(native.isValid() && browser.isValid() && native.rgba() == browser.rgba(),
            id + QStringLiteral("/fill"));
    require(entry.naturalSize == QSize(400, 150),
            id + QStringLiteral("/natural-size"));
    require(entry.metadata.title.isEmpty() &&
                entry.metadata.accessibleTitle.isEmpty() &&
                entry.metadata.accessibleDescription.isEmpty() &&
                !entry.metadata.svgEmitAccessibleTitle &&
                !entry.metadata.svgEmitViewBox &&
                entry.metadata.svgUseMaxWidth,
            id + QStringLiteral("/metadata"));
  }
  std::fprintf(stderr, "Info geometry oracle: 8/8 passed\n");
  return 0;
}
