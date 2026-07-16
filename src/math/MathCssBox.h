#pragma once

#include "math/MathRenderNode.h"

#include <QString>
#include <QVector>

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

// Models Chromium's native MathML CSS boxes produced by Mermaid/KaTeX. The
// render tree is built at renderFontPixelSize while native MathML has its own
// fixed CSS root size (16px in Mermaid 11.16.0 sequence labels).
MathCssBox layoutMathMlCssBox(const MathLayoutResult& layout,
                              qreal renderFontPixelSize,
                              qreal cssRootFontPixelSize = 16.0);

}  // namespace muffin::math
