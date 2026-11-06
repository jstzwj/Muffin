#include "theme/TypographyOverrides.h"
#include "theme/CssThemeParser.h"
#include <QSettings>
#include <algorithm>

namespace muffin {
bool TypographyOverrides::hasDocumentOverrides() const {
  return !bodyFamily.isEmpty() || !codeFamily.isEmpty() || bodySize || codeSize ||
         std::any_of(headingSizes.begin(), headingSizes.end(), [](int size) { return size != 0; });
}
QVariantMap TypographyOverrides::values() const {
  QVariantMap result{{"bodyFamily", bodyFamily},
                     {"codeFamily", codeFamily},
                     {"bodySize", bodySize},
                     {"codeSize", codeSize},
                     {"sourceSize", sourceSize}};
  for (int i = 0; i < 6; ++i)
    result.insert(QStringLiteral("h%1Size").arg(i + 1), headingSizes[i]);
  return result;
}
TypographyOverrides TypographyOverrides::fromValues(const QVariantMap& values) {
  TypographyOverrides result;
  result.bodyFamily = values.value("bodyFamily").toString().trimmed();
  result.codeFamily = values.value("codeFamily").toString().trimmed();
  const auto size = [&](const QString& key) {
    const auto value = values.value(key).toInt();
    return value > 0 ? qBound(8, value, 96) : 0;
  };
  result.bodySize = size("bodySize");
  result.codeSize = size("codeSize");
  result.sourceSize = size("sourceSize");
  for (int i = 0; i < 6; ++i)
    result.headingSizes[i] = size(QStringLiteral("h%1Size").arg(i + 1));
  return result;
}
TypographyOverrides TypographyOverrides::load() {
  return fromValues(QSettings().value(QStringLiteral("appearance/typography")).toMap());
}
void TypographyOverrides::save() const {
  QSettings().setValue(QStringLiteral("appearance/typography"), values());
}
QString TypographyOverrides::css(qreal bodyScale) const {
  QString result;
  const auto family = [](QString value) {
    value.replace(u'\\', QStringLiteral("\\\\"));
    value.replace(u'"', QStringLiteral("\\\""));
    value.replace(u'\n', u' ');
    value.replace(u'\r', u' ');
    return u'"' + value + u'"';
  };
  const auto rule = [&](const QString& selectors, const QString& property, const QString& value) {
    result += selectors + " { " + property + ": " + value + " !important; }\n";
  };
  // Semantic text hosts inherit into inline runs. Do not force a family onto
  // arbitrary spans, generated icons, SVG text or the mathematics renderer.
  if (!bodyFamily.isEmpty())
    rule("#write, #write p, #write li, #write blockquote, #write th, #write td, "
         "#write h1, #write h2, #write h3, #write h4, #write h5, #write h6",
         "font-family", family(bodyFamily));
  const QString code = "#write pre, #write code, #write kbd, #write samp, #write tt";
  if (!codeFamily.isEmpty())
    rule(code, "font-family", family(codeFamily) + ", monospace");
  const auto px = [&](int size) { return QString::number(size / qMax<qreal>(.001, bodyScale), 'g', 12) + "px"; };
  if (codeSize) {
    // Setting both pre and code to an absolute size avoids nesting .875em twice.
    rule(code, "font-size", px(codeSize));
  }
  for (int i = 0; i < 6; ++i)
    if (headingSizes[i])
      rule(QStringLiteral("#write h%1").arg(i + 1), "font-size", px(headingSizes[i]));
  return result;
}
CssThemeSheet TypographyOverrides::styleSheet(qreal bodyScale) const {
  const auto parsed = CssThemeParser::parse(css(bodyScale), {});
  CssThemeSheet result;
  for (auto rule : parsed.rules()) {
    rule.userOverride = true;
    result.addRule(std::move(rule));
  }
  return result;
}
}
