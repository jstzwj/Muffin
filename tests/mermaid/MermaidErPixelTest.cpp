// Oracle for the erDiagram renderer against real Mermaid 11.16.0.
//
// tests/fixtures/mermaid/er-pixel holds, for each source, the PNG that
// Mermaid 11.16.0 produced in Chrome with the bundled Noto fonts: the
// canonical customer/order example in all eleven themes, and fifteen
// structures of the family (attribute keys and comments, every crow's-foot
// cardinality, non-identifying dashed relationships, a tree, wide attributes,
// CJK labels, a lone entity, a title, long labels, a self-relationship and its
// loop, a ring, aliases, a left-to-right diagram, and classDef and style), all
// in the classic look; nine under the neo look, over the themes whose shadow
// and strokes differ and the structures whose lines and markers it changes;
// and eight under the handDrawn look, which pins its seed, over the themes
// whose copy behind the shapes shows and the structures it redraws.
// The native renderer must produce an image of the browser's size whose ink
// lies where the browser's does, whose worst tile is close, and in which no
// color covers a notably different area. Every image is measured and
// reported, so one run shows the whole distance to upstream.

#include "MermaidPixelCompare.h"
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstdio>

using namespace muffin::mermaid;
using mermaid_pixel::require;

namespace {

// Floors with room for the rasterizer's variation between machines: each sits
// a little beyond the worst measure of the 42 images on this machine (0.992
// IoU, 0.966 RGBA, 0.914 tile, 0.015 of the image in one color outside the
// hachure). The worst tile is a tile of text, where Qt's glyphs and Chrome's
// differ in their antialiasing.
constexpr qreal kMinIoU = 0.985;
constexpr qreal kMinRgba = 0.95;
constexpr qreal kMinTile = 0.82;
// A dominant color that covers 3 % more or less of the image than in the
// browser's is a region painted otherwise, not a fringe of antialiasing.
constexpr qreal kMaxArea = 0.03;
// The handDrawn look fills with a hachure, ten thousand antialiased edges of
// overlapping strokes to an image: Qt's and Skia's coverage of each differs a
// little, and in a small image that is all hachure the flat color's area
// moves by up to 7 % (look-hand-drawn-attributeless: 0.067) where its ink
// lies as the browser's does (alpha IoU 0.9988).
constexpr qreal kMaxHachureArea = 0.07;

QByteArray fileDigest(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  if (argc != 2) {
    std::fprintf(stderr, "Expected ER pixel manifest\n");
    return 1;
  }
  const QString manifestPath = QString::fromLocal8Bit(argv[1]);
  QFile file(manifestPath);
  if (!file.open(QIODevice::ReadOnly)) {
    std::fprintf(stderr, "%s\n", qPrintable(file.errorString()));
    return 1;
  }
  const QByteArray bytes = file.readAll();
  if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
      QByteArrayLiteral("ae7f4b4bb7cf59ed750c85aa5fbd233e1bccb83d2d4f2e537e7ffbd9c391ba3b")) {
    std::fprintf(stderr, "ER pixel manifest bytes changed\n");
    return 1;
  }
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  if (root.value(QStringLiteral("fixtureSha256")).toString() !=
          QLatin1String("7d0c206812c7696c198cd79f99aa14e8471946a221ffd5496efaf2ca571e0e2d") ||
      root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() !=
          QLatin1String("11.16.0")) {
    std::fprintf(stderr, "ER pixel manifest provenance changed\n");
    return 1;
  }
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 42, QStringLiteral("ER pixel case count drifted: %1").arg(cases.size()));

  const QDir dir = QFileInfo(manifestPath).dir();
  QStringList failures;
  qreal worstIoU = 1.0, worstRgba = 1.0, worstTile = 1.0, worstArea = 0.0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString path = dir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(fileDigest(path) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG drifted"));
    const QImage referenceImage = QImage(path);
    const QImage nativeImage = mermaid_pixel::decodeDataUrl(
        editor::MermaidRenderCache::renderMermaidSourceToPng(fixture.value(QStringLiteral("source")).toString(),
                                                             fixture.value(QStringLiteral("dpr")).toDouble())
            .dataUrl);
    require(!nativeImage.isNull() && !referenceImage.isNull(), id + QStringLiteral(": PNG decode"));
    // MUFFIN_SAVE_NATIVE=<dir> also writes every native image, to look at a
    // difference by eye.
    if (qEnvironmentVariableIsSet("MUFFIN_SAVE_NATIVE"))
      nativeImage.save(QDir(qEnvironmentVariable("MUFFIN_SAVE_NATIVE")).filePath(id + QStringLiteral(".png")));

    // The image is the browser's size, unless the manifest declares the pixel
    // difference for this case: it must then match exactly, so a difference
    // that grows, changes or disappears all fail. The common area is compared.
    const QJsonObject declared = fixture.value(QStringLiteral("canvasDelta")).toObject();
    const QSize delta(nativeImage.width() - referenceImage.width(), nativeImage.height() - referenceImage.height());
    QImage native = nativeImage;
    QImage reference = referenceImage;
    if (delta.width() != 0 || delta.height() != 0) {
      if (declared.isEmpty()) {
        failures << QStringLiteral("%1: native %2x%3, browser %4x%5")
                        .arg(id)
                        .arg(nativeImage.width())
                        .arg(nativeImage.height())
                        .arg(referenceImage.width())
                        .arg(referenceImage.height());
        continue;
      }
      if (declared.value(QStringLiteral("width")).toInt() != delta.width() ||
          declared.value(QStringLiteral("height")).toInt() != delta.height()) {
        failures << QStringLiteral("%1: canvas differs by %2x%3, the manifest declares %4x%5")
                        .arg(id)
                        .arg(delta.width())
                        .arg(delta.height())
                        .arg(declared.value(QStringLiteral("width")).toInt())
                        .arg(declared.value(QStringLiteral("height")).toInt());
        continue;
      }
      const QRect common(QPoint(0, 0), reference.size().boundedTo(native.size()));
      native = native.copy(common);
      reference = reference.copy(common);
    } else if (!declared.isEmpty()) {
      failures
          << QStringLiteral("%1: canvas now matches the browser; remove its canvasDelta from the manifest").arg(id);
    }

    const mermaid_pixel::Measures measures = mermaid_pixel::measure(native, reference);
    worstIoU = qMin(worstIoU, measures.iou);
    worstRgba = qMin(worstRgba, measures.rgba);
    worstTile = qMin(worstTile, measures.tile);
    worstArea = qMax(worstArea, measures.area);
    std::fprintf(stderr, "%s%s iou=%.6f rgba=%.6f tile=%.6f area=%.4f worst=%.4f\n", qPrintable(id),
                 delta.width() || delta.height() ? " [canvas delta]" : "", measures.iou, measures.rgba, measures.tile,
                 measures.area, measures.worst);
    if (measures.iou < kMinIoU)
      failures << QStringLiteral("%1: alpha IoU %2 below %3").arg(id).arg(measures.iou, 0, 'f', 4).arg(kMinIoU);
    if (measures.rgba < kMinRgba)
      failures << QStringLiteral("%1: RGBA similarity %2 below %3").arg(id).arg(measures.rgba, 0, 'f', 4).arg(kMinRgba);
    if (measures.tile < kMinTile)
      failures << QStringLiteral("%1: worst tile similarity %2 below %3")
                      .arg(id)
                      .arg(measures.tile, 0, 'f', 4)
                      .arg(kMinTile);
    const qreal maxArea =
        fixture.value(QStringLiteral("look")).toString() == QLatin1String("handDrawn") ? kMaxHachureArea : kMaxArea;
    if (measures.area > maxArea)
      failures << QStringLiteral(
                      "%1: a dominant color covers %2 of the image more or less than in the browser, above %3")
                      .arg(id)
                      .arg(measures.area, 0, 'f', 4)
                      .arg(maxArea);
  }

  std::fprintf(stderr,
               "ER pixel: worst alpha IoU %.4f, worst RGBA similarity %.4f, worst tile similarity %.4f, "
               "worst dominant color area difference %.4f\n",
               worstIoU, worstRgba, worstTile, worstArea);
  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidErPixelTest: %lld image(s) differ from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::puts("MermaidErPixelTest passed: 42 images");
  return 0;
}
