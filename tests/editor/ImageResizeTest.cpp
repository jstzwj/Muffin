#include "document/DocumentSession.h"
#include "document/ImageSyntaxOps.h"
#include "document/MarkdownNode.h"
#include "editor/EditorController.h"
#include "editor/SelectionController.h"

#include "EditorTestUtils.h"

#include <QApplication>
#include <functional>

using namespace muffin;

// Drives the image queries against a real DocumentSession + EditorController (no EditorView —
// attach(session, nullptr) is safe since the image queries never touch the view). Guards the
// editor-side wiring the menu relies on: cursor-on-image detection and the source rewrite applied
// through the document. The pure source-text transforms live in ImageSyntaxOpsTest; this test pins
// the detection path — including the fix that recognizes an inline <img> HtmlInline node as an image
// (previously only InlineType::Image nodes matched, disabling every <img> command).
//
// Note: a standalone <img> on its own line is parsed by cmark-gfm as an HTML *block* (not inline),
// so it is rendered by the HTML engine rather than the inline projection. The editor queries here
// therefore exercise images that live inline within a paragraph: markdown images and inline <img>.
struct ImageHarness {
  DocumentSession session;
  EditorController controller;

  explicit ImageHarness(QString markdown) {
    session.setMarkdownText(std::move(markdown), false);
    controller.attach(&session, nullptr);
  }

  MarkdownNode* firstParagraph() { return blockAt(session, 0); }

  void cursorOn(MarkdownNode* block, qsizetype offset) {
    setCursor(controller.selection(), block, offset);
  }

  // Apply a transform to the image under the cursor the way RenderCommandFacade does: resolve the
  // source range, transform via image_syntax, apply as a text delta.
  QString applyTransform(const std::function<QString(const QString&)>& transform) {
    qsizetype start = 0, end = 0;
    if (!controller.imageSourceRangeAtCursor(start, end)) {
      return {};
    }
    const QString source = session.markdownText().mid(start, end - start);
    const QString replacement = transform(source);
    session.applyTextDelta(start, end - start, replacement, true);
    return replacement;
  }
};

void testMarkdownImageDetected() {
  ImageHarness h(QStringLiteral("![a](x.png)\n"));
  MarkdownNode* para = h.firstParagraph();
  require(para != nullptr && para->type() == BlockType::Paragraph, QStringLiteral("image lives in a paragraph"));
  h.cursorOn(para, 0);

  require(h.controller.isOnImage(), QStringLiteral("cursor detects the markdown image"));
  require(h.controller.imageSrcAtCursor() == QStringLiteral("x.png"),
          QStringLiteral("markdown image src resolved"));
  qsizetype start = 0, end = 0;
  require(h.controller.imageSourceRangeAtCursor(start, end), QStringLiteral("markdown image range resolved"));
  require(h.session.markdownText().mid(start, end - start) == QStringLiteral("![a](x.png)"),
          QStringLiteral("range spans the whole markdown image"));
}

void testCursorInUrlStillDetected() {
  // The user clicks *inside* the revealed image syntax — often the URL part, not the alt label.
  // The alt "a" only covers visible offset 2-3, so a visible-range-only check would miss a cursor
  // in "(x.png)". A resolved source offset inside the image's source extent must still match.
  ImageHarness h(QStringLiteral("![a](x.png)\n"));
  MarkdownNode* para = h.firstParagraph();
  require(para != nullptr && para->type() == BlockType::Paragraph, QStringLiteral("image lives in a paragraph"));
  setSourceCursor(h.controller.selection(), para, 7, 7);  // block-local offset 7 lands in "x.png"

  require(h.controller.isOnImage(), QStringLiteral("cursor inside the URL still detects the image"));
  require(h.controller.imageSrcAtCursor() == QStringLiteral("x.png"),
          QStringLiteral("image src resolved from a URL-position cursor"));
  qsizetype start = 0, end = 0;
  require(h.controller.imageSourceRangeAtCursor(start, end), QStringLiteral("range resolved from URL cursor"));
  require(h.session.markdownText().mid(start, end - start) == QStringLiteral("![a](x.png)"),
          QStringLiteral("range spans the whole image from a URL-position cursor"));
}

