#pragma once

#include <QSizeF>
#include <QString>
#include <QTextLayout>
#include <QVector>

#include <memory>

class QColor;
class QPainter;
class QRectF;

namespace muffin::mermaid::flowchart {

struct FlowLabelPreparedMath;

struct FlowLabelMathSpan {
  qsizetype start = 0;
  qsizetype length = 1;
  QString source;
  std::shared_ptr<const FlowLabelPreparedMath> prepared;
};

enum class FlowLabelDomItemKind {
  AnonymousText,
  BlockText,
  Math
};

// A direct child of Mermaid's label flex container. Browser whitespace
// collapsing happens at this boundary: an anonymous item containing only
// collapsible whitespace is discarded, while whitespace inside a non-empty
// text item still contributes its normal glyph advance.
struct FlowLabelDomItem {
  qsizetype start = 0;
  qsizetype length = 0;
  FlowLabelDomItemKind kind = FlowLabelDomItemKind::AnonymousText;
};

enum class FlowLabelFormattingContext {
  FlowSvgText,
  FlowForeignObjectFlex,
  SequenceSvgText,
  SequenceForeignObjectFlex
};

enum class FlowLabelBreakBehavior {
  PreserveLines,
  CollapseIntoMathFlexLine
};

struct FlowLabelDocument {
  QString text;
  QVector<QTextLayout::FormatRange> formats;
  QVector<FlowLabelMathSpan> math;
  QVector<FlowLabelDomItem> domItems;
  FlowLabelFormattingContext formattingContext =
      FlowLabelFormattingContext::FlowSvgText;
  FlowLabelBreakBehavior breakBehavior = FlowLabelBreakBehavior::PreserveLines;
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
  qreal mathBoxHeight = 0.0;
  qreal mathBaseline = 0.0;
  qreal mathInkTop = 0.0;
  qreal mathInkBottom = 0.0;
  qreal fontAscent = 0.0;
  qreal fontDescent = 0.0;
};

struct FlowLabelLineMetrics {
  qsizetype start = 0;
  qsizetype length = 0;
  qreal width = 0.0;
  qreal height = 0.0;
  qreal blockHeight = 0.0;
  qreal baseline = 0.0;
  qreal ascent = 0.0;
  qreal descent = 0.0;
  QVector<FlowLabelVisualRun> runs;
};

struct FlowLabelLayoutMetrics {
  QSizeF size{0.0, 0.0};
  QVector<FlowLabelLineMetrics> lines;
};

struct FlowLabelFontMetrics {
  qreal ascent = 0.0;
  qreal descent = 0.0;
  qreal xHeight = 0.0;

  qreal height() const { return ascent + descent; }
  qreal middleBaseline() const { return ascent - xHeight / 2.0; }
};

// Converts Mermaid's text/string/markdown label variants into a safe, native
// text model. Formatting markers and the supported inline HTML tags never reach
// QPainter as literal text and no HTML engine is involved.
FlowLabelDocument parseFlowLabel(const QString& source, const QString& labelType,
                                 bool mathEnabled = true);

// Compiles MathML into immutable, color-independent paint operations.
// The resulting document is safe to copy into an immutable scene and reuse for
// every layout/paint pass.
qsizetype prepareFlowLabelMath(FlowLabelDocument& label,
                               qreal fontPixelSize);

FlowLabelLayoutMetrics layoutFlowLabel(const FlowLabelDocument& label,
                                       const QString& fontFamily,
                                       qreal fontPixelSize,
                                       qreal lineHeight);

QSizeF measureFlowLabel(const FlowLabelDocument& label,
                        const QString& fontFamily,
                        qreal fontPixelSize,
                        qreal lineHeight);

// SVGTextElement.getBBox() semantics: painted glyph bounds rather than the
// CSS advance box used by normal label layout.
qreal measureFlowTextInkWidth(const FlowLabelDocument& label,
                              const QString& fontFamily,
                              qreal fontPixelSize);
qreal measureFlowTextInkWidth(const FlowLabelDocument& label,
                              qsizetype start, qsizetype length,
                              const QString& fontFamily,
                              qreal fontPixelSize);

// CSS inline advance for a document range, using the same OpenType tables and
// visual glyph runs as label layout.
qreal measureFlowTextAdvanceWidth(const FlowLabelDocument& label,
                                  qsizetype start, qsizetype length,
                                  const QString& fontFamily,
                                  qreal fontPixelSize);

// Chromium Canvas fontBoundingBox metrics are the pixel-rounded OpenType
// hhea ascent/descent used to position text inside a CSS normal line box.
FlowLabelFontMetrics flowLabelFontBoundingMetrics(
    const QString& fontFamily, qreal fontPixelSize);

void paintFlowLabel(QPainter& painter, const FlowLabelDocument& label,
                    const QRectF& rect, const QString& fontFamily,
                    qreal fontPixelSize, qreal lineHeight,
                    const QColor& color, bool centerVertically);

}  // namespace muffin::mermaid::flowchart
