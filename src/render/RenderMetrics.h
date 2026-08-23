#pragma once

#include <QtGlobal>  // qreal

namespace muffin {

// Per-line height multiplier applied to QFontMetricsF::height() for the fallback/estimated line
// height when a theme declares no explicit line-spacing. Shared by BlockLayout (literal/code line
// layout), BlockLayoutBuilder (estimate path) and InlineLayout (wrapped-line fallback). The sites
// MUST stay in lock-step or the reserved height and the painted text disagree — single-sourced so
// a tuning change is one edit.
constexpr qreal kLineHeightFactor = 1.16;

// CSS `line-height: N` multiplies the font's CSS PIXEL size (pt → px at 96/72), not the platform
// metrics line box. The pt fallback (12) matches a default-constructed QFont. Single-sourced so
// the estimate path (BlockLayoutBuilder) and the painted path (InlineLayout) resolve the same
// number — a drift here desyncs reserved height from painted text.
inline qreal cssLineHeightPx(qreal pointSizeF, qreal multiplier) {
  const qreal pointSize = pointSizeF > 0.0 ? pointSizeF : 12.0;
  return pointSize * (96.0 / 72.0) * multiplier;
}

}  // namespace muffin
