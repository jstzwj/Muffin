#pragma once

#include <QRegularExpression>
#include <QString>
#include <QVector>

#include <algorithm>
#include <utility>

namespace muffin::mermaid::scene {

// SVG stroke-dasharray lengths are CSS pixels. QPen stores each dash entry as
// a multiple of the pen width, so feed it the CSS pattern divided by width.
// SVG repeats an odd-length list before painting ("3" therefore means 3,3).
inline QVector<qreal> normalizedSvgDashPattern(
    QVector<qreal> cssPattern, qreal strokeWidth) {
  if (cssPattern.isEmpty() || !(strokeWidth > 0.0)) return {};
  qreal sum = 0.0;
  for (qreal value : cssPattern) {
    if (value < 0.0) return {};
    sum += value;
  }
  if (!(sum > 0.0)) return {};
  if (cssPattern.size() % 2 != 0) cssPattern += cssPattern;
  const qreal inverseWidth = 1.0 / strokeWidth;
  for (qreal& value : cssPattern) value *= inverseWidth;
  return cssPattern;
}

inline QVector<qreal> parseAndNormalizeSvgDashPattern(
    const QString& dasharray, qreal strokeWidth) {
  const QString value = dasharray.trimmed();
  if (value.isEmpty() || value.compare(QLatin1String("none"),
                                       Qt::CaseInsensitive) == 0)
    return {};
  QVector<qreal> pattern;
  const QStringList tokens = value.split(
      QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
  pattern.reserve(tokens.size());
  for (QString token : tokens) {
    if (token.endsWith(QLatin1String("px"), Qt::CaseInsensitive)) token.chop(2);
    bool ok = false;
    const qreal length = token.trimmed().toDouble(&ok);
    if (!ok) return {};
    pattern.append(length);
  }
  return normalizedSvgDashPattern(std::move(pattern), strokeWidth);
}

}  // namespace muffin::mermaid::scene
