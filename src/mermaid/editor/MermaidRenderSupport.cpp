#include "mermaid/editor/MermaidRenderSupport.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidRenderCache.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::editor {
namespace {

QString configString(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  if (value.isDouble()) return QString::number(value.toDouble(), 'g', 15);
  if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  return {};
}

}  // namespace

flowtheme::FlowThemeId themeIdFromName(const QString& name) {
  static const QHash<QString, flowtheme::FlowThemeId> map = {
      {QStringLiteral("default"), flowtheme::FlowThemeId::Default},
      {QStringLiteral("base"), flowtheme::FlowThemeId::Base},
      {QStringLiteral("dark"), flowtheme::FlowThemeId::Dark},
      {QStringLiteral("forest"), flowtheme::FlowThemeId::Forest},
      {QStringLiteral("neutral"), flowtheme::FlowThemeId::Neutral},
      {QStringLiteral("neo"), flowtheme::FlowThemeId::Neo},
      {QStringLiteral("neo-dark"), flowtheme::FlowThemeId::NeoDark},
      {QStringLiteral("redux"), flowtheme::FlowThemeId::Redux},
      {QStringLiteral("redux-dark"), flowtheme::FlowThemeId::ReduxDark},
      {QStringLiteral("redux-color"), flowtheme::FlowThemeId::ReduxColor},
      {QStringLiteral("redux-dark-color"), flowtheme::FlowThemeId::ReduxDarkColor},
  };
  return map.value(name.isEmpty() ? QStringLiteral("default") : name, flowtheme::FlowThemeId::Default);
}

// Extract the mermaid theme the source declares (%%{init:{theme}}%%), else default.
QString themeFromConfig(const QJsonObject& config) {
  const QJsonValue top = config.value(QStringLiteral("theme"));
  if (top.isString()) return top.toString();
  return QStringLiteral("default");
}

QHash<QString, QString> themeOverrides(const QJsonObject& config) {
  QHash<QString, QString> result;
  const QJsonObject values = config.value(QStringLiteral("themeVariables")).toObject();
  for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
    if (it.key() == QLatin1String("THEME_COLOR_LIMIT")) continue;  // handled below via jsThemeColorLimit
    const QString value = configString(it.value());
    if (!value.isEmpty()) result.insert(it.key(), value);
  }
  // THEME_COLOR_LIMIT is an integer palette size, not a free-form string: route
  // it through jsThemeColorLimit so the value carried into FlowThemeVariables::
  // set() is upstream's JS Number()+ceil result (e.g. 2.5 -> 3, "0x2" -> 2), not
  // the generic configString -> FlowThemeVariables::set toInt() path (which
  // truncates "2.5" to 0). null/absent -> nullopt -> not inserted -> the theme
  // keeps its default (12). Present-but-non-positive/NaN -> 0.
  if (const std::optional<int> lim = jsThemeColorLimit(config))
    result.insert(QStringLiteral("THEME_COLOR_LIMIT"), QString::number(*lim));
  if (config.value(QStringLiteral("fontFamily")).isString())
    result.insert(QStringLiteral("fontFamily"), config.value(QStringLiteral("fontFamily")).toString());
  if (!result.contains(QStringLiteral("fontFamily")))
    result.insert(QStringLiteral("fontFamily"), MermaidFontRegistry::cssFamilyStack());
  return result;
}

