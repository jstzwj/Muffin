#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QRgb>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

// Render a single heading block into an opaque image sized to its rect, over a
// white page. Returns the ink bounds (heading background + text) vs white, and
// the content (block) width so callers can make relative assertions.
struct HeadingInk { QRect ink; int contentWidth = 0; };

HeadingInk renderHeadingInk(const RenderTheme& theme, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  require(heading != nullptr, QStringLiteral("fixture should contain a heading"));
  const BlockLayout* block = layout.block(heading->id());
  require(block != nullptr, QStringLiteral("heading block should be promoted"));
  const QRectF rect = block->rect();
  require(rect.width() > 100.0, QStringLiteral("heading block should be reasonably wide"));

  QImage image(int(rect.width()), int(rect.height()), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr);
  painter.end();
  return {imageInkBounds(image, Qt::white), int(rect.width())};
}

struct HeadingGeometry { QRectF borderBox; QRectF hoverBox; QRectF blockRect; };

HeadingGeometry headingGeometry(const RenderTheme& theme, const QString& markdown) {
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  require(heading != nullptr, QStringLiteral("fixture should contain a heading"));
  const BlockLayout* block = layout.block(heading->id());
  require(block != nullptr, QStringLiteral("heading block should be promoted"));
  return {block->cssBorderBox(theme), block->visualOverflowRect(theme), block->rect()};
}

// CSS `filter: blur()` on a heading background bleeds colour OUTSIDE the heading's
// border box (the blur halo). Without the filter, a solid red background stays
// inside the box. End-to-end proof of the offscreen-render + boxBlur + composite
// path in DecorationPainter::paintElementBackground.
void testFilterBlurBleedsOutsideBorderBox() {
  struct RedCounts { int inside = 0; int outside = 0; };
  auto counts = [](const QString& css) {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("## Blur Me\n"), false);
    const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("fb"), QString()));
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 800.0);
    const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
    const BlockLayout* block = layout.block(heading->id());
    const QRectF rect = block->rect();
    const int margin = 40;  // room for the blur halo to land inside the image
    QImage image(int(rect.width()) + margin * 2, int(rect.height()) + margin * 2, QImage::Format_ARGB32);
    image.fill(QColor(Qt::white).rgb());
    QPainter painter(&image);
    painter.translate(margin - rect.left(), margin - rect.top());
    block->paint(painter, theme, 0.0, nullptr);
    painter.end();
    const QRectF box = block->cssBorderBox(theme).translated(margin - rect.left(), margin - rect.top());
    RedCounts out;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const QRgb p = image.pixel(x, y);
        // "reddish" — catches solid red AND the pinkish fringe of a red blurred over white.
        if (qRed(p) > qGreen(p) + 40 && qRed(p) > qBlue(p) + 40) {
          if (box.contains(QPointF(x, y))) { ++out.inside; } else { ++out.outside; }
        }
      }
    }
    return out;
  };
  const QString base = QStringLiteral("#write { color:#222222; } #write h2 { background-image:linear-gradient(#ff0000,#ff0000); }");
  const QString blurred = QStringLiteral("#write { color:#222222; } #write h2 { background-image:linear-gradient(#ff0000,#ff0000); filter: blur(8px); }");
  const RedCounts baseCounts = counts(base);
  require(baseCounts.inside > 50, QStringLiteral("red background should paint inside the box (inside=%1)").arg(baseCounts.inside));
  require(baseCounts.outside == 0, QStringLiteral("no filter ⇒ red stays inside the box (outside=%1)").arg(baseCounts.outside));
  const RedCounts blurredCounts = counts(blurred);
  require(blurredCounts.outside > 50, QStringLiteral("filter: blur(8px) ⇒ red halo bleeds outside the box (outside=%1)").arg(blurredCounts.outside));
}

