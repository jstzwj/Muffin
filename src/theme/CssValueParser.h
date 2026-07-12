#pragma once

#include "theme/ThemeDefinition.h"  // GradientSpec / GradientStop (parseGradientSpec return)

#include <QChar>
#include <QColor>
#include <QHash>
#include <QLoggingCategory>
#include <QMarginsF>
#include <QString>
#include <QStringList>

namespace muffin {

// Theme-resolution warnings (e.g. a colour value that failed every parse strategy and would
// otherwise silently render default/black). Default-off (QtCriticalMsg threshold): enable on
// demand with QT_LOGGING_RULES="muffin.theme.warn=true" when chasing a black/missing render.
// Defined in CssValueParser.cpp.
Q_DECLARE_LOGGING_CATEGORY(themeWarn)

// CSS px ↔ pt conversion (96 px = 72 pt). Inline one-liners; shared by length resolution here
// and by heading-size round-tripping (ptToPx) in CssThemeMapper.
inline qreal pxToPt(qreal px) { return px * 72.0 / 96.0; }
inline qreal ptToPx(qreal pt) { return pt * 96.0 / 72.0; }

// The CSS root em default (browser default font-size = 16px). Used wherever a length must be
// resolved without a configured root/body size: the rem fallback, the body-size default, and the
// default emPx handed to lengthToPx when there is no inherited context. Single-sourced so a
// future root-default change is one edit.
constexpr qreal kRootEmPx = 16.0;

// Parse a CSS colour literal with the CORRECT alpha byte order. CSS specifies #RRGGBBAA
// (alpha LAST); Qt's QColor(QString) reads 8-hex as #AARRGGBB (alpha FIRST), which silently
// misreads every alpha-bearing hex colour (e.g. #7aeaf018 → yellow-green instead of pale cyan).
// This is the shared chokepoint: the theme engine (CssThemeMapper) and the HTML-block
// inline-style parser (HtmlBoxBuilder) both route hex colours through here so the fix applies
// once. For non-hex values it falls back to QColor(literal) (named colours, #RGB, #RRGGBB).
inline QColor cssColor(const QString& literal) {
  const QString s = literal.trimmed();
  if (s.size() == 9 && s.at(0) == QLatin1Char('#')) {
    bool ok = false;
    const quint32 v = s.mid(1).toUInt(&ok, 16);  // toUInt validates the whole string
    if (ok) {
      return QColor((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
    }
  } else if (s.size() == 5 && s.at(0) == QLatin1Char('#')) {
    // CSS #RGBA (alpha last) == #RRGGBBAA with each digit doubled.
    const QChar hash('#');
    const QString doubled = hash + QString(s.at(1)) + QString(s.at(1)) + QString(s.at(2)) + QString(s.at(2)) +
        QString(s.at(3)) + QString(s.at(3)) + QString(s.at(4)) + QString(s.at(4));
    return cssColor(doubled);
  }
  return QColor(literal);
}

// Split a CSS shorthand value into top-level space-separated tokens, respecting parens
// (rgb(...), calc(...), var(...)) and string literals. Shared by box/border/shadow/mask
// shorthand parsing. Defined in CssValueParser.cpp.
QStringList splitTopLevelSpaces(const QString& text);

// Resolve a CSS colour value (var()/color-mix()/rgb()/rgba()/hsl()/hsla()/#hex/named) → QColor.
// The full funnel every theme colour read routes through; returns invalid for unresolvable
// input (and logs the offending value to themeWarn when that category is enabled).
QColor extractColor(const QString& value, const QHash<QString, QString>& vars);

// CSS length → pixels. Accepts CSS absolute units, em/rem/%/calc()/bare-number. `rem` resolves against
// rootPx (the root html font, 16px default — NOT the local em, which is the historic
// 1.5×-too-big bug); `%` resolves against containingPx when supplied (pseudo width/height)
// else against emPx. Returns 0 for unset/auto/unrecognised. Defined in CssValueParser.cpp.
qreal lengthToPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx = 16.0,
                 qreal rootPx = -1.0, qreal containingPx = -1.0);

// CSS length → points (font-size resolution). Same unit handling as lengthToPx but expressed
// in points for ThemeTypography's *SizePt fields. Defined in CssValueParser.cpp.
qreal lengthToPt(const QString& value, const QHash<QString, QString>& vars, qreal emPx = 16.0);

// Resolve a CSS box shorthand (margin/padding, 1-4 space-separated values) to pixel margins.
// Defined in CssValueParser.cpp.
QMarginsF boxToMarginsPx(const QString& value, const QHash<QString, QString>& vars,
                         qreal emPx = 16.0, qreal rootPx = -1.0);

// Width of the first positive length in a border shorthand (e.g. `1px solid #d0d7de` → 1.0).
// Defined in CssValueParser.cpp.
qreal borderWidthPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx = 16.0);

// Blur radius from a `box-shadow` shorthand: the 3rd length token, or 8px when only offset +
// colour are present, or 0 otherwise. Colour tokens (rgba(...)) are skipped. Shared by the
// hover-glow extractor and the computed-style element builder. Defined in CssValueParser.cpp.
qreal shadowBlurPx(const QString& shadowRaw, const QHash<QString, QString>& vars);

// Parse linear-gradient(...)/radial-gradient(...)/conic-gradient(...) → a rect-independent
// GradientSpec (GradientPainter builds a QGradient per target rect). Stop colours resolve
// through extractColor — the same path as every other theme colour. Defined in CssValueParser.cpp.
GradientSpec parseGradientSpec(const QString& raw, const QHash<QString, QString>& vars);

}  // namespace muffin
