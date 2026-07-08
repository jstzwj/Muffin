#include "projection/MarkdownHtmlSerializer.h"

#include "document/MarkdownNode.h"
#include "editor/EmojiDictionary.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownParser.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QString>
#include <QStringView>

#include <cstdio>
#include <cstdlib>

// Verifies MarkdownHtmlSerializer (the export path's MarkdownNode->HTML serializer) against
// cmark-gfm's HTML output element-by-element, and — the point of P0-2 — that Muffin's custom
// inline features (==highlight== / ~sub~ / ^sup^ / GitHub Alerts / emoji) now survive export.
//
// Project test convention (no QTest): require() asserts that exit on failure, plain test
// functions, main() under QCoreApplication. Most cases drive serializeTree with explicit options
// (deterministic, no QSettings); one smoke test exercises serializeSource's QSettings wiring.

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
    std::fflush(stderr);
    std::exit(1);
  }
}

// Parse `md` through the editor's parser and serialize to HTML. Default options mirror the export
// defaults (all GFM ext + alertBox on; highlight/sub/sup OFF unless callers opt in).
QString serialize(QStringView md, muffin::ParseOptions opts = {}, muffin::MarkdownHtmlOptions hopts = {}) {
  muffin::CmarkGfmParser parser;
  muffin::ParseResult result = parser.parseDocument(md, opts);
  require(result.root != nullptr, QStringLiteral("parse should produce a root for: ") + md.toString());
  return muffin::MarkdownHtmlSerializer::serializeTree(*result.root, hopts);
}

muffin::ParseOptions withHighlight() {
  muffin::ParseOptions o;
  o.enableHighlight = true;
  return o;
}
muffin::ParseOptions withSubSup() {
  muffin::ParseOptions o;
  o.enableSubscript = true;
  o.enableSuperscript = true;
  return o;
}

// --- blocks ---
void testHeading() {
  require(serialize(QStringLiteral("# Hi")) == QStringLiteral("<h1>Hi</h1>\n"), QStringLiteral("h1 exact"));
  require(serialize(QStringLiteral("## H2\n### H3")) == QStringLiteral("<h2>H2</h2>\n<h3>H3</h3>\n"),
          QStringLiteral("heading levels"));
}
void testParagraph() {
  require(serialize(QStringLiteral("Hello")) == QStringLiteral("<p>Hello</p>\n"), QStringLiteral("paragraph"));
}
void testThematicBreak() {
  const QString out = serialize(QStringLiteral("a\n\n---\n\nb"));
  require(out.contains(QStringLiteral("<hr />\n")), QStringLiteral("thematic break"));
  require(out.contains(QStringLiteral("<p>a</p>")) && out.contains(QStringLiteral("<p>b</p>")),
          QStringLiteral("paragraphs around hr"));
}
void testCodeFenceWithLang() {
  const QString out = serialize(QStringLiteral("```cpp\nint x;\n```"));
  require(out.contains(QStringLiteral("<pre><code class=\"language-cpp\">")), QStringLiteral("code class"));
  require(out.contains(QStringLiteral("int x;")), QStringLiteral("code content"));
}
void testCodeFenceWithoutLang() {
  const QString out = serialize(QStringLiteral("```\ncode\n```"));
  require(out.contains(QStringLiteral("<pre><code>")), QStringLiteral("code without lang has no class"));
  require(!out.contains(QStringLiteral("language-")), QStringLiteral("no language class when absent"));
}
void testIndentedCode() {
  const QString out = serialize(QStringLiteral("    indented"));
  require(out.contains(QStringLiteral("<pre><code>")) && out.contains(QStringLiteral("indented")),
          QStringLiteral("indented code block"));
}
void testHtmlBlockRaw() {
  const QString out = serialize(QStringLiteral("<div>raw</div>"));
  require(out.contains(QStringLiteral("<div>raw</div>")), QStringLiteral("html block raw passthrough"));
}
void testBlockQuotePlain() {
  require(serialize(QStringLiteral("> quote")) == QStringLiteral("<blockquote>\n<p>quote</p>\n</blockquote>\n"),
          QStringLiteral("plain blockquote"));
}

