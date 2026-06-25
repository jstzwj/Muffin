#include "export/HtmlExporter.h"

#include "projection/SelectionSerializer.h"

namespace muffin {

namespace {

// Compact, GitHub-ish stylesheet for the "styled" export. Self-contained (no
// external fonts), respects a sensible reading width, and styles the elements
// cmark-gfm emits: headings, code, blockquotes, tables, task lists, images, hr.
const char* const kStyledCss = R"CSS(
:root { color-scheme: light dark; }
html { -webkit-text-size-adjust: 100%; }
body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Helvetica Neue", Arial, "Noto Sans", sans-serif;
  line-height: 1.6;
  max-width: 980px;
  margin: 0 auto;
  padding: 32px 24px 96px;
  color: #1f2328;
  background: #ffffff;
  word-wrap: break-word;
}
@media (prefers-color-scheme: dark) {
  body { color: #e6edf3; background: #0d1117; }
  a { color: #4493f8; }
  code, pre, kbd, samp { background: #161b22; }
  pre code { background: transparent; }
  blockquote { color: #9198a1; border-left-color: #30363d; }
  table tr { background: #0d1117; }
  table tr:nth-child(2n) { background: #161b22; }
  table td, table th { border-color: #30363d; }
  hr { background: #21262d; }
}
h1, h2, h3, h4, h5, h6 { line-height: 1.25; margin: 24px 0 16px; font-weight: 600; }
h1 { font-size: 2em; padding-bottom: .3em; border-bottom: 1px solid #d1d9e0b3; }
h2 { font-size: 1.5em; padding-bottom: .3em; border-bottom: 1px solid #d1d9e0b3; }
h3 { font-size: 1.25em; } h4 { font-size: 1em; } h5 { font-size: .875em; } h6 { font-size: .85em; color: #636c76; }
p { margin: 0 0 16px; }
a { color: #0969da; text-decoration: none; }
a:hover { text-decoration: underline; }
ul, ol { margin: 0 0 16px; padding-left: 2em; }
li + li { margin-top: .25em; }
li input[type="checkbox"] { margin: 0 .5em 0 -1.3em; vertical-align: middle; }
code, kbd, samp, pre, tt { font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace; font-size: .875em; }
code, kbd, samp, tt { background: rgba(175,184,193,.2); padding: .2em .35em; border-radius: 6px; }
pre { background: #f6f8fa; padding: 16px; overflow: auto; line-height: 1.45; border-radius: 8px; }
pre code { background: none; padding: 0; font-size: 1em; }
blockquote { margin: 0 0 16px; padding: 0 1em; color: #636c76; border-left: .25em solid #d1d9e0; }
blockquote > :first-child { margin-top: 0; } blockquote > :last-child { margin-bottom: 0; }
table { display: block; width: 100%; max-width: 100%; overflow: auto; border-collapse: collapse; margin: 0 0 16px; }
table th, table td { padding: 6px 13px; border: 1px solid #d1d9e0; }
table th { font-weight: 600; background: #f6f8fa; }
table tr { background: #ffffff; border-top: 1px solid #d1d9e0b3; }
table tr:nth-child(2n) { background: #f6f8fa; }
img { max-width: 100%; box-sizing: content-box; }
hr { height: .25em; padding: 0; margin: 24px 0; border: 0; background: #d1d9e0; }
)CSS";

QString escapeHtmlText(const QString& text) {
  QString out;
  out.reserve(text.size());
  for (const QChar ch : text) {
    switch (ch.unicode()) {
      case '&': out += QStringLiteral("&amp;"); break;
      case '<': out += QStringLiteral("&lt;"); break;
      case '>': out += QStringLiteral("&gt;"); break;
      default: out += ch;
    }
  }
  return out;
}

}  // namespace

QString renderDocumentHtml(const QString& markdownSource, const QString& title, bool styled,
                           const QString& themeCss) {
  const QString body = SelectionSerializer::renderMarkdownToHtml(markdownSource);

  const bool useTheme = styled && !themeCss.isEmpty();
  QString html;
  html.reserve(body.size() + (styled ? (useTheme ? themeCss.size() : 0) + 512 : 512));
  html += QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n");
  html += QStringLiteral("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>");
  html += escapeHtmlText(title);
  html += QStringLiteral("</title>\n");
  if (styled) {
    html += QStringLiteral("<style>\n");
    html += useTheme ? themeCss : QString::fromLatin1(kStyledCss);
    html += QStringLiteral("</style>\n");
  }
  html += QStringLiteral("</head>\n<body>\n");
  // Typora themes style #write (the centred content card: max-width/margin/
  // padding); wrap the body so a theme's layout rules apply. Harmless for the
  // built-in fallback, which targets <body> directly.
  if (styled) {
    html += QStringLiteral("<div id=\"write\">\n");
  }
  html += body;
  if (!body.endsWith(QLatin1Char('\n'))) {
    html += QLatin1Char('\n');
  }
  if (styled) {
    html += QStringLiteral("</div>\n");
  }
  html += QStringLiteral("</body>\n</html>\n");
  return html;
}

}  // namespace muffin
