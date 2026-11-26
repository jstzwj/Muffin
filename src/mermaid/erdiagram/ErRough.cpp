#include "mermaid/erdiagram/ErRough.h"

#include "mermaid/editor/MermaidRenderSupport.h"

#include <QRegularExpression>
#include <QStringList>

#include <cmath>

// What erDiagram asks of rough.js, as a shape and as the box the shape takes.
// Per IMPLEMENTATION_SPEC.md this .cpp defines its free functions with
// fully-qualified names and keeps its helpers in an anonymous namespace.

namespace er = muffin::mermaid::er;
namespace rough = muffin::mermaid::rough;

namespace {

// A double as JavaScript writes it: the fewest significant digits, of 15 to
// 17, that read back as the same number.
QString number(qreal value) {
  for (int precision = 15; precision < 17; ++precision) {
    const QString text = QString::number(value, 'g', precision);
    if (text.toDouble() == value)
      return text;
  }
  return QString::number(value, 'g', 17);
}

// A number as JavaScript's Number() reads a token of a style: what is not a
// number is not a number, which rough.js then takes as no dash at all.
qreal jsNumber(const QString& token) {
  bool ok = false;
  const qreal value = token.trimmed().toDouble(&ok);
  return ok ? value : 0.0;
}

// Where an op ends: a curve at its end point, past its two control points.
QPointF opEndPoint(const rough::Op& op) {
  const qsizetype at = op.type == rough::OpType::BcurveTo ? 4 : 0;
  return QPointF(op.data.at(at), op.data.at(at + 1));
}

constexpr qreal kPi = 3.14159265358979323846;

qreal angleDeg(const QPointF& from, const QPointF& to) {
  return std::atan2(to.y() - from.y(), to.x() - from.x()) * 180.0 / kPi;
}

} // namespace

rough::Options muffin::mermaid::er::roughOptions(const ErRoughOptions& options) {
  rough::Options result;
  result.maxRandomnessOffset = 2.0;
  result.roughness = options.solid ? 0.0 : 0.7;
  result.bowing = 1.0;
  result.stroke = options.stroke.isEmpty() ? QStringLiteral("none") : options.stroke;
  result.strokeWidth = options.strokeWidth;
  result.fill = options.fill;
  result.fillStyle = options.solid ? QStringLiteral("solid") : QStringLiteral("hachure");
  result.fillWeight = 4.0;
  result.hachureAngle = -41.0;
  result.hachureGap = 5.2;
  result.seed = options.seed;
  return result;
}

qreal muffin::mermaid::er::roughStrokeWidth(const QString& declared) {
  if (declared.trimmed().isEmpty())
    return 1.3;
  const qreal width = muffin::mermaid::editor::cssStrokeWidthPx(declared, {}, 1.3);
  return width > 0.0 ? width : 1.3;
}

QString muffin::mermaid::er::roughStrokeDasharray(const QString& declared) {
  static const QRegularExpression whitespace(QStringLiteral("\\s+"));
  const QStringList tokens = declared.trimmed().split(whitespace, Qt::SkipEmptyParts);
  if (tokens.isEmpty())
    return QStringLiteral("0 0");
  if (tokens.size() == 1)
    return number(jsNumber(tokens.first())) + QLatin1Char(' ') + number(jsNumber(tokens.first()));
  return number(jsNumber(tokens.at(0))) + QLatin1Char(' ') + number(jsNumber(tokens.at(1)));
}

QString muffin::mermaid::er::roughPathData(const rough::OpSet& set) {
  QString result;
  for (const rough::Op& op : set.ops) {
    if (op.type == rough::OpType::Move && op.data.size() >= 2)
      result +=
          QStringLiteral("M") + number(op.data.at(0)) + QLatin1Char(' ') + number(op.data.at(1)) + QLatin1Char(' ');
    else if (op.type == rough::OpType::LineTo && op.data.size() >= 2)
      result +=
          QStringLiteral("L") + number(op.data.at(0)) + QLatin1Char(' ') + number(op.data.at(1)) + QLatin1Char(' ');
    else if (op.type == rough::OpType::BcurveTo && op.data.size() >= 6)
      result += QStringLiteral("C") + number(op.data.at(0)) + QLatin1Char(' ') + number(op.data.at(1)) +
                QStringLiteral(", ") + number(op.data.at(2)) + QLatin1Char(' ') + number(op.data.at(3)) +
                QStringLiteral(", ") + number(op.data.at(4)) + QLatin1Char(' ') + number(op.data.at(5)) +
                QLatin1Char(' ');
  }
  return result.trimmed();
}

