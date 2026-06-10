#include "EditorViewTestUtils.h"

using namespace muffin;

void testInlineProjectionMarkerSourcePositions() {
  QVector<InlineNode> strikeChildren;
  strikeChildren.push_back(InlineNode::text(QStringLiteral("through")));
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::strikethrough(QStringLiteral("~~"), strikeChildren));

  InlineProjectionState state;
  state.cursorSourceOffset = 1;
  InlineProjection projection(inlines, QStringLiteral("~~through~~"), state);
  require(projection.isValid(), "projection should be valid for strikethrough");
  qsizetype displayOffset = -1;
  require(projection.displayOffsetForSourceOffset(1, displayOffset), "projection should map marker source to display");
  require(displayOffset == 1, "projection marker display offset mismatch");
  qsizetype sourceOffset = -1;
  require(projection.sourceOffsetForDisplayOffset(1, sourceOffset), "projection should map marker display to source");
  require(sourceOffset == 1, "projection marker source offset mismatch");
}

void testInlineProjectionUsesParserSourceRangesForRepeatedUnicodeMarkers() {
  const QString markdown = QStringLiteral("**bold** and **bold**");
  InlineNode first = InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))});
  setInlineRanges(first, 0, QStringLiteral("**bold**").size(), 2, QStringLiteral("**bold").size());
  InlineNode spacer = InlineNode::text(QStringLiteral(" and "));
  spacer.setSourceStart(first.sourceEnd());
  spacer.setSourceEnd(first.sourceEnd() + spacer.text().size());
  InlineNode second = InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))});
  const qsizetype secondStart = QStringLiteral("**bold** and ").size();
  setInlineRanges(second, secondStart, markdown.size(), secondStart + 2, markdown.size() - 2);

  InlineProjectionState state;
  state.cursorSourceOffset = second.sourceStart() + 1;
  InlineProjection projection({first, spacer, second}, markdown, state, 0);
  require(projection.isValid(), "projection should be valid for repeated markers");
  require(projection.displayText() == QStringLiteral("bold and **bold**"),
          "projection should expand the repeated inline selected by parser source range");
  require(projection.visibleText() == QStringLiteral("bold and bold"),
          "projection visible text should collapse repeated markers");
}
void testInlineProjectionUsesParserSourceRangesForRepeatedNonCanonicalMarkdown() {
  DocumentSession session;
  const QString markdown = QStringLiteral("__bold__ and __bold__");
  session.setMarkdownText(markdown, false);
  MarkdownNode* block = blockAt(session, 0);
  require(block->inlines().size() == 3, "repeated strong sample should parse as three inlines");
  require(block->inlines().at(0).type() == InlineType::Strong, "first repeated strong inline missing");
  require(block->inlines().at(2).type() == InlineType::Strong, "second repeated strong inline missing");

  const qsizetype secondStart = QStringLiteral("__bold__ and ").size();
  require(block->inlines().at(2).sourceStart() == secondStart,
          QStringLiteral("second repeated strong parser source start mismatch: %1 vs %2")
              .arg(block->inlines().at(2).sourceStart())
              .arg(secondStart));
  require(block->inlines().at(2).sourceEnd() == markdown.size(),
          QStringLiteral("second repeated strong parser source end mismatch: %1 vs %2")
              .arg(block->inlines().at(2).sourceEnd())
              .arg(markdown.size()));

  InlineProjectionState state;
  state.cursorSourceOffset = secondStart + 2;
  InlineProjection projection(block->inlines(), markdown, state, 0);
  require(projection.isValid(), "projection should be valid for repeated non-canonical markdown");
  require(projection.displayText() == QStringLiteral("bold and __bold__"),
          QStringLiteral("active repeated strong display mismatch: %1").arg(projection.displayText()));
  require(projection.visibleText() == QStringLiteral("bold and bold"),
          QStringLiteral("active repeated strong visible mismatch: %1").arg(projection.visibleText()));

  bool foundSecondTextSpan = false;
  for (const InlineProjectionSpan& span : projection.spans()) {
    if (span.type == InlineType::Text && span.kind == InlineSpanKind::Text && span.bold &&
        span.contentSourceStart == secondStart + 2 && span.contentSourceEnd == markdown.size() - 2) {
      foundSecondTextSpan = true;
      break;
    }
  }
  require(foundSecondTextSpan, "second repeated strong text span should use parser source range");
}

