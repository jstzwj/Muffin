#pragma once
#include <QtTypes>

namespace muffin {
// A reading anchor survives a new AST, wrapping and theme changes.
struct ViewportPosition {
  qsizetype sourceOffset = -1;
  qreal screenOffset = 0;
  int horizontal = 0;
  bool textAnchor = false;
  bool atStart = false;
};
} // namespace muffin
