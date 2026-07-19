#include "mermaid/math/MathGlyphRasterizer.h"

#include "math/OpenTypeMathFont.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <cmath>

namespace {

constexpr int kCoverageScale = 4;

QRect enclosingDeviceRect(const QRectF& bounds) {
  const int left = qFloor(bounds.left());
  const int top = qFloor(bounds.top());
  const int right = qCeil(bounds.right());
  const int bottom = qCeil(bounds.bottom());
  return QRect(left, top, std::max(0, right - left),
               std::max(0, bottom - top));
}

bool paintDeterministicCoverage(QPainter& target, const QPainterPath& logicalPath,
                                const QRectF& logicalClip,
                                const QColor& color) {
  const QTransform toDevice = target.combinedTransform();
  const QPainterPath devicePath = toDevice.map(logicalPath);
  const QRectF deviceClip = toDevice.mapRect(logicalClip);
  const QRect deviceBounds = enclosingDeviceRect(
      devicePath.boundingRect().intersected(deviceClip));
  if (deviceBounds.isEmpty()) return false;

  QImage coverage(deviceBounds.width() * kCoverageScale,
                  deviceBounds.height() * kCoverageScale,
                  QImage::Format_ARGB32_Premultiplied);
  coverage.fill(Qt::transparent);
  QPainter coveragePainter(&coverage);
  coveragePainter.setRenderHint(QPainter::Antialiasing);
  coveragePainter.scale(kCoverageScale, kCoverageScale);
  coveragePainter.translate(-deviceBounds.x(), -deviceBounds.y());
  coveragePainter.setClipRect(deviceClip, Qt::IntersectClip);
  coveragePainter.setPen(Qt::NoPen);
  coveragePainter.setBrush(color);
  coveragePainter.drawPath(devicePath);
  coveragePainter.end();

  const QImage downsampled = coverage.scaled(
      deviceBounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  target.save();
  target.resetTransform();
  target.drawImage(deviceBounds.topLeft(), downsampled);
  target.restore();
  return true;
}

}  // namespace

namespace muffin::mermaid::math {

bool MathGlyphRasterizer::paintOutlineRun(
    QPainter& painter, const QVector<quint32>& glyphIndexes,
    const QVector<QPointF>& positions, QPointF baselineOrigin,
    qreal fontScale, QRectF clip, const QColor& color,
    bool deterministicCoverage, QPointF rasterPhase) {
  if (glyphIndexes.isEmpty() || glyphIndexes.size() != positions.size() ||
      clip.isEmpty() || fontScale <= 0.0)
    return false;
  const muffin::math::OpenTypeMathFont& font =
      muffin::math::OpenTypeMathFont::instance();
  QPainterPath path;
  for (qsizetype index = 0; index < glyphIndexes.size(); ++index) {
    const quint32 glyphIndex = glyphIndexes.at(index);
    const QPainterPath glyphPath = font.glyphPath(glyphIndex);
    if (glyphPath.isEmpty()) continue;
    const QRectF outlineBounds = glyphPath.boundingRect();
    const QRectF rasterBounds = font.rasterGlyphBounds(glyphIndex, fontScale);
    const QPointF rasterOriginCorrection(
        rasterBounds.left() - outlineBounds.left() * fontScale,
        rasterBounds.top() - outlineBounds.top() * fontScale);
    QTransform placement;
    placement.translate(
        baselineOrigin.x() + positions.at(index).x() +
            rasterOriginCorrection.x() + rasterPhase.x(),
        baselineOrigin.y() + positions.at(index).y() +
            rasterOriginCorrection.y() + rasterPhase.y());
    placement.scale(fontScale, fontScale);
    path.addPath(placement.map(glyphPath));
  }
  if (path.isEmpty()) return false;
  if (deterministicCoverage)
    return paintDeterministicCoverage(painter, path, clip, color);
  painter.save();
  painter.setClipRect(clip, Qt::IntersectClip);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawPath(path);
  painter.restore();
  return true;
}

}  // namespace muffin::mermaid::math