// `width: fit-content` on a heading shrinks its own background box to the text
// (a left-aligned pill), so the painted background ink stops well before the
// right edge of the content column. Without fit-content the same background
// spans the full block width. This is the Phase 2 paint contract that turns
// phycat's h2 from a full-width bar into the Typora-style pill. The assertion
// is relative (ink right edge vs block width), so it is robust to the offscreen
// font-metric differences that make absolute geometry unreliable.
void testFitContentHeadingHoverUsesSameBorderBox() {
  const QString css = QStringLiteral(
      "#write { color:#000000; }"
      "#write h2 { width:fit-content; padding:0 12px; background:#eeeeee; }"
      "#write h2:hover { box-shadow:0 0 16px #d00000; }");
  const ThemeDefinition def = CssThemeMapper::fromCss(css, QStringLiteral("hover-pill"), QString());
  const RenderTheme theme = RenderTheme::fromDefinition(def);
  const HeadingGeometry g = headingGeometry(theme, QStringLiteral("## Short Heading\n"));
  require(g.borderBox.isValid(), QStringLiteral("heading CSS border box should be valid"));
  require(g.borderBox.width() < g.blockRect.width() * 0.75,
          QStringLiteral("fit-content heading CSS border box should be narrower than full block"));
  require(g.hoverBox.left() < g.borderBox.left() && g.hoverBox.right() > g.borderBox.right(),
          QStringLiteral("visual overflow should include hover blur around the same border box"));
  require(g.hoverBox.right() < g.blockRect.right() * 0.85,
          QStringLiteral("hover overflow should not span the full row"));
}

void testFitContentHeadingBackgroundIsPill() {
  const QString pillCss = QStringLiteral(
      "#write { color:#000000; }"
      "#write h2 { width:fit-content; padding:0 12px; background-image:linear-gradient(#d00000,#d00000); }");
  const ThemeDefinition pillDef = CssThemeMapper::fromCss(pillCss, QStringLiteral("pill"), QString());
  const RenderTheme pillTheme = RenderTheme::fromDefinition(pillDef);
  require(pillTheme.headingFitContent(2), QStringLiteral("h2 width:fit-content must set the flag"));
  const HeadingInk pill = renderHeadingInk(pillTheme, QStringLiteral("## Short Heading\n"));
  require(pill.ink.isValid(), QStringLiteral("fit-content heading should paint visible ink"));
  require(pill.contentWidth > 400, QStringLiteral("block should be wide enough for a meaningful pill test"));
  // The pill ends well inside the block's right edge (short heading + 24px pad).
  require(pill.ink.right() < pill.contentWidth * 0.75,
          QStringLiteral("fit-content heading background should be a left pill, not full-width (ink right=%1/%2)")
              .arg(pill.ink.right()).arg(pill.contentWidth));

  const QString fullCss = QStringLiteral(
      "#write { color:#000000; }"
      "#write h2 { padding:0 12px; background-image:linear-gradient(#d00000,#d00000); }");
  const ThemeDefinition fullDef = CssThemeMapper::fromCss(fullCss, QStringLiteral("full"), QString());
  const RenderTheme fullTheme = RenderTheme::fromDefinition(fullDef);
  require(!fullTheme.headingFitContent(2), QStringLiteral("h2 with no width must stay full-width"));
  const HeadingInk full = renderHeadingInk(fullTheme, QStringLiteral("## Short Heading\n"));
  require(full.ink.isValid(), QStringLiteral("full-width heading should paint visible ink"));
  // Full-width background reaches the block's right edge.
  require(full.ink.right() > full.contentWidth * 0.9,
          QStringLiteral("non-fit-content heading background should span the block width (ink right=%1/%2)")
              .arg(full.ink.right()).arg(full.contentWidth));
  require(full.ink.right() > pill.ink.right() + 150,
          QStringLiteral("fit-content pill should be dramatically narrower than full-width background"));
}

// Regression for the h1 hover effect (phycat): on hover the heading text must
// recolour toward `h1:hover { color }` (animated by the HoverAnimator phase) while
// the ::after underline widens — WITHOUT the text vanishing or the bar jumping to
// the left edge. The earlier setFormats-after-endLayout approach corrupted the
// QTextLayout: draw() rendered nothing AND visualTextBounds() went degenerate,
// shoving the ::after bar to the border-box left edge.
struct HoverPixels { int cyanish = 0; int darkish = 0; int cyanAboveBar = 0; QRect ink; };

