#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

class QColor;
class QPainter;

namespace muffin::mermaid::math {

class MathGlyphRasterizer final {
public:
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