void testInlineProjectionFallbackDoesNotSkipAheadToRepeatedMarkdown() {
  const QString markdown = QStringLiteral("**bold** middle **bold**");
  InlineNode first = InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))});
  InlineNode second = InlineNode::strong(QStringLiteral("**"), {InlineNode::text(QStringLiteral("bold"))});

  InlineProjection projection({first, second}, markdown, InlineProjectionState{}, 0);
  require(projection.isValid(), "projection should remain valid when fallback cannot safely align every inline");
  require(projection.displayText() == QStringLiteral("bold middle **bold**"),
          QStringLiteral("fallback should preserve unmatched source instead of skipping ahead: %1").arg(projection.displayText()));
  require(projection.visibleText() == QStringLiteral("bold middle **bold**"),
          QStringLiteral("fallback visible text should preserve unmatched source: %1").arg(projection.visibleText()));

  for (const InlineProjectionSpan& span : projection.spans()) {
    require(!(span.type == InlineType::Strong && span.sourceStart == 17 && span.sourceEnd == markdown.size()),
            "range-less fallback must not skip ahead to a repeated strong marker");
  }
}

void testInlineProjectionExpandsParserContentRangesForDelimitedInlines() {
  DocumentSession session;
  const QString markdown = QStringLiteral("``a`b`` and ``a`b``");
  session.setMarkdownText(markdown, false);
  MarkdownNode* block = blockAt(session, 0);
  require(block->inlines().size() == 3, "content-range code sample should parse as three inlines");
  const InlineNode& first = block->inlines().at(0);
  const InlineNode& second = block->inlines().at(2);
  require(first.sourceRange().start == 0 && first.sourceRange().end == 7, "first code source range should include delimiters");
  require(first.contentRange().start == 2 && first.contentRange().end == 5, "first code content range mismatch");
  require(second.sourceRange().start == 12 && second.sourceRange().end == markdown.size(), "second code source range should include delimiters");
  require(second.contentRange().start == 14 && second.contentRange().end == 17, "second code content range mismatch");

  InlineProjectionState state;
  state.cursorSourceOffset = 14;
  InlineProjection projection(block->inlines(), markdown, state, 0);
  require(projection.isValid(), "projection should be valid for parser-normalized ranges");
  require(projection.displayText() == QStringLiteral("a`b and ``a`b``"),
          QStringLiteral("active content-range code display mismatch: %1").arg(projection.displayText()));
  require(projection.visibleText() == QStringLiteral("a`b and a`b"),
          QStringLiteral("active content-range code visible mismatch: %1").arg(projection.visibleText()));

  bool foundSecondTextSpan = false;
  for (const InlineProjectionSpan& span : projection.spans()) {
    if (span.type == InlineType::Code && span.kind == InlineSpanKind::Text && span.sourceStart == 12 &&
        span.sourceEnd == markdown.size() && span.contentSourceStart == 14 && span.contentSourceEnd == 17) {
      foundSecondTextSpan = true;
      break;
    }
  }
  require(foundSecondTextSpan, "second code span should consume parser-normalized source/content ranges");
}
void testInlineProjectionAutolinkUsesFullSourceRangeAndLabel() {
  DocumentSession session;
  const QString markdown = QStringLiteral("autolinks like https://example.com, done");
  session.setMarkdownText(markdown, false);
  MarkdownNode* block = blockAt(session, 0);
  require(block->inlines().size() >= 2, "autolink sample should parse into multiple inlines");
  const InlineNode& autolink = block->inlines().at(1);
  require(autolink.type() == InlineType::Link, "autolink node should be a link");
  require(autolink.children().size() == 1, "autolink node should have one text child");
  require(autolink.children().at(0).text() == QStringLiteral("https://example.com"),
          QStringLiteral("autolink label mismatch: %1").arg(autolink.children().at(0).text()));
  require(autolink.href() == QStringLiteral("https://example.com"),
          QStringLiteral("autolink href mismatch: %1").arg(autolink.href()));
  const qsizetype urlStart = markdown.indexOf(QStringLiteral("https://example.com"));
  require(autolink.sourceRange().start == urlStart,
          QStringLiteral("autolink source start mismatch: %1 vs %2").arg(autolink.sourceRange().start).arg(urlStart));
  require(autolink.sourceRange().end == urlStart + QStringLiteral("https://example.com").size(),
          QStringLiteral("autolink source end mismatch: %1").arg(autolink.sourceRange().end));
  require(autolink.contentRange().start == autolink.sourceRange().start &&
              autolink.contentRange().end == autolink.sourceRange().end,
          "autolink content range should match source range");

  InlineProjection projection(block->inlines(), markdown, InlineProjectionState{}, 0);
  require(projection.isValid(), "autolink projection should be valid");
  require(projection.visibleText() == markdown,
          QStringLiteral("autolink visible text mismatch: %1").arg(projection.visibleText()));
  require(projection.displayText() == markdown,
          QStringLiteral("autolink display text mismatch: %1").arg(projection.displayText()));

  bool foundAutolinkSpan = false;
  for (const InlineProjectionSpan& span : projection.spans()) {
    if (span.type == InlineType::Link && span.kind == InlineSpanKind::Text && span.sourceStart == urlStart &&
        span.sourceEnd == urlStart + QStringLiteral("https://example.com").size() &&
        span.contentSourceStart == span.sourceStart && span.contentSourceEnd == span.sourceEnd) {
      foundAutolinkSpan = true;
      break;
    }
  }
  require(foundAutolinkSpan, "autolink text span should cover the full source URL");
}

