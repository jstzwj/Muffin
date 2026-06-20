#pragma once

// Shared smart-punctuation (SmartyPants) helpers for the input level
// (InputController — writes converted chars into the source) and the render
// level (InlineProjection — display-only conversion). Header-only so both can
// include it without adding a translation unit or a cross-library dependency.

#include <QString>

namespace muffin::smartpunct {

// Whether a quote at this position should be an opening (left) quote: true when
// preceded by start-of-text, whitespace, or a non-word character. Mirrors the
// inline logic that previously lived in InputController::maybeConvertSmartPunctuation;
// the render path needs the identical rule so display conversion matches input.
inline bool isOpeningQuoteContext(QChar prev) {
  return prev.isNull() || prev.isSpace() || !prev.isLetterOrNumber();
}

// Unicode replacements as UTF-8 byte sequences. Shared so the input path (which
// persists them to the Markdown source) and the render path (display-only) emit
// the exact same code points.
inline const QString kLeftDoubleQuote = QStringLiteral("\xe2\x80\x9c");   // “
inline const QString kRightDoubleQuote = QStringLiteral("\xe2\x80\x9d");  // ”
inline const QString kLeftSingleQuote = QStringLiteral("\xe2\x80\x98");   // ‘
inline const QString kRightSingleQuote = QStringLiteral("\xe2\x80\x99");  // ’
inline const QString kEmDash = QStringLiteral("\xe2\x80\x94");            // —
inline const QString kEnDash = QStringLiteral("\xe2\x80\x93");            // –
inline const QString kEllipsis = QStringLiteral("\xe2\x80\xa6");          // …

}  // namespace muffin::smartpunct
