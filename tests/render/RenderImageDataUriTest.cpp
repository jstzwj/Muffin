#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRgb>
#include <QString>

#include <functional>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// A 40x40 solid red rectangle as SVG source. Built once; both data: URI forms
// (percent-encoded and base64) are derived from it at the byte level, so the
// test never depends on a hand-maintained blob or on QImage's PNG encoder
// (qpng/qjpeg are absent from this Qt build).
QByteArray redSvgBytes() {
  return QByteArray(
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
      "<rect width=\"40\" height=\"40\" fill=\"#d00000\"/></svg>");
}

QString redSvgPercentDataUri() {
  // Percent-encoding is essential for the SVG form: bare '#', '<', '>', '"'
  // and spaces would either break the markdown image destination or be read as
  // a URL fragment.
  return QStringLiteral("data:image/svg+xml,") + QString::fromLatin1(redSvgBytes().toPercentEncoding());
}

QString redSvgBase64DataUri() {
  return QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(redSvgBytes().toBase64());
}

QImage renderParagraphImage(const RenderTheme& theme, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* p = findFirstBlock(session.document().root(), BlockType::Paragraph);
  require(p != nullptr, QStringLiteral("fixture should contain a paragraph"));
  const BlockLayout* block = layout.block(p->id());
  require(block != nullptr, QStringLiteral("paragraph block should be promoted"));
  const QRectF rect = block->rect();

  QImage image(int(rect.width()), int(rect.height() + 4), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr);
  painter.end();
  return image;
}

const std::function<bool(QRgb)> isRed = [](QRgb p) {
  return qRed(p) > 150 && qGreen(p) < 90 && qBlue(p) < 90;
};

QRect colorBBox(const QImage& image, const std::function<bool(QRgb)>& pred) {
  int left = image.width(), top = image.height(), right = -1, bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (pred(image.pixel(x, y))) {
        left = qMin(left, x); top = qMin(top, y);
        right = qMax(right, x); bottom = qMax(bottom, y);
      }
    }
  }
  return right < left ? QRect() : QRect(left, top, right - left + 1, bottom - top + 1);
}

RenderTheme plainTheme() {
  return RenderTheme::fromDefinition(
      CssThemeMapper::fromCss(QStringLiteral("#write { color:#000000; }"), QStringLiteral("t"), QString()));
}

// Regression guard for the "inline image paints above its own block" bug: under a
// CSS `line-height` the image used to be placed with a `ceil(line.height()*1.16)`
// estimate that only matched a plain text line, drifting negative and drawing the
// image on top of the previous block's text. The image must stay inside its block.
void testImageDoesNotOverflowAboveItsBlock() {
  const RenderTheme theme = RenderTheme::fromDefinition(
      CssThemeMapper::fromCss(QStringLiteral("body { line-height: 1.6; } #write { color:#000000; }"),
                              QStringLiteral("g"), QString()));
  const QString md = QStringLiteral("WebP images:\n\n![red](%1)\n").arg(redSvgPercentDataUri());

  DocumentSession session;
  session.setMarkdownText(md, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  const auto& children = session.document().root().children();
  qreal docTop = 1e9, docBottom = -1e9;
  qreal imageBlockTop = 0.0;
  bool firstParagraph = true;
  for (const auto& child : children) {
    const BlockLayout* b = layout.block(child->id());
    if (!b) continue;
    docTop = qMin(docTop, b->rect().top());
    docBottom = qMax(docBottom, b->rect().bottom());
    if (child->type() == BlockType::Paragraph) {
      if (!firstParagraph) {
        imageBlockTop = b->rect().top();  // the image sits in the second paragraph
      }
      firstParagraph = false;
    }
  }
  require(imageBlockTop > 0.0, QStringLiteral("image-bearing paragraph not found"));

  QImage img(400, qMax<int>(1, int(docBottom - docTop) + 8), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  QPainter painter(&img);
  painter.translate(0, -docTop);
  for (const auto& child : children) {
    const BlockLayout* b = layout.block(child->id());
    if (b) b->paint(painter, theme, 0.0, nullptr);
  }
  painter.end();

  const QRect red = colorBBox(img, isRed);
  require(red.isValid(), QStringLiteral("inline image should paint red pixels"));
  const qreal inkTopDoc = red.top() + docTop;
  // Allow 1px for anti-aliasing at the SVG edge; anything more means the image
  // escaped its block rect upward — the regression.
  require(inkTopDoc >= imageBlockTop - 1.0,
          QStringLiteral("image paints above its own block (ink top=%1, block top=%2)")
              .arg(inkTopDoc).arg(imageBlockTop));
}

// A valid inline data: URI image renders as actual coloured pixels (a red
// block), not as the grey "broken image" placeholder. Exercises the whole path:
// markdown image -> InlineLayout::buildImageAtoms -> decodeDataUri.
void testPercentEncodedDataUriRendersImage() {
  const QString md = QStringLiteral("![red](%1)\n").arg(redSvgPercentDataUri());
  const QImage img = renderParagraphImage(plainTheme(), md);
  const QRect red = colorBBox(img, isRed);
  require(red.isValid(), QStringLiteral("percent-encoded data: URI image should paint red pixels"));
  require(red.width() >= 10 && red.height() >= 10,
          QStringLiteral("rendered image should be a real block, not a tiny placeholder (bbox=%1x%2)")
              .arg(red.width()).arg(red.height()));
}

// Same end-to-end check via the base64 form — the shape real emitters produce
// for binary payloads.
void testBase64DataUriRendersImage() {
  const QString md = QStringLiteral("![red](%1)\n").arg(redSvgBase64DataUri());
  const QImage img = renderParagraphImage(plainTheme(), md);
  const QRect red = colorBBox(img, isRed);
  require(red.isValid(), QStringLiteral("base64 data: URI image should paint red pixels"));
  require(red.width() >= 10 && red.height() >= 10,
          QStringLiteral("rendered image should be a real block, not a tiny placeholder (bbox=%1x%2)")
              .arg(red.width()).arg(red.height()));
}

// A data: URI whose payload is not a decodable image falls back to the broken
// placeholder — no coloured pixels leak through.
void testBrokenDataUriShowsPlaceholder() {
  // "AAAA" is valid base64 but decodes to 3 NUL bytes — not any image format.
  const QString md = QStringLiteral("![x](data:image/png;base64,AAAA)\n");
  const QImage img = renderParagraphImage(plainTheme(), md);
  const QRect red = colorBBox(img, isRed);
  require(!red.isValid(),
          QStringLiteral("broken data: URI should render the placeholder, not a coloured image"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testImageDoesNotOverflowAboveItsBlock);
  RUN_TEST(testPercentEncodedDataUriRendersImage);
  RUN_TEST(testBase64DataUriRendersImage);
  RUN_TEST(testBrokenDataUriShowsPlaceholder);
#undef RUN_TEST
  return 0;
}