namespace {
constexpr double kJsNaN = std::numeric_limits<double>::quiet_NaN();

// Parse the digits of a JS 0x/0b/0o integer literal (after the prefix) to a double.
// Valid digits but a value beyond ~1e15 saturate there (the caller's ceil saturates
// at INT_MAX, so any large positive selects the full palette); an INVALID digit —
// out of radix range (e.g. '2' in binary, '8' in octal) or non-alphanumeric — is
// NaN. Avoids qulonglong overflow: e.g. "0x10000000000000000" is a valid 2^64 that
// toULongLong can't hold (it would return 0 and wrongly disable the palette).
double parseJsRadixInt(const QString& digits, int base) {
  double acc = 0.0;
  bool any = false;
  for (const QChar& ch : digits) {
    const int lc = ch.toLower().toLatin1();
    int dv;
    if (lc >= '0' && lc <= '9') dv = lc - '0';
    else if (lc >= 'a' && lc <= 'z') dv = lc - 'a' + 10;
    else return kJsNaN;  // non-alphanumeric -> not a valid literal
    if (dv >= base) return kJsNaN;  // digit out of radix range
    if (acc < 1e15) acc = acc * base + dv;  // stop growing once huge (no +inf)
    any = true;
  }
  return any ? acc : kJsNaN;  // empty -> NaN
}

// JS Number(string): trims, then a numeric literal — decimal (with sign +
// scientific), or an unsigned 0x/0b/0o integer literal; "" -> 0; else NaN.
// (Signed hex/bin/oct and hex floats are not in the StringNumericLiteral grammar.)
double jsNumberString(QString s) {
  s = s.trimmed();
  if (s.isEmpty()) return 0.0;  // Number("") == 0
  // 0x / 0b / 0o are all 2-char prefixes; a valid literal has >= 1 digit after.
  const auto intPrefix = [&s](QLatin1String pfx) {
    return s.length() > 2 && s.left(2).compare(pfx, Qt::CaseInsensitive) == 0;
  };
  if (intPrefix(QLatin1String("0x"))) return parseJsRadixInt(s.mid(2), 16);
  if (intPrefix(QLatin1String("0b"))) return parseJsRadixInt(s.mid(2), 2);
  if (intPrefix(QLatin1String("0o"))) return parseJsRadixInt(s.mid(2), 8);
  bool ok = false;
  const double n = s.toDouble(&ok);  // decimal / scientific (toDouble trims too)
  return ok ? n : kJsNaN;
}

// JS String() of a value as an Array element (Array.prototype.join's per-element
// String()). number->a round-trippable decimal (max_digits10 'g' so 2.0000001 is
// not truncated to "2"; integral values still print without ".0"); bool->
// "true"/"false"; null/undefined->""; string->itself; array->recursive comma-join;
// object->"[object Object]".
QString jsElementString(const QJsonValue& v);
QString jsArrayToString(const QJsonArray& a) {
  QString r;
  for (int i = 0; i < a.size(); ++i) {
    if (i) r += QLatin1Char(',');
    r += jsElementString(a.at(i));
  }
  return r;  // [] -> ""  (so Number([]) == Number("") == 0)
}
QString jsElementString(const QJsonValue& v) {
  switch (v.type()) {
    case QJsonValue::Double:
      // 'g' with max_digits10 (17) round-trips; integral doubles still print as "2".
      return QString::number(v.toDouble(), 'g', std::numeric_limits<double>::max_digits10);
    case QJsonValue::Bool:   return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Null:   return QString();  // null/undefined element -> ""
    case QJsonValue::String: return v.toString();
    case QJsonValue::Array:  return jsArrayToString(v.toArray());
    case QJsonValue::Object:
    default:                 return QStringLiteral("[object Object]");
  }
}

// JS Number(value): number->itself; bool->1/0; null->0; string->jsNumberString;
// array->Number(array.toString()) (a comma-join, so [2]->"2"->2, ["2.5"]->2.5,
// [true]->"true"->NaN, [null]->""->0, [[2]]->"2"->2, [x,y]->"x,y"->NaN);
// object/undefined->NaN.
double jsNumber(const QJsonValue& v) {
  switch (v.type()) {
    case QJsonValue::Double: return v.toDouble();
    case QJsonValue::Bool:   return v.toBool() ? 1.0 : 0.0;
    case QJsonValue::Null:   return 0.0;  // Number(null) == 0
    case QJsonValue::String: return jsNumberString(v.toString());
    case QJsonValue::Array:  return jsNumberString(jsArrayToString(v.toArray()));
    case QJsonValue::Object:
    case QJsonValue::Undefined:
    default:                 return kJsNaN;  // Number({}) / Number(undefined) == NaN
  }
}
}  // namespace

