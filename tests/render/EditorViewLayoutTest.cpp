#include "EditorViewTestUtils.h"

using namespace muffin;

void testEditorViewPlainInlineLayout() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("alpha beta gamma delta epsilon"), false);
  view.resize(900, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();

  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, QStringLiteral("plain inline"));
  const QVector<qsizetype> offsets{0, 1, 6, 12, inlineLayout->plainText().size()};
  for (qsizetype offset : offsets) {
    const HitTestResult hit = hitAtTextOffset(view, blockId, offset, QStringLiteral("plain inline %1").arg(offset));
    require(hit.textOffset == offset, QStringLiteral("plain inline hit offset mismatch"));
    require(hit.sourceOffset == offset, QStringLiteral("plain inline source offset mismatch"));
    require(!inlineLayout->cursorRect(offset).isEmpty(), QStringLiteral("plain inline cursor rect should exist"));
  }

  const SelectionRange selection = inlineSelection(blockId, 1, inlineLayout->plainText().size() - 1);
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("plain inline"))->selectionRects(selection, RenderTheme::defaultTheme());
  require(!rects.isEmpty(), QStringLiteral("plain inline selection rects should exist"));
}

void testEditorViewStyledInlineLayout() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("before **bold** [link](u) `code` after"), false);
  view.resize(900, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();

  const CursorPosition activeCursor = inlineCursor(blockId, 9, 11);
  view.setCursorPosition(activeCursor);

  const HitTestResult hit = hitAtSourceOffset(view, blockId, 11, QStringLiteral("styled inline"));
  require(hit.sourceOffset == 11, QStringLiteral("styled active source offset mismatch"));

  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, QStringLiteral("styled inline"));
  const QRectF cursor = inlineLayout->cursorRectForSourceOffset(11);
  require(!cursor.isEmpty(), QStringLiteral("styled active cursor rect should exist"));

  SelectionRange selection;
  selection.anchor = inlineCursor(blockId, 7, 9);
  selection.focus = inlineCursor(blockId, 11, 13);
  view.setSelectionRange(selection);
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("styled inline"))->selectionRects(selection, RenderTheme::defaultTheme());
  require(!rects.isEmpty(), QStringLiteral("styled inline selection rects should exist"));
}

void testEditorViewListInlineMathHitEditing() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);
  view.resize(900, 500);
  session.setMarkdownText(QStringLiteral("- before $x+y$ after"), false);
  view.setDocument(session.document());

  MarkdownNode* item = listItemAt(session, 0, 0);
  const NodeId blockId = item->id();
  const qsizetype contentSourceStart = QStringLiteral("- ").size();
  const qsizetype localMathSourceOffset = QStringLiteral("before $x").size();
  CursorPosition activeCursor = inlineCursor(blockId, QStringLiteral("before x").size(), contentSourceStart + localMathSourceOffset);
  view.setCursorPosition(activeCursor);

  MarkdownNode* paragraph = childAt(item, 0);
  InlineLayout inlineLayout;
  InlineLayout::BuildOptions options;
  options.projectionState.cursorVisibleOffset = activeCursor.text.textOffset;
  options.projectionState.cursorSourceOffset = localMathSourceOffset;
  options.sourceBase = contentSourceStart;
  const RenderTheme theme = RenderTheme::defaultTheme();
  inlineLayout.build(
      paragraph->inlines(),
      QStringLiteral("before $x+y$ after"),
      theme,
      qMax<qreal>(1.0, view.nodeRect(blockId).width() - theme.listIndent()),
      theme.paragraphFont(),
      options);
  const QRectF cursor = inlineLayout.cursorRectForSourceOffset(localMathSourceOffset);
  require(!cursor.isEmpty(), "list inline math source cursor rect should exist");
  const QPointF hitPoint = view.nodeRect(blockId).topLeft() + QPointF(theme.listIndent() + cursor.left(), cursor.center().y());
  const HitTestResult hit = view.hitTest(hitPoint);
  require(hit.isValid() && hit.sourceOffset == contentSourceStart + localMathSourceOffset,
          QStringLiteral("list inline math hit should keep source offset: actual=%1 expected=%2 textOffset=%3")
              .arg(hit.sourceOffset)
              .arg(contentSourceStart + localMathSourceOffset)
              .arg(hit.textOffset));
  controller.activateHit(hit);
  require(controller.inputController().insertText(QStringLiteral("0")), "typing after list inline math hit should edit source");
  require(session.markdownText() == QStringLiteral("- before $x0+y$ after"), "list inline math hit insert should not drift");
  require(controller.selection().cursorPosition().text.sourceOffset == QStringLiteral("- before $x0").size(),
          "list inline math cursor source should stay after inserted text");
  require(controller.inputController().insertText(QStringLiteral("1")), "consecutive typing after list inline math hit should edit source");
  require(session.markdownText() == QStringLiteral("- before $x01+y$ after"), "consecutive list inline math insert should not drift");
}

