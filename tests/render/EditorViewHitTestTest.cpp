#include "EditorViewTestUtils.h"

#include <QTextLayout>
#include <QTextOption>

using namespace muffin;

void testDefinitionPlaceholderHitKeepsCursorInSlot() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);

  session.setMarkdownText(QStringLiteral("[]: "), false);
  view.resize(800, 240);
  view.setDocument(session.document());

  MarkdownNode* link = blockAt(session, 0);
  const DefinitionBlock definition = link->definition();
  CursorPosition focusedCursor;
  focusedCursor.blockId = link->id();
  focusedCursor.text.nodeId = link->id();
  focusedCursor.text.textOffset = definition.destinationRange.start - definition.markerRange.start;
  focusedCursor.text.sourceOffset = definition.destinationRange.start;
  SelectionRange focusedSelection;
  focusedSelection.anchor = focusedCursor;
  focusedSelection.focus = focusedCursor;
  view.setSelectionRange(focusedSelection);

  const BlockLayout* block = requireViewBlock(view, link->id(), QStringLiteral("definition"));
  const auto& definitionSlotLayouts = block->definitionSlots();
  const BlockLayout::DefinitionSlotLayout* destination = nullptr;
  for (const BlockLayout::DefinitionSlotLayout& slot : definitionSlotLayouts) {
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Destination) {
      destination = &slot;
      break;
    }
  }
  require(destination != nullptr, "definition destination slot should exist");

  const QPointF clickPoint(destination->rect.right() - 2.0, destination->rect.center().y());
  HitTestResult hit = view.hitTest(clickPoint);
  require(hit.blockId == link->id(), "definition placeholder hit block mismatch");
  require(hit.sourceOffset == definition.destinationRange.start, "definition placeholder hit should target destination slot");
  require(hit.definitionField == HitTestResult::DefinitionField::Destination, "definition placeholder hit should target destination field");
  require(hit.cursorRect.left() >= destination->rect.left(), "definition placeholder hit cursor should stay in destination slot");
  require(hit.cursorRect.left() < destination->rect.right(), "definition placeholder hit cursor should not jump to optional title end");
  require(!block->definitionCursorRectForSourceOffset(hit.sourceOffset, RenderTheme::defaultTheme()).isEmpty(),
          "definition source cursor rect should rebuild from source offset");

  controller.activateHit(hit);
  require(controller.inputController().insertText(QStringLiteral("123")), "typing after definition placeholder hit should insert destination");
  require(session.markdownText() == QStringLiteral("[]: 123"), "definition placeholder input markdown mismatch");
}

void testEmptyLinkDefinitionTitlePlaceholderOnlyWhenFocused() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  session.setMarkdownText(QStringLiteral("[1]: url"), false);
  view.resize(800, 240);
  view.setDocument(session.document());

  MarkdownNode* link = blockAt(session, 0);
  const BlockLayout* block = requireViewBlock(view, link->id(), QStringLiteral("unfocused link definition"));
  bool hasTitleSlot = false;
  for (const BlockLayout::DefinitionSlotLayout& slot : block->definitionSlots()) {
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Title) {
      hasTitleSlot = true;
    }
  }
  require(!hasTitleSlot, "unfocused empty link definition should hide optional title slot");

  session.setMarkdownText(QStringLiteral("[1]: url"), false);
  view.setDocument(session.document());
  link = blockAt(session, 0);
  block = requireViewBlock(view, link->id(), QStringLiteral("unfocused no-title link definition"));
  hasTitleSlot = false;
  for (const BlockLayout::DefinitionSlotLayout& slot : block->definitionSlots()) {
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Title) {
      hasTitleSlot = true;
    }
  }
  require(!hasTitleSlot, "unfocused no-title link definition should hide optional title slot");

  session.setMarkdownText(QStringLiteral("[1]: url \"\""), false);
  view.setDocument(session.document());
  link = blockAt(session, 0);
  block = requireViewBlock(view, link->id(), QStringLiteral("unfocused empty-title link definition"));
  const BlockLayout::DefinitionSlotLayout* emptyTitle = nullptr;
  for (const BlockLayout::DefinitionSlotLayout& slot : block->definitionSlots()) {
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Title) {
      emptyTitle = &slot;
      break;
    }
  }
  require(emptyTitle != nullptr, "unfocused explicit empty-title definition should expose title slot");
  const auto& tokens = block->definitionTokens();
  const BlockLayout::DefinitionTokenLayout* titleToken = nullptr;
  for (const BlockLayout::DefinitionTokenLayout& token : tokens) {
    if (token.editable && token.field == BlockLayout::DefinitionSlotLayout::Field::Title) {
      titleToken = &token;
      break;
    }
  }
  require(titleToken != nullptr, "empty-title definition should expose editable title token");
  require(titleToken->text.isEmpty(), "empty-title token text should be empty");
  require(titleToken->placeholder.isEmpty(), "explicit empty title should not render placeholder between quotes");
  require(titleToken->rect.width() <= 1.1, "explicit empty title should be a caret-width slot between quotes");
  const HitTestResult titleHit = view.hitTest(emptyTitle->rect.center());
  require(titleHit.blockId == link->id(), "empty-title hit block mismatch");
  require(titleHit.definitionField == HitTestResult::DefinitionField::Title, "empty-title hit should target title field");
  require(titleHit.sourceOffset == link->definition().titleRange.start, "empty-title hit should place cursor in title range");
  require(std::abs(titleHit.cursorRect.left() - titleToken->rect.left()) < 0.01,
          "empty-title hit cursor should render inside the quotes");
  controller.activateHit(titleHit);
  view.setCursorPosition(controller.selection().cursorPosition());
  block = requireViewBlock(view, link->id(), QStringLiteral("empty-title link definition after cursor restore"));
  titleToken = nullptr;
  for (const BlockLayout::DefinitionTokenLayout& token : block->definitionTokens()) {
    if (token.editable && token.field == BlockLayout::DefinitionSlotLayout::Field::Title) {
      titleToken = &token;
      break;
    }
  }
  require(titleToken != nullptr, "empty-title cursor restore should keep editable title token");
  const QRectF restoredCursor = block->definitionCursorRectForSourceOffset(controller.selection().cursorPosition().text.sourceOffset,
                                                                          RenderTheme::defaultTheme());
  require(restoredCursor.left() >= titleToken->rect.left() - 0.01 &&
              restoredCursor.left() <= titleToken->rect.right() + 0.01,
          "empty-title cursor restored from source offset should stay inside the quotes");

  const DefinitionBlock definition = link->definition();
  CursorPosition cursor;
  cursor.blockId = link->id();
  cursor.text.nodeId = link->id();
  cursor.text.textOffset = definition.titleRange.start - definition.markerRange.start;
  cursor.text.sourceOffset = definition.titleRange.start;
  SelectionRange selection;
  selection.anchor = cursor;
  selection.focus = cursor;
  view.setSelectionRange(selection);

  block = requireViewBlock(view, link->id(), QStringLiteral("focused link definition"));
  hasTitleSlot = false;
  for (const BlockLayout::DefinitionSlotLayout& slot : block->definitionSlots()) {
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Title) {
      hasTitleSlot = true;
      require(slot.text.isEmpty(), "focused optional title slot should be empty");
      require(slot.placeholder.isEmpty(), "focused explicit empty title should keep caret slot between quotes");
    }
  }
  require(hasTitleSlot, "focused empty link definition should show optional title slot");
}

