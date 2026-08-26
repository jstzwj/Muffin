#pragma once

#include <QString>
#include <QStringView>
#include <QtGlobal>

#include <utility>

// Word-boundary navigation shared by the rendered-mode input controller and the source-mode
// editor. Programmer-word semantics (letter/number/underscore), with surrogate-pair stepping so
// non-BMP characters are consumed as one unit. Templated over the text container so both the
// PieceTable virtual sequence and plain QString/QStringView use one implementation and can never
// diverge.
namespace muffin::words {

inline bool isWordCharacter(QChar ch) {
  return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

// Text must provide qsizetype size() const and QChar at(qsizetype) const
// (PieceTable, QString, QStringView all qualify).

template <typename Text>
qsizetype previousCharacterOffset(const Text& text, qsizetype offset) {
  offset = qBound<qsizetype>(0, offset, text.size());
  if (offset == 0) return 0;
  --offset;
  if (text.at(offset).isLowSurrogate() && offset > 0 && text.at(offset - 1).isHighSurrogate()) {
    --offset;
  }
  return offset;
}

template <typename Text>
qsizetype nextCharacterOffset(const Text& text, qsizetype offset) {
  offset = qBound<qsizetype>(0, offset, text.size());
  if (offset >= text.size()) return text.size();
  if (text.at(offset).isHighSurrogate() && offset + 1 < text.size() &&
      text.at(offset + 1).isLowSurrogate()) {
    return offset + 2;
  }
  return offset + 1;
}

template <typename Text>
qsizetype previousWordOffset(const Text& text, qsizetype offset) {
  offset = qBound<qsizetype>(0, offset, text.size());
  while (offset > 0 && !isWordCharacter(text.at(previousCharacterOffset(text, offset)))) {
    offset = previousCharacterOffset(text, offset);
  }
  while (offset > 0 && isWordCharacter(text.at(previousCharacterOffset(text, offset)))) {
    offset = previousCharacterOffset(text, offset);
  }
  return offset;
}

template <typename Text>
qsizetype nextWordOffset(const Text& text, qsizetype offset) {
  offset = qBound<qsizetype>(0, offset, text.size());
  while (offset < text.size() && isWordCharacter(text.at(offset))) {
    offset = nextCharacterOffset(text, offset);
  }
  while (offset < text.size() && !isWordCharacter(text.at(offset))) {
    offset = nextCharacterOffset(text, offset);
  }
  return offset;
}

// Extent of the word containing `position`, clamped to [boundStart, boundEnd). Positions on a
// non-word character (or outside the bounds) collapse to {position, position}.
template <typename Text>
std::pair<qsizetype, qsizetype> wordRangeAt(
    const Text& text, qsizetype position, qsizetype boundStart, qsizetype boundEnd) {
  const qsizetype bounded = qBound<qsizetype>(boundStart, position, boundEnd);
  qsizetype probe = bounded;
  if (probe == boundEnd && probe > boundStart) --probe;
  if (probe < boundStart || probe >= boundEnd || !isWordCharacter(text.at(probe))) {
    return {bounded, bounded};
  }
  qsizetype start = probe;
  qsizetype end = probe + 1;
  while (start > boundStart && isWordCharacter(text.at(start - 1))) --start;
  while (end < boundEnd && isWordCharacter(text.at(end))) ++end;
  return {start, end};
}

}  // namespace muffin::words
