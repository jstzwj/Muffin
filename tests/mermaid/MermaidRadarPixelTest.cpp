// Browser-vs-native Radar raster oracle. Fixtures are attached-DOM Chrome
// screenshots at DPR 1 with the bundled Noto Sans font.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QByteArray>
#include <QColor>
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
#include <QString>

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

qreal alphaIou(const QImage& native, const QImage& reference) {
  const QImage left = native.scaled(reference.size(), Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
  qint64 intersection = 0;
  qint64 united = 0;
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = left.pixelColor(x, y).alpha() >= 32;
      const bool b = reference.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  }
  return united ? qreal(intersection) / qreal(united) : 1.0;
}

qreal foregroundRgba(const QImage& native, const QImage& reference) {
  const QImage left = native.scaled(reference.size(), Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
  qreal difference = 0.0;
  qint64 united = 0;
  const auto premultiplied = [](int channel, int alpha) {
    return channel * alpha / 255;
  };
  for (int y = 0; y < reference.height(); ++y) {
    for (int x = 0; x < reference.width(); ++x) {
      const QColor a = left.pixelColor(x, y);
      const QColor b = reference.pixelColor(x, y);
      if (a.alpha() < 32 && b.alpha() < 32) continue;
      ++united;
      difference += std::abs(premultiplied(a.red(), a.alpha()) -
                             premultiplied(b.red(), b.alpha()));
      difference += std::abs(premultiplied(a.green(), a.alpha()) -
                             premultiplied(b.green(), b.alpha()));
      difference += std::abs(premultiplied(a.blue(), a.alpha()) -
                             premultiplied(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
    }
  }
  return united ? 1.0 - difference / (qreal(united) * 4.0 * 255.0)
                : 1.0;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected radar pixel manifest path"));

  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("upstream")).toObject()
                      .value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("224a017a84fd602c2fc777e6a534f41795cb98bf14fe879f3a745d51b6dc365e"),
          QStringLiteral("Radar pixel fixture provenance drifted"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 4,
          QStringLiteral("Radar pixel fixture must contain four cases"));
  const QDir fixtureDir = QFileInfo(manifestPath).dir();
  qreal minimumIou = 1.0;
  qreal minimumRgba = 1.0;
  bool sawDefault = false;
  bool sawDark = false;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    sawDefault |= id == QLatin1String("default");
    sawDark |= id == QLatin1String("dark");
    require(id == QLatin1String("default") || id == QLatin1String("dark") ||
                id == QLatin1String("redux-color") ||
                id == QLatin1String("path-stress"),
            QStringLiteral("Unexpected Radar pixel case: ") + id);

    const int width = fixture.value(QStringLiteral("width")).toInt();
    const int height = fixture.value(QStringLiteral("height")).toInt();
    require(width == 700 && height == 700,
            id + QStringLiteral(": fixture canvas dimensions drifted"));
    const QString pngPath = fixtureDir.filePath(
        fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG hash drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull() && browser.size() == QSize(width, height),
            id + QStringLiteral(": browser PNG failed to load"));

    const qreal dpr = fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    const editor::MermaidPngRenderResult result =
        editor::MermaidRenderCache::renderMermaidSourceToPng(
            fixture.value(QStringLiteral("source")).toString(), dpr);
    const QImage native = decodePng(result.dataUrl);
    require(!native.isNull(), id + QStringLiteral(": production PNG did not decode"));
    require(native.size() == browser.size(),
            QStringLiteral("%1: native canvas %2x%3, expected %4x%5")
                .arg(id)
                .arg(native.width())
                .arg(native.height())
                .arg(browser.width())
                .arg(browser.height()));

    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgba(native, browser);
    std::fprintf(stderr, "%s native=%dx%d browser=%dx%d iou=%.6f rgba=%.6f\n",
                 qPrintable(id), native.width(), native.height(), browser.width(),
                 browser.height(), iou, rgba);
    std::fflush(stderr);
    require(iou >= 0.88, id + QStringLiteral(": alpha IoU regressed"));
    require(rgba >= 0.85,
            id + QStringLiteral(": foreground RGBA similarity regressed"));
    minimumIou = std::min(minimumIou, iou);
    minimumRgba = std::min(minimumRgba, rgba);
  }

  require(sawDefault && sawDark,
          QStringLiteral("Radar pixel oracle must cover default and dark"));
  qDebug() << "MermaidRadarPixelTest: minimum alpha IoU" << minimumIou
           << "minimum foreground RGBA" << minimumRgba;
  return 0;
}
