#include "editor/EditorView.h"

#include "diagnostics/ScopedPerfProbe.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "editor/EditorViewGeometry.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"

#include <QEasingCurve>
#include <QFontMetricsF>
#include <QLoggingCategory>
#include <QPair>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QWheelEvent>

namespace muffin {

// Pull the stateless geometry/hit-test helpers into scope so call sites read as
// before (selectableLength, …). The moved logic lives in editor_geometry.
using namespace editor_geometry;

namespace {

Q_LOGGING_CATEGORY(viewPerf, "muffin.perf", QtWarningMsg)

struct PerfTimer : diag::ScopedPerfProbe {
  explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, viewPerf()) {}
};

}  // namespace

// ─── coordinate / hit-test queries ───────────────────────────────────────────

QRectF EditorView::nodeRect(NodeId id) const {
  if (!layout_) {
    return {};
  }
  const BlockLayout* block = layout_->block(id, theme_);
  return block ? block->rect() : QRectF();
}

void EditorView::scrollToNode(NodeId id) {
  if (!layout_) {
    return;
  }
  // One-shot promote the target block so its top Y is accurate, then scroll to it. Blocks above
  // the target may still be estimated, so the landing is approximate until the surrounding window
  // is promoted by the ensuing scrollContentsBy -> ensureVisibleBuilt (anchor-corrected).
  const qsizetype idx = layout_->topLevelIndexFor(id);
  if (idx < 0) {
    return;
  }
  layout_->ensureBuilt(idx, idx, theme_);
  const qreal targetTop = layout_->slotTop(idx);
  QScrollBar* scrollBar = verticalScrollBar();
  const int target = qBound(scrollBar->minimum(), qRound(targetTop - 24.0), scrollBar->maximum());
  scrollBar->setValue(target);
}

QRectF EditorView::effectiveCursorRect() const {
  if (!cursorHit_.isValid()) {
    return QRectF();
  }
  QRectF cursor = cursorHit_.cursorRect;
  if (cursor.isEmpty()) {
    cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
  }
  // A scrollable code fence (wrap off + overflow) paints its content translated by -offset, so the
  // caret must shift by the same amount to stay aligned with the character under it.
  if (cursorHit_.zone == HitTestResult::Zone::Code && codeFenceScroll_ != nullptr && layout_) {
    const BlockLayout* block = layout_->blockIfPromoted(cursorHit_.blockId);
    if (block != nullptr && block->type() == BlockType::CodeFence &&
        block->codeMaxLineWidth() > block->literalContentRect(theme_).width() + 0.5) {
      cursor.translate(-codeFenceScroll_->offsetFor(cursorHit_.blockId), 0.0);
    }
  }
  return cursor;
}

int EditorView::typewriterScrollTarget(const QRectF& cursor) const {
  QScrollBar* scrollBar = verticalScrollBar();
  const qreal cursorCenterY = cursor.center().y();
  const qreal viewportHeight = static_cast<qreal>(viewport()->height());
  if (typewriterCursorMiddle_) {
    // Keep the caret pinned to the vertical centre of the viewport.
    return qBound(scrollBar->minimum(), qRound(cursorCenterY - viewportHeight / 2.0), scrollBar->maximum());
  }
  // Relaxed policy: scroll only when the caret leaves a comfort band (top/bottom third of the
  // viewport). When it already sits inside the band, no scrolling is wanted.
  const qreal cursorViewportY = cursorCenterY - static_cast<qreal>(scrollBar->value());
  const qreal topMargin = viewportHeight / 3.0;
  const qreal bottomMargin = viewportHeight * 2.0 / 3.0;
  if (cursorViewportY < topMargin) {
    return qBound(scrollBar->minimum(), qRound(cursorCenterY - topMargin), scrollBar->maximum());
  }
  if (cursorViewportY > bottomMargin) {
    return qBound(scrollBar->minimum(), qRound(cursorCenterY - bottomMargin), scrollBar->maximum());
  }
  return -1;
}