void testDefinitionBoundaryHitPlacesCursorAtSourceEdges() {
  DocumentSession session;
  EditorView view;

  const QString markdown = QStringLiteral("[1]: 测试 \"标题\"");
  session.setMarkdownText(markdown, false);
  view.resize(800, 240);
  view.setDocument(session.document());

  MarkdownNode* link = blockAt(session, 0);
  const DefinitionBlock definition = link->definition();
  const BlockLayout* block = requireViewBlock(view, link->id(), QStringLiteral("definition edge"));
  const QRectF startCursor = block->definitionCursorRectForSourceOffset(definition.sourceRange.start, RenderTheme::defaultTheme());
  const QRectF endCursor = block->definitionCursorRectForSourceOffset(definition.sourceRange.end, RenderTheme::defaultTheme());
  require(!startCursor.isEmpty(), "definition start cursor rect should exist");
  require(!endCursor.isEmpty(), "definition end cursor rect should exist");

  const HitTestResult startHit = view.hitTest(QPointF(startCursor.left() - 3.0, startCursor.center().y()));
  require(startHit.sourceOffset == definition.sourceRange.start, "definition left edge hit should target source start");
  require(startHit.cursorRect.left() == startCursor.left(), "definition left edge cursor rect mismatch");

  const HitTestResult endHit = view.hitTest(QPointF(endCursor.left() + 3.0, endCursor.center().y()));
  require(endHit.sourceOffset == definition.sourceRange.end, "definition right edge hit should target source end");
  require(endHit.cursorRect.left() == endCursor.left(), "definition right edge cursor rect mismatch");
}

void testNoTitleDefinitionRightEdgeHasNoInvisibleQuote() {
  DocumentSession session;
  EditorView view;

  session.setMarkdownText(QStringLiteral("[1]: url"), false);
  view.resize(800, 240);
  view.setDocument(session.document());

  MarkdownNode* link = blockAt(session, 0);
  const DefinitionBlock definition = link->definition();
  const BlockLayout* block = requireViewBlock(view, link->id(), QStringLiteral("definition without title"));

  const BlockLayout::DefinitionSlotLayout* destination = nullptr;
  for (const BlockLayout::DefinitionSlotLayout& slot : block->definitionSlots()) {
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Destination) {
      destination = &slot;
      break;
    }
  }
  require(destination != nullptr, "definition destination slot should exist");

  const QRectF endCursor = block->definitionCursorRectForSourceOffset(definition.sourceRange.end, RenderTheme::defaultTheme());
  require(!endCursor.isEmpty(), "definition end cursor rect should exist");
  require(std::abs(endCursor.left() - destination->rect.right()) < 0.01,
          "no-title definition end cursor should not include invisible quote width");
}

void testDefinitionTokenModelDrivesSyntaxAndSlots() {
  DocumentSession session;
  EditorView view;

  session.setMarkdownText(QStringLiteral("[1]: url \"Title\""), false);
  view.resize(800, 240);
  view.setDocument(session.document());

  MarkdownNode* link = blockAt(session, 0);
  const BlockLayout* block = requireViewBlock(view, link->id(), QStringLiteral("tokenized link definition"));
  const auto& tokens = block->definitionTokens();
  require(!tokens.isEmpty(), "definition tokens should exist");
  require(tokens.first().kind == BlockLayout::DefinitionTokenLayout::Kind::Syntax, "first definition token should be syntax");
  require(tokens.first().text == QStringLiteral("["), "first syntax token should be opening bracket");
  require(tokens.last().kind == BlockLayout::DefinitionTokenLayout::Kind::Syntax, "last titled link token should be syntax");
  require(tokens.last().text == QStringLiteral("\""), "last titled link syntax token should be quote");

  int editableCount = 0;
  int syntaxCount = 0;
  for (const BlockLayout::DefinitionTokenLayout& token : tokens) {
    editableCount += token.editable ? 1 : 0;
    syntaxCount += token.kind == BlockLayout::DefinitionTokenLayout::Kind::Syntax ? 1 : 0;
    if (token.editable) {
      require(token.kind == BlockLayout::DefinitionTokenLayout::Kind::Slot, "editable definition token should be a slot");
    }
  }
  require(editableCount == block->definitionSlots().size(), "definition editable tokens should mirror slot count");
  require(syntaxCount >= 4, "definition should include syntax tokens");
}

