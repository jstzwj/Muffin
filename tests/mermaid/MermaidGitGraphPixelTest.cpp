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

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool value, const QString& message) {
  if (!value) fail(message);
}
QByteArray sha256(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QCryptographicHash::hash(file.readAll(),
                                        QCryptographicHash::Sha256).toHex()
             : QByteArray();
}
QImage decode(const QString& dataUrl) {
  QImage image;
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  if (comma >= 0)
    image.loadFromData(
        QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}
qreal alphaIou(const QImage& actual, const QImage& expected) {
  qint64 intersection = 0;
  qint64 united = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const bool a = actual.pixelColor(x, y).alpha() >= 32;
      const bool b = expected.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  return united ? qreal(intersection) / qreal(united) : 1.0;
}
qreal foregroundRgba(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  qint64 count = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const QColor a = actual.pixelColor(x, y);
      const QColor b = expected.pixelColor(x, y);
      if (a.alpha() < 16 && b.alpha() < 16) continue;
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
      ++count;
    }
  return count ? 1.0 - difference / (qreal(count) * 1020.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected GitGraph pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile manifest(manifestPath);
  require(manifest.open(QIODevice::ReadOnly), manifest.errorString());
  const QByteArray manifestBytes = manifest.readAll();
  require(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral("081feeda566dc9e621123fb4a5731f5b22b3b12d1b930bd43d149e4cc748f011"),
          QStringLiteral("GitGraph pixel manifest bytes drifted"));
  const QJsonObject root = QJsonDocument::fromJson(manifestBytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("4904052b84dc14de0d6c3d617b5125581df538a866b22a24ed64128beab6d453"),
          QStringLiteral("GitGraph pixel fixture provenance drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 4, QStringLiteral("GitGraph pixel case count"));
  const QDir directory = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString pngPath =
        directory.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull(), id + QStringLiteral(": browser PNG decode"));
    require(browser.size() ==
                QSize(fixture.value(QStringLiteral("width")).toInt(),
                      fixture.value(QStringLiteral("height")).toInt()),
            id + QStringLiteral(": manifest dimensions"));
    const QImage native = decode(
        editor::MermaidRenderCache::renderMermaidSourceToPng(
            fixture.value(QStringLiteral("source")).toString(), 1.0).dataUrl);
    require(!native.isNull(), id + QStringLiteral(": native PNG decode"));
    require(native.size() == browser.size(),
            QStringLiteral("%1: canvas %2x%3 != %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(browser.width()).arg(browser.height()));
    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgba(native, browser);
    std::fprintf(stderr, "%s IoU %.6f RGBA %.6f\n", qPrintable(id), iou,
                 rgba);
    require(iou >= 0.80, id + QStringLiteral(": alpha IoU regressed"));
    require(rgba >= 0.80,
            id + QStringLiteral(": foreground RGBA regressed"));
  }
  std::puts("MermaidGitGraphPixelTest: 4 cases passed");
  return 0;
}
