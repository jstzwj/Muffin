#pragma once

#include "math/MathRenderNode.h"

#include <QString>
#include <QVector>

#include <optional>

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

struct MathCssFractionOperation {
  MathCssFractionBox box;
  qreal lineAscent = 0.0;
  const MathRenderNode* numeratorNode = nullptr;
  const MathRenderNode* denominatorNode = nullptr;
  bool nested = false;
  QVector<MathCssFractionOperation> children;
  QVector<MathCssScriptOperation> scripts;
  QVector<MathCssRadicalOperation> radicals;
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
std::optional<MathCssFractionOperation> layoutMathMlFractionOperations(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize = 16.0);

}  // namespace muffin::math
