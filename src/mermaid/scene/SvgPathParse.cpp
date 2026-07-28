#include "mermaid/scene/SvgPathParse.h"

#include <QPainterPath>
#include <QPointF>
#include <QRegularExpression>
#include <QStringList>

namespace muffin::mermaid::scene {

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
    } else {
      command = {};
    }
  }
  return path;
}

}  // namespace muffin::mermaid::scene
