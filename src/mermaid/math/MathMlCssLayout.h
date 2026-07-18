#pragma once

#include "math/MathRenderNode.h"

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QRawFont>

#include <optional>
#include <stdexcept>
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

struct MathCssGlyphRunOperation {
  QString text;
  QString fontFamily;
  QString fontClass;
  QString atomClass;
  QRawFont rawFont;
  QVector<quint32> glyphIndexes;
  QVector<QPointF> positions;
  QPointF baselineOrigin;
  qreal advance = 0.0;
  qreal fontScale = 1.0;
  QRectF inkBounds;
  QRectF clip;
};

struct MathCssGlyphRunGroupOperation {
  QRectF container;
  QVector<MathCssGlyphRunOperation> runs;
  qreal lineAscent = 0.0;
};

struct MathCssRowOperation {
  QRectF container;
  const MathRenderNode* node = nullptr;
  QVector<MathCssGlyphRunOperation> glyphRuns;
  qreal lineAscent = 0.0;
};

enum class MathCssVerticalGlyphKind {
  FixedVariant,
  Assembly
};

enum class MathCssVerticalScalePolicy {
  PreserveVariantScale,
  FitTargetExtent
};

struct MathCssVerticalGlyphPart {
  quint32 glyphIndex = 0;
  QPointF position;
  qreal offset = 0.0;
  qreal fullAdvance = 0.0;
  qreal connectorOverlap = 0.0;
  bool extender = false;
};

struct MathCssVerticalGlyphOperation {
  MathCssVerticalGlyphKind kind = MathCssVerticalGlyphKind::FixedVariant;
  MathCssVerticalScalePolicy scalePolicy =
      MathCssVerticalScalePolicy::PreserveVariantScale;
  QRectF target;
  QPointF baselineOrigin;
  qreal selectionTarget = 0.0;
  qreal realizedExtent = 0.0;
  qreal advance = 0.0;
  qreal italicCorrection = 0.0;
  QRectF inkBounds;
  quint32 fixedGlyphIndex = 0;
  QVector<MathCssVerticalGlyphPart> parts;
};

struct MathCssFencePair {
  QRectF left;
  QRectF right;
  QString leftCharacter;
  QString rightCharacter;
  std::optional<MathCssVerticalGlyphOperation> leftGlyph;
  std::optional<MathCssVerticalGlyphOperation> rightGlyph;
};

struct MathCssScriptOperation {
  MathScriptKind kind = MathScriptKind::None;
  bool limits = false;
  qreal lineAscent = 0.0;
  qreal lineDescent = 0.0;
  QRectF container;
  QRectF base;
  QRectF superscript;
  QRectF subscript;
  const MathRenderNode* baseNode = nullptr;
  const MathRenderNode* superscriptNode = nullptr;
  const MathRenderNode* subscriptNode = nullptr;
  QVector<MathCssGlyphRunOperation> baseGlyphRuns;
  QVector<MathCssGlyphRunOperation> superscriptGlyphRuns;
  QVector<MathCssGlyphRunOperation> subscriptGlyphRuns;
  std::optional<MathCssVerticalGlyphOperation> largeOperatorGlyph;
  std::optional<MathCssFencePair> fences;
};

struct MathCssRadicalOperation {
  qreal lineAscent = 0.0;
  qreal lineDescent = 0.0;
  QRectF container;
  QRectF glyph;
  QRectF rule;
  QRectF body;
  const MathRenderNode* bodyNode = nullptr;
  MathCssGlyphRunOperation glyphRun;
  QVector<MathCssGlyphRunOperation> bodyGlyphRuns;
  std::optional<MathCssFencePair> fences;
};

struct MathCssFractionPaint {
  MathCssFractionBox box;
  qreal lineAscent = 0.0;
  const MathRenderNode* numeratorNode = nullptr;
  const MathRenderNode* denominatorNode = nullptr;
  QVector<MathCssGlyphRunOperation> numeratorGlyphRuns;
  QVector<MathCssGlyphRunOperation> denominatorGlyphRuns;
  std::optional<MathCssVerticalGlyphOperation> leftDelimiterGlyph;
  std::optional<MathCssVerticalGlyphOperation> rightDelimiterGlyph;
};

struct MathCssArrayCell {
  int row = 0;
  int column = 0;
  QRectF box;
  QRectF content;
  qreal baseline = 0.0;
  const MathRenderNode* contentNode = nullptr;
  QVector<MathCssGlyphRunOperation> glyphRuns;
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
  std::optional<MathCssVerticalGlyphOperation> leftDelimiterGlyph;
  std::optional<MathCssVerticalGlyphOperation> rightDelimiterGlyph;
};

struct MathCssLeftRightBodyRegion {
  QRectF box;
  const MathRenderNode* node = nullptr;
  QVector<MathCssGlyphRunOperation> glyphRuns;
};

