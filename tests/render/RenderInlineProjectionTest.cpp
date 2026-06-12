#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "render/DocumentLayout.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

void testInlineMarkerExpansion() {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("before ")));
  inlines.push_back(InlineNode::strong(QStringLiteral("**"), QVector<InlineNode>{InlineNode::text(QStringLiteral("bold"))}));
  inlines.push_back(InlineNode::text(QStringLiteral(" after")));

  RenderTheme theme = RenderTheme::github();
  InlineLayout collapsed;
  collapsed.build(inlines, theme, 400.0, theme.paragraphFont());
  require(!collapsed.displayText().contains(QStringLiteral("**")), QStringLiteral("collapsed inline should hide strong markers"));

  InlineLayout expanded;
  InlineLayout::BuildOptions options;
  options.projectionState.cursorVisibleOffset = 8;
  options.projectionState.cursorSourceOffset = 10;
  expanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), options);
  require(expanded.displayText().contains(QStringLiteral("**")), QStringLiteral("active inline should show strong markers"));
  require(expanded.plainText() == QStringLiteral("before bold after"), QStringLiteral("expanded plain text should stay collapsed"));
  require(expanded.hitTestTextOffset(expanded.cursorRect(8).center()) == 8,
          QStringLiteral("expanded hit test should map display marker offsets back to visible offsets"));
  require(expanded.hitTestSourceOffset(expanded.cursorRectForSourceOffset(9).center()) == 9,
          QStringLiteral("expanded hit test should map opener marker display to source offset"));

  QVector<InlineNode> mathInlines;
  mathInlines.push_back(InlineNode::inlineMath(QStringLiteral("a123")));
  InlineLayout math;
  InlineLayout::BuildOptions mathOptions;
  mathOptions.projectionState.cursorSourceOffset = 2;
  math.build(mathInlines, QStringLiteral("$a123$"), theme, 400.0, theme.paragraphFont(), mathOptions);
  require(math.mathAtomCount() == 0 && math.displayText() == QStringLiteral("$a123$"),
          QStringLiteral("active inline math should expand to editable source text"));
  require(math.hitTestSourceOffset(math.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("math cursor rect should round-trip source offset after first char"));

  InlineLayout inactiveMath;
  inactiveMath.build(mathInlines, QStringLiteral("$a123$"), theme, 400.0, theme.paragraphFont(), InlineLayout::BuildOptions{});
  require(inactiveMath.mathAtomCount() == 1 && !inactiveMath.displayText().contains(QStringLiteral("a123")),
          QStringLiteral("inactive inline math should collapse to a native math atom"));

  InlineLayout selectionExpanded;
  InlineLayout::BuildOptions selectionOptions;
  selectionOptions.projectionState.selectionVisibleStart = 8;
  selectionOptions.projectionState.selectionVisibleEnd = 10;
  selectionExpanded.build(inlines, QStringLiteral("before **bold** after"), theme, 400.0, theme.paragraphFont(), selectionOptions);
  require(selectionExpanded.displayText().contains(QStringLiteral("**")), QStringLiteral("selection touching inline should show strong markers"));
}

