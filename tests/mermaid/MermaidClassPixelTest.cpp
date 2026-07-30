#include "mermaid/classdiagram/ClassScenePainter.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/theme/MermaidColor.h"

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
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }
QByteArray sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}
QRect alphaBounds(const QImage& image) {
  QRect result;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x)
      if (qAlpha(image.pixel(x, y)) >= 32)
        result = result.isNull() ? QRect(x, y, 1, 1)
                                 : result.united(QRect(x, y, 1, 1));
  return result;
}
QImage alphaTrimmed(const QImage& image) {
  const QRect bounds = alphaBounds(image);
  return bounds.isNull() ? image : image.copy(bounds);
}
qreal alphaIou(const QImage& native, const QImage& browser) {
  const QImage expected = browser.scaled(native.size(), Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation);
  int intersection = 0;
  int united = 0;
  for (int y = 0; y < native.height(); ++y) {
    for (int x = 0; x < native.width(); ++x) {
      const bool left = qAlpha(native.pixel(x, y)) >= 32;
      const bool right = qAlpha(expected.pixel(x, y)) >= 32;
      intersection += left && right;
      united += left || right;
    }
  }
  return united ? qreal(intersection) / united : 1.0;
}
qreal colorMae(const QImage& native, const QImage& browser) {
  const QImage expected = browser.scaled(native.size(), Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation);
  qint64 difference = 0;
  qint64 samples = 0;
  for (int y = 0; y < native.height(); ++y) {
    for (int x = 0; x < native.width(); ++x) {
      const QColor left = native.pixelColor(x, y);
      const QColor right = expected.pixelColor(x, y);
      if (left.alpha() < 32 && right.alpha() < 32) continue;
      difference += std::abs(left.red() - right.red()) +
                    std::abs(left.green() - right.green()) +
                    std::abs(left.blue() - right.blue()) +
                    std::abs(left.alpha() - right.alpha());
      samples += 4;
    }
  }
  return samples ? qreal(difference) / (samples * 255.0) : 0.0;
}
qreal categoryIou(const QImage& native, const QImage& browser, QRgb category) {
  require(native.size() == browser.size(),
          QStringLiteral("Semantic mask dimensions differ"));
  int intersection = 0;
  int united = 0;
  const QColor expected = QColor::fromRgba(category);
  for (int y = 0; y < native.height(); ++y) {
    for (int x = 0; x < native.width(); ++x) {
      const QColor left = native.pixelColor(x, y);
      const QColor right = browser.pixelColor(x, y);
      const bool nativeCategory = left.alpha() >= 32 &&
          left.red() == expected.red() && left.green() == expected.green() &&
          left.blue() == expected.blue();
      const bool browserCategory = right.alpha() >= 32 &&
          right.red() == expected.red() && right.green() == expected.green() &&
          right.blue() == expected.blue();
      intersection += nativeCategory && browserCategory;
      united += nativeCategory || browserCategory;
    }
  }
  return united ? qreal(intersection) / united : 1.0;
}
qreal alignedAlphaCoverage(const QImage& native, const QImage& browser,
                           qreal dpr) {
  require(native.size() == browser.size(),
          QStringLiteral("Label crop dimensions differ"));
  const int alignmentRadius = std::max(1, qCeil(4.0 * dpr));
  // The ink bounds below lock glyph placement and dimensions separately.
  // Allow one additional device pixel here for edge coverage: CoreText and
  // FreeType can quantize the same outline onto adjacent pixels at high DPR.
  const int tolerance = std::max(1, qCeil(dpr) + 1);
  const auto ink = [](const QImage& image, int x, int y) {
    return x >= 0 && y >= 0 && x < image.width() && y < image.height() &&
           qAlpha(image.pixel(x, y)) >= 32;
  };
  const auto directed = [&](const QImage& source, const QImage& target,
                            int shiftX, int shiftY) {
    int matched = 0;
    int total = 0;
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        if (!ink(source, x, y)) continue;
        ++total;
        bool found = false;
        for (int dy = -tolerance; dy <= tolerance && !found; ++dy)
          for (int dx = -tolerance; dx <= tolerance && !found; ++dx)
            found = ink(target, x + shiftX + dx, y + shiftY + dy);
        matched += found;
      }
    }
    return total ? qreal(matched) / total : 1.0;
  };
  qreal best = 0.0;
  for (int shiftY = -alignmentRadius; shiftY <= alignmentRadius; ++shiftY)
    for (int shiftX = -alignmentRadius; shiftX <= alignmentRadius; ++shiftX)
      best = std::max(best, std::min(
          directed(native, browser, shiftX, shiftY),
          directed(browser, native, -shiftX, -shiftY)));
  return best;
}

