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

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QByteArray sha256(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QCryptographicHash::hash(file.readAll(),
                                        QCryptographicHash::Sha256)
                   .toHex()
             : QByteArray();
}

QImage renderNative(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1.0)
          .dataUrl;
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage image;
  if (comma >= 0)
    image.loadFromData(
        QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}

qreal alphaIou(const QImage& native, const QImage& reference) {
  qint64 intersection = 0;
  qint64 united = 0;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const bool actual = native.pixelColor(x, y).alpha() >= 32;
      const bool expected = reference.pixelColor(x, y).alpha() >= 32;
      intersection += actual && expected;
      united += actual || expected;
    }
  }
  return united ? qreal(intersection) / qreal(united) : 1.0;
}

qreal foregroundRgba(const QImage& native, const QImage& reference) {
  qreal difference = 0.0;
  qint64 samples = 0;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const QColor actual = native.pixelColor(x, y);
      const QColor expected = reference.pixelColor(x, y);
      if (actual.alpha() < 16 && expected.alpha() < 16) continue;
      const auto premultiplied = [](int channel, int alpha) {
        return channel * alpha / 255;
      };
      difference += std::abs(premultiplied(actual.red(), actual.alpha()) -
                             premultiplied(expected.red(), expected.alpha()));
      difference += std::abs(premultiplied(actual.green(), actual.alpha()) -
                             premultiplied(expected.green(), expected.alpha()));
      difference += std::abs(premultiplied(actual.blue(), actual.alpha()) -
                             premultiplied(expected.blue(), expected.alpha()));
      difference += std::abs(actual.alpha() - expected.alpha());
      ++samples;
    }
  }
  return samples
             ? 1.0 - difference / (qreal(samples) * 4.0 * 255.0)
             : 1.0;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected C4 pixel manifest"));

  const QFileInfo manifestInfo(QString::fromLocal8Bit(argv[1]));
  QFile manifestFile(manifestInfo.absoluteFilePath());
  require(manifestFile.open(QIODevice::ReadOnly), manifestFile.errorString());
  const QByteArray manifestBytes = manifestFile.readAll();
  require(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral(
                  "02ecbcd66252c1f7240b29c8ed147257d675b9c4fea01da27c4a15305cec2fbc"),
          QStringLiteral("C4 pixel manifest bytes drifted"));
  const QJsonObject manifest = QJsonDocument::fromJson(manifestBytes).object();
  require(manifest.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "c9f489d3eb29503844373276cfdfb584abb91d11c8b24a871043f65fd4b7b358"),
          QStringLiteral("C4 pixel fixture provenance drifted"));
  require(manifest.value(QStringLiteral("provenance")).toObject()
                  .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("C4 pixel Mermaid version drifted"));

  const QJsonArray cases = manifest.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 3, QStringLiteral("C4 pixel case count"));
  const QString directory = manifestInfo.absolutePath();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString pngPath =
        directory + QLatin1Char('/') +
        fixture.value(QStringLiteral("file")).toString();
    require(sha256(pngPath) ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull(), id + QStringLiteral(": browser PNG decode"));
    require(browser.size() ==
                QSize(fixture.value(QStringLiteral("width")).toInt(),
                      fixture.value(QStringLiteral("height")).toInt()),
            id + QStringLiteral(": manifest dimensions"));

    const QImage native =
        renderNative(fixture.value(QStringLiteral("source")).toString());
    require(!native.isNull(), id + QStringLiteral(": production PNG decode"));
    require(native.size() == browser.size(),
            QStringLiteral("%1: canvas %2x%3 != %4x%5")
                .arg(id)
                .arg(native.width())
                .arg(native.height())
                .arg(browser.width())
                .arg(browser.height()));
    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgba(native, browser);
    std::fprintf(stderr, "%s IoU %.6f RGBA %.6f\n", qPrintable(id), iou,
                 rgba);
    require(iou >= 0.80, id + QStringLiteral(": alpha IoU regressed"));
    require(rgba >= 0.80,
            id + QStringLiteral(": foreground RGBA regressed"));
  }

  std::puts("MermaidC4PixelTest: 3 cases passed");
  return 0;
}
