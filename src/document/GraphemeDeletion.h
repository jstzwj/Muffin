#pragma once

#include <QString>
#include <QTextBoundaryFinder>

namespace muffin {

struct GraphemeDeletion {
  qsizetype start;
  qsizetype length;
};

// Text offsets are UTF-16 units, while Backspace/Delete operate on user-visible characters.
// A programmatically placed caret inside a cluster removes that whole cluster as well.
inline GraphemeDeletion graphemeDeletion(const QString& text, qsizetype offset, bool backward) {
  offset = qBound<qsizetype>(0, offset, text.size());
  if ((backward && offset == 0) || (!backward && offset == text.size())) {
    return {offset, 0};
  }
  QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
  finder.setPosition(offset);
  const bool boundary = finder.isAtBoundary();
  const qsizetype start = boundary && !backward ? offset : finder.toPreviousBoundary();
  finder.setPosition(offset);
  const qsizetype end = boundary && backward ? offset : finder.toNextBoundary();
  return {start, end - start};
}

}  // namespace muffin
