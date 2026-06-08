#include "app/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/InlineProjection.h"
#include "document/MarkdownNode.h"
#include "document/SelectionSerializer.h"
#include "document/SourceRangeUtil.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>

#include <iostream>

using namespace muffin;

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

void require(bool condition, const QString& message) {
  if (!condition) {
    std::cerr << message.toStdString() << "\n";
    std::exit(1);
  }
}

void runTest(const char* name, void (*test)()) {
  std::cerr << "RUN " << name << "\n";
  test();
}

MarkdownNode* blockAt(const DocumentSession& session, qsizetype index) {
  const auto& children = session.document().root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "block index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

MarkdownNode* childAt(MarkdownNode* node, qsizetype index) {
  require(node != nullptr, "parent node should exist");
  const auto& children = node->children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "child index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

MarkdownNode* listItemAt(const DocumentSession& session, qsizetype listIndex, qsizetype itemIndex) {
  return childAt(blockAt(session, listIndex), itemIndex);
}

void setCursor(SelectionController& selection, MarkdownNode* block, qsizetype offset) {
  CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = offset;
  selection.setCursorPosition(cursor);
}

void setSourceCursor(SelectionController& selection, MarkdownNode* block, qsizetype visibleOffset, qsizetype sourceOffset) {
  CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = visibleOffset;
  cursor.text.sourceOffset = sourceOffset;
  selection.setCursorPosition(cursor);
}

bool pressKey(InputController& input, QObject* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  return input.eventFilter(target, &event);
}

void wireInput(
    InputController& input,
    DocumentSession& session,
    SelectionController& selection,
    UndoStack& undoStack,
    BrushQueue& brushQueue,
    EditorView* view = nullptr,
    QHash<int, LiteralBlockController*> literalEditors = {}) {
  input.setContext({&session, &selection, &undoStack, &brushQueue, view, literalEditors});
}

void setSelection(SelectionController& selection, MarkdownNode* block, qsizetype anchor, qsizetype focus) {
  SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = block->id();
  range.anchor.text.textOffset = anchor;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = block->id();
  range.focus.text.textOffset = focus;
  selection.setSelection(range);
}

void setSourceSelection(
    SelectionController& selection,
    MarkdownNode* block,
    MarkdownNode* textNode,
    qsizetype anchorTextOffset,
    qsizetype anchorSourceOffset,
    qsizetype focusTextOffset,
    qsizetype focusSourceOffset) {
  SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = textNode->id();
  range.anchor.text.textOffset = anchorTextOffset;
  range.anchor.text.sourceOffset = anchorSourceOffset;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = textNode->id();
  range.focus.text.textOffset = focusTextOffset;
  range.focus.text.sourceOffset = focusSourceOffset;
  selection.setSelection(range);
}

QString selectedMarkdown(const DocumentSession& session, const SelectionController& selection) {
  return SelectionSerializer().exportMarkdown(session.document(), selection.selection());
}

QString selectedPlainText(const DocumentSession& session, const SelectionController& selection) {
  return SelectionSerializer().exportPlainText(session.document(), selection.selection());
}

struct ExpectedProjectionSpan {
  InlineType type = InlineType::Unknown;
  InlineSpanKind kind = InlineSpanKind::Text;
  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  qsizetype contentSourceStart = 0;
  qsizetype contentSourceEnd = 0;
  qsizetype displayStart = 0;
  qsizetype displayEnd = 0;
  qsizetype visibleStart = 0;
  qsizetype visibleEnd = 0;
};

QVector<InlineNode> withInlineTestRanges(QVector<InlineNode> inlines, const QString& markdown);

void requireSpanContract(const InlineProjectionSpan& span, const ExpectedProjectionSpan& expected, qsizetype index, const char* label) {
  const QString prefix = QStringLiteral("%1 span %2").arg(QString::fromUtf8(label)).arg(index);
  require(span.type == expected.type, prefix + QStringLiteral(" type mismatch"));
  require(span.kind == expected.kind, prefix + QStringLiteral(" kind mismatch"));
  require(span.sourceStart == expected.sourceStart, prefix + QStringLiteral(" sourceStart mismatch"));
  require(span.sourceEnd == expected.sourceEnd, prefix + QStringLiteral(" sourceEnd mismatch"));
  require(span.contentSourceStart == expected.contentSourceStart, prefix + QStringLiteral(" contentSourceStart mismatch"));
  require(span.contentSourceEnd == expected.contentSourceEnd, prefix + QStringLiteral(" contentSourceEnd mismatch"));
  require(span.displayStart == expected.displayStart, prefix + QStringLiteral(" displayStart mismatch"));
  require(span.displayEnd == expected.displayEnd, prefix + QStringLiteral(" displayEnd mismatch"));
  require(span.visibleStart == expected.visibleStart, prefix + QStringLiteral(" visibleStart mismatch"));
  require(span.visibleEnd == expected.visibleEnd, prefix + QStringLiteral(" visibleEnd mismatch"));
}

void requireProjectionSpans(
    const QVector<InlineNode>& inlines,
    const QString& markdown,
    InlineProjectionState state,
    const QVector<ExpectedProjectionSpan>& expected,
    const QString& displayText,
    const QString& visibleText,
    const char* label) {
  InlineProjection projection(withInlineTestRanges(inlines, markdown), markdown, state);
  require(projection.isValid(), label);
  require(projection.displayText() == displayText,
          QStringLiteral("%1 display text mismatch: %2").arg(QString::fromUtf8(label), projection.displayText()));
  require(projection.visibleText() == visibleText,
          QStringLiteral("%1 visible text mismatch: %2").arg(QString::fromUtf8(label), projection.visibleText()));
  require(projection.spans().size() == expected.size(),
          QStringLiteral("%1 span count mismatch: %2").arg(QString::fromUtf8(label)).arg(projection.spans().size()));
  for (qsizetype i = 0; i < expected.size(); ++i) {
    requireSpanContract(projection.spans().at(i), expected.at(i), i, label);
  }
}

void requireProjectionRoundTrip(
    const QVector<InlineNode>& inlines,
    const QString& markdown,
    qsizetype cursorSourceOffset,
    const QString& visibleText,
    const QString& displayText,
    const char* label) {
  InlineProjectionState state;
  state.cursorSourceOffset = cursorSourceOffset;
  InlineProjection projection(withInlineTestRanges(inlines, markdown), markdown, state);
  require(projection.isValid(), label);
  require(projection.visibleText() == visibleText, QStringLiteral("%1 visible text mismatch: %2").arg(QString::fromUtf8(label), projection.visibleText()));
  require(projection.displayText() == displayText, QStringLiteral("%1 display text mismatch: %2").arg(QString::fromUtf8(label), projection.displayText()));

  for (qsizetype sourceOffset = 0; sourceOffset <= markdown.size(); ++sourceOffset) {
    qsizetype displayOffset = -1;
    require(projection.displayOffsetForSourceOffset(sourceOffset, displayOffset), "projection source->display should succeed");
    qsizetype mappedSourceOffset = -1;
    require(projection.sourceOffsetForDisplayOffset(displayOffset, mappedSourceOffset), "projection display->source should succeed");
    require(mappedSourceOffset >= 0 && mappedSourceOffset <= markdown.size(), "projection display round-trip should stay in source");
  }

  for (qsizetype visibleOffset = 0; visibleOffset <= visibleText.size(); ++visibleOffset) {
    qsizetype sourceOffset = -1;
    require(projection.sourceOffsetForVisibleOffset(visibleOffset, sourceOffset), "projection visible->source should succeed");
    qsizetype mappedVisibleOffset = -1;
    require(projection.visibleOffsetForSourceOffset(sourceOffset, mappedVisibleOffset), "projection source->visible should succeed");
    require(mappedVisibleOffset >= 0 && mappedVisibleOffset <= visibleText.size(), "projection visible round-trip should stay in visible text");
  }
}

void setInlineRanges(
    InlineNode& node,
    qsizetype sourceStart,
    qsizetype sourceEnd,
    qsizetype contentStart,
    qsizetype contentEnd) {
  InlineSourceRanges ranges;
  ranges.source = {sourceStart, sourceEnd};
  ranges.content = {contentStart, contentEnd};
  ranges.openMarker = {sourceStart, contentStart};
  ranges.closeMarker = {contentEnd, sourceEnd};
  node.setSourceRanges(ranges);
}

QString testMarkerForInline(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Code:
      return QStringLiteral("`");
    case InlineType::InlineMath:
      return QStringLiteral("$");
    case InlineType::Emphasis:
      return node.marker().isEmpty() ? QStringLiteral("*") : node.marker();
    case InlineType::Strong:
      return node.marker().isEmpty() ? QStringLiteral("**") : node.marker();
    case InlineType::Strikethrough:
      return QStringLiteral("~~");
    default:
      return {};
  }
}

void annotateInlineTestRanges(QVector<InlineNode>& inlines, const QString& markdown, qsizetype sourceStart, qsizetype sourceEnd) {
  qsizetype searchFrom = sourceStart;
  for (InlineNode& node : inlines) {
    const QString nodeMarkdown = InlineProjection::markdownForInlines({node});
    const qsizetype start = markdown.indexOf(nodeMarkdown, searchFrom);
    if (start < 0 || start + nodeMarkdown.size() > sourceEnd) {
      continue;
    }
    const qsizetype end = start + nodeMarkdown.size();
    const QString marker = testMarkerForInline(node);
    if (!marker.isEmpty() && end - start >= marker.size() * 2 &&
        (node.type() == InlineType::Code || node.type() == InlineType::InlineMath || node.type() == InlineType::Emphasis ||
         node.type() == InlineType::Strong || node.type() == InlineType::Strikethrough)) {
      setInlineRanges(node, start, end, start + marker.size(), end - marker.size());
      annotateInlineTestRanges(node.children(), markdown, start + marker.size(), end - marker.size());
    } else if (node.type() == InlineType::Link) {
      InlineSourceRanges ranges;
      ranges.source = {start, end};
      ranges.openMarker = {start, start + 1};
      const qsizetype labelEnd = markdown.indexOf(QLatin1Char(']'), start);
      ranges.content = labelEnd >= 0 && labelEnd <= end ? InlineRange{start + 1, labelEnd} : InlineRange{start + 1, start + 1};
      node.setSourceRanges(ranges);
      annotateInlineTestRanges(node.children(), markdown, ranges.content.start, ranges.content.end);
    } else if (node.type() == InlineType::Image) {
      InlineSourceRanges ranges;
      ranges.source = {start, end};
      ranges.openMarker = {start, start + 2};
      const qsizetype labelEnd = markdown.indexOf(QLatin1Char(']'), start);
      ranges.content = labelEnd >= 0 && labelEnd <= end ? InlineRange{start + 2, labelEnd} : InlineRange{start + 2, start + 2};
      node.setSourceRanges(ranges);
    } else {
      InlineSourceRanges ranges;
      ranges.source = {start, end};
      ranges.content = ranges.source;
      node.setSourceRanges(ranges);
    }
    searchFrom = end;
  }
}

QVector<InlineNode> withInlineTestRanges(QVector<InlineNode> inlines, const QString& markdown) {
  annotateInlineTestRanges(inlines, markdown, 0, markdown.size());
  return inlines;
}

const BlockLayout* requireViewBlock(EditorView& view, NodeId blockId, const QString& label);
const InlineLayout* requireViewInlineLayout(EditorView& view, NodeId blockId, const QString& label);
CursorPosition inlineCursor(NodeId blockId, qsizetype textOffset, qsizetype sourceOffset);

HitTestResult hitAtTextOffset(EditorView& view, NodeId blockId, qsizetype textOffset, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, label);
  const QRectF cursor = inlineLayout->cursorRect(textOffset);
  require(!cursor.isEmpty(), label + QStringLiteral(" cursor rect should exist"));
  return view.hitTest(blockRect.topLeft() + QPointF(cursor.left(), cursor.center().y()));
}

