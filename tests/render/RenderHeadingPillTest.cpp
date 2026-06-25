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

// Bounding box (inclusive) of pixels matching `pred`, or an empty rect if none.
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

const std::function<bool(QRgb)> isGreen = [](QRgb p) {
  return qGreen(p) > 120 && qRed(p) < 100 && qBlue(p) < 100;
};
const std::function<bool(QRgb)> isRed = [](QRgb p) {
  return qRed(p) > 150 && qGreen(p) < 90 && qBlue(p) < 90;
};
bool isWhite(QRgb p) { return qRed(p) > 230 && qGreen(p) > 230 && qBlue(p) > 230; }

// phycat-style h2: a fit-content pill with rounded corners, a fill, and a top
// hairline. Exercises Phase 2c: paintElementBackground must round the box, fill
// it, and draw the border-top — all clipped to the rounded shape.
void testRoundedPillWithTopBorder() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "#write h2 { width:fit-content; padding:0 14px; border-radius:10px;"
      "  background-image:linear-gradient(#00aa00,#00aa00); border-top:3px solid #d00000; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("pill"), QString()));
  const QImage img = renderHeadingImage(theme, QStringLiteral("## Hi\n"));
  const QRect green = colorBBox(img, isGreen);
  const QRect red = colorBBox(img, isRed);
  require(green.isValid(), QStringLiteral("rounded pill should paint its green fill"));
  require(red.isValid(), QStringLiteral("rounded pill should paint its red top border"));
  // Border-top sits at the very top of the pill (the hairline is the highest red).
  require(red.top() < 4, QStringLiteral("border-top should be at the pill's top edge (red top=%1)").arg(red.top()));
  // Corner rounding: the top-left pixel of the pill (0,0) is the page background
  // (white), not fill/border — a square pill would paint it. The green fill
  // starts inset from the left edge at the top row (the arc cuts the corner).
  require(isWhite(img.pixel(0, 0)), QStringLiteral("rounded corner should be transparent (pixel(0,0) white)"));

  // Control: the same pill WITHOUT border-radius has a square corner — pixel(0,0)
  // is painted (red border-top spans the full square top), proving the rounding
  // above is what cut the corner.
  const QString squareCss = QStringLiteral(
      "#write { color:#000000; }"
      "#write h2 { width:fit-content; padding:0 14px;"
      "  background-image:linear-gradient(#00aa00,#00aa00); border-top:3px solid #d00000; }");
  const RenderTheme squareTheme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(squareCss, QStringLiteral("sq"), QString()));
  const QImage squareImg = renderHeadingImage(squareTheme, QStringLiteral("## Hi\n"));
  require(!isWhite(squareImg.pixel(0, 0)),
          QStringLiteral("square-pill corner (0,0) should be painted, unlike the rounded pill"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testRoundedPillWithTopBorder);
#undef RUN_TEST
  return 0;
}
