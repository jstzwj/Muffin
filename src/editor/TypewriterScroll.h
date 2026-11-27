#pragma once

#include <QtMath>

#include <optional>

namespace muffin {

// Where typewriter mode lets the caret rest, measured from the top of the viewport in pixels.
//
// One band decides two things that have to agree: the scroll value that brings the caret back onto
// it, and how far the document may scroll past its first and last line. The caret can rest inside
// the band while it is on the document's first or last line only when the scroll range reaches
// `top` above the document and `viewportHeight - bottom` below it.
struct TypewriterBand {
  qreal top = 0.0;
  qreal bottom = 0.0;

  // Keep the caret on one line, in the middle of the viewport.
  static TypewriterBand centered(qreal viewportHeight) { return {viewportHeight / 2.0, viewportHeight / 2.0}; }

  // Let the caret move through the middle third of the viewport before the view follows it.
  static TypewriterBand comfort(qreal viewportHeight) { return {viewportHeight / 3.0, viewportHeight * 2.0 / 3.0}; }

  int overscrollAbove() const { return qMax(0, qFloor(top)); }
  int overscrollBelow(qreal viewportHeight) const { return qMax(0, qFloor(viewportHeight - bottom)); }

  // The scroll value that brings a caret centred at document Y `cursorY` into the band while the
  // view is scrolled to `scrollY`, clamped to [minimum, maximum]. nullopt once the caret already
  // rests in the band.
  //
  // The target is negative while the caret is less than `top` below the document start, so it is
  // reported as the value it is and not as a sentinel: a range without the overscroll clamps it
  // like any other unreachable position.
  std::optional<int> scrollTarget(qreal cursorY, qreal scrollY, int minimum, int maximum) const {
    const qreal onScreen = cursorY - scrollY;
    // The centered band has no width: half a pixel to either side still rests on its line.
    if (onScreen >= top - 0.5 && onScreen <= bottom + 0.5)
      return std::nullopt;
    return qBound(minimum, qRound(cursorY - (onScreen < top ? top : bottom)), maximum);
  }
};

} // namespace muffin