HitTestResult hitAtSourceOffset(EditorView& view, NodeId blockId, qsizetype sourceOffset, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  const InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, label);
  const QRectF cursor = inlineLayout->cursorRectForSourceOffset(sourceOffset);
  require(!cursor.isEmpty(), label + QStringLiteral(" source cursor rect should exist"));
  return view.hitTest(blockRect.topLeft() + QPointF(cursor.left(), cursor.center().y()));
}

SelectionRange inlineSelection(NodeId blockId, qsizetype anchorOffset, qsizetype focusOffset) {
  SelectionRange selection;
  selection.anchor = inlineCursor(blockId, anchorOffset, anchorOffset);
  selection.focus = inlineCursor(blockId, focusOffset, focusOffset);
  return selection;
}

struct CursorLineRange {
  qsizetype start = 0;
  qsizetype end = 0;
  qreal y = 0;
};

QVector<CursorLineRange> cursorLineRanges(const InlineLayout& layout) {
  QVector<CursorLineRange> ranges;
  const qsizetype length = layout.plainText().size();
  for (qsizetype offset = 0; offset <= length; ++offset) {
    const QRectF cursor = layout.cursorRect(offset);
    if (cursor.isEmpty()) {
      continue;
    }
    const qreal y = cursor.center().y();
    if (ranges.isEmpty() || qAbs(ranges.last().y - y) > 0.5) {
      CursorLineRange range;
      range.start = offset;
      range.end = offset;
      range.y = y;
      ranges.push_back(range);
    } else {
      ranges.last().end = offset;
    }
  }
  return ranges;
}

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