void testEditorViewWrappedInlineLayout() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(
      QStringLiteral("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau"),
      false);
  view.resize(360, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();

  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, QStringLiteral("wrapped inline"));
  const QVector<CursorLineRange> lines = cursorLineRanges(*inlineLayout);
  require(lines.size() >= 2, QStringLiteral("wrapped inline view should create multiple lines"));

  for (const CursorLineRange& line : lines) {
    QVector<qsizetype> offsets{line.start, (line.start + line.end) / 2, line.end};
    for (qsizetype offset : offsets) {
      const HitTestResult hit = hitAtTextOffset(view, blockId, offset, QStringLiteral("wrapped inline %1").arg(offset));
      require(hit.textOffset == offset, QStringLiteral("wrapped inline hit-test should round-trip line offset"));
      require(hit.sourceOffset == offset, QStringLiteral("wrapped inline source offset should round-trip line offset"));
    }
  }
  for (qsizetype i = 1; i < lines.size(); ++i) {
    require(lines.at(i).y > lines.at(i - 1).y, QStringLiteral("wrapped inline cursor y should increase by line"));
    const QRectF previousEnd = inlineLayout->cursorRect(lines.at(i - 1).end);
    const QRectF nextStart = inlineLayout->cursorRect(lines.at(i).start);
    require(nextStart.left() <= previousEnd.left(), QStringLiteral("wrapped inline line start should return toward x origin"));
  }

  const SelectionRange selection = inlineSelection(blockId, 0, inlineLayout->plainText().size());
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("wrapped inline"))->selectionRects(selection, RenderTheme::defaultTheme());
  require(rects.size() == lines.size(), QStringLiteral("wrapped inline selection rect count should match line count"));
}

void testEditorViewTableCellInlineLayout() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("| Name | Value |\n| --- | --- |\n| one | two |"), false);
  view.resize(900, 500);
  view.setDocument(session.document());

  MarkdownNode* table = blockAt(session, 0);
  MarkdownNode* firstBodyCell = childAt(childAt(table, 1), 0);
  const BlockLayout* tableLayout = requireViewBlock(view, table->id(), QStringLiteral("table inline"));
  require(tableLayout->tableRows().size() >= 2, QStringLiteral("table rows should exist"));
  require(!tableLayout->tableRows().at(1).cells.empty(), QStringLiteral("table cells should exist"));

  const auto& cell = tableLayout->tableRows().at(1).cells.at(0);
  require(cell.nodeId == firstBodyCell->id(), QStringLiteral("table cell node id mismatch"));
  require(!cell.text.cursorRect(1).isEmpty(), QStringLiteral("table cell cursor rect should exist"));
  require(!cell.text.selectionRects(0, 3).isEmpty(), QStringLiteral("table cell selection rects should exist"));

  const QMarginsF padding = RenderTheme::defaultTheme().tableCellPadding();
  const QPointF point = cell.rect.marginsRemoved(padding).topLeft() + QPointF(cell.text.cursorRect(1).left(), cell.text.cursorRect(1).center().y());
  const HitTestResult hit = view.hitTest(point);
  require(hit.zone == HitTestResult::Zone::TableCell, QStringLiteral("table cell hit should use table cell zone"));
  require(hit.textNodeId == firstBodyCell->id(), QStringLiteral("table cell hit should return cell node id"));
}

