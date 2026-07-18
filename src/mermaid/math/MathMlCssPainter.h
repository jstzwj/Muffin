#pragma once

#include "mermaid/math/MathMlCssLayout.h"

class QColor;
class QPainter;

namespace muffin::math {

enum class MathMlPaintLayer { All, Body, Accent };

void paintMathMlOperation(
    QPainter& painter, const MathCssPaintOperation& operation,
    const QColor& color,
    MathMlPaintLayer layer = MathMlPaintLayer::All);

MathMlPaintOperationBuildResult paintMathMlOperations(
    QPainter& painter, const MathLayoutResult& layout, const QColor& color,
    qreal renderFontPixelSize,
    MathMlPaintLayer layer = MathMlPaintLayer::All);

}  // namespace muffin::math
