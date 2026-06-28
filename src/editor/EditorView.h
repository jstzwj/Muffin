#pragma once

#include "document/TopLevelRangeChange.h"
#include "editor/HtmlBlockHoverController.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QPointer>
#include <QRectF>
#include <QHash>

#include <memory>

class QPropertyAnimation;
class QTimer;
class QWidget;

namespace muffin {

class CodeFenceScrollController;
class CodeLanguageEditor;
class MarkdownDocument;
class TableToolbar;

class EditorView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit EditorView(QWidget* parent = nullptr);

  void setDocument(const MarkdownDocument& document, QString documentPath = {});
  // True while an async open parse is in flight: paintEvent shows a centered loading hint instead of
  // the (stale/empty) page.
  void setLoading(bool loading);
  bool refreshBlock(NodeId blockId, const MarkdownDocument& document);
  bool refreshBlocks(const QVector<NodeId>& blockIds, const MarkdownDocument& document);
  // Re-runs the per-block rebuild only for currently-promoted (visible) top-level blocks — used
  // by the spell-check overlay toggle so we don't rebuild the whole large document at once;
  // un-promoted blocks pick up the new state when they scroll into view.
  bool refreshVisibleBlocks(const MarkdownDocument& document);
  bool refreshTopLevelRange(TopLevelRangeChange range, const MarkdownDocument& document);
  void setZoomPercent(int percent);
  void setFontSizePx(int px);
  void setTheme(RenderTheme theme);
  void setCursorHit(HitTestResult hit);
  void setCursorPosition(CursorPosition position);
  void setSelectionRange(SelectionRange selection);
  void setEditingHtmlBlock(NodeId id);
  // Per-code-fence horizontal scroll state (owned by EditorController). EditorView mutates the
  // offset on Shift+wheel / scrollbar drag and passes it down to paint/hit-test.
  void setCodeFenceScroll(CodeFenceScrollController* controller);
  void clearCursor();
  void setCodeLanguageSuggestions(QStringList languages);

  QRectF nodeRect(NodeId id) const;
  // Map a document-coordinate point to viewport coordinates (applies the current vertical scroll).
  QPointF mapDocumentToViewport(const QPointF& documentPos) const;
  // Exposed for tests: the current caret hit and the laid-out document height.
  HitTestResult cursorHit() const { return cursorHit_; }
  CursorPosition cursorPosition() const { return cursorPosition_; }
  qreal layoutTotalHeight() const { return layout_ ? layout_->totalHeight() : 0.0; }
  const RenderTheme& theme() const { return theme_; }
  void scrollToNode(NodeId id);
  void scrollToCursorCentered();
  void scrollToCursorCenteredAnimated();
  void setTypewriterMode(bool enabled);
  // editor/typewriterCursorMiddle: when typewriter mode is on, keep the cursor centered (on) or
  // only scroll when it leaves the comfort margins (off).
  void setTypewriterCursorMiddle(bool enabled);
  void setFocusMode(bool enabled);
  const BlockLayout* blockAtViewportPos(QPointF viewportPos) const;
  // Direct layout lookup by node id (resolves nested blocks such as a list item,
  // unlike blockAtViewportPos which returns the innermost block under a point).
  const BlockLayout* blockLayoutForNode(NodeId id) const;
  HitTestResult hitTest(QPointF viewportPos) const;
  // The caret's document-space rect with a scrollable code fence's horizontal offset subtracted, so
  // it matches where the translated text is actually drawn. paintInsertionCursor / the IME cursor
  // rectangle / caret dirty-rects all consume this; exposing it lets tests verify the caret lands on
  // the visible character instead of the natural advance when a fence is scrolled.
  QRectF effectiveCursorRect() const;

signals:
  void blockClicked(HitTestResult result);
  void selectionChanged(SelectionRange selection, HitTestResult focusHit);
  void textCommitted(QString text);
  void folderDropped(QString path);
  void markdownFileDropped(QString path);
  void importableFileDropped(QString path);
  void codeLanguageCommitted(NodeId codeId, QString language);
  void tableResizeRequested(int rows, int columns);
  void tableColumnAlignmentRequested(TableAlignment alignment);
  void tableDeleteRequested();
  void tableMoreActionsRequested(QPoint globalPos);
  void htmlEditToggleRequested(NodeId blockId);
  void taskCheckboxToggled(NodeId blockId);
  // Right-click in rendered mode: carries the hit under the cursor (zone/link/
  // image/table context) plus the global position. The caret has already been
  // moved to the click (unless it landed on the current selection), so the
  // receiver can build a caret-based menu from the command registry. MainWindow
  // owns commands_ and builds/execs the menu; EditorView stays decoupled.
  void contextMenuRequested(HitTestResult hit, QPoint globalPos);

protected:
  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  struct HeadingBadge {
    NodeId blockId;
    QRectF viewportRect;
    int level = 0;

    bool isValid() const { return blockId.isValid() && !viewportRect.isEmpty() && level >= 3 && level <= 6; }
  };

