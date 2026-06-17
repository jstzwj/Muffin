#include "document/ImageSyntaxOps.h"

#include "../TestUtils.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

using namespace muffin;
using image_syntax::Image;
using image_syntax::Syntax;

// ---- parse() ----------------------------------------------------------------

void testParseMarkdown() {
  auto md = image_syntax::parse(QStringLiteral("![alt](src.png)"));
  require(md.syntax == Syntax::Markdown, QStringLiteral("![alt](src) parses as Markdown"));
  require(md.alt == QStringLiteral("alt"), QStringLiteral("markdown alt extracted"));
  require(md.src == QStringLiteral("src.png"), QStringLiteral("markdown src extracted"));
  require(md.title.isEmpty(), QStringLiteral("markdown title empty when absent"));

  auto titled = image_syntax::parse(QStringLiteral("![cap](a/b.png \"My Title\")"));
  require(titled.syntax == Syntax::Markdown, QStringLiteral("titled markdown parses"));
  require(titled.alt == QStringLiteral("cap"), QStringLiteral("titled alt"));
  require(titled.src == QStringLiteral("a/b.png"), QStringLiteral("titled src"));
  require(titled.title == QStringLiteral("My Title"), QStringLiteral("titled title"));

  auto spaces = image_syntax::parse(QStringLiteral("![my image](folder/x.png)"));
  require(spaces.alt == QStringLiteral("my image"), QStringLiteral("alt keeps spaces"));
  require(spaces.src == QStringLiteral("folder/x.png"), QStringLiteral("src keeps slashes"));

  auto emptyAlt = image_syntax::parse(QStringLiteral("![](src.png)"));
  require(emptyAlt.alt.isEmpty(), QStringLiteral("empty alt text"));
}

void testParseHtml() {
  auto img = image_syntax::parse(QStringLiteral("<img src=\"x.png\" alt=\"alt\">"));
  require(img.syntax == Syntax::Html, QStringLiteral("<img> parses as Html"));
  require(img.src == QStringLiteral("x.png"), QStringLiteral("html src extracted"));
  require(img.alt == QStringLiteral("alt"), QStringLiteral("html alt extracted"));
  require(img.otherAttrs.isEmpty(), QStringLiteral("no extra attrs for src/alt-only img"));

  auto noAlt = image_syntax::parse(QStringLiteral("<img src=\"x.png\">"));
  require(noAlt.syntax == Syntax::Html, QStringLiteral("img without alt is Html"));
  require(noAlt.alt.isEmpty(), QStringLiteral("img alt empty when absent"));

  auto extra = image_syntax::parse(QStringLiteral("<img src=\"x\" alt=\"a\" style=\"zoom:50%;\" class=\"c\">"));
  require(extra.syntax == Syntax::Html, QStringLiteral("img with extras is Html"));
  require(extra.otherAttrs == QStringList{QStringLiteral("style"), QStringLiteral("class")},
          QStringLiteral("extra attribute names captured in order, excluding src/alt"));

  auto singleQuote = image_syntax::parse(QStringLiteral("<img src='x' alt='a'>"));
  require(singleQuote.syntax == Syntax::Html && singleQuote.src == QStringLiteral("x"),
          QStringLiteral("single-quoted attributes parse"));
}

void testParseNonImage() {
  require(image_syntax::parse(QStringLiteral("hello world")).syntax == Syntax::None,
          QStringLiteral("plain text is not an image"));
  require(image_syntax::parse(QString()).syntax == Syntax::None, QStringLiteral("empty is not an image"));
  require(image_syntax::parse(QStringLiteral("[link](url)")).syntax == Syntax::None,
          QStringLiteral("a link is not an image"));
}

// ---- zoomPercent() / zoomFactor() -------------------------------------------

void testZoomPercent() {
  require(image_syntax::zoomPercent(QStringLiteral("<img src=\"x\" style=\"zoom:25%;\">")) == 25,
          QStringLiteral("zoom:25% -> 25"));
  require(image_syntax::zoomPercent(QStringLiteral("<img src=\"x\" style=\"zoom: 50%\">")) == 50,
          QStringLiteral("zoom: 50% (spaced, no semicolon) -> 50"));
  require(image_syntax::zoomPercent(QStringLiteral("<img src=\"x\" style=\"color:red; zoom:150%;\">")) == 150,
          QStringLiteral("zoom among other declarations -> 150"));
  require(image_syntax::zoomPercent(QStringLiteral("<img src=\"x\">")) == 100,
          QStringLiteral("no style -> 100"));
  require(image_syntax::zoomPercent(QStringLiteral("<img src=\"x\" style=\"color:red;\">")) == 100,
          QStringLiteral("style without zoom -> 100"));
  require(image_syntax::zoomPercent(QStringLiteral("![alt](x)")) == 100,
          QStringLiteral("markdown image -> 100"));
  require(image_syntax::zoomPercent(QStringLiteral("<img src=\"x\" style=\"zoom:33%;\">")) == 33,
          QStringLiteral("zoom:33% -> 33"));
  require(image_syntax::zoomPercent(QStringLiteral("<img src=\"x\" style=\"zoom:0%;\">")) == 100,
          QStringLiteral("zoom:0% treated as natural (100)"));
  require(image_syntax::zoomFactor(QStringLiteral("<img src=\"x\" style=\"zoom:50%;\">")) == 0.5,
          QStringLiteral("zoomFactor 50% -> 0.5"));
}

