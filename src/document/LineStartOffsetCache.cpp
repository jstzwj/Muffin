#include "document/LineStartOffsetCache.h"

#include <algorithm>

namespace muffin {

namespace {

qsizetype utf8ByteLength(uint ucs4) {
  if (ucs4 <= 0x7F) {
    return 1;
  }
  if (ucs4 <= 0x7FF) {
    return 2;
  }
  if (ucs4 <= 0xFFFF) {
    return 3;
  }
  return 4;
}

}  // namespace

LineStartOffsetCache::LineStartOffsetCache() {
  rebuild(QStringView());
}

LineStartOffsetCache::LineStartOffsetCache(QStringView text) {
  rebuild(text);
}

void LineStartOffsetCache::rebuild(QStringView text) {
  text_ = text.toString();
  textSize_ = text.size();
  lineStarts_.clear();
  lineStarts_.reserve(qMax<qsizetype>(1, text.size() / 48));
  lineStarts_.push_back(0);
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text.at(i) == QLatin1Char('\n')) {
      lineStarts_.push_back(i + 1);
    }
  }
}

qsizetype LineStartOffsetCache::offsetForLineColumn(int line, int column) const {
  if (line <= 0 || column <= 0 || line > lineStarts_.size()) {
    return -1;
  }
  const qsizetype start = lineStarts_.at(line - 1);
  const qsizetype end = lineEndOffset(line);
  if (end < start) {
    return -1;
  }
  return qMin(start + column - 1, end);
}

qsizetype LineStartOffsetCache::offsetForLineByteColumn(int line, int column) const {
  if (line <= 0 || column <= 0 || line > lineStarts_.size()) {
    return -1;
  }
  const qsizetype start = lineStarts_.at(line - 1);
  const qsizetype end = lineEndOffset(line);
  if (end < start) {
    return -1;
  }

  qsizetype offset = start;
  qsizetype byteColumn = 1;
  while (offset < end && byteColumn < column) {
    const uint ucs4 = text_.at(offset).isHighSurrogate() && offset + 1 < end && text_.at(offset + 1).isLowSurrogate()
                          ? QChar::surrogateToUcs4(text_.at(offset), text_.at(offset + 1))
                          : text_.at(offset).unicode();
    const qsizetype nextByteColumn = byteColumn + utf8ByteLength(ucs4);
    if (column < nextByteColumn) {
      break;
    }
    byteColumn = nextByteColumn;
    offset += ucs4 > 0xFFFF ? 2 : 1;
  }
  return qMin(offset, end);
}

qsizetype LineStartOffsetCache::lineEndOffset(int line) const {
  if (line <= 0 || line > lineStarts_.size()) {
    return -1;
  }
  if (line < lineStarts_.size()) {
    return qMax<qsizetype>(0, lineStarts_.at(line) - 1);
  }
  return textSize_;
}

int LineStartOffsetCache::lineForOffset(qsizetype offset) const {
  if (lineStarts_.isEmpty()) {
    return 0;
  }
  const qsizetype bounded = qBound<qsizetype>(0, offset, textSize_);
  const auto it = std::upper_bound(lineStarts_.cbegin(), lineStarts_.cend(), bounded);
  return static_cast<int>(it - lineStarts_.cbegin());
}

int LineStartOffsetCache::lineCount() const {
  return static_cast<int>(lineStarts_.size());
}

}  // namespace muffin