  void rebuildLayout();
  void updateScrollBars();
  // Lazy-layout: promote the visible window (+buffer) to full detail, pinning the scrollbar so
  // promoting blocks above the viewport doesn't shift what the user sees.
  void ensureVisibleBuilt();
  // A stable on-screen reference used to keep the viewport from jumping when a layout change moves
  // block heights. Captured as the top-level *node id* at the top of the viewport (not a slot
  // index), so it survives in-place rebuilds, structural edits AND a full re-parse/rebuild — the
  // scrollbar value is re-derived to keep that block pinned to its captured screen offset. This
  // single mechanism replaces the old split between "anchored" (lazy promotion, marker reveal) and
  // "unanchored" (every edit-driven refresh) paths that caused the scrollbar to flicker.
  struct ViewportAnchor {
    bool valid = false;
    NodeId nodeId;
    qreal screenOffset = 0.0;  // slotTop(nodeId) - scrollY() at capture time
  };
  // Resolve the block to anchor on into a ViewportAnchor. When `preferNode` is valid it is used
  // directly (e.g. the cursor's block, so a marker reveal keeps the clicked line put); otherwise
  // the topmost visible block is used. Invalid when the document is empty / above its first block.
  ViewportAnchor captureViewportAnchor(NodeId preferNode = {}) const;
  // Re-pin the captured block to its original screen offset after a layout change; returns true
  // when the scrollbar moved. A no-op (false) when the block was removed — the caller's
  // cursor-follow path then decides where to land. Self-guards inScrollBuild_, so it is safe both
  // inside a ScopedViewportPin and on its own.
  bool restoreViewportAnchor(const ViewportAnchor& anchor);
  // RAII pin: every mutation that can change block Y-coordinates or totalHeight (per-block
  // refresh, range refresh, full rebuild, lazy promotion) runs inside one of these. It captures the
  // viewport anchor on construction, holds inScrollBuild_ so the implicit
  // setRange -> clamp -> valueChanged -> scrollContentsBy -> ensureVisibleBuilt cascade cannot
  // re-enter layout, and on destruction re-derives the scrollbar value so the visible content never
  // jumps. Nested pins are safe: the outermost governs the final scroll position.
  class ScopedViewportPin {
   public:
    explicit ScopedViewportPin(EditorView& view, NodeId preferNode = {});
    ~ScopedViewportPin();
    ScopedViewportPin(const ScopedViewportPin&) = delete;
    ScopedViewportPin& operator=(const ScopedViewportPin&) = delete;

   private:
    EditorView& view_;
    ViewportAnchor anchor_;
    bool prevGuard_;
  };
  QRectF documentViewportRect() const;
  qreal scrollY() const;
  void applyScrollBarStyle();
  void updateCodeLanguageEditor();
  void updateTableToolbar();
  void updateCursorHitFromPosition();
  void refreshInlineProjectionForSelectionChange(SelectionRange previousSelection);
  void addSelectionBlocks(QVector<NodeId>& blockIds, const SelectionRange& selection) const;
  void paintCurrentTableCell(QPainter& painter) const;
  void paintSelection(QPainter& painter) const;
  // Map a code fence's document-space selection rects into viewport space. Scrollable fences
  // (wrap off + overflow) paint their text translated by -offset, so the highlight must shift by
  // the same amount and be clipped to the visible text window (the content rect minus the scrollbar
  // strip) — otherwise the highlight sits at the content's natural x while the text has scrolled.
  void paintSelectionRectsForBlock(QPainter& painter, const BlockLayout* block, const QVector<QRectF>& documentRects) const;
  // True for a code fence that paints a horizontal scrollbar (wrap off + a line wider than content).
  bool isScrollableCodeFence(const BlockLayout* block) const;
  // True when `viewportPos` falls inside the current selection's painted rects.
  // Used by contextMenuEvent to decide whether a right-click should move the
  // caret: a click on an existing selection leaves it intact (so Cut/Copy act on
  // it), a click elsewhere repositions the caret to the click so caret-based
  // commands target the clicked location.
  bool selectionContainsViewportPoint(const HitTestResult& hit, QPointF viewportPos) const;
  void paintInsertionCursor(QPainter& painter) const;
  void paintHeadingBadge(QPainter& painter) const;
  void paintHtmlHoverOverlay(QPainter& painter) const;
  // Centered spinner + H1-sized translatable "Loading…" shown while an async open parse is
  // in flight, replacing the stale/empty page so the user gets immediate feedback.
  void paintLoadingOverlay(QPainter& painter) const;
  HeadingBadge headingBadgeForBlock(NodeId blockId) const;
  QRectF headingBadgeViewportRectForBlock(NodeId blockId) const;
  QRectF htmlHoverButtonViewportRect() const;
  void updateHtmlHover(QPointF viewportPos);
  void updateBlockHover(QPointF viewportPos);
  void repaintHoverBlock(NodeId blockId);
  qreal hoverTransitionMs(const QString& host) const;
  // CSS `:focus` (the top-level block holding the caret): drive a FocusAnimator
  // whose phase feeds the focus glow/bg/scale/text-recolour, parallel to hover.
  void updateBlockFocus();
  void repaintFocusBlock(NodeId blockId);
  qreal focusTransitionMs(const QString& host) const;
  void repaintAnimatedBlocks();
  void clearHtmlHover();
  HtmlBlockHoverController::Inputs htmlHoverInputs() const;
  void applySelectionRange(SelectionRange selection);
  void updateDragSelection(QPointF viewportPos);
  void updateMouseCursor(QPointF viewportPos);
  void ensureScrollAnimation();
  void stopScrollAnimation();
  // Horizontal scrollbar for scrollable code fences (wrap off + an overflowing line).
  bool scrollCodeFenceHorizontally(QWheelEvent* event, bool horizontal);
  void dragCodeFenceScrollBarTo(NodeId blockId, QPointF viewportPos);
  // Keep the caret inside the visible horizontal span of the code fence it lives in.
  void ensureCodeFenceCursorVisible();
  // Scroll target that keeps the cursor on screen under the active typewriter policy, or -1 when
  // the relaxed policy decides the cursor is already comfortable and no scroll is needed.
  int typewriterScrollTarget(const QRectF& cursor) const;