void testEditorViewHitTestActivatesInlineSourceEditing() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());
  const QRectF blockRect = view.nodeRect(blockAt(session, 0)->id());
  require(!blockRect.isEmpty(), "view should layout inline paragraph");
  const InlineLayout* inlineLayout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(inlineLayout != nullptr, "view hit test should find inline layout");

  const QPointF documentPos = blockRect.topLeft() + inlineLayout->cursorRectForSourceOffset(11).center();
  HitTestResult hit = view.hitTest(documentPos);
  require(hit.isValid() && hit.zone == HitTestResult::Zone::Text, "view hit test should return text hit");
  require(hit.sourceOffset == 11, "view hit test source offset mismatch");

  controller.activateHit(hit);
  require(controller.selection().cursorPosition().text.sourceOffset == 11, "view hit should resolve source offset");
  require(controller.inputController().insertText(QStringLiteral("X")), "typing after view hit should edit inline");
  require(session.markdownText() == QStringLiteral("before **boXld** after"), "view hit inline insert mismatch");
}

void testEditorViewInlineProjectionStateChanges() {
  DocumentSession session;
  EditorView view;
  view.resize(900, 500);
  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());

  MarkdownNode* block = blockAt(session, 0);
  const QRectF collapsedRect = view.nodeRect(block->id());
  const InlineLayout* collapsedLayout = view.blockAtViewportPos(collapsedRect.center())->inlineLayout();
  require(collapsedLayout != nullptr, "collapsed inline layout should exist");
  const QRectF collapsedCursor = collapsedLayout->cursorRectForSourceOffset(9);

  CursorPosition inside;
  inside.blockId = block->id();
  inside.text.nodeId = block->id();
  inside.text.textOffset = 1;
  inside.text.sourceOffset = 9;
  view.setCursorPosition(inside);
  const QRectF expandedRect = view.nodeRect(block->id());
  const InlineLayout* expandedLayout = view.blockAtViewportPos(expandedRect.center())->inlineLayout();
  require(expandedLayout != nullptr, "expanded inline layout should exist");
  const QRectF expandedCursor = expandedLayout->cursorRectForSourceOffset(9);
  require(expandedCursor.left() != collapsedCursor.left(), "cursor entering inline should expand marker layout");

  const QPointF expandedDocumentPos = expandedRect.topLeft() + expandedCursor.center();
  const HitTestResult expandedHit = view.hitTest(expandedDocumentPos);
  require(expandedHit.isValid() && expandedHit.sourceOffset == 9, "expanded inline hit-test should round-trip source offset");

  CursorPosition outside;
  outside.blockId = block->id();
  outside.text.nodeId = block->id();
  outside.text.textOffset = 0;
  outside.text.sourceOffset = 0;
  view.setCursorPosition(outside);
  const QRectF recollapsedRect = view.nodeRect(block->id());
  const InlineLayout* recollapsedLayout = view.blockAtViewportPos(recollapsedRect.center())->inlineLayout();
  require(recollapsedLayout != nullptr, "recollapsed inline layout should exist");
  require(recollapsedLayout->cursorRectForSourceOffset(9).left() == collapsedCursor.left(), "cursor leaving inline should collapse marker layout");

  SelectionRange selection;
  selection.anchor = inside;
  selection.focus = inside;
  selection.focus.text.textOffset = 3;
  selection.focus.text.sourceOffset = 11;
  view.setSelectionRange(selection);
  const QRectF selectedRect = view.nodeRect(block->id());
  const InlineLayout* selectedLayout = view.blockAtViewportPos(selectedRect.center())->inlineLayout();
  require(selectedLayout != nullptr, "selection inline layout should exist");
  require(selectedLayout->cursorRectForSourceOffset(9).left() != collapsedCursor.left(), "selection touching inline should expand marker layout");
}

