#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "editor/ResourceUrl.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRgb>
#include <QString>

#include <functional>
#include <cmath>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// A 40x40 solid red rectangle as SVG source. Built once; both data: URI forms
// (percent-encoded and base64) are derived from it at the byte level, so the
// test never depends on a hand-maintained blob or on QImage's PNG encoder
// (qpng/qjpeg are absent from this Qt build).
QByteArray redSvgBytes() {
  return QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\">"
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

const std::function<bool(QRgb)> isRed = [](QRgb p) { return qRed(p) > 150 && qGreen(p) < 90 && qBlue(p) < 90; };

QRect colorBBox(const QImage& image, const std::function<bool(QRgb)>& pred) {
  int left = image.width(), top = image.height(), right = -1, bottom = -1;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (pred(image.pixel(x, y))) {
        left = qMin(left, x);
        top = qMin(top, y);
        right = qMax(right, x);
        bottom = qMax(bottom, y);
      }
    }
  }
  return right < left ? QRect() : QRect(left, top, right - left + 1, bottom - top + 1);
}

RenderTheme plainTheme() {
  return RenderTheme::fromDefinition(
      CssThemeMapper::fromCss(QStringLiteral("#write { color:#000000; }"), QStringLiteral("t"), QString()));
}

void testDownsamplingPreservesThinStrokes() {
  QTemporaryDir dir;
  QImage stripes(128, 64, QImage::Format_RGB32);
  for (int y = 0; y < stripes.height(); ++y)
    for (int x = 0; x < stripes.width(); ++x)
      stripes.setPixel(x, y, qRgb(64, x % 4 == 0 ? 0 : 255, 192));
  require(stripes.save(dir.filePath("stripes.png")), "write striped screenshot");
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss("img{width:32px}", "stripes", {}));
  for (const auto& markdown : {QStringLiteral("![strokes](stripes.png)"), QStringLiteral("<img src='stripes.png'>")}) {
    DocumentSession session;
    session.setMarkdownText(markdown, false);
    const auto id = session.document().root().children().front()->id();
    for (const bool preview : {false, true}) {
      if (preview && !markdown.startsWith('!'))
        continue;
      SelectionRange selection;
      if (preview)
        selection.anchor = selection.focus = {id, {id, 1, 2}};
      DocumentLayout layout;
      layout.rebuild(session.document(), theme, 400, selection, dir.filePath("doc.md"));
      const auto* block = layout.block(id);
      require(block != nullptr, "image block is laid out");
      for (const qreal dpr : {1.0, 1.25, 1.5, 2.0, 4.0}) {
        QImage canvas(qCeil(400 * dpr), qCeil((block->height() + 4) * dpr), QImage::Format_RGB32);
        canvas.setDevicePixelRatio(dpr);
        canvas.fill(Qt::white);
        QPainter painter(&canvas);
        painter.translate(-block->rect().topLeft());
        block->paint(painter, theme, 0);
        painter.end();
        const QRect ink =
            colorBBox(canvas, [](QRgb p) { return qAbs(qBlue(p) - 192) <= 2 && qAbs(qRed(p) - 64) <= 2; });
        require(ink.width() == qRound(32 * dpr), "image spans the expected physical pixels");
        // Each group of four source pixels contains exactly one dark column.
        const auto darkCoverage = [](qreal x) { return std::floor(x / 4) + qMin(1.0, std::fmod(x, 4.0)); };
        const qreal step = stripes.width() / qreal(ink.width());
        for (int x = 0; x < ink.width(); ++x) {
          const qreal coverage = darkCoverage((x + 1) * step) - darkCoverage(x * step);
          const qreal expected = 255 * (1 - coverage / step);
          const int green = qGreen(canvas.pixel(ink.left() + x, ink.center().y()));
          require(qAbs(green - expected) <= 3, QString("stroke coverage for %1 at DPR %2, column %3: %4 vs %5")
                                                   .arg(markdown)
                                                   .arg(dpr)
                                                   .arg(x)
                                                   .arg(green)
                                                   .arg(expected));
        }
      }
    }
  }
}