// --- GitHub Alerts (the headline fix) ---
void testAlertNote() {
  const QString out = serialize(QStringLiteral("> [!NOTE]\n> body"));
  require(out.contains(QStringLiteral("class=\"markdown-alert markdown-alert-note\"")), QStringLiteral("alert class"));
  require(out.contains(QStringLiteral("<p class=\"markdown-alert-title\">Note</p>")), QStringLiteral("alert title"));
  require(out.contains(QStringLiteral("<p>body</p>")), QStringLiteral("alert body present"));
  require(!out.contains(QStringLiteral("[!NOTE]")), QStringLiteral("alert marker must be stripped"));
}
void testAlertAllKinds() {
  struct Case { QLatin1String marker; QLatin1String cls; QLatin1String title; };
  const Case cases[] = {
      {QLatin1String("[!NOTE]"), QLatin1String("note"), QLatin1String("Note")},
      {QLatin1String("[!TIP]"), QLatin1String("tip"), QLatin1String("Tip")},
      {QLatin1String("[!IMPORTANT]"), QLatin1String("important"), QLatin1String("Important")},
      {QLatin1String("[!WARNING]"), QLatin1String("warning"), QLatin1String("Warning")},
      {QLatin1String("[!CAUTION]"), QLatin1String("caution"), QLatin1String("Caution")},
  };
  for (const Case& c : cases) {
    const QString md = QStringLiteral("> ") + c.marker + QStringLiteral("\n> body");
    const QString out = serialize(md);
    require(out.contains(QStringLiteral("markdown-alert-") + c.cls), c.cls + QStringLiteral(" class"));
    require(out.contains(QStringLiteral("<p class=\"markdown-alert-title\">") + c.title + QStringLiteral("</p>")),
            c.cls + QStringLiteral(" title"));
    require(!out.contains(c.marker), c.cls + QStringLiteral(" marker stripped"));
  }
}
void testAlertMarkerOnlyNoBody() {
  const QString out = serialize(QStringLiteral("> [!NOTE]"));
  require(out.contains(QStringLiteral("markdown-alert-note")), QStringLiteral("marker-only still an alert"));
  require(out.contains(QStringLiteral("markdown-alert-title")), QStringLiteral("marker-only has title"));
  require(!out.contains(QStringLiteral("[!NOTE]")), QStringLiteral("marker stripped when no body"));
  require(!out.contains(QStringLiteral("<p></p>")), QStringLiteral("no empty paragraph emitted"));
}
void testNonAlertBlockquoteNoAlertClass() {
  const QString out = serialize(QStringLiteral("> plain"));
  require(!out.contains(QStringLiteral("markdown-alert")), QStringLiteral("plain quote has no alert class"));
}

// --- custom delimited inlines (the other headline fix) ---
void testHighlight() {
  require(serialize(QStringLiteral("==x=="), withHighlight()) == QStringLiteral("<p><mark>x</mark></p>\n"),
          QStringLiteral("highlight renders as <mark> when enabled"));
}
void testHighlightDefaultOff() {
  const QString out = serialize(QStringLiteral("==x=="));  // enableHighlight defaults false
  require(out.contains(QStringLiteral("==x==")), QStringLiteral("highlight stays literal when disabled"));
  require(!out.contains(QStringLiteral("<mark>")), QStringLiteral("no <mark> when disabled"));
}
void testSubscriptSuperscript() {
  require(serialize(QStringLiteral("~x~"), withSubSup()) == QStringLiteral("<p><sub>x</sub></p>\n"),
          QStringLiteral("subscript renders as <sub>"));
  require(serialize(QStringLiteral("^x^"), withSubSup()) == QStringLiteral("<p><sup>x</sup></p>\n"),
          QStringLiteral("superscript renders as <sup>"));
}
void testStrikethrough() {
  require(serialize(QStringLiteral("~~x~~")) == QStringLiteral("<p><del>x</del></p>\n"),
          QStringLiteral("strikethrough renders as <del>"));
}
void testEmphasisStrongCode() {
  require(serialize(QStringLiteral("*i*")) == QStringLiteral("<p><em>i</em></p>\n"), QStringLiteral("emphasis"));
  require(serialize(QStringLiteral("**b**")) == QStringLiteral("<p><strong>b</strong></p>\n"), QStringLiteral("strong"));
  require(serialize(QStringLiteral("`c`")) == QStringLiteral("<p><code>c</code></p>\n"), QStringLiteral("inline code"));
}
void testInlineMath() {
  const QString out = serialize(QStringLiteral("$x$"));
  require(out.contains(QStringLiteral("<span class=\"mfn-inline-math\" data-tex=\"x\">x</span>")),
          QStringLiteral("inline math patched class + data-tex"));
}
void testMathBlock() {
  const QString out = serialize(QStringLiteral("$$\ny=x\n$$"));
  require(out.contains(QStringLiteral("mfn-math-block")), QStringLiteral("math block class"));
  require(out.contains(QStringLiteral("y=x")), QStringLiteral("math block tex"));
}

