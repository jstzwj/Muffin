#pragma once

// Export targets surfaced by the File → Export menu. PDF and the two HTML
// variants are produced natively (the app's own DocumentLayout renderer for
// PDF; SelectionSerializer / cmark-gfm for HTML); the rest are delegated to an
// external Pandoc process (see PandocRunner). Image uses the native renderer;
// Command delegates to a user-configured executable.

namespace muffin {

enum class ExportFormat {
  Pdf,
  Html,
  HtmlPlain,
  Image,
  Docx,
  Odt,
  Rtf,
  Epub,
  Latex,
  MediaWiki,
  Rst,
  Textile,
  Opml,
  Command,
};

} // namespace muffin
