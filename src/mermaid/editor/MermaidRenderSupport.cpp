#include "mermaid/editor/MermaidRenderSupport.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "theme/CssCalc.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QFontMetricsF>
#include <QRawFont>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>

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
    // An explicitly empty string is a real theme override (Journey uses it to
    // suppress an individual fillType CSS declaration). Arrays/objects still
    // stringify to empty here and remain outside the source-entry surface.
    if (it.value().isString() || !value.isEmpty()) result.insert(it.key(), value);
  }
  // XYChart is the one native family whose theme fields live in a nested
  // themeVariables object. Flatten its reviewed scalar surface for the typed
  // FlowTheme model. `cleanAndMerge(defaultThemeVariables.xyChart, raw)` keeps
  // unspecified defaults, while an explicitly empty string remains a real
  // final override, so preserve empty strings exactly like top-level fields.
  static const QStringList xyChartFields = {
      QStringLiteral("backgroundColor"), QStringLiteral("titleColor"),
      QStringLiteral("dataLabelColor"), QStringLiteral("xAxisTitleColor"),
      QStringLiteral("xAxisLabelColor"), QStringLiteral("xAxisTickColor"),
      QStringLiteral("xAxisLineColor"), QStringLiteral("yAxisTitleColor"),
      QStringLiteral("yAxisLabelColor"), QStringLiteral("yAxisTickColor"),
      QStringLiteral("yAxisLineColor"), QStringLiteral("plotColorPalette")};
  const QJsonObject xyChart = values.value(QStringLiteral("xyChart")).toObject();
  for (const QString& field : xyChartFields) {
    if (!xyChart.contains(field)) continue;
    const QJsonValue raw = xyChart.value(field);
    const QString value = configString(raw);
    if (raw.isString() || !value.isEmpty())
      result.insert(QStringLiteral("xyChart.") + field, value);
  }
  // Packet is another nested theme object, but its source-entry surface is
  // intentionally narrower than PacketStyleOptions. Mermaid's config-key
  // sanitizer retains only these four names (they occur elsewhere in
  // defaultConfig); the six packet-specific names are initialize()-API-only.
  // The object itself is a top-level theme override, so it replaces the full
  // Dark/Forest object before styles.ts fills omitted fields from defaults.
  const QJsonValue rawPacket = values.value(QStringLiteral("packet"));
  if (rawPacket.isObject()) {
    result.insert(QStringLiteral("packet.__replace"), QStringLiteral("true"));
    static const QStringList packetSourceFields = {
        QStringLiteral("labelColor"), QStringLiteral("labelFontSize"),
        QStringLiteral("titleColor"), QStringLiteral("titleFontSize")};
    const QJsonObject packet = rawPacket.toObject();
    for (const QString& field : packetSourceFields) {
      if (!packet.contains(field)) continue;
      const QJsonValue raw = packet.value(field);
      const QString value = configString(raw);
      if (raw.isString() || !value.isEmpty())
        result.insert(QStringLiteral("packet.") + field, value);
    }
  }
  static const QStringList cynefinFields = {
      QStringLiteral("domainFontSize"), QStringLiteral("itemFontSize"),
      QStringLiteral("boundaryColor"), QStringLiteral("boundaryWidth"),
      QStringLiteral("cliffColor"), QStringLiteral("cliffWidth"),
      QStringLiteral("arrowColor"), QStringLiteral("arrowWidth"),
      QStringLiteral("complexBg"), QStringLiteral("complicatedBg"),
      QStringLiteral("chaoticBg"), QStringLiteral("clearBg"),
      QStringLiteral("confusionBg"), QStringLiteral("textColor"),
      QStringLiteral("labelColor")};
  const QJsonObject cynefin = values.value(QStringLiteral("cynefin")).toObject();
  for (const QString& field : cynefinFields) {
    if (!cynefin.contains(field)) continue;
    const QJsonValue raw = cynefin.value(field);
    const QString value = configString(raw);
    if (raw.isString() || !value.isEmpty())
      result.insert(QStringLiteral("cynefin.") + field, value);
  }
  static const QStringList wardleyFields = {
      QStringLiteral("backgroundColor"), QStringLiteral("axisColor"),
      QStringLiteral("axisTextColor"), QStringLiteral("gridColor"),
      QStringLiteral("componentFill"), QStringLiteral("componentStroke"),
      QStringLiteral("componentLabelColor"), QStringLiteral("linkStroke"),
      QStringLiteral("evolutionStroke")};
  const QJsonObject wardley =
      values.value(QStringLiteral("wardley")).toObject();
  for (const QString& field : wardleyFields) {
    if (!wardley.contains(field)) continue;
    const QJsonValue raw = wardley.value(field);
    const QString value = configString(raw);
    if (raw.isString() || !value.isEmpty())
      result.insert(QStringLiteral("wardley.") + field, value);
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
  if (s == QLatin1String("Infinity") || s == QLatin1String("+Infinity"))
    return std::numeric_limits<double>::infinity();
  if (s == QLatin1String("-Infinity"))
    return -std::numeric_limits<double>::infinity();
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

double jsNumberValue(const QJsonValue& value) { return jsNumber(value); }

QString jsNumberToString(double value) {
  if (value == 0.0) return QStringLiteral("0");  // includes -0
  if (std::isnan(value)) return QStringLiteral("NaN");
  if (std::isinf(value))
    return value < 0.0 ? QStringLiteral("-Infinity") : QStringLiteral("Infinity");

  char buffer[64];
  const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value,
                                    std::chars_format::general);
  if (result.ec != std::errc())
    return QString::number(value, 'g', std::numeric_limits<double>::max_digits10);
  std::string raw(buffer, result.ptr);
  const size_t exponentAt = raw.find_first_of("eE");
  if (exponentAt == std::string::npos) return QString::fromStdString(raw);

  const std::string mantissa = raw.substr(0, exponentAt);
  const int exponent = std::stoi(raw.substr(exponentAt + 1));
  if (exponent >= -6 && exponent < 21) {
    const bool negative = !mantissa.empty() && mantissa.front() == '-';
    const size_t start = negative ? 1 : 0;
    const size_t dot = mantissa.find('.', start);
    std::string digits = mantissa.substr(start);
    const int beforeDot = dot == std::string::npos
                              ? static_cast<int>(digits.size())
                              : static_cast<int>(dot - start);
    if (dot != std::string::npos) digits.erase(dot - start, 1);
    const int decimalPos = beforeDot + exponent;
    std::string fixed = negative ? "-" : "";
    if (decimalPos <= 0) {
      fixed += "0.";
      fixed.append(static_cast<size_t>(-decimalPos), '0');
      fixed += digits;
    } else if (decimalPos >= static_cast<int>(digits.size())) {
      fixed += digits;
      fixed.append(static_cast<size_t>(decimalPos - static_cast<int>(digits.size())), '0');
    } else {
      fixed += digits.substr(0, static_cast<size_t>(decimalPos));
      fixed += '.';
      fixed += digits.substr(static_cast<size_t>(decimalPos));
    }
    return QString::fromStdString(fixed);
  }

  std::string normalized = mantissa;
  normalized += 'e';
  normalized += exponent >= 0 ? '+' : '-';
  normalized += std::to_string(std::abs(exponent));
  return QString::fromStdString(normalized);
}

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

