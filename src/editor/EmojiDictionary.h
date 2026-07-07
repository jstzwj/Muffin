#pragma once

#include <QHash>
#include <QString>
#include <QStringView>

namespace muffin {

// Process-wide, lazily-loaded map of GitHub-style emoji shortcodes (without the
// surrounding colons, e.g. "smile") to their glyphs, built once from the
// ":/emoji/emoji.txt" resource. Shared by the input completer (EmojiCompleter)
// and the render path (InlineProjection) so that `:smile:` renders as 😄 at
// display time WITHOUT touching the Markdown source — the same N:1
// source→display machinery as backslash escapes and HTML entities. Keeping the
// table here (rather than parsing the resource in each consumer) avoids two
// independent loads of the same data and gives both paths one source of truth.
const QHash<QString, QString>& emojiShortcodeMap();

// Length (in QChars) of a complete `:shortcode:` beginning at `source[i]`
// (`source[i]` must be ':'), or 0 if there is no closing ':' within range or the
// enclosed name is not a known shortcode. The name must be non-empty and consist
// of [A-Za-z0-9_+-] (the GFM shortcode charset). Bounded so a stray ':' in prose
// never scans far. Returns the whole token length (both colons + name) on a hit.
qsizetype emojiShortcodeLengthAt(QStringView source, qsizetype i);

}  // namespace muffin