void EditorView::scrollToCursorCentered() {
  const QRectF cursor = effectiveCursorRect();
  if (cursor.isEmpty()) {
    return;
  }
  const int target = typewriterScrollTarget(cursor);
  if (target < 0) {
    return;
  }
  verticalScrollBar()->setValue(target);
}

void EditorView::setTypewriterMode(bool enabled) {
  typewriterMode_ = enabled;
  updateScrollBars();
  if (enabled && cursorHit_.isValid()) {
    scrollToCursorCentered();
  } else if (!enabled) {
    // Clamp scroll back to normal range [0, normalMax].
    QScrollBar* sb = verticalScrollBar();
    sb->setValue(qBound(sb->minimum(), sb->value(), sb->maximum()));
  }
}

void EditorView::setTypewriterCursorMiddle(bool enabled) {
  typewriterCursorMiddle_ = enabled;
  updateScrollBars();
  if (typewriterMode_ && cursorHit_.isValid()) {
    scrollToCursorCentered();
  } else if (typewriterMode_) {
    // Relaxed mode uses the normal range; re-clamp any overshoot from the previous centred mode.
    QScrollBar* sb = verticalScrollBar();
    sb->setValue(qBound(sb->minimum(), sb->value(), sb->maximum()));
  }
}

void EditorView::ensureScrollAnimation() {
  if (!scrollAnimation_) {
    scrollAnimation_ = new QPropertyAnimation(verticalScrollBar(), QByteArrayLiteral("value"), this);
    scrollAnimation_->setEasingCurve(QEasingCurve(QEasingCurve::OutCubic));
  }
}

void EditorView::stopScrollAnimation() {
  if (scrollAnimation_ && scrollAnimation_->state() == QAbstractAnimation::Running) {
    scrollAnimation_->stop();
  }
}

void EditorView::scrollToCursorCenteredAnimated() {
  const QRectF cursor = effectiveCursorRect();
  if (cursor.isEmpty()) {
    return;
  }

  QScrollBar* scrollBar = verticalScrollBar();
  const int target = typewriterScrollTarget(cursor);
  if (target < 0) {
    return;
  }

  const int current = scrollBar->value();
  if (current == target) {
    return;
  }

  ensureScrollAnimation();
  stopScrollAnimation();

  const int delta = qAbs(target - current);
  const int duration = qBound(100, delta / 2, 300);

  scrollAnimation_->setDuration(duration);
  scrollAnimation_->setStartValue(current);
  scrollAnimation_->setEndValue(target);
  scrollAnimation_->start();
}

const BlockLayout* EditorView::blockAtViewportPos(QPointF viewportPos) const {
  if (!layout_) {
    return nullptr;
  }
  return layout_->blockAt(QPointF(viewportPos.x(), viewportPos.y() + scrollY()), theme_);
}

const BlockLayout* EditorView::blockLayoutForNode(NodeId id) const {
  if (!layout_) {
    return nullptr;
  }
  return layout_->block(id, theme_);
}

HitTestResult EditorView::hitForCursorPosition(CursorPosition position) const {
  if (!layout_) {
    return {};
  }
  return editor_geometry::hitForCursorPosition(*layout_, theme_, position);
}

