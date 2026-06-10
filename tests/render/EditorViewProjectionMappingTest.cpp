#include "EditorViewTestUtils.h"

using namespace muffin;

void testInlineProjectionEntityDisplayAfterEdit() {
  DocumentSession session;
  const QString markdown = QStringLiteral("Entities are decoded by renderers: &amp; &lt; &gt; &copy;.");
  session.setMarkdownText(markdown, false);
  MarkdownNode* block = blockAt(session, 0);

  // Check initial projection
  InlineProjection initialProjection(block->inlines(), markdown, InlineProjectionState{}, 0);
  require(initialProjection.isValid(), "initial entity projection should be valid");
  require(initialProjection.visibleText() == QString::fromUtf8("Entities are decoded by renderers: & < > ©."),
          QStringLiteral("initial entity visible text mismatch: %1").arg(initialProjection.visibleText()));

  // Insert 'a' at the beginning (source offset 0)
  require(session.applyTextDelta(0, 0, QStringLiteral("a"), true),
          "entity edit should apply");

  const QString editedMarkdown = session.markdownText();
  require(editedMarkdown == QStringLiteral("aEntities are decoded by renderers: &amp; &lt; &gt; &copy;."),
          QStringLiteral("entity edited text mismatch: %1").arg(editedMarkdown));

  block = blockAt(session, 0);
  require(block != nullptr, "block should exist after edit");

  // Extract source text the way the layout builder does it
  const SourceRange range = block->sourceRange();
  const qsizetype contentStart = sourceOffsetForLineColumn(editedMarkdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype contentEnd = sourceOffsetForLineEnd(editedMarkdown, range.lineEnd);
  const QString contentText = contentStart >= 0 && contentEnd > contentStart
      ? editedMarkdown.mid(contentStart, contentEnd - contentStart)
      : editedMarkdown;

  InlineProjection editProjection(block->inlines(), contentText, InlineProjectionState{}, contentStart);
  require(editProjection.isValid(), "post-edit entity projection should be valid");
  require(editProjection.visibleText() == QString::fromUtf8("aEntities are decoded by renderers: & < > ©."),
          QStringLiteral("post-edit entity visible text mismatch: %1").arg(editProjection.visibleText()));
}

void testInlineProjectionEntityRevealsRawSyntaxWhenActive() {
  DocumentSession session;
  const QString markdown = QStringLiteral("before &amp; &lt; after");
  session.setMarkdownText(markdown, false);
  MarkdownNode* block = blockAt(session, 0);

  // Inactive: show decoded characters
  InlineProjection inactive(block->inlines(), markdown, InlineProjectionState{}, 0);
  require(inactive.isValid(), "entity projection should be valid");
  require(inactive.visibleText() == QString::fromUtf8("before & < after"),
          QStringLiteral("inactive entity visible text mismatch: %1").arg(inactive.visibleText()));
  require(inactive.displayText() == QString::fromUtf8("before & < after"),
          QStringLiteral("inactive entity display text mismatch: %1").arg(inactive.displayText()));

  // Active on source syntax: reveal only the touched entity's raw syntax in gray.
  InlineProjectionState activeState;
  activeState.cursorSourceOffset = markdown.indexOf(QStringLiteral("&amp;"));
  InlineProjection active(block->inlines(), markdown, activeState, 0);
  require(active.isValid(), "active entity projection should be valid");
  require(active.visibleText() == QString::fromUtf8("before & < after"),
          QStringLiteral("active entity visible text mismatch: %1").arg(active.visibleText()));
  require(active.displayText() == QString::fromUtf8("before &&amp; < after"),
          QStringLiteral("active entity display text mismatch: %1").arg(active.displayText()));

  int hiddenCount = 0;
  int entityContentCount = 0;
  for (const InlineProjectionSpan& span : active.spans()) {
    if (span.kind == InlineSpanKind::HiddenSyntax && span.type == InlineType::Text) {
      ++hiddenCount;
    }
    // Entity content spans: source range is shorter than display length (decoded char)
    if (span.kind == InlineSpanKind::Text && span.type == InlineType::Text) {
      const QString display = active.displayText().mid(
          static_cast<int>(span.displayStart),
          static_cast<int>(span.displayEnd - span.displayStart));
      // Entity decoded chars: source range is wider than 1 char display
      if (span.sourceEnd - span.sourceStart > 1 && display.size() == 1) {
        ++entityContentCount;
      }
    }
  }
  require(hiddenCount == 1,
          QStringLiteral("expected 1 hidden entity span, got %1").arg(hiddenCount));
  require(entityContentCount == 2,
          QStringLiteral("expected 2 entity content spans, got %1").arg(entityContentCount));

  InlineProjectionState visibleActiveState;
  visibleActiveState.cursorVisibleOffset = QStringLiteral("before & ").size();
  InlineProjection visibleActive(block->inlines(), markdown, visibleActiveState, 0);
  require(visibleActive.isValid(), "visible-active entity projection should be valid");
  require(visibleActive.displayText() == QString::fromUtf8("before & <&lt; after"),
          QStringLiteral("visible-active entity display text mismatch: %1").arg(visibleActive.displayText()));
}

void testInlineProjectionKeepsValidEntitiesDecodedAfterBrokenEntityEdit() {
  DocumentSession session;
  const QString original = QStringLiteral("Entities are decoded by renderers: &amp; &lt; &gt; &copy;.");
  session.setMarkdownText(original, false);
  const qsizetype copyAmp = original.indexOf(QStringLiteral("&copy;"));
  require(copyAmp >= 0, "copy entity ampersand should exist");
  require(session.applyTextDelta(copyAmp, 1, QString(), true), "copy entity ampersand delete should apply");

  const QString markdown = QStringLiteral("Entities are decoded by renderers: &amp; &lt; &gt; copy;.");
  require(session.markdownText() == markdown,
          QStringLiteral("broken trailing entity markdown mismatch: %1").arg(session.markdownText()));
  MarkdownNode* block = blockAt(session, 0);

  InlineProjection projection(block->inlines(), markdown, InlineProjectionState{}, 0);
  require(projection.isValid(), "broken trailing entity projection should be valid");
  require(projection.visibleText() == QString::fromUtf8("Entities are decoded by renderers: & < > copy;."),
          QStringLiteral("broken trailing entity visible text mismatch: %1").arg(projection.visibleText()));
  require(projection.displayText() == QString::fromUtf8("Entities are decoded by renderers: & < > copy;."),
          QStringLiteral("broken trailing entity display text mismatch: %1").arg(projection.displayText()));
}

void testInlineProjectionEntityDisplayAfterEditMultiParagraph() {
  DocumentSession session;
  const QString markdown = QStringLiteral("First paragraph.\n\nEntities: &amp; &lt; &gt; &copy;.");
  session.setMarkdownText(markdown, false);

  MarkdownNode* block = blockAt(session, 1);
  require(block != nullptr, "entity block should exist");
  require(block->type() == BlockType::Paragraph, "entity block should be paragraph");

  const SourceRange range = block->sourceRange();
  const qsizetype contentStart = sourceOffsetForLineColumn(markdown, range.lineStart, qMax(1, range.columnStart));
  const qsizetype contentEnd = sourceOffsetForLineEnd(markdown, range.lineEnd);
  require(contentStart >= 0 && contentEnd > contentStart, "entity block content range should be valid");
  const QString contentText = markdown.mid(contentStart, contentEnd - contentStart);

  InlineProjection initialProjection(block->inlines(), contentText, InlineProjectionState{}, contentStart);
  require(initialProjection.isValid(), "initial multi-paragraph entity projection should be valid");
  require(initialProjection.visibleText() == QString::fromUtf8("Entities: & < > \xC2\xA9."),
          QStringLiteral("initial visible text mismatch: %1").arg(initialProjection.visibleText()));

  // Edit the first paragraph to trigger local re-parse of a slice containing the second paragraph
  require(session.applyTextDelta(0, 0, QStringLiteral("x"), true),
          "multi-paragraph entity edit should apply");

  const QString editedMarkdown = session.markdownText();
  require(editedMarkdown == QStringLiteral("xFirst paragraph.\n\nEntities: &amp; &lt; &gt; &copy;."),
          QStringLiteral("multi-paragraph edited text mismatch: %1").arg(editedMarkdown));

  block = blockAt(session, 1);
  require(block != nullptr, "entity block should still exist after edit");
  require(block->type() == BlockType::Paragraph, "entity block should still be paragraph");

  const SourceRange editedRange = block->sourceRange();
  const qsizetype editedStart = sourceOffsetForLineColumn(editedMarkdown, editedRange.lineStart, qMax(1, editedRange.columnStart));
  const qsizetype editedEnd = sourceOffsetForLineEnd(editedMarkdown, editedRange.lineEnd);
  require(editedStart >= 0 && editedEnd > editedStart, "post-edit content range should be valid");
  const QString editedContentText = editedMarkdown.mid(editedStart, editedEnd - editedStart);

  InlineProjection editProjection(block->inlines(), editedContentText, InlineProjectionState{}, editedStart);
  require(editProjection.isValid(), "post-edit multi-paragraph entity projection should be valid");
  require(editProjection.visibleText() == QString::fromUtf8("Entities: & < > \xC2\xA9."),
          QStringLiteral("post-edit visible text mismatch: %1").arg(editProjection.visibleText()));
}

void testInlineMathOpeningMarkerHitEntersContent() {
  DocumentSession session;
  const QString markdown = QStringLiteral("inline math $E = mc^2$.");
  session.setMarkdownText(markdown, false);
  MarkdownNode* block = blockAt(session, 0);
  const qsizetype mathStart = markdown.indexOf(QLatin1Char('$'));

  InlineLayout::BuildOptions options;
  options.sourceBase = 0;
  options.projectionState.cursorSourceOffset = mathStart + 1;
  InlineLayout layout;
  const RenderTheme theme = RenderTheme::typoraLike();
  layout.build(block->inlines(), markdown, theme, 900.0, theme.paragraphFont(), options);
  require(layout.mathAtomCount() == 0, "active inline math should not collapse to atom");

  const QRectF markerStart = layout.cursorRectForSourceOffset(mathStart);
  const QRectF contentStart = layout.cursorRectForSourceOffset(mathStart + 1);
  const QPointF markerPoint((markerStart.left() + contentStart.left()) / 2.0, markerStart.center().y());
  const qsizetype hitSourceOffset = layout.hitTestSourceOffset(markerPoint);
  require(hitSourceOffset == mathStart + 1,
          QStringLiteral("inline math opening marker hit should enter content: %1 vs %2")
              .arg(hitSourceOffset)
              .arg(mathStart + 1));
}

void testInlineProjectionForwardBiasAtInlineEnd() {
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("vendored ")));
  inlines.push_back(InlineNode::code(QStringLiteral("cmark-gfm")));

  const QString markdown = QStringLiteral("vendored `cmark-gfm`");
  InlineProjectionState state;
  state.cursorSourceOffset = markdown.size();
  InlineProjection projection(inlines, markdown, state);

  qsizetype displayOffset = -1;
  require(projection.displayOffsetForSourceOffset(markdown.size(), InlineProjectionBias::Forward, displayOffset),
          "projection forward display mapping should succeed at inline end");
  require(displayOffset == projection.displayText().size(), "projection forward display offset should reach inline end");

  qsizetype sourceOffset = -1;
  require(projection.sourceOffsetForDisplayOffset(displayOffset, InlineProjectionBias::Forward, sourceOffset),
          "projection forward source mapping should succeed at inline end");
  require(sourceOffset == markdown.size(), "projection forward source offset should reach inline end");
}

