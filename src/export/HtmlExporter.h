#pragma once

#include <QString>

namespace muffin {

// Produces a complete, self-contained HTML document for an entire markdown
// source string. The body is rendered through SelectionSerializer (cmark-gfm —
// the same engine the editor uses), so the exported HTML matches what the app
// renders. `styled` embeds a compact, GitHub-ish stylesheet so the file is
// readable standalone; passing false wraps the bare cmark fragment with no
// styling (the "HTML (without Styles)" export option).
QString renderDocumentHtml(const QString& markdownSource, const QString& title, bool styled);

}  // namespace muffin
