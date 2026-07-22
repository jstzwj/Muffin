#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/state/StateScenePainter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
QByteArray sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}
qreal alphaIou(const QImage& first, const QImage& second) {
  constexpr int side = 400;
  const QImage left = first.scaled(side, side, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
  const QImage right = second.scaled(side, side, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
  int intersection = 0, united = 0;
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      const bool a = left.pixelColor(x, y).alpha() >= 32;
      const bool b = right.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  }
  return united ? qreal(intersection) / united : 1.0;
}
qreal foregroundRgbaSimilarity(const QImage& first, const QImage& second) {
  constexpr int side = 400;
  const QImage left = first.scaled(side, side, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
  const QImage right = second.scaled(side, side, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
  qreal difference = 0.0;
  int united = 0;
  for (int y = 0; y < side; ++y) {
    for (int x = 0; x < side; ++x) {
      const QColor a = left.pixelColor(x, y);
      const QColor b = right.pixelColor(x, y);
      if (a.alpha() < 32 && b.alpha() < 32) continue;
      ++united;
      const auto premultiplied = [](int channel, int alpha) {
        return channel * alpha / 255;
      };
      difference += std::abs(premultiplied(a.red(), a.alpha()) -
                             premultiplied(b.red(), b.alpha()));
      difference += std::abs(premultiplied(a.green(), a.alpha()) -
                             premultiplied(b.green(), b.alpha()));
      difference += std::abs(premultiplied(a.blue(), a.alpha()) -
                             premultiplied(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
    }
  }
  return united ? 1.0 - difference / (united * 4.0 * 255.0) : 1.0;
}
qreal relativeDifference(qreal actual, qreal expected) {
  return expected == 0.0 ? std::abs(actual) : std::abs(actual - expected) / expected;
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  if (argc != 2) fail(QStringLiteral("Expected state pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("3edaf534fa51b0963998342a62d365997600dc06ce243c4c7370f3a854ae3e18"),
          QStringLiteral("State pixel fixture changed; audit and update its digest"));
  const QDir fixtureDir = QFileInfo(manifestPath).dir();
  editor::MermaidRenderCache cache;
  qreal minimumIou = 1.0;
  int dprVariants = 0, darkCases = 0, clusterCases = 0;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QString pngPath = fixtureDir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG hash drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull() && browser.width() == fixture.value(QStringLiteral("width")).toInt() &&
                browser.height() == fixture.value(QStringLiteral("height")).toInt(),
            id + QStringLiteral(": browser PNG dimensions drifted"));
    const auto entry = cache.getSync(cache.makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready && entry.stateScene,
            id + QStringLiteral(": native render failed: ") + entry.errorMessage);
    const qreal dpr = fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    const QImage native = state::renderStateSceneToImage(*entry.stateScene, dpr, 0.0);
    const qreal widthDifference = relativeDifference(native.width(), browser.width());
    const qreal heightDifference = relativeDifference(native.height(), browser.height());
    const qreal iou = alphaIou(native, browser);
    const qreal rgbaSimilarity = foregroundRgbaSimilarity(native, browser);
    qDebug().noquote() << QStringLiteral("%1 native=%2x%3 browser=%4x%5 iou=%6 rgba=%7")
        .arg(id).arg(native.width()).arg(native.height()).arg(browser.width())
        .arg(browser.height()).arg(iou, 0, 'f', 3).arg(rgbaSimilarity, 0, 'f', 3);
    require(widthDifference <= 0.01 && heightDifference <= 0.01,
            QStringLiteral("%1: native/browser canvas differs by %2%% x %3%%")
                .arg(id).arg(widthDifference * 100.0, 0, 'f', 2)
                .arg(heightDifference * 100.0, 0, 'f', 2));
    require(iou >= 0.84, id + QStringLiteral(": native/browser alpha IoU regressed"));
    require(rgbaSimilarity >= 0.70,
            id + QStringLiteral(": native/browser foreground RGBA regressed"));
    minimumIou = std::min(minimumIou, iou);
    dprVariants += dpr != 1.0;
    darkCases += fixture.value(QStringLiteral("theme")).toString() == QLatin1String("dark");
    clusterCases += !entry.stateScene->clusters.isEmpty();
    const QImage repeated = state::renderStateSceneToImage(*entry.stateScene, dpr, 0.0);
    require(native == repeated, id + QStringLiteral(": native raster is non-deterministic"));
  }
  require(dprVariants >= 3 && darkCases >= 1 && clusterCases >= 2,
          QStringLiteral("State pixel matrix coverage regressed"));
  qDebug() << "MermaidStatePixelTest: 5 cases passed; minimum alpha IoU" << minimumIou;
  return 0;
}
