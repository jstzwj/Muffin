#include "math/MathDimension.h"

#include <QHash>
#include <QRegularExpression>

namespace muffin::math {

namespace {

const QHash<QString, qreal>& texPtPerUnit() {
  static const QHash<QString, qreal> units{
      {QStringLiteral("pt"), 1.0},
      {QStringLiteral("mm"), 7227.0 / 2540.0},
      {QStringLiteral("cm"), 7227.0 / 254.0},
      {QStringLiteral("in"), 72.27},
      {QStringLiteral("bp"), 803.0 / 800.0},
      {QStringLiteral("pc"), 12.0},
      {QStringLiteral("dd"), 1238.0 / 1157.0},
      {QStringLiteral("cc"), 14856.0 / 1157.0},
      {QStringLiteral("nd"), 685.0 / 642.0},
      {QStringLiteral("nc"), 1370.0 / 107.0},
      {QStringLiteral("sp"), 1.0 / 65536.0},
      {QStringLiteral("px"), 803.0 / 800.0}};
  return units;
}

}  // namespace

qreal sizeTextToEm(const QString& sizeText) {
  static const QHash<QString, qreal> relativeToEm{
      {QStringLiteral("em"), 1.0},
      {QStringLiteral("ex"), 0.431},
      {QStringLiteral("mu"), 1.0 / 18.0}};
  static const QRegularExpression re(QStringLiteral("^([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*([a-zA-Z]+)?$"));
  const QRegularExpressionMatch match = re.match(sizeText.trimmed());
  if (!match.hasMatch()) {
    return 0.0;
  }
  const qreal number = match.captured(1).toDouble();
  const QString unit = match.captured(2).isEmpty() ? QStringLiteral("em") : match.captured(2).toLower();
  if (relativeToEm.contains(unit)) {
    return number * relativeToEm.value(unit);
  }
  if (texPtPerUnit().contains(unit)) {
    return number * texPtPerUnit().value(unit) / 10.0;
  }
  return 0.0;
}

qreal dimensionToPoints(const QString& sizeText, qreal fontPointSize, qreal absoluteReferencePointSize) {
  static const QHash<QString, qreal> relativeToEm{
      {QStringLiteral("em"), 1.0},
      {QStringLiteral("ex"), 0.431},
      {QStringLiteral("mu"), 1.0 / 18.0}};

  static const QRegularExpression re(QStringLiteral("^([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+))\\s*([a-zA-Z]+)?$"));
  const QRegularExpressionMatch match = re.match(sizeText.trimmed());
  if (!match.hasMatch()) {
    return 0.0;
  }
  const qreal number = match.captured(1).toDouble();
  const QString unit = match.captured(2).isEmpty() ? QStringLiteral("em") : match.captured(2).toLower();

  if (texPtPerUnit().contains(unit)) {
    const qreal reference = absoluteReferencePointSize > 0.0 ? absoluteReferencePointSize : fontPointSize;
    return number * texPtPerUnit().value(unit) * reference / 10.0;
  }
  if (relativeToEm.contains(unit)) {
    return number * relativeToEm.value(unit) * fontPointSize;
  }
  return 0.0;
}

}  // namespace muffin::math