  QPointer<const MarkdownDocument> document_;
  QString documentPath_;
  RenderTheme theme_ = RenderTheme::defaultTheme();
  std::unique_ptr<DocumentLayout> layout_;
  CursorPosition cursorPosition_;
  SelectionRange selection_;
  HitTestResult cursorHit_;
  bool cursorVisible_ = false;
  // Document-coordinate rect of the last painted caret; dirtied on the next
  // refresh so a moved caret is erased at its old position, even when that
  // position lies outside any block (e.g. the virtual trailing paragraph).
  mutable QRectF lastPaintedCaretDocumentRect_;
  bool draggingSelection_ = false;
  bool dragSelectionPending_ = false;
  SelectionRange preDragSelection_;
  QPointF dragStartViewportPos_;
  HitTestResult dragAnchorHit_;
  CodeLanguageEditor* codeLanguageEditor_ = nullptr;
  TableToolbar* tableToolbar_ = nullptr;
  bool typewriterMode_ = false;
  bool typewriterCursorMiddle_ = true;
  bool focusMode_ = false;
  QPropertyAnimation* scrollAnimation_ = nullptr;
  NodeId editingHtmlBlockId_;
  // Per-block coalesce of the per-keystroke double-refresh (immediate selection refresh + deferred
  // BrushQueue edit refresh both rebuild the same block). A block is skipped only if it was last
  // built with the CURRENT selection AND the CURRENT document revision — so a genuinely different
  // selection or a content edit still wins. Cleared on full rebuild (setDocument).
  struct BuiltStamp { SelectionRange selection; quint64 revision; };
  QHash<NodeId, BuiltStamp> blockBuiltAt_;
  CodeFenceScrollController* codeFenceScroll_ = nullptr;
  NodeId codeFenceScrollDragId_;  // block being horizontally dragged via its scrollbar (invalid when idle)
  HtmlBlockHoverController htmlHover_;
  class HoverAnimator* hoverAnimator_ = nullptr;
  NodeId hoveredBlockId_;  // top-level block under the cursor (for CSS :hover glow)
  class FocusAnimator* focusAnimator_ = nullptr;
  NodeId focusedBlockId_;  // top-level block holding the caret (for CSS :focus)
  class KeyframeAnimator* keyframeAnimator_ = nullptr;
  // True while a ScopedViewportPin (or restoreViewportAnchor) is reconciling the scrollbar, so the
  // implicit setRange -> clamp -> valueChanged -> scrollContentsBy -> ensureVisibleBuilt cascade
  // cannot re-enter layout and re-promote mid-transaction.
  bool inScrollBuild_ = false;
  bool loading_ = false;  // set by setLoading; paintEvent shows a loading hint while true
  QTimer* loadingTimer_ = nullptr;  // idle-gated: runs only while loading_, advancing loadingPhase_
  qreal loadingPhase_ = 0.0;  // 0..1 head position of the bright wave around the dot ring
};

}  // namespace muffin
