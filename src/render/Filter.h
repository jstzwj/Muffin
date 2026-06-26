#pragma once

#include <QImage>

namespace muffin {

struct ThemeElementPaintStyle;

// CSS `filter:` applied to the element's background-box image. Operates in place on
// a Format_ARGB32_Premultiplied image: brightness/contrast/grayscale/sepia/
// hue-rotate (per-pixel colour matrix), then blur (boxBlur), then opacity. The
// defaults (brightness=contrast=1, rest=0/1) leave the image unchanged.
void applyElementFilter(QImage& image, const ThemeElementPaintStyle& style);
// CSS `backdrop-filter:` — same pipeline, applied to a sample of the backdrop
// (the content painted behind the box) the caller captured.
void applyElementBackdrop(QImage& image, const ThemeElementPaintStyle& style);

// True when the style declares any non-default filter / backdrop-filter (so callers
// can skip the offscreen render + composite when nothing would change).
bool hasElementFilter(const ThemeElementPaintStyle& style);
bool hasElementBackdrop(const ThemeElementPaintStyle& style);

}  // namespace muffin
