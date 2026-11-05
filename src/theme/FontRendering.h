#pragma once

#include <QFont>
#include <QHash>
#include <QStringList>

namespace muffin {
enum class TextBackend;
}
namespace muffin::font_rendering {

// Apply the platform screen-rasterization policy without changing any
// typographic property (family, size, weight, spacing, or kerning).
void configureForScreen(QFont& font);

// Application chrome uses one sans-serif stack and logical-pixel size. Document
// themes and the source editor retain their independent typography.
QFont uiFont();

// Author spacing remains separate from the platform's synthetic-bold advance
// correction. Reapplying this function is idempotent.
void configureCssFont(QFont& font, qreal letterSpacing, qreal wordSpacing);
void configureCssFont(QFont& font, qreal letterSpacing, qreal wordSpacing, TextBackend backend);

// Shared CSS family aliases, generic families and ordered fallback tail for
// Markdown, HTML and generated content. Refreshes when fonts are registered.
QString sansFamily();
QString serifFamily();
QString codeFamily();
// Source editing has its own platform monospace font, independent of themes.
QFont sourceEditorFont();
// Deterministic fallback tail shared by Markdown, HTML and Mermaid.  The
// bundled Noto faces are registered lazily, so CJK/Arabic/Hebrew shaping does
// not depend on whichever system font happens to be installed first.
QString documentFallbackTail();
// Descriptor keys select a single @font-face before handing it to the platform
// matcher, which may otherwise round CSS weights (for example 450) to 400.
QString fontFaceAliasKey(const QString& family, int weight, bool italic);
QStringList cssFamilyList(const QString& raw, const QString& platformTail, const QHash<QString, QString>& aliases = {},
                          int weight = 400, bool italic = false);

} // namespace muffin::font_rendering
