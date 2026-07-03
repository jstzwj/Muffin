#pragma once

#include <QtGlobal>  // qreal

namespace muffin {

// Per-line height multiplier applied to QFontMetricsF::height() for the fallback/estimated line
// height when a theme declares no explicit line-spacing. Shared by BlockLayout (literal/code line
// layout), BlockLayoutBuilder (estimate path) and InlineLayout (wrapped-line fallback). The sites
// MUST stay in lock-step or the reserved height and the painted text disagree — single-sourced so
// a tuning change is one edit.
constexpr qreal kLineHeightFactor = 1.16;

}  // namespace muffin