HoverPixels renderHoverHeading(const RenderTheme& theme, BlockLayout::BlockPaintState hover) {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# Heading One\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  require(heading != nullptr, QStringLiteral("fixture should contain a heading"));
  const BlockLayout* block = layout.block(heading->id());
  require(block != nullptr, QStringLiteral("heading block should be promoted"));
  const QRectF rect = block->rect();
  QImage image(int(rect.width()), int(rect.height()), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr, hover);
  painter.end();
  HoverPixels out;
  out.ink = imageInkBounds(image, Qt::white);
  const int barZoneY = image.height() - 8;  // ::after underline sits in the bottom rows
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb p = image.pixel(x, y);
      if (p == QColor(Qt::white).rgb()) { continue; }
      const int r = qRed(p), g = qGreen(p), b = qBlue(p);
      if (b > 120 && b >= r && g > r) { ++out.cyanish; if (y < barZoneY) { ++out.cyanAboveBar; } }
      if (r < 90 && g < 90 && b < 90) { ++out.darkish; }
    }
  }
  return out;
}

void testHoverHeadingRecolourKeepsTextAndBar() {
  // Mimics phycat's h1: fit-content + centred, letter/word spacing (these put the
  // QTextLayout in the state that exposed the setFormats-after-endLayout bug), a
  // 40px ::after underline that widens to 100% on hover, and a hover text colour.
  const QString css = QStringLiteral(
      "#write { color:#222222; letter-spacing:0.5px; word-spacing:1px; }"
      "#write h1 { color:#222222; width:fit-content; text-align:center; padding-bottom:12px; }"
      "#write h1::after { content:''; position:absolute; bottom:0; left:50%; width:40px; height:4px;"
      "  background:linear-gradient(to right,#80F7C4,#3DB8D3,#80F7C4); }"
      "#write h1:hover { color:#3db8bf; }"
      "#write h1:hover::after { width:100%; }");
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("hover"), QString()));
  const HoverPixels base = renderHoverHeading(theme, BlockLayout::BlockPaintState{false, 0.0});
  const HoverPixels hover = renderHoverHeading(theme, BlockLayout::BlockPaintState{true, 1.0});
  require(base.darkish > 20, QStringLiteral("base heading should paint dark text (dark=%1)").arg(base.darkish));
  // Text survives hover and turns cyan (ink ABOVE the underline bar).
  require(hover.cyanAboveBar > 20,
          QStringLiteral("hover text should turn cyan above the bar (cyanAboveBar=%1; base dark=%2)").arg(hover.cyanAboveBar).arg(base.darkish));
  // h1 is text-align:center, so hover ink (text + bar) sits in the centred text
  // region — not jammed against the left edge (the degenerate-bar symptom).
  require(hover.ink.x() > 100,
          QStringLiteral("hover ink should be centred under the text, not at the left edge (ink.x=%1)").arg(hover.ink.x()));
  require(hover.ink.width() > 80,
          QStringLiteral("hover ink should span text + bar (ink.w=%1)").arg(hover.ink.width()));
}

// The headline correctness guard for the hover recolour: a heading containing a
// link and inline code. On hover the heading's OWN text recolours toward
// `h1:hover { color }`, but the link and the code must keep their OWN colours —
// the temp-image SourceIn overlay this replaced tinted every glyph in the bounds
// (visible "other styles affected"). Selection-based recolouring is per-run.
struct SpanHoverPixels { int cyanish = 0; int reddish = 0; int blueish = 0; int greenish = 0; };

SpanHoverPixels renderSpanHoverHeading(const RenderTheme& theme, BlockLayout::BlockPaintState hover) {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("# See [site](https://example.com) and `tag` here\n"), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);
  const MarkdownNode* heading = findFirstBlock(session.document().root(), BlockType::Heading);
  require(heading != nullptr, QStringLiteral("fixture should contain a heading"));
  const BlockLayout* block = layout.block(heading->id());
  require(block != nullptr, QStringLiteral("heading block should be promoted"));
  const QRectF rect = block->rect();
  QImage image(int(rect.width()), int(rect.height()), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.translate(-rect.left(), -rect.top());
  block->paint(painter, theme, 0.0, nullptr, hover);
  painter.end();
  SpanHoverPixels out;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb p = image.pixel(x, y);
      if (p == QColor(Qt::white).rgb()) { continue; }
      const int r = qRed(p), g = qGreen(p), b = qBlue(p);
      if (b > 120 && g > 120 && r < 120) { ++out.cyanish; }        // recoloured plain text
      else if (r > 150 && g < 110 && b < 110) { ++out.reddish; }   // preserved inline code
      else if (b > 150 && r < 110 && g < 110) { ++out.blueish; }   // preserved link
      else if (g > 120 && r < 110 && b < 110) { ++out.greenish; }  // recoloured plain text (focus→green)
    }
  }
  return out;
}