void testDefinitionSyntaxHitChoosesNearestEditableSlot() {
  DocumentSession session;
  EditorView view;

  session.setMarkdownText(QStringLiteral("[1]: url"), false);
  view.resize(800, 240);
  view.setDocument(session.document());

  MarkdownNode* link = blockAt(session, 0);
  const BlockLayout* block = requireViewBlock(view, link->id(), QStringLiteral("syntax hit definition"));
  const auto& tokens = block->definitionTokens();
  const BlockLayout::DefinitionTokenLayout* marker = nullptr;
  for (const BlockLayout::DefinitionTokenLayout& token : tokens) {
    if (!token.editable && token.text == QStringLiteral("]:")) {
      marker = &token;
      break;
    }
  }
  require(marker != nullptr, "definition marker syntax token should exist");

  const HitTestResult hit = view.hitTest(marker->rect.center());
  require(hit.blockId == link->id(), "definition syntax hit block mismatch");
  require(hit.zone == HitTestResult::Zone::Text, "definition syntax hit should remain text zone");
  require(hit.definitionField == HitTestResult::DefinitionField::Label ||
              hit.definitionField == HitTestResult::DefinitionField::Destination,
          "definition syntax hit should choose nearest editable field");
  require(hit.sourceOffset >= link->definition().labelRange.start &&
              hit.sourceOffset <= link->definition().destinationRange.end,
          "definition syntax hit should resolve to editable source range");
}

void testFootnoteDefinitionUsesTokenPath() {
  DocumentSession session;
  EditorView view;

  session.setMarkdownText(QStringLiteral("[^1]: note"), false);
  view.resize(800, 240);
  view.setDocument(session.document());

  MarkdownNode* footnote = blockAt(session, 0);
  const BlockLayout* block = requireViewBlock(view, footnote->id(), QStringLiteral("tokenized footnote"));
  bool sawCaret = false;
  bool sawNote = false;
  for (const BlockLayout::DefinitionTokenLayout& token : block->definitionTokens()) {
    sawCaret = sawCaret || (!token.editable && token.text == QStringLiteral("^"));
    sawNote = sawNote || (token.editable && token.field == BlockLayout::DefinitionSlotLayout::Field::Note);
  }
  require(sawCaret, "footnote definition should include caret syntax token");
  require(sawNote, "footnote definition should include editable note token");

  const DefinitionBlock definition = footnote->definition();
  const QRectF noteCursor = block->definitionCursorRectForSourceOffset(definition.noteRange.start, RenderTheme::defaultTheme());
  require(!noteCursor.isEmpty(), "footnote note cursor should come from token path");
  const HitTestResult hit = view.hitTest(noteCursor.center());
  require(hit.definitionField == HitTestResult::DefinitionField::Note, "footnote token hit should target note field");
}

void testFocusedEmptyFootnoteDefinitionSlotSuppressesPlaceholder() {
  DocumentSession session;
  EditorView view;
  const RenderTheme theme = RenderTheme::defaultTheme();
  const QFontMetricsF metrics(theme.paragraphFont());

  session.setMarkdownText(QStringLiteral("[^]: "), false);
  MarkdownNode* footnote = blockAt(session, 0);
  const DefinitionBlock definition = footnote->definition();
  CursorPosition cursor;
  cursor.blockId = footnote->id();
  cursor.text.nodeId = footnote->id();
  cursor.text.textOffset = definition.labelRange.start - definition.markerRange.start;
  cursor.text.sourceOffset = definition.labelRange.start;
  SelectionRange selection;
  selection.anchor = cursor;
  selection.focus = cursor;

  view.resize(800, 240);
  view.setDocument(session.document());
  view.setSelectionRange(selection);

  const BlockLayout* block = requireViewBlock(view, footnote->id(), QStringLiteral("focused footnote"));
  bool sawFocusedLabel = false;
  bool sawFocusedNote = false;
  for (const BlockLayout::DefinitionSlotLayout& slot : block->definitionSlots()) {
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Label) {
      sawFocusedLabel = slot.focused && slot.text.isEmpty();
      require(slot.rect.width() < metrics.horizontalAdvance(QStringLiteral("name")),
              "focused empty footnote label slot should collapse placeholder width");
    }
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Note) {
      sawFocusedNote = slot.focused;
    }
  }
  require(sawFocusedLabel, "focused empty footnote label slot should suppress placeholder");
  require(!sawFocusedNote, "unfocused empty footnote note slot should keep placeholder");

  cursor.text.textOffset = definition.noteRange.start - definition.markerRange.start;
  cursor.text.sourceOffset = definition.noteRange.start;
  selection.anchor = cursor;
  selection.focus = cursor;
  view.setSelectionRange(selection);
  block = requireViewBlock(view, footnote->id(), QStringLiteral("focused footnote note"));
  sawFocusedNote = false;
  for (const BlockLayout::DefinitionSlotLayout& slot : block->definitionSlots()) {
    if (slot.field == BlockLayout::DefinitionSlotLayout::Field::Note) {
      sawFocusedNote = slot.focused && slot.text.isEmpty();
      require(slot.rect.width() < metrics.horizontalAdvance(QStringLiteral("input description here")),
              "focused empty footnote note slot should collapse placeholder width");
    }
  }
  require(sawFocusedNote, "focused empty footnote note slot should suppress placeholder");
}

// testClickBelowLastBlockHitsBlockAfterWithDistinctCursorRect
// Clicking below the last block lands on the virtual trailing paragraph with a
// caret that reads as a fresh empty line (paragraph line height, sitting a
// block-spacing below the last block — not glued to its bottom edge).
void testClickBelowLastBlockHitsBlockAfterWithDistinctCursorRect() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  view.resize(800, 600);
  view.setDocument(session.document());

  MarkdownNode* paragraph = blockAt(session, 0);
  const QRectF blockRect = view.nodeRect(paragraph->id());
  require(!blockRect.isEmpty(), "paragraph block rect should exist");

  const RenderTheme theme = view.theme();
  const QPointF clickPoint(blockRect.left(), blockRect.bottom() + theme.blockSpacing() + 5.0);
  const HitTestResult hit = view.hitTest(clickPoint);
  require(hit.zone == HitTestResult::Zone::BlockAfter, "click below last block should hit BlockAfter zone");
  require(hit.cursorRect.top() >= blockRect.bottom() + theme.blockSpacing() - 0.5,
          "trailing caret should sit a block-spacing below the last block");
  const qreal expectedLineHeight = QFontMetricsF(theme.paragraphFont()).height();
  require(qAbs(hit.cursorRect.height() - expectedLineHeight) < 0.5,
          "trailing caret should use paragraph line height, not code line height");

  // The virtual trailing line should always be reachable: total height must
  // reserve room for it plus a click margin below the last block.
  const qreal expectedFloor = blockRect.bottom() + theme.bottomMargin() + theme.blockSpacing() + expectedLineHeight;
  require(view.layoutTotalHeight() >= expectedFloor - 0.5, "layout height should reserve trailing virtual-paragraph space");
}

