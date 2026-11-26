#pragma once

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QString>
#include <QVector>

namespace muffin::mermaid::scene {

// The common 11.16 sheet applies the theme's dropShadow variable as a
// `filter:` on the neo look's nodes (`[data-look="neo"].node rect`,
// `.node circle`, `.node .outer-path`): a Gaussian blur of the element's
// silhouette, offset and in a color of its own.
struct NeoShadow {
  bool valid = false;
  qreal dx = 0.0, dy = 0.0, sigma = 0.0;
  QColor tint;
};

// A theme's dropShadow variable. One written as `url(#drop-shadow)` keeps flat
// semantics: nothing casts a shadow.
NeoShadow parseNeoShadow(const QString& css);

// A resolved `filter:` as the painter reads a shadow of it: the
// `url(#…-drop-shadow)` the redux themes carry becomes the filter the
// renderer defines for it, a `drop-shadow(4px 4px 0px rgba(…, 0.06))` in
// white over a theme whose name contains "dark" and black over the others.
// Any other value is returned as it is.
QString dropShadowText(const QString& filter, bool darkTheme);

// Channel composite alpha for a shape as the filter sees it: each paint
// channel contributes its region at its resolved alpha (color alpha x
// fill-/stroke-opacity x element opacity), source-over combined where the
// regions overlap. A channel that paints nothing contributes nothing.
struct ShadowChannels {
  qreal fillAlpha = 0.0;
  qreal strokeAlpha = 0.0;
  bool paints() const { return fillAlpha > 0.0 || strokeAlpha > 0.0; }
};

// One region contribution to the filter input: a path, its stroke width, and
// the per-channel alphas of ITS OWN rendering. A filtered group is several
// contributions source-over combined in ONE mask.
struct ShadowPart {
  QPainterPath path;
  qreal strokeWidth = 0.0;
  ShadowChannels channels;
};

// Paints the element silhouette blurred by `shadow.sigma`, offset by
// (dx, dy), in `shadow.tint`: the raster equivalent of filter: drop-shadow().
// When nothing paints, the filter output is empty, as in the browser.
void paintNeoShadow(QPainter& painter, const QVector<ShadowPart>& parts, const NeoShadow& shadow);

} // namespace muffin::mermaid::scene