// --- links / images / autolinks ---
void testLink() {
  require(serialize(QStringLiteral("[t](http://x)")) == QStringLiteral("<p><a href=\"http://x\">t</a></p>\n"),
          QStringLiteral("link without title"));
  const QString withTitle = serialize(QStringLiteral("[t](http://x \"T\")"));
  require(withTitle.contains(QStringLiteral("<a href=\"http://x\" title=\"T\">t</a>")), QStringLiteral("link with title"));
}
void testImage() {
  const QString out = serialize(QStringLiteral("![alt](http://x)"));
  require(out.contains(QStringLiteral("<img src=\"http://x\" alt=\"alt\" />")), QStringLiteral("image without title"));
  const QString withTitle = serialize(QStringLiteral("![alt](http://x \"T\")"));
  require(withTitle.contains(QStringLiteral("title=\"T\"")), QStringLiteral("image with title"));
}
void testAutolink() {
  const QString out = serialize(QStringLiteral("<http://x>"));
  require(out.contains(QStringLiteral("<a href=\"http://x\">http://x</a>")), QStringLiteral("autolink"));
}

// --- breaks ---
void testSoftBreakHardbreak() {
  require(serialize(QStringLiteral("a\nb")) == QStringLiteral("<p>a<br />\nb</p>\n"),
          QStringLiteral("soft break as <br> when breakOnSingleNewline"));
}
void testSoftBreakNewline() {
  muffin::MarkdownHtmlOptions hopts;
  hopts.breakOnSingleNewline = false;
  require(serialize(QStringLiteral("a\nb"), {}, hopts) == QStringLiteral("<p>a\nb</p>\n"),
          QStringLiteral("soft break as newline when break off"));
}
void testLineBreak() {
  require(serialize(QStringLiteral("a  \nb")) == QStringLiteral("<p>a<br />\nb</p>\n"), QStringLiteral("hard line break"));
}
void testHtmlInline() {
  const QString out = serialize(QStringLiteral("text <b>bold</b> more"));
  require(out.contains(QStringLiteral("<b>bold</b>")), QStringLiteral("html inline raw passthrough"));
}

// --- footnotes ---
void testFootnoteReference() {
  const QString out = serialize(QStringLiteral("text[^1]\n\n[^1]: note"));
  require(out.contains(QStringLiteral("<sup class=\"footnote-ref\"><a href=\"#fn:1\">1</a></sup>")),
          QStringLiteral("footnote ref as superscript link"));
  require(out.contains(QStringLiteral("<section class=\"footnotes\">")), QStringLiteral("footnotes hoisted to section"));
  require(out.contains(QStringLiteral("id=\"fn:1\"")), QStringLiteral("footnote def anchor"));
  require(out.contains(QStringLiteral("<p>note</p>")), QStringLiteral("footnote def body"));
}

// --- lists ---
void testTightList() {
  require(serialize(QStringLiteral("- a\n- b")) == QStringLiteral("<ul>\n<li>a</li>\n<li>b</li>\n</ul>\n"),
          QStringLiteral("tight list has no <p>"));
}
void testLooseList() {
  const QString out = serialize(QStringLiteral("- a\n\n- b"));
  require(out.contains(QStringLiteral("<p>a</p>")) && out.contains(QStringLiteral("<p>b</p>")),
          QStringLiteral("loose list wraps items in <p>"));
}
void testOrderedListStart() {
  require(serialize(QStringLiteral("1. a")) == QStringLiteral("<ol>\n<li>a</li>\n</ol>\n"),
          QStringLiteral("ordered list start=1 omits start attr"));
  require(serialize(QStringLiteral("3. a")).contains(QStringLiteral("<ol start=\"3\">")),
          QStringLiteral("ordered list start=3 emits start attr"));
}
void testTaskList() {
  require(serialize(QStringLiteral("- [ ] x")).contains(QStringLiteral("<input type=\"checkbox\" disabled=\"\" /> ")),
          QStringLiteral("unchecked tasklist checkbox"));
  require(serialize(QStringLiteral("- [x] x")).contains(
              QStringLiteral("<input type=\"checkbox\" checked=\"\" disabled=\"\" /> ")),
          QStringLiteral("checked tasklist checkbox"));
}
void testNestedList() {
  const QString out = serialize(QStringLiteral("- a\n  - b"));
  require(out.count(QStringLiteral("<ul>")) >= 2, QStringLiteral("nested list has two <ul>"));
}