void testEditorViewTableCellInlineCodeEndHit() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);

  const QString cellContent = QStringLiteral("vendored `cmark-gfm`");
  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| %1 |").arg(cellContent), false);
  view.resize(900, 500);
  view.setDocument(session.document());

  MarkdownNode* table = blockAt(session, 0);
  MarkdownNode* cellNode = childAt(childAt(table, 1), 0);
  const BlockLayout* tableLayout = requireViewBlock(view, table->id(), QStringLiteral("table inline code end"));
  const auto& cell = tableLayout->tableRows().at(1).cells.at(0);
  const QMarginsF padding = RenderTheme::defaultTheme().tableCellPadding();
  const QPointF textOrigin = cell.rect.marginsRemoved(padding).topLeft();
  const QRectF endCursor = cell.text.cursorRectForSourceOffset(cellContent.size());
  const QPointF point = textOrigin + QPointF(endCursor.left() + 2.0, endCursor.center().y());

  const HitTestResult hit = view.hitTest(point);
  require(hit.zone == HitTestResult::Zone::TableCell, QStringLiteral("inline code table end hit should use table cell zone"));
  require(hit.textNodeId == cellNode->id(), QStringLiteral("inline code table end hit should return cell node id"));
  require(hit.sourceOffset == cellNode->sourceRange().byteStart + cellContent.size(),
          QStringLiteral("inline code table end hit should map after closing marker"));

  const qsizetype cellSourceStart = cellNode->sourceRange().byteStart;
  controller.activateHit(hit);
  require(controller.inputController().insertText(QStringLiteral("!")), "table inline code end insert should work");
  require(session.markdownText().contains(QStringLiteral("| vendored `cmark-gfm`! |")),
          "table inline code end insert should append after inline code");
  require(controller.selection().cursorPosition().text.sourceOffset ==
              cellSourceStart + cellContent.size() + 1,
          "table inline code end insert source cursor mismatch");
  require(controller.selection().cursorPosition().text.textOffset == QStringLiteral("vendored cmark-gfm!").size(),
          "table inline code end insert text cursor mismatch");
}

void testTableCellRichInlineSelectionDeleteAndCopyUseSourceOffsets() {
  DocumentSession session;
  EditorController controller;
  EditorView view;
  controller.attach(&session, &view);

  const QString cellContent = QStringLiteral("vendored `cmark-gfm` tail");
  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| %1 |").arg(cellContent), false);
  view.resize(900, 500);
  view.setDocument(session.document());

  MarkdownNode* table = blockAt(session, 0);
  MarkdownNode* cell = childAt(childAt(table, 1), 0);
  const qsizetype cellStart = cell->sourceRange().byteStart;
  const qsizetype codeStart = cellContent.indexOf(QStringLiteral("cmark"));
  const qsizetype codeEnd = codeStart + QStringLiteral("cmark-gfm").size();
  setSourceSelection(
      controller.selection(),
      table,
      cell,
      QStringLiteral("vendored ").size(),
      cellStart + codeStart,
      QStringLiteral("vendored cmark-gfm").size(),
      cellStart + codeEnd);

  const QString selectedTablePlain = selectedPlainText(session, controller.selection());
  const QString selectedTableMarkdown = selectedMarkdown(session, controller.selection());
  require(selectedTablePlain == QStringLiteral("cmark-gfm"),
          QStringLiteral("table rich inline selected plain text mismatch: %1").arg(selectedTablePlain));
  require(selectedTableMarkdown == QStringLiteral("cmark-gfm"),
          QStringLiteral("table rich inline selected markdown mismatch: %1").arg(selectedTableMarkdown));
  require(controller.clipboardController().copy(), "table rich inline copy should work");
  require(QApplication::clipboard()->text() == QStringLiteral("cmark-gfm"), "table rich inline clipboard text mismatch");

  require(controller.inputController().deleteBackward(), "table rich inline selection backspace should delete selection");
  require(session.markdownText().contains(QStringLiteral("| vendored `` tail |")),
          "table rich inline selection delete should preserve code markers");
  require(controller.selection().cursorPosition().text.sourceOffset == cellStart + codeStart,
          "table rich inline selection delete source cursor mismatch");

  session.setMarkdownText(QStringLiteral("| A |\n| --- |\n| %1 |").arg(cellContent), false);
  table = blockAt(session, 0);
  cell = childAt(childAt(table, 1), 0);
  setSourceSelection(
      controller.selection(),
      table,
      cell,
      QStringLiteral("vendored ").size(),
      cell->sourceRange().byteStart + codeStart,
      QStringLiteral("vendored cmark-gfm").size(),
      cell->sourceRange().byteStart + codeEnd);
  require(controller.inputController().deleteForward(), "table rich inline selection delete should delete selection");
  require(session.markdownText().contains(QStringLiteral("| vendored `` tail |")),
          "table rich inline delete key should preserve code markers");
}

