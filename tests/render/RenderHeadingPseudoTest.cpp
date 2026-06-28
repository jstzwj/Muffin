#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QRgb>

#include <functional>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Render a single heading block into an opaque white image translated to the
// block's origin, so rect.left() maps to image x = 0.
QImage renderHeadingImage(const RenderTheme& theme, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  require(heading != nullptr, QStringLiteral("fixture should contain a heading"));
  const BlockLayout* block = layout.block(heading->id());
  require(block != nullptr, QStringLiteral("heading block should be promoted"));
  const QRectF rect = block->rect();
  require(rect.width() > 100.0 && rect.height() > 4.0, QStringLiteral("heading block should be non-trivial"));

  QImage image(int(rect.width()), int(rect.height() + 4), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr);
  painter.end();
  return image;
}

// Leftmost x where a pixel matches `pred` (scanning columns left→right), or -1.
int leftmostX(const QImage& image, const std::function<bool(QRgb)>& pred) {
  for (int x = 0; x < image.width(); ++x) {
    for (int y = 0; y < image.height(); ++y) {
      if (pred(image.pixel(x, y))) { return x; }
    }
  }
  return -1;
}

int rightmostX(const QImage& image, const std::function<bool(QRgb)>& pred) {
  for (int x = image.width() - 1; x >= 0; --x) {
    for (int y = 0; y < image.height(); ++y) {
      if (pred(image.pixel(x, y))) { return x; }
    }
  }
  return -1;
}

const std::function<bool(QRgb)> isRed = [](QRgb p) {
  return qRed(p) > 150 && qGreen(p) < 90 && qBlue(p) < 90;
};
const std::function<bool(QRgb)> isBlack = [](QRgb p) {
  return qRed(p) < 80 && qGreen(p) < 80 && qBlue(p) < 80;
};

const QString kBase = QStringLiteral("#write { color:#000000; }");

// h3 `::before { position:absolute; left:0; … }` paints a bar at the heading's
// left edge while the (padding-inset) text stays clear of it.
void testAbsoluteBeforeBarAtLeftEdge() {
  const QString css = kBase + QStringLiteral(
      "#write h3 { padding-left:15px; }"
      "#write h3::before { content:''; position:absolute; left:0; width:4px; height:16px;"
      "  background-color:#d00000; border-radius:2px; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("abs"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("### Heading Three\n"));
  const int red = leftmostX(img, isRed);
  const int black = leftmostX(img, isBlack);
  require(red >= 0, QStringLiteral("absolute ::before bar should paint red ink"));
  require(red < 6, QStringLiteral("absolute bar should sit at the heading left edge (red leftmost=%1)").arg(red));
  require(black > 10, QStringLiteral("heading text should be inset past the bar (black leftmost=%1)").arg(black));
}

// An inline `::before` disc (h4) reserves left space, shifting the text right of
// the marker. A control theme without the marker has text at the left edge.
void testInlineBeforeDiscShiftsText() {
  const QString withCss = kBase + QStringLiteral(
      "#write h4::before { content:''; width:8px; height:8px; border-radius:50%;"
      "  background-color:#d00000; margin-right:10px; }");
  const RenderTheme withTheme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(withCss, QStringLiteral("disc"), QString()));
  const QImage withImg = renderHeadingImage(withTheme, QStringLiteral("#### Heading Four\n"));
  const int withRed = leftmostX(withImg, isRed);
  const int withBlack = leftmostX(withImg, isBlack);
  require(withRed >= 0 && withRed < 6, QStringLiteral("inline disc should sit at the left edge (red=%1)").arg(withRed));
  require(withBlack > 12, QStringLiteral("text should be shifted right of the disc (black=%1)").arg(withBlack));

  const QImage ctrlImg = renderHeadingImage(RenderTheme::github(), QStringLiteral("#### Heading Four\n"));
  const int ctrlBlack = leftmostX(ctrlImg, isBlack);
  require(ctrlBlack >= 0, QStringLiteral("control heading should paint text"));
  require(ctrlBlack < 8, QStringLiteral("control heading text should start at the left edge (black=%1)").arg(ctrlBlack));
  require(withBlack > ctrlBlack + 8, QStringLiteral("disc heading text should start well right of control text"));
}

