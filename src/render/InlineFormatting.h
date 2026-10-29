#pragma once

#include "render/RenderMetrics.h"

#include <QChar>
#include "render/TextLayout.h"
#include <QMarginsF>
#include <QTextCharFormat>
#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin {

// A generated inline box edge must reserve advance without introducing a word
// boundary or being discarded as trailing whitespace. It has no source text.
// A private-use character has a stable shaped advance. Default-ignorable
// WORD JOINER can report a missing-glyph advance in TextFontMetrics while Qt's
// layout strips that glyph, making width calibration font-dependent.
inline constexpr QChar kInlineBoxSpacer{0xe001};
// A replaced object is not trailing whitespace. Treating an image as a thin
// space made right/center alignment discard its reserved advance.
inline constexpr QChar kInlineObjectPlaceholder{0xe002};

// CSS owns the used font size. Qt's AlignSuper/SubScript silently scales it
// again (and rounds to integer points), so represent only the baseline shift.
inline qreal scriptBaselineShift(QTextCharFormat::VerticalAlignment alignment, const QFont& parent) {
  const qreal em = parent.pointSizeF() > 0 ? parent.pointSizeF() * 96. / 72. : parent.pixelSize();
  return alignment == QTextCharFormat::AlignSuperScript ? em * .35
         : alignment == QTextCharFormat::AlignSubScript ? -em * .2
                                                        : 0;
}
inline void applyScriptBaseline(QTextCharFormat& format, QTextCharFormat::VerticalAlignment alignment,
                                const QFont& parent) {
  if (alignment != QTextCharFormat::AlignSuperScript && alignment != QTextCharFormat::AlignSubScript)
    return;
  const qreal height = TextFontMetrics(format.font()).height();
  format.setVerticalAlignment(QTextCharFormat::AlignNormal);
  format.setBaselineOffset(100 * scriptBaselineShift(alignment, parent) / qMax<qreal>(.001, height));
}

// CSS line boxes and glyph ink are different geometries. Explicit line-height
// contributes half-leading, even when negative; replaced/atomic inline boxes
// contribute their actual baseline extents. Both document renderers use this.
struct InlineLineBox {
  qreal ascent = 0, descent = 0;
  qreal atomicDescent = -std::numeric_limits<qreal>::infinity();

  InlineLineBox(const QFont& font, qreal lineHeight) {
    const TextFontMetrics metrics(font);
    const qreal leading = (usedLineHeight(font, lineHeight) - metrics.height()) * .5;
    ascent = metrics.ascent() + leading;
    descent = metrics.descent() + leading;
  }

  static qreal usedLineHeight(const QFont& font, qreal specified) {
    return specified > 0 ? specified : std::ceil(TextFontMetrics(font).height() * kLineHeightFactor);
  }
  void includeText(const QFont& font, qreal lineHeight) {
    const InlineLineBox run(font, lineHeight);
    ascent = std::max(ascent, run.ascent);
    descent = std::max(descent, run.descent);
  }
  void includeShiftedText(const QFont& font, qreal lineHeight, qreal shift) {
    const InlineLineBox run(font, lineHeight);
    ascent = std::max(ascent, run.ascent + shift);
    descent = std::max(descent, run.descent - shift);
  }
  void includeAtomic(qreal baseline, qreal height, QMarginsF margin = {}) {
    ascent = std::max(ascent, baseline + margin.top());
    descent = std::max(descent, height - baseline + margin.bottom());
    atomicDescent = std::max(atomicDescent, height - baseline + margin.bottom());
  }
  void includeTopAligned(qreal boxHeight) {
    // A top-aligned atom grows the line below its top edge without moving the
    // baseline up as a baseline-aligned replaced box would.
    descent = std::max(descent, boxHeight - ascent);
  }
  void includeBottomAligned(qreal boxHeight) { ascent = std::max(ascent, boxHeight - descent); }
  qreal height() const { return ascent + descent; }
  qreal placeLine(TextLine line, qreal top) const {
    // Qt stores positions at 1/64px. Round the baseline inward so a replaced
    // box cannot escape above its allocated line due to truncation. Ordinary
    // CSS text keeps the specified line height rather than accumulating this
    // rounding adjustment on every line of a long document.
    const qreal requestedY = top + ascent - line.ascent();
    line.setPosition(QPointF(0, std::ceil(requestedY * 64) / 64));
    return std::max(height(), line.y() + line.ascent() + atomicDescent - top);
  }
};

} // namespace muffin
