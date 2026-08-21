#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/state/StateScenePainter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
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
qreal alphaIou(const QImage& first, const QImage& second) {
  constexpr int side = 400;
  const QImage left = first.scaled(side, side, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
  const QImage right = second.scaled(side, side, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
  int intersection = 0, united = 0;
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      const bool a = left.pixelColor(x, y).alpha() >= 32;
      const bool b = right.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  }
  return united ? qreal(intersection) / united : 1.0;
}
qreal foregroundRgbaSimilarity(const QImage& first, const QImage& second) {
  constexpr int side = 400;
  const QImage left = first.scaled(side, side, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
  const QImage right = second.scaled(side, side, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
  qreal difference = 0.0;
  int united = 0;
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      const QColor a = left.pixelColor(x, y);
      const QColor b = right.pixelColor(x, y);
      if (a.alpha() < 32 && b.alpha() < 32) continue;
      ++united;
      const auto premultiplied = [](int channel, int alpha) {
        return channel * alpha / 255;
      };
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
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  if (argc != 2) fail(QStringLiteral("Expected state pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("f2d9bb765bf7a8b485b4609d4b35ec80f654c2de11de74dd4129007d1856bde6"),
          QStringLiteral("State pixel fixture changed; audit and update its digest"));
  const QDir fixtureDir = QFileInfo(manifestPath).dir();
  editor::MermaidRenderCache cache;
  qreal minimumIou = 1.0;
  int dprVariants = 0, darkCases = 0, clusterCases = 0;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QString pngPath = fixtureDir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG hash drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull() && browser.width() == fixture.value(QStringLiteral("width")).toInt() &&
                browser.height() == fixture.value(QStringLiteral("height")).toInt(),
            id + QStringLiteral(": browser PNG dimensions drifted"));
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* stateScene = dynamic_cast<const state::StateScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && stateScene != nullptr,
            id + QStringLiteral(": native render failed: ") + entry.errorMessage);
    const qreal dpr = fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    // Titled cases exercise the PRODUCTION export path: the title strip is
    // composited above the content exactly as the editor/print paths do —
    // the scene-only raster would silently skip the band.
    QImage native;
    if (fixture.value(QStringLiteral("title")).toBool()) {
      const QString dataUrl =
          editor::MermaidRenderCache::renderMermaidSourceToPngDataUrl(source, dpr);
      const int comma = dataUrl.indexOf(QLatin1Char(','));
      require(comma > 0, id + QStringLiteral(": production PNG export failed"));
      native = QImage::fromData(QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
      require(!native.isNull(), id + QStringLiteral(": production PNG undecodable"));
      // The production raster is DPR-aware already.
      native = native.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } else {
      native = state::renderStateSceneToImage(*stateScene, dpr, 0.0);
    }
    // Pixel triage hook (mirrors the requirement suite): dump the native
    // raster next to the browser PNG when MUFFIN_SAVE_NATIVE points at a
    // directory.
    if (const QString saveDir = QString::fromLocal8Bit(qgetenv("MUFFIN_SAVE_NATIVE"));
        !saveDir.isEmpty()) {
      QDir().mkpath(saveDir);
      native.save(QDir(saveDir).filePath(id + QStringLiteral("-native.png")));
    }
    const qreal iou = alphaIou(native, browser);
    const qreal rgbaSimilarity = foregroundRgbaSimilarity(native, browser);
    std::fprintf(stderr, "%s native=%dx%d browser=%dx%d iou=%.3f rgba=%.3f\n",
                 qPrintable(id), native.width(), native.height(), browser.width(),
                 browser.height(), iou, rgbaSimilarity);
    // Chromium element screenshots snap the fractional client box to the
    // nearest device pixel and the native raster now follows the same rule —
    // the canvases must agree exactly (the old 1% tolerance hid the
    // 132-vs-131 / 1105-vs-1104 / 289-vs-288 off-by-one rows).
    require(native.width() == browser.width() &&
                native.height() == browser.height(),
            QStringLiteral("%1: native/browser canvas differs: %2x%3 vs %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(browser.width()).arg(browser.height()));
    require(iou >= 0.90, id + QStringLiteral(": native/browser alpha IoU regressed"));
    require(rgbaSimilarity >= 0.93,
            id + QStringLiteral(": native/browser foreground RGBA regressed"));
    minimumIou = std::min(minimumIou, iou);
    dprVariants += dpr != 1.0;
    darkCases += fixture.value(QStringLiteral("theme")).toString() == QLatin1String("dark");
    clusterCases += !stateScene->clusters.isEmpty();
    QImage repeated;
    if (fixture.value(QStringLiteral("title")).toBool()) {
      const QString dataUrl =
          editor::MermaidRenderCache::renderMermaidSourceToPngDataUrl(source, dpr);
      const int comma = dataUrl.indexOf(QLatin1Char(','));
      repeated = QImage::fromData(QByteArray::fromBase64(
          dataUrl.mid(comma + 1).toLatin1()), "PNG");
      repeated = repeated.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } else {
      repeated = state::renderStateSceneToImage(*stateScene, dpr, 0.0);
    }
    require(native == repeated, id + QStringLiteral(": native raster is non-deterministic"));
  }
  require(dprVariants >= 3 && darkCases >= 1 && clusterCases >= 2,
          QStringLiteral("State pixel matrix coverage regressed"));
  std::fprintf(stderr, "MermaidStatePixelTest: %d cases passed; minimum alpha IoU %.3f\n",
               static_cast<int>(root.value(QStringLiteral("cases")).toArray().size()),
               minimumIou);
  return 0;
}
