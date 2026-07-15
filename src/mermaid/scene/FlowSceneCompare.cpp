#include "mermaid/scene/FlowSceneCompare.h"

#include "mermaid/scene/FlowScenePainter.h"

#include <QColor>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::flowscene {
namespace {

enum class Cat { Empty, Node, Cluster, Edge, EdgeLabelBg, Text, Shadow, Boundary };

Cat decodeCat(QRgb px) {
  switch (px) {
    case kCatNode: return Cat::Node;
    case kCatCluster: return Cat::Cluster;
    case kCatEdge: return Cat::Edge;
    case kCatEdgeLabelBg: return Cat::EdgeLabelBg;
    case kCatText: return Cat::Text;
    case kCatShadow: return Cat::Shadow;
    case kCatBoundary: return Cat::Boundary;
    default: return Cat::Empty;  // transparent (never painted) = EMPTY
  }
}

int pxSize(const QString& s) {
  static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)?)"));
  const QRegularExpressionMatch m = re.match(s);
  return m.hasMatch() ? static_cast<int>(std::round(m.captured(1).toDouble())) : 16;
}

QFont labelFont(const FlowSceneLabel& label, const QString& fontFamily) {
  QFont font(fontFamily);
  font.setPixelSize(pxSize(label.fontSize.isEmpty() ? QStringLiteral("16px") : label.fontSize));
  if (label.fontWeight == QLatin1String("bold")) font.setBold(true);
  return font;
}

// Tight ink bounding rect (scene coords) for every label the painter draws.
QVector<QRectF> collectLabelRectsScene(const FlowScene& scene, const QString& fontFamily) {
  QVector<QRectF> rects;
  auto add = [&](const FlowSceneLabel& label, const QPointF& center) {
    if (label.text.isEmpty()) return;
    const QFontMetrics fm(labelFont(label, fontFamily));
    const QRectF tight = fm.tightBoundingRect(label.text);
    // Inflate generously (+3px): the AA-on colour render's glyph coverage extends
    // beyond QFontMetrics::tightBoundingRect, and those fringe glyph pixels must
    // be excluded from the INTERIOR check (otherwise text-over-fill reads as a
    // fill-colour mismatch).
    rects.append(QRectF(center.x() + tight.x() - 3, center.y() + tight.y() - 3,
                        tight.width() + 6, tight.height() + 6));
  };
  for (const FlowSceneNode& n : scene.nodes) add(n.label, QPointF(n.label.x, n.label.y));
  for (const FlowSceneEdge& e : scene.edges) add(e.label, QPointF(e.label.x, e.label.y));
  for (const FlowSceneCluster& c : scene.clusters) {
    if (c.label.text.isEmpty()) continue;
    const QRectF r(c.cx - c.width / 2.0, c.cy - c.height / 2.0, c.width, c.height);
    add(c.label, QPointF(r.left() + 4 + c.label.x, r.top() + 2 + c.label.y));
  }
  return rects;
}

// Tight bounding box of all non-transparent pixels (the actual painted content).
QRect paintedBBox(const QImage& img, int alphaThreshold) {
  int minX = img.width(), minY = img.height(), maxX = -1, maxY = -1;
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      if (qAlpha(img.pixel(x, y)) > alphaThreshold) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
  }
  if (maxX < 0) return QRect();
  return QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

bool rectsIntersectAny(const QRectF& r, const QVector<QRectF>& rects) {
  for (const QRectF& b : rects)
    if (b.intersects(r)) return true;
  return false;
}

