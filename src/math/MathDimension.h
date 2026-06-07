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

}  // namespace muffin::math
