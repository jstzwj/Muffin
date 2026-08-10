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
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
QByteArray sha256(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()
             : QByteArray();
}
QImage decode(const QString& url) {
  QImage image;
  const qsizetype comma = url.indexOf(QLatin1Char(','));
  if (comma >= 0)
    image.loadFromData(QByteArray::fromBase64(url.mid(comma + 1).toLatin1()), "PNG");
  return image;
}
qreal alphaIou(const QImage& native, const QImage& reference) {
  const QImage scaled = native.scaled(reference.size(), Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
  int intersection = 0;
  int united = 0;
  for (int y = 0; y < reference.height(); ++y)
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = scaled.pixelColor(x, y).alpha() >= 32;
      const bool b = reference.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  return united ? qreal(intersection) / united : 1.0;
}
qreal rgbaSimilarity(const QImage& native, const QImage& reference) {
  const QImage scaled = native.scaled(reference.size(), Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation);
  qreal difference = 0.0;
  int count = 0;
  for (int y = 0; y < reference.height(); ++y)
    for (int x = 0; x < reference.width(); ++x) {
      const QColor a = scaled.pixelColor(x, y);
      const QColor b = reference.pixelColor(x, y);
      if (a.alpha() < 32 && b.alpha() < 32) continue;
      ++count;
      auto premul = [](int channel, int alpha) { return channel * alpha / 255; };
      difference += std::abs(premul(a.red(), a.alpha()) - premul(b.red(), b.alpha()));
      difference += std::abs(premul(a.green(), a.alpha()) - premul(b.green(), b.alpha()));
      difference += std::abs(premul(a.blue(), a.alpha()) - premul(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
    }
  return count ? 1.0 - difference / (count * 4.0 * 255.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected timeline pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("7fd102390c7521ec7044888ef6cb8399d877a8178496c6ea90dcf6ff7b373dde"),
          QStringLiteral("Timeline pixel fixture changed; audit its digest"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 3, QStringLiteral("Timeline pixel fixture must have 3 cases"));
  const QDir dir = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString pngPath = dir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull(), id + QStringLiteral(": browser PNG failed to decode"));
    const QImage native = decode(editor::MermaidRenderCache::renderMermaidSourceToPng(
                                     fixture.value(QStringLiteral("source")).toString(), 1.0)
                                     .dataUrl);
    require(!native.isNull(), id + QStringLiteral(": native PNG failed to decode"));
    require(std::abs(native.width() - browser.width()) <= 1 &&
                std::abs(native.height() - browser.height()) <= 1,
            QStringLiteral("%1: canvas %2x%3 != %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(browser.width()).arg(browser.height()));
    const qreal iou = alphaIou(native, browser);
    const qreal rgba = rgbaSimilarity(native, browser);
    std::fprintf(stderr, "%s native=%dx%d browser=%dx%d iou=%.6f rgba=%.6f\n",
                 qPrintable(id), native.width(), native.height(), browser.width(),
                 browser.height(), iou, rgba);
    require(iou >= 0.86, id + QStringLiteral(": alpha IoU regressed"));
    require(rgba >= 0.82, id + QStringLiteral(": foreground RGBA regressed"));
  }
  return 0;
}