void testMixedInlineParagraphHitEditingBeforeAutolink() {
  DocumentSession session;
  EditorController controller;
  controller.attach(&session, nullptr);

  const QString markdown = QStringLiteral(
      "Plain text can mix **bold**, *italic*, ~~strikethrough~~, `inline code`, inline HTML <kbd>Ctrl</kbd> + "
      "<kbd>S</kbd>, links such as [Muffin](https://example.com), autolinks like https://example.com, and inline math $E = mc^2$.");
  session.setMarkdownText(markdown, false);

  HitTestResult boldHit;
  boldHit.zone = HitTestResult::Zone::Text;
  boldHit.blockId = blockAt(session, 0)->id();
  boldHit.textNodeId = boldHit.blockId;
  boldHit.textOffset = QStringLiteral("Plain text can mix bo").size();
  controller.activateHit(boldHit);
  require(controller.inputController().insertText(QStringLiteral("X")), "typing in mixed paragraph bold should work");
  require(session.markdownText().contains(QStringLiteral("**boXld**")), "mixed paragraph bold insert mismatch");

  session.setMarkdownText(markdown, false);
  HitTestResult italicHit;
  italicHit.zone = HitTestResult::Zone::Text;
  italicHit.blockId = blockAt(session, 0)->id();
  italicHit.textNodeId = italicHit.blockId;
  italicHit.textOffset = QStringLiteral("Plain text can mix bold, ita").size();
  controller.activateHit(italicHit);
  require(controller.inputController().insertText(QStringLiteral("Y")), "typing in mixed paragraph italic should work");
  require(session.markdownText().contains(QStringLiteral("*itaYlic*")), "mixed paragraph italic insert mismatch");

  session.setMarkdownText(markdown, false);
  HitTestResult strikeHit;
  strikeHit.zone = HitTestResult::Zone::Text;
  strikeHit.blockId = blockAt(session, 0)->id();
  strikeHit.textNodeId = strikeHit.blockId;
  strikeHit.textOffset = QStringLiteral("Plain text can mix bold, italic, strike").size();
  controller.activateHit(strikeHit);
  require(controller.inputController().insertText(QStringLiteral("Z")), "typing in mixed paragraph strike should work");
  require(session.markdownText().contains(QStringLiteral("~~strikeZthrough~~")), "mixed paragraph strike insert mismatch");
}

void testInlineSelectionRects() {
  RenderTheme theme = RenderTheme::defaultTheme();
  InlineLayout layout;
  QVector<InlineNode> inlines;
  inlines.push_back(InlineNode::text(QStringLiteral("alpha beta gamma")));
  layout.build(inlines, theme, 180, theme.paragraphFont());

  const QVector<QRectF> rects = layout.selectionRects(1, 8);
  require(!rects.isEmpty(), "inline selection rects should not be empty");
  qreal totalWidth = 0;
  for (const QRectF& rect : rects) {
    require(rect.width() > 0, "selection rect should have positive width");
    require(rect.height() > 0, "selection rect should have positive height");
    totalWidth += rect.width();
  }
  require(totalWidth > 0, "selection rect total width should be positive");
  require(layout.selectionRects(3, 3).isEmpty(), "collapsed selection should not create rects");
}

