#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }
QByteArray sha256(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()
             : QByteArray();
}
QImage renderNative(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1.0).dataUrl;
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage image;
  if (comma >= 0)
    image.loadFromData(QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}
qreal alphaIou(const QImage& native, const QImage& reference) {
  qint64 intersection = 0, united = 0;
  for (int y = 0; y < reference.height(); ++y)
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = native.pixelColor(x, y).alpha() >= 32;
      const bool b = reference.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  return united ? qreal(intersection) / qreal(united) : 1.0;
}
qreal foregroundRgba(const QImage& native, const QImage& reference) {
  qreal difference = 0.0;
  qint64 samples = 0;
  for (int y = 0; y < reference.height(); ++y)
    for (int x = 0; x < reference.width(); ++x) {
      const QColor a = native.pixelColor(x, y);
      const QColor b = reference.pixelColor(x, y);
      if (a.alpha() < 16 && b.alpha() < 16) continue;
      const auto premul = [](int c, int alpha) { return c * alpha / 255; };
      difference += std::abs(premul(a.red(), a.alpha()) - premul(b.red(), b.alpha()));
      difference += std::abs(premul(a.green(), a.alpha()) - premul(b.green(), b.alpha()));
      difference += std::abs(premul(a.blue(), a.alpha()) - premul(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
      ++samples;
    }
  return samples ? 1.0 - difference / (qreal(samples) * 4.0 * 255.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected railroad pixel manifest"));
  const QFileInfo manifestInfo(QString::fromLocal8Bit(argv[1]));
  QFile file(manifestInfo.absoluteFilePath());
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("3661b639fc61cee644b19c3415d8b53b5166ee41141cde7674f0ac4e7eee0c85"),
          QStringLiteral("Railroad pixel manifest bytes drifted"));
  const QJsonObject manifest = QJsonDocument::fromJson(bytes).object();
  require(manifest.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("4f3e334582b5779b3d42200840a7338f98ade441c7bf02f4833899135ea884ad"),
          QStringLiteral("Railroad pixel provenance drifted"));
  const QJsonArray cases = manifest.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 3, QStringLiteral("Railroad pixel case count"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString pngPath = manifestInfo.absolutePath() + QLatin1Char('/') +
                            fixture.value(QStringLiteral("file")).toString();
    require(sha256(pngPath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull(), id + QStringLiteral(": browser PNG decode"));
    const QImage native = renderNative(fixture.value(QStringLiteral("source")).toString());
    require(!native.isNull(), id + QStringLiteral(": native PNG decode"));
    require(native.size() == browser.size(),
            QStringLiteral("%1: %2x%3 != %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(browser.width()).arg(browser.height()));
    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgba(native, browser);
    std::fprintf(stderr, "%s IoU %.6f RGBA %.6f\n", qPrintable(id), iou, rgba);
    require(iou >= 0.88, id + QStringLiteral(": alpha IoU regressed"));
    require(rgba >= 0.88, id + QStringLiteral(": foreground RGBA regressed"));
  }
  std::puts("MermaidRailroadPixelTest: 3 cases passed");
  return 0;
}
