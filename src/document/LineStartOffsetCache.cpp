#include "document/LineStartOffsetCache.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

#include <algorithm>

namespace muffin {

namespace {

Q_LOGGING_CATEGORY(lineOffsetPerf, "muffin.perf", QtWarningMsg)

qint64 g_byteColNs = 0;
bool g_byteColPerf = false;

struct ByteColGuard {
  qint64& bucket;
  QElapsedTimer timer;
  explicit ByteColGuard(qint64& b) : bucket(b) {
    if (g_byteColPerf) {
      timer.start();
    }
  }
  ~ByteColGuard() {
    if (g_byteColPerf) {
      bucket += timer.nsecsElapsed();
    }
  }
};

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

void LineStartOffsetCache::applyEdit(
    qsizetype sourceStart, qsizetype removedLen, qsizetype insertedLen, QStringView fullPostEditText) {
  if (sourceStart < 0 || removedLen < 0 || sourceStart + removedLen > textSize_) {
    return;
  }
  const qsizetype sourceEnd = sourceStart + removedLen;
  const qsizetype editDelta = insertedLen - removedLen;

  // All offsets below are pre-edit until winEndPost. lineStarts_/textSize_ still describe the
  // pre-edit text here; textSize_ is refreshed at the end.
  const int firstLine = lineForOffset(sourceStart);      // index of first entry > sourceStart
  const int suffixStartIdx = lineForOffset(sourceEnd);   // index of first entry > sourceEnd
  const qsizetype winStartPre = lineStarts_.at(firstLine - 1);

  const bool hasSuffix = suffixStartIdx < lineStarts_.size();
  const qsizetype winEndPre = hasSuffix ? lineStarts_.at(suffixStartIdx) : textSize_;
  const qsizetype winEndPost = winEndPre + editDelta;

  QVector<qsizetype> result;
  result.reserve(lineStarts_.size() + 8);

  // 1. Prefix: line starts strictly inside the line that contains sourceStart are unchanged
  //    (their preceding '\n' is before sourceStart, never touched by the edit).
  for (int i = 0; i < firstLine; ++i) {
    result.push_back(lineStarts_.at(i));
  }

  // 2. Rescan the affected window of the post-edit text. A '\n' at offset p starts a line at p+1.
  //    Skip the boundary line-start that the shifted suffix already provides (winEndPost).
  const qsizetype scanEnd = qBound(winStartPre, winEndPost, fullPostEditText.size());
  for (qsizetype p = winStartPre; p < scanEnd; ++p) {
    if (fullPostEditText.at(p) == QLatin1Char('\n')) {
      const qsizetype lineStart = p + 1;
      if (hasSuffix && lineStart == winEndPost) {
        continue;
      }
      result.push_back(lineStart);
    }
  }

  // 3. Suffix: line starts whose value is strictly greater than sourceEnd survived the edit
  //    (their preceding '\n' is outside the removed range); shift them by editDelta.
  for (int i = suffixStartIdx; i < lineStarts_.size(); ++i) {
    result.push_back(lineStarts_.at(i) + editDelta);
  }

  lineStarts_ = std::move(result);
  textSize_ = fullPostEditText.size();
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

qsizetype LineStartOffsetCache::offsetForLineByteColumn(QStringView text, int line, int column) const {
  ByteColGuard guard(g_byteColNs);
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
    const uint ucs4 = text.at(offset).isHighSurrogate() && offset + 1 < end && text.at(offset + 1).isLowSurrogate()
                          ? QChar::surrogateToUcs4(text.at(offset), text.at(offset + 1))
                          : text.at(offset).unicode();
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

void LineStartOffsetCache::setByteColPerfEnabled(bool enabled) {
  g_byteColPerf = enabled;
}

void LineStartOffsetCache::resetByteColPerf() {
  g_byteColNs = 0;
}

qreal LineStartOffsetCache::byteColPerfMs() {
  return g_byteColNs / 1000000.0;
}

}  // namespace muffin