struct MathCssMiddleDelimiterOperation {
  QString character;
  QRectF box;
  std::optional<MathCssVerticalGlyphOperation> glyph;
};

struct MathCssMiddlePaintOperation {
  QString character;
  QRectF container;
  QRectF allocation;
  MathCssGlyphRunOperation glyphRun;
  qreal lineAscent = 0.0;
};

struct MathCssLeftRightOperation {
  QRectF container;
  QRectF body;
  QVector<MathCssLeftRightBodyRegion> bodyRegions;
  QRectF leftDelimiter;
  QRectF rightDelimiter;
  QString leftDelimiterCharacter;
  QString rightDelimiterCharacter;
  std::optional<MathCssVerticalGlyphOperation> leftDelimiterGlyph;
  std::optional<MathCssVerticalGlyphOperation> rightDelimiterGlyph;
  QVector<MathCssMiddleDelimiterOperation> middleDelimiters;
  qreal lineAscent = 0.0;
};

enum class MathCssHorizontalGlyphKind {
  FixedVariant,
  Assembly,
  ShapedText
};

enum class MathCssHorizontalScalePolicy {
  StretchToTarget,
  PreserveVariantScale
};

struct MathCssHorizontalGlyphPart {
  quint32 glyphIndex = 0;
  qreal offset = 0.0;
  qreal fullAdvance = 0.0;
  qreal connectorOverlap = 0.0;
  bool extender = false;
};

struct MathCssHorizontalGlyphOperation {
  MathCssHorizontalGlyphKind kind = MathCssHorizontalGlyphKind::FixedVariant;
  MathCssHorizontalScalePolicy scalePolicy =
      MathCssHorizontalScalePolicy::StretchToTarget;
  QRectF target;
  qreal selectionTarget = 0.0;
  qreal fontScale = 1.0;
  qreal realizedExtent = 0.0;
  qreal italicCorrection = 0.0;
  QPointF paintOffset;
  QRectF inkBounds;
  quint32 fixedGlyphIndex = 0;
  QVector<MathCssHorizontalGlyphPart> parts;
  QString text;
  QVector<quint32> textGlyphIndexes;
  QVector<QPointF> textGlyphPositions;
};

struct MathCssAccentOperation {
  MathAccentKind accentKind = MathAccentKind::None;
  MathCssAccentBox box;
  QRectF container;
  QRectF annotationContent;
  const MathRenderNode* bodyNode = nullptr;
  const MathRenderNode* annotationNode = nullptr;
  QVector<MathCssGlyphRunOperation> bodyGlyphRuns;
  QVector<MathCssGlyphRunOperation> annotationGlyphRuns;
  MathCssHorizontalGlyphOperation glyph;
  qreal lineAscent = 0.0;
};

enum class MathCssPaintKind {
  GlyphRun,
  Row,
  LeftRight,
  MiddleDelimiter,
  Fraction,
  Radical,
  SupSub,
  Array,
  Accent
};

using MathCssPaintPayload = std::variant<MathCssGlyphRunGroupOperation,
                                         MathCssRowOperation,
                                         MathCssLeftRightOperation,
                                         MathCssMiddlePaintOperation,
                                         MathCssFractionPaint,
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
  QJsonObject toJson() const;
};

enum class MathMlPaintFailureCode {
  InvalidLayout,
  InvalidFontSize,
  RootGlyphRunsUnavailable,
  ChildOperationUnavailable,
  AlignedChildOperationUnavailable,
  AccentOperationUnavailable,
  UnsupportedRootOperation,
  FractionOperationUnavailable,
  RootOperationUnavailable
};

struct MathMlPaintFailure {
  MathMlPaintFailureCode code = MathMlPaintFailureCode::InvalidLayout;
  MathRenderKind renderKind = MathRenderKind::Error;
  MathSemanticKind semanticKind = MathSemanticKind::None;
  QString nodePath;
  QString expectedMathMlTag;

  QJsonObject toJson() const;
};

QString mathMlPaintFailureCodeName(MathMlPaintFailureCode code);
QString formatMathMlPaintFailure(const MathMlPaintFailure& failure);

class MathMlPaintError final : public std::runtime_error {
public:
  explicit MathMlPaintError(MathMlPaintFailure failure);

  const MathMlPaintFailure& failure() const noexcept { return failure_; }

private:
  MathMlPaintFailure failure_;
};

struct MathMlPaintOperationBuildResult {
  std::optional<MathCssPaintOperation> operation;
  std::optional<MathMlPaintFailure> failure;

  bool succeeded() const { return operation.has_value(); }
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
MathMlPaintOperationBuildResult buildMathMlPaintOperations(
    const MathLayoutResult& layout, qreal renderFontPixelSize,
    qreal cssRootFontPixelSize = 16.0);

}  // namespace muffin::math
