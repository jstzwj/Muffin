#pragma once

#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "document/SourceRangeUtil.h"
#include "edit/UndoStack.h"
#include "editor/BrushQueue.h"
#include "editor/EditorContext.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/InputController.h"
#include "editor/SelectionController.h"
#include "projection/InlineProjection.h"
#include "projection/SelectionSerializer.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include "../TestUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>

#include <iostream>

using namespace muffin;

inline muffin::MarkdownNode* blockAt(const muffin::DocumentSession& session, qsizetype index) {
  const auto& children = session.document().root().children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "block index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

inline muffin::MarkdownNode* childAt(muffin::MarkdownNode* node, qsizetype index) {
  require(node != nullptr, "parent node should exist");
  const auto& children = node->children();
  require(index >= 0 && index < static_cast<qsizetype>(children.size()), "child index out of range");
  return children.at(static_cast<size_t>(index)).get();
}

inline muffin::MarkdownNode* listItemAt(const muffin::DocumentSession& session, qsizetype listIndex, qsizetype itemIndex) {
  return childAt(blockAt(session, listIndex), itemIndex);
}

inline void setCursor(muffin::SelectionController& selection, muffin::MarkdownNode* block, qsizetype offset) {
  muffin::CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = offset;
  selection.setCursorPosition(cursor);
}

inline void setSourceCursor(muffin::SelectionController& selection, muffin::MarkdownNode* block, qsizetype visibleOffset, qsizetype sourceOffset) {
  muffin::CursorPosition cursor;
  cursor.blockId = block->id();
  cursor.text.nodeId = block->id();
  cursor.text.textOffset = visibleOffset;
  cursor.text.sourceOffset = sourceOffset;
  selection.setCursorPosition(cursor);
}

inline bool pressKey(muffin::InputController& input, QObject* target, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  return input.eventFilter(target, &event);
}

inline void wireInput(
    muffin::InputController& input,
    muffin::DocumentSession& session,
    muffin::SelectionController& selection,
    muffin::UndoStack& undoStack,
    muffin::BrushQueue& brushQueue,
    muffin::EditorView* view = nullptr,
    QHash<int, muffin::LiteralBlockController*> literalEditors = {}) {
  input.setContext({&session, &selection, &undoStack, &brushQueue, view, literalEditors});
}

inline void setSelection(muffin::SelectionController& selection, muffin::MarkdownNode* block, qsizetype anchor, qsizetype focus) {
  muffin::SelectionRange range;
  range.anchor.blockId = block->id();
  range.anchor.text.nodeId = block->id();
  range.anchor.text.textOffset = anchor;
  range.focus.blockId = block->id();
  range.focus.text.nodeId = block->id();
  range.focus.text.textOffset = focus;
  selection.setSelection(range);
}

inline void setSourceSelection(
    muffin::SelectionController& selection,
    muffin::MarkdownNode* block,
    muffin::MarkdownNode* textNode,
    qsizetype anchorTextOffset,
    qsizetype anchorSourceOffset,
    qsizetype focusTextOffset,
    qsizetype focusSourceOffset) {
  muffin::SelectionRange range;
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

inline QString selectedMarkdown(const muffin::DocumentSession& session, const muffin::SelectionController& selection) {
  return muffin::SelectionSerializer().exportMarkdown(session.document(), selection.selection());
}

inline QString selectedPlainText(const muffin::DocumentSession& session, const muffin::SelectionController& selection) {
  return muffin::SelectionSerializer().exportPlainText(session.document(), selection.selection());
}

struct ExpectedProjectionSpan {
  muffin::InlineType type = muffin::InlineType::Unknown;
  muffin::InlineSpanKind kind = muffin::InlineSpanKind::Text;
  qsizetype sourceStart = 0;
  qsizetype sourceEnd = 0;
  qsizetype contentSourceStart = 0;
  qsizetype contentSourceEnd = 0;
  qsizetype displayStart = 0;
  qsizetype displayEnd = 0;
  qsizetype visibleStart = 0;
  qsizetype visibleEnd = 0;
};

QVector<muffin::InlineNode> withInlineTestRanges(QVector<muffin::InlineNode> inlines, const QString& markdown);

inline void requireSpanContract(const muffin::InlineProjectionSpan& span, const ExpectedProjectionSpan& expected, qsizetype index, const char* label) {
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

inline void requireProjectionSpans(
    const QVector<muffin::InlineNode>& inlines,
    const QString& markdown,
    muffin::InlineProjectionState state,
    const QVector<ExpectedProjectionSpan>& expected,
    const QString& displayText,
    const QString& visibleText,
    const char* label) {
  muffin::InlineProjection projection(withInlineTestRanges(inlines, markdown), markdown, state);
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

inline void requireProjectionRoundTrip(
    const QVector<muffin::InlineNode>& inlines,
    const QString& markdown,
    qsizetype cursorSourceOffset,
    const QString& visibleText,
    const QString& displayText,
    const char* label) {
  muffin::InlineProjectionState state;
  state.cursorSourceOffset = cursorSourceOffset;
  muffin::InlineProjection projection(withInlineTestRanges(inlines, markdown), markdown, state);
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

inline void setInlineRanges(
    muffin::InlineNode& node,
    qsizetype sourceStart,
    qsizetype sourceEnd,
    qsizetype contentStart,
    qsizetype contentEnd) {
  muffin::InlineSourceRanges ranges;
  ranges.source = {sourceStart, sourceEnd};
  ranges.content = {contentStart, contentEnd};
  ranges.openMarker = {sourceStart, contentStart};
  ranges.closeMarker = {contentEnd, sourceEnd};
  node.setSourceRanges(ranges);
}

inline QString testMarkerForInline(const muffin::InlineNode& node) {
  switch (node.type()) {
    case muffin::InlineType::Code:
      return QStringLiteral("`");
    case muffin::InlineType::InlineMath:
      return QStringLiteral("$");
    case muffin::InlineType::Emphasis:
      return node.marker().isEmpty() ? QStringLiteral("*") : node.marker();
    case muffin::InlineType::Strong:
      return node.marker().isEmpty() ? QStringLiteral("**") : node.marker();
    case muffin::InlineType::Strikethrough:
      return QStringLiteral("~~");
    default:
      return {};
  }
}

inline void annotateInlineTestRanges(QVector<muffin::InlineNode>& inlines, const QString& markdown, qsizetype sourceStart, qsizetype sourceEnd) {
  qsizetype searchFrom = sourceStart;
  for (muffin::InlineNode& node : inlines) {
    const QString nodeMarkdown = muffin::InlineProjection::markdownForInlines({node});
    const qsizetype start = markdown.indexOf(nodeMarkdown, searchFrom);
    if (start < 0 || start + nodeMarkdown.size() > sourceEnd) {
      continue;
    }
    const qsizetype end = start + nodeMarkdown.size();
    const QString marker = testMarkerForInline(node);
    if (!marker.isEmpty() && end - start >= marker.size() * 2 &&
        (node.type() == muffin::InlineType::Code || node.type() == muffin::InlineType::InlineMath || node.type() == muffin::InlineType::Emphasis ||
         node.type() == muffin::InlineType::Strong || node.type() == muffin::InlineType::Strikethrough)) {
      setInlineRanges(node, start, end, start + marker.size(), end - marker.size());
      annotateInlineTestRanges(node.children(), markdown, start + marker.size(), end - marker.size());
    } else if (node.type() == muffin::InlineType::Link) {
      muffin::InlineSourceRanges ranges;
      ranges.source = {start, end};
      ranges.openMarker = {start, start + 1};
      const qsizetype labelEnd = markdown.indexOf(QLatin1Char(']'), start);
      ranges.content = labelEnd >= 0 && labelEnd <= end ? muffin::InlineRange{start + 1, labelEnd} : muffin::InlineRange{start + 1, start + 1};
      node.setSourceRanges(ranges);
      annotateInlineTestRanges(node.children(), markdown, ranges.content.start, ranges.content.end);
    } else if (node.type() == muffin::InlineType::Image) {
      muffin::InlineSourceRanges ranges;
      ranges.source = {start, end};
      ranges.openMarker = {start, start + 2};
      const qsizetype labelEnd = markdown.indexOf(QLatin1Char(']'), start);
      ranges.content = labelEnd >= 0 && labelEnd <= end ? muffin::InlineRange{start + 2, labelEnd} : muffin::InlineRange{start + 2, start + 2};
      node.setSourceRanges(ranges);
    } else {
      muffin::InlineSourceRanges ranges;
      ranges.source = {start, end};
      ranges.content = ranges.source;
      node.setSourceRanges(ranges);
    }
    searchFrom = end;
  }
}

inline QVector<muffin::InlineNode> withInlineTestRanges(QVector<muffin::InlineNode> inlines, const QString& markdown) {
  annotateInlineTestRanges(inlines, markdown, 0, markdown.size());
  return inlines;
}

inline const muffin::BlockLayout* requireViewBlock(muffin::EditorView& view, muffin::NodeId blockId, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  require(!blockRect.isEmpty(), label + QStringLiteral(" block rect should exist"));
  const muffin::BlockLayout* block = view.blockAtViewportPos(blockRect.center());
  require(block != nullptr, label + QStringLiteral(" block layout should exist"));
  return block;
}

inline const muffin::InlineLayout* requireViewInlineLayout(muffin::EditorView& view, muffin::NodeId blockId, const QString& label) {
  const muffin::BlockLayout* block = requireViewBlock(view, blockId, label);
  require(block->inlineLayout() != nullptr, label + QStringLiteral(" inline layout should exist"));
  return block->inlineLayout();
}

inline muffin::CursorPosition inlineCursor(muffin::NodeId blockId, qsizetype textOffset, qsizetype sourceOffset) {
  muffin::CursorPosition cursor;
  cursor.blockId = blockId;
  cursor.text.nodeId = blockId;
  cursor.text.textOffset = textOffset;
  cursor.text.sourceOffset = sourceOffset;
  return cursor;
}

inline muffin::HitTestResult hitAtTextOffset(muffin::EditorView& view, muffin::NodeId blockId, qsizetype textOffset, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  const muffin::InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, label);
  const QRectF cursor = inlineLayout->cursorRect(textOffset);
  require(!cursor.isEmpty(), label + QStringLiteral(" cursor rect should exist"));
  return view.hitTest(blockRect.topLeft() + QPointF(cursor.left(), cursor.center().y()));
}

inline muffin::HitTestResult hitAtSourceOffset(muffin::EditorView& view, muffin::NodeId blockId, qsizetype sourceOffset, const QString& label) {
  const QRectF blockRect = view.nodeRect(blockId);
  const muffin::InlineLayout* inlineLayout = requireViewInlineLayout(view, blockId, label);
  const QRectF cursor = inlineLayout->cursorRectForSourceOffset(sourceOffset);
  require(!cursor.isEmpty(), label + QStringLiteral(" source cursor rect should exist"));
  return view.hitTest(blockRect.topLeft() + QPointF(cursor.left(), cursor.center().y()));
}

inline muffin::SelectionRange inlineSelection(muffin::NodeId blockId, qsizetype anchorOffset, qsizetype focusOffset) {
  muffin::SelectionRange selection;
  selection.anchor = inlineCursor(blockId, anchorOffset, anchorOffset);
  selection.focus = inlineCursor(blockId, focusOffset, focusOffset);
  return selection;
}

struct CursorLineRange {
  qsizetype start = 0;
  qsizetype end = 0;
  qreal y = 0;
};

inline QVector<CursorLineRange> cursorLineRanges(const muffin::InlineLayout& layout) {
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
