#include "editor/WordBoundary.h"

#include "../TestUtils.h"

#include <QString>
#include <QStringView>

#include <cstdio>

namespace {

using muffin::words::nextWordOffset;
using muffin::words::previousWordOffset;
using muffin::words::wordRangeAt;

void testAsciiWords() {
  const QString text = QStringLiteral("hello world_foo bar");
  // "hello| world" — stop between words
  require(previousWordOffset(text, 5) == 0, "previous word from end of 'hello' → 0");
  require(nextWordOffset(text, 0) == 6, "next word from 0 → start of 'world_foo'");
  // Underscore is a word character: world_foo is one word, so the next stop is 'bar'
  require(nextWordOffset(text, 6) == 16, "underscore glues into one programmer word");
  require(previousWordOffset(text, 16) == 6, "previous word from start of 'bar' → 'world_foo' start");
  // Next from end of text stays clamped
  require(nextWordOffset(text, text.size()) == text.size(), "next word clamps at end");
  require(previousWordOffset(text, 0) == 0, "previous word clamps at 0");
}

void testPunctuationRuns() {
  const QString text = QStringLiteral("foo...  bar");
  // Skipping punctuation and spaces forward lands on 'bar'
  require(nextWordOffset(text, 0) == 8, "punctuation+space run skipped forward");
  // And back again crosses the same run
  require(previousWordOffset(text, 8) == 0, "punctuation+space run skipped backward");
  // Caret inside the punctuation run skips it entirely in both directions
  require(nextWordOffset(text, 4) == 8, "from inside punctuation run forward");
  require(previousWordOffset(text, 4) == 0, "from inside punctuation run backward");
}

void testCjkRuns() {
  // CJK characters are letters — one long run, then a latin word.
  const QString text = QStringLiteral("你好世界 abc");
  require(nextWordOffset(text, 0) == 5, "CJK run consumed as one word");
  require(previousWordOffset(text, 5) == 0, "back over the CJK run");
  require(nextWordOffset(text, 5) == text.size(), "latin tail consumed");
}

void testSurrogatePairs() {
  // Non-BMP emoji (U+1F600) is not a word character, but surrogate halves must still be stepped
  // as a unit so offsets never land mid-pair.
  const QString text = QStringLiteral("a😀b");
  require(text.size() == 4, "emoji occupies two UTF-16 units");
  // nextWordOffset(0): skip word 'a' → 1; skip non-word emoji pair → 3, start of 'b'.
  require(nextWordOffset(text, 0) == 3, "emoji pair skipped whole, lands on 'b'");
  require(previousWordOffset(text, 3) == 0, "back over emoji pair without stopping inside it");
  // Supplementary-plane Han (U+20000) is a letter, but QChar::isLetterOrNumber judges the UTF-16
  // unit and surrogate halves are Cs — so the pair acts as an atomic non-word separator. This
  // matches source-mode behavior exactly (unit-level semantics, pair never split).
  const QString han = QStringLiteral("x𠀀y");
  require(han.size() == 4, "supplementary Han occupies two UTF-16 units");
  require(nextWordOffset(han, 0) == 3, "supplementary Han skipped atomically, lands on 'y'");
  require(previousWordOffset(han, 3) == 0, "back over supplementary Han without stopping inside it");
}

void testWordRangeAt() {
  const QString text = QStringLiteral("foo bar baz");
  require(wordRangeAt(text, 0, 0, text.size()).first == 0, "range start at word head");
  require(wordRangeAt(text, 1, 0, text.size()).second == 3, "range inside first word");
  require(wordRangeAt(text, 5, 0, text.size()).first == 4, "range inside second word");
  const auto onSpace = wordRangeAt(text, 3, 0, text.size());
  require(onSpace.first == 3 && onSpace.second == 3, "position on space collapses");
  const auto atEnd = wordRangeAt(text, text.size(), 0, text.size());
  require(atEnd.first == 8 && atEnd.second == 11, "end-of-text probes last word");
}

void testQStringViewInstantiation() {
  // The render-mode caller works on QStringView over a block's content text.
  const QString text = QStringLiteral("alpha beta");
  const QStringView view(text);
  require(nextWordOffset(view, 0) == 6, "QStringView overload works");
  require(previousWordOffset(view, 6) == 0, "QStringView previous works");
}

void testEmptyAndSingle() {
  const QString empty;
  require(nextWordOffset(empty, 0) == 0, "empty text next");
  require(previousWordOffset(empty, 0) == 0, "empty text previous");
  const QString single = QStringLiteral("x");
  require(nextWordOffset(single, 0) == 1, "single word char next");
  require(previousWordOffset(single, 1) == 0, "single word char previous");
  const QString nonWord = QStringLiteral(" ");
  require(nextWordOffset(nonWord, 0) == 1, "single non-word char next");
}

}  // namespace

int main() {
  runTest("asciiWords", testAsciiWords);
  runTest("punctuationRuns", testPunctuationRuns);
  runTest("cjkRuns", testCjkRuns);
  runTest("surrogatePairs", testSurrogatePairs);
  runTest("wordRangeAt", testWordRangeAt);
  runTest("qStringViewInstantiation", testQStringViewInstantiation);
  runTest("emptyAndSingle", testEmptyAndSingle);
  qInfo("All word-boundary tests passed.");
  return 0;
}
