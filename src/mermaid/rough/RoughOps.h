#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::rough {

enum class OpType { Move, LineTo, BcurveTo };
enum class OpSetType { Path, FillPath, FillSketch };

struct Op {
  OpType type = OpType::Move;
  QVector<qreal> data;
};

struct OpSet {
  OpSetType type = OpSetType::Path;
  QVector<Op> ops;
};

struct Options {
  qreal maxRandomnessOffset = 2.0;
  qreal roughness = 1.0;
  qreal bowing = 1.0;
  QString stroke = QStringLiteral("#000");
  qreal strokeWidth = 1.0;
  QString fill;
  QString fillStyle = QStringLiteral("hachure");
  qreal fillWeight = -1.0;
  qreal hachureAngle = -41.0;
  qreal hachureGap = -1.0;
  qreal curveTightness = 0.0;
  qreal curveFitting = 0.95;
  int curveStepCount = 9;
  quint32 seed = 0;
  bool disableMultiStroke = false;
  bool disableMultiStrokeFill = false;
  bool preserveVertices = false;
  qreal fillShapeRoughnessGain = 0.8;
};

struct Drawable {
  QString shape;
  QVector<OpSet> sets;
  Options options;
};

Drawable line(qreal x1, qreal y1, qreal x2, qreal y2, Options options = {});
Drawable rectangle(qreal x, qreal y, qreal width, qreal height, Options options = {});
Drawable polygon(const QVector<QPointF>& points, Options options = {});
Drawable ellipse(qreal x, qreal y, qreal width, qreal height, Options options = {});
Drawable arc(qreal x, qreal y, qreal width, qreal height,
             qreal start, qreal stop, bool closed, Options options = {});
Drawable path(const QPainterPath& source, Options options = {},
              bool closed = false);

QPainterPath toPainterPath(const OpSet& set);
QRectF tightBounds(const OpSet& set);
QRectF tightBounds(const Drawable& drawable);
QString opTypeName(OpType type);
QString opSetTypeName(OpSetType type);

}  // namespace muffin::mermaid::rough
