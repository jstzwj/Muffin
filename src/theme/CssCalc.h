#pragma once

#include <QString>

namespace muffin {

// Evaluate a CSS `calc(<expr>)` expression to pixels. Supports `+ - * /`, nested
// parentheses, and per-term units (px/pt/em/rem/%). `emPx` resolves em, `rootPx`
// resolves rem (defaults to emPx), `containingPx` resolves `%` (defaults to emPx).
// Lenient: CSS requires consistent units on +/- and a dimensionless operand on
// `*`/`/`, but every operand is resolved to px first and combined numerically.
// Returns 0.0 for any expression it cannot fully parse (callers treat 0 as
// "unset"). `expression` is the calc() argument with vars already resolved.
qreal evalCalcPx(const QString& expression, qreal emPx, qreal rootPx, qreal containingPx);

}  // namespace muffin