// testBlockAfterCursorSurvivesRebuild
// Regression guard: a caret placed on the virtual trailing paragraph must stay
// there across a layout rebuild (here triggered by resize). Before the fix,
// hitForCursorPosition recomputed from a CursorPosition and snapped the caret
// back inside the last block.
void testBlockAfterCursorSurvivesRebuild() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  session.setMarkdownText(QStringLiteral("alpha"), false);
  view.resize(800, 600);
  view.setDocument(session.document());

  MarkdownNode* paragraph = blockAt(session, 0);
  const QRectF blockRect = view.nodeRect(paragraph->id());
  const RenderTheme theme = view.theme();
  const QPointF clickPoint(blockRect.left(), blockRect.bottom() + theme.blockSpacing() + 5.0);
  const HitTestResult hit = view.hitTest(clickPoint);
  require(hit.zone == HitTestResult::Zone::BlockAfter, "click below last block should hit BlockAfter zone");

  view.setCursorHit(hit);
  require(view.cursorHit().zone == HitTestResult::Zone::BlockAfter, "caret should be on the trailing paragraph after click");
  require(view.cursorPosition().afterBlock, "stored cursor position should carry afterBlock");

  // Trigger a full layout rebuild (page width changes → rebuildLayout path,
  // which recomputes cursorHit_ via hitForCursorPosition).
  view.resize(640, 480);
  require(view.cursorHit().zone == HitTestResult::Zone::BlockAfter,
          "trailing caret should survive a layout rebuild (regression: it used to snap back into the last block)");
  const QRectF newBlockRect = view.nodeRect(paragraph->id());
  require(view.cursorHit().cursorRect.top() >= newBlockRect.bottom() + theme.blockSpacing() - 0.5,
          "trailing caret should still sit below the last block after rebuild");
}

// When the last block is already an empty paragraph, the virtual trailing
// paragraph must NOT appear — the empty paragraph itself is the append target.
// Clicking below it lands inside the empty paragraph instead of BlockAfter.
void testNoVirtualTrailingParagraphBelowEmptyLastParagraph() {
  // Trailing empty paragraph (document ends with a blank line).
  {
    DocumentSession session;
    EditorView view;
    EditorController controller;
    controller.attach(&session, &view);

    session.setMarkdownText(QStringLiteral("alpha\n\n"), false);
    view.resize(800, 600);
    view.setDocument(session.document());
    require(session.document().root().children().size() == 2, "fixture should have alpha + empty paragraph");
    MarkdownNode* emptyParagraph = blockAt(session, 1);
    require(emptyParagraph->type() == BlockType::Paragraph, "last block should be a paragraph");

    const QRectF emptyRect = view.nodeRect(emptyParagraph->id());
    require(!emptyRect.isEmpty(), "empty trailing paragraph rect should exist");
    const RenderTheme theme = view.theme();
    const QPointF belowEmpty(emptyRect.left(), emptyRect.bottom() + theme.blockSpacing() + 5.0);
    const HitTestResult hit = view.hitTest(belowEmpty);
    require(hit.zone != HitTestResult::Zone::BlockAfter, "no virtual trailing paragraph below an empty last paragraph");
    require(hit.blockId == emptyParagraph->id(), "click below empty trailing paragraph should land in that paragraph");
  }

  // Empty document: the parser inserts one virtual empty paragraph, which is
  // the type target — no second virtual trailing line below it.
  {
    DocumentSession session;
    EditorView view;
    EditorController controller;
    controller.attach(&session, &view);

    session.setMarkdownText(QString(), false);
    view.resize(800, 600);
    view.setDocument(session.document());
    require(session.document().root().children().size() == 1, "empty document should have one virtual empty paragraph");
    MarkdownNode* emptyParagraph = blockAt(session, 0);

    const QRectF emptyRect = view.nodeRect(emptyParagraph->id());
    require(!emptyRect.isEmpty(), "empty document paragraph rect should exist");
    const RenderTheme theme = view.theme();
    const QPointF belowEmpty(emptyRect.left(), emptyRect.bottom() + theme.blockSpacing() + 5.0);
    const HitTestResult hit = view.hitTest(belowEmpty);
    require(hit.zone != HitTestResult::Zone::BlockAfter, "no virtual trailing paragraph below empty document's paragraph");
    require(hit.blockId == emptyParagraph->id(), "click below empty document paragraph should land in that paragraph");
  }
}

