#pragma once

#include <QString>
#include <QStringView>
#include <QUrl>

namespace muffin {

class MarkdownNode;

struct MarkdownHtmlOptions {
  // cmark OPT_HARDBREAKS equivalent: render a single '\n' (soft break) as `<br />`.
  bool breakOnSingleNewline = true;
  // Decode `:shortcode:` emoji to glyphs in text content (mirrors the editor's renderEmoji setting).
  bool renderEmoji = true;
  // Final HTML document URL. Mermaid uses it when arrowMarkerAbsolute is true;
  // leave empty for fragments whose embedding location is not known yet.
  QUrl documentUrl;
};

// Serializes a fully-annotated MarkdownNode tree to cmark-gfm-compatible HTML, INCLUDING Muffin's
// custom inline features (`==highlight==` / `~subscript~` / `^superscript^` / `~~strike~~` / GitHub
// Alerts) that the editor renders but the old bare-cmark export path dropped. Driven by the editor's
// own CmarkGfmParser so the export tree is identical to the editor's — single source of truth, no
// parallel parser to keep in sync. Output mirrors cmark-gfm's HTML element-by-element (headings,
// code, tables, task lists, the patched `mfn-*-math` classes, …) so the styled-export CSS still
// applies, and replicates cmark's safe-URL omission (drops `javascript:`/`vbscript:`/`data:`).
class MarkdownHtmlSerializer {
public:
  // One-shot: reads QSettings (markdown/highlight, markdown/subscript, markdown/superscript,
  // markdown/renderEmoji, markdown/breakOnSingleNewline), parses `markdown` through CmarkGfmParser
  // (which runs splitDelimInlines + annotateAlertKinds + all post-parse passes), then serializes.
  // Used by SelectionSerializer::renderMarkdownToHtml (the single export chokepoint).
  static QString serializeSource(QStringView markdown);

  // Serialize an already-parsed Document tree. Exposed for unit testing (parse once, assert per
  // type) and so a future caller holding a live document tree can skip the re-parse.
  static QString serializeTree(const MarkdownNode& root, const MarkdownHtmlOptions& options);
};

}  // namespace muffin
