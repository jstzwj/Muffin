#pragma once

// Export targets surfaced by the File → Export menu. PDF and the two HTML
// variants are produced natively (the app's own DocumentLayout renderer for
// PDF; SelectionSerializer / cmark-gfm for HTML); the rest are delegated to an
// external Pandoc process (see PandocRunner). "Image" export (render each page
// to a pixmap) is intentionally deferred — it needs a page-raster pass the
// renderer does not currently expose.

namespace muffin {

enum class ExportFormat {
  Pdf,
  Html,
  HtmlPlain,
  Docx,
  Odt,
  Rtf,
  Epub,
  Latex,
  MediaWiki,
  Rst,
  Textile,
  Opml,
};

}  // namespace muffin