HitTestResult EditorView::hitTest(QPointF viewportPos) const {
  PerfTimer perf("view.hitTest");
  if (!layout_) {
    return {};
  }
  const QPointF documentPos(viewportPos.x(), viewportPos.y() + scrollY());
  HitTestResult hit = layout_->hitTest(documentPos, theme_);
  if (hit.isValid()) {
    const NodeId hostId = layout_->topLevelBlockIdFor(hit.blockId);
    const auto openIt = openSequenceMenus_.constFind(hostId);
    if (openIt != openSequenceMenus_.cend() && !openIt->isEmpty()) {
      if (const BlockLayout* block = layout_->blockIfPromoted(hostId)) {
        const HitTestResult interactive = block->hitTest(
            documentPos, theme_, codeFenceScroll_, &openIt.value());
        if (interactive.isValid() &&
            (!interactive.linkHref.isEmpty() ||
             !interactive.toolTip.isEmpty() ||
             !interactive.mermaidMenuActorId.isEmpty())) {
          hit = interactive;
        }
      }
    }
  }
  // The virtual trailing paragraph sits after the last block's content, so as a
  // selection endpoint it is the END of that block — not offset 0. Resolve it
  // here so drag/shift-click selection from the document end selects back
  // through the block content, and serialization copies it, with no special
  // casing downstream.
  if (hit.zone == HitTestResult::Zone::BlockAfter) {
    if (const BlockLayout* block = layout_->block(hit.blockId, theme_)) {
      hit.textOffset = selectableLength(block);
    }
  }
  return hit;
}

// ─── scroll-bar / lazy-promotion / viewport anchor + pin ─────────────────────

void EditorView::scrollContentsBy(int dx, int dy) {
  QAbstractScrollArea::scrollContentsBy(dx, dy);
  if (layout_ && document_) {
    ensureVisibleBuilt();
  }
  updateCodeLanguageEditor();
  updateTableToolbar();
}

void EditorView::ensureVisibleBuilt() {
  if (!layout_ || !document_ || inScrollBuild_ || viewport()->height() <= 0) {
    return;
  }
  const QRectF vis = documentViewportRect();
  const qreal buffer = viewport()->height() * 1.5;
  const QPair<qsizetype, qsizetype> range = layout_->slotRangeOverlappingY(vis.top() - buffer, vis.bottom() + buffer);
  if (range.first < 0) {
    return;
  }
  // Promoting estimated slots to measured ones changes totalHeight; pin the topmost visible block
  // so the snap is absorbed entirely into the scrollbar range/value and never moves the content.
  ScopedViewportPin pin(*this);
  layout_->ensureBuilt(range.first, range.second, theme_);
}

EditorView::ViewportAnchor EditorView::captureViewportAnchor(NodeId preferNode) const {
  ViewportAnchor anchor;
  if (!layout_ || layout_->slotCount() == 0 || viewport()->height() <= 0) {
    return anchor;
  }
  qsizetype slot = -1;
  if (preferNode.isValid()) {
    slot = layout_->topLevelIndexFor(preferNode);  // honor the caller's preferred block (e.g. cursor)
  }
  if (slot < 0) {
    // Default reference: the topmost block visible at the top of the viewport.
    const QRectF vis = documentViewportRect();
    const QPair<qsizetype, qsizetype> visRange = layout_->slotRangeOverlappingY(vis.top(), vis.bottom());
    slot = visRange.first;
  }
  if (slot < 0 || slot >= layout_->slotCount()) {
    return anchor;
  }
  anchor.valid = true;
  anchor.nodeId = layout_->slotNodeId(slot);  // node id survives re-parse / reorder / full rebuild
  anchor.screenOffset = layout_->slotTop(slot) - scrollY();
  return anchor;
}

bool EditorView::restoreViewportAnchor(const ViewportAnchor& anchor) {
  if (!anchor.valid || !layout_) {
    return false;
  }
  const qsizetype slot = layout_->topLevelIndexFor(anchor.nodeId);
  if (slot < 0) {
    return false;  // block removed by the change — leave cursor-follow to decide
  }
  QScrollBar* bar = verticalScrollBar();
  const int target = qBound(bar->minimum(), qRound(layout_->slotTop(slot) - anchor.screenOffset), bar->maximum());
  if (target == bar->value()) {
    return false;
  }
  // Suppress the valueChanged -> scrollContentsBy -> ensureVisibleBuilt cascade whether or not a
  // ScopedViewportPin already holds the guard.
  const bool prev = inScrollBuild_;
  inScrollBuild_ = true;
  bar->setValue(target);
  inScrollBuild_ = prev;
  return true;
}