// ---- setZoom() --------------------------------------------------------------

void requireEq(const QString& actual, const QString& expected, const QString& message) {
  require(actual == expected,
          QStringLiteral("%1\n   expected: %2\n   actual:   %3").arg(message, expected, actual));
}

void testSetZoomMarkdownPromotesToImg() {
  // A markdown image forced to a non-100 zoom is promoted to <img> with a zoom style.
  requireEq(image_syntax::setZoom(QStringLiteral("![a](x)"), 25),
            QStringLiteral("<img src=\"x\" alt=\"a\" style=\"zoom: 25%;\">"),
            QStringLiteral("markdown + 25% -> <img> with zoom"));
}

void testSetZoomReplacesExistingZoom() {
  requireEq(image_syntax::setZoom(QStringLiteral("<img src=\"x\" alt=\"a\" style=\"zoom: 25%;\">"), 50),
            QStringLiteral("<img src=\"x\" alt=\"a\" style=\"zoom: 50%;\">"),
            QStringLiteral("img zoom 25 -> 50"));
}

void testSetZoomHundredRemovesZoom() {
  requireEq(image_syntax::setZoom(QStringLiteral("<img src=\"x\" alt=\"a\" style=\"zoom: 25%;\">"), 100),
            QStringLiteral("<img src=\"x\" alt=\"a\">"),
            QStringLiteral("img zoom 25 -> 100 removes the style attribute entirely"));
  // No-op when there is no zoom to remove.
  requireEq(image_syntax::setZoom(QStringLiteral("<img src=\"x\" alt=\"a\">"), 100),
            QStringLiteral("<img src=\"x\" alt=\"a\">"),
            QStringLiteral("img without zoom @ 100 is unchanged"));
  // Markdown @ 100 is unchanged.
  requireEq(image_syntax::setZoom(QStringLiteral("![a](x)"), 100),
            QStringLiteral("![a](x)"),
            QStringLiteral("markdown @ 100 is unchanged"));
}

void testSetZoomPreservesOtherDeclarations() {
  requireEq(
      image_syntax::setZoom(QStringLiteral("<img src=\"x\" alt=\"a\" style=\"color: red; zoom: 25%;\">"), 100),
      QStringLiteral("<img src=\"x\" alt=\"a\" style=\"color: red;\">"),
      QStringLiteral("removing zoom keeps other declarations"));
  requireEq(
      image_syntax::setZoom(QStringLiteral("<img src=\"x\" alt=\"a\" style=\"color: red; zoom: 25%;\">"), 50),
      QStringLiteral("<img src=\"x\" alt=\"a\" style=\"color: red; zoom: 50%;\">"),
      QStringLiteral("replacing zoom keeps other declarations"));
}

void testSetZoomPreservesOtherAttributes() {
  requireEq(image_syntax::setZoom(QStringLiteral("<img src=\"x\" alt=\"a\" class=\"c\">"), 50),
            QStringLiteral("<img src=\"x\" alt=\"a\" class=\"c\" style=\"zoom: 50%;\">"),
            QStringLiteral("non-style attributes preserved, style appended"));
}

void testSetZoomCustomAndClamp() {
  requireEq(image_syntax::setZoom(QStringLiteral("<img src=\"x\" alt=\"a\">"), 200),
            QStringLiteral("<img src=\"x\" alt=\"a\" style=\"zoom: 200%;\">"),
            QStringLiteral("custom 200% adds zoom"));
  requireEq(image_syntax::setZoom(QStringLiteral("![a](x)"), 0),
            QStringLiteral("<img src=\"x\" alt=\"a\" style=\"zoom: 1%;\">"),
            QStringLiteral("percent clamped to minimum 1"));
  requireEq(image_syntax::setZoom(QStringLiteral("![a](x)"), 5000),
            QStringLiteral("<img src=\"x\" alt=\"a\" style=\"zoom: 1000%;\">"),
            QStringLiteral("percent clamped to maximum 1000"));
}

void testSetZoomIdempotent() {
  const QString once = image_syntax::setZoom(QStringLiteral("![a](x)"), 25);
  const QString twice = image_syntax::setZoom(once, 25);
  requireEq(twice, once, QStringLiteral("applying the same zoom twice is a no-op"));
}

