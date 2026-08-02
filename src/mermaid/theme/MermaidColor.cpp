#include "mermaid/theme/MermaidColor.h"

#include <QChar>
#include <QColor>
#include <QHash>
#include <QRegularExpression>
#include <QString>

#include <cmath>

namespace muffin::mermaid::color {
namespace {

// khroma Lang.round = Math.round(x*1e10)/1e10. JS Math.round rounds half toward
// +∞, which equals floor(x + 0.5) for all x (verified: round(2.5)=3, round(-2.5)=-2).
// std::round rounds half away from zero and would diverge on negative .5 values.
qreal langRound(qreal x) { return std::floor(x * 1e10 + 0.5) / 1e10; }

// JS Number.toString: the shortest string that round-trips. langRound results
// are multiples of 1e-10, so 'f' with 10 decimals is exact; strip trailing
// zeros + dangling dot to match JS (240, not 240.0000000000). QString::arg's
// default 6 sig-figs would truncate -79.4117647059 -> -79.4118.
QString numberToString(qreal x) {
  if (x == 0.0) return QStringLiteral("0");  // also catches -0
  QString s = QString::number(x, 'f', 10);
  if (s.contains(QLatin1Char('.'))) {
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
  }
  return s;
}

// khroma Channel.clamp — note h uses JS signed modulo (`h % 360`), which keeps
// the sign of the dividend; std::fmod matches that for finite values.
qreal clampR(qreal v) { return v >= 255.0 ? 255.0 : v < 0.0 ? 0.0 : v; }
qreal clampG(qreal v) { return v >= 255.0 ? 255.0 : v < 0.0 ? 0.0 : v; }
qreal clampB(qreal v) { return v >= 255.0 ? 255.0 : v < 0.0 ? 0.0 : v; }
qreal clampH(qreal v) { return std::fmod(v, 360.0); }
qreal clampS(qreal v) { return v >= 100.0 ? 100.0 : v < 0.0 ? 0.0 : v; }
qreal clampL(qreal v) { return v >= 100.0 ? 100.0 : v < 0.0 ? 0.0 : v; }
qreal clampA(qreal v) { return v >= 1.0 ? 1.0 : v < 0.0 ? 0.0 : v; }

qreal hue2rgb(qreal p, qreal q, qreal t) {
  if (t < 0.0) t += 1.0;
  if (t > 1.0) t -= 1.0;
  if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
  if (t < 1.0 / 2.0) return q;
  if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
  return p;
}

// khroma hsl2rgb({h,s,l}, channel). Assumes s/l in 0-100, h in degrees.
qreal hsl2rgbChannel(qreal h, qreal s, qreal l, QChar channel) {
  if (s == 0.0) return l * 2.55;
  h /= 360.0;
  s /= 100.0;
  l /= 100.0;
  const qreal q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
  const qreal p = 2.0 * l - q;
  if (channel == QLatin1Char('r')) return hue2rgb(p, q, h + 1.0 / 3.0) * 255.0;
  if (channel == QLatin1Char('g')) return hue2rgb(p, q, h) * 255.0;
  return hue2rgb(p, q, h - 1.0 / 3.0) * 255.0;  // 'b'
}

// khroma rgb2hsl({r,g,b}, channel). Assumes r/g/b in 0-255.
qreal rgb2hslChannel(qreal r, qreal g, qreal b, QChar channel) {
  r /= 255.0;
  g /= 255.0;
  b /= 255.0;
  const qreal max = std::max({r, g, b});
  const qreal min = std::min({r, g, b});
  const qreal l = (max + min) / 2.0;
  if (channel == QLatin1Char('l')) return l * 100.0;
  if (max == min) return 0.0;
  const qreal d = max - min;
  const qreal s = l > 0.5 ? d / (2.0 - max - min) : d / (max + min);
  if (channel == QLatin1Char('s')) return s * 100.0;
  if (max == r) return ((g - b) / d + (g < b ? 6.0 : 0.0)) * 60.0;
  if (max == g) return ((b - r) / d + 2.0) * 60.0;
  return ((r - g) / d + 4.0) * 60.0;  // max == b
}

QString dec2hex(int dec) {
  const int v = std::round(static_cast<qreal>(dec));
  QString hex = QString::number(v, 16);
  return hex.length() > 1 ? hex : QStringLiteral("0") + hex;
}

// --- parse helpers (khroma Hex/RGB/HSL/Keyword.parse) ---

const QHash<QString, QString>& keywordColors() {
  static const QHash<QString, QString> table = {
    {QStringLiteral("aliceblue"), QStringLiteral("#f0f8ff")},
    {QStringLiteral("antiquewhite"), QStringLiteral("#faebd7")},
    {QStringLiteral("aqua"), QStringLiteral("#00ffff")},
    {QStringLiteral("aquamarine"), QStringLiteral("#7fffd4")},
    {QStringLiteral("azure"), QStringLiteral("#f0ffff")},
    {QStringLiteral("beige"), QStringLiteral("#f5f5dc")},
    {QStringLiteral("bisque"), QStringLiteral("#ffe4c4")},
    {QStringLiteral("black"), QStringLiteral("#000000")},
    {QStringLiteral("blanchedalmond"), QStringLiteral("#ffebcd")},
    {QStringLiteral("blue"), QStringLiteral("#0000ff")},
    {QStringLiteral("blueviolet"), QStringLiteral("#8a2be2")},
    {QStringLiteral("brown"), QStringLiteral("#a52a2a")},
    {QStringLiteral("burlywood"), QStringLiteral("#deb887")},
    {QStringLiteral("cadetblue"), QStringLiteral("#5f9ea0")},
    {QStringLiteral("chartreuse"), QStringLiteral("#7fff00")},
    {QStringLiteral("chocolate"), QStringLiteral("#d2691e")},
    {QStringLiteral("coral"), QStringLiteral("#ff7f50")},
    {QStringLiteral("cornflowerblue"), QStringLiteral("#6495ed")},
    {QStringLiteral("cornsilk"), QStringLiteral("#fff8dc")},
    {QStringLiteral("crimson"), QStringLiteral("#dc143c")},
    {QStringLiteral("cyanaqua"), QStringLiteral("#00ffff")},
    {QStringLiteral("darkblue"), QStringLiteral("#00008b")},
    {QStringLiteral("darkcyan"), QStringLiteral("#008b8b")},
    {QStringLiteral("darkgoldenrod"), QStringLiteral("#b8860b")},
    {QStringLiteral("darkgray"), QStringLiteral("#a9a9a9")},
    {QStringLiteral("darkgreen"), QStringLiteral("#006400")},
    {QStringLiteral("darkgrey"), QStringLiteral("#a9a9a9")},
    {QStringLiteral("darkkhaki"), QStringLiteral("#bdb76b")},
    {QStringLiteral("darkmagenta"), QStringLiteral("#8b008b")},
    {QStringLiteral("darkolivegreen"), QStringLiteral("#556b2f")},
    {QStringLiteral("darkorange"), QStringLiteral("#ff8c00")},
    {QStringLiteral("darkorchid"), QStringLiteral("#9932cc")},
    {QStringLiteral("darkred"), QStringLiteral("#8b0000")},
    {QStringLiteral("darksalmon"), QStringLiteral("#e9967a")},
    {QStringLiteral("darkseagreen"), QStringLiteral("#8fbc8f")},
    {QStringLiteral("darkslateblue"), QStringLiteral("#483d8b")},
    {QStringLiteral("darkslategray"), QStringLiteral("#2f4f4f")},
    {QStringLiteral("darkslategrey"), QStringLiteral("#2f4f4f")},
    {QStringLiteral("darkturquoise"), QStringLiteral("#00ced1")},
    {QStringLiteral("darkviolet"), QStringLiteral("#9400d3")},
    {QStringLiteral("deeppink"), QStringLiteral("#ff1493")},
    {QStringLiteral("deepskyblue"), QStringLiteral("#00bfff")},
    {QStringLiteral("dimgray"), QStringLiteral("#696969")},
    {QStringLiteral("dimgrey"), QStringLiteral("#696969")},
    {QStringLiteral("dodgerblue"), QStringLiteral("#1e90ff")},
    {QStringLiteral("firebrick"), QStringLiteral("#b22222")},
    {QStringLiteral("floralwhite"), QStringLiteral("#fffaf0")},
    {QStringLiteral("forestgreen"), QStringLiteral("#228b22")},
    {QStringLiteral("fuchsia"), QStringLiteral("#ff00ff")},
    {QStringLiteral("gainsboro"), QStringLiteral("#dcdcdc")},
    {QStringLiteral("ghostwhite"), QStringLiteral("#f8f8ff")},
    {QStringLiteral("gold"), QStringLiteral("#ffd700")},
    {QStringLiteral("goldenrod"), QStringLiteral("#daa520")},
    {QStringLiteral("gray"), QStringLiteral("#808080")},
    {QStringLiteral("green"), QStringLiteral("#008000")},
    {QStringLiteral("greenyellow"), QStringLiteral("#adff2f")},
    {QStringLiteral("grey"), QStringLiteral("#808080")},
    {QStringLiteral("honeydew"), QStringLiteral("#f0fff0")},
    {QStringLiteral("hotpink"), QStringLiteral("#ff69b4")},
    {QStringLiteral("indianred"), QStringLiteral("#cd5c5c")},
    {QStringLiteral("indigo"), QStringLiteral("#4b0082")},
    {QStringLiteral("ivory"), QStringLiteral("#fffff0")},
    {QStringLiteral("khaki"), QStringLiteral("#f0e68c")},
    {QStringLiteral("lavender"), QStringLiteral("#e6e6fa")},
    {QStringLiteral("lavenderblush"), QStringLiteral("#fff0f5")},
    {QStringLiteral("lawngreen"), QStringLiteral("#7cfc00")},
    {QStringLiteral("lemonchiffon"), QStringLiteral("#fffacd")},
    {QStringLiteral("lightblue"), QStringLiteral("#add8e6")},
    {QStringLiteral("lightcoral"), QStringLiteral("#f08080")},
    {QStringLiteral("lightcyan"), QStringLiteral("#e0ffff")},
    {QStringLiteral("lightgoldenrodyellow"), QStringLiteral("#fafad2")},
    {QStringLiteral("lightgray"), QStringLiteral("#d3d3d3")},
    {QStringLiteral("lightgreen"), QStringLiteral("#90ee90")},
    {QStringLiteral("lightgrey"), QStringLiteral("#d3d3d3")},
    {QStringLiteral("lightpink"), QStringLiteral("#ffb6c1")},
    {QStringLiteral("lightsalmon"), QStringLiteral("#ffa07a")},
    {QStringLiteral("lightseagreen"), QStringLiteral("#20b2aa")},
    {QStringLiteral("lightskyblue"), QStringLiteral("#87cefa")},
    {QStringLiteral("lightslategray"), QStringLiteral("#778899")},
    {QStringLiteral("lightslategrey"), QStringLiteral("#778899")},
    {QStringLiteral("lightsteelblue"), QStringLiteral("#b0c4de")},
    {QStringLiteral("lightyellow"), QStringLiteral("#ffffe0")},
    {QStringLiteral("lime"), QStringLiteral("#00ff00")},
    {QStringLiteral("limegreen"), QStringLiteral("#32cd32")},
    {QStringLiteral("linen"), QStringLiteral("#faf0e6")},
    {QStringLiteral("magenta"), QStringLiteral("#ff00ff")},
    {QStringLiteral("maroon"), QStringLiteral("#800000")},
    {QStringLiteral("mediumaquamarine"), QStringLiteral("#66cdaa")},
    {QStringLiteral("mediumblue"), QStringLiteral("#0000cd")},
    {QStringLiteral("mediumorchid"), QStringLiteral("#ba55d3")},
    {QStringLiteral("mediumpurple"), QStringLiteral("#9370db")},
    {QStringLiteral("mediumseagreen"), QStringLiteral("#3cb371")},
    {QStringLiteral("mediumslateblue"), QStringLiteral("#7b68ee")},
    {QStringLiteral("mediumspringgreen"), QStringLiteral("#00fa9a")},
    {QStringLiteral("mediumturquoise"), QStringLiteral("#48d1cc")},
    {QStringLiteral("mediumvioletred"), QStringLiteral("#c71585")},
    {QStringLiteral("midnightblue"), QStringLiteral("#191970")},
    {QStringLiteral("mintcream"), QStringLiteral("#f5fffa")},
    {QStringLiteral("mistyrose"), QStringLiteral("#ffe4e1")},
    {QStringLiteral("moccasin"), QStringLiteral("#ffe4b5")},
    {QStringLiteral("navajowhite"), QStringLiteral("#ffdead")},
    {QStringLiteral("navy"), QStringLiteral("#000080")},
    {QStringLiteral("oldlace"), QStringLiteral("#fdf5e6")},
    {QStringLiteral("olive"), QStringLiteral("#808000")},
    {QStringLiteral("olivedrab"), QStringLiteral("#6b8e23")},
    {QStringLiteral("orange"), QStringLiteral("#ffa500")},
    {QStringLiteral("orangered"), QStringLiteral("#ff4500")},
    {QStringLiteral("orchid"), QStringLiteral("#da70d6")},
    {QStringLiteral("palegoldenrod"), QStringLiteral("#eee8aa")},
    {QStringLiteral("palegreen"), QStringLiteral("#98fb98")},
    {QStringLiteral("paleturquoise"), QStringLiteral("#afeeee")},
    {QStringLiteral("palevioletred"), QStringLiteral("#db7093")},
    {QStringLiteral("papayawhip"), QStringLiteral("#ffefd5")},
    {QStringLiteral("peachpuff"), QStringLiteral("#ffdab9")},
    {QStringLiteral("peru"), QStringLiteral("#cd8533")},
    {QStringLiteral("pink"), QStringLiteral("#ffc0cb")},
    {QStringLiteral("plum"), QStringLiteral("#dda0dd")},
    {QStringLiteral("powderblue"), QStringLiteral("#b0e0e6")},
    {QStringLiteral("purple"), QStringLiteral("#800080")},
    {QStringLiteral("rebeccapurple"), QStringLiteral("#663399")},
    {QStringLiteral("red"), QStringLiteral("#ff0000")},
    {QStringLiteral("rosybrown"), QStringLiteral("#bc8f8f")},
    {QStringLiteral("royalblue"), QStringLiteral("#4169e1")},
    {QStringLiteral("saddlebrown"), QStringLiteral("#8b4513")},
    {QStringLiteral("salmon"), QStringLiteral("#fa8072")},
    {QStringLiteral("sandybrown"), QStringLiteral("#f4a460")},
    {QStringLiteral("seagreen"), QStringLiteral("#2e8b57")},
    {QStringLiteral("seashell"), QStringLiteral("#fff5ee")},
    {QStringLiteral("sienna"), QStringLiteral("#a0522d")},
    {QStringLiteral("silver"), QStringLiteral("#c0c0c0")},
    {QStringLiteral("skyblue"), QStringLiteral("#87ceeb")},
    {QStringLiteral("slateblue"), QStringLiteral("#6a5acd")},
    {QStringLiteral("slategray"), QStringLiteral("#708090")},
    {QStringLiteral("slategrey"), QStringLiteral("#708090")},
    {QStringLiteral("snow"), QStringLiteral("#fffafa")},
    {QStringLiteral("springgreen"), QStringLiteral("#00ff7f")},
    {QStringLiteral("tan"), QStringLiteral("#d2b48c")},
    {QStringLiteral("teal"), QStringLiteral("#008080")},
    {QStringLiteral("thistle"), QStringLiteral("#d8bfd8")},
    {QStringLiteral("transparent"), QStringLiteral("#00000000")},
    {QStringLiteral("turquoise"), QStringLiteral("#40e0d0")},
    {QStringLiteral("violet"), QStringLiteral("#ee82ee")},
    {QStringLiteral("wheat"), QStringLiteral("#f5deb3")},
    {QStringLiteral("white"), QStringLiteral("#ffffff")},
    {QStringLiteral("whitesmoke"), QStringLiteral("#f5f5f5")},
    {QStringLiteral("yellow"), QStringLiteral("#ffff00")},
    {QStringLiteral("yellowgreen"), QStringLiteral("#9acd32")},
  };
  return table;
}

std::optional<MermaidColor> parseHex(const QString& color) {
  if (color.isEmpty() || color.at(0) != QLatin1Char('#')) return std::nullopt;
  static const QRegularExpression re(QStringLiteral("^#((?:[a-f0-9]{2}){2,4}|[a-f0-9]{3})$"),
                                     QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(color);
  if (!m.hasMatch()) return std::nullopt;
  const QString hex = m.captured(1);
  bool ok = false;
  const quint64 dec = hex.toULongLong(&ok, 16);
  if (!ok) return std::nullopt;
  const int length = hex.length();
  const bool hasAlpha = length % 4 == 0;
  const bool isFullLength = length > 4;
  const int multiplier = isFullLength ? 1 : 17;
  const int bits = isFullLength ? 8 : 4;
  const int bitsOffset = hasAlpha ? 0 : -1;
  const quint64 mask = isFullLength ? 255 : 15;
  MermaidColor c;
  c.r = static_cast<qreal>(((dec >> (bits * (bitsOffset + 3))) & mask) * multiplier);
  c.g = static_cast<qreal>(((dec >> (bits * (bitsOffset + 2))) & mask) * multiplier);
  c.b = static_cast<qreal>(((dec >> (bits * (bitsOffset + 1))) & mask) * multiplier);
  c.a = hasAlpha ? static_cast<qreal>((dec & mask) * multiplier) / 255.0 : 1.0;
  c.color = color;
  return c;
}

qreal hue2deg(const QString& hue) {
  static const QRegularExpression hueRe(QStringLiteral("^(.+?)(deg|grad|rad|turn)$"),
                                        QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = hueRe.match(hue);
  if (m.hasMatch()) {
    const QString number = m.captured(1);
    const QString unit = m.captured(2).toLower();
    const qreal v = number.toDouble();
    if (unit == QStringLiteral("grad")) return clampH(v * 0.9);
    if (unit == QStringLiteral("rad")) return clampH(v * 180.0 / M_PI);
    if (unit == QStringLiteral("turn")) return clampH(v * 360.0);
  }
  return clampH(hue.toDouble());
}

std::optional<MermaidColor> parseHsl(const QString& color) {
  if (color.isEmpty()) return std::nullopt;
  const QChar c0 = color.at(0).toLower();
  if (c0 != QLatin1Char('h')) return std::nullopt;
  static const QRegularExpression re(
      QStringLiteral(
          "^hsla?\\s*\\(\\s*(-?(?:\\d+(?:\\.\\d+)?|(?:\\.\\d+))(?:e-?\\d+)?(?:deg|grad|rad|turn)?)\\s*(?:,|\\s)\\s*"
          "(-?(?:\\d+(?:\\.\\d+)?|(?:\\.\\d+))(?:e-?\\d+)?%)\\s*(?:,|\\s)\\s*"
          "(-?(?:\\d+(?:\\.\\d+)?|(?:\\.\\d+))(?:e-?\\d+)?%)"
          "(?:\\s*(?:,|/)\\s*\\+?(-?(?:\\d+(?:\\.\\d+)?|(?:\\.\\d+))(?:e-?\\d+)?(%)?))?\\s*\\)$"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(color);
  if (!m.hasMatch()) return std::nullopt;
  MermaidColor c;
  c.h = hue2deg(m.captured(1));
  c.s = clampS(m.captured(2).left(m.captured(2).length() - 1).toDouble());
  c.l = clampL(m.captured(3).left(m.captured(3).length() - 1).toDouble());
  const QString aStr = m.captured(4);
  if (!aStr.isEmpty()) {
    const bool isPct = !m.captured(5).isEmpty();
    const qreal av = aStr.left(aStr.length() - (isPct ? 1 : 0)).toDouble();
    c.a = clampA(isPct ? av / 100.0 : av);
  } else {
    c.a = 1.0;
  }
  c.color = color;
  return c;
}

std::optional<MermaidColor> parseRgb(const QString& color) {
  if (color.isEmpty()) return std::nullopt;
  const QChar c0 = color.at(0).toLower();
  if (c0 != QLatin1Char('r')) return std::nullopt;
  static const QRegularExpression re(
      QStringLiteral(
          "^rgba?\\s*\\(\\s*(-?(?:\\d+(?:\\.\\d+)?|(?:\\.\\d+))(?:e\\d+)?(%?))\\s*(?:,|\\s)\\s*"
          "(-?(?:\\d+(?:\\.\\d+)?|(?:\\.\\d+))(?:e\\d+)?(%?))\\s*(?:,|\\s)\\s*"
          "(-?(?:\\d+(?:\\.\\d+)?|(?:\\.\\d+))(?:e\\d+)?(%?))"
          "(?:\\s*(?:,|/)\\s*\\+?(-?(?:\\d+(?:\\.\\d+)?|(?:\\.\\d+))(?:e\\d+)?(%?)))?\\s*\\)$"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(color);
  if (!m.hasMatch()) return std::nullopt;
  auto chan = [](const QString& raw, const QString& pct) -> qreal {
    const bool isPct = !pct.isEmpty();
    const qreal v = raw.left(raw.length() - (isPct ? 1 : 0)).toDouble();
    return isPct ? v * 2.55 : v;
  };
  MermaidColor c;
  c.r = clampR(chan(m.captured(1), m.captured(2)));
  c.g = clampG(chan(m.captured(3), m.captured(4)));
  c.b = clampB(chan(m.captured(5), m.captured(6)));
  const QString aStr = m.captured(7);
  if (!aStr.isEmpty()) {
    const bool isPct = !m.captured(8).isEmpty();
    const qreal av = aStr.left(aStr.length() - (isPct ? 1 : 0)).toDouble();
    c.a = clampA(isPct ? av / 100.0 : av);
  } else {
    c.a = 1.0;
  }
  c.color = color;
  return c;
}

std::optional<MermaidColor> parseKeyword(const QString& color) {
  const QString lower = color.toLower();
  const auto& table = keywordColors();
  const auto it = table.constFind(lower);
  if (it == table.constEnd()) return std::nullopt;
  return parseHex(it.value());
}

}  // namespace

// --- MermaidColor lazy getters (mirror Channels getters) ---

void MermaidColor::ensureHSL() {
  // Populate missing h/s/l from r/g/b (RGB family must be present).
  const qreal r = this->r.value_or(0.0), g = this->g.value_or(0.0), b = this->b.value_or(0.0);
  if (!h.has_value()) h = rgb2hslChannel(r, g, b, QLatin1Char('h'));
  if (!s.has_value()) s = rgb2hslChannel(r, g, b, QLatin1Char('s'));
  if (!l.has_value()) l = rgb2hslChannel(r, g, b, QLatin1Char('l'));
}

void MermaidColor::ensureRGB() {
  // Populate missing r/g/b from h/s/l (HSL family must be present).
  const qreal h = this->h.value_or(0.0), s = this->s.value_or(0.0), l = this->l.value_or(0.0);
  if (!r.has_value()) r = hsl2rgbChannel(h, s, l, QLatin1Char('r'));
  if (!g.has_value()) g = hsl2rgbChannel(h, s, l, QLatin1Char('g'));
  if (!b.has_value()) b = hsl2rgbChannel(h, s, l, QLatin1Char('b'));
}

double MermaidColor::R() {
  if (type != Type::HSL && r.has_value()) return *r;
  ensureHSL();
  return hsl2rgbChannel(h.value_or(0.0), s.value_or(0.0), l.value_or(0.0), QLatin1Char('r'));
}
double MermaidColor::G() {
  if (type != Type::HSL && g.has_value()) return *g;
  ensureHSL();
  return hsl2rgbChannel(h.value_or(0.0), s.value_or(0.0), l.value_or(0.0), QLatin1Char('g'));
}
double MermaidColor::B() {
  if (type != Type::HSL && b.has_value()) return *b;
  ensureHSL();
  return hsl2rgbChannel(h.value_or(0.0), s.value_or(0.0), l.value_or(0.0), QLatin1Char('b'));
}
double MermaidColor::H() {
  if (type != Type::RGB && h.has_value()) return *h;
  ensureRGB();
  return rgb2hslChannel(r.value_or(0.0), g.value_or(0.0), b.value_or(0.0), QLatin1Char('h'));
}
double MermaidColor::S() {
  if (type != Type::RGB && s.has_value()) return *s;
  ensureRGB();
  return rgb2hslChannel(r.value_or(0.0), g.value_or(0.0), b.value_or(0.0), QLatin1Char('s'));
}
double MermaidColor::L() {
  if (type != Type::RGB && l.has_value()) return *l;
  ensureRGB();
  return rgb2hslChannel(r.value_or(0.0), g.value_or(0.0), b.value_or(0.0), QLatin1Char('l'));
}

// --- parse / stringify ---

MermaidColor parse(const QString& color) {
  if (const auto c = parseHex(color)) return *c;
  if (const auto c = parseRgb(color)) return *c;
  if (const auto c = parseHsl(color)) return *c;
  if (const auto c = parseKeyword(color)) return *c;
  // Unrecognized: leave as an opaque original-string round-trip (matches
  // khroma throwing, but we degrade gracefully so a bad theme value doesn't
  // crash the painter).
  MermaidColor c;
  c.color = color;
  c.a = 1.0;
  return c;
}

namespace {
QString stringifyHsl(const MermaidColor& c) {
  // Reads via the lazy getters (non-const in khroma); stringify is given a copy
  // already materialized by the caller's parse/operate cycle. Here we read the
  // stored h/s/l (populated) — for a type=HSL color these are authoritative.
  MermaidColor m = c;
  const QString h = numberToString(langRound(m.H()));
  const QString s = numberToString(langRound(m.S()));
  const QString l = numberToString(langRound(m.L()));
  if (m.a < 1.0)
    return QStringLiteral("hsla(%1, %2%, %3%, %4)").arg(h, s, l, numberToString(m.a));  // hsla uses raw a, not langRound(a)
  return QStringLiteral("hsl(%1, %2%, %3%)").arg(h, s, l);
}
QString stringifyRgb(const MermaidColor& c) {
  MermaidColor m = c;
  const QString r = numberToString(langRound(m.R()));
  const QString g = numberToString(langRound(m.G()));
  const QString b = numberToString(langRound(m.B()));
  if (m.a < 1.0)
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(r, g, b, numberToString(langRound(m.a)));
  return QStringLiteral("rgb(%1, %2, %3)").arg(r, g, b);
}
QString stringifyHex(const MermaidColor& c) {
  MermaidColor m = c;
  const QString r = dec2hex(static_cast<int>(std::round(m.R())));
  const QString g = dec2hex(static_cast<int>(std::round(m.G())));
  const QString b = dec2hex(static_cast<int>(std::round(m.B())));
  if (m.a < 1.0) {
    const QString a = dec2hex(static_cast<int>(std::round(m.a * 255.0)));
    return QStringLiteral("#%1%2%3%4").arg(r, g, b, a);
  }
  return QStringLiteral("#%1%2%3").arg(r, g, b);
}
}  // namespace

QString stringify(const MermaidColor& c) {
  // 4-branch decision (khroma Color.stringify):
  //  1. unchanged with original string -> round-trip verbatim
  //  2. type HSL or r undefined -> hsl()
  //  3. alpha<1 or non-integer rgb -> rgba()/rgb()
  //  4. else -> #hex
  if (!c.changed && !c.color.isEmpty()) return c.color;
  if (c.type == MermaidColor::Type::HSL || !c.r.has_value()) return stringifyHsl(c);
  MermaidColor m = c;
  const qreal r = m.R(), g = m.G(), b = m.B();
  const bool nonInteger = r != std::floor(r) || g != std::floor(g) || b != std::floor(b);
  if (c.a < 1.0 || nonInteger) return stringifyRgb(c);
  return stringifyHex(c);
}

// --- methods ---

QString change(const QString& color, const ChannelAdjust& ch) {
  MermaidColor m = parse(color);
  if (ch.h.has_value()) m.setH(clampH(*ch.h));
  if (ch.s.has_value()) m.setS(clampS(*ch.s));
  if (ch.l.has_value()) m.setL(clampL(*ch.l));
  if (ch.r.has_value()) m.setR(clampR(*ch.r));
  if (ch.g.has_value()) m.setG(clampG(*ch.g));
  if (ch.b.has_value()) m.setB(clampB(*ch.b));
  return stringify(m);
}

QString adjust(const QString& color, const ChannelAdjust& ch) {
  MermaidColor m = parse(color);
  ChannelAdjust changes;
  // khroma: `if (!channels[c]) continue;` — skip falsy (0). Present-but-zero
  // is skipped; absent optionals are skipped by has_value.
  if (ch.h.has_value() && *ch.h != 0.0) changes.h = m.H() + *ch.h;
  if (ch.s.has_value() && *ch.s != 0.0) changes.s = m.S() + *ch.s;
  if (ch.l.has_value() && *ch.l != 0.0) changes.l = m.L() + *ch.l;
  if (ch.r.has_value() && *ch.r != 0.0) changes.r = m.R() + *ch.r;
  if (ch.g.has_value() && *ch.g != 0.0) changes.g = m.G() + *ch.g;
  if (ch.b.has_value() && *ch.b != 0.0) changes.b = m.B() + *ch.b;
  return change(color, changes);
}

QString adjustChannel(const QString& color, QChar channel, double amount) {
  MermaidColor m = parse(color);
  double current = 0.0, next = 0.0;
  if (channel == QLatin1Char('h')) { current = m.H(); next = clampH(current + amount); if (current != next) m.setH(next); }
  else if (channel == QLatin1Char('s')) { current = m.S(); next = clampS(current + amount); if (current != next) m.setS(next); }
  else if (channel == QLatin1Char('l')) { current = m.L(); next = clampL(current + amount); if (current != next) m.setL(next); }
  else if (channel == QLatin1Char('a')) { current = m.a; next = clampA(current + amount); if (current != next) m.setA(next); }
  else if (channel == QLatin1Char('r')) { current = m.R(); next = clampR(current + amount); if (current != next) m.setR(next); }
  else if (channel == QLatin1Char('g')) { current = m.G(); next = clampG(current + amount); if (current != next) m.setG(next); }
  else if (channel == QLatin1Char('b')) { current = m.B(); next = clampB(current + amount); if (current != next) m.setB(next); }
  return stringify(m);
}

QString lighten(const QString& color, double amount) { return adjustChannel(color, QLatin1Char('l'), amount); }
QString darken(const QString& color, double amount) { return adjustChannel(color, QLatin1Char('l'), -amount); }
QString transparentize(const QString& color, double amount) { return adjustChannel(color, QLatin1Char('a'), -amount); }

// Internal mix operating on parsed colors (khroma's mix receives the inverse
// Channels directly when called from invert; the public mix parses strings).
QString mixChannels(const MermaidColor& c1, const MermaidColor& c2, double weight) {
  MermaidColor a = c1;
  MermaidColor b = c2;
  const qreal r1 = a.R(), g1 = a.G(), b1 = a.B(), a1 = a.A();
  const qreal r2 = b.R(), g2 = b.G(), b2 = b.B(), a2 = b.A();
  const qreal weightScale = weight / 100.0;
  const qreal weightNormalized = weightScale * 2.0 - 1.0;
  const qreal alphaDelta = a1 - a2;
  const qreal weight1combined = (weightNormalized * alphaDelta == -1.0)
      ? weightNormalized
      : (weightNormalized + alphaDelta) / (1.0 + weightNormalized * alphaDelta);
  const qreal w1 = (weight1combined + 1.0) / 2.0;
  const qreal w2 = 1.0 - w1;
  return rgba(r1 * w1 + r2 * w2, g1 * w1 + g2 * w2, b1 * w1 + b2 * w2,
              a1 * weightScale + a2 * (1.0 - weightScale));
}

QString mix(const QString& color1, const QString& color2, double weight) {
  return mixChannels(parse(color1), parse(color2), weight);
}

QString invert(const QString& color, double weight) {
  MermaidColor m = parse(color);
  // khroma: `inverse.r = 255 - r` etc. on the parsed object — the setters flip
  // it to RGB type. Build the inverse directly (RGB family, changed).
  MermaidColor inverse;
  inverse.r = 255.0 - m.R();
  inverse.g = 255.0 - m.G();
  inverse.b = 255.0 - m.B();
  inverse.a = m.a;
  inverse.changed = true;
  inverse.type = MermaidColor::Type::RGB;
  return mixChannels(inverse, m, weight);
}

double channel(const QString& color, QChar which) {
  MermaidColor m = parse(color);
  double v = 0.0;
  if (which == QLatin1Char('r')) v = m.R();
  else if (which == QLatin1Char('g')) v = m.G();
  else if (which == QLatin1Char('b')) v = m.B();
  else if (which == QLatin1Char('h')) v = m.H();
  else if (which == QLatin1Char('s')) v = m.S();
  else if (which == QLatin1Char('l')) v = m.L();
  else if (which == QLatin1Char('a')) v = m.a;
  return langRound(v);
}

bool isDark(const QString& color) {
  MermaidColor m = parse(color);
  auto toLinear = [](qreal c) {
    const qreal n = c / 255.0;
    return c > 0.03928 ? std::pow((n + 0.055) / 1.055, 2.4) : n / 12.92;
  };
  const qreal lum = 0.2126 * toLinear(m.R()) + 0.7152 * toLinear(m.G()) + 0.0722 * toLinear(m.B());
  return !(langRound(lum) >= 0.5);
}

QString mkBorder(const QString& col, bool darkMode) {
  // darkMode is always false for built-in themes, but the branch is faithful.
  return darkMode ? adjust(col, {.s = -40.0, .l = 10.0}) : adjust(col, {.s = -40.0, .l = -10.0});
}

QString rgba(double r, double g, double b, double a) {
  // khroma: reusable_default.set({r,g,b,a}) — populates data without touching
  // type/changed (set() resets type=ALL, changed=false, color=empty).
  MermaidColor m;
  m.r = clampR(r);
  m.g = clampG(g);
  m.b = clampB(b);
  m.a = clampA(a);
  return stringify(m);
}

QString rgba(const QString& color, double alpha) {
  // khroma: rgba(color, alpha) = change(color, {a: alpha}).
  MermaidColor m = parse(color);
  m.setA(clampA(alpha));
  return stringify(m);
}

QColor toQColor(const QString& color) {
  MermaidColor m = parse(color);
  QColor q;
  q.setRgbF(m.R() / 255.0, m.G() / 255.0, m.B() / 255.0, m.a);
  return q;
}

bool isParsableColor(const QString& color) {
  // parseHex/parseRgb/parseHsl/parseKeyword are file-local (anonymous
  // namespace) but visible anywhere in this TU. A recognized color matches at
  // least one; parse()'s fallback (opaque original-string round-trip) matches
  // none.
  return parseHex(color).has_value() || parseRgb(color).has_value() ||
         parseHsl(color).has_value() || parseKeyword(color).has_value();
}

}  // namespace muffin::mermaid::color
