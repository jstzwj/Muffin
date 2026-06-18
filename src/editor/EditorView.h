#pragma once

#include "document/TopLevelRangeChange.h"
#include "editor/HtmlBlockHoverController.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QPointer>
#include <QRectF>

#include <memory>

class QPropertyAnimation;
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
  void codeLanguageCommitted(NodeId codeId, QString language);
  void tableResizeRequested(int rows, int columns);
  void tableColumnAlignmentRequested(TableAlignment alignment);
  void tableDeleteRequested();
  void tableMoreActionsRequested(QPoint globalPos);
  void htmlEditToggleRequested(NodeId blockId);
  void taskCheckboxToggled(NodeId blockId);
  void spellCorrectionRequested(qsizetype sourceStart, qsizetype removedLength, QString replacement);

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
  // Lazy-layout: promote the visible window (+buffer) to full detail, anchor-correcting the
  // scrollbar so promoting blocks above the viewport doesn't shift what the user sees.
  void ensureVisibleBuilt();
  void promoteWithAnchor(qsizetype first, qsizetype last);
  // A stable on-screen reference point used to keep the viewport from jumping when a rebuild
  // changes block heights. Capture a top-level slot's screen offset before the rebuild, restore
  // it after by moving the scrollbar. Shared by lazy-block promotion and selection-driven inline
  // rebuilds (marker reveal) so neither drifts what the user is looking at.
  struct ViewportAnchor {
    bool valid = false;
    qsizetype slotIndex = -1;
    qreal screenOffset = 0.0;  // slotTop(slotIndex) - scrollY() at capture time
  };
  ViewportAnchor captureSlotAnchor(qsizetype slotIndex) const;
  // Re-pins the captured slot to its original screen offset; returns true if the scrollbar moved.
  bool restoreSlotAnchor(const ViewportAnchor& anchor);
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
  void paintInsertionCursor(QPainter& painter) const;
  void paintHeadingBadge(QPainter& painter) const;
  void paintHtmlHoverOverlay(QPainter& painter) const;
  HeadingBadge headingBadgeForBlock(NodeId blockId) const;
  QRectF headingBadgeViewportRectForBlock(NodeId blockId) const;
  QRectF htmlHoverButtonViewportRect() const;
  void updateHtmlHover(QPointF viewportPos);
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
  CodeFenceScrollController* codeFenceScroll_ = nullptr;
  NodeId codeFenceScrollDragId_;  // block being horizontally dragged via its scrollbar (invalid when idle)
  HtmlBlockHoverController htmlHover_;
  bool inScrollBuild_ = false;  // guards ensureVisibleBuilt against re-entry via anchor setValue
};

}  // namespace muffin
