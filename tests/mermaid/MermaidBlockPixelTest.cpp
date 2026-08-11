#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;
namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool value, const QString& message) { if (!value) fail(message); }
QImage nativePng(const QString& source) {
  const QString url =
      editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1).dataUrl;
  QImage image;
  image.loadFromData(QByteArray::fromBase64(
      url.mid(url.indexOf(QLatin1Char(',')) + 1).toLatin1()), "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}
qreal alphaIou(const QImage& actual, const QImage& expected) {
  qint64 intersection = 0, unionArea = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const bool a = actual.pixelColor(x, y).alpha() >= 32;
      const bool b = expected.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      unionArea += a || b;
    }
  return unionArea ? qreal(intersection) / unionArea : 1.0;
}
qreal rgbaScore(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  qint64 count = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const QColor a = actual.pixelColor(x, y);
      const QColor b = expected.pixelColor(x, y);
      if (a.alpha() < 16 && b.alpha() < 16) continue;
      difference += std::abs(a.red() - b.red()) +
                    std::abs(a.green() - b.green()) +
                    std::abs(a.blue() - b.blue()) +
                    std::abs(a.alpha() - b.alpha());
      ++count;
    }
  return count ? 1.0 - difference / (count * 1020.0) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Block pixel manifest"));
  const QFileInfo manifestInfo(QString::fromLocal8Bit(argv[1]));
  QFile manifest(manifestInfo.absoluteFilePath());
  require(manifest.open(QIODevice::ReadOnly), manifest.errorString());
  const QByteArray manifestBytes = manifest.readAll();
  require(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral("7c65f1d9e4cbdcea0a507574a4321ef2fabf78f0c4703cbc3aed4539e4c2590b"),
          QStringLiteral("Block pixel manifest bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(manifestBytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("55c1b32ef0c7a016db889f92afc3421c70f32bd33593d4cf0a142c380e578179"),
          QStringLiteral("Block pixel provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 4, QStringLiteral("Block pixel case count"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    QFile reference(manifestInfo.absolutePath() + QLatin1Char('/') +
                    fixture.value(QStringLiteral("file")).toString());
    require(reference.open(QIODevice::ReadOnly), id + QStringLiteral("/png"));
    const QByteArray bytes = reference.readAll();
    require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral("/png-sha"));
    QImage expected;
    expected.loadFromData(bytes, "PNG");
    const QImage actual =
        nativePng(fixture.value(QStringLiteral("source")).toString());
    require(!actual.isNull(), id + QStringLiteral("/native-png"));
    require(actual.size() == expected.size(),
            QStringLiteral("%1/size %2x%3 != %4x%5")
                .arg(id).arg(actual.width()).arg(actual.height())
                .arg(expected.width()).arg(expected.height()));
    const qreal iou = alphaIou(actual, expected);
    const qreal rgba = rgbaScore(actual, expected);
    std::fprintf(stderr, "%s IoU %.5f RGBA %.5f\n",
                 qPrintable(id), iou, rgba);
    require(iou >= .88, id + QStringLiteral("/alpha-IoU"));
    require(rgba >= .90, id + QStringLiteral("/RGBA"));
  }
  std::puts("MermaidBlockPixelTest: 4/4 passed");
  return 0;
}