// testNoWrapCodeFenceCursorStaysOnSingleRow
// Regression: with markdown/codeBlockWrap OFF, a caret at the end of a long code
// line must stay on that single visual row when recomputed from a CursorPosition
// (rebuildLayout → editor_geometry::hitForCursorPosition → literalCursorRectForOffset).
// Before the fix the caret-geometry path hardcoded soft-wrap, so the caret dropped
// onto a phantom second (wrapped) row even though the line renders unwrapped —
// typing then appeared to insert after the first line while the caret sat below it.
void testNoWrapCodeFenceCursorStaysOnSingleRow() {
  SettingsOverride wrapOff("markdown/codeBlockWrap", false);
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  const QString longLine(400, QLatin1Char('a'));
  session.setMarkdownText(QStringLiteral("```\n") + longLine + QStringLiteral("\n```"), false);
  view.resize(640, 320);
  view.setDocument(session.document());

  MarkdownNode* code = blockAt(session, 0);
  require(code->type() == BlockType::CodeFence, "fixture block should be a code fence");
  const BlockLayout* block = requireViewBlock(view, code->id(), QStringLiteral("long-line code fence"));
  const RenderTheme theme = view.theme();
  const QRectF contentRect = block->literalContentRect(theme);
  require(block->codeMaxLineWidth() > contentRect.width() + 0.5,
          "the long line should overflow the content width so the fence is horizontally scrollable");

  // Place the caret at the very end of the long line.
  CursorPosition endCursor;
  endCursor.blockId = code->id();
  endCursor.text.nodeId = code->id();
  endCursor.text.textOffset = longLine.size();
  view.setCursorPosition(endCursor);

  // A resize forces rebuildLayout, which recomputes cursorHit_ from cursorPosition_
  // via editor_geometry::hitForCursorPosition — the exact path that used to hardcode wrap.
  view.resize(720, 320);

  const BlockLayout* rebuiltBlock = requireViewBlock(view, code->id(), QStringLiteral("rebuilt long-line code fence"));
  const QRectF rebuiltContent = rebuiltBlock->literalContentRect(theme);
  const HitTestResult caret = view.cursorHit();
  require(caret.zone == HitTestResult::Zone::Code, "recomputed caret should still be in the code zone");
  // The caret must remain on the FIRST content row (its top == content top), not wrapped down to a
  // phantom second row. Under the bug the caret sat ~8 wrapped rows lower.
  require(qAbs(caret.cursorRect.top() - rebuiltContent.top()) < 0.5,
          "NoWrap code caret should stay on the single content row, not wrap to a phantom second row");
  // And its x is the line's natural advance (well past the visible window), proving the layout used
  // NoWrap rather than wrapping the long line inside the content width.
  require(caret.cursorRect.left() - rebuiltContent.left() > rebuiltContent.width(),
          "NoWrap code caret x should be the natural advance, beyond the wrap width");
}

// testScrollableCodeFenceHitRespectsHorizontalOffset
// paintCodeFence draws a NoWrap line with painter.translate(-offset); a click at view-space x must
// therefore resolve to the character whose advance is ~viewX+offset, i.e. the hit must ADD the
// horizontal scroll offset (undoing the paint's leftward translate). A sign flip here mapped any
// scrolled-right click back toward the line start (right-edge click → offset 0), which surfaced as
// the drag-selection caret jumping left on drag-start.
void testScrollableCodeFenceHitRespectsHorizontalOffset() {
  SettingsOverride wrapOff("markdown/codeBlockWrap", false);
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  const QString longLine(400, QLatin1Char('a'));
  session.setMarkdownText(QStringLiteral("```\n") + longLine + QStringLiteral("\n```"), false);
  view.resize(640, 320);
  view.setDocument(session.document());

  MarkdownNode* code = blockAt(session, 0);
  require(code->type() == BlockType::CodeFence, "fixture block should be a code fence");
  const BlockLayout* block = requireViewBlock(view, code->id(), QStringLiteral("scrollable code fence"));
  const RenderTheme theme = view.theme();
  const QRectF content = block->literalContentRect(theme);
  require(block->codeMaxLineWidth() > content.width() + 0.5, "line should overflow");
  const qreal maxOffset = block->codeMaxLineWidth() - content.width();
  controller.codeFenceScroll().setOffset(code->id(), maxOffset);

  // Scrolled to show the line's END: a click at the right edge must map near the end, and the left
  // edge must map near the start of the visible span (well into the line, not back to offset 0).
  const QPointF clickAtEnd(content.right() - 6.0, content.top() + theme.codeLineHeight() * 0.5);
  const HitTestResult hit = view.hitTest(clickAtEnd);
  require(hit.blockId == code->id(), "click should hit the code fence");
  require(hit.textOffset > longLine.size() - 12,
          QStringLiteral("right-edge click in the scrolled-to-end window should map near the line end, got %1").arg(hit.textOffset));

  const QPointF clickAtStart(content.left() + 6.0, content.top() + theme.codeLineHeight() * 0.5);
  const HitTestResult hitStart = view.hitTest(clickAtStart);
  require(hitStart.textOffset > 0 && hitStart.textOffset < hit.textOffset,
          QStringLiteral("left-edge click should map before the right-edge click within the scrolled span, got %1 < %2")
              .arg(hitStart.textOffset)
              .arg(hit.textOffset));
}

// testScrollableCodeFenceDragAnchorStaysUnderPress
// Empirical drag simulation: in a wrap-OFF code fence scrolled to the right, press then drag-select.
// The selection anchor must stay where the mouse pressed (a high offset in the right half of the
// long line) — not jump back toward the line start. Decides whether the drag symptom is a real bug
// or a stale running process: drag uses the same hitTest as click, so once the offset-sign is fixed
// both must be correct.
void testScrollableCodeFenceDragAnchorStaysUnderPress() {
  SettingsOverride wrapOff("markdown/codeBlockWrap", false);
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  const QString longLine(400, QLatin1Char('a'));
  session.setMarkdownText(QStringLiteral("```\n") + longLine + QStringLiteral("\n```"), false);
  view.resize(640, 320);
  view.setDocument(session.document());

  MarkdownNode* code = blockAt(session, 0);
  const BlockLayout* block = requireViewBlock(view, code->id(), QStringLiteral("scrollable code fence"));
  const RenderTheme theme = view.theme();
  const QRectF content = block->literalContentRect(theme);
  require(block->codeMaxLineWidth() > content.width() + 0.5, "line should overflow");
  const qreal maxOffset = block->codeMaxLineWidth() - content.width();
  controller.codeFenceScroll().setOffset(code->id(), maxOffset);

  SelectionRange captured;
  QObject::connect(&view, &EditorView::selectionChanged, &view,
                   [&captured](SelectionRange s, HitTestResult) { captured = s; });

  // Press in the right third of the visible window (showing the line's end), then drag a bit further right.
  const qreal y = content.top() + theme.codeLineHeight() * 0.5;
  const QPointF pressPos(content.right() - content.width() * 0.25, y);
  const QPointF dragPos(content.right() - content.width() * 0.10, y);
  QMouseEvent press(QEvent::MouseButtonPress, pressPos, pressPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);
  QMouseEvent move(QEvent::MouseMove, dragPos, dragPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &move);

  require(captured.anchor.text.nodeId.isValid(), "drag should have produced a selection");
  require(captured.anchor.text.textOffset > longLine.size() / 2,
          QStringLiteral("drag anchor should stay in the pressed (right) region, not jump to the line start; got anchor=%1 focus=%2")
              .arg(captured.anchor.text.textOffset)
              .arg(captured.focus.text.textOffset));
}

