// quadrantChart pixel-parity golden via the production PNG path. Mirrors
// MermaidPiePixelTest: the native image must be the golden's size to the
// pixel and is compared to it directly, never scaled to fit. The point fill is the
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
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& m) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(m));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool c, const QString& m) {
  if (!c)
    fail(m);
}
QByteArray sha256(const QString& p) {
  QFile f(p);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex();
}
QImage decodePng(const QString& url) {
  QImage img;
  img.loadFromData(QByteArray::fromBase64(url.mid(url.indexOf(QLatin1Char(',')) + 1).toLatin1()), "PNG");
  return img;
}
qreal alphaIou(const QImage& n, const QImage& ref) {
  int inter = 0, uni = 0;
  for (int y = 0; y < ref.height(); ++y)
    for (int x = 0; x < ref.width(); ++x) {
      const bool a = n.pixelColor(x, y).alpha() >= 32, b = ref.pixelColor(x, y).alpha() >= 32;
      inter += a && b;
      uni += a || b;
    }
  return uni ? qreal(inter) / uni : 1.0;
}
// Foreground RGBA similarity — catches COLOR regressions the alpha-only IoU
// misses (theme fills, classDef point colors). Both sides use the exact
// upstream theme palette, so this sits well above the threshold.
qreal foregroundRgba(const QImage& n, const QImage& ref) {
  qreal diff = 0.0;
  int uni = 0;
  const auto pm = [](int c, int a) { return c * a / 255; };
  for (int y = 0; y < ref.height(); ++y)
    for (int x = 0; x < ref.width(); ++x) {
      const QColor a = n.pixelColor(x, y), b = ref.pixelColor(x, y);
      if (a.alpha() < 32 && b.alpha() < 32)
        continue;
      ++uni;
      diff += std::abs(pm(a.red(), a.alpha()) - pm(b.red(), b.alpha()));
      diff += std::abs(pm(a.green(), a.alpha()) - pm(b.green(), b.alpha()));
      diff += std::abs(pm(a.blue(), a.alpha()) - pm(b.blue(), b.alpha()));
      diff += std::abs(a.alpha() - b.alpha());
    }
  return uni ? 1.0 - diff / (uni * 4.0 * 255.0) : 1.0;
}
} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected quadrant pixel manifest"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly))
    fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("154db3e7608e817a2ffe14198ec05b8a7362748ceb6ff4ff50b4dd3d20261ec6"),
          QStringLiteral("Quadrant pixel fixture changed; audit and update its digest"));
  const QDir dir = QFileInfo(QString::fromLocal8Bit(argv[1])).dir();
  qreal minIou = 1.0;
  QSet<QString> coveredThemes;
  for (const QJsonValue& cv : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject f = cv.toObject();
    const QString id = f.value(QStringLiteral("id")).toString();
    coveredThemes.insert(f.value(QStringLiteral("theme")).toString());
    const QString pngPath = dir.filePath(f.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) == f.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + ": browser PNG hash drifted");
    const QImage browser(pngPath);
    const qreal dpr = f.value(QStringLiteral("dpr")).toDouble(1.0);
    const QImage native = decodePng(
        editor::MermaidRenderCache::renderMermaidSourceToPng(f.value(QStringLiteral("source")).toString(), dpr)
            .dataUrl);
    require(!native.isNull(), id + ": production PNG must decode");
    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgba(native, browser);
    std::fprintf(stderr, "%s native=%dx%d browser=%dx%d iou=%.3f rgba=%.3f\n", qPrintable(id), native.width(),
                 native.height(), browser.width(), browser.height(), iou, rgba);
    std::fflush(stderr);
    // Upstream's viewBox is fractional and the browser screenshots it to the
    // nearest pixel; the port rounds its extent the same way.
    require(native.size() == browser.size(),
            id + QStringLiteral(": canvas %1x%2, browser %3x%4")
                    .arg(native.width()).arg(native.height()).arg(browser.width()).arg(browser.height()));
    require(iou >= 0.97, id + QStringLiteral(": alpha IoU regressed"));
    require(rgba >= 0.96, id + QStringLiteral(": foreground RGBA regressed"));
    minIou = std::min(minIou, iou);
  }
  // Theme axis: all 11 built-in themes are pinned at pixel level (mirrors the
  // flowchart golden-pixel coverage), so a dropped/renamed theme fails loudly.
  require(coveredThemes == QSet<QString>{QStringLiteral("default"), QStringLiteral("base"), QStringLiteral("dark"),
                                         QStringLiteral("forest"), QStringLiteral("neutral"), QStringLiteral("neo"),
                                         QStringLiteral("neo-dark"), QStringLiteral("redux"),
                                         QStringLiteral("redux-dark"), QStringLiteral("redux-color"),
                                         QStringLiteral("redux-dark-color")},
          QStringLiteral("Quadrant pixel theme coverage regressed"));
  qDebug() << "MermaidQuadrantPixelTest: passed; minimum alpha IoU" << minIou;
  return 0;
}
