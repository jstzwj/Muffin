#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/gitgraph/GitGraphScene.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool value, const QString& message) {
  if (!value) fail(message);
}
QByteArray sha256(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QCryptographicHash::hash(file.readAll(),
                                        QCryptographicHash::Sha256).toHex()
             : QByteArray();
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
  qint64 united = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const bool a = actual.pixelColor(x, y).alpha() >= 32;
      const bool b = expected.pixelColor(x, y).alpha() >= 32;
      intersection += a && b;
      united += a || b;
    }
  return united ? qreal(intersection) / qreal(united) : 1.0;
}
qreal foregroundRgba(const QImage& actual, const QImage& expected) {
  qreal difference = 0.0;
  qint64 count = 0;
  for (int y = 0; y < expected.height(); ++y)
    for (int x = 0; x < expected.width(); ++x) {
      const QColor a = actual.pixelColor(x, y);
      const QColor b = expected.pixelColor(x, y);
      if (a.alpha() < 16 && b.alpha() < 16) continue;
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
      ++count;
    }
  return count ? 1.0 - difference / (qreal(count) * 1020.0) : 1.0;
}
QImage isolatedDashImage(gitgraph::GitGraphScene scene) {
  QVector<gitgraph::GitGraphPrimitive> branches;
  for (gitgraph::GitGraphPrimitive primitive : scene.primitives) {
    if (primitive.role != QLatin1String("branch")) continue;
    primitive.css.stroke = QStringLiteral("#00ff00");
    branches.append(std::move(primitive));
  }
  scene.primitives = std::move(branches);
  const QRectF bounds = scene.renderBounds();
  QImage image(qCeil(bounds.width()), qCeil(bounds.height()),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.translate(-bounds.left(), -bounds.top());
  scene.paint(painter, MermaidPaintOptions{});
  painter.end();
  return image;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected GitGraph pixel manifest"));
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile manifest(manifestPath);
  require(manifest.open(QIODevice::ReadOnly), manifest.errorString());
  const QByteArray manifestBytes = manifest.readAll();
  require(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256)
                  .toHex() ==
              QByteArrayLiteral("e4578851cd14f0624d3f35c86a34dfed6c9fec35e7a7242f6d824ac5f032b3a7"),
          QStringLiteral("GitGraph pixel manifest bytes drifted"));
  const QJsonObject root = QJsonDocument::fromJson(manifestBytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("7e83f9e1ae14644c3bad81fad309117abde2602b84b37077c0a27df99646440d"),
          QStringLiteral("GitGraph pixel fixture provenance drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 5, QStringLiteral("GitGraph pixel case count"));
  const QDir directory = QFileInfo(manifestPath).dir();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString pngPath =
        directory.filePath(fixture.value(QStringLiteral("file")).toString());
    require(sha256(pngPath) ==
                fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG drifted"));
    const QImage browser(pngPath);
    require(!browser.isNull(), id + QStringLiteral(": browser PNG decode"));
    require(browser.size() ==
                QSize(fixture.value(QStringLiteral("width")).toInt(),
                      fixture.value(QStringLiteral("height")).toInt()),
            id + QStringLiteral(": manifest dimensions"));
    const QImage native = decode(
        editor::MermaidRenderCache::renderMermaidSourceToPng(
            fixture.value(QStringLiteral("source")).toString(), 1.0).dataUrl);
    require(!native.isNull(), id + QStringLiteral(": native PNG decode"));
    const bool dashCase = id == QLatin1String("dash-width-4");
    require(native.size() == browser.size(),
            QStringLiteral("%1: canvas %2x%3 != %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(browser.width()).arg(browser.height()));
    const qreal iou = alphaIou(native, browser);
    const qreal rgba = foregroundRgba(native, browser);
    std::fprintf(stderr, "%s IoU %.6f RGBA %.6f\n", qPrintable(id), iou,
                 rgba);
    require(iou >= 0.80, id + QStringLiteral(": alpha IoU regressed"));
    require(rgba >= 0.80,
            id + QStringLiteral(": foreground RGBA regressed"));
    if (dashCase) {
      const QJsonObject rootGeometry =
          fixture.value(QStringLiteral("rootGeometry")).toObject();
      require(rootGeometry.value(QStringLiteral("clientX")).toDouble() == 0.0 &&
                  rootGeometry.value(QStringLiteral("clientY")).toDouble() == 0.0 &&
                  rootGeometry.value(QStringLiteral("clientWidth")).toDouble() == 116.0 &&
                  rootGeometry.value(QStringLiteral("clientHeight")).toDouble() == 126.0 &&
                  rootGeometry.value(QStringLiteral("viewBox")).toString() ==
                      QLatin1String("-8 -20 116 126"),
              id + QStringLiteral(": browser client/viewBox drifted"));
      const QJsonArray styles = fixture.value(QStringLiteral("dashStyles")).toArray();
      require(styles.size() == 2, id + QStringLiteral(": two branch lines expected"));
      for (const QJsonValue& styleValue : styles) {
        const QJsonObject style = styleValue.toObject();
        require(style.value(QStringLiteral("stroke")).toString() ==
                    QLatin1String("rgb(0, 255, 0)") &&
                    style.value(QStringLiteral("strokeWidth")).toString() ==
                    QLatin1String("4px") &&
                    style.value(QStringLiteral("strokeDasharray")).toString() ==
                    QLatin1String("2px") &&
                    style.value(QStringLiteral("strokeLinecap")).toString() ==
                    QLatin1String("butt") &&
                    style.value(QStringLiteral("strokeLinejoin")).toString() ==
                    QLatin1String("miter"),
                id + QStringLiteral(": browser computed dash contract drifted"));
      }
      editor::MermaidRenderCache cache;
      const QString source = fixture.value(QStringLiteral("source")).toString();
      const auto entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
      const auto scene = std::dynamic_pointer_cast<const gitgraph::GitGraphScene>(entry.scene);
      require(bool(scene), id + QStringLiteral(": native GitGraph scene missing"));
      int branchCount = 0;
      for (const gitgraph::GitGraphPrimitive& primitive : scene->primitives) {
        if (primitive.role != QLatin1String("branch")) continue;
        ++branchCount;
        require(primitive.dash == QVector<qreal>{2.0, 2.0} &&
                    primitive.css.strokeWidth == QLatin1String("4px"),
                id + QStringLiteral(": native branch dash model drifted"));
      }
      require(branchCount == 2, id + QStringLiteral(": native branch count drifted"));
      const QRectF sceneBounds = scene->sceneBounds();
      const QRectF rasterBounds = scene->renderBounds();
      require(sceneBounds == QRectF(-8.0, -20.0, 116.0, 126.0) &&
                  rasterBounds == sceneBounds,
              id + QStringLiteral(": native client/raster bounds drifted"));
      const QString maskPath = directory.filePath(
          fixture.value(QStringLiteral("dashMaskFile")).toString());
      require(sha256(maskPath) ==
                  fixture.value(QStringLiteral("dashMaskSha256")).toString().toLatin1(),
              id + QStringLiteral(": browser dash mask hash drifted"));
      const QImage browserMask(maskPath);
      const QImage nativeMask = isolatedDashImage(*scene);
      if (qEnvironmentVariableIsSet("MUFFIN_SAVE_NATIVE"))
        nativeMask.save(QStringLiteral("build/native-gitgraph-dash-width-4-mask.png"));
      const qreal dashIou = alphaIou(nativeMask, browserMask);
      std::fprintf(stderr, "%s isolated dash native=%dx%d browser=%dx%d IoU %.6f\n",
                   qPrintable(id), nativeMask.width(), nativeMask.height(),
                   browserMask.width(), browserMask.height(), dashIou);
      require(dashIou >= 0.95,
              QStringLiteral("%1: isolated branch dash drifted: %2")
                  .arg(id).arg(dashIou));
    }
  }
  std::puts("MermaidGitGraphPixelTest: 5 cases passed");
  return 0;
}
