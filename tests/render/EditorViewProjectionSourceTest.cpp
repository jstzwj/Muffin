#include "EditorViewTestUtils.h"
#include "theme/CssThemeMapper.h"
#include <QElapsedTimer>
#include <QFile>

void testSelectionRefreshRetainsStylesAndUnchangedProjection() {
  const QString markdown =
      "Plain **bold** and ~~strikethrough~~ with <kbd>Ctrl</kbd> + <kbd>S</kbd>.\n\nother paragraph";
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "p{font-size:16px}p:first-child{color:red}strong{background:yellow}strong:hover{color:blue}", "nav-test", {}));
  theme.updateForViewport(700, 600);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 700, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  layout.buildAll(theme);
  const auto& node = *session.document().root().children().front();
  const auto nodeId = node.id();
  const auto other = session.document().root().children().back()->id();
  const auto* otherLayout = layout.block(other);
  const auto* style = theme.elementStyleForNode(node, "p");
  SelectionRange selection;
  const auto move = [&](qsizetype source) {
    selection.anchor = selection.focus = inlineCursor(node.id(), source, source);
    require(
        layout
            .rebuildBlock(node.id(), session.document(), theme, selection, DocumentLayout::BlockUpdateReason::Selection)
            .rebuilt,
        "selection refresh");
  };
  move(markdown.indexOf("bold") + 1);
  const auto* expanded = layout.block(node.id())->inlineLayout();
  require(expanded->displayText().contains("**bold**"), "entering bold reveals syntax");
  move(markdown.indexOf("bold") + 2);
  require(layout.block(node.id())->inlineLayout() == expanded,
          "moving within revealed syntax retains shaping and HTML");
  selection.anchor.text.sourceOffset = 0;
  require(
      layout.rebuildBlock(node.id(), session.document(), theme, selection, DocumentLayout::BlockUpdateReason::Selection)
          .rebuilt,
      "selection extent refresh");
  require(layout.block(node.id())->inlineLayout() == expanded,
          "selection extent does not reflow unchanged focus syntax");
  move(markdown.indexOf("strikethrough") + 2);
  const auto text = layout.block(node.id())->inlineLayout()->displayText();
  require(!text.contains("**bold**") && text.contains("~~strikethrough~~"),
          "crossing inline boundary updates both markers");
  require(layout.block(other) == otherLayout && theme.elementStyleForNode(node, "p") == style,
          "selection changes preserve other blocks and the document style cache");
  require(session.applyTextEdit(0, 0, "edited ", {{nodeId, 0, BlockType::Paragraph}}) !=
              muffin::TextEditResult::Rejected,
          "edit before queued refresh");
  require(
      layout.rebuildBlock(nodeId, session.document(), theme, selection, DocumentLayout::BlockUpdateReason::Selection)
          .rebuilt,
      "revision change cannot reuse selection-only layout");
  require(layout.block(nodeId)->inlineLayout()->visibleText().startsWith("edited Plain"),
          "pending edit is not lost to navigation reuse");
}

void testProjectionReuseDecisionsMatchFreshProjection() {
  for (const QString& source : {QString("plain **bold and *nested*** / ~~strike~~ / `code` / $E=mc^2$"),
                                QString("&amp; &#x41; \\* :smile: [link](https://example.com)"),
                                QString("text <kbd>Ctrl</kbd> + <span style=\"color:red\">HTML</span> after")}) {
    DocumentSession session;
    session.setMarkdownText(source, false);
    const auto& inlines = session.document().root().children().front()->inlines();
    for (int mode = 0; mode < 3; ++mode) {
      for (qsizetype offset : {qsizetype(-1), qsizetype(0), source.size() / 3, source.size() / 2, source.size()}) {
        InlineProjectionState initial;
        if (mode != 1)
          initial.cursorSourceOffset = offset;
        if (mode != 0)
          initial.cursorVisibleOffset = offset;
        InlineProjection projection(inlines, source, initial, 0);
        for (qsizetype target = -1; target <= source.size(); ++target) {
          InlineProjectionState next;
          if (mode != 1)
            next.cursorSourceOffset = target;
          if (mode != 0)
            next.cursorVisibleOffset = target;
          if (!projection.matchesState(next))
            continue;
          InlineProjection fresh(inlines, source, next, 0);
          require(projection.displayText() == fresh.displayText() && projection.visibleText() == fresh.visibleText(),
                  "reused reveal decisions agree with fresh display text");
          for (qsizetype sourceOffset = 0; sourceOffset <= source.size(); ++sourceOffset) {
            qsizetype before = -1, after = -1;
            require(projection.displayOffsetForSourceOffset(sourceOffset, before) ==
                            fresh.displayOffsetForSourceOffset(sourceOffset, after) &&
                        before == after,
                    "reused projection retains source mapping");
          }
        }
      }
    }
  }
}

