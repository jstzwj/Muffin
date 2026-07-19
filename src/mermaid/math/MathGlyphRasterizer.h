#pragma once

#include <QPointF>
#include <QPainterPath>
#include <QRectF>
#include <QVector>

class QColor;
class QPainter;

namespace muffin::mermaid::math {

class MathGlyphRasterizer final {
public:
  static bool paintPath(QPainter& painter, const QPainterPath& path,
                        QRectF clip, const QColor& color);
  static bool paintStrikeGlyphFitBlock(QPainter& painter,
                                       quint32 glyphIndex, QRectF inkBounds,
                                       qreal fontScale, QRectF target,
                                       bool allowBlockOverflow,
                                       const QColor& color);
  static bool paintOutlineRun(
      QPainter& painter, const QVector<quint32>& glyphIndexes,
      const QVector<QPointF>& positions, QPointF baselineOrigin,
      qreal fontScale, QRectF clip, const QColor& color,
      bool deterministicCoverage = false);
  static bool paintStrikeRun(
      QPainter& painter, const QVector<quint32>& glyphIndexes,
      const QVector<QPointF>& positions, QPointF baselineOrigin,
      qreal fontScale, QRectF clip, const QColor& color);
};

}  // namespace muffin::mermaid::math
