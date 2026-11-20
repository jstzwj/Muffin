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

QImage paintTable(const BlockLayout& block, const RenderTheme& theme, QColor bg, qreal dpr = 1) {
  const QRectF rect = block.rect();
  QImage image(qCeil(rect.width() * dpr), qCeil((rect.height() + 4) * dpr), QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(dpr);
  image.fill(bg);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.translate(-rect.left(), -rect.top());
  block.paint(painter, theme, 0.0);
  return image;
}

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
  return paintTable(*block, theme, bg);
}

// Count near-solid-black pixels. Text colour is #444 (above the threshold) so only an
// invalid-colour cell fill (true #000) registers.
int blackPixelCount(const QImage& image) {
  int n = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb p = image.pixel(x, y);
      if (qRed(p) < 30 && qGreen(p) < 30 && qBlue(p) < 30) {
        ++n;
      }
    }
  }
  return n;
}

// whitey-style: page bg + borders, but NO `th` / `tr:nth-child(even)` background. The
// header/stripe tokens stay invalid; the renderer must fall back to the page background,
// not paint black.
void testUndeclaredTableBackgroundIsNotBlack() {
  const QString css =
      QStringLiteral("#write { color:#444444; background:#fefefe; }"
                     "table, table th, table td { border:1px solid #dddddd; }"); // no background declared
  const RenderTheme theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("nobg"), QString()));
  require(!theme.tableHeaderBackgroundColor().isValid(),
          QStringLiteral("precondition: a theme with no th bg must leave the header token invalid"));
  require(!theme.tableAlternateBackgroundColor().isValid(),
          QStringLiteral("precondition: a theme with no tr:nth-child bg must leave the stripe token invalid"));

  const QImage image =
      renderTableImage(theme, QStringLiteral("| H1 | H2 |\n| --- | --- |\n| a | b |\n| c | d |\n"), QColor("#fefefe"));
  const int black = blackPixelCount(image);
  require(black == 0,
          QStringLiteral("table with no declared bg must have no solid-black cells (got %1 black px)").arg(black));
}

// A theme that DOES declare header + stripe backgrounds still paints them (regression
// guard: the validity fallback must not suppress declared colours).
void testDeclaredTableBackgroundPaints() {
  const QString css = QStringLiteral("#write { color:#000000; }"
                                     "table th { background:#1122aa; }"                   // distinct blue header
                                     "tbody tr:nth-child(even) { background:#22aa55; }"); // distinct green stripe
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("bg"), QString()));
  const QImage image =
      renderTableImage(theme, QStringLiteral("| H1 | H2 |\n| --- | --- |\n| a | b |\n| c | d |\n"), Qt::white);
  bool hasBlue = false, hasGreen = false;
  for (int y = 0; y < image.height() && !(hasBlue && hasGreen); ++y) {
    for (int x = 0; x < image.width() && !(hasBlue && hasGreen); ++x) {
      const QRgb p = image.pixel(x, y);
      if (qBlue(p) > 120 && qRed(p) < 80) {
        hasBlue = true;
      }
      if (qGreen(p) > 120 && qRed(p) < 100 && qBlue(p) < 100) {
        hasGreen = true;
      }
    }
  }
  require(hasBlue, QStringLiteral("declared header background (#1122aa) should paint"));
  require(hasGreen, QStringLiteral("declared stripe background (#22aa55) should paint"));
}

// Sample across a particular grid line, well away from text and intersections.
int borderPixels(const QImage& image, QPointF point, bool vertical, QColor color) {
  const QPoint center = (point * image.devicePixelRatio()).toPoint();
  int count = 0;
  for (int i = -8; i <= 8; ++i) {
    const QPoint p = center + (vertical ? QPoint(i, 0) : QPoint(0, i));
    if (image.rect().contains(p) && image.pixelColor(p) == color)
      ++count;
  }
  return count;
}