// Alpha-mask IoU between native and golden ink over a label crop. The caller
// supplies a DPR-scaled search radius because DirectWrite's Qt/Chromium
// baselines can differ by several physical pixels for unboxed text shapes.
qreal labelIou(const QImage& native, const QImage& golden, const QRect& crop,
               int goldenOffX, int goldenOffY, int inkAlpha, int searchRadius) {
  const int tolerance = std::max(1, searchRadius / 5);
  auto inkNear = [&](const QImage& image, int x, int y) {
    for (int oy = -tolerance; oy <= tolerance; ++oy) {
      for (int ox = -tolerance; ox <= tolerance; ++ox) {
        const int sx = std::clamp(x + ox, 0, image.width() - 1);
        const int sy = std::clamp(y + oy, 0, image.height() - 1);
        if (qAlpha(image.pixel(sx, sy)) >= inkAlpha) return true;
      }
    }
    return false;
  };
  auto coverageAt = [&](int dx, int dy) {
    qint64 nativeInk = 0, goldenInk = 0;
    qint64 nativeMatched = 0, goldenMatched = 0;
    for (int y = crop.top(); y <= crop.bottom(); ++y) {
      for (int x = crop.left(); x <= crop.right(); ++x) {
        const bool a = qAlpha(native.pixel(x, y)) >= inkAlpha;
        const int gx = std::clamp(x + goldenOffX + dx, 0, golden.width() - 1);
        const int gy = std::clamp(y + goldenOffY + dy, 0, golden.height() - 1);
        const bool b = qAlpha(golden.pixel(gx, gy)) >= inkAlpha;
        if (a) {
          ++nativeInk;
          if (inkNear(golden, gx, gy)) ++nativeMatched;
        }
        if (b) {
          ++goldenInk;
          if (inkNear(native, x, y)) ++goldenMatched;
        }
      }
    }
    if (nativeInk == 0 && goldenInk == 0) return 1.0;
    if (nativeInk == 0 || goldenInk == 0) return 0.0;
    return std::min(static_cast<qreal>(nativeMatched) / nativeInk,
                    static_cast<qreal>(goldenMatched) / goldenInk);
  };
  qreal best = 0.0;
  for (int dy = -searchRadius; dy <= searchRadius; ++dy)
    for (int dx = -searchRadius; dx <= searchRadius; ++dx)
      best = std::max(best, coverageAt(dx, dy));
  return best;
}

}  // namespace