// genColor rule count for THEME_COLOR_LIMIT via the %%{init}%% SOURCE entry (probed
// vs mermaid 11.16.0, G:/github/req-probe/step4-source-entry-report.json). Upstream
// runs `for (let i = 0; i < THEME_COLOR_LIMIT; i++)`, so the rule count is the count
// of non-negative integers < Number(TCL) = ceil of a positive finite Number(TCL).
// Config MERGE runs first: a null/absent value does NOT override -> return nullopt
// and the caller keeps the theme default (12). Otherwise Number() the raw value
// (full JS semantics incl. 0x/0b/0o strings and single-element arrays) and ceil it;
// NaN / non-positive -> 0 rules.
std::optional<int> jsThemeColorLimit(const QJsonObject& config) {
  const QJsonValue v = config.value(QStringLiteral("themeVariables")).toObject()
                           .value(QStringLiteral("THEME_COLOR_LIMIT"));
  if (v.isUndefined() || v.isNull()) return std::nullopt;  // merge: null/absent keep default
  const double n = jsNumber(v);
  if (!std::isfinite(n) || n <= 0.0) return 0;
  // Saturate: a huge TCL (e.g. 2147483648) would overflow int on the cast; clamp
  // to INT_MAX so the consumer's `k < limit` gate still selects the full palette
  // (matching upstream, which visually covers all 12 color-ids for large TCL).
  const double c = std::ceil(n);
  constexpr double kIntMax = static_cast<double>(std::numeric_limits<int>::max());
  return c >= kIntMax ? std::numeric_limits<int>::max() : static_cast<int>(c);
}

qreal pixelValue(const QString& value, qreal fallback) {
  static const QRegularExpression number(QStringLiteral(R"(^\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))px\s*$)"),
                                          QRegularExpression::CaseInsensitiveOption);
  const auto match = number.match(value);
  if (!match.hasMatch()) return fallback;
  bool ok = false;
  const qreal parsed = match.captured(1).toDouble(&ok);
  return ok && parsed > 0.0 ? parsed : fallback;
}

QString firstFontFamily(QString cssFamily) {
  cssFamily = cssFamily.section(QLatin1Char(','), 0, 0).trimmed();
  if (cssFamily.size() >= 2 &&
      ((cssFamily.front() == QLatin1Char('"') && cssFamily.back() == QLatin1Char('"')) ||
       (cssFamily.front() == QLatin1Char('\'') && cssFamily.back() == QLatin1Char('\''))))
    cssFamily = cssFamily.mid(1, cssFamily.size() - 2);
  return cssFamily.isEmpty() ? QStringLiteral("Arial") : cssFamily;
}

qreal configNumber(const QJsonObject& object, const QString& key, qreal fallback) {
  const QJsonValue value = object.value(key);
  return value.isDouble() && value.toDouble() >= 0.0 ? value.toDouble() : fallback;
}

QFont::Weight cssFontWeightToQt(const QJsonValue& value, QFont::Weight fallback) {
  if (value.isDouble()) {
    const double raw = value.toDouble();
    // CSS font-weight only has meaning in 1..1000; anything outside (0, 1001,
    // negative) is invalid and a browser falls back to normal — like mermaid.
    if (raw < 1.0 || raw > 1000.0) return fallback;
    return static_cast<QFont::Weight>(static_cast<int>(std::round(raw)));
  }
  if (value.isString()) {
    const QString text = value.toString().trimmed().toLower();
    if (text == QLatin1String("normal")) return QFont::Normal;
    if (text == QLatin1String("bold")) return QFont::Bold;
    // bolder/lighter resolve relative to the inherited weight. Sequence labels
    // inherit the default normal (400), so bolder -> 700 and lighter -> 100
    // (matching mermaid 11.16.0 / Chromium).
    if (text == QLatin1String("bolder")) return QFont::Bold;
    if (text == QLatin1String("lighter")) return QFont::Thin;
    // Parse with toDouble, not toInt: a CSS font-weight string may be a decimal
    // ("500.5"), scientific ("1e2"), or carry a sign/leading zeros ("+500",
    // "0500"). Qt's toDouble requires the whole string to be a valid number, and
    // we apply the same 1..1000 range gate + rounding as the number branch.
    bool ok = false;
    const double weight = text.toDouble(&ok);
    if (ok && weight >= 1.0 && weight <= 1000.0)
      return static_cast<QFont::Weight>(static_cast<int>(std::round(weight)));
    return fallback;
  }
  return fallback;
}