void testInlineProjectionEntitiesAndMarkdownLinksUseParserRanges() {
  DocumentSession session;
  const QString markdown = QStringLiteral("&amp; &lt; &gt; &copy;. [Muffin](https://example.com)");
  session.setMarkdownText(markdown, false);
  MarkdownNode* block = blockAt(session, 0);

  InlineProjection projection(block->inlines(), markdown, InlineProjectionState{}, 0);
  require(projection.isValid(), "entity/link projection should be valid");
  require(projection.visibleText() == QString::fromUtf8("& < > ©. Muffin"),
          QStringLiteral("entity/link visible text mismatch: %1").arg(projection.visibleText()));
  require(projection.displayText() == QString::fromUtf8("& < > ©. Muffin"),
          QStringLiteral("entity/link display text mismatch: %1").arg(projection.displayText()));

  InlineProjectionState activeLink;
  activeLink.cursorSourceOffset = markdown.indexOf(QStringLiteral("Muffin"));
  InlineProjection activeProjection(block->inlines(), markdown, activeLink, 0);
  // Cursor is inside the link, not inside the entity text node, so entities
  // remain decoded and only link syntax is revealed.
  require(activeProjection.displayText() == QString::fromUtf8("& < > ©. [Muffin](https://example.com)"),
          QStringLiteral("active entity/link display text mismatch: %1").arg(activeProjection.displayText()));
}