void benchmarkInlineNavigation() {
  if (!qEnvironmentVariableIsSet("MUFFIN_INLINE_NAV_BENCH"))
    return;
  QFile file(qEnvironmentVariable("MUFFIN_NAV_DOCUMENT"));
  require(file.open(QIODevice::ReadOnly), "open navigation benchmark document");
  const auto markdown = QString::fromUtf8(file.readAll());
  DocumentSession session;
  session.setMarkdownText(markdown, false);
  EditorView view;
  view.resize(1000, 700);
  const auto cssPath = qEnvironmentVariable("MUFFIN_NAV_THEME");
  if (!cssPath.isEmpty())
    view.setTheme(RenderTheme::fromDefinition(ThemeDefinition::fromCss(cssPath, "nav-benchmark")));
  view.setDocument(session.document());
  EditorController controller;
  controller.attach(&session, &view);
  view.show();
  QApplication::processEvents();
  const auto bold = markdown.indexOf("**bold**") + 3;
  const auto strike = markdown.indexOf("~~strikethrough~~") + 3;
  require(bold > 3 && strike > 3, "navigation benchmark inline samples");
  const auto measure = [&](const char* label, const auto& action) {
    QVector<qreal> eventMs, frameMs;
    for (int i = 0; i < 20; ++i) {
      QElapsedTimer timer;
      timer.start();
      action(i);
      eventMs.append(timer.nsecsElapsed() / 1000000.);
      QApplication::processEvents();
      view.viewport()->repaint();
      frameMs.append(timer.nsecsElapsed() / 1000000.);
    }
    const auto report = [&](QVector<qreal> values, const char* phase) {
      std::sort(values.begin(), values.end());
      std::cout << "[inline-nav-bench] " << label << ' ' << phase << " p50Ms=" << values[values.size() / 2]
                << " p95Ms=" << values[values.size() * 95 / 100] << " maxMs=" << values.back() << std::endl;
    };
    report(eventMs, "event");
    report(frameMs, "frame");
  };
  measure("activate", [&](int i) {
    const auto cursor = controller.inputController().cursorForSourceOffset(i % 2 ? strike : bold);
    view.setCursorHit(HitTestResult::from(cursor));
    controller.activateHit(HitTestResult::from(cursor));
  });
  controller.selection().setCursorPosition(controller.inputController().cursorForSourceOffset(strike + 4));
  measure("horizontal", [&](int i) {
    require(pressKey(controller.inputController(), &view, i % 2 ? Qt::Key_Left : Qt::Key_Right),
            "horizontal benchmark navigation");
  });
  measure("vertical", [&](int i) {
    require(pressKey(controller.inputController(), &view, i % 2 ? Qt::Key_Up : Qt::Key_Down),
            "vertical benchmark navigation");
  });
}

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
          QStringLiteral("fallback should preserve unmatched source instead of skipping ahead: %1")
              .arg(projection.displayText()));
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
  require(first.sourceRange().start == 0 && first.sourceRange().end == 7,
          "first code source range should include delimiters");
  require(first.contentRange().start == 2 && first.contentRange().end == 5, "first code content range mismatch");
  require(second.sourceRange().start == 12 && second.sourceRange().end == markdown.size(),
          "second code source range should include delimiters");
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
  require(session.markdownText().toString() == QStringLiteral("*X*bold**"), "strong opener marker edit mismatch");

  session.setMarkdownText(QStringLiteral("*italic*"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move into italic opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "italic opener marker source offset mismatch");

  session.setMarkdownText(QStringLiteral("~~through~~"), false);
  setSourceCursor(selection, blockAt(session, 0), 0, 0);
  require(pressKey(input, &view, Qt::Key_Right), "right arrow should move into strike opener");
  require(selection.cursorPosition().text.sourceOffset == 1, "strike opener marker source offset mismatch");
  require(input.deleteBackward(), "backspace inside strike opener should edit source");
  require(session.markdownText().toString() == QStringLiteral("~through~~"), "strike opener backspace mismatch");

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

  require(controller.selection().cursorPosition().text.sourceOffset == 11,
          "text hit should resolve strong source offset");
  require(controller.inputController().insertText(QStringLiteral("X")),
          "typing after text hit should edit strong inline");
  require(session.markdownText().toString() == QStringLiteral("before **boXld** after"),
          "text hit inline insert mismatch");
}