qreal rawShapeRadius(const flowtheme::FlowThemeVariables& theme) {
  // shapes/roundedRect reads config.themeVariables.radius — the MERGED object
  // (user override ?: resolved theme value; `?? 5` only for null/undefined,
  // which never happens for the 11 built-ins since every constructor pins the
  // literal). theme.radius after resolveFlowTheme is exactly that merged
  // value. Invalid numbers fall back to 5 (the CSSOM drops an invalid rx).
  bool ok = false;
  const qreal radius = theme.radius.toDouble(&ok);
  return ok && radius > 0.0 ? radius : 5.0;
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

// Chromium clamps a CSS length's USED value per property before it reaches layout
// or painting. Replicate both caps so an absurd config (font-size:"1e9px") does NOT
// (a) diverge from the browser's rendered geometry, or (b) overflow int downstream
// -- a font-size >10000 would reach QFont::setPixelSize(int) via qRound and a value
// >INT_MAX overflows. Probed vs 11.16.0 (scripts/probe_mermaid_pie_length_clamp.mjs):
//   - font-size computed value saturates at 10000px: 9999.9 -> 9999.9, 10000 -> 10000,
//     10000.5/10001/1e5/1e9/1e10 -> 10000 (min(v, 10000); NOT floored -- 9999.9 kept).
//   - stroke-width computed value saturates at 33554428px. getComputedStyle()
//     rounds this whole range to "3.35544e+07px"; CSS Typed OM exposes the exact
//     used value (scripts/probe_mermaid_pie_length_clamp.mjs).
// The caps compose with the cascade: a root "1e9px" -> 10000 (font-size cap), then a
// child "3em" -> 3*10000 = 30000 -> re-capped to 10000; "200%" -> 20000 -> 10000;
// stroke "3em" -> 30000 (under the stroke cap); stroke "10ex"/"10ch" -> the linearly
// scaled ex/ch at the 10000 root (52k-ish, under the stroke cap).
constexpr qreal kChromiumMaxFontSizePx = 10000.0;
constexpr qreal kChromiumMaxStrokeWidthPx = 33554428.0;

CssLengthContext pieCssLengthContext(const QString& fontFamily, qreal emPx) {
  // An EXACT-zero (or negative) root font-size is PRESERVED with zero metrics --
  // upstream honors fontSize:"0px": em/%/inherited sizes collapse to 0 (probed:
  // root "0px" + title 200%/3em/invalid/bare -> 0px, pieStrokeWidth "3em" -> 0px).
  // Do NOT coerce 0 -> 16; the caller resolves the inherited root, which is 16
  // only when absent/invalid (cssFontSizePx already falls back to the parent).
  // Skip the QFont at 0: setPixelSize(0) warns and keeps the default, so ex/ch
  // are 0 (their natural value at a 0-size font).
  if (emPx <= 0.0) return {emPx, 16.0, 0.0, 0.0, QSizeF(800.0, 600.0)};
  // Any POSITIVE emPx -- including a sub-pixel root like 0.4px -- measures ex/ch
  // at the ACTUAL font size. Font metrics (x-height, '0' advance) scale LINEARLY
  // with pixel size for a scalable font, so measure them ONCE at a safe reference
  // size and scale to emPx; this is exact for any positive emPx. QFont has no
  // fractional setPixelSize, and qRound(0.4) = 0 would otherwise zero ex/ch
  // (probed vs 11.16.0: root 0.4px + pieStrokeWidth 10ex -> 2.0918px = 10 * exPx,
  // so exPx = 0.20918; 10ch -> 2.09766; 1em -> 0.4; 200% -> 0.8). Measuring at
  // the reference (16) also sidesteps setPixelSize(0) for any 0 < emPx < 0.5.
  constexpr qreal kReferencePx = 16.0;
  QStringList families;
  for (QString family : fontFamily.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) families.append(family);
  }
  if (families.isEmpty()) families.append(QStringLiteral("Noto Sans"));
  QFont f(families.first());
  if (families.size() > 1) f.setFamilies(families);
  f.setPixelSize(int(kReferencePx));
  const QFontMetricsF m(f);
  const qreal scale = emPx / kReferencePx;
  // viewport = mmdc default raster profile (RequirementScene.cpp:46).
  return {emPx, 16.0, m.xHeight() * scale,
          m.horizontalAdvance(QChar('0')) * scale, QSizeF(800.0, 600.0)};
}

