#pragma once

#include <QSizeF>
#include <QString>
#include <QTextLayout>
#include <QVector>

class QColor;
class QPainter;
class QRectF;

namespace muffin::mermaid::flowchart {

inline constexpr qreal kFlowMathMlScaleX = 46.21875 / 56.796875;
inline constexpr qreal kFlowMathMlScaleY = 32.8125 / 38.864;
inline constexpr qreal kFlowMathMlTextScaleX = 39.734375 / 44.140625;

struct FlowLabelMathSpan {
  qsizetype start = 0;
  qsizetype length = 1;
  QString source;
};

struct FlowLabelDocument {
  QString text;
  QVector<QTextLayout::FormatRange> formats;
  QVector<FlowLabelMathSpan> math;
};

// Converts Mermaid's text/string/markdown label variants into a safe, native
// text model. Formatting markers and the supported inline HTML tags never reach
// QPainter as literal text and no HTML engine is involved.
FlowLabelDocument parseFlowLabel(const QString& source, const QString& labelType,
                                 bool mathEnabled = true);

QSizeF measureFlowLabel(const FlowLabelDocument& label,
                        const QString& fontFamily,
                        qreal fontPixelSize,
                        qreal lineHeight);

void paintFlowLabel(QPainter& painter, const FlowLabelDocument& label,
                    const QRectF& rect, const QString& fontFamily,
                    qreal fontPixelSize, qreal lineHeight,
                    const QColor& color, bool centerVertically);

}  // namespace muffin::mermaid::flowchart
