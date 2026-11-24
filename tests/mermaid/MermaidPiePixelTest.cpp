// Pie pixel-parity golden via the REAL production PNG path
// (MermaidRenderCache::renderMermaidSourceToPng). The native PNG must be the
// Chrome golden's size to the pixel, and is compared to it directly: no
// scaling, which would hide a canvas that is a pixel off and blur the detail
// the comparison is for. Mirrors MermaidRequirementPixelTest.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
constexpr qreal kMinIou = 0.97;
constexpr qreal kMinRgba = 0.96;
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition)
    fail(message);
}
QByteArray sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}
QImage decodePng(const QString& dataUrl) {
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage img;
  img.loadFromData(QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return img;
}
// Both images have the same size.
qreal alphaIou(const QImage& native, const QImage& reference) {
  int intersection = 0, united = 0;
  for (int y = 0; y < reference.height(); ++y)
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = native.pixelColor(x, y).alpha() >= 32;
      const bool b = reference.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  return united ? qreal(intersection) / united : 1.0;
}
qreal foregroundRgbaSimilarity(const QImage& native, const QImage& reference) {
  qreal difference = 0.0;
  int united = 0;
  const auto pm = [](int channel, int alpha) { return channel * alpha / 255; };
  for (int y = 0; y < reference.height(); ++y)
    for (int x = 0; x < reference.width(); ++x) {
      const QColor a = native.pixelColor(x, y);
      const QColor b = reference.pixelColor(x, y);
      if (a.alpha() < 32 && b.alpha() < 32)
        continue;
      ++united;
      difference += std::abs(pm(a.red(), a.alpha()) - pm(b.red(), b.alpha()));
      difference += std::abs(pm(a.green(), a.alpha()) - pm(b.green(), b.alpha()));
      difference += std::abs(pm(a.blue(), a.alpha()) - pm(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
    }
  return united ? 1.0 - difference / (united * 4.0 * 255.0) : 1.0;
}
} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected pie pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  if (!file.open(QIODevice::ReadOnly))
    fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("84b2150cc9fcc9304f61ed08985645922d5acbc8ced2445503308392be603883"),
          QStringLiteral("Pie pixel fixture changed; audit and update its digest"));
  const QDir fixtureDir = QFileInfo(manifestPath).dir();
  qreal minimumIou = 1.0, minimumRgba = 1.0;
  QSet<QString> coveredThemes;
  for (const QJsonValue& cv : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = cv.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    coveredThemes.insert(fixture.value(QStringLiteral("theme")).toString());
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QString pngPath = fixtureDir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG hash drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull() && browser.width() == fixture.value(QStringLiteral("width")).toInt() &&
                browser.height() == fixture.value(QStringLiteral("height")).toInt(),
            id + QStringLiteral(": browser PNG dimensions drifted"));
    const qreal dpr = fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    const editor::MermaidPngRenderResult result = editor::MermaidRenderCache::renderMermaidSourceToPng(source, dpr);
    const QImage native = decodePng(result.dataUrl);
    require(!native.isNull(), id + QStringLiteral(": production PNG must decode"));
    const qreal widthDiff = std::abs(native.width() - browser.width());
    const qreal heightDiff = std::abs(native.height() - browser.height());
    // Canvas: upstream's viewBox is fractional and the browser screenshots it to
    // the nearest pixel; the port rounds its extent the same way, so the image is
    // the golden's size exactly.
    require(widthDiff == 0 && heightDiff == 0,
            QStringLiteral("%1: canvas %2x%3, browser %4x%5")
                .arg(id).arg(native.width()).arg(native.height()).arg(browser.width()).arg(browser.height()));
    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgbaSimilarity(native, browser);
    std::fprintf(stderr, "%s native=%dx%d iou=%.6f rgba=%.6f\n", qPrintable(id), native.width(), native.height(), iou,
                 rgba);
    std::fflush(stderr);
    // Alpha IoU (shape parity) — the slice geometry is exact. Foreground RGBA
    // catches theme/palette regressions IoU misses; both themes render with the
    // exact upstream palette. Floors leave room for the rasterizers to differ:
    // the closest image measures 0.99 IoU and 0.98 RGBA.
    require(iou >= kMinIou, id + QStringLiteral(": alpha IoU %1 below %2").arg(iou, 0, 'f', 4).arg(kMinIou));
    require(rgba >= kMinRgba,
            id + QStringLiteral(": foreground RGBA %1 below %2").arg(rgba, 0, 'f', 4).arg(kMinRgba));
    minimumIou = std::min(minimumIou, iou);
    minimumRgba = std::min(minimumRgba, rgba);
  }
  // Theme axis: all 11 built-in themes are pinned at pixel level (mirrors the
  // flowchart golden-pixel coverage), so a dropped/renamed theme fails loudly.
  require(coveredThemes == QSet<QString>{QStringLiteral("default"), QStringLiteral("base"), QStringLiteral("dark"),
                                         QStringLiteral("forest"), QStringLiteral("neutral"), QStringLiteral("neo"),
                                         QStringLiteral("neo-dark"), QStringLiteral("redux"),
                                         QStringLiteral("redux-dark"), QStringLiteral("redux-color"),
                                         QStringLiteral("redux-dark-color")},
          QStringLiteral("Pie pixel theme coverage regressed"));
  std::printf("MermaidPiePixelTest passed: minimum alpha IoU %.4f, minimum foreground RGBA %.4f\n", minimumIou,
              minimumRgba);
  return 0;
}
