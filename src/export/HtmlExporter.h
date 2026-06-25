#pragma once

#include <QString>

namespace muffin {

// Produces a complete, self-contained HTML document for an entire markdown
// source string. The body is rendered through SelectionSerializer (cmark-gfm —
// the same engine the editor uses), so the exported HTML matches what the app
// renders. `styled` embeds a stylesheet so the file is readable standalone;
// passing false wraps the bare cmark fragment with no styling (the "HTML
// (without Styles)" export option). When `styled` and `themeCss` is non-empty,
// that CSS is embedded instead of the built-in GitHub-ish fallback and the body
// is wrapped in <div id="write"> (Typora themes style #write), so the export
// carries the active theme. `themeCss` is ignored when `styled` is false.
QString renderDocumentHtml(const QString& markdownSource, const QString& title, bool styled,
                           const QString& themeCss = {});

}  // namespace muffin