// --- tables ---
void testTableAlignments() {
  const QString out = serialize(QStringLiteral("| a | b | c | d |\n|:--|:-:|--:|---|\n| 1 | 2 | 3 | 4 |"));
  require(out.contains(QStringLiteral("<thead>")), QStringLiteral("table thead"));
  require(out.contains(QStringLiteral("<tbody>")), QStringLiteral("table tbody"));
  require(out.contains(QStringLiteral("<th align=\"left\">a</th>")), QStringLiteral("left align"));
  require(out.contains(QStringLiteral("<th align=\"center\">b</th>")), QStringLiteral("center align"));
  require(out.contains(QStringLiteral("<th align=\"right\">c</th>")), QStringLiteral("right align"));
  require(out.contains(QStringLiteral("<th>d</th>")), QStringLiteral("default align omits attr"));
  require(out.contains(QStringLiteral("<td align=\"left\">1</td>")), QStringLiteral("body cell inherits column align"));
}

// --- security: dangerous URLs dropped (cmark safe-default parity) ---
void testJavascriptLinkDropped() {
  const QString out = serialize(QStringLiteral("[x](javascript:alert(1))"));
  require(out.contains(QStringLiteral("<p>x</p>")), QStringLiteral("unsafe link emits label text only"));
  require(!out.contains(QStringLiteral("javascript")), QStringLiteral("javascript scheme must not appear"));
  require(!out.contains(QStringLiteral("href=")), QStringLiteral("unsafe link has no href"));
}
void testDataImage() {
  const QString dropped = serialize(QStringLiteral("![x](data:text/html,bad)"));
  require(!dropped.contains(QStringLiteral("<img")), QStringLiteral("non-image data: URI dropped"));
  const QString allowed = serialize(QStringLiteral("![alt](data:image/png;base64,AA==)"));
  require(allowed.contains(QStringLiteral("<img src=\"data:image/png")), QStringLiteral("data:image/ allowed"));
}

// --- emoji parity (bonus) ---
void testEmoji() {
  // Compare against the map's glyph (a proper QString) rather than hardcoded UTF-8 bytes: QStringLiteral
  // interprets `\xNN` byte escapes per-platform (UTF-8 on MSVC, individual chars on GCC), which made a
  // byte-literal expectation fail under Linux ASan. Both the serializer and this lookup use the same map.
  const QString smile = muffin::emojiShortcodeMap().value(QStringLiteral("smile"));
  require(!smile.isEmpty(), QStringLiteral(":smile: must be in the emoji map"));
  const QString on = serialize(QStringLiteral(":smile:"));
  require(on.contains(smile), QStringLiteral("emoji shortcode decodes when renderEmoji on"));
  require(!on.contains(QStringLiteral(":smile:")), QStringLiteral("shortcode fully decoded when renderEmoji on"));
  muffin::MarkdownHtmlOptions hopts;
  hopts.renderEmoji = false;
  require(serialize(QStringLiteral(":smile:"), {}, hopts).contains(QStringLiteral(":smile:")),
          QStringLiteral("emoji stays literal when renderEmoji off"));
}

// --- serializeSource wiring (QSettings -> ParseOptions) ---
void testSerializeSourceReadsSettings() {
  QSettings s;
  s.setValue(QStringLiteral("markdown/highlight"), true);
  s.setValue(QStringLiteral("markdown/renderEmoji"), false);
  const QString out = muffin::MarkdownHtmlSerializer::serializeSource(QStringLiteral("==x== :smile:"));
  require(out.contains(QStringLiteral("<mark>x</mark>")), QStringLiteral("serializeSource honors markdown/highlight"));
  require(out.contains(QStringLiteral(":smile:")), QStringLiteral("serializeSource honors markdown/renderEmoji=off"));
  s.setValue(QStringLiteral("markdown/highlight"), false);
  s.setValue(QStringLiteral("markdown/renderEmoji"), true);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("MuffinTest"));
  QCoreApplication app(argc, argv);

  testHeading();
  testParagraph();
  testThematicBreak();
  testCodeFenceWithLang();
  testCodeFenceWithoutLang();
  testIndentedCode();
  testHtmlBlockRaw();
  testBlockQuotePlain();
  testAlertNote();
  testAlertAllKinds();
  testAlertMarkerOnlyNoBody();
  testNonAlertBlockquoteNoAlertClass();
  testHighlight();
  testHighlightDefaultOff();
  testSubscriptSuperscript();
  testStrikethrough();
  testEmphasisStrongCode();
  testInlineMath();
  testMathBlock();
  testLink();
  testImage();
  testAutolink();
  testSoftBreakHardbreak();
  testSoftBreakNewline();
  testLineBreak();
  testHtmlInline();
  testFootnoteReference();
  testTightList();
  testLooseList();
  testOrderedListStart();
  testTaskList();
  testNestedList();
  testTableAlignments();
  testJavascriptLinkDropped();
  testDataImage();
  testEmoji();
  testSerializeSourceReadsSettings();
  return 0;
}
