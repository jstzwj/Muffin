#pragma once

#include "render/RenderMetrics.h"

#include <QChar>
#include <QFontMetricsF>
#include <QMarginsF>
#include <QTextLine>
#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin {

// A generated inline box edge must reserve advance without introducing a word
// boundary or being discarded as trailing whitespace. It has no source text.
// A private-use character has a stable shaped advance. Default-ignorable
// WORD JOINER can report a missing-glyph advance in QFontMetricsF while Qt's
// layout strips that glyph, making width calibration font-dependent.
inline constexpr QChar kInlineBoxSpacer{0xe001};

// CSS line boxes and glyph ink are different geometries. Explicit line-height
// contributes half-leading, even when negative; replaced/atomic inline boxes
// contribute their actual baseline extents. Both document renderers use this.
struct InlineLineBox {
  qreal ascent = 0, descent = 0;
  qreal atomicDescent = -std::numeric_limits<qreal>::infinity();

  InlineLineBox(const QFont& font, qreal lineHeight) {
    const QFontMetricsF metrics(font);
    const qreal leading = (usedLineHeight(font, lineHeight) - metrics.height()) * .5;
    ascent = metrics.ascent() + leading;
    descent = metrics.descent() + leading;
  }

  static qreal usedLineHeight(const QFont& font, qreal specified) {
    return specified > 0 ? specified : std::ceil(QFontMetricsF(font).height() * kLineHeightFactor);
  }
  void includeText(const QFont& font, qreal lineHeight) {
    const InlineLineBox run(font, lineHeight);
    ascent = std::max(ascent, run.ascent);
    descent = std::max(descent, run.descent);
  }
  void includeAtomic(qreal baseline, qreal height, QMarginsF margin = {}) {
    ascent = std::max(ascent, baseline + margin.top());
    descent = std::max(descent, height - baseline + margin.bottom());
    atomicDescent = std::max(atomicDescent, height - baseline + margin.bottom());
  }
  qreal height() const { return ascent + descent; }
  qreal placeLine(QTextLine line, qreal top) const {
    // Qt stores positions at 1/64px. Round the baseline inward so a replaced
    // box cannot escape above its allocated line due to truncation. Ordinary
    // CSS text keeps the specified line height rather than accumulating this
    // rounding adjustment on every line of a long document.
    const qreal requestedY = top + ascent - line.ascent();
    line.setPosition(QPointF(0, std::ceil(requestedY * 64) / 64));
    return std::max(height(), line.y() + line.ascent() + atomicDescent - top);
  }
};

}  // namespace muffin
