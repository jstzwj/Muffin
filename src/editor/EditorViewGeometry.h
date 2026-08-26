#pragma once

// Pure geometry / hit-test helpers split out of EditorView. These are stateless
// transforms of (layout, theme, cursor/viewport state) into rects and hit
// results; EditorView holds the mutable state and calls into here. Keeping them
// free functions (rather than a state-holding class) matches their nature: none
// of them write member state, so there is nothing for an object to encapsulate.

#include "editor/CursorPosition.h"  // HitTestResult, CursorPosition, SelectionRange, NodeId
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QFont>
#include <QPair>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QVector>

namespace muffin {
namespace editor_geometry {

// Cursor rect within a literal (code/front-matter/math/html source) block.
// Single-line variant: no wrapping, advances by horizontalAdvance from origin.
QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin);

// Wrapped-or-not variant for code-style blocks. With `wrap` it soft-wraps each
// physical line at the given width; without, each physical line is one visual
// line laid out at its natural advance (so a long line in a wrap-OFF code fence
// keeps the caret on that single row instead of dropping to a phantom second
// one). Code fences honour markdown/codeBlockWrap; math/html/front-matter wrap.
QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin,
                                  qreal width, qreal lineHeight, bool wrap);

// Zones a click/selection can land on (text-bearing content).
bool isSelectableZone(HitTestResult::Zone zone);
// Zones a drag (or shift-click) can start or land on — additionally includes the
// virtual trailing paragraph so dragging from the document end selects back
// through the preceding block content.
bool isDragSelectableZone(HitTestResult::Zone zone);

// Top-left where a table cell's text is painted, honouring per-cell alignment.
QPointF tableCellTextOrigin(const BlockLayout::TableCellLayout& cell, const RenderTheme& theme);

// Number of selectable offsets a block exposes (inline plain-text length, or
// literal length for code/math/html/front-matter, or 1 for a table cell range,
// or the marker→end span for link/footnote definitions). The virtual trailing
// paragraph resolves its END offset through this.
qsizetype selectableLength(const BlockLayout* block);

// Map a document-coordinate rect into a viewport dirty rect (clipped to the
// viewport, expanded by a small margin) for partial repaints.
QRect viewportUpdateRect(QRectF documentRect, qreal scrollY, const QSize& viewportSize);
// Union a document-coordinate rect into an existing viewport dirty region; used
// to repaint the caret even when it lies outside refreshed block rects.
QRect uniteDocumentRectDirty(QRect dirty, QRectF documentRect, qreal scrollY, const QSize& viewportSize);

// Fold the old/new/shifted rects from a rebuild result into the dirty region.
// Templated because BlockRebuildResult and RangeRebuildResult expose the same
// {oldRect,newRect,shiftedRect} shape but are distinct types.
template <typename RebuildResult>
void addRebuildDirtyRect(QRect& dirty, const RebuildResult& result, QRectF documentViewport, qreal scrollY,
                         const QSize& viewportSize) {
  dirty = dirty.united(viewportUpdateRect(result.oldRect.united(result.newRect), scrollY, viewportSize));
  if (!result.shiftedRect.isEmpty()) {
    dirty = dirty.united(viewportUpdateRect(result.shiftedRect.intersected(documentViewport), scrollY, viewportSize));
  }
}

// Word boundaries (ICU-backed) around an offset, for double-click selection.
QPair<qsizetype, qsizetype> wordRangeAtOffset(const QString& text, qsizetype offset);

// Resolve the caret HitTestResult for a logical cursor position against a laid-out
// document. Reproduces the virtual trailing-paragraph caret and per-zone cursor
// geometry so a CursorPosition survives layout rebuilds (resize/theme/refresh)
// without snapping back inside the last block.
HitTestResult hitForCursorPosition(DocumentLayout& layout, const RenderTheme& theme, CursorPosition position);

// Top-level + nested blocks between two block ids, in document order (geometric
// top-to-bottom). Used to paint multi-block selection spans.
QVector<const BlockLayout*> blocksBetween(const DocumentLayout& layout, NodeId first, NodeId last);
// True if `first` precedes (or equals) `second` in document order.
bool blockComesBefore(const DocumentLayout& layout, NodeId first, NodeId second);

// Visit every block covered by a MULTI-block selection in document order, with the per-block
// [start, end] selectable offsets paintSelection draws: fully-covered middle blocks get
// [0, selectableLength], endpoint blocks clamp to their anchor/focus text offsets, and tables
// collapse to their whole-block [0, 1]. Single source of truth for paintSelection and
// selectionContainsViewportPoint (the right-click in-selection test) so the two can never drift.
// Single-block selections are NOT handled here — paint uses the recursive selectionRects against
// the endpoint block instead.
void forEachMultiBlockSelectionBlock(
    const DocumentLayout& layout,
    const SelectionRange& selection,
    const std::function<void(const BlockLayout* block, qsizetype start, qsizetype end)>& visit);

}  // namespace editor_geometry
}  // namespace muffin
