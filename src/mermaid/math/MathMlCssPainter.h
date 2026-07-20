#pragma once

#include "mermaid/math/MathMlCssLayout.h"

#include <variant>

class QColor;
class QPainter;

namespace muffin::math {

enum class MathMlPaintLayer { All, Body, Accent };

enum class MathMlPaintPrimitiveKind {
  GlyphRun,
  VerticalGlyph,
  FractionRule,
  GlyphPath,
  SolidRule,
  Accent
};

enum class MathMlPaintPrimitiveRole {
  Row,
  FractionNumerator,
  FractionDenominator,
  FractionDelimiter,
  LeftRightBody,
  LeftRightDelimiter,
  ArrayCell,
  ArrayDelimiter,
  AccentBody,
  AccentAnnotation,
  ScriptBase,
  ScriptSuperscript,
  ScriptSubscript,
  LargeOperator,
  Fence,
  RadicalBody,
  RadicalDegree,
  Middle
};

enum class MathMlGlyphRasterMode { Native, Outline, Strike };

using MathMlPaintPrimitivePayload = std::variant<
    const MathCssGlyphRunOperation*,
    const MathCssVerticalGlyphOperation*,
    const MathCssFractionBox*,
    const MathCssGlyphPathOperation*,
    const MathCssSolidRuleOperation*,
    const MathCssAccentOperation*>;

struct MathMlPaintPrimitive {
  MathMlPaintPrimitiveKind kind = MathMlPaintPrimitiveKind::GlyphRun;
  MathMlPaintPrimitiveRole role = MathMlPaintPrimitiveRole::Row;
  QString operationPath;
  MathMlPaintPrimitivePayload payload =
      static_cast<const MathCssGlyphRunOperation*>(nullptr);
  MathMlGlyphRasterMode glyphRasterMode = MathMlGlyphRasterMode::Native;
  bool deterministicCoverage = false;
};

// Primitive payloads remain valid while the source operation tree is alive.
QVector<MathMlPaintPrimitive> collectMathMlPaintPrimitives(
    const MathCssPaintOperation& operation,
    MathMlPaintLayer layer = MathMlPaintLayer::All);

void paintMathMlPrimitives(
    QPainter& painter, const QVector<MathMlPaintPrimitive>& primitives,
    const QColor& color);

void paintMathMlVerticalGlyphOperation(
    QPainter& painter, const MathCssVerticalGlyphOperation& glyph,
    const QColor& color);

void paintMathMlOperation(
    QPainter& painter, const MathCssPaintOperation& operation,
    const QColor& color,
    MathMlPaintLayer layer = MathMlPaintLayer::All);

MathMlPaintOperationBuildResult paintMathMlOperations(
    QPainter& painter, const MathLayoutResult& layout, const QColor& color,
    qreal renderFontPixelSize,
    MathMlPaintLayer layer = MathMlPaintLayer::All);

}  // namespace muffin::math
