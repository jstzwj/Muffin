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

// `width: fit-content` on a heading shrinks its own background box to the text
// (a left-aligned pill), so the painted background ink stops well before the
// right edge of the content column. Without fit-content the same background
// spans the full block width. This is the Phase 2 paint contract that turns
// phycat's h2 from a full-width bar into the Typora-style pill. The assertion
// is relative (ink right edge vs block width), so it is robust to the offscreen
// font-metric differences that make absolute geometry unreliable.
void testFitContentHeadingBackgroundIsPill() {
  const QString pillCss = QStringLiteral(
      "#write { color:#000000; }"
      "#write h2 { width:fit-content; padding:0 12px; background-image:linear-gradient(#d00000,#d00000); }");
  const ThemeDefinition pillDef = CssThemeMapper::fromCss(pillCss, QStringLiteral("pill"), QString());
  require(pillDef.spacing.headingFitContent[1], QStringLiteral("h2 width:fit-content must set the flag"));
  const RenderTheme pillTheme = RenderTheme::fromDefinition(pillDef);
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
  require(!fullDef.spacing.headingFitContent[1], QStringLiteral("h2 with no width must stay full-width"));
  const RenderTheme fullTheme = RenderTheme::fromDefinition(fullDef);
  const HeadingInk full = renderHeadingInk(fullTheme, QStringLiteral("## Short Heading\n"));
  require(full.ink.isValid(), QStringLiteral("full-width heading should paint visible ink"));
  // Full-width background reaches the block's right edge.
  require(full.ink.right() > full.contentWidth * 0.9,
          QStringLiteral("non-fit-content heading background should span the block width (ink right=%1/%2)")
              .arg(full.ink.right()).arg(full.contentWidth));
  require(full.ink.right() > pill.ink.right() + 150,
          QStringLiteral("fit-content pill should be dramatically narrower than full-width background"));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen").toUtf8());
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testFitContentHeadingBackgroundIsPill);
#undef RUN_TEST
  return 0;
}
