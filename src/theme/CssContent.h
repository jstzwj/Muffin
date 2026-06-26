#pragma once

#include "theme/ThemeDefinition.h"

#include <QString>
#include <QVector>

#include <functional>
#include <vector>

namespace muffin {

// Format a 1-based counter value per a CSS list-style-type / counter() style
// (decimal, decimal-leading-zero, lower/upper-alpha & -latin, lower/upper-roman,
// lower-greek). Returns the raw value (no trailing punctuation).
QString formatCounterValue(int value, const QString& style);

// Split a CSS `content` value into literal / counter() / counters() tokens.
// `counter(name)` / `counter(name, style)` / `counters(name, "sep")` /
// `counters(name, "sep", style)` are recognized; everything else is a literal run.
std::vector<ContentToken> parseContentTokens(const QString& content);

// Resolve a token list to a string. `value(name)` returns the counter's current
// value; `chain(name)` returns its ancestor-chain values (outermost first) for
// `counters(name, sep)` (which joins them with the separator). Literal tokens are
// appended verbatim.
QString resolveContentTokens(const std::vector<ContentToken>& tokens,
                             const std::function<int(const QString&)>& value,
                             const std::function<QVector<int>(const QString&)>& chain);

}  // namespace muffin
