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
  require(argc == 2, QStringLiteral("Expected Info geometry fixture"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("9db0db3939cc6623d205c6c089b4d10041edf11a9fca4595559ece0c186db9bc"),
          QStringLiteral("Info geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("439e82ef230023d6a6dd774cb1e2c71df93506370b72344c9c00ca776d6c6334"),
          QStringLiteral("Info geometry fixture provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 9, QStringLiteral("Expected nine Info geometry cases"));

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
    const QJsonArray rootChildren = expected.value(QStringLiteral("childTags")).toArray();
    require(rootChildren.size() == 3 &&
                rootChildren.at(0).toString() == QLatin1String("style") &&
                rootChildren.at(1).toString() == QLatin1String("g") &&
                rootChildren.at(2).toString() == QLatin1String("g"),
            id + QStringLiteral("/root-child-order"));
    const QJsonObject parent = expectedText.value(QStringLiteral("parent")).toObject();
    const QJsonArray parentChildren = parent.value(QStringLiteral("childTags")).toArray();
    require(parent.value(QStringLiteral("tag")).toString() == QLatin1String("g") &&
                parent.value(QStringLiteral("typeIndex")).toInt() == 2 &&
                parentChildren.size() == 1 &&
                parentChildren.at(0).toString() == QLatin1String("text"),
            id + QStringLiteral("/text-parent"));
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
      const QJsonObject browserBounds =
          expectedText.value(QStringLiteral("bbox")).toObject();
      near(scene->textBounds.x(), browserBounds.value(QStringLiteral("x")).toDouble(),
           id + QStringLiteral("/text-x"), 0.02);
      near(scene->textBounds.y(), browserBounds.value(QStringLiteral("y")).toDouble(),
           id + QStringLiteral("/text-y"), 0.02);
      near(scene->textBounds.width(),
           browserBounds.value(QStringLiteral("width")).toDouble(),
           id + QStringLiteral("/text-width"), 0.02);
      near(scene->textBounds.height(),
           browserBounds.value(QStringLiteral("height")).toDouble(),
           id + QStringLiteral("/text-height"), 0.02);
      near(scene->textAdvance,
           expectedText.value(QStringLiteral("computedTextLength")).toDouble(),
           id + QStringLiteral("/text-advance"), 0.02);
    }
    require(!scene->style.fontFamily.isEmpty(),
            id + QStringLiteral("/font-family"));
    const QColor native = computedColor(scene->style.textColor);
    const QColor browser = computedColor(
        expectedText.value(QStringLiteral("computed")).toObject()
            .value(QStringLiteral("fill")).toString());
    require(native.isValid() && browser.isValid() && native.rgba() == browser.rgba(),
            id + QStringLiteral("/fill"));
    if (id == QLatin1String("theme-css-structure")) {
      near(scene->style.fontSize, 24.0, id + QStringLiteral("/font-size"), 0.001);
      near(scene->style.opacity, 0.5, id + QStringLiteral("/opacity"), 0.001);
      require(scene->style.fontWeight == QFont::Bold,
              id + QStringLiteral("/font-weight"));
      require(native == QColor(0, 255, 0), id + QStringLiteral("/structural-fill"));
    }
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
  std::fprintf(stderr, "Info geometry oracle: 9/9 passed\n");
  return 0;
}