void testInlineProjectionContract() {
  RenderTheme theme = RenderTheme::github();
  InlineLayout::BuildOptions options;
  const QString linkMarkdown = QStringLiteral("[label](https://example.com)");
  DocumentSession linkSession;
  linkSession.setMarkdownText(linkMarkdown, false);
  const QVector<InlineNode> linkInlines = linkSession.document().root().children().front()->inlines();

  InlineLayout collapsedLink;
  collapsedLink.build(linkInlines, linkMarkdown, theme, 400.0, theme.paragraphFont(), options);
  require(collapsedLink.displayText() == QStringLiteral("label"), QStringLiteral("inactive link projection text mismatch"));
  require(!collapsedLink.displayText().contains(QStringLiteral("](")), QStringLiteral("inactive link should not render source syntax"));

  InlineLayout::BuildOptions activeLinkOptions = options;
  activeLinkOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout activeLink;
  activeLink.build(linkInlines, linkMarkdown, theme, 400.0, theme.paragraphFont(), activeLinkOptions);
  require(activeLink.displayText() == linkMarkdown, QStringLiteral("active link projection text mismatch"));
  require(activeLink.displayText().contains(QStringLiteral("](")), QStringLiteral("active link should render source syntax"));
  require(activeLink.hitTestSourceOffset(activeLink.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("active link cursor rect should round-trip source offset"));
  require(!activeLink.selectionRects(0, 5).isEmpty(), QStringLiteral("active link selection rects should remain valid"));

  const QString imageMarkdown = QStringLiteral("![alt](https://example.com/image.png)");
  DocumentSession imageSession;
  imageSession.setMarkdownText(imageMarkdown, false);
  const QVector<InlineNode> imageInlines = imageSession.document().root().children().front()->inlines();

  InlineLayout collapsedImage;
  collapsedImage.build(imageInlines, imageMarkdown, theme, 400.0, theme.paragraphFont(), options);
  require(collapsedImage.displayText() == QStringLiteral("alt"), QStringLiteral("inactive image projection text mismatch"));
  require(!collapsedImage.displayText().contains(QStringLiteral("![")), QStringLiteral("inactive image should not render source syntax"));

  InlineLayout::BuildOptions activeImageOptions = options;
  activeImageOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout activeImage;
  activeImage.build(imageInlines, imageMarkdown, theme, 400.0, theme.paragraphFont(), activeImageOptions);
  require(activeImage.displayText() == imageMarkdown, QStringLiteral("active image projection text mismatch"));
  require(activeImage.displayText().contains(QStringLiteral("![")), QStringLiteral("active image should render source syntax"));
  require(activeImage.hitTestSourceOffset(activeImage.cursorRectForSourceOffset(2).center()) == 2,
          QStringLiteral("active image cursor rect should round-trip source offset"));
  require(!activeImage.selectionRects(0, 3).isEmpty(), QStringLiteral("active image selection rects should remain valid"));
}

void testActiveLoadedImageKeepsSourceTextAndAddsPreviewSpace() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("temporary image directory should be valid"));
  const QString imagePath = dir.filePath(QStringLiteral("active-image.png"));
  QImage image(QSize(640, 480), QImage::Format_ARGB32);
  image.fill(QColor(20, 120, 200));
  require(image.save(imagePath), QStringLiteral("temporary image should save"));

  const QString markdown = QStringLiteral("![San Juan Mountains](%1 \"San Juan Mountains\")").arg(imagePath);
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  const QVector<InlineNode> inlines = session.document().root().children().front()->inlines();
  const RenderTheme theme = RenderTheme::github();

  InlineLayout inactive;
  inactive.build(inlines, markdown, theme, 400.0, theme.paragraphFont(), InlineLayout::BuildOptions{});
  require(inactive.displayText() != markdown, QStringLiteral("inactive loaded image should collapse"));
  require(!inactive.displayText().contains(QStringLiteral("![")), QStringLiteral("inactive loaded image should hide source syntax"));

  InlineLayout::BuildOptions activeOptions;
  activeOptions.projectionState.cursorSourceOffset = 2;
  InlineLayout active;
  active.build(inlines, markdown, theme, 400.0, theme.paragraphFont(), activeOptions);
  require(active.displayText() == markdown,
          QStringLiteral("active loaded image should show complete source, got: %1").arg(active.displayText()));
  require(active.height() > inactive.height(), QStringLiteral("active loaded image should reserve preview space below source text"));

  const auto renderBlueBounds = [&](const InlineLayout& layout) {
    QImage canvas(QSize(420, qCeil(layout.height()) + 20), QImage::Format_ARGB32);
    canvas.fill(theme.backgroundColor());
    QPainter painter(&canvas);
    layout.paint(painter, QPointF(0.0, 0.0));
    painter.end();

    QRect bounds;
    const QColor imageColor(20, 120, 200);
    for (int y = 0; y < canvas.height(); ++y) {
      for (int x = 0; x < canvas.width(); ++x) {
        if (QColor(canvas.pixel(x, y)) == imageColor) {
          bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
        }
      }
    }
    return bounds;
  };
  const QRect inactiveImageBounds = renderBlueBounds(inactive);
  const QRect activeImageBounds = renderBlueBounds(active);
  require(!inactiveImageBounds.isNull() && !activeImageBounds.isNull(), QStringLiteral("loaded image should paint in both states"));
  require(qAbs(inactiveImageBounds.width() - activeImageBounds.width()) <= 1 &&
              qAbs(inactiveImageBounds.height() - activeImageBounds.height()) <= 1,
          QStringLiteral("active preview should keep the same image size as inactive rendering"));
}