void testEditorViewInlineMarkerSourceSelection() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **bold** after"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);
  const NodeId blockId = block->id();

  CursorPosition inside = inlineCursor(blockId, QStringLiteral("before b").size(), QStringLiteral("before **b").size());
  view.setCursorPosition(inside);
  const QRectF expandedBlockRect = view.nodeRect(blockId);
  const InlineLayout* expandedLayout = requireViewInlineLayout(view, blockId, QStringLiteral("marker source"));

  const QRectF betweenStarsCursor = expandedLayout->cursorRectForSourceOffset(QStringLiteral("before *").size());
  require(!betweenStarsCursor.isEmpty(), "cursor between strong opener stars should exist");
  const HitTestResult betweenStarsHit = view.hitTest(expandedBlockRect.topLeft() + betweenStarsCursor.center());
  require(betweenStarsHit.isValid(), "hit between strong opener stars should be valid");
  require(betweenStarsHit.sourceOffset == QStringLiteral("before *").size(), "hit between strong opener stars should keep source offset");

  SelectionRange markerSelection;
  markerSelection.anchor = inlineCursor(blockId, QStringLiteral("before ").size(), QStringLiteral("before ").size());
  markerSelection.focus = inlineCursor(blockId, QStringLiteral("before ").size(), QStringLiteral("before **").size());
  view.setSelectionRange(markerSelection);
  const BlockLayout* selectedBlock = requireViewBlock(view, blockId, QStringLiteral("marker source selection"));
  const QVector<QRectF> markerRects = selectedBlock->selectionRects(markerSelection, RenderTheme::typoraLike());
  require(!markerRects.isEmpty(), "strong opener marker source selection should draw");
  qreal markerWidth = 0;
  for (const QRectF& rect : markerRects) {
    markerWidth += rect.width();
  }
  require(markerWidth > 2.0, "strong opener marker source selection should have visible width");

  const QRectF currentBlockRect = view.nodeRect(blockId);
  const InlineLayout* currentLayout = requireViewInlineLayout(view, blockId, QStringLiteral("marker drag start"));
  const QPointF dragStart = currentBlockRect.topLeft() + currentLayout->cursorRectForSourceOffset(QStringLiteral("before ").size()).center();
  QMouseEvent press(
      QEvent::MouseButtonPress,
      dragStart,
      QPointF(dragStart),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);

  const QRectF dragBlockRect = view.nodeRect(blockId);
  const InlineLayout* dragLayout = requireViewInlineLayout(view, blockId, QStringLiteral("marker drag"));
  const QPointF dragEnd = dragBlockRect.topLeft() + dragLayout->cursorRectForSourceOffset(QStringLiteral("before **bold").size()).center();
  QMouseEvent move(
      QEvent::MouseMove,
      dragEnd,
      QPointF(dragEnd),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &move);
  require(!controller.selection().selection().isCollapsed(), "dragging from marker into content should create source selection");
  const qsizetype expectedAnchorSource = QStringLiteral("before ").size();
  const qsizetype expectedFocusSource = QStringLiteral("before **bold").size();
  require(controller.selection().selection().anchor.text.sourceOffset == expectedAnchorSource,
          "marker drag anchor should stay at opener source offset");
  require(controller.selection().selection().focus.text.sourceOffset == expectedFocusSource,
          "marker drag focus should stay at content source offset");
}