void testSharedTableEdges() {
  DocumentSession session;
  session.setMarkdownText("| Header | Value |\n| --- | --- |\n| First | 42 |\n| Second | 99 |\n", false);
  const auto* node = findFirstBlock(session.document().root(), BlockType::Table);
  require(node, "expected a Markdown table");
  for (bool separate : {false, true}) {
    // The document base selects collapse. An explicit theme override must still select separate.
    // Align the independent strokes too so the DPR=1 reference has exact pixel colours.
    const QString css = QStringLiteral("table { width:400px; } th, td { height:40px; box-sizing:border-box; }"
                                       "th, td, tr { border:1px solid #dfe2e5; }") +
                        (separate ? QStringLiteral("table { border-collapse:separate; }") : QString());
    const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, "borders", {}));
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 800);
    const auto* table = layout.block(node->id());
    const auto& left = table->tableRows().at(1).cells.at(0);
    const auto& right = table->tableRows().at(1).cells.at(1);
    const qreal inset = separate ? 1 : .5;
    require(left.box.usedBox.borderRightWidth == inset && right.box.usedBox.borderLeftWidth == inset,
            "adjacent cells reserve their share of the border");
    const auto caret = right.text.cursorRectForSourceOffset(0).translated(right.textOrigin());
    const auto hit = table->hitTest(caret.center(), theme);
    require(hit.textNodeId == right.nodeId && hit.sourceOffset == right.contentSourceStart && hit.cursorRect == caret,
            "hit testing restores the caret at the painted text origin");

    const QRectF cell = left.rect.translated(-table->rect().topLeft());
    const QColor border("#dfe2e5");
    for (qreal dpr : {1., 1.25, 2.}) {
      // Separate strokes rasterize independently, with scale-dependent rounding.
      if (separate && dpr != 1)
        continue;
      const auto image = paintTable(*table, theme, Qt::white, dpr);
      const auto outer = borderPixels(image, {cell.left(), cell.center().y()}, true, border);
      const auto inner = borderPixels(image, {cell.right(), cell.center().y()}, true, border);
      const auto horizontal = borderPixels(image, {cell.center().x(), cell.bottom()}, false, border);
      const int expected = separate ? 2 : qRound(dpr);
      require(outer == qRound(dpr) && inner == expected && horizontal == expected,
              QStringLiteral("border pixels at DPR %1, separate %2: outer %3, internal %4 and %5")
                  .arg(dpr)
                  .arg(separate)
                  .arg(outer)
                  .arg(inner)
                  .arg(horizontal));
    }
  }
}

void testCollapsedBorderConflicts() {
  DocumentSession session;
  session.setMarkdownText("| A | B |\n| --- | --- |\n| C | D |\n", false);
  const auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "table { border:2px solid green; } thead { border-bottom:4px solid blue; }"
      "tr { border:2px solid red; } td, th { border:2px solid purple; }"
      "td:first-child { border-right:6px solid orange; } td:last-child { border-left-style:HIDDEN; }",
      "conflicts", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800);
  const auto* table = layout.block(session.document().root().children().front()->id());
  const auto& cell = table->tableRows().at(1).cells.at(0);
  require(cell.box.usedBox.borderRightWidth == 0, "hidden suppresses a wider neighbour");
  require(cell.box.usedBox.borderTopWidth == 2, "each cell reserves half of the winning row-group border");
  const auto image = paintTable(*table, theme, Qt::white);
  const auto rect = cell.rect.translated(-table->rect().topLeft());
  require(borderPixels(image, {rect.center().x(), rect.top()}, false, Qt::blue) == 4,
          "the wider row-group border spans the header/body boundary");
  require(borderPixels(image, {rect.left(), rect.center().y()}, true, QColor("purple")) == 2,
          "a cell wins a tie with row and table perimeter borders");
  require(borderPixels(image, {rect.right(), rect.center().y()}, true, QColor("orange")) == 0,
          "a hidden shared edge is unpainted");
}

void testTranslucentTableJunction() {
  DocumentSession session;
  session.setMarkdownText("| A | B |\n| --- | --- |\n| C | D |\n", false);
  const auto theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss("th, td { border:4px solid rgba(0,0,0,.5); }", "alpha", {}));
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800);
  const auto* table = layout.block(session.document().root().children().front()->id());
  const auto rect = table->tableRows().front().cells.front().rect.translated(-table->rect().topLeft());
  const auto image = paintTable(*table, theme, Qt::white);
  const QColor stroke = image.pixelColor(qRound(rect.right()), qRound(rect.center().y()));
  require(qAbs(stroke.red() - 127) <= 1 && image.pixelColor(rect.bottomRight().toPoint()) == stroke,
          "a translucent intersection receives a single coat of ink");
}

} // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testUndeclaredTableBackgroundIsNotBlack);
  RUN_TEST(testDeclaredTableBackgroundPaints);
  RUN_TEST(testSharedTableEdges);
  RUN_TEST(testCollapsedBorderConflicts);
  RUN_TEST(testTranslucentTableJunction);
#undef RUN_TEST
  return 0;
}
