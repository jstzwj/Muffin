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

QImage downsampleCoverage(const QImage& source, const QSize& targetSize) {
  QImage result(targetSize, QImage::Format_ARGB32_Premultiplied);
  constexpr int sampleCount = kCoverageScale * kCoverageScale;
  for (int y = 0; y < targetSize.height(); ++y) {
    QRgb* target = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < targetSize.width(); ++x) {
      int alpha = 0;
      int red = 0;
      int green = 0;
      int blue = 0;
      for (int sampleY = 0; sampleY < kCoverageScale; ++sampleY) {
        const QRgb* sourceLine = reinterpret_cast<const QRgb*>(
            source.constScanLine(y * kCoverageScale + sampleY));
        for (int sampleX = 0; sampleX < kCoverageScale; ++sampleX) {
          const QRgb sample =
              sourceLine[x * kCoverageScale + sampleX];
          alpha += qAlpha(sample);
          red += qRed(sample);
          green += qGreen(sample);
          blue += qBlue(sample);
        }
      }
      target[x] = qRgba((red + sampleCount / 2) / sampleCount,
                        (green + sampleCount / 2) / sampleCount,
                        (blue + sampleCount / 2) / sampleCount,
                        (alpha + sampleCount / 2) / sampleCount);
    }
  }
  return result;
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

  const QImage downsampled = downsampleCoverage(coverage, deviceBounds.size());
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
    bool deterministicCoverage) {
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
            rasterOriginCorrection.x(),
        baselineOrigin.y() + positions.at(index).y() +
            rasterOriginCorrection.y());
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

bool MathGlyphRasterizer::paintStrikeRun(
    QPainter& painter, const QVector<quint32>& glyphIndexes,
    const QVector<QPointF>& positions, QPointF baselineOrigin,
    qreal fontScale, QRectF clip, const QColor& color) {
  if (glyphIndexes.isEmpty() || glyphIndexes.size() != positions.size() ||
      clip.isEmpty() || fontScale <= 0.0)
    return false;

  const QTransform toDevice = painter.combinedTransform();
  const qreal deviceScale = std::hypot(toDevice.m11(), toDevice.m12());
  if (deviceScale <= 0.0) return false;
  const muffin::math::OpenTypeMathFont& mathFont =
      muffin::math::OpenTypeMathFont::instance();
  const QRawFont strikeFont = mathFont.rasterFont(fontScale * deviceScale);
  if (!strikeFont.isValid()) return false;

  painter.save();
  painter.resetTransform();
  painter.setClipRect(toDevice.mapRect(clip), Qt::IntersectClip);
  for (qsizetype index = 0; index < glyphIndexes.size(); ++index) {
    const quint32 glyphIndex = glyphIndexes.at(index);
    const QPointF deviceBaseline = toDevice.map(
        baselineOrigin + positions.at(index));
    const QPoint deviceOrigin(qFloor(deviceBaseline.x()),
                              qFloor(deviceBaseline.y()));
    const QPointF devicePhase = deviceBaseline - QPointF(deviceOrigin);
    const QTransform strikeTransform(
        1.0, 0.0, 0.0, 1.0,
        devicePhase.x(), devicePhase.y());
    const QImage alpha = strikeFont.alphaMapForGlyph(
        glyphIndex, QRawFont::PixelAntialiasing, strikeTransform);
    if (alpha.isNull()) continue;
    QImage tinted(alpha.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(color);
    tinted.setAlphaChannel(alpha);
    const QRectF bounds = strikeTransform.mapRect(
        strikeFont.boundingRect(glyphIndex));
    const QPoint topLeft(deviceOrigin.x() + qFloor(bounds.left()),
                         deviceOrigin.y() + qCeil(bounds.top()));
    painter.drawImage(topLeft, tinted);
  }
  painter.restore();
  return true;
}

}  // namespace muffin::mermaid::math
