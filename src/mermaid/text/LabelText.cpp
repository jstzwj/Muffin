#include "mermaid/text/LabelText.h"

#include <QRegularExpression>

namespace muffin::mermaid::text {

QString collapsedSvgText(QString value, bool trimEdges) {
  static const QRegularExpression whitespace(
      QStringLiteral(R"([\x{0009}-\x{000D}\x{0020}]+)"));
  value.replace(whitespace, QStringLiteral(" "));
  if (!trimEdges) {
    return value;
  }
  // Edge-trim by the SAME collapsible class, NOT QString::trimmed(): trimmed() also strips
  // NBSP (U+00A0 is Unicode White_Space), but Chromium's white-space:normal never collapses
  // NBSP — it renders as a preserved space. The radar/packet parity tests pin this.
  static const QRegularExpression edges(
      QStringLiteral(R"(^[\x{0009}-\x{000D}\x{0020}]+|[\x{0009}-\x{000D}\x{0020}]+$)"));
  value.remove(edges);
  return value;
}

QStringList cssFontFamilies(const QString& expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\'')))) {
      family = family.mid(1, family.size() - 2);
    }
    if (!family.isEmpty()) {
      result.append(family);
    }
  }
  return result;
}

}  // namespace muffin::mermaid::text
