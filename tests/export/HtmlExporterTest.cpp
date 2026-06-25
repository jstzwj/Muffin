#include "export/HtmlExporter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include <cstdlib>

// Verifies the native HTML export wrapper: it produces a complete, self-contained
// HTML document around SelectionSerializer's cmark-gfm fragment, embeds the
// stylesheet only for the styled variant, and HTML-escapes the <title>.
//
// Follows the project test convention (no QTest): require() asserts that exit on
// failure, plain test functions, main() under QCoreApplication.

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

void testStyledWrapsBodyAndEmbedsCss() {
  const QString html = muffin::renderDocumentHtml(QStringLiteral("# Hello"), QStringLiteral("Notes"), true);
  require(html.startsWith(QStringLiteral("<!DOCTYPE html>")), QStringLiteral("Missing doctype"));
  require(html.contains(QStringLiteral("<html")), QStringLiteral("Missing <html>"));
  require(html.contains(QStringLiteral("<title>Notes</title>")), QStringLiteral("Missing/incorrect title"));
  require(html.contains(QStringLiteral("<style>")), QStringLiteral("Styled export must embed <style>"));
  require(html.contains(QStringLiteral("max-width")), QStringLiteral("Styled export must contain body CSS"));
  require(html.contains(QStringLiteral("<h1")), QStringLiteral("Body must contain rendered <h1>"));
  require(html.contains(QStringLiteral("</body></html>")) || html.contains(QStringLiteral("</body>\n</html>")),
          QStringLiteral("Missing closing tags"));
}

void testPlainOmitsStylesheet() {
  const QString html = muffin::renderDocumentHtml(QStringLiteral("**bold**"), QStringLiteral("Doc"), false);
  require(!html.contains(QStringLiteral("<style>")), QStringLiteral("Plain export must not embed <style>"));
  require(html.contains(QStringLiteral("<strong>bold</strong>")), QStringLiteral("Body must contain rendered <strong>"));
}

void testTitleIsHtmlEscaped() {
  const QString html = muffin::renderDocumentHtml(QStringLiteral("x"), QStringLiteral("a < b & c > d"), true);
  require(html.contains(QStringLiteral("<title>a &lt; b &amp; c &gt; d</title>")),
          QStringLiteral("Title must be HTML-escaped"));
}

void testEmptyMarkdownStillWraps() {
  const QString html = muffin::renderDocumentHtml(QString(), QStringLiteral("Empty"), true);
  require(html.contains(QStringLiteral("<body>")), QStringLiteral("Empty doc still needs a body wrapper"));
  require(html.contains(QStringLiteral("<title>Empty</title>")), QStringLiteral("Empty doc needs its title"));
}

// When a theme CSS is supplied, it is embedded verbatim (replacing the fallback
// GitHub stylesheet) and the body is wrapped in <div id="write">, which Typora
// themes rely on for layout. themeCss is ignored for the plain (unstyled) export.
void testThemeCssEmbeddedAndWriteWrapped() {
  const QString themeCss =
      QStringLiteral("#write { max-width: 700px; margin: 0 auto; }\nbody { color: #123456; }");
  const QString html = muffin::renderDocumentHtml(QStringLiteral("# Hi"), QStringLiteral("T"), true, themeCss);
  require(html.contains(QStringLiteral("<style>")), QStringLiteral("Theme export must embed <style>"));
  require(html.contains(QStringLiteral("#123456")), QStringLiteral("Theme CSS must be embedded verbatim"));
  require(html.contains(QStringLiteral("<div id=\"write\">")),
          QStringLiteral("Theme export must wrap body in #write"));
  require(!html.contains(QStringLiteral("980px")),
          QStringLiteral("Theme export must not include the fallback GitHub stylesheet"));

  // themeCss must be ignored when styled is false.
  const QString plain = muffin::renderDocumentHtml(QStringLiteral("x"), QStringLiteral("T"), false, themeCss);
  require(!plain.contains(QStringLiteral("<style>")), QStringLiteral("Plain export must not embed theme CSS"));
  require(!plain.contains(QStringLiteral("<div id=\"write\">")),
          QStringLiteral("Plain export must not wrap in #write"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testStyledWrapsBodyAndEmbedsCss();
  testPlainOmitsStylesheet();
  testTitleIsHtmlEscaped();
  testEmptyMarkdownStillWraps();
  testThemeCssEmbeddedAndWriteWrapped();
  return 0;
}
