#pragma once

#include <QString>
#include <QtGlobal>

namespace muffin {

struct WordSegment {
  qsizetype start = 0;
  qsizetype end = 0;
  bool isWord = false;
};

// Finds the word segment at or adjacent to `offset` in `text` using ICU
// BreakIterator with dictionary-based CJK word segmentation.
//
// `offset` is a character index (0-based). The function first checks the
// character at `offset`, then at `offset - 1` if the first is not a word
// character. Returns {start, end, isWord} where [start, end) is the segment
// range and isWord is true if the segment is a word (letter, number, CJK).
//
// Declared only: the ICU implementation lives in WordBoundary.cpp inside
// MuffinUi, so consumers in other binaries (Muffin.exe) link through the
// library instead of instantiating ICU symbols themselves — under the SHARED
// library build that keeps the ICU archives out of every consumer.
WordSegment findWordSegment(const QString& text, qsizetype offset);

}  // namespace muffin