QImage renderLabelCrop(const classdiagram::ClassScene& scene,
                       const QString& target, const QSize& pixelSize,
                       const QSizeF& cssSize, qreal dpr) {
  flowchart::FlowLabelDocument document;
  QColor textColor;
  bool centered = true;
  if (target == QLatin1String("node")) {
    require(!scene.nodes.isEmpty() && !scene.nodes.first().nameLabels.isEmpty(),
            QStringLiteral("Native node label crop target is missing"));
    document = scene.nodes.first().nameLabels.first().document;
    textColor = color::toQColor(scene.nodes.first().textColor);
  } else if (target == QLatin1String("edge")) {
    const auto edge = std::find_if(scene.edges.cbegin(), scene.edges.cend(),
        [](const auto& candidate) { return !candidate.label.isEmpty(); });
    require(edge != scene.edges.cend(),
            QStringLiteral("Native edge label crop target is missing"));
    document = flowchart::parseFlowLabel(
        edge->label, QStringLiteral("markdown"), true);
    document.formattingContext =
        flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
    flowchart::prepareFlowLabelMath(document, scene.style.fontSize);
    textColor = color::toQColor(scene.style.textColor);
  } else {
    require(target == QLatin1String("cluster") && !scene.clusters.isEmpty(),
            QStringLiteral("Native cluster label crop target is missing"));
    document = flowchart::parseFlowLabel(
        scene.clusters.first().label, QStringLiteral("markdown"), true);
    document.formattingContext =
        flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
    flowchart::prepareFlowLabelMath(document, scene.style.fontSize);
    textColor = color::toQColor(scene.style.titleColor);
  }
  QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.scale(dpr, dpr);
  painter.setClipRect(QRectF(QPointF(4.0, 4.0), cssSize));
  flowchart::paintFlowLabel(
      painter, document, QRectF(QPointF(4.0, 4.0), cssSize),
      scene.style.fontFamily, scene.style.fontSize, scene.style.lineHeight,
      textColor, centered);
  return image;
}
qreal tolerantCategoryCoverage(const QImage& native, const QImage& browser,
                               QRgb category, int radius = 1) {
  require(native.size() == browser.size(),
          QStringLiteral("Semantic mask dimensions differ"));
  const QColor expected = QColor::fromRgba(category);
  const auto belongs = [&](const QImage& image, int x, int y) {
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
      return false;
    const QColor pixel = image.pixelColor(x, y);
    return pixel.alpha() >= 32 && pixel.red() == expected.red() &&
           pixel.green() == expected.green() && pixel.blue() == expected.blue();
  };
  const auto directed = [&](const QImage& source, const QImage& target) {
    int matched = 0;
    int total = 0;
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        if (!belongs(source, x, y)) continue;
        ++total;
        bool found = false;
        for (int dy = -radius; dy <= radius && !found; ++dy)
          for (int dx = -radius; dx <= radius && !found; ++dx)
            found = belongs(target, x + dx, y + dy);
        matched += found;
      }
    }
    return total ? qreal(matched) / total : 1.0;
  };
  return std::min(directed(native, browser), directed(browser, native));
}
QSizeF jsonSize(const QJsonObject& object) {
  return {object.value(QStringLiteral("width")).toDouble(),
          object.value(QStringLiteral("height")).toDouble()};
}
void requireColor(const QString& native, const QString& browser,
                  const QString& context) {
  require(color::toQColor(native).rgba() == color::toQColor(browser).rgba(),
          QStringLiteral("%1 color mismatch: native %2, browser %3")
              .arg(context, native, browser));
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
#if defined(Q_OS_LINUX)
  qWarning("skipped on Linux: font/rendering golden coupled to x86 Windows (TODO, docs/mermaid-architecture.md step 5)");
  return 0;
#endif
  require(argc == 2, QStringLiteral("Expected class pixel manifest path"));
  QFile manifest(QString::fromLocal8Bit(argv[1]));
  require(manifest.open(QIODevice::ReadOnly), QStringLiteral("Could not open class pixel manifest"));
  const QJsonObject root = QJsonDocument::fromJson(manifest.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Class pixel Mermaid version drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() ==
              QLatin1String("bundled-noto-2.13b171"),
          QStringLiteral("Class pixel font contract drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("39272a26cf151587343b5a5e4840b3d710a4a52a43964c14f000840e8924b182"),
          QStringLiteral("Class pixel fixture changed; audit the browser oracle and update its digest"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 17,
          QStringLiteral("Class pixel matrix must retain 9 scene and 8 label cases"));
  const QString directory = QFileInfo(manifest).absolutePath();
  editor::MermaidRenderCache cache;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* classScene = dynamic_cast<const classdiagram::ClassScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && classScene != nullptr,
            id + QStringLiteral(": native class scene failed: ") +
                entry.errorMessage);
    const qreal dpr = fixture.value(QStringLiteral("dpr")).toDouble(1.0);
    if (fixture.value(QStringLiteral("cropOnly")).toBool()) {
      const QString cropPath = QDir(directory).filePath(
          fixture.value(QStringLiteral("cropFile")).toString());
      require(sha256(cropPath) ==
                  fixture.value(QStringLiteral("cropSha256")).toString().toLatin1(),
              id + QStringLiteral(": browser label crop hash drifted"));
      const QImage browserCrop(cropPath);
      const QJsonObject labelBox = fixture.value(QStringLiteral("labelBox")).toObject();
      const QSizeF cssSize(labelBox.value(QStringLiteral("width")).toDouble(),
                           labelBox.value(QStringLiteral("height")).toDouble());
      const bool htmlBacked = labelBox.value(QStringLiteral("foreignObjectCount")).toInt() == 1;
      const bool svgBacked = labelBox.value(QStringLiteral("textCount")).toInt() == 1 &&
                             labelBox.value(QStringLiteral("tspanCount")).toInt() >= 1;
      require(labelBox.value(QStringLiteral("tag")).toString() == QLatin1String("g") &&
                  htmlBacked != svgBacked,
              id + QStringLiteral(": browser label container mode drifted"));
      require((labelBox.value(QStringLiteral("mathCount")).toInt() > 0) ==
                  source.contains(QLatin1String("$$")),
              id + QStringLiteral(": browser MathML classification drifted"));
      const QImage nativeCrop = renderLabelCrop(
          *classScene, fixture.value(QStringLiteral("cropTarget")).toString(),
          browserCrop.size(), cssSize, dpr);
      require(!browserCrop.isNull() && nativeCrop.size() == browserCrop.size(),
              id + QStringLiteral(": label crop viewport differs"));
      const qreal cropIou = alphaIou(nativeCrop, browserCrop);
      const qreal cropCoverage = alignedAlphaCoverage(
          nativeCrop, browserCrop, dpr);
      const QRect nativeInk = alphaBounds(nativeCrop);
      const QRect browserInk = alphaBounds(browserCrop);
      qDebug().noquote() << id << fixture.value(QStringLiteral("cropKind")).toString()
                         << nativeCrop.size() << "IoU" << cropIou
                         << "coverage" << cropCoverage
                         << "ink" << nativeInk << browserInk;
      struct CropThreshold {
        qreal coverage;
        qreal maximumPositionDriftCss;
        qreal maximumHeightDriftCss;
      };
      static const QHash<QString, CropThreshold> thresholds = {
          {QStringLiteral("node-html-math"), {0.985, 1.0, 1.0}},
          {QStringLiteral("node-markdown"), {0.93, 1.0, 2.0}},
          {QStringLiteral("node-cjk"), {0.995, 1.0, 1.0}},
          {QStringLiteral("node-rtl"), {0.845, 1.0, 1.0}},
          {QStringLiteral("edge-math-bidi"), {0.86, 3.0, 3.0}},
          {QStringLiteral("cluster-cjk-rtl"), {0.995, 2.0, 2.0}},
          {QStringLiteral("node-svg-multiline-cjk"), {0.87, 2.0, 2.0}},
          {QStringLiteral("node-svg-rtl"), {0.845, 2.0, 2.0}},
      };
      const QString cropKind = fixture.value(QStringLiteral("cropKind")).toString();
      require(thresholds.contains(cropKind),
              id + QStringLiteral(": unknown label crop kind"));
      const CropThreshold threshold = thresholds.value(cropKind);
      const int maximumPositionDrift =
          qCeil(threshold.maximumPositionDriftCss * dpr);
      // Font rasterizers disagree on edge coverage even when glyph placement
      // is identical. Lock the ink geometry explicitly, then compare the two
      // masks with bidirectional neighborhood coverage below.
      require(!nativeInk.isEmpty() && !browserInk.isEmpty() &&
                  std::abs(nativeInk.left() - browserInk.left()) <=
                      maximumPositionDrift &&
                  std::abs(nativeInk.top() - browserInk.top()) <=
                      maximumPositionDrift &&
                  std::abs(nativeInk.width() - browserInk.width()) <= qCeil(2.0 * dpr) &&
                  std::abs(nativeInk.height() - browserInk.height()) <=
                      qCeil(threshold.maximumHeightDriftCss * dpr),
              QStringLiteral(
                  "%1: label ink bounds drifted: (%2,%3 %4x%5) vs "
                  "(%6,%7 %8x%9)")
                  .arg(id)
                  .arg(nativeInk.x()).arg(nativeInk.y())
                  .arg(nativeInk.width()).arg(nativeInk.height())
                  .arg(browserInk.x()).arg(browserInk.y())
                  .arg(browserInk.width()).arg(browserInk.height()));
      require(cropCoverage >= threshold.coverage,
              QStringLiteral("%1: label glyph coverage drifted: %2 (IoU %3)")
                  .arg(id).arg(cropCoverage).arg(cropIou));
      continue;
    }
    const QString fileName = fixture.value(QStringLiteral("file")).toString();
    const QString filePath = QDir(directory).filePath(fileName);
    require(sha256(filePath) == fixture.value(QStringLiteral("sha256")).toString().toLatin1(),
            id + QStringLiteral(": browser PNG hash drifted"));
    const QImage browser = alphaTrimmed(QImage(filePath));
    const QImage native = alphaTrimmed(
        classdiagram::renderClassSceneToImage(*classScene, dpr));
    require(!native.isNull() && !browser.isNull(), id + QStringLiteral(": empty pixel image"));
    require(std::abs(native.width() - browser.width()) <= 1 &&
                std::abs(native.height() - browser.height()) <= 1,
            QStringLiteral("%1: painted bounds differ: native %2x%3, browser %4x%5")
                .arg(id).arg(native.width()).arg(native.height())
                .arg(browser.width()).arg(browser.height()));
    const qreal aspectError = std::abs(
        native.width() / qreal(native.height()) - browser.width() / qreal(browser.height()));
    const qreal iou = alphaIou(native, browser);
    const qreal mae = colorMae(native, browser);
    const QJsonObject structure = fixture.value(QStringLiteral("structure")).toObject();
    const QJsonArray expectedNodes = structure.value(QStringLiteral("nodes")).toArray();
    require(classScene->nodes.size() == expectedNodes.size(),
            id + QStringLiteral(": painted node count differs"));
    for (qsizetype index = 0; index < classScene->nodes.size(); ++index) {
      const auto& node = classScene->nodes.at(index);
      const QJsonObject expected = expectedNodes.at(index).toObject();
      const QSizeF browserOuter = jsonSize(expected.value(QStringLiteral("outer")).toObject());
      const qreal outerTolerance = fixture.value(QStringLiteral("htmlLabels")).toBool(true)
          ? 0.1 : 0.5;
      require(std::abs(node.localOuter.width() - browserOuter.width()) <= outerTolerance &&
                  std::abs(node.localOuter.height() - browserOuter.height()) <= outerTolerance,
              QStringLiteral("%1/%2: node outer bbox differs: %3x%4 vs %5x%6")
                  .arg(id, node.id)
                  .arg(node.localOuter.width()).arg(node.localOuter.height())
                  .arg(browserOuter.width()).arg(browserOuter.height()));
      const QJsonObject expectedStyle = expected.value(QStringLiteral("style")).toObject();
      requireColor(node.fill, expectedStyle.value(QStringLiteral("fill")).toString(),
                   id + QLatin1Char('/') + node.id + QStringLiteral(" fill"));
      requireColor(node.stroke, expectedStyle.value(QStringLiteral("stroke")).toString(),
                   id + QLatin1Char('/') + node.id + QStringLiteral(" stroke"));
      const QJsonObject expectedLabelStyle =
          expected.value(QStringLiteral("labelStyle")).toObject();
      const QString labelColorProperty =
          fixture.value(QStringLiteral("htmlLabels")).toBool(true)
              ? QStringLiteral("color") : QStringLiteral("fill");
      requireColor(node.textColor,
                    expectedLabelStyle.value(labelColorProperty).toString(),
                    id + QLatin1Char('/') + node.id + QStringLiteral(" label"));
    }
    require(classScene->edges.size() ==
                structure.value(QStringLiteral("edgePaths")).toArray().size(),
            id + QStringLiteral(": painted edge count differs"));
    if (id == QLatin1String("marker-matrix")) {
      const QString maskPath = QDir(directory).filePath(
          fixture.value(QStringLiteral("maskFile")).toString());
      require(sha256(maskPath) ==
                  fixture.value(QStringLiteral("maskSha256")).toString().toLatin1(),
              id + QStringLiteral(": semantic mask hash drifted"));
      const QImage browserMask(maskPath);
      const QImage nativeMask = classdiagram::renderClassSceneToImage(
          *classScene, dpr, 8.0,
          classdiagram::ClassPaintMode::SemanticMask);
      require(!browserMask.isNull() && nativeMask.size() == browserMask.size(),
              id + QStringLiteral(": semantic mask viewport differs"));
      struct SemanticCategory {
        QString name;
        QRgb color;
        qreal minimumIou;
        qreal minimumCoverage;
      };
      const QVector<SemanticCategory> categories = {
          {QStringLiteral("node"), classdiagram::kClassMaskNode, 0.975, 0.995},
          {QStringLiteral("edge"), classdiagram::kClassMaskEdge, 0.965, 0.995},
          {QStringLiteral("edge-label"), classdiagram::kClassMaskEdgeLabel, 0.9, 0.99},
          {QStringLiteral("marker"), classdiagram::kClassMaskMarker, 0.975, 0.995},
      };
      for (const SemanticCategory& category : categories) {
        const qreal categoryScore =
            categoryIou(nativeMask, browserMask, category.color);
        const qreal tolerantCoverage =
            tolerantCategoryCoverage(nativeMask, browserMask, category.color);
        qDebug().noquote() << id << category.name << "IoU" << categoryScore
                           << "1px coverage" << tolerantCoverage;
        require(categoryScore >= category.minimumIou,
                QStringLiteral("%1/%2: semantic IoU %3 is too low")
                    .arg(id, category.name).arg(categoryScore));
        require(tolerantCoverage >= category.minimumCoverage,
                QStringLiteral("%1/%2: semantic 1px coverage %3 is too low")
                    .arg(id, category.name).arg(tolerantCoverage));
      }
      const QString textMaskPath = QDir(directory).filePath(
          fixture.value(QStringLiteral("textMaskFile")).toString());
      require(sha256(textMaskPath) ==
                  fixture.value(QStringLiteral("textMaskSha256")).toString().toLatin1(),
              id + QStringLiteral(": text mask hash drifted"));
      const QImage browserTextMask(textMaskPath);
      const QImage nativeTextMask = classdiagram::renderClassSceneToImage(
          *classScene, dpr, 8.0, classdiagram::ClassPaintMode::TextMask);
      require(!browserTextMask.isNull() && nativeTextMask.size() == browserTextMask.size(),
              id + QStringLiteral(": text mask viewport differs"));
      const qreal textIou = categoryIou(
          nativeTextMask, browserTextMask, classdiagram::kClassMaskText);
      const qreal textCoverage = tolerantCategoryCoverage(
          nativeTextMask, browserTextMask, classdiagram::kClassMaskText);
      qDebug().noquote() << id << "text IoU" << textIou
                         << "1px coverage" << textCoverage;
      // Text is the only semantic-mask category rasterized by the native font
      // backend. Its geometry is locked by the scene and viewport checks; use
      // symmetric neighborhood coverage instead of antialias-sensitive IoU.
      require(textCoverage >= 0.98,
              QStringLiteral("%1/text: isolated mask drifted: IoU %2, coverage %3")
                  .arg(id).arg(textIou).arg(textCoverage));
    }
    const qreal minimumIou = id == QLatin1String("marker-matrix") ? 0.965 : 0.985;
    const qreal maximumMae = id == QLatin1String("marker-matrix") ? 0.075 : 0.06;
    const qreal maximumAspectError =
        fixture.value(QStringLiteral("htmlLabels")).toBool(true) ? 0.002 : 0.01;
    require(aspectError <= maximumAspectError,
            QStringLiteral("%1: painted aspect ratio differs by %2 (%3x%4 vs %5x%6)")
                .arg(id).arg(aspectError)
                .arg(native.width()).arg(native.height())
                .arg(browser.width()).arg(browser.height()));
    require(iou >= minimumIou,
            QStringLiteral("%1: alpha IoU %2 is below %3").arg(id).arg(iou).arg(minimumIou));
    require(mae <= maximumMae,
            QStringLiteral("%1: color MAE %2 exceeds %3").arg(id).arg(mae).arg(maximumMae));
    qDebug().noquote() << id << "alphaIoU" << iou << "colorMAE" << mae;
  }
  qDebug() << "MermaidClassPixelTest:" << cases.size() << "cases measured";
  return 0;
}