// testCodeFenceClickDoesNotScrollAwayFromPress
// Root cause of the drag-jump symptom: ensureCodeFenceCursorVisible used to margin-scroll on a click
// whose caret was already visible. That changed the horizontal offset AFTER the drag anchor had been
// captured from the (pre-scroll) hit, but BEFORE the drag focus was resolved (post-scroll offset) —
// so the anchor no longer sat under the pressed pixel and the caret visibly jumped sideways the
// instant the drag began. Now the follow only scrolls when the caret is genuinely off-screen, so a
// click leaves the offset untouched and the drag anchor stays exactly where the mouse pressed.
// The 75%-press test above never tripped the old margin-scroll (both branches clamp at maxOffset),
// so it passed while the real bug survived — this test parks the offset just shy of the end so a
// right-edge click used to fire the un-clamped scroll.
void testCodeFenceClickDoesNotScrollAwayFromPress() {
  SettingsOverride wrapOff("markdown/codeBlockWrap", false);
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  const QString longLine(400, QLatin1Char('a'));
  session.setMarkdownText(QStringLiteral("```\n") + longLine + QStringLiteral("\n```"), false);
  view.resize(640, 320);
  view.setDocument(session.document());

  MarkdownNode* code = blockAt(session, 0);
  const BlockLayout* block = requireViewBlock(view, code->id(), QStringLiteral("scrollable code fence"));
  const RenderTheme theme = view.theme();
  const QRectF content = block->literalContentRect(theme);
  const qreal maxOffset = block->codeMaxLineWidth() - content.width();
  require(maxOffset > 40.0, "line should overflow enough to scroll well past a margin");
  // Park just shy of the far-right end so a right-edge click used to trip the (un-clamped)
  // margin-scroll — exactly the user's "scrolled near the end" scenario.
  const qreal parkedOffset = maxOffset - 40.0;
  controller.codeFenceScroll().setOffset(code->id(), parkedOffset);

  SelectionRange captured;
  QObject::connect(&view, &EditorView::selectionChanged, &view,
                   [&captured](SelectionRange s, HitTestResult) { captured = s; });

  const QFontMetricsF metrics(theme.codeFont());
  const qreal charWidth = metrics.horizontalAdvance(QLatin1Char('a'));
  const qreal y = content.top() + theme.codeLineHeight() * 0.5;
  // Press near the RIGHT edge of the visible window. Under the bug the offset increased and the
  // content shifted left, so the drag anchor ended up left of the pressed pixel ("jumped left").
  const QPointF pressPos(content.right() - charWidth * 1.5, y);
  QMouseEvent press(QEvent::MouseButtonPress, pressPos, pressPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);

  // The caret was placed at a visible spot, so the offset must be untouched — this is the fix.
  require(qAbs(controller.codeFenceScroll().offsetFor(code->id()) - parkedOffset) < 0.5,
          QStringLiteral("clicking a visible code-fence position must not scroll the content away from the click; offset=%1")
              .arg(controller.codeFenceScroll().offsetFor(code->id())));

  // Drag a little further right; the anchor must stay under the press, not jump sideways.
  const QPointF dragPos(content.right() - charWidth * 0.5, y);
  QMouseEvent move(QEvent::MouseMove, dragPos, dragPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &move);

  require(captured.anchor.text.nodeId.isValid(), "drag should have produced a selection");
  const qreal anchorAdvance = metrics.horizontalAdvance(longLine.left(captured.anchor.text.textOffset));
  const qreal anchorViewportX = anchorAdvance - controller.codeFenceScroll().offsetFor(code->id());
  const qreal pressViewportX = pressPos.x() - content.left();
  require(qAbs(anchorViewportX - pressViewportX) < charWidth * 1.5,
          QStringLiteral("drag anchor should stay under the press position (no sideways jump); anchorViewportX=%1 pressViewportX=%2")
              .arg(anchorViewportX)
              .arg(pressViewportX));
}

// testCodeFenceCaretRectFollowsHorizontalScroll
// paintInsertionCursor (and the IME cursor rectangle) consume effectiveCursorRect, which subtracts a
// scrollable code fence's horizontal offset. Before the fix both used cursorHit_.cursorRect directly
// (the natural advance), so in a scrolled code fence the caret sat at ~line width — far outside the
// viewport — while the text had scrolled left underneath it.
void testCodeFenceCaretRectFollowsHorizontalScroll() {
  SettingsOverride wrapOff("markdown/codeBlockWrap", false);
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  const QString longLine(400, QLatin1Char('a'));
  session.setMarkdownText(QStringLiteral("```\n") + longLine + QStringLiteral("\n```"), false);
  view.resize(640, 320);
  view.setDocument(session.document());

  MarkdownNode* code = blockAt(session, 0);
  const BlockLayout* block = requireViewBlock(view, code->id(), QStringLiteral("scrollable code fence"));
  const RenderTheme theme = view.theme();
  const QRectF content = block->literalContentRect(theme);
  const qreal maxOffset = block->codeMaxLineWidth() - content.width();
  require(maxOffset > 1.0, "line should overflow enough to scroll");
  controller.codeFenceScroll().setOffset(code->id(), maxOffset);  // scrolled to the far-right end

  const QFontMetricsF metrics(theme.codeFont());
  const qreal charWidth = metrics.horizontalAdvance(QLatin1Char('a'));
  const qreal y = content.top() + theme.codeLineHeight() * 0.5;
  // Press near the right edge so the caret lands near the line end (visible only because we scrolled).
  const QPointF pressPos(content.right() - charWidth * 1.5, y);
  QMouseEvent press(QEvent::MouseButtonPress, pressPos, pressPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);

  const QRectF caret = view.effectiveCursorRect();
  require(!caret.isEmpty(), "caret rect should be valid after a click");
  // The caret must land INSIDE the content's visible span (offset-adjusted). Before the fix it sat at
  // x≈natural advance (~line width), well past the right edge and off-screen.
  require(caret.left() >= content.left() - 1.0 && caret.left() <= content.right() + 1.0,
          QStringLiteral("scrolled code-fence caret should be inside the viewport (offset-adjusted), got x=%1").arg(caret.left()));
  // And specifically at the pressed character, not at the natural advance.
  require(qAbs(caret.left() - pressPos.x()) < charWidth * 2.0,
          QStringLiteral("caret should follow the horizontal scroll to the pressed character; caretX=%1 pressX=%2")
              .arg(caret.left())
              .arg(pressPos.x()));
}

