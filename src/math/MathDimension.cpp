#include "math/MathDimension.h"

#include <QHash>
#include <QRegularExpression>

namespace muffin::math {

qreal sizeTextToEm(const QString& sizeText) {
  static const QHash<QString, qreal> unitToEm{
      {QStringLiteral("em"), 1.0},          {QStringLiteral("ex"), 0.431},       {QStringLiteral("mu"), 1.0 / 18.0},
      {QStringLiteral("pt"), 1.0 / 10.0},   {QStringLiteral("mm"), 0.2845},      {QStringLiteral("cm"), 2.845},
      {QStringLiteral("in"), 7.227},        {QStringLiteral("px"), 0.1},         {QStringLiteral("bp"), 0.1004},
      {QStringLiteral("pc"), 1.2},          {QStringLiteral("dd"), 0.1070},      {QStringLiteral("cc"), 1.284},
      {QStringLiteral("sp"), 1.0 / 655360.0}};
  static const QRegularExpression re(QStringLiteral("^([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*([a-zA-Z]+)?$"));
  const QRegularExpressionMatch match = re.match(sizeText.trimmed());
  if (!match.hasMatch()) {
    return 0.0;
  }
  const qreal number = match.captured(1).toDouble();
  const QString unit = match.captured(2).isEmpty() ? QStringLiteral("em") : match.captured(2).toLower();
  return number * unitToEm.value(unit, 1.0);
}

}  // namespace muffin::math
