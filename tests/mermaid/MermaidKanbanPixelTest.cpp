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

QImage renderNative(const QString& source) {
  const QString url = editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1.0).dataUrl;
  const qsizetype comma = url.indexOf(QLatin1Char(','));
  QImage image;
  if (comma >= 0)
    image.loadFromData(QByteArray::fromBase64(url.mid(comma + 1).toLatin1()), "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}

qreal alphaIou(const QImage& a, const QImage& b) {
  qint64 intersection = 0, united = 0;
  for (int y = 0; y < b.height(); ++y) for (int x = 0; x < b.width(); ++x) {
    const bool aa = a.pixelColor(x,y).alpha() >= 32;
    const bool bb = b.pixelColor(x,y).alpha() >= 32;
    intersection += aa && bb; united += aa || bb;
  }
  return united ? qreal(intersection) / united : 1.0;
}
qreal rgbaSimilarity(const QImage& a, const QImage& b) {
  qreal diff = 0; qint64 count = 0;
  for (int y = 0; y < b.height(); ++y) for (int x = 0; x < b.width(); ++x) {
    const QColor ca = a.pixelColor(x,y), cb = b.pixelColor(x,y);
    if (ca.alpha() < 16 && cb.alpha() < 16) continue;
    diff += std::abs(ca.red()-cb.red()) + std::abs(ca.green()-cb.green()) +
            std::abs(ca.blue()-cb.blue()) + std::abs(ca.alpha()-cb.alpha());
    ++count;
  }
  return count ? 1.0 - diff / qreal(count * 1020) : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Kanban pixel manifest"));
  const QFileInfo info(QString::fromLocal8Bit(argv[1]));
  QFile manifestFile(info.absoluteFilePath());
  require(manifestFile.open(QIODevice::ReadOnly), manifestFile.errorString());
  const QJsonObject root = QJsonDocument::fromJson(manifestFile.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("e5921cb9490024fd48402720cee1a89f29c7c9d6d2e6fc02752fa69f25f29c87"),
          QStringLiteral("Kanban pixel fixture provenance drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 5, QStringLiteral("Kanban pixel case count"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString path = info.absolutePath() + QLatin1Char('/') +
                         fixture.value(QStringLiteral("file")).toString();
    QFile pngFile(path);
    require(pngFile.open(QIODevice::ReadOnly), id + QStringLiteral(": fixture missing"));
    const QByteArray bytes = pngFile.readAll();
    require(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) ==
                fixture.value(QStringLiteral("sha256")).toString(),
            id + QStringLiteral(": PNG sha drifted"));
    QImage reference; reference.loadFromData(bytes, "PNG");
    const QImage native = renderNative(fixture.value(QStringLiteral("source")).toString());
    require(!native.isNull(), id + QStringLiteral(": native render failed"));
    require(native.size() == reference.size(),
            QStringLiteral("%1: native %2x%3 != Chrome %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(reference.width()).arg(reference.height()));
    const qreal iou = alphaIou(native, reference);
    const qreal rgba = rgbaSimilarity(native, reference);
    std::fprintf(stderr, "%s: IoU %.4f RGBA %.4f\n", qPrintable(id), iou, rgba);
    const qreal iouFloor = id == QLatin1String("hand-drawn") ? 0.75 : 0.90;
    const qreal rgbaFloor = id == QLatin1String("hand-drawn") ? 0.75 : 0.88;
    require(iou >= iouFloor && rgba >= rgbaFloor,
            id + QStringLiteral(": Kanban pixel parity below threshold"));
  }
  return 0;
}
