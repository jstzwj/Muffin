#include "EditorViewTestUtils.h"

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
  require(!block->definitionCursorRectForSourceOffset(hit.sourceOffset, RenderTheme::typoraLike()).isEmpty(),
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
                                                                          RenderTheme::typoraLike());
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
  const QRectF startCursor = block->definitionCursorRectForSourceOffset(definition.sourceRange.start, RenderTheme::typoraLike());
  const QRectF endCursor = block->definitionCursorRectForSourceOffset(definition.sourceRange.end, RenderTheme::typoraLike());
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

  const QRectF endCursor = block->definitionCursorRectForSourceOffset(definition.sourceRange.end, RenderTheme::typoraLike());
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
  const QRectF noteCursor = block->definitionCursorRectForSourceOffset(definition.noteRange.start, RenderTheme::typoraLike());
  require(!noteCursor.isEmpty(), "footnote note cursor should come from token path");
  const HitTestResult hit = view.hitTest(noteCursor.center());
  require(hit.definitionField == HitTestResult::DefinitionField::Note, "footnote token hit should target note field");
}

void testFocusedEmptyFootnoteDefinitionSlotSuppressesPlaceholder() {
  DocumentSession session;
  EditorView view;
  const RenderTheme theme = RenderTheme::typoraLike();
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

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testDefinitionPlaceholderHitKeepsCursorInSlot);
  RUN_TEST(testEmptyLinkDefinitionTitlePlaceholderOnlyWhenFocused);
  RUN_TEST(testDefinitionBoundaryHitPlacesCursorAtSourceEdges);
  RUN_TEST(testNoTitleDefinitionRightEdgeHasNoInvisibleQuote);
  RUN_TEST(testDefinitionTokenModelDrivesSyntaxAndSlots);
  RUN_TEST(testDefinitionSyntaxHitChoosesNearestEditableSlot);
  RUN_TEST(testFootnoteDefinitionUsesTokenPath);
  RUN_TEST(testFocusedEmptyFootnoteDefinitionSlotSuppressesPlaceholder);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