QImage renderDocumentImage(DocumentLayout& layout, DocumentSession& session, const QString& path) {
  const RenderTheme theme = plainTheme();
  layout.rebuild(session.document(), theme, 800.0, path);
  QImage image(800, qMax(1, int(layout.totalHeight()) + 8), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  for (const auto& child : session.document().root().children()) {
    if (const auto* block = layout.block(child->id()))
      block->paint(painter, theme, 0.0, nullptr);
  }
  return image;
}

void testDocumentRelativeImagesAndIndependentBases() {
  QTemporaryDir root;
  require(root.isValid(), QStringLiteral("image fixture directory unavailable"));
  const QString originalCwd = QDir::currentPath();
  const auto isBlue = [](QRgb pixel) { return qBlue(pixel) > 150 && qRed(pixel) < 90 && qGreen(pixel) < 90; };
  for (const QString& folder : {QStringLiteral("red"), QStringLiteral("blue")}) {
    require(QDir().mkpath(root.filePath(folder + QStringLiteral("/assets"))), QStringLiteral("fixture mkdir failed"));
    QFile image(root.filePath(folder + QStringLiteral("/assets/a b.svg")));
    require(image.open(QIODevice::WriteOnly), QStringLiteral("fixture image open failed"));
    QByteArray bytes = redSvgBytes();
    if (folder == QStringLiteral("blue"))
      bytes.replace("#d00000", "#0000d0");
    require(image.write(bytes) == bytes.size(), QStringLiteral("fixture image write failed"));
  }
  const QString redPath = root.filePath(QStringLiteral("red/doc.md"));
  const QString bluePath = root.filePath(QStringLiteral("blue/doc.md"));
  const QString image = QStringLiteral("![x](assets/a%20b.svg)");
  const QStringList markdown{image, QStringLiteral("# ") + image, QStringLiteral("- ") + image,
                             QStringLiteral("| A |\n| --- |\n| %1 |").arg(image)};
  for (const QString& md : markdown) {
    DocumentSession session;
    session.setMarkdownText(md, false);
    DocumentLayout first, second;
    require(colorBBox(renderDocumentImage(first, session, redPath), isRed).isValid(),
            QStringLiteral("relative image must resolve from document in every block kind"));
    require(colorBBox(renderDocumentImage(second, session, bluePath), isBlue).isValid(),
            QStringLiteral("same href in another document must not reuse first document's resource"));
    require(colorBBox(renderDocumentImage(first, session, bluePath), isBlue).isValid(),
            QStringLiteral("Save As resource-base change must rebuild image atoms"));
    require(colorBBox(renderDocumentImage(first, session, redPath), isRed).isValid(),
            QStringLiteral("changing resource base back must reload the right image"));
  }
  const QString local = root.filePath(QStringLiteral("red/assets/a b.svg"));
  require(resolvedUrlForDocumentResource(QStringLiteral("assets/a%20b.svg"), redPath).toLocalFile() == local,
          QStringLiteral("encoded spaces must decode once for local images"));
  require(resolvedUrlForDocumentResource(QStringLiteral("../red/assets/a%20b.svg"), bluePath).toLocalFile() == local,
          QStringLiteral("parent-relative URL must resolve from its own document"));
  const QUrl fileUrl = QUrl::fromLocalFile(local);
  require(resolvedUrlForDocumentResource(fileUrl.toString(QUrl::FullyEncoded), bluePath) == fileUrl,
          QStringLiteral("absolute file URL must stay absolute"));
  require(resolvedUrlForDocumentResource(QStringLiteral("https://example.com/a%20b.png"), redPath)
                  .toString(QUrl::FullyEncoded) == QStringLiteral("https://example.com/a%20b.png"),
          QStringLiteral("remote URL must retain its own base and escapes"));
  require(QDir::currentPath() == originalCwd, QStringLiteral("resource resolution must not change process CWD"));
}

// Regression guard for the "inline image paints above its own block" bug: under a
// CSS `line-height` the image used to be placed with a `ceil(line.height()*1.16)`
// estimate that only matched a plain text line, drifting negative and drawing the
// image on top of the previous block's text. The image must stay inside its block.
void testImageDoesNotOverflowAboveItsBlock() {
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      QStringLiteral("body { line-height: 1.6; } #write { color:#000000; }"), QStringLiteral("g"), QString()));
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
    if (!b)
      continue;
    docTop = qMin(docTop, b->rect().top());
    docBottom = qMax(docBottom, b->rect().bottom());
    if (child->type() == BlockType::Paragraph) {
      if (!firstParagraph) {
        imageBlockTop = b->rect().top(); // the image sits in the second paragraph
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
    if (b)
      b->paint(painter, theme, 0.0, nullptr);
  }
  painter.end();

  const QRect red = colorBBox(img, isRed);
  require(red.isValid(), QStringLiteral("inline image should paint red pixels"));
  const qreal inkTopDoc = red.top() + docTop;
  // Allow 1px for anti-aliasing at the SVG edge; anything more means the image
  // escaped its block rect upward — the regression.
  require(
      inkTopDoc >= imageBlockTop - 1.0,
      QStringLiteral("image paints above its own block (ink top=%1, block top=%2)").arg(inkTopDoc).arg(imageBlockTop));
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
              .arg(red.width())
              .arg(red.height()));
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
              .arg(red.width())
              .arg(red.height()));
}

// A data: URI whose payload is not a decodable image falls back to the broken
// placeholder — no coloured pixels leak through.
void testBrokenDataUriShowsPlaceholder() {
  // "AAAA" is valid base64 but decodes to 3 NUL bytes — not any image format.
  const QString md = QStringLiteral("![x](data:image/png;base64,AAAA)\n");
  const QImage img = renderParagraphImage(plainTheme(), md);
  const QRect red = colorBBox(img, isRed);
  require(!red.isValid(), QStringLiteral("broken data: URI should render the placeholder, not a coloured image"));
}

} // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testDocumentRelativeImagesAndIndependentBases);
  RUN_TEST(testDownsamplingPreservesThinStrokes);
  RUN_TEST(testImageDoesNotOverflowAboveItsBlock);
  RUN_TEST(testPercentEncodedDataUriRendersImage);
  RUN_TEST(testBase64DataUriRendersImage);
  RUN_TEST(testBrokenDataUriShowsPlaceholder);
#undef RUN_TEST
  return 0;
}