// testCodeFenceScrollableSelectionRectAlignsWithDrag
// The selection HIGHLIGHT (not just the logical anchor) must land under the mouse in a scrolled
// code fence. The rects come back in document space at the content's natural advance; paint shifts
// them by -offset to match the translated text. Asserting the offset-adjusted span equals the
// press/drag viewport span guards against a highlight that drifts off the characters it covers
// (e.g. from a QFontMetricsF-vs-QTextLayout divergence, a double offset, or a stale offset).
void testCodeFenceScrollableSelectionRectAlignsWithDrag() {
  SettingsOverride wrapOff("markdown/codeBlockWrap", false);
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  const QString longLine(400, QLatin1Char('a'));
  session.setMarkdownText(QStringLiteral("```\n") + longLine + QStringLiteral("\n```"), false);
  view.resize(640, 320);
  view.setDocument(session.document());

  MarkdownNode* code = blockAt(session, 0);
  const RenderTheme theme = view.theme();
  const QFontMetricsF metrics(theme.codeFont());
  const qreal charWidth = metrics.horizontalAdvance(QLatin1Char('a'));
  const BlockLayout* block = view.blockLayoutForNode(code->id());
  require(block != nullptr && block->type() == BlockType::CodeFence, "code fence block should resolve");
  const QRectF content = block->literalContentRect(theme);
  const qreal maxOffset = block->codeMaxLineWidth() - content.width();
  controller.codeFenceScroll().setOffset(code->id(), maxOffset);

  SelectionRange captured;
  QObject::connect(&view, &EditorView::selectionChanged, &view, [&captured](SelectionRange s, HitTestResult) { captured = s; });
  const qreal y = content.top() + theme.codeLineHeight() * 0.5;
  const QPointF pressPos(content.left() + 120.0, y);
  const QPointF dragPos(content.left() + 420.0, y);
  QMouseEvent press(QEvent::MouseButtonPress, pressPos, pressPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);
  QMouseEvent move(QEvent::MouseMove, dragPos, dragPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &move);

  const BlockLayout* blk = view.blockLayoutForNode(code->id());
  const QVector<QRectF> rects = blk->selectionRects(captured, theme);
  require(!rects.isEmpty(), "drag should produce at least one selection rect");
  const qreal offset = controller.codeFenceScroll().offsetFor(code->id());
  const qreal rectLeftVx = rects.first().left() - offset - content.left();
  const qreal rectRightVx = rects.last().right() - offset - content.left();
  const qreal spanLeft = qMin(rectLeftVx, rectRightVx);
  const qreal spanRight = qMax(rectLeftVx, rectRightVx);
  const qreal pressVx = pressPos.x() - content.left();
  const qreal dragVx = dragPos.x() - content.left();
  require(qAbs(spanLeft - qMin(pressVx, dragVx)) < charWidth * 1.5,
          QStringLiteral("selection span left should sit under the drag start; spanLeft=%1 expected=%2")
              .arg(spanLeft).arg(qMin(pressVx, dragVx)));
  require(qAbs(spanRight - qMax(pressVx, dragVx)) < charWidth * 1.5,
          QStringLiteral("selection span right should sit under the drag end; spanRight=%1 expected=%2")
              .arg(spanRight).arg(qMax(pressVx, dragVx)));
}

// testCodeFenceHitKeepsScrollControllerAcrossLayoutReset
// Regression for the "drag selection shifts left, more the further I scroll" bug. rebuildLayout's
// no-document branch (reached e.g. when the app applies theme/zoom before a file is open) handed
// back a fresh DocumentLayout, dropping the CodeFenceScrollController that attach() had wired onto
// the previous one. The paint path reads the controller from the EditorView directly (so the text
// scrolled correctly), but DocumentLayout::hitTest forwards its OWN stored member — which was now
// null — so clicks mapped at offset 0. Drag selections therefore landed on characters offset by
// the full scroll amount from where the user pointed. The fix re-wires the controller onto any
// (re)created layout in rebuildLayout.
void testCodeFenceHitKeepsScrollControllerAcrossLayoutReset() {
  SettingsOverride wrapOff("markdown/codeBlockWrap", false);
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);

  // Force a rebuild with no document yet (mimics setTheme/setZoom applied before a file is opened):
  // pre-fix this replaced layout_ and dropped the controller.
  view.setTheme(view.theme());

  const QString longLine(400, QLatin1Char('a'));
  session.setMarkdownText(QStringLiteral("```\n") + longLine + QStringLiteral("\n```"), false);
  view.resize(640, 320);
  view.setDocument(session.document());

  MarkdownNode* code = blockAt(session, 0);
  const BlockLayout* block = view.blockLayoutForNode(code->id());
  require(block != nullptr && block->type() == BlockType::CodeFence, "code fence block should resolve");
  const RenderTheme theme = view.theme();
  const QRectF content = block->literalContentRect(theme);
  const qreal maxOffset = block->codeMaxLineWidth() - content.width();
  require(maxOffset > 1.0, "line should overflow enough to scroll");
  controller.codeFenceScroll().setOffset(code->id(), maxOffset);  // scrolled to the far-right end

  // A click at the right edge of the visible window must map near the line END. Under the bug the
  // hit saw a null controller (offset 0), so a right-edge click resolved to offset ~0 (the start).
  const QPointF clickAtEnd(content.right() - 6.0, content.top() + theme.codeLineHeight() * 0.5);
  const HitTestResult hit = view.hitTest(clickAtEnd);
  require(hit.blockId == code->id(), "click should hit the code fence");
  require(hit.textOffset > longLine.size() - 12,
          QStringLiteral("scrolled-to-end right-edge click should map near the line end (hit must see the offset); got %1")
              .arg(hit.textOffset));
}