QVector<rough::PathCommand> muffin::mermaid::er::pathCommands(const QString& data) {
  // A command letter, or a number.
  static const QRegularExpression token(
      QStringLiteral("[MmLlHhVvCcZz]|[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?"));
  QStringList tokens;
  for (const auto& match : token.globalMatch(data))
    tokens.append(match.captured());
  QVector<rough::PathCommand> result;
  QPointF current, subpathStart;
  const auto numbers = [&](qsizetype& index, int count) {
    QVector<qreal> values;
    for (int i = 0; i < count && index < tokens.size(); ++i, ++index)
      values.append(tokens.at(index).toDouble());
    return values.size() == count ? values : QVector<qreal>();
  };
  qsizetype index = 0;
  QChar command;
  while (index < tokens.size()) {
    const QString& head = tokens.at(index);
    if (head.size() == 1 && head.at(0).isLetter()) {
      command = head.at(0);
      ++index;
    } else if (command.isNull()) {
      break; // a number before any command
    } else if (command == QLatin1Char('M')) {
      command = QLatin1Char('L'); // what follows a move is a line
    } else if (command == QLatin1Char('m')) {
      command = QLatin1Char('l');
    }
    const bool relative = command.isLower();
    const auto absolute = [&](qreal x, qreal y) { return relative ? current + QPointF(x, y) : QPointF(x, y); };
    if (command.toUpper() == QLatin1Char('Z')) {
      using rough::PathCommandType;
      result.append({PathCommandType::Close, subpathStart, {}, {}});
      current = subpathStart;
      continue;
    }
    const int count = command.toUpper() == QLatin1Char('C')                                              ? 6
                      : (command.toUpper() == QLatin1Char('H') || command.toUpper() == QLatin1Char('V')) ? 1
                                                                                                         : 2;
    const QVector<qreal> values = numbers(index, count);
    if (values.size() != count)
      break;
    using rough::PathCommandType;
    if (command.toUpper() == QLatin1Char('H')) {
      const QPointF point = absolute(values.at(0), relative ? 0.0 : current.y());
      result.append({PathCommandType::LineTo, point, {}, {}});
      current = point;
    } else if (command.toUpper() == QLatin1Char('V')) {
      const QPointF point = absolute(relative ? 0.0 : current.x(), values.at(0));
      result.append({PathCommandType::LineTo, point, {}, {}});
      current = point;
    } else if (command.toUpper() == QLatin1Char('C')) {
      const QPointF control1 = absolute(values.at(0), values.at(1));
      const QPointF control2 = absolute(values.at(2), values.at(3));
      const QPointF point = absolute(values.at(4), values.at(5));
      result.append({PathCommandType::CubicTo, point, control1, control2});
      current = point;
    } else {
      const QPointF point = absolute(values.at(0), values.at(1));
      const bool move = command.toUpper() == QLatin1Char('M');
      result.append({move ? PathCommandType::Move : PathCommandType::LineTo, point, {}, {}});
      current = point;
      if (move)
        subpathStart = point;
    }
  }
  return result;
}

muffin::mermaid::er::ErRoughShape muffin::mermaid::er::roughRectangle(const QRectF& rect,
                                                                      const ErRoughOptions& options) {
  ErRoughShape result;
  result.drawable = rough::rectangle(rect.x(), rect.y(), rect.width(), rect.height(), roughOptions(options));
  result.bounds = rough::tightBounds(result.drawable);
  return result;
}

muffin::mermaid::er::ErRoughShape muffin::mermaid::er::roughPolygon(const QVector<QPointF>& points,
                                                                    const ErRoughOptions& options) {
  ErRoughShape result;
  result.drawable = rough::polygon(points, roughOptions(options));
  result.bounds = rough::tightBounds(result.drawable);
  return result;
}

muffin::mermaid::er::ErRoughShape muffin::mermaid::er::roughTranslated(ErRoughShape shape, const QPointF& delta) {
  for (rough::OpSet& set : shape.drawable.sets) {
    for (rough::Op& op : set.ops) {
      for (qsizetype i = 0; i + 1 < op.data.size(); i += 2) {
        op.data[i] += delta.x();
        op.data[i + 1] += delta.y();
      }
    }
  }
  shape.bounds.translate(delta);
  return shape;
}

muffin::mermaid::er::ErRoughShape muffin::mermaid::er::roughRelationship(const QString& path, quint32 seed) {
  ErRoughShape result;
  rough::Options options;
  options.roughness = 0.3;
  options.strokeWidth = 1.0;
  options.seed = seed;
  result.drawable = rough::path(pathCommands(path), options);
  result.bounds = rough::tightBounds(result.drawable);
  return result;
}

muffin::mermaid::er::ErPathAnchor muffin::mermaid::er::roughPathStart(const ErRoughShape& shape) {
  ErPathAnchor anchor;
  if (shape.drawable.sets.isEmpty())
    return anchor;
  const QVector<rough::Op>& ops = shape.drawable.sets.last().ops;
  if (ops.isEmpty())
    return anchor;
  anchor.point = opEndPoint(ops.first());
  if (ops.size() > 1) {
    const rough::Op& next = ops.at(1);
    // A curve leaves its start towards its first control point, and where
    // that is the start itself, towards its second, and where that is too,
    // towards its end.
    QPointF towards = opEndPoint(next);
    if (next.type == rough::OpType::BcurveTo) {
      towards = QPointF(next.data.at(0), next.data.at(1));
      if (towards == anchor.point)
        towards = QPointF(next.data.at(2), next.data.at(3));
      if (towards == anchor.point)
        towards = opEndPoint(next);
    }
    anchor.angleDeg = angleDeg(anchor.point, towards);
  }
  return anchor;
}

muffin::mermaid::er::ErPathAnchor muffin::mermaid::er::roughPathEnd(const ErRoughShape& shape) {
  ErPathAnchor anchor;
  if (shape.drawable.sets.isEmpty())
    return anchor;
  const QVector<rough::Op>& ops = shape.drawable.sets.last().ops;
  if (ops.isEmpty())
    return anchor;
  const rough::Op& last = ops.last();
  anchor.point = opEndPoint(last);
  // A curve arrives at its end from its second control point, and where that
  // is the end itself, from its first, and where that is too, from the point
  // the curve started at.
  QPointF from = ops.size() > 1 ? opEndPoint(ops.at(ops.size() - 2)) : anchor.point;
  if (last.type == rough::OpType::BcurveTo) {
    from = QPointF(last.data.at(2), last.data.at(3));
    if (from == anchor.point)
      from = QPointF(last.data.at(0), last.data.at(1));
    if (from == anchor.point && ops.size() > 1)
      from = opEndPoint(ops.at(ops.size() - 2));
  }
  anchor.angleDeg = angleDeg(from, anchor.point);
  return anchor;
}
