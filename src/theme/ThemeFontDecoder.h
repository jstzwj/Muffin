#pragma once
#include <QByteArray>

namespace muffin {
// Reconstruct SFNT from local WOFF/WOFF2 through FreeType's webfont decoder.
// The result is consumed by the native Qt font backend, including DirectWrite.
QByteArray decodeThemeWebFont(const QByteArray& source);
}  // namespace muffin