// A text `::before` marker (h6 "-") also reserves space and shifts the text.
void testInlineBeforeDashShiftsText() {
  const QString css = kBase + QStringLiteral(
      "#write h6::before { content:'-'; color:#d00000; margin-right:7px; font-weight:700; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("dash"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("###### Heading Six\n"));
  const int red = leftmostX(img, isRed);
  const int black = leftmostX(img, isBlack);
  require(red >= 0, QStringLiteral("text ::before '-' should paint red ink"));
  require(black > 10, QStringLiteral("text should be shifted right of the dash (black=%1)").arg(black));

  const QImage ctrlImg = renderHeadingImage(RenderTheme::github(), QStringLiteral("###### Heading Six\n"));
  require(black > leftmostX(ctrlImg, isBlack) + 8, QStringLiteral("dash heading text should start well right of control text"));
}

// The h5 hollow ring (border only, no fill) still paints and shifts text — this
// exercises the outline path (borderColor/borderWidth without backgroundColor).
void testInlineBeforeHollowRingShiftsText() {
  const QString css = kBase + QStringLiteral(
      "#write h5::before { content:''; width:8px; height:8px; border-radius:50%;"
      "  border:1.5px solid #d00000; margin-right:10px; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("ring"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("##### Heading Five\n"));
  const int red = leftmostX(img, isRed);
  const int black = leftmostX(img, isBlack);
  require(red >= 0 && red < 6, QStringLiteral("hollow ring outline should paint at the left edge (red=%1)").arg(red));
  require(black > 12, QStringLiteral("text should be shifted right of the ring (black=%1)").arg(black));
}

// A `::after` mask icon paints to the RIGHT of the heading text (not overlapping
// it). Uses a tiny inline SVG mask so the icon is a solid red shape.
void testAfterIconPaintsRightOfText() {
  const QString svg = QStringLiteral("url(\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'>"
                                     "<rect width='10' height='10' fill='%2300d000'/></svg>\")");
  const QString css = kBase + QStringLiteral(
      "#write h3::after { content:''; width:10px; height:10px; margin-left:4px;"
      "  -webkit-mask:%1 center/contain; mask:%1 center/contain; background-color:#00d000; }").arg(svg);
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("after"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("### Hi\n"));
  const std::function<bool(QRgb)> isGreen = [](QRgb p) {
    return qGreen(p) > 150 && qRed(p) < 90 && qBlue(p) < 90;
  };
  const int blackRight = rightmostX(img, isBlack);
  const int greenLeft = leftmostX(img, isGreen);
  require(greenLeft >= 0, QStringLiteral("::after mask icon should paint green ink"));
  require(blackRight >= 0, QStringLiteral("heading text should paint black ink"));
  require(greenLeft > blackRight, QStringLiteral("::after icon should sit right of the text (green=%1 > blackRight=%2)")
                                     .arg(greenLeft).arg(blackRight));
}

// CSS `content: none` (and `normal`) on ::before/::after means "no generated
// content", not the literal word "none". newsprint declares `blockquote:before
// { content:''; content:none }`; bestValue picks the later `none`, so without the
// guard the blockquote ::before content was stored as "none" and painted verbatim
// before every blockquote. Assert the rule's content is blanked.
void testContentNoneIsNotLiteralText() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "blockquote:before, blockquote:after { content:''; content:none; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("none"), QString()));
  const PseudoElementRule* rule = nullptr;
  for (const PseudoElementRule& r : theme.decorations().pseudos) {
    if (r.host == QStringLiteral("blockquote") && r.pseudo == QStringLiteral("before")) { rule = &r; break; }
  }
  require(rule != nullptr, QStringLiteral("blockquote::before rule should be extracted"));
  require(rule->content.isEmpty(),
          QStringLiteral("content:none must suppress the pseudo (got literal '%1')").arg(rule->content));

  // `content: normal` is the same no-content keyword.
  const QString css2 = QStringLiteral("#write { color:#000000; } h1::before { content: normal; }");
  const RenderTheme theme2 = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css2, QStringLiteral("normal"), QString()));
  const PseudoElementRule* r2 = nullptr;
  for (const PseudoElementRule& r : theme2.decorations().pseudos) {
    if (r.host == QStringLiteral("h1") && r.pseudo == QStringLiteral("before")) { r2 = &r; break; }
  }
  require(r2 != nullptr, QStringLiteral("h1::before rule should be extracted"));
  require(r2->content.isEmpty(), QStringLiteral("content:normal must suppress the pseudo"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testAbsoluteBeforeBarAtLeftEdge);
  RUN_TEST(testInlineBeforeDiscShiftsText);
  RUN_TEST(testInlineBeforeDashShiftsText);
  RUN_TEST(testInlineBeforeHollowRingShiftsText);
  RUN_TEST(testAfterIconPaintsRightOfText);
  RUN_TEST(testContentNoneIsNotLiteralText);
#undef RUN_TEST
  return 0;
}