CompareReport compareLevel3(const FlowScene& scene, const QImage& goldenIn, const QString& fontFamily,
                            const CompareThresholds& thresholds, const QString& failureDir,
                            bool enforceInterior, qreal dpr) {
  CompareReport report;
  dpr = std::max<qreal>(1.0, dpr);
  const bool debug = !qgetenv("MUFFIN_PIXEL_DEBUG").isEmpty();
  QHash<QString, qint64>* debugHistogram = debug ? new QHash<QString, qint64> : nullptr;
  QImage golden = goldenIn.convertToFormat(QImage::Format_ARGB32);

  // Native render: scene.bounds inflated by a margin so edges/markers/labels that
  // extend beyond the node+cluster bounds are captured. The margin is later
  // stripped by cropping to the actual painted bbox.
  const qreal margin = 80.0;
  const qreal originX = scene.bounds.left() - margin;
  const qreal originY = scene.bounds.top() - margin;
  const int cw = static_cast<int>(std::ceil((scene.bounds.width() + margin * 2.0) * dpr));
  const int ch = static_cast<int>(std::ceil((scene.bounds.height() + margin * 2.0) * dpr));

  QImage color(cw, ch, QImage::Format_ARGB32);
  color.fill(Qt::transparent);
  QImage mask(cw, ch, QImage::Format_ARGB32);
  mask.fill(Qt::transparent);
  {
    QPainter p(&color);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(dpr, dpr);
    p.translate(-originX, -originY);
    paintFlowScene(scene, p, fontFamily, PaintMode::Color);
  }
  {
    QPainter p(&mask);
    p.scale(dpr, dpr);
    p.translate(-originX, -originY);
    paintFlowScene(scene, p, fontFamily, PaintMode::CategoryMask);
  }

  // Normalize both to their painted-content tight bbox (strips origin + margin +
  // mermaid padding), then center-align — so pixel (x,y) is the same content
  // location in both, regardless of how each side computed its bounds.
  const QRect nativeBB = paintedBBox(color, thresholds.emptyMaxGoldenAlpha);
  const QRect goldenBB = paintedBBox(golden, thresholds.emptyMaxGoldenAlpha);
  if (nativeBB.isEmpty() || goldenBB.isEmpty()) {
    report.passed = false;
    report.summary = QStringLiteral("empty render: native=%1x%2 golden=%3x%4").arg(cw).arg(ch).arg(golden.width()).arg(golden.height());
    return report;
  }
  const QImage nativeCrop = color.copy(nativeBB);
  const QImage maskCrop = mask.copy(nativeBB);
  const QImage goldenCrop = golden.copy(goldenBB);

  const int nw = nativeCrop.width(), nh = nativeCrop.height();
  const int gw = goldenCrop.width(), gh = goldenCrop.height();
  // Refine the alignment: the painted bboxes include AA fringe that differs
  // between Qt and Chrome, so the naive center offset can be ~1px off. Search a
  // small window for the integer offset maximizing alpha-silhouette overlap
  // (shapes match to ≤0.002px in geometry, so the true offset is within ±3px).
  const int baseOffX = (gw - nw) / 2, baseOffY = (gh - nh) / 2;
  int offX = baseOffX, offY = baseOffY;
  {
    qint64 bestOverlap = -1;
    const int alignmentRadius = static_cast<int>(std::ceil(3.0 * dpr));
    for (int dy = -alignmentRadius; dy <= alignmentRadius; ++dy) {
      for (int dx = -alignmentRadius; dx <= alignmentRadius; ++dx) {
        const int ox = baseOffX + dx, oy = baseOffY + dy;
        qint64 overlap = 0;
        for (int y = 0; y < nh; ++y) {
          for (int x = 0; x < nw; ++x) {
            if (qAlpha(nativeCrop.pixel(x, y)) <= 128) continue;
            const int gx = x + ox, gy = y + oy;
            if (gx >= 0 && gx < gw && gy >= 0 && gy < gh && qAlpha(goldenCrop.pixel(gx, gy)) > 128) ++overlap;
          }
        }
        if (overlap > bestOverlap) {
          bestOverlap = overlap;
          offX = ox;
          offY = oy;
        }
      }
    }
  }
  // Content size drift beyond a small fraction ⇒ genuine layout/render mismatch.
  const int tolW = std::max(4, nw / 8), tolH = std::max(4, nh / 8);
  if (std::abs(gw - nw) > tolW || std::abs(gh - nh) > tolH) {
    report.passed = false;
    report.summary = QStringLiteral("painted-content size drift: native=%1x%2 golden=%3x%4 (tol %5x%6)")
                         .arg(nw).arg(nh).arg(gw).arg(gh).arg(tolW).arg(tolH);
    return report;
  }

  // Label rects: scene coords → native canvas coords (-origin) → cropped coords (-nativeBB.topLeft).
  const QVector<QRectF> labelRectsScene = collectLabelRectsScene(scene, fontFamily);
  QVector<QRectF> labelRects;
  QVector<QRectF> labelProtectionRects;
  labelRects.reserve(labelRectsScene.size());
  labelProtectionRects.reserve(labelRectsScene.size());
  for (const QRectF& r : labelRectsScene) {
    QRectF s((r.x() - originX) * dpr - nativeBB.left(),
             (r.y() - originY) * dpr - nativeBB.top(),
             r.width() * dpr, r.height() * dpr);
    labelRects.append(s);
    // The global image alignment and per-label glyph alignment are independent.
    // Keep their combined search fringe under the label metric without removing
    // empty pixels from the shape-coverage denominator.
    labelProtectionRects.append(
        s.adjusted(-10.0 * dpr, -10.0 * dpr, 10.0 * dpr, 10.0 * dpr));
  }

  auto goldenPx = [&](int x, int y) -> QRgb {
    return goldenCrop.pixel(std::clamp(x + offX, 0, gw - 1), std::clamp(y + offY, 0, gh - 1));
  };

  // Decode the mask into a category grid + a deep-interior mask (a fill pixel
  // whose 8 neighbours are the SAME fill category). The 1px ring at every shape
  // edge is where sub-pixel Chrome-vs-Qt alignment jitter lives, so it is judged
  // by the tolerant BOUNDARY rule, not the exact INTERIOR rule.
  QVector<Cat> cats(nw * nh, Cat::Empty);
  for (int y = 0; y < nh; ++y)
    for (int x = 0; x < nw; ++x) cats[y * nw + x] = decodeCat(maskCrop.pixel(x, y));
  auto isFill = [&](Cat c) {
    if (scene.look == flowchart::FlowLook::HandDrawn) return false;
    return c == Cat::Node || c == Cat::Cluster || c == Cat::EdgeLabelBg;
  };
  auto deepInterior = [&](int x, int y) {
    const Cat c = cats[y * nw + x];
    if (!isFill(c)) return false;
    // Erode by 2px (5x5 neighbourhood uniform) so the thick-stroke band (e.g. a
    // 2px node border) and the 1px sub-pixel alignment ring fall in BOUNDARY.
    const qreal cssErosion = scene.look == flowchart::FlowLook::Neo ? 3.0 : 2.0;
    const int erosion = static_cast<int>(std::ceil(cssErosion * dpr));
    for (int dy = -erosion; dy <= erosion; ++dy)
      for (int dx = -erosion; dx <= erosion; ++dx) {
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= nw || ny >= nh) return false;
        if (cats[ny * nw + nx] != c) return false;
      }
    return true;
  };

  for (int y = 0; y < nh; ++y) {
    for (int x = 0; x < nw; ++x) {
      if (rectsIntersectAny(QRectF(x, y, 1, 1), labelRects)) continue;
      const Cat cat = cats[y * nw + x];
      const QRgb np = nativeCrop.pixel(x, y);
      const QRgb gp = goldenPx(x, y);
      const int nAlpha = qAlpha(np);
      if (cat == Cat::Empty) {
        ++report.emptyPixels;
        if (qAlpha(gp) > thresholds.emptyMaxGoldenAlpha) ++report.emptyMismatch;
        continue;
      }
      if (cat == Cat::Text) continue;
      if (isFill(cat) && rectsIntersectAny(QRectF(x, y, 1, 1), labelProtectionRects)) {
        ++report.boundary;
        continue;
      }
      if (deepInterior(x, y)) {
        ++report.interior;
        if (qRed(np) != qRed(gp) || qGreen(np) != qGreen(gp) || qBlue(np) != qBlue(gp)) {
          ++report.interiorMismatch;
          if (debugHistogram) {
            const QString key = QStringLiteral("%1,%2,%3->%4,%5,%6")
                                    .arg(qRed(gp)).arg(qGreen(gp)).arg(qBlue(gp))
                                    .arg(qRed(np)).arg(qGreen(np)).arg(qBlue(np));
            ++(*debugHistogram)[key];
          }
        }
      } else {
        // BOUNDARY (edge ring, strokes, edges, AA fringe). A pixel only counts
        // as deviant when BOTH sides have real ink yet the colour differs (a
        // genuine stroke-colour/edge mismatch) — silhouette-fringe pixels where
        // only one side has ink are expected Chrome-vs-Qt rasterization.
        ++report.boundary;
        if (nAlpha > 128 && qAlpha(gp) > 128) {
          const int diff = std::max({std::abs(qRed(np) - qRed(gp)), std::abs(qGreen(np) - qGreen(gp)),
                                     std::abs(qBlue(np) - qBlue(gp))});
          if (diff > thresholds.boundaryMaxRgbaDiff) ++report.boundaryDeviant;
        }
      }
    }
  }

  for (const QRectF& lr : labelRects) {
    const QRect crop = lr.toRect().intersected(QRect(0, 0, nw, nh));
    if (crop.isEmpty()) continue;
    ++report.labels;
    const qreal iou = labelIou(nativeCrop, goldenCrop, crop, offX, offY,
                               thresholds.textInkAlpha,
                               static_cast<int>(std::ceil(5.0 * dpr)));
    if (iou < report.worstLabelIou) {
      report.worstLabelIou = iou;
      report.worstLabel = QStringLiteral("%1,%2 %3x%4").arg(crop.x()).arg(crop.y()).arg(crop.width()).arg(crop.height());
    }
    if (iou < thresholds.textGlyphIou) ++report.labelFailures;
  }

  const qreal boundaryDeviantRatio = report.boundary == 0 ? 0.0 : static_cast<qreal>(report.boundaryDeviant) / report.boundary;
  const qreal interiorMismatchRatio = report.interior == 0 ? 0.0 : static_cast<qreal>(report.interiorMismatch) / report.interior;
  const qreal emptyMismatchRatio = (report.interior + report.boundary) == 0 ? 0.0
                                       : static_cast<qreal>(report.emptyMismatch) / static_cast<qreal>(report.interior + report.boundary);
  const bool interiorOk = !enforceInterior || interiorMismatchRatio <= thresholds.interiorMaxMismatchRatio;
  report.passed = interiorOk && boundaryDeviantRatio <= thresholds.boundaryMaxDeviantRatio &&
                  emptyMismatchRatio <= thresholds.emptyMaxMismatchRatio && report.labelFailures == 0;
  report.summary = QStringLiteral("interior %1/%2 (%3%%4), boundary deviant %5/%6 (%7%), empty mismatch %8 (%9%), labels %10/%11 bad (worst IoU %12 @ %13)")
                       .arg(report.interiorMismatch).arg(report.interior).arg(interiorMismatchRatio * 100.0, 0, 'f', 2)
                       .arg(enforceInterior ? QStringLiteral("") : QStringLiteral(" NOT-ENFORCED"))
                       .arg(report.boundaryDeviant).arg(report.boundary).arg(boundaryDeviantRatio * 100.0, 0, 'f', 2)
                       .arg(report.emptyMismatch).arg(emptyMismatchRatio * 100.0, 0, 'f', 2)
                       .arg(report.labelFailures).arg(report.labels)
                       .arg(report.worstLabelIou, 0, 'f', 3).arg(report.worstLabel);
  if (debugHistogram) {
    QVector<QPair<qint64, QString>> sorted;
    for (auto it = debugHistogram->constBegin(); it != debugHistogram->constEnd(); ++it)
      sorted.append({it.value(), it.key()});
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QStringList top;
    for (int i = 0; i < std::min<int>(8, sorted.size()); ++i)
      top.append(QStringLiteral("%1 [%2]").arg(sorted[i].second).arg(sorted[i].first));
    report.summary += QStringLiteral(" | interior buckets: ") + top.join(QStringLiteral("; "));
    delete debugHistogram;
  }

  if (!report.passed && !failureDir.isEmpty()) {
    QDir().mkpath(failureDir);
    QImage diff(nw, nh, QImage::Format_ARGB32);
    diff.fill(Qt::black);
    for (int y = 0; y < nh; ++y)
      for (int x = 0; x < nw; ++x) {
        const QRgb np = nativeCrop.pixel(x, y), gp = goldenPx(x, y);
        const int d = std::min(255, 8 * std::max({std::abs(qRed(np) - qRed(gp)), std::abs(qGreen(np) - qGreen(gp)),
                                                   std::abs(qBlue(np) - qBlue(gp)), std::abs(qAlpha(np) - qAlpha(gp))}));
        diff.setPixel(x, y, qRgba(d, d, d, 255));
      }
    report.expectedPath = failureDir + QStringLiteral("/expected.png");
    report.actualPath = failureDir + QStringLiteral("/actual.png");
    report.diffPath = failureDir + QStringLiteral("/diff.png");
    report.maskPath = failureDir + QStringLiteral("/mask.png");
    goldenCrop.save(report.expectedPath);
    nativeCrop.save(report.actualPath);
    diff.save(report.diffPath);
    maskCrop.save(report.maskPath);
  }
  return report;
}

}  // namespace muffin::mermaid::flowscene