qreal cssStrokeWidthPx(const QString& value, const CssLengthContext& ctx, qreal diagonalPx) {
  const QString t = value.trimmed();
  // stroke-width % -> N/100 of the SVG normalized diagonal (probed; resolved at
  // paint, so getComputedStyle leaves it as "%").
  if (t.endsWith(QLatin1Char('%'))) {
    bool ok = false;
    const qreal n = t.left(t.size() - 1).toDouble(&ok);
    if (!ok || !std::isfinite(n)) return 1.0;
    const qreal px = n / 100.0 * diagonalPx;
    return px < 0.0 ? 1.0 : std::min(px, kChromiumMaxStrokeWidthPx);
  }
  const CssLengthResult r = resolveCssLengthToPx(value, ctx);
  if (r.status != CssLengthStatus::Valid) return 1.0;  // missing/invalid -> CSS initial
  if (r.px < 0.0) return 1.0;                           // negative -> CSS initial
  return std::min(r.px, kChromiumMaxStrokeWidthPx);     // 0 or positive (0 -> caller NoPen)
}

qreal cssOpacity(const QString& value) {
  QString s = value.trimmed();
  const bool percent = s.endsWith(QLatin1Char('%'));
  if (percent) s.chop(1);
  bool ok = false;
  const double n = s.toDouble(&ok);
  if (!ok || !std::isfinite(n)) return 1.0;  // CSS initial
  return std::clamp(percent ? n / 100.0 : n, 0.0, 1.0);
}