bool truthyConfigValue(const QJsonValue& value) {
  switch (value.type()) {
    case QJsonValue::Bool: return value.toBool();
    case QJsonValue::Double: return value.toDouble() != 0.0;
    // JS truthiness: any non-empty string is truthy (including " ", "0", and
    // "false") — do not trim. mermaid setConf() gates the global mirror on this.
    case QJsonValue::String: return !value.toString().isEmpty();
    // JS: objects and arrays are always truthy.
    case QJsonValue::Array:
    case QJsonValue::Object: return true;
    default: return false;  // Null, Undefined
  }
}

MermaidRenderMetadata renderMetadata(
    const MermaidPreprocessResult& pre, const QString& diagramType,
    const QString& diagramTitle, const QString& accessibleTitle,
    const QString& accessibleDescription, const QString& titleColor,
    const QString& fontFamily, qreal titleFontSize,
    qreal titleTopMargin, qreal diagramPadding) {
  MermaidRenderMetadata metadata;
  metadata.diagramType = diagramType;
  metadata.roleDescription = diagramType;
  metadata.title = diagramTitle.trimmed().isEmpty() ? pre.title : diagramTitle;
  metadata.accessibleTitle = accessibleTitle;
  metadata.accessibleDescription = accessibleDescription;
  metadata.titleColor = titleColor;
  metadata.fontFamily = firstFontFamily(fontFamily);
  metadata.titleFontSize = titleFontSize;
  metadata.titleTopMargin = titleTopMargin;
  metadata.diagramPadding = qMax<qreal>(0.0, diagramPadding);
  const bool classDiagram = diagramType.startsWith(QLatin1String("class"));
  QString configSection = QStringLiteral("state");
  if (diagramType.startsWith(QLatin1String("flowchart")))
    configSection = QStringLiteral("flowchart");
  else if (diagramType == QLatin1String("sequence"))
    configSection = QStringLiteral("sequence");
  // Mermaid 11.16's unified class renderer reads state.useMaxWidth; the class
  // field itself is upstream-inert and the generated effect matrix records it.
  const QJsonObject svgConfig = pre.config.value(configSection).toObject();
  metadata.svgUseMaxWidth = svgConfig.contains(QStringLiteral("useMaxWidth"))
      ? svgConfig.value(QStringLiteral("useMaxWidth")).toBool(true) : true;
  const QJsonObject familyConfig = pre.config.value(
      classDiagram ? QStringLiteral("class") : configSection).toObject();
  metadata.svgArrowMarkerAbsolute = familyConfig.contains(
      QStringLiteral("arrowMarkerAbsolute"))
      ? familyConfig.value(QStringLiteral("arrowMarkerAbsolute")).toBool(false)
      : pre.config.value(QStringLiteral("arrowMarkerAbsolute")).toBool(false);
  metadata.svgDeterministicIds =
      pre.config.value(QStringLiteral("deterministicIds")).toBool(false);
  metadata.svgDeterministicIdSeed =
      pre.config.value(QStringLiteral("deterministicIDSeed")).toString();
  return metadata;
}

void finalizeReadyEntry(MermaidRenderEntry& entry,
                        MermaidRenderMetadata metadata) {
  metadata.contentSize = QSizeF(entry.naturalSize);
  entry.naturalSize = QSize(
      qCeil(metadata.contentSize.width() + 2.0 * metadata.diagramPadding),
      qCeil(metadata.contentSize.height() + 2.0 * metadata.diagramPadding));
  if (metadata.hasVisibleTitle()) {
    // Mermaid places its title 25 px above the diagram and reserves about a
    // 40 px strip in the resulting SVG viewBox. Grow for larger configured
    // margins while retaining that 11.16 default geometry.
    metadata.titleHeight = qCeil(qMax(
        40.0, metadata.titleTopMargin +
                  qMax(15.0, metadata.titleFontSize * 0.75)));
    const qreal titleWidth = measureMermaidTitleWidth(metadata) + 16.0;
    entry.naturalSize.setWidth(qCeil(qMax(
        static_cast<qreal>(entry.naturalSize.width()), titleWidth)));
    entry.naturalSize.setHeight(qCeil(
        entry.naturalSize.height() + metadata.titleHeight));
  }
  entry.metadata = std::move(metadata);
}

}  // namespace muffin::mermaid::editor