EditorView::ScopedViewportPin::ScopedViewportPin(EditorView& view, NodeId preferNode)
    : view_(view), anchor_(view_.captureViewportAnchor(preferNode)), prevGuard_(view_.inScrollBuild_) {
  view_.inScrollBuild_ = true;
}

EditorView::ScopedViewportPin::~ScopedViewportPin() {
  view_.updateScrollBars();           // range may have changed; value clamp is harmless under the guard
  view_.restoreViewportAnchor(anchor_);  // re-derive value so the captured block stays on screen
  view_.inScrollBuild_ = prevGuard_;
}

void EditorView::updateScrollBars() {
  const int pageStep = qMax(1, viewport()->height());
  const int vh = viewport()->height();
  const int normalMax = layout_ ? qMax(0, static_cast<int>(std::ceil(layout_->totalHeight() - vh))) : 0;
  verticalScrollBar()->setPageStep(pageStep);
  verticalScrollBar()->setSingleStep(qMax(16, pageStep / 12));
  if (typewriterMode_ && typewriterCursorMiddle_) {
    // Allow scrolling past document boundaries so the cursor can always be
    // centered: negative scroll (empty space above) and extra range (empty
    // space below).  Half a viewport on each side is enough to center the
    // cursor at the very first or last line. The relaxed (non-centered)
    // policy never forces the caret past the document edges, so it keeps the
    // normal [0, normalMax] range.
    const int halfVh = vh / 2;
    verticalScrollBar()->setRange(-halfVh, normalMax + halfVh);
  } else {
    verticalScrollBar()->setRange(0, normalMax);
  }
}

QRectF EditorView::documentViewportRect() const {
  return QRectF(0, scrollY(), viewport()->width(), viewport()->height());
}

qreal EditorView::scrollY() const {
  return static_cast<qreal>(verticalScrollBar()->value());
}

QPointF EditorView::mapDocumentToViewport(const QPointF& documentPos) const {
  return QPointF(documentPos.x(), documentPos.y() - scrollY());
}

void EditorView::applyScrollBarStyle() {
  const QString background = theme_.backgroundColor().name(QColor::HexRgb);
  setStyleSheet(QStringLiteral(
      "EditorView { background:%1; border:0; }"
      "QScrollBar:vertical { background:%1; width:8px; margin:0; }"
      "QScrollBar::handle:vertical { background:#b7b7b7; min-height:54px; border-radius:3px; margin:1px 2px; }"
      "QScrollBar::handle:vertical:hover { background:#999999; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; border:0; background:transparent; }"
      "QScrollBar:add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
      "QScrollBar:horizontal { background:%1; height:8px; margin:0; }"
      "QScrollBar::handle:horizontal { background:#b7b7b7; min-width:54px; border-radius:3px; margin:2px 1px; }"
      "QScrollBar::handle:horizontal:hover { background:#999999; }"
      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; border:0; background:transparent; }"
      "QScrollBar:add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }")
                    .arg(background));
}

// ─── scrollable code-fence horizontal scroll ─────────────────────────────────

void EditorView::setCodeFenceScroll(CodeFenceScrollController* controller) {
  codeFenceScroll_ = controller;
  if (layout_) {
    layout_->setCodeFenceScroll(controller);
  }
}

bool EditorView::scrollCodeFenceHorizontally(QWheelEvent* event, bool horizontal) {
  if (codeFenceScroll_ == nullptr || layout_ == nullptr) {
    return false;
  }
  const HitTestResult hit = hitTest(event->position());
  if (!hit.isValid()) {
    return false;
  }
  const BlockLayout* block = layout_->blockIfPromoted(hit.blockId);
  if (block == nullptr || block->type() != BlockType::CodeFence) {
    return false;
  }
  const qreal maxW = block->codeMaxLineWidth();
  const qreal visibleW = block->literalContentRect(theme_).width();
  if (maxW <= visibleW + 0.5) {
    return false;  // not scrollable (wrap on, or no overflowing line)
  }
  const int delta = horizontal ? event->angleDelta().x() : event->angleDelta().y();
  const qreal step = QFontMetricsF(theme_.codeFont()).horizontalAdvance(QLatin1Char('M')) * 3.0;
  const qreal current = codeFenceScroll_->offsetFor(hit.blockId);
  const qreal maxOffset = qMax<qreal>(0.0, maxW - visibleW);
  codeFenceScroll_->setOffset(hit.blockId, qBound<qreal>(0.0, current - delta / 120.0 * step, maxOffset));
  viewport()->update();
  return true;
}