void testEntityDisplayAfterEdit() {
  DocumentSession session;
  const QString markdown = QStringLiteral("Entities: &amp; &lt; &gt; &copy;.");
  session.setMarkdownText(markdown, false);
  const QVector<InlineNode> inlines = session.document().root().children().front()->inlines();

  // Build layout with raw source text — this is what buildEditable does
  RenderTheme theme = RenderTheme::github();
  InlineLayout layout;
  InlineLayout::BuildOptions options;
  layout.build(inlines, markdown, theme, 400.0, theme.paragraphFont(), options);
  require(layout.displayText() == QString::fromUtf8("Entities: & < > \xc2\xa9."),
          QStringLiteral("initial entity display text mismatch: %1").arg(layout.displayText()));

  // Simulate inserting 'a' at the beginning
  require(session.applyTextDelta(0, 0, QStringLiteral("a"), true),
          "entity edit should apply");

  const QString edited = session.markdownText();
  const QVector<InlineNode> editedInlines = session.document().root().children().front()->inlines();

  InlineLayout editedLayout;
  editedLayout.build(editedInlines, edited, theme, 400.0, theme.paragraphFont(), options);
  require(editedLayout.displayText() == QString::fromUtf8("aEntities: & < > \xc2\xa9."),
          QStringLiteral("post-edit entity display text mismatch: %1").arg(editedLayout.displayText()));
}

void testInlineCodeEndSourceHitUsesForwardBias() {
  RenderTheme theme = RenderTheme::github();
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("vendored ")));
  inlines.push_back(InlineNode::code(QStringLiteral("cmark-gfm")));
  const QString source = QStringLiteral("vendored `cmark-gfm`");

  InlineLayout layout;
  InlineLayout::BuildOptions options;
  options.projectionState.cursorSourceOffset = source.size();
  layout.build(inlines, source, theme, 400.0, theme.paragraphFont(), options);

  const QRectF endCursor = layout.cursorRectForSourceOffset(source.size());
  require(endCursor.left() >= layout.cursorRectForSourceOffset(source.size() - 1).left(),
          QStringLiteral("inline code end cursor should be at or after content end"));
  require(layout.hitTestSourceOffset(QPointF(endCursor.left() + 2.0, endCursor.center().y())) == source.size(),
          QStringLiteral("inline code end hit should map after closing marker"));
}

void testPendingPrefixFallbackDoesNotDuplicateSource() {
  RenderTheme theme = RenderTheme::github();

  InlineLayout fence;
  InlineLayout::BuildOptions fenceOptions;
  fenceOptions.pendingPrefixLength = 3;
  fence.build({}, QStringLiteral("```"), theme, 400.0, theme.paragraphFont(), fenceOptions);
  require(fence.displayText() == QStringLiteral("```"),
          QStringLiteral("pending fence display should not duplicate source: %1").arg(fence.displayText()));
  require(fence.visibleText() == QStringLiteral("```"),
          QStringLiteral("pending fence visible text should not duplicate source: %1").arg(fence.visibleText()));

  InlineLayout math;
  InlineLayout::BuildOptions mathOptions;
  mathOptions.pendingPrefixLength = 2;
  math.build({}, QStringLiteral("$$"), theme, 400.0, theme.paragraphFont(), mathOptions);
  require(math.displayText() == QStringLiteral("$$"),
          QStringLiteral("pending math display should not duplicate source: %1").arg(math.displayText()));
  require(math.visibleText() == QStringLiteral("$$"),
          QStringLiteral("pending math visible text should not duplicate source: %1").arg(math.visibleText()));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInlineMarkerExpansion);
  RUN_TEST(testInlineProjectionContract);
  RUN_TEST(testActiveLoadedImageKeepsSourceTextAndAddsPreviewSpace);
  RUN_TEST(testEntityDisplayAfterEdit);
  RUN_TEST(testInlineCodeEndSourceHitUsesForwardBias);
  RUN_TEST(testPendingPrefixFallbackDoesNotDuplicateSource);
#undef RUN_TEST
  return 0;
}