void testHoverRecolourPreservesLinkAndCodeColours() {
  const QString css = QStringLiteral(
      "#write { color:#222222; }"
      "#write h1 { color:#222222; width:fit-content; text-align:center; }"
      "#write h1:hover { color:#3db8bf; }"      // cyan hover target
      "#write a { color:#0000ff; }"              // blue links
      "#write code { color:#ff0000; background:#f0f0f0; }");  // red inline code
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("spans"), QString()));
  const SpanHoverPixels hover = renderSpanHoverHeading(theme, BlockLayout::BlockPaintState{true, 1.0});
  // Heading's own text recolours toward cyan.
  require(hover.cyanish > 30,
          QStringLiteral("hover plain text should turn cyan (cyanish=%1)").arg(hover.cyanish));
  // Link and inline code keep their own colours — NOT recoloured by the hover.
  require(hover.blueish > 10,
          QStringLiteral("link text should keep blue on hover, not be tinted (blueish=%1)").arg(hover.blueish));
  require(hover.reddish > 10,
          QStringLiteral("inline code should keep red on hover, not be tinted (reddish=%1)").arg(hover.reddish));
}

// :focus is the orthogonal-to-hover interactive state (the caret block). Driven by
// the FocusAnimator phase, it recolours the heading's OWN text toward `h1:focus
// { color }` via the SAME inherited-base-run selection mechanism — so links/code
// keep their colours. Proves the focus plumbing end to end (mapper builds
// h1:focus → builder sets focusTextColor → InlineLayout blends with focusPhase),
// independent of hover (paintState here is focus-only).
void testFocusRecolourPreservesLinkAndCodeColours() {
  const QString css = QStringLiteral(
      "#write { color:#222222; }"
      "#write h1 { color:#222222; width:fit-content; text-align:center; }"
      "#write h1:focus { color:#1aa85a; }"     // green focus target
      "#write a { color:#0000ff; }"             // blue links
      "#write code { color:#ff0000; background:#f0f0f0; }");  // red inline code
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("fspans"), QString()));
  // focus-only state: hover inactive, focus at full phase.
  const SpanHoverPixels focus = renderSpanHoverHeading(theme, BlockLayout::BlockPaintState{false, 0.0, true, 1.0});
  require(focus.greenish > 30,
          QStringLiteral("focus plain text should turn green (greenish=%1)").arg(focus.greenish));
  require(focus.blueish > 10,
          QStringLiteral("link text should keep blue on focus, not be tinted (blueish=%1)").arg(focus.blueish));
  require(focus.reddish > 10,
          QStringLiteral("inline code should keep red on focus, not be tinted (reddish=%1)").arg(focus.reddish));
}

// Empirical probe of this Qt build's QTextLayout. The hover-text recolour must
// NOT be implemented via QTextLayout::setFormats() after endLayout() — this test
// locks in WHY: setFormats-after-endLayout makes draw() render nothing (ink≈0).
// It also validates the approach the recolour DOES use: a foreground-only
// `selection` passed to draw() recolours text at draw time without mutating the
// layout's formats, sidestepping the bug. Both findings are asserted here so a
// future Qt build that changes either behaviour surfaces immediately.
struct TextDrawCount { int ink = 0; int reddish = 0; };

TextDrawCount drawAndCount(const QTextLayout& layout) {
  QImage image(500, 40, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  {
    QPainter p(&image);
    layout.draw(&p, QPointF(0, 30));
  }
  TextDrawCount out;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb px = image.pixel(x, y);
      if ((px & 0x00ffffff) == (QColor(Qt::white).rgb() & 0x00ffffff)) { continue; }
      ++out.ink;
      const int r = qRed(px), g = qGreen(px), b = qBlue(px);
      if (r > 150 && g < 110 && b < 110) { ++out.reddish; }
    }
  }
  return out;
}

