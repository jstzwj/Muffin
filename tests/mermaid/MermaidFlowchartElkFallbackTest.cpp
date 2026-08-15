#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/scene/FlowScene.h"

#include <QCryptographicHash>
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
#include <memory>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
QByteArray sha256(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QCryptographicHash::hash(file.readAll(),
                                        QCryptographicHash::Sha256).toHex()
             : QByteArray();
}
QImage renderNative(const QString& source) {
  const QString dataUrl =
      editor::MermaidRenderCache::renderMermaidSourceToPng(source, 1.0).dataUrl;
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage image;
  if (comma >= 0)
    image.loadFromData(
        QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return image.convertToFormat(QImage::Format_RGBA8888);
}
qreal alphaIou(const QImage& native, const QImage& reference) {
  qint64 intersection = 0, united = 0;
  for (int y = 0; y < reference.height(); ++y)
    for (int x = 0; x < reference.width(); ++x) {
      const bool a = native.pixelColor(x, y).alpha() >= 32;
      const bool b = reference.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  return united ? qreal(intersection) / qreal(united) : 1.0;
}
qreal foregroundRgba(const QImage& native, const QImage& reference) {
  qreal difference = 0.0;
  qint64 samples = 0;
  for (int y = 0; y < reference.height(); ++y)
    for (int x = 0; x < reference.width(); ++x) {
      const QColor a = native.pixelColor(x, y);
      const QColor b = reference.pixelColor(x, y);
      if (a.alpha() < 16 && b.alpha() < 16) continue;
      const auto premul = [](int value, int alpha) {
        return value * alpha / 255;
      };
      difference += std::abs(premul(a.red(), a.alpha()) -
                             premul(b.red(), b.alpha()));
      difference += std::abs(premul(a.green(), a.alpha()) -
                             premul(b.green(), b.alpha()));
      difference += std::abs(premul(a.blue(), a.alpha()) -
                             premul(b.blue(), b.alpha()));
      difference += std::abs(a.alpha() - b.alpha());
      ++samples;
    }
  return samples ? 1.0 - difference / (qreal(samples) * 4.0 * 255.0)
                 : 1.0;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected flowchart-elk fixture"));
  const QFileInfo fixtureInfo(QString::fromLocal8Bit(argv[1]));
  QFile file(fixtureInfo.absoluteFilePath());
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral(
                  "0192a7731815fbb71005d59f60aa3833c6078426549a96df27c25890d6593e5f"),
          QStringLiteral("flowchart-elk fixture bytes drifted"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String(
                  "08796e86008c64b203d5ff3ce17118dc609ab99aa41c701ff928320d4232b31e"),
          QStringLiteral("flowchart-elk fixture provenance drifted"));
  const QJsonObject provenance =
      root.value(QStringLiteral("provenance")).toObject();
  require(provenance.value(QStringLiteral("version")).toString() ==
                  QLatin1String("11.16.0") &&
              !provenance.value(QStringLiteral("elkPackageRegistered")).toBool() &&
              provenance.value(QStringLiteral("registeredLayouts")).toArray().size() == 3,
          QStringLiteral("flowchart-elk runtime registration drifted"));

  editor::MermaidRenderCache cache;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 7, QStringLiteral("flowchart-elk case count"));
  int pixelCases = 0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QString dagreSource =
        fixture.value(QStringLiteral("dagreSource")).toString();
    const auto elk = cache.getSync(editor::MermaidRenderCache::makeKey(source),
                                   source);
    const auto dagre = cache.getSync(
        editor::MermaidRenderCache::makeKey(dagreSource), dagreSource);
    require(elk.status == editor::MermaidRenderStatus::Ready &&
                dagre.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral(": fallback did not render"));
    const auto elkScene =
        std::dynamic_pointer_cast<const flowscene::FlowScene>(elk.scene);
    const auto dagreScene =
        std::dynamic_pointer_cast<const flowscene::FlowScene>(dagre.scene);
    require(elkScene && dagreScene, id + QStringLiteral(": flow scene"));
    require(elkScene->toJson() == dagreScene->toJson() &&
                elk.naturalSize == dagre.naturalSize &&
                elk.metadata.contentSize == dagre.metadata.contentSize,
            id + QStringLiteral(": fallback scene differs from Dagre"));
    const QString expectedType = fixture.value(QStringLiteral("root")).toObject()
                                     .value(QStringLiteral("roleDescription"))
                                     .toString();
    require(elk.metadata.diagramType == expectedType &&
                fixture.value(QStringLiteral("fallbackWarning")).toString()
                    .contains(QStringLiteral(
                        "rendered using `dagre` layout as a fallback")),
            id + QStringLiteral(": detector/fallback evidence"));
    const qsizetype browserNodes =
        fixture.value(QStringLiteral("nodes")).toArray().size();
    const qsizetype browserEdges =
        fixture.value(QStringLiteral("edges")).toArray().size();
    const qsizetype browserClusters =
        fixture.value(QStringLiteral("clusters")).toArray().size();
    require(elkScene->nodes.size() == browserNodes &&
                elkScene->edges.size() == browserEdges &&
                elkScene->clusters.size() == browserClusters,
            QStringLiteral("%1: structure count native=%2/%3/%4 browser=%5/%6/%7")
                .arg(id).arg(elkScene->nodes.size()).arg(elkScene->edges.size())
                .arg(elkScene->clusters.size()).arg(browserNodes)
                .arg(browserEdges).arg(browserClusters));

    const QJsonObject pixel = fixture.value(QStringLiteral("pixel")).toObject();
    if (pixel.isEmpty()) continue;
    ++pixelCases;
    const QString pngPath = fixtureInfo.absolutePath() +
                            QStringLiteral("/flowchart-elk-pixel/") +
                            pixel.value(QStringLiteral("file")).toString();
    require(sha256(pngPath) ==
                pixel.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG drifted"));
    const QImage browser(pngPath);
    const QImage native = renderNative(source);
    const QImage nativeDagre = renderNative(dagreSource);
    require(!browser.isNull() && !native.isNull() && !nativeDagre.isNull(),
            id + QStringLiteral(": PNG decode"));
    require(native == nativeDagre,
            id + QStringLiteral(": production fallback PNG differs from Dagre"));
    require(native.size() == browser.size(),
            QStringLiteral("%1: %2x%3 != %4x%5 scene=%6x%7 content=%8x%9")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(browser.width()).arg(browser.height())
                .arg(elkScene->bounds.width(), 0, 'g', 17)
                .arg(elkScene->bounds.height(), 0, 'g', 17)
                .arg(elk.metadata.contentSize.width(), 0, 'g', 17)
                .arg(elk.metadata.contentSize.height(), 0, 'g', 17));
    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgba(native, browser);
    std::fprintf(stderr, "%s IoU %.6f RGBA %.6f\n", qPrintable(id), iou,
                 rgba);
    require(iou >= 0.88 && rgba >= 0.88,
            id + QStringLiteral(": browser pixel parity regressed"));
  }
  require(pixelCases == 3, QStringLiteral("flowchart-elk pixel case count"));
  std::puts("MermaidFlowchartElkFallbackTest: 7 cases passed");
  return 0;
}
