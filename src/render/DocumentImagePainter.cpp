#include "render/DocumentImagePainter.h"

#include <QCache>
#include <QImage>
#include <QPaintEngine>
#include <QPainter>
#include <cmath>

namespace muffin {

void paintDocumentImage(QPainter& painter, const QRectF& target, const QImage& original) {
  if (!painter.isActive() || target.isEmpty() || original.isNull())
    return;

  QImage pixels = original;
  const auto transform = painter.deviceTransform();
  bool smooth = true;
  // A PDF or recorded picture can be replayed at any resolution. Only prepare
  // device-sized pixels once the actual raster destination is known.
  if (painter.paintEngine()->type() == QPaintEngine::Raster && transform.isAffine()) {
    const qreal width = target.width() * std::hypot(transform.m11(), transform.m12());
    const qreal height = target.height() * std::hypot(transform.m21(), transform.m22());
    const QSize size(qRound(qBound<qreal>(1, width, original.width())),
                     qRound(qBound<qreal>(1, height, original.height())));
    if (size != original.size()) {
      // QImage's smooth downscaler integrates the contributing source pixels.
      // Filtering a single point in drawImage loses narrow strokes at large reductions.
      static thread_local QCache<QPair<qint64, QSize>, QImage> thumbnails(16 * 1024 * 1024);
      const auto key = qMakePair(original.cacheKey(), size);
      if (const auto* cached = thumbnails.object(key)) {
        pixels = *cached;
      } else {
        const auto scaled = original.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (!scaled.isNull()) {
          pixels = scaled;
          if (scaled.sizeInBytes() <= thumbnails.maxCost())
            thumbnails.insert(key, new QImage(scaled), scaled.sizeInBytes());
        }
      }
    }
    // For a pixel-matched raster, fractional layout origins should not blend
    // adjacent samples a second time. Rotation and enlargement still need filtering.
    smooth = transform.type() > QTransform::TxScale || std::abs(width - pixels.width()) > 0.5 ||
             std::abs(height - pixels.height()) > 0.5;
  }

  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
  painter.drawImage(target, pixels, QRectF(pixels.rect()));
  painter.restore();
}

} // namespace muffin
