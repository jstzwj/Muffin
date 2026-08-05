// quadrantChart pixel-parity golden via the production PNG path. Mirrors
// MermaidPiePixelTest. Canvas is the fixed 500x500 viewBox; alpha IoU absorbs
// the Noto-Sans-vs-trebuchet font difference. The point fill is the
// upstream-invalid hsl(NaN), which both sides render as black.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& m) { std::fprintf(stderr, "FAIL: %s\n", qPrintable(m)); std::fflush(stderr); std::exit(1); }
void require(bool c, const QString& m) { if (!c) fail(m); }
QByteArray sha256(const QString& p) { QFile f(p); if (!f.open(QIODevice::ReadOnly)) return {}; return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex(); }
QImage decodePng(const QString& url) { QImage img; img.loadFromData(QByteArray::fromBase64(url.mid(url.indexOf(QLatin1Char(',')) + 1).toLatin1()), "PNG"); return img; }
qreal alphaIou(const QImage& n, const QImage& ref) {
  const QImage l = n.scaled(ref.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  int inter = 0, uni = 0;
  for (int y = 0; y < ref.height(); ++y) for (int x = 0; x < ref.width(); ++x) {
    const bool a = l.pixelColor(x, y).alpha() >= 32, b = ref.pixelColor(x, y).alpha() >= 32;
    inter += a && b; uni += a || b;
  }
  return uni ? qreal(inter) / uni : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected quadrant pixel manifest"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("03d35f14c59c747f5c13a15af0ebeea04f4af6e6513f75900854d32d7fb37e83"),
          QStringLiteral("Quadrant pixel fixture changed; audit and update its digest"));
  const QDir dir = QFileInfo(QString::fromLocal8Bit(argv[1])).dir();
  qreal minIou = 1.0;
  for (const QJsonValue& cv : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject f = cv.toObject();
    const QString id = f.value(QStringLiteral("id")).toString();
    const QString pngPath = dir.filePath(f.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) == f.value(QStringLiteral("sha256")).toString().toLatin1(), id + ": browser PNG hash drifted");
    const QImage browser(pngPath);
    const qreal dpr = f.value(QStringLiteral("dpr")).toDouble(1.0);
    const QImage native = decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(f.value(QStringLiteral("source")).toString(), dpr).dataUrl);
    require(!native.isNull(), id + ": production PNG must decode");
    const qreal iou = alphaIou(native, browser);
    std::fprintf(stderr, "%s native=%dx%d browser=%dx%d iou=%.3f\n", qPrintable(id),
                 native.width(), native.height(), browser.width(), browser.height(), iou);
    std::fflush(stderr);
    require(std::abs(native.width() - browser.width()) <= 0.03 * browser.width() &&
                std::abs(native.height() - browser.height()) <= 0.03 * browser.height(),
            id + QStringLiteral(": canvas differs"));
    require(iou >= 0.90, id + QStringLiteral(": alpha IoU regressed"));
    minIou = std::min(minIou, iou);
  }
  qDebug() << "MermaidQuadrantPixelTest: passed; minimum alpha IoU" << minIou;
  return 0;
}
