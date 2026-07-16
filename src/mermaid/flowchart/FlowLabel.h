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
inline constexpr qreal kFlowMathMlLiteralFallbackScaleX =
    kFlowMathMlScaleX * (44.21875 / 42.4375);

struct FlowLabelMathSpan {
  qsizetype start = 0;
  qsizetype length = 1;
  QString source;
};

struct FlowLabelDocument {
  QString text;
  QVector<QTextLayout::FormatRange> formats;
  QVector<FlowLabelMathSpan> math;
  bool literalMarkdownMathFallback = false;
  bool sequenceMathMlModel = false;
  Qt::LayoutDirection direction = Qt::LayoutDirectionAuto;
};

enum class FlowLabelMathStructure {
  None,
  Plain,
  SupSub,
  Fraction,
  Radical,
  Array
};

struct FlowLabelVisualRun {
  qsizetype start = 0;
  qsizetype length = 0;
  qreal x = 0.0;
  qreal width = 0.0;
  bool rightToLeft = false;
  bool math = false;
  QString fontFamily;
  FlowLabelMathStructure mathStructure = FlowLabelMathStructure::None;
};

struct FlowLabelLineMetrics {
  qsizetype start = 0;
  qsizetype length = 0;
  qreal width = 0.0;
  qreal height = 0.0;
  qreal baseline = 0.0;
  qreal ascent = 0.0;
  qreal descent = 0.0;
  QVector<FlowLabelVisualRun> runs;
};

struct FlowLabelLayoutMetrics {
  QSizeF size{0.0, 0.0};
  QVector<FlowLabelLineMetrics> lines;
};

// Converts Mermaid's text/string/markdown label variants into a safe, native
// text model. Formatting markers and the supported inline HTML tags never reach
// QPainter as literal text and no HTML engine is involved.
FlowLabelDocument parseFlowLabel(const QString& source, const QString& labelType,
                                 bool mathEnabled = true);

FlowLabelLayoutMetrics layoutFlowLabel(const FlowLabelDocument& label,
                                       const QString& fontFamily,
                                       qreal fontPixelSize,
                                       qreal lineHeight);

QSizeF measureFlowLabel(const FlowLabelDocument& label,
                        const QString& fontFamily,
                        qreal fontPixelSize,
                        qreal lineHeight);

void paintFlowLabel(QPainter& painter, const FlowLabelDocument& label,
                    const QRectF& rect, const QString& fontFamily,
                    qreal fontPixelSize, qreal lineHeight,
                    const QColor& color, bool centerVertically);

}  // namespace muffin::mermaid::flowchart
