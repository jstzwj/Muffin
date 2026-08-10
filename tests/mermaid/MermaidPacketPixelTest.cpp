#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <QCryptographicHash>

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

QImage renderNative(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1.0).dataUrl;
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage image;
  if (comma >= 0)
    image.loadFromData(
        QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image;
}

qreal alphaIou(const QImage& native, const QImage& reference) {
  qint64 intersection = 0;
  qint64 united = 0;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = native.pixelColor(x, y).alpha() >= 32;
      const bool b = reference.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  }
  return united ? qreal(intersection) / qreal(united) : 1.0;
}

qreal foregroundRgba(const QImage& native, const QImage& reference) {
  qreal difference = 0.0;
  qint64 samples = 0;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const QColor a = native.pixelColor(x, y);
      const QColor b = reference.pixelColor(x, y);
      if (a.alpha() < 16 && b.alpha() < 16) continue;
      difference += std::abs(a.red() - b.red()) +
                    std::abs(a.green() - b.green()) +
                    std::abs(a.blue() - b.blue()) +
                    std::abs(a.alpha() - b.alpha());
      ++samples;
    }
  }
  return samples ? 1.0 - difference / qreal(samples * 4 * 255) : 1.0;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected packet pixel manifest"));
  const QFileInfo manifestInfo(QString::fromLocal8Bit(argv[1]));
  const QString dir = manifestInfo.absolutePath();
  QFile file(manifestInfo.absoluteFilePath());
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
  require(manifest.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("1bd1e813cd59767e8a5b78703ef8a430292be841dd6ad5f84fe32895ebf99462") &&
              manifest.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Packet pixel fixture provenance drifted"));
  const QJsonArray cases = manifest.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 3, QStringLiteral("Packet pixel case count"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    QImage reference(dir + QLatin1Char('/') +
                     fixture.value(QStringLiteral("file")).toString());
    require(!reference.isNull(), id + QStringLiteral(": reference PNG missing"));
    QFile pngFile(dir + QLatin1Char('/') +
                  fixture.value(QStringLiteral("file")).toString());
    require(pngFile.open(QIODevice::ReadOnly),
            id + QStringLiteral(": reference PNG unreadable"));
    require(QString::fromLatin1(
                QCryptographicHash::hash(pngFile.readAll(),
                                         QCryptographicHash::Sha256)
                    .toHex()) == fixture.value(QStringLiteral("sha256")).toString(),
            id + QStringLiteral(": reference PNG sha256 drifted"));
    const QImage native =
        renderNative(fixture.value(QStringLiteral("source")).toString());
    require(!native.isNull(), id + QStringLiteral(": production PNG failed"));
    require(native.size() == reference.size(),
            QStringLiteral("%1: native %2x%3 != browser %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(reference.width()).arg(reference.height()));
    const qreal iou = alphaIou(native, reference);
    const qreal rgba = foregroundRgba(native, reference);
    std::fprintf(stderr, "%s: IoU %.4f RGBA %.4f\n", qPrintable(id), iou, rgba);
    require(iou >= 0.95 && rgba >= 0.95,
            id + QStringLiteral(": Packet pixel parity below threshold"));
  }
  return 0;
}
