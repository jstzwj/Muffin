// Oracle for the erDiagram renderer against real Mermaid 11.16.0.
//
// tests/fixtures/mermaid/er-pixel holds, for each source, the PNG that
// Mermaid 11.16.0 produced in Chrome with the bundled Noto fonts: the
// canonical customer/order example in all eleven themes, and nine structures
// of the family (attribute keys and comments, every crow's-foot cardinality,
// non-identifying dashed relationships, a tree, wide attributes, CJK labels,
// a lone entity, a title, long labels). The native renderer must produce an
// image of the same size and closely the same pixels. Every image is measured
// and reported, so one run shows the whole distance to upstream.
//
// ER had no pixel oracle before; its geometry test asserted heights and
// cardinalities only. The structures stay inside the grammar the port reads
// like upstream; MermaidErGrammarTest and MermaidErGeometryOracleTest list
// what it does not.

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

// Floors with room for the rasterizer's variation between machines: the
// closest image measures 0.972 IoU and 0.871 RGBA.
constexpr qreal kMinIoU = 0.96;
constexpr qreal kMinRgba = 0.85;

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
      QByteArrayLiteral("c3446c64d22d09bc8985ffb0959a9a92d3afd1cc51c722c341c1a78fe0f4714c")) {
    std::fprintf(stderr, "ER pixel manifest bytes changed\n");
    return 1;
  }
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  if (root.value(QStringLiteral("fixtureSha256")).toString() !=
          QLatin1String("83954956bf3f7616372ee9deb9a25543fd64b2d9a286b76a9cd504d4dd1dc641") ||
      root.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() != QLatin1String("11.16.0")) {
    std::fprintf(stderr, "ER pixel manifest provenance changed\n");
    return 1;
  }
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 20, QStringLiteral("ER pixel case count drifted: %1").arg(cases.size()));

  const QDir dir = QFileInfo(manifestPath).dir();
  QStringList failures;
  qreal worstIoU = 1.0, worstRgba = 1.0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString path = dir.filePath(fixture.value(QStringLiteral("file")).toString());
    require(fileDigest(path) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG drifted"));
    const QImage referenceImage = QImage(path);
    const QImage nativeImage = mermaid_pixel::decodeDataUrl(editor::MermaidRenderCache::renderMermaidSourceToPng(
        fixture.value(QStringLiteral("source")).toString(), fixture.value(QStringLiteral("dpr")).toDouble()).dataUrl);
    require(!nativeImage.isNull() && !referenceImage.isNull(), id + QStringLiteral(": PNG decode"));

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
                        .arg(id).arg(nativeImage.width()).arg(nativeImage.height()).arg(referenceImage.width()).arg(referenceImage.height());
        continue;
      }
      if (declared.value(QStringLiteral("width")).toInt() != delta.width() ||
          declared.value(QStringLiteral("height")).toInt() != delta.height()) {
        failures << QStringLiteral("%1: canvas differs by %2x%3, the manifest declares %4x%5")
                        .arg(id).arg(delta.width()).arg(delta.height())
                        .arg(declared.value(QStringLiteral("width")).toInt())
                        .arg(declared.value(QStringLiteral("height")).toInt());
        continue;
      }
      const QRect common(QPoint(0, 0), reference.size().boundedTo(native.size()));
      native = native.copy(common);
      reference = reference.copy(common);
    } else if (!declared.isEmpty()) {
      failures << QStringLiteral("%1: canvas now matches the browser; remove its canvasDelta from the manifest").arg(id);
    }

    const mermaid_pixel::Measures measures = mermaid_pixel::measure(native, reference);
    worstIoU = qMin(worstIoU, measures.iou);
    worstRgba = qMin(worstRgba, measures.rgba);
    std::fprintf(stderr, "%s%s iou=%.6f rgba=%.6f worst=%.4f\n", qPrintable(id),
                 delta.width() || delta.height() ? " [canvas delta]" : "", measures.iou, measures.rgba, measures.worst);
    if (measures.iou < kMinIoU)
      failures << QStringLiteral("%1: alpha IoU %2 below %3").arg(id).arg(measures.iou, 0, 'f', 4).arg(kMinIoU);
    if (measures.rgba < kMinRgba)
      failures << QStringLiteral("%1: RGBA similarity %2 below %3").arg(id).arg(measures.rgba, 0, 'f', 4).arg(kMinRgba);
  }

  std::fprintf(stderr, "ER pixel: worst alpha IoU %.4f, worst RGBA similarity %.4f\n", worstIoU, worstRgba);
  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidErPixelTest: %lld image(s) differ from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::puts("MermaidErPixelTest passed: 20 images");
  return 0;
}
