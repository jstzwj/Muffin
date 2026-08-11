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

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QByteArray fileSha(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
      .toHex();
}

QImage decode(const QString& dataUrl) {
  QImage image;
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  if (comma >= 0)
    image.loadFromData(
        QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image;
}

qreal alphaIou(const QImage& actual, const QImage& expected) {
  int intersection = 0;
  int unionCount = 0;
  for (int y = 0; y < expected.height(); ++y) {
    for (int x = 0; x < expected.width(); ++x) {
      const bool actualInk = actual.pixelColor(x, y).alpha() >= 32;
      const bool expectedInk = expected.pixelColor(x, y).alpha() >= 32;
      intersection += actualInk && expectedInk;
      unionCount += actualInk || expectedInk;
    }
  }
  return unionCount ? qreal(intersection) / unionCount : 1.0;
}

qreal rgbaSimilarity(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  int compared = 0;
  const auto premultiplied = [](int channel, int alpha) {
    return channel * alpha / 255;
  };
  for (int y = 0; y < expected.height(); ++y) {
    for (int x = 0; x < expected.width(); ++x) {
      const QColor actualPixel = actual.pixelColor(x, y);
      const QColor expectedPixel = expected.pixelColor(x, y);
      if (actualPixel.alpha() < 32 && expectedPixel.alpha() < 32) continue;
      ++compared;
      difference += std::abs(premultiplied(actualPixel.red(),
                                            actualPixel.alpha()) -
                             premultiplied(expectedPixel.red(),
                                            expectedPixel.alpha()));
      difference += std::abs(premultiplied(actualPixel.green(),
                                            actualPixel.alpha()) -
                             premultiplied(expectedPixel.green(),
                                            expectedPixel.alpha()));
      difference += std::abs(premultiplied(actualPixel.blue(),
                                            actualPixel.alpha()) -
                             premultiplied(expectedPixel.blue(),
                                            expectedPixel.alpha()));
      difference += std::abs(actualPixel.alpha() - expectedPixel.alpha());
    }
  }
  return compared ? 1.0 - difference / (compared * 4.0 * 255.0) : 1.0;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Ishikawa pixel manifest"));

  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile manifestFile(manifestPath);
  require(manifestFile.open(QIODevice::ReadOnly), manifestFile.errorString());
  const QByteArray bytes = manifestFile.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "95d38e76b9fe8bbe78425f507dcff3ab3f485513a713dee3305e25e70a4fde7f"),
          QStringLiteral("Ishikawa pixel manifest bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "31aa81eed01b0b1d843256a31dac6a0edddc38f550ba89c63092cd5f152ea145"),
          QStringLiteral("Ishikawa pixel provenance changed"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 3, QStringLiteral("Expected three pixel cases"));
  const QDir fixtureDirectory = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString referencePath = fixtureDirectory.filePath(
        fixture.value(QStringLiteral("file")).toString());
    require(fileSha(referencePath) ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral("/browser PNG hash"));

    const QImage reference(referencePath);
    const QImage native = decode(
        editor::MermaidRenderCache::renderMermaidSourceToPng(
            fixture.value(QStringLiteral("source")).toString(), 1.0)
            .dataUrl);
    require(!reference.isNull() && !native.isNull(),
            id + QStringLiteral("/decode"));
    require(native.size() == reference.size(),
            QStringLiteral("%1: native %2x%3 != browser %4x%5")
                .arg(id)
                .arg(native.width())
                .arg(native.height())
                .arg(reference.width())
                .arg(reference.height()));

    const qreal iou = alphaIou(native, reference);
    const qreal rgba = rgbaSimilarity(native, reference);
    std::fprintf(stderr, "%s alphaIoU=%.6f rgba=%.6f\n", qPrintable(id),
                 iou, rgba);
    require(iou >= 0.72, id + QStringLiteral("/alpha IoU"));
    require(rgba >= 0.80, id + QStringLiteral("/RGBA"));
  }
  return 0;
}