QStringList cssFamilyList(const QString& expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

QFont::Weight faceAwareMetricWeight(const QString& family,
                                    QFont::Weight weight) {
  if (weight == QFont::Normal) return weight;
  // Chromium answers a weight request against the registered Regular webfont
  // with synthesized bold — the physical face (vertical metrics, advances)
  // stays Regular. Qt's font matching instead substitutes a real bold face of
  // another family (Arial for the bundled Noto Sans), so measurement falls
  // back to the Regular face whenever matching left the requested family.
  // Probe with the exact font the measurement path builds.
  const QFont probe = flowchart::makeFlowLabelFont(family, 16.0, weight);
  const QRawFont raw = QRawFont::fromFont(probe);
  const QString requested = cssFamilyList(family).first();
  if (raw.isValid() && !raw.familyName().isEmpty() &&
      raw.familyName().compare(requested, Qt::CaseInsensitive) != 0)
    return QFont::Normal;
  return weight;
}

qreal cssFontSizePx(const QString& value, const CssLengthContext& ctx) {
  const QString t = value.trimmed();
  const QString keyword = t.toLower();
  if (keyword == QLatin1String("xx-small")) return 9.0;
  if (keyword == QLatin1String("x-small")) return 10.0;
  if (keyword == QLatin1String("small")) return 13.0;
  if (keyword == QLatin1String("medium") || keyword == QLatin1String("initial")) return 16.0;
  if (keyword == QLatin1String("large")) return 18.0;
  if (keyword == QLatin1String("x-large")) return 24.0;
  if (keyword == QLatin1String("xx-large")) return 32.0;
  if (keyword == QLatin1String("xxx-large")) return 48.0;
  if (keyword == QLatin1String("larger"))
    return std::min(ctx.emPx * 1.2, kChromiumMaxFontSizePx);
  if (keyword == QLatin1String("smaller")) return ctx.emPx / 1.2;
  if (keyword == QLatin1String("inherit") || keyword == QLatin1String("unset") ||
      keyword == QLatin1String("revert") || keyword == QLatin1String("revert-layer"))
    return ctx.emPx;
  // font-size % -> N/100 of the PARENT font-size (ctx.emPx). An invalid/negative
  // percentage is dropped by the CSS parser and INHERITS the parent (ctx.emPx),
  // like any invalid font-size.
  if (t.endsWith(QLatin1Char('%'))) {
    bool ok = false;
    const qreal n = t.left(t.size() - 1).toDouble(&ok);
    if (!ok || !std::isfinite(n)) return ctx.emPx;
    const qreal px = n / 100.0 * ctx.emPx;
    return px < 0.0 ? ctx.emPx : std::min(px, kChromiumMaxFontSizePx);
  }
  // font-size REQUIRES a unit: a bare number (full CSS <number> incl. exponent
  // like "1e2" or "25") is invalid -> the declaration is dropped and the element
  // INHERITS the parent font-size (ctx.emPx), NOT a hardcoded 16. Probed vs
  // 11.16.0: neo root 14 -> pieTitleTextSize "25"/"abc"/"-2px"/"" all inherit 14;
  // "2em" root + "200%" -> 64 (200% of 32). Full <number> grammar so "1e2" is bare
  // (invalid) while "1e2px" carries a unit and resolves to 100.
  static const QRegularExpression bareNumber(
      QStringLiteral(R"(^\s*[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?\s*$)"));
  if (bareNumber.match(value).hasMatch()) {
    bool ok = false;
    const qreal number = t.toDouble(&ok);
    // CSS permits unitless zero for a length-valued property. Any non-zero bare
    // number remains invalid and inherits the parent font size.
    return ok && number == 0.0 ? 0.0 : ctx.emPx;
  }
  const CssLengthResult r = resolveCssLengthToPx(value, ctx);
  if (r.status != CssLengthStatus::Valid || r.px < 0.0) return ctx.emPx;  // inherited
  return std::min(r.px, kChromiumMaxFontSizePx);  // Chromium used-value clamp
}

qreal CssPixelFont::horizontalAdvance(const QString& text) const {
  return QFontMetricsF(font).horizontalAdvance(text) * scale;
}

CssPixelFont makeCssPixelFont(const QString& family, qreal pixelSize) {
  CssPixelFont result;
  result.font = QFont(family);
  if (!(pixelSize > 0.0) || !std::isfinite(pixelSize)) {
    result.scale = 0.0;
    return result;
  }
  const qreal integral = std::floor(pixelSize);
  int referencePx;
  if (pixelSize == integral && pixelSize <= qreal(std::numeric_limits<int>::max())) {
    referencePx = static_cast<int>(pixelSize);
  } else if (pixelSize < 1.0) {
    referencePx = 16;
  } else {
    referencePx = static_cast<int>(std::min<qreal>(std::ceil(pixelSize), 10000.0));
  }
  referencePx = std::max(referencePx, 1);
  result.font.setPixelSize(referencePx);
  result.scale = pixelSize / qreal(referencePx);
  return result;
}

CssPixelFont makeUnhintedCssPixelFont(const QString& family, qreal pixelSize) {
  CssPixelFont result = makeCssPixelFont(family, pixelSize);
  result.font.setHintingPreference(QFont::PreferNoHinting);
  return result;
}

qreal parseFontSizeNumber(const QString& value) {
  // Upstream parseInt(value, 10): the leading signed integer (decimals truncated,
  // any trailing unit ignored); no leading integer -> default 2. Parsed as a JS
  // Number (double), NOT qint64: parseInt returns a Number, so a value beyond the
  // qint64 range (e.g. "9223372036854775808px" = 2^63) parses to the nearest
  // double like JS instead of overflowing toLongLong and falling back to 2.
  // Probed: parseInt("9223372036854775808px",10) === 2^63; toDouble matches
  // (correctly-rounded, same as V8).
  static const QRegularExpression re(QStringLiteral(R"(^\s*([+-]?\d+))"));
  const auto m = re.match(value);
  if (!m.hasMatch()) return 2.0;
  bool ok = false;
  const double n = m.captured(1).toDouble(&ok);
  return ok ? qreal(n) : 2.0;
}

qreal parseFontSizeNumber(const QJsonValue& raw, const QString& fallbackString) {
  if (raw.isDouble()) return raw.toDouble();  // number verbatim (1.7 -> 1.7)
  return parseFontSizeNumber(raw.isString() ? raw.toString() : fallbackString);
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

bool evaluateConfigValue(const QJsonValue& value) {
  if (value.isBool() && !value.toBool()) return false;
  const auto jsString = [&](const auto& self, const QJsonValue& item) -> QString {
    if (item.isString()) return item.toString();
    if (item.isDouble()) return jsNumberToString(item.toDouble());
    if (item.isBool())
      return item.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (item.isNull()) return QStringLiteral("null");
    if (item.isUndefined()) return QStringLiteral("undefined");
    if (item.isObject()) return QStringLiteral("[object Object]");
    QStringList elements;
    for (const QJsonValue& element : item.toArray())
      elements.append(element.isNull() || element.isUndefined()
                          ? QString() : self(self, element));
    return elements.join(QLatin1Char(','));
  };
  QString text = jsString(jsString, value);
  text = text.trimmed().toLower();
  return text != QLatin1String("false") && text != QLatin1String("null") &&
         text != QLatin1String("0");
}

MermaidRenderMetadata renderMetadata(
    const MermaidPreprocessResult& pre, const QString& diagramType,
    const QString& diagramTitle, const QString& accessibleTitle,
    const QString& accessibleDescription, const QString& titleColor,
    const QString& fontFamily, qreal titleFontSize,
    qreal titleTopMargin, qreal diagramPadding,
    qreal titleBandPadding) {
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
  metadata.titleBandPadding = titleBandPadding;
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
  // Mermaid's source-entry mirror writes the root option into the effective
  // Flow/Sequence configs. Nested family keys with the same name are schema
  // fields but are not observed by the 11.16 render path.
  metadata.svgArrowMarkerAbsolute =
      pre.config.value(QStringLiteral("arrowMarkerAbsolute")).toBool(false);
  metadata.svgDeterministicIds =
      pre.config.value(QStringLiteral("deterministicIds")).toBool(false);
  metadata.svgDeterministicIdSeed =
      pre.config.value(QStringLiteral("deterministicIDSeed")).toString();
  return metadata;
}

QRectF mermaidClientBox(const std::shared_ptr<const MermaidScene>& scene,
                        const MermaidRenderMetadata& metadata) {
  if (!scene) return {};
  const QRectF clientViewBox = scene->svgClientViewBox();
  if (!clientViewBox.isValid()) return {};
  // Upstream (state draw + rendering-util, browser-verified): the renderer
  // inserts the title BEFORE sizing the viewport — insertTitle anchors the
  // text baseline at ABSOLUTE -titleTopMargin (text y attr), centered
  // (text-anchor:middle) on the content bbox — then setupViewPortForSVG /
  // setupGraphViewbox write viewBox = svgBBox(content ∪ title) ± padding
  // with NO translate, so the origin carries the content's raw coordinates.
  // Chrome's text getBBox uses rounded integer font metrics for ascent and
  // descent (the same quantization the raster titleHeight applies).
  QRectF clientBox = clientViewBox;
  if (metadata.hasVisibleTitle()) {
    QFont titleFace(metadata.fontFamily);
    titleFace.setPixelSize(qMax(1, qRound(metadata.titleFontSize)));
    const qreal ascent = qRound(QFontMetricsF(titleFace).ascent());
    const qreal descent = qRound(QFontMetricsF(titleFace).descent());
    const qreal titleWidth = measureMermaidTitleWidth(metadata);
    const qreal padding = metadata.titleBandPadding >= 0.0
        ? metadata.titleBandPadding : metadata.diagramPadding;
    const QRectF titleBox(
        clientViewBox.center().x() - titleWidth / 2.0,
        -metadata.titleTopMargin - ascent,
        titleWidth, ascent + descent);
    clientBox = clientBox.united(titleBox.adjusted(-padding, -padding,
                                                   padding, padding));
  }
  return clientBox;
}

QRectF mermaidClientBox(const MermaidRenderEntry& entry) {
  return mermaidClientBox(entry.scene, entry.metadata);
}

void finalizeReadyEntry(MermaidRenderEntry& entry,
                        MermaidRenderMetadata metadata) {
  const QRectF clientBox = mermaidClientBox(entry.scene, metadata);
  if (clientBox.isValid()) {
    // Client-box families: ONE rounding of the FRACTIONAL total box
    // (Chromium screenshots the replaced element at the nearest device
    // pixel). The piecewise path below would qRound the content and qCeil
    // the title band separately — a diagramPadding of 8.25 with a title
    // rastered 71 + ceil(52.25) = 124 instead of round(119.5) = 120, with
    // the title baseline pushed ~0.75px low.
    metadata.contentSize = clientBox.size();
    entry.naturalSize = QSize(qMax(1, qRound(clientBox.width())),
                              qMax(1, qRound(clientBox.height())));
    if (metadata.hasVisibleTitle()) {
      QFont titleFace(metadata.fontFamily);
      titleFace.setPixelSize(qMax(1, qRound(metadata.titleFontSize)));
      const qreal ascent = QFontMetricsF(titleFace).ascent();
      const qreal bandPadding = metadata.titleBandPadding >= 0.0
          ? metadata.titleBandPadding : metadata.diagramPadding;
      // Integer titleHeight for the paint-time title strip; the BASELINE
      // position is independent of this ceil (bottom-anchored at
      // -titleTopMargin).
      metadata.titleHeight = qCeil(
          metadata.titleTopMargin + qRound(ascent) + bandPadding);
    }
    entry.metadata = std::move(metadata);
    return;
  }
  metadata.contentSize = QSizeF(entry.naturalSize);
  entry.naturalSize = QSize(
      qCeil(metadata.contentSize.width() + 2.0 * metadata.diagramPadding),
      qCeil(metadata.contentSize.height() + 2.0 * metadata.diagramPadding));
  if (metadata.hasVisibleTitle()) {
    // Upstream (insertTitle + setupGraphViewbox, browser-verified against
    // the pinned Noto face): the title text bbox sits titleTopMargin +
    // round(font ascent) above the content box and the viewbox adds the
    // family padding above the union — a 25+19+8 = 52px strip for the
    // default state/flowchart geometry.
    QFont titleFace(metadata.fontFamily);
    titleFace.setPixelSize(qMax(1, qRound(metadata.titleFontSize)));
    const qreal ascent = QFontMetricsF(titleFace).ascent();
    const qreal bandPadding = metadata.titleBandPadding >= 0.0
        ? metadata.titleBandPadding : metadata.diagramPadding;
    metadata.titleHeight = qCeil(
        metadata.titleTopMargin + qRound(ascent) + bandPadding);
    const qreal titleWidth = measureMermaidTitleWidth(metadata) + 16.0;
    // The raster client box snaps to the NEAREST device pixel (Chromium
    // element screenshots); ceil would inflate a 104.39px title to 105.
    entry.naturalSize.setWidth(qRound(qMax(
        static_cast<qreal>(entry.naturalSize.width()), titleWidth)));
    entry.naturalSize.setHeight(qCeil(
        entry.naturalSize.height() + metadata.titleHeight));
  }
  entry.metadata = std::move(metadata);
}

}  // namespace muffin::mermaid::editor
