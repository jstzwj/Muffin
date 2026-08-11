#include "mermaid/scene/SvgPathParse.h"

#include <QPainterPath>
#include <QPointF>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::scene {
namespace {

void appendArc(QPainterPath& path, const QPointF& start, const QPointF& end,
               qreal rawRx, qreal rawRy, qreal rotationDegrees,
               bool largeArc, bool sweep) {
  qreal rx = std::abs(rawRx);
  qreal ry = std::abs(rawRy);
  if (rx == 0.0 || ry == 0.0) {
    path.lineTo(end);
    return;
  }
  if (start == end) return;

  const qreal phi = qDegreesToRadians(std::fmod(rotationDegrees, 360.0));
  const qreal cosPhi = std::cos(phi);
  const qreal sinPhi = std::sin(phi);
  const qreal halfDx = (start.x() - end.x()) / 2.0;
  const qreal halfDy = (start.y() - end.y()) / 2.0;
  const qreal xPrime = cosPhi * halfDx + sinPhi * halfDy;
  const qreal yPrime = -sinPhi * halfDx + cosPhi * halfDy;
  qreal radiiScale = xPrime * xPrime / (rx * rx) +
                     yPrime * yPrime / (ry * ry);
  if (radiiScale > 1.0) {
    radiiScale = std::sqrt(radiiScale);
    rx *= radiiScale;
    ry *= radiiScale;
  }

  const qreal rx2 = rx * rx;
  const qreal ry2 = ry * ry;
  const qreal xp2 = xPrime * xPrime;
  const qreal yp2 = yPrime * yPrime;
  const qreal denominator = rx2 * yp2 + ry2 * xp2;
  const qreal numerator = std::max(0.0, rx2 * ry2 - denominator);
  const qreal sign = largeArc == sweep ? -1.0 : 1.0;
  const qreal coefficient = denominator == 0.0
                                ? 0.0
                                : sign * std::sqrt(numerator / denominator);
  const qreal cxPrime = coefficient * rx * yPrime / ry;
  const qreal cyPrime = coefficient * -ry * xPrime / rx;
  const QPointF center(
      cosPhi * cxPrime - sinPhi * cyPrime + (start.x() + end.x()) / 2.0,
      sinPhi * cxPrime + cosPhi * cyPrime + (start.y() + end.y()) / 2.0);

  const auto angle = [](qreal ux, qreal uy, qreal vx, qreal vy) {
    const qreal dot = ux * vx + uy * vy;
    const qreal determinant = ux * vy - uy * vx;
    return std::atan2(determinant, dot);
  };
  const qreal ux = (xPrime - cxPrime) / rx;
  const qreal uy = (yPrime - cyPrime) / ry;
  const qreal vx = (-xPrime - cxPrime) / rx;
  const qreal vy = (-yPrime - cyPrime) / ry;
  qreal theta = angle(1.0, 0.0, ux, uy);
  qreal delta = angle(ux, uy, vx, vy);
  if (!sweep && delta > 0.0) delta -= 2.0 * M_PI;
  if (sweep && delta < 0.0) delta += 2.0 * M_PI;
  const int segments = std::max(1, int(std::ceil(std::abs(delta) / (M_PI / 2.0))));
  const qreal step = delta / qreal(segments);
  const auto pointAt = [&](qreal value) {
    return QPointF(center.x() + cosPhi * rx * std::cos(value) -
                       sinPhi * ry * std::sin(value),
                   center.y() + sinPhi * rx * std::cos(value) +
                       cosPhi * ry * std::sin(value));
  };
  const auto derivativeAt = [&](qreal value) {
    return QPointF(-cosPhi * rx * std::sin(value) -
                       sinPhi * ry * std::cos(value),
                   -sinPhi * rx * std::sin(value) +
                       cosPhi * ry * std::cos(value));
  };
  for (int segment = 0; segment < segments; ++segment) {
    const qreal next = theta + step;
    const qreal alpha = 4.0 / 3.0 * std::tan(step / 4.0);
    const QPointF first = pointAt(theta) + alpha * derivativeAt(theta);
    const QPointF second = pointAt(next) - alpha * derivativeAt(next);
    const QPointF finish = segment + 1 == segments ? end : pointAt(next);
    path.cubicTo(first, second, finish);
    theta = next;
  }
}

}  // namespace

// Verbatim lift of ErScenePainter's painterPath (the superset of the former
// class/state copies). The explicit x-then-y `point` keeps number() call order
// deterministic (the state copy relied on unspecified argument-evaluation
// order via QPointF(number(), number())).
QPainterPath parseSvgPath(const QString& source) {
  static const QRegularExpression token(
      QStringLiteral("[A-Za-z]|[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"));
  QStringList tokens;
  auto matches = token.globalMatch(source);
  while (matches.hasNext()) tokens.append(matches.next().captured());
  QPainterPath path;
  qsizetype i = 0;
  QChar command;
  QPointF current;
  QPointF subpathStart;
  const auto isCommand = [](const QString& value) {
    return value.size() == 1 && value.front().isLetter();
  };
  const auto number = [&]() { return tokens.value(i++).toDouble(); };
  const auto point = [&](bool relative) {
    const qreal x = number();
    const qreal y = number();
    QPointF value(x, y);
    if (relative) value += current;
    return value;
  };
  while (i < tokens.size()) {
    if (isCommand(tokens.at(i))) command = tokens.at(i++).front();
    if (command.isNull()) break;
    const bool relative = command.isLower();
    const QChar upper = command.toUpper();
    if (upper == QLatin1Char('Z')) {
      path.closeSubpath();
      current = subpathStart;
      command = {};
    } else if (upper == QLatin1Char('M')) {
      current = point(relative);
      path.moveTo(current);
      subpathStart = current;
      command = relative ? QLatin1Char('l') : QLatin1Char('L');
    } else if (upper == QLatin1Char('L')) {
      current = point(relative);
      path.lineTo(current);
    } else if (upper == QLatin1Char('H')) {
      current.setX(number() + (relative ? current.x() : 0.0));
      path.lineTo(current);
    } else if (upper == QLatin1Char('V')) {
      current.setY(number() + (relative ? current.y() : 0.0));
      path.lineTo(current);
    } else if (upper == QLatin1Char('C')) {
      const QPointF first = point(relative);
      const QPointF second = point(relative);
      current = point(relative);
      path.cubicTo(first, second, current);
    } else if (upper == QLatin1Char('Q')) {
      const QPointF control = point(relative);
      current = point(relative);
      path.quadTo(control, current);
    } else if (upper == QLatin1Char('A')) {
      const qreal rx = number();
      const qreal ry = number();
      const qreal rotation = number();
      const bool largeArc = number() != 0.0;
      const bool sweep = number() != 0.0;
      const QPointF end = point(relative);
      appendArc(path, current, end, rx, ry, rotation, largeArc, sweep);
      current = end;
    } else {
      command = {};
    }
  }
  return path;
}

}  // namespace muffin::mermaid::scene
