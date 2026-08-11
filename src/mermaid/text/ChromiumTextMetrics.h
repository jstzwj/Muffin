#pragma once

#include <QString>

#include <optional>

namespace muffin::mermaid::textmetrics {

// Shapes through the same OpenType tables as Chromium's HarfBuzz path and
// returns the CSS-pixel advance before device rasterization. This avoids
// DirectWrite/Qt hinting differences in geometry that SVG getBBox exposes.
std::optional<qreal> harfBuzzAdvance(const QString &text,
                                     const QString &cssFontFamilies,
                                     qreal fontSize);

} // namespace muffin::mermaid::textmetrics