void testQtTextLayoutSetFormatsAfterEndLayout() {
  QFont font(QStringLiteral("Sans Serif"), 14);
  QTextLayout layout(QStringLiteral("Heading text probe"), font);
  QTextOption opt;
  opt.setWrapMode(QTextOption::WrapAnywhere);
  layout.setTextOption(opt);

  // Base formats set BEFORE endLayout (the normal InlineLayout path).
  QVector<QTextLayout::FormatRange> baseFormats;
  {
    QTextCharFormat f;
    f.setForeground(QColor(40, 40, 40));
    QTextLayout::FormatRange r;
    r.start = 0;
    r.length = layout.text().size();
    r.format = f;
    baseFormats.append(r);
  }
  layout.setFormats(baseFormats);
  layout.beginLayout();
  QTextLine line = layout.createLine();
  require(line.isValid(), QStringLiteral("probe line should be created"));
  line.setLineWidth(500.0);
  line.setPosition(QPointF(0.0, 0.0));
  layout.endLayout();

  const TextDrawCount base = drawAndCount(layout);
  require(base.ink > 30, QStringLiteral("baseline draw should paint text (ink=%1)").arg(base.ink));

  // CONFIRMED in this Qt build: setFormats() AFTER endLayout() breaks draw()
  // (ink collapses to ~0). This documents the constraint that drives the hover
  // recolour to use a draw-time overlay rather than a format swap. If a future
  // Qt build fixes it, this assertion fails and the simpler swap becomes viable.
  QVector<QTextLayout::FormatRange> redFormats;
  {
    QTextCharFormat f;
    f.setForeground(QColor(220, 20, 20));
    QTextLayout::FormatRange r;
    r.start = 0;
    r.length = layout.text().size();
    r.format = f;
    redFormats.append(r);
  }
  layout.setFormats(redFormats);
  const TextDrawCount recoloured = drawAndCount(layout);
  require(recoloured.ink < base.ink * 0.1,
          QStringLiteral("setFormats AFTER endLayout is expected to break draw here (ink=%1; base=%2)").arg(recoloured.ink).arg(base.ink));

  // Candidate approach: QTextLayout::draw(pos, selections) applies a FormatRange
  // as a "selection" at draw time WITHOUT mutating the layout's formats — so it
  // sidesteps the setFormats bug. Question: does a foreground-only selection
  // recolour text (no background highlight)? If yes, per-run recolour is cheap.
  // NB: build a FRESH layout — calling setFormats on the layout above to "reset"
  // it would re-trigger the setFormats-after-endLayout bug and draw nothing.
  QTextLayout selLayout(QStringLiteral("Heading text probe"), font);
  selLayout.setTextOption(opt);
  selLayout.setFormats(baseFormats);
  selLayout.beginLayout();
  QTextLine selLine = selLayout.createLine();
  selLine.setLineWidth(500.0);
  selLine.setPosition(QPointF(0.0, 0.0));
  selLayout.endLayout();

  QImage sel(500, 40, QImage::Format_ARGB32_Premultiplied);
  sel.fill(Qt::white);
  {
    QPainter p(&sel);
    QVector<QTextLayout::FormatRange> selection;
    {
      QTextCharFormat f;
      f.setForeground(QColor(220, 20, 20));  // foreground only, no background
      QTextLayout::FormatRange r;
      r.start = 0;
      r.length = selLayout.text().size();
      r.format = f;
      selection.append(r);
    }
    selLayout.draw(&p, QPointF(0, 30), selection);
  }
  int selInk = 0, selRed = 0;
  for (int y = 0; y < sel.height(); ++y) {
    for (int x = 0; x < sel.width(); ++x) {
      const QRgb px = sel.pixel(x, y);
      if ((px & 0x00ffffff) == (QColor(Qt::white).rgb() & 0x00ffffff)) { continue; }
      ++selInk;
      const int r = qRed(px), g = qGreen(px), b = qBlue(px);
      if (r > 150 && g < 110 && b < 110) { ++selRed; }
    }
  }
  require(selInk > 30, QStringLiteral("selection draw should still paint text (ink=%1)").arg(selInk));
  require(selRed > 20, QStringLiteral("foreground-only selection should recolour text (selRed=%1)").arg(selRed));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testQtTextLayoutSetFormatsAfterEndLayout);
  RUN_TEST(testFitContentHeadingBackgroundIsPill);
  RUN_TEST(testFitContentHeadingHoverUsesSameBorderBox);
  RUN_TEST(testHoverHeadingRecolourKeepsTextAndBar);
  RUN_TEST(testHoverRecolourPreservesLinkAndCodeColours);
  RUN_TEST(testFocusRecolourPreservesLinkAndCodeColours);
  RUN_TEST(testFilterBlurBleedsOutsideBorderBox);
#undef RUN_TEST
  return 0;
}
