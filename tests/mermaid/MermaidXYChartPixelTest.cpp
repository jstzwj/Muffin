// XYChart production-path pixel oracle. The browser fixtures use Mermaid
// 11.16.0 and bundled Noto Sans at the native 700x500 viewport.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QDebug>
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
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QByteArray sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
      .toHex();
}

QImage decodePng(const QString& dataUrl) {
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage image;
  if (comma >= 0)
    image.loadFromData(
        QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image;
}

bool isForeground(const QColor& color, const QColor& background) {
  if (color.alpha() < 32) return false;
  return std::abs(color.red() - background.red()) +
             std::abs(color.green() - background.green()) +
             std::abs(color.blue() - background.blue()) +
             std::abs(color.alpha() - background.alpha()) >
         12;
}

qreal foregroundIou(const QImage& native, const QImage& reference) {
  const QImage left = native.scaled(reference.size(), Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
  const QColor leftBackground = left.pixelColor(0, 0);
  const QColor referenceBackground = reference.pixelColor(0, 0);
  int intersection = 0;
  int united = 0;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = isForeground(left.pixelColor(x, y), leftBackground);
      const bool b = isForeground(reference.pixelColor(x, y), referenceBackground);
      intersection += a && b;
      united += a || b;
    }
  }
  return united ? qreal(intersection) / united : 1.0;
}

qreal foregroundRgbaSimilarity(const QImage& native, const QImage& reference) {
  const QImage left = native.scaled(reference.size(), Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
  const QColor leftBackground = left.pixelColor(0, 0);
  const QColor referenceBackground = reference.pixelColor(0, 0);
  qreal difference = 0.0;
  int pixels = 0;
  const auto premultiplied = [](int channel, int alpha) {
    return channel * alpha / 255;
  };
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const QColor a = left.pixelColor(x, y);
      const QColor b = reference.pixelColor(x, y);
      if (!isForeground(a, leftBackground) &&
          !isForeground(b, referenceBackground))
        continue;
      ++pixels;
      difference += std::abs(premultiplied(a.red(), a.alpha()) -
                             premultiplied(b.red(), b.alpha()));
      difference += std::abs(premultiplied(a.green(), a.alpha()) -
                             premultiplied(b.green(), b.alpha()));
      difference += std::abs(premultiplied(a.blue(), a.alpha()) -
                             premultiplied(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
    }
  }
  return pixels ? 1.0 - difference / (pixels * 4.0 * 255.0) : 1.0;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected XYChart pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("3981f694c3f955c16c294a9bd3913b5c2d01a66ed6d9a013b980952a67a5a3fa"),
          QStringLiteral("XYChart pixel fixture changed; audit its provenance"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 5,
          QStringLiteral("XYChart pixel fixture must contain five audited cases"));
  const QDir fixtureDir = QFileInfo(manifestPath).dir();
  qreal minimumIou = 1.0;
  qreal minimumRgba = 1.0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(id == QLatin1String("default") || id == QLatin1String("dark") ||
                id == QLatin1String("redux-color") ||
                id == QLatin1String("source-order-rotation") ||
                id == QLatin1String("hanging-data-label"),
            QStringLiteral("Unexpected XYChart pixel case: ") + id);
    const int width = fixture.value(QStringLiteral("width")).toInt();
    const int height = fixture.value(QStringLiteral("height")).toInt();
    require(width == 700 && height == 500,
            id + QStringLiteral(": browser canvas contract drifted"));
    const QString pngPath =
        fixtureDir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG hash drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull() && browser.size() == QSize(width, height),
            id + QStringLiteral(": browser PNG dimensions drifted"));

    const editor::MermaidPngRenderResult rendered =
        editor::MermaidRenderCache::renderMermaidSourceToPng(
            fixture.value(QStringLiteral("source")).toString(),
            fixture.value(QStringLiteral("dpr")).toDouble(1.0));
    const QImage native = decodePng(rendered.dataUrl);
    require(!native.isNull(), id + QStringLiteral(": native PNG did not decode"));
    require(native.size() == QSize(width, height),
            QStringLiteral("%1: native canvas %2x%3, expected %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(width).arg(height));

    const qreal iou = foregroundIou(native, browser);
    const qreal rgba = foregroundRgbaSimilarity(native, browser);
    std::fprintf(stderr, "%s native=%dx%d iou=%.6f rgba=%.6f\n",
                 qPrintable(id), native.width(), native.height(), iou, rgba);
    std::fflush(stderr);
    // Geometry is independently locked by the 16-case SVG oracle. These
    // thresholds retain cross-platform rasterization headroom while still
    // rejecting a wrong palette, background, or materially shifted chart.
    require(iou >= 0.90, id + QStringLiteral(": foreground IoU regressed"));
    require(rgba >= 0.90, id + QStringLiteral(": foreground RGBA similarity regressed"));
    minimumIou = std::min(minimumIou, iou);
    minimumRgba = std::min(minimumRgba, rgba);
  }

  qDebug() << "MermaidXYChartPixelTest: passed; minimum foreground IoU"
           << minimumIou << "minimum RGBA" << minimumRgba;
  return 0;
}
