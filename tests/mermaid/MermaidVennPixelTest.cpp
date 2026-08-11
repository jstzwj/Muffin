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
  return image.convertToFormat(QImage::Format_RGBA8888);
}
qreal alphaIou(const QImage& actual, const QImage& expected) {
  qint64 intersection = 0;
  qint64 unionCount = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const bool a = actual.pixelColor(x, y).alpha() >= 32;
      const bool b = expected.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      unionCount += a || b;
    }
  return unionCount ? qreal(intersection) / unionCount : 1.0;
}
qreal rgbaSimilarity(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  qint64 count = 0;
  const auto premultiplied = [](int channel, int alpha) {
    return channel * alpha / 255;
  };
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const QColor a = actual.pixelColor(x, y);
      const QColor b = expected.pixelColor(x, y);
      if (a.alpha() < 16 && b.alpha() < 16) continue;
      difference += std::abs(premultiplied(a.red(), a.alpha()) -
                             premultiplied(b.red(), b.alpha()));
      difference += std::abs(premultiplied(a.green(), a.alpha()) -
                             premultiplied(b.green(), b.alpha()));
      difference += std::abs(premultiplied(a.blue(), a.alpha()) -
                             premultiplied(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
      ++count;
    }
  return count ? 1.0 - difference / (count * 4.0 * 255.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Venn pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "dd4d933aad5e0148e1fd84b8747a4f6940fc5223a01b70efef2790dc1f83f418"),
          QStringLiteral("Venn pixel manifest bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "8b4f5d9b2857009ccddced737c12b8e8b41824be5015eb68ad5a5bea630e447a"),
          QStringLiteral("Venn pixel provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 4, QStringLiteral("Expected four Venn pixel cases"));
  const QDir directory = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString path = directory.filePath(fixture.value(QStringLiteral("file")).toString());
    require(fileSha(path) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral("/browser PNG hash"));
    const QImage reference(path);
    const QImage native = decode(editor::MermaidRenderCache::renderMermaidSourceToPng(
                                     fixture.value(QStringLiteral("source")).toString(), 1.0)
                                     .dataUrl);
    require(!reference.isNull() && !native.isNull(), id + QStringLiteral("/decode"));
    require(native.size() == reference.size(),
            QStringLiteral("%1: native %2x%3 != browser %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(reference.width()).arg(reference.height()));
    const qreal iou = alphaIou(native, reference);
    const qreal rgba = rgbaSimilarity(native, reference);
    std::fprintf(stderr, "%s alphaIoU=%.6f rgba=%.6f\n", qPrintable(id), iou, rgba);
    const bool handDrawn = id == QLatin1String("hand-drawn");
    require(iou >= (handDrawn ? 0.92 : 0.95),
            id + QStringLiteral("/alpha IoU"));
    require(rgba >= (handDrawn ? 0.96 : 0.98),
            id + QStringLiteral("/RGBA"));
  }
  return 0;
}
