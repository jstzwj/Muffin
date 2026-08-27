#include "unicode/WordBoundary.h"

#include <unicode/brkiter.h>
#include <unicode/utypes.h>
#include <unicode/unistr.h>

#include <memory>

namespace muffin {

WordSegment findWordSegment(const QString& text, qsizetype offset) {
  if (text.isEmpty()) {
    return {};
  }

  offset = qBound<qsizetype>(0, offset, text.size());

  auto isWordChar = [](QChar ch) {
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
  };

  // Determine which character the cursor is "on".
  qsizetype checkPos = -1;
  if (offset < text.size() && isWordChar(text.at(offset))) {
    checkPos = offset;
  } else if (offset > 0 && isWordChar(text.at(offset - 1))) {
    checkPos = offset - 1;
  }
  if (checkPos < 0) {
    return {offset, offset, false};
  }

  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::BreakIterator> bi(
      icu::BreakIterator::createWordInstance(icu::Locale::getDefault(), status));
  if (U_FAILURE(status)) {
    return {offset, offset, false};
  }

  // QString is UTF-16; UChar is char16_t on Windows. Construct directly.
  const icu::UnicodeString ustr(
      reinterpret_cast<const UChar*>(text.utf16()),
      static_cast<int32_t>(text.size()));
  bi->setText(ustr);

  // Find the boundary at or before checkPos.
  int32_t segStart = bi->preceding(static_cast<int32_t>(checkPos + 1));
  if (segStart == icu::BreakIterator::DONE) {
    segStart = 0;
  }

  // Find the boundary after checkPos.
  int32_t segEnd = bi->following(static_cast<int32_t>(checkPos));
  if (segEnd == icu::BreakIterator::DONE) {
    segEnd = static_cast<int32_t>(text.size());
  }

  // ICU rule status tells whether the preceding segment is a word.
  const int32_t ruleStatus = bi->getRuleStatus();
  const bool isWord = (ruleStatus != UBRK_WORD_NONE);

  return {static_cast<qsizetype>(segStart),
          static_cast<qsizetype>(segEnd),
          isWord};
}

}  // namespace muffin