void testDoubleDollarNavigationEditAndUndo() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  const QString markdown = QStringLiteral("before $$x+y$$ after");
  session.setMarkdownText(markdown, false);
  const qsizetype start = markdown.indexOf(QStringLiteral("$$"));
  setSourceCursor(controller.selection(), blockAt(session, 0), start, start);
  auto& input = controller.inputController();
  for (qsizetype offset = start + 1; offset <= start + 5; ++offset) {
    require(pressKey(input, &view, Qt::Key_Right), "right arrow should traverse both dollars and TeX");
    require(controller.selection().cursorPosition().text.sourceOffset == offset,
            "double-dollar right navigation should retain source coordinates");
  }
  require(pressKey(input, &view, Qt::Key_Left), "left arrow should move inside TeX");
  require(controller.selection().cursorPosition().text.sourceOffset == start + 4,
          "double-dollar left navigation should retain source coordinates");
  require(input.insertText(QStringLiteral("z")), "typing inside double-dollar math should edit TeX");
  const QString edited = QStringLiteral("before $$x+zy$$ after");
  require(session.markdownText().toString() == edited, "editing should preserve both dollar delimiters");
  controller.undo();
  require(session.markdownText().toString() == markdown, "undo should restore double-dollar math");
  controller.redo();
  require(session.markdownText().toString() == edited, "redo should restore double-dollar edits");
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
  require(controller.selection().cursorPosition().text.sourceOffset == 11,
          "probe view activation should keep source offset");
  const QRectF activatedRect = view.nodeRect(block->id());
  const BlockLayout* activatedBlock = view.blockAtViewportPos(activatedRect.center());
  require(activatedBlock != nullptr && activatedBlock->inlineLayout() != nullptr,
          "probe activated inline layout should exist");
  const QPointF activatedPoint =
      activatedRect.topLeft() + activatedBlock->inlineLayout()->cursorRectForSourceOffset(11).center();
  require(view.hitTest(activatedPoint).sourceOffset == 11, "probe view cursor rect should round-trip through hit-test");

  CursorPosition inside;
  inside.blockId = block->id();
  inside.text.nodeId = block->id();
  inside.text.textOffset = 1;
  inside.text.sourceOffset = 9;
  view.setCursorPosition(inside);
  const QRectF expandedRect = view.nodeRect(block->id());
  const BlockLayout* expandedBlock = view.blockAtViewportPos(expandedRect.center());
  require(expandedBlock != nullptr && expandedBlock->inlineLayout() != nullptr,
          "probe expanded inline layout should exist");
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
  const QVector<QRectF> selectionRects = selectedBlock->selectionRects(selection, RenderTheme::defaultTheme());
  require(!selectionRects.isEmpty(), "probe view selection rects should be drawable");
  for (const QRectF& rect : selectionRects) {
    require(rect.width() > 0 && rect.height() > 0, "probe view selection rect should have area");
  }
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", qEnvironmentVariableIsSet("MUFFIN_INLINE_NAV_BENCH") ? "minimal" : "offscreen");
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
  RUN_TEST(testDoubleDollarNavigationEditAndUndo);
  RUN_TEST(testEditorViewInlineLayoutSmoke);
  RUN_TEST(testSelectionRefreshRetainsStylesAndUnchangedProjection);
  RUN_TEST(testProjectionReuseDecisionsMatchFreshProjection);
#undef RUN_TEST
  benchmarkInlineNavigation();
  QApplication::clipboard()->clear();
  return 0;
}