// A click in the gap below a NON-last block must NOT resolve to the trailing BlockAfter zone.
// hitTest used to return BlockAfter(nearestBlock) for ANY click below a block's bottom (not just
// the last block), so clicking the gap after a block — and typing — called insertBlockAfterCurrentBlock
// and inserted a brand-new paragraph (the user's "empty line I can click into and type" between a
// heading and the following text). BlockAfter is only valid below the LAST block. To reproduce
// deterministically (independent of inter-block gap size), we click below a SHORT block that is
// followed by a TALLER block: the tall block's centre sits far enough below that the short block
// stays the nearest-by-centre for clicks just below its bottom, exercising exactly the buggy branch.
void testGapClickBelowNonLastBlockIsNotTrailingBlockAfter() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText(QStringLiteral("Intro paragraph.\n\n# A Heading Line\n\nFinal paragraph."), false);
  view.resize(800, 600);
  view.setDocument(session.document());

  MarkdownNode* intro = blockAt(session, 0);
  MarkdownNode* heading = blockAt(session, 1);
  MarkdownNode* finalPara = blockAt(session, 2);
  const BlockLayout* introBlock = requireViewBlock(view, intro->id(), QStringLiteral("intro"));
  const BlockLayout* headingBlock = requireViewBlock(view, heading->id(), QStringLiteral("heading"));
  const BlockLayout* finalBlock = requireViewBlock(view, finalPara->id(), QStringLiteral("final"));

  const qreal windowTop = introBlock->rect().bottom();
  const qreal windowBottom = (introBlock->rect().center().y() + headingBlock->rect().center().y()) / 2.0;
  require(windowBottom > windowTop + 1.0,
          QStringLiteral("test setup expects a gap below the intro block where it is the nearest block"));
  const QPointF gapClick(introBlock->rect().left() + 8.0, (windowTop + windowBottom) / 2.0);
  const HitTestResult gapHit = view.hitTest(gapClick);
  require(gapHit.isValid(), QStringLiteral("gap click should resolve to a valid hit"));
  require(gapHit.zone != HitTestResult::Zone::BlockAfter,
          QStringLiteral("clicking the gap below a non-last block must NOT be the trailing BlockAfter zone"));
  require(gapHit.blockId == intro->id() || gapHit.blockId == heading->id(),
          QStringLiteral("gap click should snap to the intro or the heading, not signal a new block"));

  // Typing at that hit must not spawn a new top-level block.
  controller.activateHit(gapHit);
  require(controller.inputController().insertText(QStringLiteral("1")),
          QStringLiteral("typing after the gap click should be handled"));
  require(session.document().root().children().size() == 3,
          QStringLiteral("typing in the gap must not insert a new paragraph (still 3 top-level blocks)"));

  // The trailing position below the LAST block is unchanged: still BlockAfter.
  const QPointF trailingClick(finalBlock->rect().left() + 8.0, finalBlock->rect().bottom() + 60.0);
  const HitTestResult trailingHit = view.hitTest(trailingClick);
  require(trailingHit.zone == HitTestResult::Zone::BlockAfter,
          QStringLiteral("clicking below the last block should still be the trailing BlockAfter zone"));
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
  // codeBlockWrapEnabled() reads a default-constructed QSettings(); without org/app names the
  // SettingsOverride below writes to a location the production code reads inconsistently, so the
  // wrap setting never takes effect (the markdown-prefs gotcha).
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("EditorViewHitTestTest"));
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testGapClickBelowNonLastBlockIsNotTrailingBlockAfter);
  RUN_TEST(testDefinitionPlaceholderHitKeepsCursorInSlot);
  RUN_TEST(testEmptyLinkDefinitionTitlePlaceholderOnlyWhenFocused);
  RUN_TEST(testDefinitionBoundaryHitPlacesCursorAtSourceEdges);
  RUN_TEST(testNoTitleDefinitionRightEdgeHasNoInvisibleQuote);
  RUN_TEST(testDefinitionTokenModelDrivesSyntaxAndSlots);
  RUN_TEST(testDefinitionSyntaxHitChoosesNearestEditableSlot);
  RUN_TEST(testFootnoteDefinitionUsesTokenPath);
  RUN_TEST(testFocusedEmptyFootnoteDefinitionSlotSuppressesPlaceholder);
  RUN_TEST(testClickBelowLastBlockHitsBlockAfterWithDistinctCursorRect);
  RUN_TEST(testBlockAfterCursorSurvivesRebuild);
  RUN_TEST(testNoVirtualTrailingParagraphBelowEmptyLastParagraph);
  RUN_TEST(testNoWrapCodeFenceCursorStaysOnSingleRow);
  RUN_TEST(testScrollableCodeFenceHitRespectsHorizontalOffset);
  RUN_TEST(testScrollableCodeFenceDragAnchorStaysUnderPress);
  RUN_TEST(testCodeFenceClickDoesNotScrollAwayFromPress);
  RUN_TEST(testCodeFenceCaretRectFollowsHorizontalScroll);
  RUN_TEST(testCodeFenceScrollableSelectionRectAlignsWithDrag);
  RUN_TEST(testCodeFenceHitKeepsScrollControllerAcrossLayoutReset);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
