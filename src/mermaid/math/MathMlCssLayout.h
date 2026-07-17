#pragma once

#include "math/MathRenderNode.h"

#include <QString>
#include <QVector>

#include <optional>
#include <variant>

namespace muffin::math {

enum class MathCssBoxRole {
  Root,
  Row,
  Glyph,
  Fraction,
  Radical,
  SupSub,
  Array
};

struct MathCssBox {
  MathCssBoxRole role = MathCssBoxRole::Row;
  MathSemanticKind semanticKind = MathSemanticKind::None;
  MathScriptKind scriptKind = MathScriptKind::None;
  bool radicalIndex = false;
  QString text;
  qreal x = 0.0;
  qreal y = 0.0;
  qreal width = 0.0;
  qreal height = 0.0;
  qreal advance = 0.0;
  qreal baseline = 0.0;
  qreal inkTop = 0.0;
  qreal inkBottom = 0.0;
  QVector<MathCssBox> children;
};

struct MathCssAccentBox {
  bool over = false;
  QString character;
  qreal fontScale = 1.0;
  QRectF body;
  QRectF accent;
  QRectF annotation;
};

struct MathCssFractionBox {
  bool hasRule = false;
  int styleSize = 1;
  qreal fontScale = 1.0;
  QRectF container;
  QRectF fraction;
  QRectF numerator;
  QRectF rule;
  QRectF denominator;
  QRectF leftDelimiter;
  QRectF rightDelimiter;
  QString leftDelimiterCharacter;
  QString rightDelimiterCharacter;
};

struct MathCssScriptOperation {
  MathScriptKind kind = MathScriptKind::None;
  qreal lineAscent = 0.0;
  qreal lineDescent = 0.0;
  QRectF container;
  QRectF base;
  QRectF superscript;
  QRectF subscript;
  const MathRenderNode* baseNode = nullptr;
  const MathRenderNode* superscriptNode = nullptr;
  const MathRenderNode* subscriptNode = nullptr;
};

struct MathCssRadicalOperation {
  qreal lineAscent = 0.0;
  qreal lineDescent = 0.0;
  QRectF container;
  QRectF glyph;
  QRectF rule;
  QRectF body;
  const MathRenderNode* bodyNode = nullptr;
  quint32 glyphIndex = 0;
};

struct MathCssFractionPaint {
  MathCssFractionBox box;
  qreal lineAscent = 0.0;
  const MathRenderNode* numeratorNode = nullptr;
  const MathRenderNode* denominatorNode = nullptr;
  bool nested = false;
};

struct MathCssArrayCell {
  int row = 0;
  int column = 0;
  QRectF box;
  QRectF content;
  const MathRenderNode* contentNode = nullptr;
};

struct MathCssArrayOperation {
  QRectF container;
  QRectF table;
  QVector<QRectF> rows;
  QVector<MathCssArrayCell> cells;
  QRectF leftDelimiter;
  QRectF rightDelimiter;
  QString leftDelimiterCharacter;
  QString rightDelimiterCharacter;
  qreal lineAscent = 0.0;
};

struct MathCssAccentOperation {
  MathCssAccentBox box;
  QRectF container;
  QRectF annotationContent;
  const MathRenderNode* bodyNode = nullptr;
  const MathRenderNode* annotationNode = nullptr;
  QPointF bodySourceOrigin;
  QPointF annotationSourceOrigin;
  bool hasBodySourceOrigin = false;
  bool hasAnnotationSourceOrigin = false;
  bool bodyUsesLayoutScale = false;
  bool fixedVariantUsesNaturalScale = false;
  qreal fixedVariantTargetWidth = 0.0;
  qreal lineAscent = 0.0;
};

enum class MathCssPaintKind {
  Fraction,
  Radical,
  SupSub,
  Array,
  Accent
};

using MathCssPaintPayload = std::variant<MathCssFractionPaint,
                                         MathCssScriptOperation,
                                         MathCssRadicalOperation,
                                         MathCssArrayOperation,
                                         MathCssAccentOperation>;

struct MathCssPaintOperation {
  MathCssPaintPayload payload;
  QVector<MathCssPaintOperation> children;

  MathCssPaintKind kind() const;
  MathSemanticKind semanticKind() const;
  QRectF container() const;
  qreal lineAscent() const;
  qreal alignmentBaseline() const;
};

// Models Chromium's native MathML CSS boxes produced by Mermaid/KaTeX. The
// render tree is built at renderFontPixelSize while native MathML has its own
// fixed CSS root size (16px in Mermaid 11.16.0 sequence labels).
MathCssBox layoutMathMlCssBox(const MathLayoutResult& layout,
                              qreal renderFontPixelSize,
                              qreal cssRootFontPixelSize = 16.0);
std::optional<MathCssAccentBox> layoutMathMlAccentBox(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize = 16.0);
std::optional<MathCssFractionBox> layoutMathMlFractionBox(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize = 16.0);
std::optional<MathCssPaintOperation> layoutMathMlPaintOperations(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize = 16.0);

}  // namespace muffin::math
