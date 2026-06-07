#pragma once

#include <QString>
#include <QStringList>

namespace muffin {

struct InlineSplitMarker {
  QString open;
  QString close;
  qsizetype sourceStart = -1;
  qsizetype sourceEnd = -1;
};

inline InlineSplitMarker inlineSplitMarkerAt(const QString& source, qsizetype offset) {
  const QStringList markers = {
      QStringLiteral("**"),
      QStringLiteral("~~"),
      QStringLiteral("`"),
      QStringLiteral("$"),
      QStringLiteral("*"),
  };

  for (const QString& marker : markers) {
    const qsizetype open = source.lastIndexOf(marker, qMax<qsizetype>(0, offset - 1));
    if (open < 0) {
      continue;
    }
    const qsizetype contentStart = open + marker.size();
    if (offset <= contentStart) {
      continue;
    }

    const qsizetype close = source.indexOf(marker, qMax(contentStart, offset));
    if (close < 0 || offset >= close) {
      continue;
    }

    InlineSplitMarker split;
    split.open = marker;
    split.close = marker;
    split.sourceStart = open;
    split.sourceEnd = close + marker.size();
    return split;
  }
  return {};
}

/// Adjusts the split offset to avoid breaking inline marker pairs (e.g. splitting inside `**bold**`)
/// and trims whitespace around the split point.  May modify `source` in-place by removing one space.
inline qsizetype normalizeSplitOffset(QString& source, qsizetype offset) {
  offset = qBound<qsizetype>(0, offset, source.size());
  if (offset > 0 && offset < source.size()) {
    const QChar previous = source.at(offset - 1);
    const QChar next = source.at(offset);
    if ((previous == QLatin1Char('*') && next == QLatin1Char('*')) ||
        (previous == QLatin1Char('`') && next == QLatin1Char('`')) ||
        (previous == QLatin1Char('$') && next == QLatin1Char('$'))) {
      --offset;
    }
  }
  if (offset < source.size() && source.at(offset).isSpace()) {
    source.remove(offset, 1);
  } else if (offset > 0 && source.at(offset - 1).isSpace()) {
    source.remove(offset - 1, 1);
    --offset;
  }
  return offset;
}

/// Wraps `blockBreak` with inline marker close/open if the split point falls inside an inline span.
inline QString insertionWithInlineSplit(const QString& blockBreak, const QString& source, qsizetype offset) {
  const InlineSplitMarker inlineSplit = inlineSplitMarkerAt(source, offset);
  if (inlineSplit.open.isEmpty()) {
    return blockBreak;
  }
  return inlineSplit.close + blockBreak + inlineSplit.open;
}

}  // namespace muffin