// An active heading projects content-only: its `# ` prefix is never rendered, so the projection's
// source space starts at the content start for both the active and inactive cases. The caret at
// every visible offset must hit-test back to document source offset (contentStart + visible); a
// drift means the layout's content-source-start diverged from the projection's source base.
void testEditorViewActiveHeadingHitContentOffsetStable() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("# hello"), false);
  view.resize(900, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();
  const qsizetype contentStart = QStringLiteral("# ").size();

  // Activate the heading by placing the caret in its content (after the "# " prefix).
  view.setCursorPosition(inlineCursor(blockId, 0, contentStart));

  for (qsizetype visible = 0; visible <= 5; ++visible) {
    const HitTestResult hit = hitAtTextOffset(view, blockId, visible, QStringLiteral("active heading @%1").arg(visible));
    require(hit.textOffset == visible, QStringLiteral("active heading visible offset drift @%1").arg(visible));
    require(hit.sourceOffset == contentStart + visible, QStringLiteral("active heading source offset drift @%1").arg(visible));
  }
}

// An active heading must render content-only: the `# ` prefix is NOT part of the projection's
// display text. Previously the prefix was rendered as a gray OpenMarker span whenever the cursor
// was on the heading; this guards against regressing back to always showing it.
void testEditorViewActiveHeadingRendersContentWithoutPrefix() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("# hello"), false);
  view.resize(900, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();
  require(blockAt(session, 0)->type() == BlockType::Heading, QStringLiteral("expected a heading node"));

  // Cursor inside the heading content makes the heading active.
  view.setCursorPosition(inlineCursor(blockId, 0, QStringLiteral("# ").size()));

  const InlineLayout* layout = requireViewInlineLayout(view, blockId, QStringLiteral("active heading"));
  require(layout->displayText() == QStringLiteral("hello"),
          QStringLiteral("active heading display text should be content only, got: %1").arg(layout->displayText()));
  require(layout->visibleText() == QStringLiteral("hello"),
          QStringLiteral("active heading visible text should be content only, got: %1").arg(layout->visibleText()));
}

// A Setext heading has no `# ` prefix at all (byteStart == contentStart), so it must also render
// content-only with no crash. Guards the "headings don't necessarily start with #" path.
void testEditorViewActiveSetextHeadingRendersContentWithoutPrefix() {
  DocumentSession session;
  EditorView view;
  session.setMarkdownText(QStringLiteral("hello\n==="), false);
  view.resize(900, 500);
  view.setDocument(session.document());
  const NodeId blockId = blockAt(session, 0)->id();
  require(blockAt(session, 0)->type() == BlockType::Heading, QStringLiteral("expected a setext heading node"));

  // Cursor inside the content (Setext content start == byte start == 0).
  view.setCursorPosition(inlineCursor(blockId, 0, 0));

  const InlineLayout* layout = requireViewInlineLayout(view, blockId, QStringLiteral("active setext heading"));
  require(layout->displayText() == QStringLiteral("hello"),
          QStringLiteral("active setext heading display text should be content only, got: %1").arg(layout->displayText()));
  require(layout->visibleText() == QStringLiteral("hello"),
          QStringLiteral("active setext heading visible text should be content only, got: %1").arg(layout->visibleText()));
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testEditorViewPlainInlineLayout);
  RUN_TEST(testEditorViewStyledInlineLayout);
  RUN_TEST(testEditorViewListInlineMathHitEditing);
  RUN_TEST(testEditorViewWrappedInlineLayout);
  RUN_TEST(testEditorViewTableCellInlineLayout);
  RUN_TEST(testEditorViewTableCellInlineCodeEndHit);
  RUN_TEST(testTableCellRichInlineSelectionDeleteAndCopyUseSourceOffsets);
  RUN_TEST(testMixedInlineParagraphHitEditingBeforeAutolink);
  RUN_TEST(testInlineSelectionRects);
  RUN_TEST(testEditorViewActiveHeadingHitContentOffsetStable);
  RUN_TEST(testEditorViewActiveHeadingRendersContentWithoutPrefix);
  RUN_TEST(testEditorViewActiveSetextHeadingRendersContentWithoutPrefix);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