void EditorView::dragCodeFenceScrollBarTo(NodeId blockId, QPointF viewportPos) {
  if (codeFenceScroll_ == nullptr || layout_ == nullptr || !blockId.isValid()) {
    return;
  }
  const BlockLayout* block = layout_->blockIfPromoted(blockId);
  if (block == nullptr) {
    return;
  }
  const qreal maxW = block->codeMaxLineWidth();
  const QRectF content = block->literalContentRect(theme_);  // document space
  const qreal visibleW = content.width();
  const qreal totalW = qMax(maxW, visibleW);
  if (totalW <= visibleW + 0.5) {
    return;
  }
  const qreal stripH = BlockLayout::scrollBarStripHeight(theme_);
  const QRectF strip(content.left(), content.bottom() - stripH, content.width(), stripH);
  const qreal thumbW = qMax<qreal>(24.0, strip.width() * visibleW / totalW);
  const qreal trackRange = qMax<qreal>(1.0, strip.width() - thumbW);
  // Scroll is vertical-only, so a document x equals its viewport x.
  const qreal t = qBound<qreal>(0.0, (viewportPos.x() - strip.left() - thumbW * 0.5) / trackRange, 1.0);
  const qreal maxOffset = qMax<qreal>(0.0, maxW - visibleW);
  codeFenceScroll_->setOffset(blockId, t * maxOffset);
  viewport()->update();
}

void EditorView::ensureCodeFenceCursorVisible() {
  if (codeFenceScroll_ == nullptr || layout_ == nullptr) {
    return;
  }
  if (!cursorHit_.isValid() || cursorHit_.zone != HitTestResult::Zone::Code) {
    return;
  }
  const BlockLayout* block = layout_->blockIfPromoted(cursorHit_.blockId);
  if (block == nullptr || block->type() != BlockType::CodeFence) {
    return;
  }
  const qreal maxW = block->codeMaxLineWidth();
  const QRectF content = block->literalContentRect(theme_);
  const qreal visibleW = content.width();
  if (maxW <= visibleW + 0.5) {
    return;  // wrap on sets maxW to 0; otherwise no overflow
  }
  const qreal caretX = cursorHit_.cursorRect.left() - content.left();
  const qreal current = codeFenceScroll_->offsetFor(cursorHit_.blockId);
  const qreal margin = QFontMetricsF(theme_.codeFont()).horizontalAdvance(QLatin1Char('M')) * 2.0;
  qreal next = current;
  // Only scroll when the caret is genuinely OUTSIDE the visible window. The earlier margin-based
  // test (caretX < current+margin / caretX > current+visibleW-margin) also fired on a click whose
  // caret was already visible but within `margin` of an edge — that shifted the content, which
  // desynchronized a drag's anchor (captured from the pre-scroll hit) from its focus (resolved at
  // the post-scroll offset): the caret then "jumped" sideways by ~margin the moment the drag began.
  // A click places the caret at a visible spot, so it must not move; typing/keyboard nav can push
  // the caret fully off-screen, and those cases still scroll to reveal it (with a small margin).
  if (caretX < current) {
    next = qMax<qreal>(0.0, caretX - margin);
  } else if (caretX > current + visibleW) {
    next = qMin<qreal>(maxW - visibleW, caretX - visibleW + margin);
  }
  if (next != current) {
    codeFenceScroll_->setOffset(cursorHit_.blockId, next);
    viewport()->update();
  }
}

}  // namespace muffin
