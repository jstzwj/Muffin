#pragma once

#include <QFont>
#include <QGlyphRun>
#include <QSizeF>
#include <QString>
#include <QTextLayout>
#include <QVector>

#include <memory>
#include <optional>

class QColor;
class QPainter;
class QRectF;

namespace muffin::mermaid::flowchart {

struct FlowLabelPreparedMath;

// Horizontal placement of a laid-out label inside its target rect. Flowchart
// labels are always Center; sequence message/note labels honor the upstream
// messageAlign/noteAlign (start/middle/end -> Left/Center/Right). Align is
// paint-only: it shifts the line origin, so it never affects wrap or metrics.
enum class FlowLabelAlign { Left, Center, Right };

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

struct FlowLabelLineRange {
  qsizetype start = 0;
  qsizetype length = 0;
};

enum class FlowLabelFormattingContext {
  FlowSvgText,
  FlowSvgFormattedText,
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
  // Optional visual lines produced by the layout/wrap stage. They reference
  // the original text so format, Math and bidi-run offsets remain immutable.
  QVector<FlowLabelLineRange> visualLines;
  qreal visualLineAdvance = 0.0;
  FlowLabelFormattingContext formattingContext =
      FlowLabelFormattingContext::FlowSvgText;
  FlowLabelBreakBehavior breakBehavior = FlowLabelBreakBehavior::PreserveLines;
  // SVG text and foreignObject content inherit CSS direction:ltr unless the
  // diagram explicitly supplies another direction.
  Qt::LayoutDirection direction = Qt::LeftToRight;
  // Base CSS font weight (Qt 6 uses the standard 100..900 scale, same as CSS:
  // Normal=400, Bold=700) applied at every QFont construction in FlowLabel —
  // ink/advance measure, wrap, layout, font bounding metrics and paint — so the
  // whole measurement chain agrees with the drawn font. Defaults to Normal, so
  // default rendering is byte-identical; sequence sets it per kind. Per-run
  // Markdown bold still overrides this via the format range.
  QFont::Weight baseWeight = QFont::Normal;
  // Base CSS font style + spacing, applied at every QFont construction in
  // FlowLabel alongside baseWeight (see makeFlowLabelFont) so the whole
  // measurement chain agrees with the drawn font. Neutral defaults keep default
  // rendering byte-identical; Requirement sets them from resolved CSS. Per-run
  // Markdown italic still overrides baseStyle via the format range.
  QFont::Style baseStyle = QFont::StyleNormal;
  qreal letterSpacingPx = 0.0;  // QFont::AbsoluteSpacing, added to every advance
  qreal wordSpacingPx = 0.0;    // added to every space advance
  // CSS text-decoration. PAINT-ONLY: drawGlyphRun does not reliably paint
  // these, so paintFlowLabel draws them explicitly over each visual line. They
  // never feed makeFlowLabelFont, so measure/wrap/layout are unaffected; default
  // false produces no extra painter operations (default rendering unchanged).
  bool underline = false;
  bool strikeOut = false;
  bool overline = false;
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
  // Full-line shaping payload, including the synthetic style face selected by
  // QTextLayout. Positions are local to x and retain the shaped baseline, so
  // painters must not reshape the source slice.
  QGlyphRun preparedGlyphs;
  qreal preparedGlyphWidth = 0.0;
  int fontWeight = QFont::Normal;
  bool fontItalic = false;
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

// Mermaid's htmlLabels:false path uses createFormattedText(): Markdown markers
// are formatting, <br> creates a line, and other HTML tags remain visible.
FlowLabelDocument parseFlowSvgLabel(const QString& source,
                                    const QString& labelType);

// Compiles MathML into immutable, color-independent paint operations.
// The resulting document is safe to copy into an immutable scene and reuse for
// every layout/paint pass.
qsizetype prepareFlowLabelMath(FlowLabelDocument& label,
                               qreal fontPixelSize);

FlowLabelLayoutMetrics layoutFlowLabel(const FlowLabelDocument& label,
                                       const QString& fontFamily,
                                       qreal fontPixelSize,
                                       qreal lineHeight);

FlowLabelDocument wrapFlowLabel(const FlowLabelDocument& label,
                                const QString& fontFamily,
                                qreal fontPixelSize,
                                qreal maximumLineWidth);

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

// Chromium SVGTextElement.getBBox() for createFormattedText(): the first
// tspan baseline is 1em and following lines advance by 1.1em.
QRectF measureFlowSvgTextBounds(const FlowLabelDocument& label,
                                const QString& fontFamily,
                                qreal fontPixelSize);

// CSS inline advance for a document range, using the same OpenType tables and
// visual glyph runs as label layout.
qreal measureFlowTextAdvanceWidth(const FlowLabelDocument& label,
                                  qsizetype start, qsizetype length,
                                  const QString& fontFamily,
                                  qreal fontPixelSize);

// Shapes directly in OpenType design units and scales only after GPOS. This
// matches Chromium/HarfBuzz CSS advances; QGlyphRun positions have already
// been rounded by DirectWrite and can differ by one LayoutUnit (1/64 px).
std::optional<qreal> measureOpenTypeDesignAdvance(
    const FlowLabelDocument& label, qsizetype start, qsizetype length,
    const QString& fontFamily, qreal fontPixelSize);
std::optional<qreal> measureOpenTypeDesignAdvance(
    const FlowLabelDocument& label, const QString& fontFamily,
    qreal fontPixelSize);

// Chromium's post-layout SVGTextElement.getBBox() for one unformatted line.
// It combines Blink's 1/64px ShapeResult cell with Skia's leading/trailing
// antialias cells; DirectWrite/QFontMetrics alone loses both edge cases.
QRectF measureChromiumSvgTextBounds(
    const FlowLabelDocument& label, const QString& fontFamily,
    qreal fontPixelSize, QFont::Weight weight = QFont::Normal,
    qreal deviceScale = 1.0);
QRectF measureChromiumSvgTextLayoutBounds(
    const FlowLabelDocument& label, const QString& fontFamily,
    qreal fontPixelSize, qreal deviceScale = 1.0);

// The font used for every text metric/advance/paint in FlowLabel: the
// MermaidFontRegistry family stack, pixel-rounded size, PreferNoHinting, the
// base weight and style, and letter/word spacing — all applied at once. Exposed
// so other scene builders resolve font-relative CSS units (ex/ch) against the
// EXACT font the text is measured/painted with — never construct a QFont ad hoc
// for those metrics or xHeight / '0'-advance will drift.
QFont makeFlowLabelFont(const QString& fontFamily, qreal fontPixelSize,
                        QFont::Weight weight = QFont::Normal,
                        QFont::Style style = QFont::StyleNormal,
                        qreal letterSpacingPx = 0.0,
                        qreal wordSpacingPx = 0.0);

// Chromium Canvas fontBoundingBox metrics are the pixel-rounded OpenType
// hhea ascent/descent used to position text inside a CSS normal line box. Only
// the vertical-metric-affecting fields (weight, style) are taken; letter/word
// spacing are horizontal and inert here.
FlowLabelFontMetrics flowLabelFontBoundingMetrics(
    const QString& fontFamily, qreal fontPixelSize,
    QFont::Weight weight = QFont::Normal,
    QFont::Style style = QFont::StyleNormal);

// Mermaid createFormattedText() advances SVG tspans by 1.1em. Its background
// rect is the union of the font cell boxes plus two CSS pixels per side.
qreal flowSvgFormattedTextLineStep(qreal fontPixelSize);
qreal flowSvgFormattedTextBlockHeight(const QString& fontFamily,
                                      qreal fontPixelSize,
                                      qsizetype lineCount,
                                      qreal padding = 2.0);

void paintFlowLabel(QPainter& painter, const FlowLabelDocument& label,
                    const QRectF& rect, const QString& fontFamily,
                    qreal fontPixelSize, qreal lineHeight,
                    const QColor& color, bool centerVertically,
                    FlowLabelAlign align = FlowLabelAlign::Center,
                    qreal alignMargin = 0.0);

}  // namespace muffin::mermaid::flowchart
