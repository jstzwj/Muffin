#include "WardleyTestSupport.h"

#include "mermaid/MermaidFontRegistry.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

using namespace wardley_test;

namespace {
QImage nativePng(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1).dataUrl;
  QImage image;
  image.loadFromData(
      QByteArray::fromBase64(dataUrl.mid(dataUrl.indexOf(',') + 1).toLatin1()),
      "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}

double alphaIou(const QImage& actual, const QImage& expected) {
  qint64 intersection = 0;
  qint64 unionPixels = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const bool lhs = actual.pixelColor(x, y).alpha() >= 32;
      const bool rhs = expected.pixelColor(x, y).alpha() >= 32;
      intersection += lhs && rhs;
      unionPixels += lhs || rhs;
    }
  return unionPixels ? double(intersection) / unionPixels : 1.0;
}

double rgbaScore(const QImage& actual, const QImage& expected) {
  double difference = 0.0;
  qint64 samples = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const QColor lhs = actual.pixelColor(x, y);
      const QColor rhs = expected.pixelColor(x, y);
      if (lhs.alpha() < 16 && rhs.alpha() < 16) continue;
      difference += std::abs(lhs.red() - rhs.red()) +
                    std::abs(lhs.green() - rhs.green()) +
                    std::abs(lhs.blue() - rhs.blue()) +
                    std::abs(lhs.alpha() - rhs.alpha());
      ++samples;
    }
  return samples ? 1.0 - difference / (samples * 1020.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  ensureWardleyFonts();
  require(argc == 2, QStringLiteral("Expected Wardley pixel manifest"));
  const QFileInfo manifestInfo(QString::fromLocal8Bit(argv[1]));
  QFile manifest(manifestInfo.absoluteFilePath());
  require(manifest.open(QIODevice::ReadOnly), manifest.errorString());
  const QByteArray manifestBytes = manifest.readAll();
  require(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral(
                  "c3fe5f5a83917903bcef99fa7cca286fc10661a14d3a552ca2be795373d28be8"),
          QStringLiteral("Wardley pixel manifest bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(manifestBytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String(
                      "d0663619648a95475b7a19ed9998c42bbef01860babc564c96ca25f6a9451b41") &&
              root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0"),
          QStringLiteral("Wardley pixel provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 4, QStringLiteral("Wardley pixel case count"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    QFile referenceFile(manifestInfo.absolutePath() + QLatin1Char('/') +
                        fixture.value(QStringLiteral("file")).toString());
    require(referenceFile.open(QIODevice::ReadOnly), id + QStringLiteral("/png"));
    const QByteArray pngBytes = referenceFile.readAll();
    require(QCryptographicHash::hash(pngBytes, QCryptographicHash::Sha256)
                    .toHex() ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral("/sha256"));
    QImage reference;
    reference.loadFromData(pngBytes, "PNG");
    reference = reference.convertToFormat(QImage::Format_RGBA8888);
    const QImage actual =
        nativePng(fixture.value(QStringLiteral("source")).toString());
    require(actual.size() == reference.size(),
            QStringLiteral("%1 size %2x%3 != %4x%5")
                .arg(id)
                .arg(actual.width())
                .arg(actual.height())
                .arg(reference.width())
                .arg(reference.height()));
    const double iou = alphaIou(actual, reference);
    const double rgba = rgbaScore(actual, reference);
    std::fprintf(stderr, "%s IoU %.5f RGBA %.5f\n", qPrintable(id), iou,
                 rgba);
    require(iou >= .88, id + QStringLiteral("/alpha IoU"));
    require(rgba >= .92, id + QStringLiteral("/RGBA"));
  }
  return 0;
}
