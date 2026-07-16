#pragma once

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

class QPainter;

namespace muffin::math {

enum class MathRenderKind {
  Span,
  Symbol,
  Rule,
  Rect,
  Sqrt,
  SupSub,
  Fraction,
  Accent,
  Phantom,
  Stretchy,
  LeftRight,
  Array,
  VList,
  Error
};

enum class MathSemanticKind {
  None,
  Fraction,
  Radical,
  SupSub,
  Array
};

enum class MathScriptKind {
  None,
  Superscript,
  Subscript,
  SubSup
};

enum class MathOperatorKind {
  None,
  Named,
  Symbol,
  Limits
};

enum class MathAccentKind {
  None,
  Over,
  Under,
  Overline,
  Underline,
  OverBrace,
  UnderBrace
};

struct MathRenderNode {
  MathRenderKind kind = MathRenderKind::Span;
  MathSemanticKind semanticKind = MathSemanticKind::None;
  MathScriptKind scriptKind = MathScriptKind::None;
  MathOperatorKind operatorKind = MathOperatorKind::None;
  MathAccentKind accentKind = MathAccentKind::None;
  bool radicalIndex = false;
  bool fractionHasBarLine = false;
  QString text;
  QString operatorText;
  QString accentLabel;
  QString leftDelimiter;
  QString rightDelimiter;
  QString atomClass;
  QString fontClass;
  QString pathName;
  QString svgPath;
  QString imageSource;
  QRectF viewBox;
  QFont font;
  QColor color = Qt::black;
  qreal width = 0.0;
  // Some KaTeX SVG nodes advance by one delimiter width while painting across
  // their complete containing expression (notably square-root vincula).
  qreal paintWidth = 0.0;
  qreal height = 0.0;
  qreal depth = 0.0;
  qreal shift = 0.0;
  qreal ruleThickness = 0.0;
  qreal italic = 0.0;
  qreal italicMarginRight = 0.0;
  qreal xOffset = 0.0;
  qreal yOffset = 0.0;
  bool allowBreak = false;
  bool tightSpacing = false;
  bool phantom = false;
  int mathStyleSize = 1;
  int columns = 0;
  int rows = 0;
  QString arrayEnvironment;
  QString arrayColumnSeparation;
  QString arrayLeftDelimiter;
  QString arrayRightDelimiter;
  qreal arrayStretch = 1.0;
  std::vector<qreal> arrayColumnWidths;
  std::vector<qreal> arrayRowHeights;
  std::vector<qreal> arrayRowDepths;
  std::vector<bool> arrayRowInkDescenders;
  std::vector<std::unique_ptr<MathRenderNode>> children;

  qreal totalHeight() const;
  QRectF boundsAt(QPointF origin) const;
  void paint(QPainter& painter, QPointF origin) const;
  QJsonObject toJson() const;
  QString toJsonString() const;
};

struct MathLayoutResult {
  std::unique_ptr<MathRenderNode> root;
  QSizeF size;
  QSizeF naturalSize;
  qreal baseline = 0.0;
  QString source;
  QString error;
  bool overflow = false;

  bool valid() const;
  void paint(QPainter& painter, QPointF origin) const;
};

std::unique_ptr<MathRenderNode> cloneNode(const MathRenderNode& node);

}  // namespace muffin::math