void testNonFirstBlockImageDetectedAndResizable() {
  // Regression for the resize/convert menu doing nothing on an image deeper in the document.
  // Detection compared the cursor's ABSOLUTE source offset (and the backing node's absolute range)
  // against the projection span's content-LOCAL offsets; the comparison only held for an image in
  // the first block at document offset 0. Any image whose block starts past offset 0 was missed:
  // isOnImage() could still light up via the visible path, but imageSourceRangeAtCursor()'s
  // containment test failed and the handler silently no-op'd — exactly "clicked, nothing happened".
  // The long heading pushes the paragraph's byteStart well past the image's local span length, so
  // there is no coincidental absolute==local alignment to mask the bug.
  ImageHarness h(QStringLiteral("# A reasonably long heading line\n\n![a](x.png)\n"));
  MarkdownNode* para = firstBlockOfType(h.session, BlockType::Paragraph);
  require(para != nullptr, QStringLiteral("image paragraph exists after the heading"));

  const QString md = h.session.markdownText();
  const qsizetype img = md.indexOf(QStringLiteral("![a](x.png)"));
  require(img > 0, QStringLiteral("image sits at a non-zero document offset"));

  // Cursor on the image atom (visible path).
  h.cursorOn(para, 0);
  require(h.controller.isOnImage(), QStringLiteral("on-atom: image detected"));
  qsizetype start = 0, end = 0;
  require(h.controller.imageSourceRangeAtCursor(start, end), QStringLiteral("on-atom: range resolves"));
  require(md.mid(start, end - start) == QStringLiteral("![a](x.png)"),
          QStringLiteral("on-atom: range spans the image"));

  // Cursor in the URL part (source path) with an absolute offset inside the image.
  setSourceCursor(h.controller.selection(), para, 0, img + 7);  // absolute offset inside "x.png"
  require(h.controller.isOnImage(), QStringLiteral("in-url: image detected"));
  require(h.controller.imageSourceRangeAtCursor(start, end), QStringLiteral("in-url: range resolves"));
  require(md.mid(start, end - start) == QStringLiteral("![a](x.png)"),
          QStringLiteral("in-url: range spans the image"));
}

void testInlineHtmlImageDetected() {
  // The line starts with "a " so the <img> stays inline HTML within a paragraph (a standalone <img>
  // would become an HTML block). The cursor sits on the image atom (offset 2 = after "a ").
  ImageHarness h(QStringLiteral("a <img src=\"x.png\" alt=\"a\" style=\"zoom: 50%;\">\n"));
  MarkdownNode* para = h.firstParagraph();
  require(para != nullptr && para->type() == BlockType::Paragraph,
          QStringLiteral("inline <img> lives in a paragraph"));
  h.cursorOn(para, 2);

  require(h.controller.isOnImage(), QStringLiteral("cursor detects the inline <img> image"));
  require(h.controller.imageSrcAtCursor() == QStringLiteral("x.png"), QStringLiteral("<img> src resolved"));
  qsizetype start = 0, end = 0;
  require(h.controller.imageSourceRangeAtCursor(start, end), QStringLiteral("<img> range resolved"));
  require(image_syntax::zoomPercent(h.session.markdownText().mid(start, end - start)) == 50,
          QStringLiteral("inline <img> zoom:50% reads as 50"));
}

void testResizeMarkdownImageWritesZoomStyle() {
  ImageHarness h(QStringLiteral("![a](x.png)\n"));
  h.cursorOn(h.firstParagraph(), 0);

  const QString snippet = h.applyTransform([](const QString& s) { return image_syntax::setZoom(s, 25); });
  require(snippet.contains(QStringLiteral("style=\"zoom: 25%;\"")),
          QStringLiteral("resize produced a zoom style snippet"));
  const QString md = h.session.markdownText();
  require(md.contains(QStringLiteral("<img src=\"x.png\" alt=\"a\"")),
          QStringLiteral("markdown image promoted to <img> in the document"));
  require(md.contains(QStringLiteral("style=\"zoom: 25%;\"")),
          QStringLiteral("zoom style written into the document"));
}

void testResizeHtmlImageUpdatesZoom() {
  ImageHarness h(QStringLiteral("a <img src=\"x.png\" alt=\"a\" style=\"zoom: 25%;\">\n"));
  h.cursorOn(h.firstParagraph(), 2);

  h.applyTransform([](const QString& s) { return image_syntax::setZoom(s, 100); });
  require(h.session.markdownText().contains(QStringLiteral("<img src=\"x.png\" alt=\"a\">")),
          QStringLiteral("100% strips the zoom style, leaving a bare <img>"));
  require(!h.session.markdownText().contains(QStringLiteral("zoom")),
          QStringLiteral("no zoom declaration remains at 100%"));
}

