#pragma once

#include <QColor>
#include <QFont>
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
  Error
};

struct MathRenderNode {
  MathRenderKind kind = MathRenderKind::Span;
  QString text;
  QString fontClass;
  QString pathName;
  QString svgPath;
  QString imageSource;
  QRectF viewBox;
  QFont font;
  QColor color = Qt::black;
  qreal width = 0.0;
  qreal height = 0.0;
  qreal depth = 0.0;
  qreal shift = 0.0;
  qreal ruleThickness = 0.0;
  qreal italic = 0.0;
  qreal xOffset = 0.0;
  qreal yOffset = 0.0;
  int columns = 0;
  int rows = 0;
  std::vector<std::unique_ptr<MathRenderNode>> children;

  qreal totalHeight() const;
  QRectF boundsAt(QPointF origin) const;
  void paint(QPainter& painter, QPointF origin) const;
};

struct MathLayoutResult {
  std::unique_ptr<MathRenderNode> root;
  QSizeF size;
  qreal baseline = 0.0;
  QString source;
  QString error;

  bool valid() const;
  void paint(QPainter& painter, QPointF origin) const;
};

std::unique_ptr<MathRenderNode> cloneNode(const MathRenderNode& node);

}  // namespace muffin::math
