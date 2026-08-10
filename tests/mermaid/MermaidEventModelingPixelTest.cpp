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
qreal alphaIou(const QImage& a, const QImage& b) {
  int intersection = 0;
  int united = 0;
  for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width(); ++x) {
      const bool aa = a.pixelColor(x, y).alpha() >= 32;
      const bool bb = b.pixelColor(x, y).alpha() >= 32;
      intersection += aa && bb;
      united += aa || bb;
    }
  return united ? qreal(intersection) / united : 1.0;
}
qreal rgbaSimilarity(const QImage& a, const QImage& b) {
  qreal difference = 0.0;
  int count = 0;
  for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width(); ++x) {
      const QColor actual = a.pixelColor(x, y);
      const QColor expected = b.pixelColor(x, y);
      if (actual.alpha() < 32 && expected.alpha() < 32) continue;
      ++count;
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
    }
  return count ? 1.0 - difference / (count * 4.0 * 255.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Event Modeling pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("f80c57b2cd357943d79530778f2f2c3f4c12c7d80af0875ef7cc5449a3502f46"),
          QStringLiteral("Event Modeling pixel manifest bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("947dcec3ea6be96097de0d28d3c51919097cef5acf73d95b0a7bd916843cc449"),
          QStringLiteral("Event Modeling pixel fixture changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 3, QStringLiteral("Expected three pixel cases"));
  const QDir directory = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString referencePath = directory.filePath(fixture.value(QStringLiteral("file")).toString());
    require(fileSha(referencePath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral("/browser PNG hash"));
    const QImage reference(referencePath);
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
    require(iou >= 0.72, id + QStringLiteral("/alpha IoU"));
    require(rgba >= 0.80, id + QStringLiteral("/foreground RGBA"));
  }
  return 0;
}