void testHorizontalNavigationEntersInlineMarkers() {
  DocumentSession session;
  SelectionController selection;
  UndoStack undoStack;
  BrushQueue brushQueue;
  InputController input;
  wireInput(input, session, selection, undoStack, brushQueue);
  EditorView view;
  wireInput(input, session, selection, undoStack, brushQueue, &view);

  session.setMarkdownText(QStringLiteral("**bold**"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move into strong opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "strong opener marker source offset mismatch");
  require(input.insertText(QStringLiteral("X")), "typing inside strong opener marker should edit source");
  require(session.markdownText() == QStringLiteral("*X*bold**"), "strong opener marker edit mismatch");

  session.setMarkdownText(QStringLiteral("*italic*"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move into italic opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "italic opener marker source offset mismatch");

  session.setMarkdownText(QStringLiteral("~~through~~"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move into strike opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "strike opener marker source offset mismatch");
  require(input.deleteBackward(), "backspace inside strike opener should edit source");
  require(session.markdownText() == QStringLiteral("~through~~"), "strike opener backspace mismatch");

  session.setMarkdownText(QStringLiteral("`code`"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move after code opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "code opener source offset mismatch");

  session.setMarkdownText(QStringLiteral("$x+y$"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move after math opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "math opener source offset mismatch");
}

void testTextHitActivationAddsSourceOffsetForInlineEditing() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  HitTestResult hit;
  hit.zone = HitTestResult::Zone::Text;
  hit.blockId = blockAt(session, 0)->id();
  hit.textNodeId = hit.blockId;
  hit.textOffset = 9;
  controller.activateHit(hit);

  require(controller.selection().cursorPosition().text.sourceOffset == 11, "text hit should resolve strong source offset");
  require(controller.inputController().insertText(QStringLiteral("X")), "typing after text hit should edit strong inline");
  require(session.markdownText() == QStringLiteral("before **boXld** after"), "text hit inline insert mismatch");
}

void testEditorViewInlineLayoutSmoke() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());

  MarkdownNode* block = blockAt(session, 0);
  const QRectF blockRect = view.nodeRect(block->id());
  require(!blockRect.isEmpty(), "probe view should layout inline paragraph");
  const BlockLayout* blockLayout = view.blockAtViewportPos(blockRect.center());
  require(blockLayout != nullptr, "probe view should find block layout");
  const InlineLayout* inlineLayout = blockLayout->inlineLayout();
  require(inlineLayout != nullptr, "probe view should build inline layout");

  const QPointF textPoint = blockRect.topLeft() + inlineLayout->cursorRectForSourceOffset(11).center();
  const HitTestResult textHit = view.hitTest(textPoint);
  require(textHit.isValid() && textHit.zone == HitTestResult::Zone::Text, "probe view hit-test should hit text");
  require(textHit.sourceOffset == 11, "probe view hit-test source offset mismatch");

  view.setCursorHit(textHit);
  controller.activateHit(textHit);
  require(controller.selection().cursorPosition().text.sourceOffset == 11, "probe view activation should keep source offset");
  const QRectF activatedRect = view.nodeRect(block->id());
  const BlockLayout* activatedBlock = view.blockAtViewportPos(activatedRect.center());
  require(activatedBlock != nullptr && activatedBlock->inlineLayout() != nullptr, "probe activated inline layout should exist");
  const QPointF activatedPoint = activatedRect.topLeft() + activatedBlock->inlineLayout()->cursorRectForSourceOffset(11).center();
  require(view.hitTest(activatedPoint).sourceOffset == 11, "probe view cursor rect should round-trip through hit-test");

  CursorPosition inside;
  inside.blockId = block->id();
  inside.text.nodeId = block->id();
  inside.text.textOffset = 1;
  inside.text.sourceOffset = 9;
  view.setCursorPosition(inside);
  const QRectF expandedRect = view.nodeRect(block->id());
  const BlockLayout* expandedBlock = view.blockAtViewportPos(expandedRect.center());
  require(expandedBlock != nullptr && expandedBlock->inlineLayout() != nullptr, "probe expanded inline layout should exist");
  const InlineLayout* expandedLayout = expandedBlock->inlineLayout();

  const QPointF markerPoint = expandedRect.topLeft() + expandedLayout->cursorRectForSourceOffset(8).center();
  const HitTestResult markerHit = view.hitTest(markerPoint);
  require(markerHit.isValid(), "probe marker hit should be valid");
  require(markerHit.sourceOffset == 8, "probe active marker source offset should round-trip");

  SelectionRange selection;
  selection.anchor = inside;
  selection.focus = inside;
  selection.focus.text.textOffset = 4;
  selection.focus.text.sourceOffset = 13;
  view.setSelectionRange(selection);
  const QRectF selectedRect = view.nodeRect(block->id());
  const BlockLayout* selectedBlock = view.blockAtViewportPos(selectedRect.center());
  require(selectedBlock != nullptr, "probe selected block layout should exist");
  const QVector<QRectF> selectionRects = selectedBlock->selectionRects(selection, RenderTheme::typoraLike());
  require(!selectionRects.isEmpty(), "probe view selection rects should be drawable");
  for (const QRectF& rect : selectionRects) {
    require(rect.width() > 0 && rect.height() > 0, "probe view selection rect should have area");
  }
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInlineProjectionMarkerSourcePositions);
  RUN_TEST(testInlineProjectionUsesParserSourceRangesForRepeatedUnicodeMarkers);
  RUN_TEST(testInlineProjectionUsesParserSourceRangesForRepeatedNonCanonicalMarkdown);
  RUN_TEST(testInlineProjectionFallbackDoesNotSkipAheadToRepeatedMarkdown);
  RUN_TEST(testInlineProjectionExpandsParserContentRangesForDelimitedInlines);
  RUN_TEST(testInlineProjectionAutolinkUsesFullSourceRangeAndLabel);
  RUN_TEST(testInlineProjectionEntitiesAndMarkdownLinksUseParserRanges);
  RUN_TEST(testHorizontalNavigationEntersInlineMarkers);
  RUN_TEST(testTextHitActivationAddsSourceOffsetForInlineEditing);
  RUN_TEST(testEditorViewInlineLayoutSmoke);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