void testInlineProjectionSpanContracts() {
  InlineProjectionState inactive;
  InlineProjectionState activeStrong;
  activeStrong.cursorSourceOffset = 2;
  requireProjectionSpans(
      {InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("**x**"),
      activeStrong,
      {
          {InlineType::Strong, InlineSpanKind::OpenMarker, 0, 2, 0, 2, 0, 2, 0, 0},
          {InlineType::Text, InlineSpanKind::Text, 2, 3, 2, 3, 2, 3, 0, 1},
          {InlineType::Strong, InlineSpanKind::CloseMarker, 3, 5, 3, 5, 3, 5, 1, 1},
      },
      QStringLiteral("**x**"),
      QStringLiteral("x"),
      "active strong span contract");
  requireProjectionSpans(
      {InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("**x**"),
      inactive,
      {
          {InlineType::Text, InlineSpanKind::Text, 0, 5, 2, 3, 0, 1, 0, 1},
      },
      QStringLiteral("x"),
      QStringLiteral("x"),
      "inactive strong span contract");

  InlineProjectionState activeLink;
  activeLink.cursorSourceOffset = 1;
  requireProjectionSpans(
      {InlineNode::link(QStringLiteral("u"), QString(), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("[x](u)"),
      activeLink,
      {
          {InlineType::Link, InlineSpanKind::OpenMarker, 0, 1, 0, 1, 0, 1, 0, 0},
          {InlineType::Link, InlineSpanKind::Text, 1, 2, 1, 2, 1, 2, 0, 1},
          {InlineType::Link, InlineSpanKind::HiddenSyntax, 2, 6, 2, 6, 2, 6, 1, 1},
      },
      QStringLiteral("[x](u)"),
      QStringLiteral("x"),
      "active link span contract");
  requireProjectionSpans(
      {InlineNode::link(QStringLiteral("u"), QString(), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("[x](u)"),
      inactive,
      {
          {InlineType::Link, InlineSpanKind::Text, 1, 2, 1, 2, 0, 1, 0, 1},
      },
      QStringLiteral("x"),
      QStringLiteral("x"),
      "inactive link span contract");

  InlineProjectionState activeImage;
  activeImage.cursorSourceOffset = 2;
  requireProjectionSpans(
      {InlineNode::image(QStringLiteral("u"), QStringLiteral("x"), QString())},
      QStringLiteral("![x](u)"),
      activeImage,
      {
          {InlineType::Image, InlineSpanKind::OpenMarker, 0, 2, 0, 2, 0, 2, 0, 0},
          {InlineType::Image, InlineSpanKind::Atom, 0, 7, 2, 3, 2, 3, 0, 1},
          {InlineType::Image, InlineSpanKind::HiddenSyntax, 3, 7, 3, 7, 3, 7, 1, 1},
      },
      QStringLiteral("![x](u)"),
      QStringLiteral("x"),
      "active image span contract");
  requireProjectionSpans(
      {InlineNode::image(QStringLiteral("u"), QStringLiteral("x"), QString())},
      QStringLiteral("![x](u)"),
      inactive,
      {
          {InlineType::Image, InlineSpanKind::Atom, 0, 7, 0, 7, 0, 1, 0, 1},
      },
      QStringLiteral("x"),
      QStringLiteral("x"),
      "inactive image span contract");
}

void testInlineHtmlImageProjectionContract() {
  DocumentSession session;
  const QString markdown = QStringLiteral("<a href=\"LICENSE\"><img src=\"license.svg\" alt=\"License\"><img src=\"platform.svg\" alt=\"Platform\"></a>");
  session.setMarkdownText(markdown, false);
  MarkdownNode* block = blockAt(session, 0);

  InlineProjection projection(block->inlines(), markdown, InlineProjectionState{}, 0);
  require(projection.isValid(), "inline html image projection should be valid");

  int imageAtomCount = 0;
  bool foundLicense = false;
  bool foundPlatform = false;
  for (const InlineProjectionSpan& span : projection.spans()) {
    if (span.type != InlineType::Image || span.kind != InlineSpanKind::Atom) {
      continue;
    }
    ++imageAtomCount;
    foundLicense = foundLicense || span.href == QStringLiteral("license.svg");
    foundPlatform = foundPlatform || span.href == QStringLiteral("platform.svg");
  }
  require(imageAtomCount == 2, "inline html image projection should emit both image atoms");
  require(foundLicense, "inline html image projection should keep first image src");
  require(foundPlatform, "inline html image projection should keep second image src");
  require(projection.visibleText() == QStringLiteral("LicensePlatform"),
          QStringLiteral("inline html image visible text mismatch: %1").arg(projection.visibleText()));
}

void testInlineProjectionMappingMatrix() {
  requireProjectionRoundTrip(
      {InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("*x*"),
      1,
      QStringLiteral("x"),
      QStringLiteral("*x*"),
      "emphasis projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("**x**"),
      2,
      QStringLiteral("x"),
      QStringLiteral("**x**"),
      "strong projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::strikethrough(QStringLiteral("~~"), {InlineNode::text(QStringLiteral("x"))})},
      QStringLiteral("~~x~~"),
      2,
      QStringLiteral("x"),
      QStringLiteral("~~x~~"),
      "strikethrough projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::code(QStringLiteral("x"))},
      QStringLiteral("`x`"),
      1,
      QStringLiteral("x"),
      QStringLiteral("`x`"),
      "code projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::inlineMath(QStringLiteral("x"))},
      QStringLiteral("$x$"),
      1,
      QStringLiteral("x"),
      QStringLiteral("$x$"),
      "inline math projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::link(QStringLiteral("https://example.com"), QString(), {InlineNode::text(QStringLiteral("label"))})},
      QStringLiteral("[label](https://example.com)"),
      2,
      QStringLiteral("label"),
      QStringLiteral("[label](https://example.com)"),
      "link projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::image(QStringLiteral("https://example.com/image.png"), QStringLiteral("alt"), QString())},
      QStringLiteral("![alt](https://example.com/image.png)"),
      2,
      QStringLiteral("alt"),
      QStringLiteral("![alt](https://example.com/image.png)"),
      "image projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::strong(
          QStringLiteral("**"),
          {InlineNode::text(QStringLiteral("bold ")),
           InlineNode::emphasis(QStringLiteral("*"), {InlineNode::text(QStringLiteral("em"))})})},
      QStringLiteral("**bold *em***"),
      9,
      QStringLiteral("bold em"),
      QStringLiteral("**bold *em***"),
      "nested inline projection should be valid");
  requireProjectionRoundTrip(
      {InlineNode::text(QStringLiteral("alpha"))},
      QStringLiteral("  alpha"),
      0,
      QStringLiteral("  alpha"),
      QStringLiteral("  alpha"),
      "leading spaces omitted by CommonMark AST should remain editable text");
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInlineProjectionEntityDisplayAfterEdit);
  RUN_TEST(testInlineProjectionEntityRevealsRawSyntaxWhenActive);
  RUN_TEST(testInlineProjectionKeepsValidEntitiesDecodedAfterBrokenEntityEdit);
  RUN_TEST(testInlineProjectionEntityDisplayAfterEditMultiParagraph);
  RUN_TEST(testInlineMathOpeningMarkerHitEntersContent);
  RUN_TEST(testInlineProjectionForwardBiasAtInlineEnd);
  RUN_TEST(testInlineProjectionSpanContracts);
  RUN_TEST(testInlineHtmlImageProjectionContract);
  RUN_TEST(testInlineProjectionMappingMatrix);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
