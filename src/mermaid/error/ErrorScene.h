#pragma once

// Native port of mermaid 11.16.0's "error" diagram (src/diagrams/error/
// errorRenderer.ts + errorDiagram.ts). Upstream registers it FIRST in
// addDiagrams(): it is the target of `Diagram.fromText("error")`, which is
// how mermaid.core's render() draws a fallback visual whenever parsing or
// drawing fails while `suppressErrorRendering` is false (the default; the
// key sits in the frontmatter sanitizer's secure set, so the Markdown source
// API cannot turn it off — only external initialize() can, which is outside
// Muffin's API).
//
// The renderer emits a fixed `viewBox="0 0 2412 512"` with six lightbulb
// `.error-icon` paths (fill errorBkgColor, stroke none by default) and two
// `.error-text` lines (fill AND stroke errorTextColor — SVG paints both
// channels, the 1px initial stroke slightly emboldening the glyphs):
// "Syntax error in text" at (1440, 250) at 150px and
// "mermaid version 11.16.0" at (1250, 400) at 100px, both anchored middle.
// `configureSvgSize(svg, 100, 512, true)` produces width=100% +
// max-width:512px with NO height attribute, so the replaced element's CSS
// box is 512 wide with the height taken from the viewBox aspect
// (512 * 512/2412 = 108.68325, LayoutUnit-floored to 1/64 =
// 108.671875 — the fixture's measured clientRect). The native scene
// canonicalizes to that quantized client box and paints the viewBox-space
// geometry under the 512/2412 scale; raster extents follow the browser
// nearest-pixel rule (108.68325 -> 109). The exported SVG root carries
// Muffin's client-box viewBox via svgClientViewBox() — "0 0 512 108.671875",
// so the intrinsic height matches the browser's replaced-element box exactly
// — while the immutable scene keeps the upstream viewBox geometry for the
// oracles.

#include "mermaid/MermaidScene.h"

#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

#include <cmath>

namespace muffin::mermaid::error {

struct ErrorTextStyle {
  // The base stylesheet's `#id{font-family;font-size;fill}` rule targets the
  // svg root, so both texts inherit the resolved theme font (the default
  // theme's stack resolves to trebuchet ms).
  QString fontFamily;
  QString errorBkgColor;
  QString errorTextColor;
};

// Per-element resolved CSS (themeCSS participates through the same cascade
// as every other family; upstream's base `.error-icon`/`.error-text` rules
// are the built-in sheet). The two text lines are separate DOM elements, so
// structural selectors can split them.
struct ErrorIconCss {
  QString fill;
  QString stroke;             // empty = none (the initial value)
  qreal strokeWidthPx = 1.0;  // SVG initial stroke-width
  bool visible = true;
  qreal opacity = 1.0;
};

struct ErrorTextCss {
  QString fill;
  QString stroke;
  qreal strokeWidthPx = 1.0;  // SVG initial stroke-width
  QString fontFamily;
  QString fontWeight;         // empty = 400
  qreal fontSize = -1.0;      // < 0 = presentation attribute value
  bool visible = true;
  qreal opacity = 1.0;
};

struct ErrorElementCss {
  // One entry per iconPaths element, index-aligned (empty = all defaults).
  // The six lightbulb paths are SIBLING <path> elements of the content
  // group, so structural selectors (`.error-icon:nth-of-type(2)`,
  // `.error-icon + .error-icon`, …) style them individually — folding them
  // into a single shared style would diverge from the browser for exactly
  // those legal rules.
  QVector<ErrorIconCss> icons;
  ErrorTextCss headline;
  ErrorTextCss version;
};

struct ErrorTextGeometry {
  QString text;
  QPointF anchor{0.0, 0.0};   // presentation x/y (y = alphabetic baseline)
  qreal fontSize = 0.0;       // presentation font-size in viewBox units
};

struct ErrorScene final : MermaidScene {
  QVector<QPainterPath> iconPaths;
  ErrorTextGeometry headline{QStringLiteral("Syntax error in text"),
                             QPointF(1440.0, 250.0), 150.0};
  ErrorTextGeometry version{QStringLiteral("mermaid version 11.16.0"),
                            QPointF(1250.0, 400.0), 100.0};
  ErrorTextStyle style;
  ErrorElementCss css;

  QRectF viewBoxBounds{0.0, 0.0, 2412.0, 512.0};
  // Client box: max-width 512 + viewBox aspect, LayoutUnit-floored to 1/64
  // (Chromium's replaced-element layout; the fixture's clientRect height).
  QRectF bounds{0.0, 0.0, 512.0,
                std::floor((512.0 * 512.0 / 2412.0) * 64.0) / 64.0};

  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  // SVG export keeps the fractional client box (108.671875) instead of the
  // raster-rounded 109 the integer canvas would write.
  QRectF svgClientViewBox() const override { return bounds; }
  bool roundRasterExtentToNearestPixel() const override { return true; }
};

// The six lightbulb paths verbatim from errorRenderer.ts, parsed once.
QStringList errorIconPathData();

ErrorScene buildErrorScene(ErrorTextStyle style);

}  // namespace muffin::mermaid::error
