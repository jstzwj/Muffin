#pragma once

#include <QFont>
#include <QHash>
#include <QStringList>

namespace muffin { enum class TextBackend; }
namespace muffin::font_rendering {

// Apply the platform screen-rasterization policy without changing any
// typographic property (family, size, weight, spacing, or kerning).
void configureForScreen(QFont& font);

// Author spacing remains separate from the platform's synthetic-bold advance
// correction. Reapplying this function is idempotent.
void configureCssFont(QFont& font, qreal letterSpacing, qreal wordSpacing);
void configureCssFont(QFont& font, qreal letterSpacing, qreal wordSpacing, TextBackend backend);

// Shared CSS family aliases, generic families and ordered fallback tail for
// Markdown, HTML and generated content. Refreshes when fonts are registered.
QString sansFamily();
QString serifFamily();
QString codeFamily();
// Deterministic fallback tail shared by Markdown, HTML and Mermaid.  The
// bundled Noto faces are registered lazily, so CJK/Arabic/Hebrew shaping does
// not depend on whichever system font happens to be installed first.
QString documentFallbackTail();
QStringList cssFamilyList(const QString& raw, const QString& platformTail, const QHash<QString, QString>& aliases = {});

}  // namespace muffin::font_rendering