void testConvertMarkdownToHtml() {
  ImageHarness h(QStringLiteral("a ![a](x.png)\n"));
  h.cursorOn(h.firstParagraph(), 2);

  h.applyTransform([](const QString& s) { return image_syntax::toHtml(s); });
  require(h.session.markdownText().contains(QStringLiteral("<img src=\"x.png\" alt=\"a\">")),
          QStringLiteral("Convert ▸ HTML produced an <img> tag"));
}

void testConvertHtmlToMarkdown() {
  ImageHarness h(QStringLiteral("a <img src=\"x.png\" alt=\"a\" style=\"zoom: 25%;\">\n"));
  h.cursorOn(h.firstParagraph(), 2);

  h.applyTransform([](const QString& s) { return image_syntax::toMarkdown(s); });
  require(h.session.markdownText().contains(QStringLiteral("![a](x.png)")),
          QStringLiteral("Convert ▸ Standard restored the markdown image"));
}

void testStandaloneHtmlBlockImageDetected() {
  // A standalone <img> on its own line is parsed by cmark-gfm as an HTML *block* (no inline
  // projection), so the paragraph-based detection must fall back to scanning the block source.
  ImageHarness h(QStringLiteral("<img src=\"x.png\" alt=\"a\" style=\"zoom: 50%;\">\n"));
  MarkdownNode* htmlBlock = blockAt(h.session, 0);
  require(htmlBlock != nullptr && htmlBlock->type() == BlockType::HtmlBlock,
          QStringLiteral("standalone <img> parses as an HTML block"));
  h.cursorOn(htmlBlock, 0);

  require(h.controller.isOnImage(), QStringLiteral("standalone <img> block detected as an image"));
  require(h.controller.imageSrcAtCursor() == QStringLiteral("x.png"),
          QStringLiteral("block image src resolved"));
  qsizetype start = 0, end = 0;
  require(h.controller.imageSourceRangeAtCursor(start, end), QStringLiteral("block image range resolved"));
  require(h.session.markdownText().mid(start, end - start).startsWith(QStringLiteral("<img")),
          QStringLiteral("range spans the <img> tag"));
  require(image_syntax::zoomPercent(h.session.markdownText().mid(start, end - start)) == 50,
          QStringLiteral("block image zoom:50% reads as 50"));
}

void testStandaloneHtmlBlockImageResize() {
  ImageHarness h(QStringLiteral("<img src=\"x.png\" alt=\"a\" style=\"zoom: 50%;\">\n"));
  h.cursorOn(blockAt(h.session, 0), 0);

  const QString snippet = h.applyTransform([](const QString& s) { return image_syntax::setZoom(s, 25); });
  require(snippet.contains(QStringLiteral("style=\"zoom: 25%;\"")),
          QStringLiteral("resizing a block image updated the zoom"));
  require(h.session.markdownText().contains(QStringLiteral("zoom: 25%;")),
          QStringLiteral("resize written into the HTML block"));
  require(!h.session.markdownText().contains(QStringLiteral("zoom: 50%")),
          QStringLiteral("old zoom replaced"));
}

void testStandaloneHtmlBlockImageConvertToMarkdown() {
  ImageHarness h(QStringLiteral("<img src=\"x.png\" alt=\"a\" style=\"zoom: 25%;\">\n"));
  h.cursorOn(blockAt(h.session, 0), 0);

  h.applyTransform([](const QString& s) { return image_syntax::toMarkdown(s); });
  require(h.session.markdownText().contains(QStringLiteral("![a](x.png)")),
          QStringLiteral("Convert ▸ Standard turned the <img> block into a markdown image"));
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  testMarkdownImageDetected();
  testCursorInUrlStillDetected();
  testNonFirstBlockImageDetectedAndResizable();
  testInlineHtmlImageDetected();
  testResizeMarkdownImageWritesZoomStyle();
  testResizeHtmlImageUpdatesZoom();
  testConvertMarkdownToHtml();
  testConvertHtmlToMarkdown();
  testStandaloneHtmlBlockImageDetected();
  testStandaloneHtmlBlockImageResize();
  testStandaloneHtmlBlockImageConvertToMarkdown();
  return 0;
}