void testEditorViewInlineClickDoesNotSelectAfterMarkerExpansion() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("before **xyz** after"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);
  const QRectF blockRect = view.nodeRect(block->id());
  const InlineLayout* collapsedLayout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(collapsedLayout != nullptr, "inline click test collapsed layout should exist");

  const QPointF clickPos = blockRect.topLeft() + collapsedLayout->cursorRectForSourceOffset(11).center();
  QMouseEvent press(
      QEvent::MouseButtonPress,
      clickPos,
      QPointF(clickPos),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);
  require(controller.selection().hasCursor(), "inline click press should activate cursor");
  require(controller.selection().selection().isCollapsed(), "inline click press should keep collapsed selection");

  QMouseEvent release(
      QEvent::MouseButtonRelease,
      clickPos,
      QPointF(clickPos),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &release);
  require(controller.selection().selection().isCollapsed(), "inline click release should not create selection after marker expansion");
  require(controller.selection().cursorPosition().text.sourceOffset == 11, "inline click release should keep original source cursor");
}

void testEditorViewDragSelectionContinuesAcrossMoves() {
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  view.resize(900, 500);

  session.setMarkdownText(QStringLiteral("abcdefghijklmnopqrstuvwxyz"), false);
  view.setDocument(session.document());
  MarkdownNode* block = blockAt(session, 0);
  const QRectF blockRect = view.nodeRect(block->id());
  const InlineLayout* layout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(layout != nullptr, "drag selection test inline layout should exist");

  const QPointF startPos = blockRect.topLeft() + layout->cursorRectForSourceOffset(0).center();
  const QPointF firstMovePos = blockRect.topLeft() + layout->cursorRectForSourceOffset(10).center();
  const QPointF secondMovePos = blockRect.topLeft() + layout->cursorRectForSourceOffset(20).center();

  QMouseEvent press(
      QEvent::MouseButtonPress,
      startPos,
      QPointF(startPos),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);

  QMouseEvent stationaryMove(
      QEvent::MouseMove,
      startPos,
      QPointF(startPos),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &stationaryMove);
  require(controller.selection().selection().isCollapsed(), "stationary first drag move should keep collapsed cursor");

  QMouseEvent firstMove(
      QEvent::MouseMove,
      firstMovePos,
      QPointF(firstMovePos),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &firstMove);
  require(!controller.selection().selection().isCollapsed(), "first drag move should create a selection");
  require(controller.selection().selection().focus.text.sourceOffset == 10, "first drag move focus offset mismatch");

  QMouseEvent secondMove(
      QEvent::MouseMove,
      secondMovePos,
      QPointF(secondMovePos),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &secondMove);
  require(controller.selection().selection().focus.text.sourceOffset == 20, "second drag move should keep extending selection");

  QMouseEvent release(
      QEvent::MouseButtonRelease,
      secondMovePos,
      QPointF(secondMovePos),
      Qt::LeftButton,
      Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &release);
  require(controller.selection().selection().focus.text.sourceOffset == 20, "drag release focus offset mismatch");
}

