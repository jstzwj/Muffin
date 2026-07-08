#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

namespace muffin {

// Single source of truth for whether a URL may be emitted into HTML, shared by the on-screen
// render path (HtmlSanitizer) and the HTML export path (MarkdownHtmlSerializer). The two used to
// carry divergent, weaker-and-stronger copies; unifying closes gaps where one path blocked a
// vector the other let through (e.g. `data:image/svg` XSS, `java\tscript:` control-char hiding).
//
// Mirrors browser URL normalisation: C0 controls (U+0000–U+001F) and DEL (U+007F) are stripped
// before analysis, so "java\tscript:" / "java\nscript:" can't smuggle a scheme past the check.
//
// `allowDataImage`:
//   • image `src` (and the preview's URL attributes) accept `data:image/*` — Muffin embeds images
//     as data URIs. `data:image/svg*` is ALWAYS rejected (SVG can carry <script>).
//   • link `href` rejects ALL `data:` URIs (data:text/html is an XSS vector).
inline bool isSafeUrl(QStringView url, bool allowDataImage) noexcept {
  QString collapsed;
  collapsed.reserve(url.size());
  for (const QChar c : url) {
    const ushort code = c.unicode();
    if (code < 0x20 || code == 0x7F) { continue; }  // browsers strip C0 controls + DEL
    collapsed += c;
  }
  collapsed = collapsed.trimmed().toLower();
  if (collapsed.isEmpty()) { return true; }
  const QChar first = collapsed.at(0);
  if (first == QLatin1Char('#') || first == QLatin1Char('/') || first == QLatin1Char('?')) {
    return true;  // fragment / absolute / relative path
  }

  if (collapsed.startsWith(QStringLiteral("data:"))) {
    if (!allowDataImage) { return false; }                 // a link href never accepts data:
    if (collapsed.startsWith(QStringLiteral("data:image/svg"))) { return false; }
    return collapsed.startsWith(QStringLiteral("data:image/"));  // only raster image data URIs
  }

  static const QStringList safeSchemes = {QStringLiteral("http://"), QStringLiteral("https://"),
      QStringLiteral("mailto:"), QStringLiteral("tel:"), QStringLiteral("ftp://"),
      QStringLiteral("ftps://")};
  for (const QString& scheme : safeSchemes) {
    if (collapsed.startsWith(scheme)) { return true; }
  }

  // Any other explicit scheme (javascript:, vbscript:, file:, blob:, about:, ...) is unsafe.
  // A ':' before any non-scheme char (e.g. "foo: bar") means there is no real scheme, so treat
  // it as a relative URL.
  const int colon = collapsed.indexOf(QLatin1Char(':'));
  if (colon <= 0) { return true; }

  // Windows drive-letter absolute path ("C:/..." / "C:\..."): the single drive letter + colon
  // parses as a one-token "scheme", but it is a local file path, not a URL scheme — and not a
  // script vector — so treat it as safe. Without this, an <img src="C:/.../x.svg"> is rewritten
  // to "#" and never loads.
  if (colon == 1 && collapsed.size() > 2 &&
      (collapsed.at(2) == QLatin1Char('/') || collapsed.at(2) == QLatin1Char('\\'))) {
    return true;
  }

  for (int i = 0; i < colon; ++i) {
    const QChar c = collapsed.at(i);
    if (!(c.isLetterOrNumber() || c == QLatin1Char('+') || c == QLatin1Char('-') ||
          c == QLatin1Char('.'))) {
      return true;  // not a scheme token -> relative URL
    }
  }
  return false;
}

}  // namespace muffin
