// A theme that declares NO table header/stripe background (e.g. whitey: only
// padding/borders) must not paint cells SOLID BLACK. The header/stripe colour tokens
// resolve invalid for such a theme; before the fix the renderer filled invalid as black.
// After the fix, cells fall back to the page background.

#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QImage>
#include <QPainter>

#include <functional>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Render the first table block to an image filled with `bg` (so unpainted cells blend in).
QImage renderTableImage(const RenderTheme& theme, const QString& markdown, QColor bg) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* table = findFirstBlock(session.document().root(), BlockType::Table);
  require(table != nullptr, QStringLiteral("fixture should contain a table"));
  const BlockLayout* block = layout.block(table->id());
  require(block != nullptr, QStringLiteral("table block should be promoted"));
  const QRectF rect = block->rect();
  QImage image(int(rect.width()), int(rect.height() + 4), QImage::Format_ARGB32_Premultiplied);
  image.fill(bg);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr);
  painter.end();
  return image;
}

// Count near-solid-black pixels. Text colour is #444 (above the threshold) so only an
// invalid-colour cell fill (true #000) registers.
int blackPixelCount(const QImage& image) {
  int n = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb p = image.pixel(x, y);
      if (qRed(p) < 30 && qGreen(p) < 30 && qBlue(p) < 30) { ++n; }
    }
  }
  return n;
}

// whitey-style: page bg + borders, but NO `th` / `tr:nth-child(even)` background. The
// header/stripe tokens stay invalid; the renderer must fall back to the page background,
// not paint black.
void testUndeclaredTableBackgroundIsNotBlack() {
  const QString css = QStringLiteral(
      "#write { color:#444444; background:#fefefe; }"
      "table, table th, table td { border:1px solid #dddddd; }");  // no background declared
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("nobg"), QString()));
  require(!theme.tableHeaderBackgroundColor().isValid(),
          QStringLiteral("precondition: a theme with no th bg must leave the header token invalid"));
  require(!theme.tableAlternateBackgroundColor().isValid(),
          QStringLiteral("precondition: a theme with no tr:nth-child bg must leave the stripe token invalid"));

  const QImage image = renderTableImage(theme, QStringLiteral("| H1 | H2 |\n| --- | --- |\n| a | b |\n| c | d |\n"), QColor("#fefefe"));
  const int black = blackPixelCount(image);
  require(black == 0,
          QStringLiteral("table with no declared bg must have no solid-black cells (got %1 black px)").arg(black));
}

// A theme that DOES declare header + stripe backgrounds still paints them (regression
// guard: the validity fallback must not suppress declared colours).
void testDeclaredTableBackgroundPaints() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "table th { background:#1122aa; }"               // distinct blue header
      "tbody tr:nth-child(even) { background:#22aa55; }");  // distinct green stripe
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("bg"), QString()));
  const QImage image = renderTableImage(theme, QStringLiteral("| H1 | H2 |\n| --- | --- |\n| a | b |\n| c | d |\n"), Qt::white);
  bool hasBlue = false, hasGreen = false;
  for (int y = 0; y < image.height() && !(hasBlue && hasGreen); ++y) {
    for (int x = 0; x < image.width() && !(hasBlue && hasGreen); ++x) {
      const QRgb p = image.pixel(x, y);
      if (qBlue(p) > 120 && qRed(p) < 80) { hasBlue = true; }
      if (qGreen(p) > 120 && qRed(p) < 100 && qBlue(p) < 100) { hasGreen = true; }
    }
  }
  require(hasBlue, QStringLiteral("declared header background (#1122aa) should paint"));
  require(hasGreen, QStringLiteral("declared stripe background (#22aa55) should paint"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testUndeclaredTableBackgroundIsNotBlack);
  RUN_TEST(testDeclaredTableBackgroundPaints);
#undef RUN_TEST
  return 0;
}
