// Journey pixel-parity golden through the production PNG path. The Chrome
// fixture and native renderer use the same bundled Noto families, so the
// canvas is exact; alpha IoU checks layout/shape coverage and foreground RGBA
// additionally catches theme/palette regressions.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QByteArray sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

QImage decodePng(const QString& dataUrl) {
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage image;
  if (comma >= 0)
    image.loadFromData(QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image;
}

qreal alphaIou(const QImage& native, const QImage& reference) {
  const QImage left =
      native.scaled(reference.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  int intersection = 0;
  int united = 0;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = left.pixelColor(x, y).alpha() >= 32;
      const bool b = reference.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  }
  return united ? qreal(intersection) / united : 1.0;
}

qreal foregroundRgbaSimilarity(const QImage& native, const QImage& reference) {
  const QImage left =
      native.scaled(reference.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  qreal difference = 0.0;
  int united = 0;
  const auto premultiplied = [](int channel, int alpha) { return channel * alpha / 255; };
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const QColor a = left.pixelColor(x, y);
      const QColor b = reference.pixelColor(x, y);
      if (a.alpha() < 32 && b.alpha() < 32) continue;
      ++united;
      difference += std::abs(premultiplied(a.red(), a.alpha()) -
                             premultiplied(b.red(), b.alpha()));
      difference += std::abs(premultiplied(a.green(), a.alpha()) -
                             premultiplied(b.green(), b.alpha()));
      difference += std::abs(premultiplied(a.blue(), a.alpha()) -
                             premultiplied(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
    }
  }
  return united ? 1.0 - difference / (united * 4.0 * 255.0) : 1.0;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected Journey pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("bb40febebe8efdc84ed1b06c91237af38412f57c0bf62b223544cc163f7ee1b0"),
          QStringLiteral("Journey pixel fixture changed; audit and update its digest"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 2, QStringLiteral("Journey pixel fixture must contain two themes"));
  const QDir fixtureDir = QFileInfo(manifestPath).dir();
  qreal minimumIou = 1.0;
  qreal minimumRgba = 1.0;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(id == QLatin1String("default") || id == QLatin1String("dark"),
            QStringLiteral("Unexpected Journey pixel case: %1").arg(id));

    const int expectedWidth = fixture.value(QStringLiteral("width")).toInt();
    const int expectedHeight = fixture.value(QStringLiteral("height")).toInt();
    require(expectedWidth == 1300 && expectedHeight == 565,
            id + QStringLiteral(": manifest canvas dimensions drifted"));
    const QString pngPath = fixtureDir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG hash drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull() && browser.width() == expectedWidth &&
                browser.height() == expectedHeight,
            id + QStringLiteral(": browser PNG dimensions drifted"));

    const qreal dpr = fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    const editor::MermaidPngRenderResult result =
        editor::MermaidRenderCache::renderMermaidSourceToPng(
            fixture.value(QStringLiteral("source")).toString(), dpr);
    const QImage native = decodePng(result.dataUrl);
    require(!native.isNull(), id + QStringLiteral(": production PNG must decode"));

    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgbaSimilarity(native, browser);
    std::fprintf(stderr, "%s native=%dx%d browser=%dx%d iou=%.6f rgba=%.6f\n",
                 qPrintable(id), native.width(), native.height(), browser.width(), browser.height(),
                 iou, rgba);
    std::fflush(stderr);

    require(native.width() == expectedWidth && native.height() == expectedHeight,
            QStringLiteral("%1: native canvas %2x%3, expected %4x%5")
                .arg(id)
                .arg(native.width())
                .arg(native.height())
                .arg(expectedWidth)
                .arg(expectedHeight));
    // Measured baselines (Noto fixture, 2026-08-10): default 0.923640 /
    // 0.957252, dark 0.923497 / 0.930508 (IoU / RGBA). These thresholds retain
    // rasterization headroom while rejecting an empty render, a wrong theme,
    // or a material layout/geometry shift.
    require(iou >= 0.90, id + QStringLiteral(": alpha IoU regressed"));
    require(rgba >= 0.90, id + QStringLiteral(": foreground RGBA regressed"));
    minimumIou = std::min(minimumIou, iou);
    minimumRgba = std::min(minimumRgba, rgba);
  }

  qDebug() << "MermaidJourneyPixelTest: passed; minimum alpha IoU" << minimumIou
           << "minimum foreground RGBA" << minimumRgba;
  return 0;
}