void testSetZoomIgnoresNonImage() {
  requireEq(image_syntax::setZoom(QStringLiteral("hello"), 50), QStringLiteral("hello"),
            QStringLiteral("non-image source is returned unchanged"));
}

// ---- toMarkdown() / toHtml() ------------------------------------------------

void testToMarkdown() {
  requireEq(image_syntax::toMarkdown(QStringLiteral("<img src=\"x.png\" alt=\"alt\">")),
            QStringLiteral("![alt](x.png)"),
            QStringLiteral("<img> -> markdown"));
  requireEq(image_syntax::toMarkdown(QStringLiteral("<img src=\"x.png\" alt=\"alt\" style=\"zoom: 25%;\">")),
            QStringLiteral("![alt](x.png)"),
            QStringLiteral("<img> with style -> markdown drops style"));
  requireEq(image_syntax::toMarkdown(QStringLiteral("![alt](x.png)")),
            QStringLiteral("![alt](x.png)"),
            QStringLiteral("markdown input unchanged by toMarkdown"));
  requireEq(image_syntax::toMarkdown(QStringLiteral("hello")), QStringLiteral("hello"),
            QStringLiteral("non-image unchanged by toMarkdown"));
}

void testToHtml() {
  requireEq(image_syntax::toHtml(QStringLiteral("![alt](x.png)")),
            QStringLiteral("<img src=\"x.png\" alt=\"alt\">"),
            QStringLiteral("markdown -> <img>"));
  requireEq(image_syntax::toHtml(QStringLiteral("![alt](x.png \"title\")")),
            QStringLiteral("<img src=\"x.png\" alt=\"alt\">"),
            QStringLiteral("markdown title is not carried into <img>"));
  requireEq(image_syntax::toHtml(QStringLiteral("<img src=\"x.png\" alt=\"alt\">")),
            QStringLiteral("<img src=\"x.png\" alt=\"alt\">"),
            QStringLiteral("html input unchanged by toHtml"));
}

// ---- round-trip behavior the menu relies on ---------------------------------

void testFindImgTag() {
  // A standalone <img> HTML block may carry surrounding whitespace/newlines (cmark keeps the whole
  // block line). findImgTag locates just the tag within the block so its range can be resolved.
  const QString block = QStringLiteral("  <img src=\"x.png\" alt=\"a\" style=\"zoom: 50%;\">  \n");
  const auto loc = image_syntax::findImgTag(block);
  require(loc.found, QStringLiteral("findImgTag locates the tag within a block"));
  requireEq(block.mid(loc.start, loc.end - loc.start),
            QStringLiteral("<img src=\"x.png\" alt=\"a\" style=\"zoom: 50%;\">"),
            QStringLiteral("located range spans exactly the <img> tag"));

  // Case-insensitive, self-closing tolerated.
  require(image_syntax::findImgTag(QStringLiteral("<IMG SRC='x' />")).found,
          QStringLiteral("uppercase/self-closing tag matched"));

  require(!image_syntax::findImgTag(QStringLiteral("no tag here")).found,
          QStringLiteral("no match when no <img> is present"));
}

void testMenuRoundTrips() {
  // resize 25% on a markdown image, then 100%: ends as a zoom-less <img> (no auto
  // reversion to markdown — Convert ▸ Standard handles that explicitly).
  const QString resized = image_syntax::setZoom(QStringLiteral("![a](x)"), 25);
  const QString reset = image_syntax::setZoom(resized, 100);
  requireEq(reset, QStringLiteral("<img src=\"x\" alt=\"a\">"),
            QStringLiteral("25% then 100% leaves a zoom-less <img>"));

  // Convert ▸ Standard Markdown strips the wrapper back to markdown.
  requireEq(image_syntax::toMarkdown(reset), QStringLiteral("![a](x)"),
            QStringLiteral("Convert to Standard restores the markdown image"));

  // The resize radio state tracks the encoded zoom.
  require(image_syntax::zoomPercent(resized) == 25, QStringLiteral("resized image reports 25%"));
  require(image_syntax::zoomPercent(reset) == 100, QStringLiteral("reset image reports 100%"));
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testParseMarkdown();
  testParseHtml();
  testParseNonImage();
  testZoomPercent();
  testSetZoomMarkdownPromotesToImg();
  testSetZoomReplacesExistingZoom();
  testSetZoomHundredRemovesZoom();
  testSetZoomPreservesOtherDeclarations();
  testSetZoomPreservesOtherAttributes();
  testSetZoomCustomAndClamp();
  testSetZoomIdempotent();
  testSetZoomIgnoresNonImage();
  testToMarkdown();
  testToHtml();
  testFindImgTag();
  testMenuRoundTrips();
  return 0;
}