void testEditorViewVerticalDragSelectionHitsWrappedLine() {
  const QString markdown = QStringLiteral(
      "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau");
  DocumentSession session;
  EditorView view;
  EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText(markdown, false);

  const auto lineStartCursorsForWidth = [&session, &view](int width, MarkdownNode* block) {
    view.resize(width, 500);
    view.setDocument(session.document());
    const QRectF blockRect = view.nodeRect(block->id());
    const BlockLayout* blockLayout = view.blockAtViewportPos(blockRect.center());
    const InlineLayout* layout = blockLayout ? blockLayout->inlineLayout() : nullptr;
    QVector<QRectF> lineStartCursors;
    if (!layout) {
      return lineStartCursors;
    }
    for (qsizetype offset = 0; offset <= layout->plainText().size(); ++offset) {
      const QRectF cursor = layout->cursorRect(offset);
      require(!cursor.isEmpty(), QStringLiteral("vertical drag cursor %1 should exist").arg(offset));
      if (lineStartCursors.isEmpty() || cursor.center().y() > lineStartCursors.last().center().y() + 0.5) {
        lineStartCursors.push_back(cursor);
        if (lineStartCursors.size() == 2) {
          break;
        }
      }
    }
    return lineStartCursors;
  };

  MarkdownNode* block = blockAt(session, 0);
  QVector<QRectF> lineStartCursors;
  for (int width : {220, 180, 150, 130, 110}) {
    lineStartCursors = lineStartCursorsForWidth(width, block);
    if (lineStartCursors.size() >= 2) {
      break;
    }
  }
  view.setDocument(session.document());
  const QRectF blockRect = view.nodeRect(block->id());
  const InlineLayout* layout = view.blockAtViewportPos(blockRect.center())->inlineLayout();
  require(layout != nullptr, "vertical drag test inline layout should exist");

  qsizetype firstLineOffset = -1;
  lineStartCursors.clear();
  for (qsizetype offset = 0; offset <= layout->plainText().size(); ++offset) {
    const QRectF cursor = layout->cursorRect(offset);
    require(!cursor.isEmpty(), QStringLiteral("vertical drag cursor %1 should exist").arg(offset));
    if (lineStartCursors.isEmpty() || cursor.center().y() > lineStartCursors.last().center().y() + 0.5) {
      lineStartCursors.push_back(cursor);
      if (lineStartCursors.size() == 1) {
        firstLineOffset = offset;
      } else if (lineStartCursors.size() == 2) {
        break;
      }
    }
  }
  require(firstLineOffset == 0, "vertical drag first line should start at offset 0");
  require(lineStartCursors.size() >= 2, "vertical drag fixture should wrap to a second line");

  const qreal dragX = lineStartCursors.at(0).center().x() + 40.0;
  const QPointF startPos(blockRect.left() + dragX, blockRect.top() + lineStartCursors.at(0).center().y());
  const QPointF secondLineSameX(blockRect.left() + dragX, blockRect.top() + lineStartCursors.at(1).center().y());

  QMouseEvent press(
      QEvent::MouseButtonPress,
      startPos,
      QPointF(startPos),
      Qt::LeftButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &press);

  QMouseEvent move(
      QEvent::MouseMove,
      secondLineSameX,
      QPointF(secondLineSameX),
      Qt::NoButton,
      Qt::LeftButton,
      Qt::NoModifier);
  QApplication::sendEvent(view.viewport(), &move);

  const QRectF focusCursor = view.hitTest(secondLineSameX).cursorRect;
  require(qAbs(focusCursor.center().y() - (blockRect.top() + lineStartCursors.at(1).center().y())) < 1.0,
          QStringLiteral("vertical drag hit should resolve to the second visual line"));
  require(!controller.selection().selection().isCollapsed(), "vertical drag should create a selection");
  require(controller.selection().selection().focus.text.sourceOffset == view.hitTest(secondLineSameX).sourceOffset,
          QStringLiteral("vertical drag focus should follow the second visual line hit"));
  const BlockLayout* selectedBlock = view.blockAtViewportPos(blockRect.center());
  require(selectedBlock != nullptr, "vertical drag wrapped paragraph block should stay visible");
  const QVector<QRectF> selectionRects = selectedBlock->selectionRects(controller.selection().selection(), RenderTheme::typoraLike());
  require(!selectionRects.isEmpty(), "vertical drag should produce visible selection rects without horizontal pre-drag");
  bool hasSecondLineRect = false;
  for (const QRectF& rect : selectionRects) {
    if (qAbs(rect.center().y() - (blockRect.top() + lineStartCursors.at(1).center().y())) < 1.0) {
      hasSecondLineRect = true;
      break;
    }
  }
  require(hasSecondLineRect, "vertical drag selection rects should include the second visual line");
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

const BlockLayout* requireViewBlock(EditorView& view, NodeId blockId, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  require(!blockRect.isEmpty(), label + QStringLiteral(" block rect should exist"));
  const BlockLayout* block = view.blockAtViewportPos(blockRect.center());
  require(block != nullptr, label + QStringLiteral(" block layout should exist"));
  return block;
}

const InlineLayout* requireViewInlineLayout(EditorView& view, NodeId blockId, const QString& label) {
  const BlockLayout* block = requireViewBlock(view, blockId, label);
  require(block->inlineLayout() != nullptr, label + QStringLiteral(" inline layout should exist"));
  return block->inlineLayout();
}

CursorPosition inlineCursor(NodeId blockId, qsizetype textOffset, qsizetype sourceOffset) {
  CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = textOffset;
  cursor.text.sourceOffset = sourceOffset;
  return cursor;
}

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
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("plain inline"))->selectionRects(selection, RenderTheme::typoraLike());
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
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("styled inline"))->selectionRects(selection, RenderTheme::typoraLike());
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
  const RenderTheme theme = RenderTheme::typoraLike();
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
  const QVector<QRectF> rects = requireViewBlock(view, blockId, QStringLiteral("wrapped inline"))->selectionRects(selection, RenderTheme::typoraLike());
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

  const QMarginsF padding = RenderTheme::typoraLike().tableCellPadding();
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
  const QMarginsF padding = RenderTheme::typoraLike().tableCellPadding();
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
  RenderTheme theme = RenderTheme::typoraLike();
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

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testInlineProjectionMarkerSourcePositions);
  RUN_TEST(testInlineProjectionUsesParserSourceRangesForRepeatedUnicodeMarkers);
  RUN_TEST(testInlineProjectionUsesParserSourceRangesForRepeatedNonCanonicalMarkdown);
  RUN_TEST(testInlineProjectionExpandsParserContentRangesForDelimitedInlines);
  RUN_TEST(testInlineProjectionAutolinkUsesFullSourceRangeAndLabel);
  RUN_TEST(testInlineProjectionEntitiesAndMarkdownLinksUseParserRanges);
  RUN_TEST(testInlineProjectionEntityDisplayAfterEdit);
  RUN_TEST(testInlineProjectionEntityRevealsRawSyntaxWhenActive);
  RUN_TEST(testInlineProjectionKeepsValidEntitiesDecodedAfterBrokenEntityEdit);
  RUN_TEST(testInlineProjectionEntityDisplayAfterEditMultiParagraph);
  RUN_TEST(testInlineMathOpeningMarkerHitEntersContent);
  RUN_TEST(testInlineProjectionForwardBiasAtInlineEnd);
  RUN_TEST(testInlineProjectionSpanContracts);
  RUN_TEST(testInlineProjectionMappingMatrix);
  RUN_TEST(testHorizontalNavigationEntersInlineMarkers);
  RUN_TEST(testTextHitActivationAddsSourceOffsetForInlineEditing);
  RUN_TEST(testDefinitionPlaceholderHitKeepsCursorInSlot);
  RUN_TEST(testEmptyLinkDefinitionTitlePlaceholderOnlyWhenFocused);
  RUN_TEST(testDefinitionBoundaryHitPlacesCursorAtSourceEdges);
  RUN_TEST(testNoTitleDefinitionRightEdgeHasNoInvisibleQuote);
  RUN_TEST(testDefinitionTokenModelDrivesSyntaxAndSlots);
  RUN_TEST(testDefinitionSyntaxHitChoosesNearestEditableSlot);
  RUN_TEST(testFootnoteDefinitionUsesTokenPath);
  RUN_TEST(testFocusedEmptyFootnoteDefinitionSlotSuppressesPlaceholder);
  RUN_TEST(testEditorViewHitTestActivatesInlineSourceEditing);
  RUN_TEST(testEditorViewInlineProjectionStateChanges);
  RUN_TEST(testEditorViewInlineMarkerSourceSelection);
  RUN_TEST(testEditorViewInlineClickDoesNotSelectAfterMarkerExpansion);
  RUN_TEST(testEditorViewDragSelectionContinuesAcrossMoves);
  RUN_TEST(testEditorViewVerticalDragSelectionHitsWrappedLine);
  RUN_TEST(testEditorViewInlineLayoutSmoke);
  RUN_TEST(testEditorViewPlainInlineLayout);
  RUN_TEST(testEditorViewStyledInlineLayout);
  RUN_TEST(testEditorViewListInlineMathHitEditing);
  RUN_TEST(testEditorViewWrappedInlineLayout);
  RUN_TEST(testEditorViewTableCellInlineLayout);
  RUN_TEST(testEditorViewTableCellInlineCodeEndHit);
  RUN_TEST(testTableCellRichInlineSelectionDeleteAndCopyUseSourceOffsets);
  RUN_TEST(testMixedInlineParagraphHitEditingBeforeAutolink);
  RUN_TEST(testInlineSelectionRects);
#undef RUN_TEST
  QApplication::clipboard()->clear();
  return 0;
}
