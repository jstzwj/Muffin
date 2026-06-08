#pragma once

#include <QString>
#include <QtGlobal>

namespace muffin::math {

/**
 * Parse a CSS/TeX dimension string (e.g. "2em", "1.5pt", "-3mu")
 * and return the value in em units.
 *
 * The conversion table follows KaTeX's unit-to-em mapping.
 * Unknown units fall back to em (multiplier 1.0).
 * Returns 0.0 for unparseable strings.
 */
qreal sizeTextToEm(const QString& sizeText);

/**
 * Parse a KaTeX/TeX dimension string and return the value in Muffin layout
 * units.
 *
 * Relative units (em, ex, mu) are multiplied by fontPointSize. Absolute units
 * (pt, mm, cm, in, bp, pc, dd, cc, nd, nc, sp, px) use KaTeX's ptPerUnit
 * conversion table, then scale by absoluteReferencePointSize / 10 to mirror
 * KaTeX's ptPerUnit / ptPerEm behavior. px follows KaTeX/pdfTeX and defaults
 * to 1bp.
 *
 * Returns 0.0 for unparseable strings and unknown units.
 */
qreal dimensionToPoints(const QString& sizeText, qreal fontPointSize, qreal absoluteReferencePointSize = -1.0);

}  // namespace muffin::math
