#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

namespace muffin::image_syntax {

/// Recognized image syntax forms for a single image source snippet.
enum class Syntax { None, Markdown, Html };

/// Parsed view of a single image source snippet — the exact text spanned by one
/// image token in the markdown source (what EditorController::imageSourceRangeAtCursor
/// returns). Pure data; no document/editor dependency.
struct Image {
  Syntax syntax = Syntax::None;
  QString alt;            // alt text (markdown label or <img alt="...">)
  QString src;            // destination URL
  QString title;          // markdown title ("..."), if present
  QStringList otherAttrs; // HTML attribute names other than src/alt (Html only)
};

/// Parse a single image source snippet into a structured view. Returns a syntax
/// of None for anything that is not a recognizable markdown or HTML image.
Image parse(const QString& source);

/// Zoom percent encoded in a `style="zoom:N%"` declaration. Returns 100 when the
/// snippet carries no zoom (or an unparseable one) — i.e. the natural size.
int zoomPercent(const QString& source);

/// `zoomPercent() / 100.0`. Convenience for the renderer.
qreal zoomFactor(const QString& source);

/// Produce the new source for a single image after setting its zoom to `percent`.
///
/// Semantics (matches the "resize = edit style=\"zoom:N%;\"" model):
///  - A markdown image forced to a non-100 zoom is rewritten as an
///    `<img src="..." alt="..." style="zoom:N%;">` tag (markdown cannot encode zoom).
///  - An `<img>` tag has its `zoom` declaration added, replaced, or removed in place;
///    any other style declarations and attributes are preserved.
///  - `percent == 100` removes the zoom declaration but does not otherwise change the
///    syntax form (use toMarkdown() to convert an `<img>` back to markdown explicitly).
///
/// Returns the input unchanged when it is not a recognized image, or when nothing
/// would change.
QString setZoom(const QString& source, int percent);

/// Convert an `<img ...>` tag to standard markdown `![alt](src)`. Returns the input
/// unchanged for markdown sources or non-images.
QString toMarkdown(const QString& source);

/// Convert a markdown image `![alt](src)` to an HTML `<img src="..." alt="...">` tag.
/// Returns the input unchanged for HTML sources or non-images.
QString toHtml(const QString& source);

/// Location of the first `<img ...>` tag within `source`. Used to resolve the source range of a
/// standalone HTML-block image (cmark parses a line-leading `<img>` as an HtmlBlock, not inline, so
/// it has no inline projection to inspect). `start`/`end` are offsets within `source`; `found` is
/// false when no tag is present.
struct ImgTagLocation {
  bool found = false;
  qsizetype start = 0;
  qsizetype end = 0;
};
ImgTagLocation findImgTag(QStringView source);

}  // namespace muffin::image_syntax
