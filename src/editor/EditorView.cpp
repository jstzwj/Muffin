#include "editor/EditorView.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "document/MarkdownDocument.h"
#include "editor/CodeLanguageEditor.h"
#include "editor/EditorViewGeometry.h"
#include "editor/HtmlBlockHoverController.h"
#include "editor/HoverAnimator.h"
#include "editor/KeyframeAnimator.h"
#include "editor/ResourceUrl.h"
#include "editor/TableToolbar.h"
#include "render/DecorationPainter.h"
#include "render/ImageLoader.h"
#include "render/KeyframeSampler.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QList>
#include <QMimeData>
#include <QPainter>
#include <QSet>
#include <QLoggingCategory>
#include <QScrollBar>
#include <QPropertyAnimation>
#include <QVector>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <QFontMetricsF>
#include <QUrl>
#include <QKeyEvent>

namespace muffin {

// Pull the stateless geometry/hit-test helpers into scope so call sites read as
// before (isSelectableZone, selectableLength, …). The moved logic lives in
// editor_geometry; EditorView keeps only the stateful orchestration.
using namespace editor_geometry;

namespace {

Q_LOGGING_CATEGORY(viewPerf, "muffin.perf", QtWarningMsg)

// Map a block to its CSS host key for decoration/hover lookup ("" → none).
QString hostKeyForBlock(const BlockLayout& block) {
  switch (block.type()) {
    case BlockType::Heading: return QStringLiteral("h%1").arg(block.headingLevel());
    case BlockType::BlockQuote: return QStringLiteral("blockquote");
    default: return QString();
  }
}

bool sameCursorPosition(const CursorPosition& a, const CursorPosition& b) {
  return a.blockId == b.blockId && a.text.nodeId == b.text.nodeId && a.text.textOffset == b.text.textOffset &&
         a.text.sourceOffset == b.text.sourceOffset && a.text.inMeta == b.text.inMeta;
}

bool sameSelectionRange(const SelectionRange& a, const SelectionRange& b) {
  return sameCursorPosition(a.anchor, b.anchor) && sameCursorPosition(a.focus, b.focus);
}

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(viewPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(viewPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

}  // namespace

EditorView::EditorView(QWidget* parent) : QAbstractScrollArea(parent), layout_(std::make_unique<DocumentLayout>()) {
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  viewport()->setMouseTracking(true);
  viewport()->setAutoFillBackground(false);
  // Enable external drag-and-drop. acceptDrops defaults to false, so without
  // this the overridden drag*/dropEvent handlers are never delivered and drops
  // of folders/.md/.txt/images onto the window are silently ignored. The
  // viewport is the real drop target for a QAbstractScrollArea (its events are
  // forwarded to these handlers), so it must accept drops too.
  setAcceptDrops(true);
  viewport()->setAcceptDrops(true);
  setBackgroundRole(QPalette::Base);
  applyScrollBarStyle();
  hoverAnimator_ = new HoverAnimator(this);
  hoverAnimator_->repaintBlock = [this](NodeId id) { repaintHoverBlock(id); };
  keyframeAnimator_ = new KeyframeAnimator(this);
  keyframeAnimator_->repaintAnimated = [this]() { repaintAnimatedBlocks(); };

  codeLanguageEditor_ = new CodeLanguageEditor(viewport(), this);
  codeLanguageEditor_->setSuggestions({
      QStringLiteral("bash"),
      QStringLiteral("c"),
      QStringLiteral("cpp"),
      QStringLiteral("csharp"),
      QStringLiteral("css"),
      QStringLiteral("go"),
      QStringLiteral("html"),
      QStringLiteral("ini"),
      QStringLiteral("java"),
      QStringLiteral("javascript"),
      QStringLiteral("json"),
      QStringLiteral("kotlin"),
      QStringLiteral("lua"),
      QStringLiteral("markdown"),
      QStringLiteral("mermaid"),
      QStringLiteral("objective-c"),
      QStringLiteral("pascal"),
      QStringLiteral("pegjs"),
      QStringLiteral("perl"),
      QStringLiteral("perl6"),
      QStringLiteral("pgp"),
      QStringLiteral("php"),
      QStringLiteral("powershell"),
      QStringLiteral("python"),
      QStringLiteral("qml"),
      QStringLiteral("r"),
      QStringLiteral("ruby"),
      QStringLiteral("rust"),
      QStringLiteral("sql"),
      QStringLiteral("swift"),
      QStringLiteral("text"),
      QStringLiteral("toml"),
      QStringLiteral("typescript"),
      QStringLiteral("xml"),
      QStringLiteral("yaml"),
  });
  connect(codeLanguageEditor_, &CodeLanguageEditor::languageCommitted, this, &EditorView::codeLanguageCommitted);

  tableToolbar_ = new TableToolbar(viewport(), this);
  connect(tableToolbar_, &TableToolbar::columnAlignmentRequested, this, &EditorView::tableColumnAlignmentRequested);
  connect(tableToolbar_, &TableToolbar::moreActionsRequested, this, &EditorView::tableMoreActionsRequested);
  connect(tableToolbar_, &TableToolbar::deleteRequested, this, &EditorView::tableDeleteRequested);
  connect(tableToolbar_, &TableToolbar::resizeRequested, this, &EditorView::tableResizeRequested);

  connect(&ImageLoader::instance(), &ImageLoader::imageReady, this, [this](const QString&) {
    if (document_) {
      setDocument(*document_, documentPath_);
    }
  });

  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
    clearHtmlHover();
    updateCodeLanguageEditor();
    updateTableToolbar();
  });
  connect(verticalScrollBar(), &QScrollBar::sliderPressed, this, [this] {
    stopScrollAnimation();
  });
}

void EditorView::setDocument(const MarkdownDocument& document, QString documentPath) {
  document_ = &document;
  documentPath_ = std::move(documentPath);
  rebuildLayout();
  updateTableToolbar();
}

bool EditorView::refreshBlock(NodeId blockId, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshBlock");
  if (!layout_ || document_ != &document) {
    return false;
  }

  // Rebuild inside a viewport pin: an edit that changes a block's height reflows every block below
  // it and changes totalHeight, which must never move what the user is looking at. The pin
  // re-derives the scrollbar value at scope exit, so the dirty rects below are computed against the
  // final scroll offset (a no-op when nothing above the viewport moved).
  DocumentLayout::BlockRebuildResult result;
  {
    ScopedViewportPin pin(*this);
    result = layout_->rebuildBlock(blockId, document, theme_, selection_);
    if (!result.rebuilt) {
      return false;  // pin reconciles (nothing moved); caller falls back to setDocument
    }
  }

  updateCursorHitFromPosition();
  // Typing in a wrap-OFF code fence grows the line at its natural width; once the caret crosses the
  // visible window's right edge it would otherwise scroll off-screen. setCursorHit/rebuildLayout
  // already follow, but the per-keystroke refresh path needs the same horizontal keep-visible.
  ensureCodeFenceCursorVisible();
  updateTableToolbar();
  QRect dirty;
  addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  const QRectF badgeRect = headingBadgeViewportRectForBlock(cursorPosition_.blockId);
  if (!badgeRect.isEmpty()) {
    dirty = dirty.united(badgeRect.adjusted(-2, -2, 2, 2).toAlignedRect());
  }
  // The caret may sit outside the refreshed block (e.g. on the virtual trailing
  // paragraph below the last block). Dirty both the new caret position (to draw
  // it) and the previously painted one (to erase the ghost on move).
  dirty = uniteDocumentRectDirty(dirty, effectiveCursorRect(), scrollY(), viewport()->size());
  dirty = uniteDocumentRectDirty(dirty, lastPaintedCaretDocumentRect_, scrollY(), viewport()->size());
  if (dirty.isEmpty()) {
    dirty = viewport()->rect();
  }
  viewport()->update(dirty);
  return true;
}

bool EditorView::refreshBlocks(const QVector<NodeId>& blockIds, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshBlocks");
  if (!layout_ || document_ != &document) {
    return false;
  }

  // Rebuild every block inside one viewport pin (the suffix shift from each rebuild accumulates),
  // then compute dirty rects against the reconciled scroll offset.
  QVector<DocumentLayout::BlockRebuildResult> results;
  {
    ScopedViewportPin pin(*this);
    for (NodeId blockId : blockIds) {
      const DocumentLayout::BlockRebuildResult result = layout_->rebuildBlock(blockId, document, theme_, selection_);
      if (!result.rebuilt) {
        return false;
      }
      results.append(result);
    }
  }

  QRect dirty;
  for (const DocumentLayout::BlockRebuildResult& result : results) {
    addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  }
  const QRectF badgeRect = headingBadgeViewportRectForBlock(cursorPosition_.blockId);
  if (!badgeRect.isEmpty()) {
    dirty = dirty.united(badgeRect.adjusted(-2, -2, 2, 2).toAlignedRect());
  }
  updateCursorHitFromPosition();
  updateTableToolbar();
  // Include the caret so a caret outside the refreshed blocks (e.g. on the
  // virtual trailing paragraph below the last block) repaints too — both the
  // new position (to draw) and the previous one (to erase the ghost on move).
  dirty = uniteDocumentRectDirty(dirty, effectiveCursorRect(), scrollY(), viewport()->size());
  dirty = uniteDocumentRectDirty(dirty, lastPaintedCaretDocumentRect_, scrollY(), viewport()->size());
  viewport()->update(dirty.isEmpty() ? viewport()->rect() : dirty);
  return true;
}

bool EditorView::refreshTopLevelRange(TopLevelRangeChange range, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshTopLevelRange");
  if (!layout_ || document_ != &document) {
    return false;
  }

  // Rebuild the changed range inside a viewport pin, then dirty against the reconciled offset.
  DocumentLayout::RangeRebuildResult result;
  {
    ScopedViewportPin pin(*this);
    result = layout_->rebuildTopLevelRange(range, document, theme_, selection_);
    if (!result.rebuilt) {
      return false;
    }
  }

  updateCursorHitFromPosition();
  updateCodeLanguageEditor();
  updateTableToolbar();
  QRect dirty;
  addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  const QRectF badgeRect = headingBadgeViewportRectForBlock(cursorPosition_.blockId);
  if (!badgeRect.isEmpty()) {
    dirty = dirty.united(badgeRect.adjusted(-2, -2, 2, 2).toAlignedRect());
  }
  viewport()->update(dirty.isEmpty() ? viewport()->rect() : dirty);
  return true;
}

void EditorView::setZoomPercent(int percent) {
  theme_.setZoomPercent(percent);
  applyScrollBarStyle();
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  rebuildLayout();
}

void EditorView::setFontSizePx(int px) {
  theme_.setFontSizePx(px);
  rebuildLayout();
}

void EditorView::setTheme(RenderTheme theme) {
  const int zoom = theme_.zoomPercent();
  const int fontSize = theme_.fontSizePx();
  theme_ = std::move(theme);
  theme_.setZoomPercent(zoom);
  theme_.setFontSizePx(fontSize);
  if (keyframeAnimator_) { keyframeAnimator_->setTheme(theme_); }
  applyScrollBarStyle();
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  rebuildLayout();
}

void EditorView::setCursorHit(HitTestResult hit) {
  dragSelectionPending_ = false;
  draggingSelection_ = false;
  const SelectionRange previousSelection = selection_;
  cursorHit_ = hit;
  cursorPosition_ = hit.cursorPosition();
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  refreshInlineProjectionForSelectionChange(previousSelection);
  ensureCodeFenceCursorVisible();
  updateTableToolbar();
}

void EditorView::setCursorPosition(CursorPosition position) {
  dragSelectionPending_ = false;
  draggingSelection_ = false;
  const SelectionRange previousSelection = selection_;
  cursorPosition_ = position;
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  refreshInlineProjectionForSelectionChange(previousSelection);
  updateTableToolbar();
}

void EditorView::setSelectionRange(SelectionRange selection) {
  if ((dragSelectionPending_ || draggingSelection_) && sameSelectionRange(selection_, selection)) {
    return;
  }
  dragSelectionPending_ = false;
  draggingSelection_ = false;
  applySelectionRange(selection);
}

void EditorView::setEditingHtmlBlock(NodeId id) {
  if (editingHtmlBlockId_ == id) {
    return;
  }
  editingHtmlBlockId_ = id;
  if (layout_) {
    layout_->setEditingHtmlBlock(id);
  }
  clearHtmlHover();
  if (document_) {
    rebuildLayout();
  }
}

void EditorView::applySelectionRange(SelectionRange selection) {
  const SelectionRange previousSelection = selection_;
  selection_ = selection;
  cursorPosition_ = selection.focus;
  if (draggingSelection_) {
    cursorVisible_ = false;
    viewport()->update();
    updateTableToolbar();
    return;
  }
  refreshInlineProjectionForSelectionChange(previousSelection);
  updateTableToolbar();
}

void EditorView::clearCursor() {
  cursorPosition_ = {};
  selection_ = {};
  preDragSelection_ = {};
  cursorHit_ = {};
  cursorVisible_ = false;
  lastPaintedCaretDocumentRect_ = {};
  dragSelectionPending_ = false;
  draggingSelection_ = false;
  updateCodeLanguageEditor();
  updateTableToolbar();
  viewport()->update();
}

void EditorView::setCodeLanguageSuggestions(QStringList languages) {
  if (codeLanguageEditor_) {
    codeLanguageEditor_->setSuggestions(std::move(languages));
  }
}

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

void EditorView::setFocusMode(bool enabled) {
  focusMode_ = enabled;
  viewport()->update();
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

HitTestResult EditorView::hitTest(QPointF viewportPos) const {
  if (!layout_) {
    return {};
  }
  HitTestResult hit = layout_->hitTest(QPointF(viewportPos.x(), viewportPos.y() + scrollY()), theme_);
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

void EditorView::paintEvent(QPaintEvent* event) {
  PerfTimer perf("view.paint");
  Q_UNUSED(event);

  QPainter painter(viewport());
  painter.fillRect(viewport()->rect(), theme_.viewportBackgroundColor());

  if (!layout_) {
    return;
  }

  const QRectF page = layout_->pageRect(theme_, viewport()->height()).translated(0, -scrollY());
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  if (theme_.pageShadowColor().isValid() && theme_.pageShadowBlur() > 0.0) {
    // CSS box-shadow blur is a Gaussian falloff. We approximate it with N
    // concentric rounded-rect shells, each grown further out and carrying an
    // equal slice of the peak alpha. Drawn outer-first, the overdraw builds a
    // soft core at the offset position and fades to nothing ~blur px out —
    // instead of one hard-edged rect that, behind a translucent paper (e.g.
    // mist-blue's rgba(248,250,253,0.58) card), read as a second panel offset
    // below the card.
    QColor base = theme_.pageShadowColor();
    base.setAlpha(qMin(base.alpha(), 42));
    const qreal peakAlphaF = base.alphaF();
    const qreal blur = theme_.pageShadowBlur();
    const qreal offsetY = theme_.pageShadowOffsetY();
    const qreal r = theme_.pageBorderRadius();
    const QRectF core = page.translated(0, offsetY);
    painter.setPen(Qt::NoPen);
    constexpr int kLayers = 8;
    for (int i = kLayers; i >= 1; --i) {
      const qreal grow = blur * (i / qreal(kLayers));
      QColor shell = base;
      shell.setAlphaF(peakAlphaF / kLayers);
      painter.setBrush(shell);
      painter.drawRoundedRect(core.adjusted(-grow, -grow, grow, grow), r + grow, r + grow);
    }
  }
  painter.setBrush(theme_.pageBackgroundColor());
  if (theme_.pageBorderColor().isValid() && theme_.pageBorderWidth() > 0.0) {
    painter.setPen(QPen(theme_.pageBorderColor(), theme_.pageBorderWidth()));
  } else {
    painter.setPen(Qt::NoPen);
  }
  const qreal radius = theme_.pageBorderRadius();
  painter.drawRoundedRect(page, radius, radius);
  // CSS #write::before full-page texture overlay (e.g. phycat's faint dot grid):
  // painted over the card, clipped to it, under the text.
  painter.save();
  painter.setClipRect(page);
  DecorationPainter::paintWriteTexture(painter, theme_, page);
  painter.restore();
  painter.restore();

  ensureVisibleBuilt();

  const QRectF visible = documentViewportRect();
  const QVector<const BlockLayout*> blocks = layout_->visibleBlocks(visible.adjusted(0, -80, 0, 80), theme_);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const NodeId activeTopLevel =
      (focusMode_ && cursorPosition_.isValid()) ? layout_->topLevelBlockIdFor(cursorPosition_.blockId) : NodeId();

  // Tell the keyframe driver which animated hosts are visible (it filters to
  // infinite animations and starts/stops its timer accordingly).
  if (keyframeAnimator_ && keyframeAnimator_->hasAnimations()) {
    QSet<QString> visibleHosts;
    for (const BlockLayout* b : blocks) {
      const QString h = hostKeyForBlock(*b);
      if (!h.isEmpty()) { visibleHosts.insert(h); }
    }
    keyframeAnimator_->setVisibleHosts(visibleHosts);
  }

  for (const BlockLayout* block : blocks) {
    const QString host = hostKeyForBlock(*block);
    const AnimatedSample* anim = (keyframeAnimator_ && !host.isEmpty()) ? keyframeAnimator_->sampleFor(host) : nullptr;
    // CSS :hover box-shadow glow (phase-animated by HoverAnimator).
    if (hoverAnimator_ && block->nodeId() == hoverAnimator_->animatedBlockId() && hoverAnimator_->phase() > 0.0) {
      if (!host.isEmpty()) {
        DecorationPainter::paintBlockHoverGlow(painter, theme_, host,
                                               block->rect().translated(0, -scrollY()), hoverAnimator_->phase());
      }
    }
    // @keyframes glow (colour/blur from the sampled frame).
    if (anim && anim->hasGlow) {
      DecorationPainter::paintGlow(painter, block->rect().translated(0, -scrollY()), anim->glowColor, anim->glowBlur, 1.0);
    }
    const bool wrap = anim && (anim->hasOpacity || (anim->hasScale && qAbs(anim->scale - 1.0) > 0.001));
    if (wrap) {
      painter.save();
      if (anim->hasOpacity) { painter.setOpacity(anim->opacity); }
      if (anim->hasScale) {
        const QRectF br = block->rect().translated(0, -scrollY());
        const QPointF c = br.center();
        painter.translate(c);
        painter.scale(anim->scale, anim->scale);
        painter.translate(-c);
      }
    }
    if (focusMode_ && activeTopLevel.isValid() && block->nodeId() != activeTopLevel) {
      painter.save();
      painter.setOpacity(0.35);
      block->paint(painter, theme_, scrollY(), codeFenceScroll_);
      painter.restore();
    } else {
      block->paint(painter, theme_, scrollY(), codeFenceScroll_);
    }
    if (wrap) { painter.restore(); }
  }
  paintSelection(painter);
  paintCurrentTableCell(painter);
  paintHtmlHoverOverlay(painter);
  paintInsertionCursor(painter);
  paintHeadingBadge(painter);
}

bool EditorView::event(QEvent* event) {
  if (event->type() == QEvent::Leave) {
    clearHtmlHover();
    hoveredBlockId_ = NodeId();
    if (hoverAnimator_) { hoverAnimator_->setHovered(NodeId(), 0.0); }
  }
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_A && keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
      if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
      }
      return QAbstractScrollArea::event(event);
    }
    if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
      event->accept();
      if (event->type() == QEvent::ShortcutOverride) {
        return true;
      }
      return QAbstractScrollArea::event(event);
    }
  }
  return QAbstractScrollArea::event(event);
}

void EditorView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  bool relayouted = false;
  if (layout_ && document_) {
    layout_->setEditingHtmlBlock(editingHtmlBlockId_);
    const int oldValue = verticalScrollBar()->value();
    relayouted = layout_->relayoutForViewportWidth(theme_, viewport()->width());
    if (relayouted) {
      updateScrollBars();
      verticalScrollBar()->setValue(qBound(verticalScrollBar()->minimum(), oldValue, verticalScrollBar()->maximum()));
      updateCursorHitFromPosition();
      viewport()->update();
    }
  }
  if (!relayouted) {
    rebuildLayout();
  }
  updateCodeLanguageEditor();
  updateTableToolbar();
  if (typewriterMode_ && cursorHit_.isValid()) {
    scrollToCursorCentered();
  }
}

void EditorView::wheelEvent(QWheelEvent* event) {
  const bool horizontal = qAbs(event->angleDelta().x()) >= qAbs(event->angleDelta().y());
  // Shift+wheel or a trackpad horizontal gesture scrolls the code fence under the cursor; plain
  // vertical wheel always scrolls the document (unchanged).
  if ((event->modifiers().testFlag(Qt::ShiftModifier) || horizontal) && scrollCodeFenceHorizontally(event, horizontal)) {
    stopScrollAnimation();
    event->accept();
    updateCodeLanguageEditor();
    updateTableToolbar();
    return;
  }
  stopScrollAnimation();
  QAbstractScrollArea::wheelEvent(event);
  updateCodeLanguageEditor();
  updateTableToolbar();
}

void EditorView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    setFocus(Qt::MouseFocusReason);
    const NodeId htmlToggleBlockId = editingHtmlBlockId_.isValid() ? editingHtmlBlockId_ : htmlHover_.visibleBlockId();
    if (htmlHoverButtonViewportRect().contains(event->position()) && htmlToggleBlockId.isValid()) {
      emit htmlEditToggleRequested(htmlToggleBlockId);
      event->accept();
      return;
    }
    const HitTestResult hit = hitTest(event->position());
    // Click anywhere on a rendered HTML block to enter source editing mode.
    // When already editing this block, fall through to normal cursor placement.
    if (hit.isValid() && hit.zone == HitTestResult::Zone::Html && editingHtmlBlockId_ != hit.blockId) {
      emit htmlEditToggleRequested(hit.blockId);
      event->accept();
      return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier) && hit.isValid() && !hit.linkHref.isEmpty()) {
      QDesktopServices::openUrl(resolvedUrlForDocumentResource(hit.linkHref, documentPath_));
      event->accept();
      return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier) && hit.isValid() && !hit.imageSrc.isEmpty()) {
      QDesktopServices::openUrl(resolvedUrlForDocumentResource(hit.imageSrc, documentPath_));
      event->accept();
      return;
    }
    if (hit.isValid() && hit.taskCheckboxHit) {
      emit taskCheckboxToggled(hit.blockId);
      event->accept();
      return;
    }
    if (hit.isValid() && hit.zone == HitTestResult::Zone::CodeHorizontalBar) {
      // Dragging the code-fence horizontal scrollbar: jump to the click and track the drag.
      codeFenceScrollDragId_ = hit.blockId;
      dragCodeFenceScrollBarTo(hit.blockId, event->position());
      event->accept();
      return;
    }
    if (event->modifiers().testFlag(Qt::ShiftModifier) && hit.isValid() && isDragSelectableZone(hit.zone) && cursorPosition_.isValid()) {
      SelectionRange range;
      range.anchor = selection_.anchor.isValid() ? selection_.anchor : cursorPosition_;
      range.focus = hit.cursorPosition();
      setSelectionRange(range);
      emit selectionChanged(range, hit);
      event->accept();
      return;
    }
    setCursorHit(hit);
    emit blockClicked(hit);
    updateCodeLanguageEditor();
    if (hit.isValid() && isDragSelectableZone(hit.zone)) {
      preDragSelection_ = selection_;
      dragSelectionPending_ = true;
      draggingSelection_ = false;
      dragStartViewportPos_ = event->position();
      dragAnchorHit_ = hit;
    }
  }
  QAbstractScrollArea::mousePressEvent(event);
}

void EditorView::mouseMoveEvent(QMouseEvent* event) {
  if (codeFenceScrollDragId_.isValid() && (event->buttons() & Qt::LeftButton)) {
    dragCodeFenceScrollBarTo(codeFenceScrollDragId_, event->position());
    event->accept();
    return;
  }
  if ((dragSelectionPending_ || draggingSelection_) && (event->buttons() & Qt::LeftButton)) {
    if (!draggingSelection_) {
      draggingSelection_ = true;
      dragSelectionPending_ = false;
    }
    if (draggingSelection_) {
      updateDragSelection(event->position());
      event->accept();
      return;
    }
  }
  updateHtmlHover(event->position());
  updateBlockHover(event->position());
  updateMouseCursor(event->position());
  QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && codeFenceScrollDragId_.isValid()) {
    codeFenceScrollDragId_ = NodeId();
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && (dragSelectionPending_ || draggingSelection_)) {
    const bool wasDragging = draggingSelection_;
    if (wasDragging) {
      updateDragSelection(event->position());
    }
    dragSelectionPending_ = false;
    draggingSelection_ = false;
    if (wasDragging) {
      refreshInlineProjectionForSelectionChange(preDragSelection_);
    }
    event->accept();
    return;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

void EditorView::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    return;
  }

  setFocus(Qt::MouseFocusReason);
  const HitTestResult hit = hitTest(event->position());
  if (!hit.isValid() || !isSelectableZone(hit.zone)) {
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    return;
  }

  // Find the block layout to get the text for word boundary detection.
  const BlockLayout* block = layout_ ? layout_->block(hit.blockId, theme_) : nullptr;
  if (!block) {
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    return;
  }

  QString text;
  if (hit.zone == HitTestResult::Zone::TableCell) {
    const auto& rows = block->tableRows();
    if (hit.tableRow >= 0 && hit.tableRow < static_cast<int>(rows.size()) &&
        hit.tableColumn >= 0 && hit.tableColumn < static_cast<int>(rows[hit.tableRow].cells.size())) {
      const InlineLayout* cellLayout = &rows[hit.tableRow].cells[hit.tableColumn].text;
      text = cellLayout->visibleText();
    }
  } else if (hit.zone == HitTestResult::Zone::Text) {
    if (const InlineLayout* inlineLayout = block->inlineLayout()) {
      text = inlineLayout->visibleText();
    }
  } else {
    // Code, Math, Html, FrontMatter — literal text.
    text = block->literal();
  }

  if (text.isEmpty()) {
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    return;
  }

  const auto [wordStart, wordEnd] = wordRangeAtOffset(text, hit.textOffset);
  if (wordStart >= wordEnd) {
    QAbstractScrollArea::mouseDoubleClickEvent(event);
    return;
  }

  // Build selection from word boundaries.
  CursorPosition anchor = hit.cursorPosition();
  anchor.text.textOffset = wordStart;
  anchor.text.sourceOffset = -1;

  CursorPosition focus = hit.cursorPosition();
  focus.text.textOffset = wordEnd;
  focus.text.sourceOffset = -1;

  SelectionRange range;
  range.anchor = anchor;
  range.focus = focus;

  applySelectionRange(range);
  emit blockClicked(hit);
  emit selectionChanged(range, hit);

  // Prevent drag from overriding the word selection.
  dragSelectionPending_ = false;
  draggingSelection_ = false;

  event->accept();
}

void EditorView::contextMenuEvent(QContextMenuEvent* event) {
  const HitTestResult hit = hitTest(QPointF(event->pos()));

  // Standard editor behavior: move the caret to the click so caret-based commands
  // (format / paragraph / table / image) target the clicked location. A click on
  // a link / image / table cell always repositions (the intent is to act on that
  // object); a plain-text click leaves an existing selection intact when it lands
  // on it, so Cut/Copy still act on the selection. The menu itself (spell
  // suggestions + command sections) is assembled by MainWindow from the registry.
  const bool onObject = !hit.linkHref.isEmpty() || !hit.imageSrc.isEmpty() || hit.zone == HitTestResult::Zone::TableCell;
  const bool placeable = hit.isValid() && isSelectableZone(hit.zone);
  if (placeable && (onObject || !selectionContainsViewportPoint(hit, QPointF(event->pos())))) {
    setCursorHit(hit);
    emit blockClicked(hit);
  }

  emit contextMenuRequested(hit, event->globalPos());
}

bool EditorView::selectionContainsViewportPoint(const HitTestResult& hit, QPointF viewportPos) const {
  if (!layout_ || !hit.isValid() || selection_.isCollapsed()) {
    return false;
  }
  const NodeId clickTop = layout_->topLevelBlockIdFor(hit.blockId);
  const BlockLayout* block = layout_->block(clickTop, theme_);
  if (!block) {
    return false;
  }

  QVector<QRectF> rects;
  if (selection_.isSingleBlock()) {
    if (clickTop != selection_.focus.blockId) {
      return false;  // click is in a different block than the (single) selected one
    }
    rects = block->selectionRects(selection_, theme_);
  } else {
    // Multi-block: BlockLayout::selectionRectsSelf bails on non-single-block
    // selections (BlockLayout.cpp:934), so selectionRects() returns nothing and
    // the naive check would always say "not in selection" — collapsing the
    // selection on right-click. Compute the per-block selected [start, end]
    // range exactly as paintSelection does, then use selectionRectsForOffsets.
    const NodeId anchorBlock = selection_.anchor.blockId;
    const NodeId focusBlock = selection_.focus.blockId;
    const QVector<const BlockLayout*> selectedBlocks = blocksBetween(*layout_, anchorBlock, focusBlock);
    bool inSelection = false;
    for (const BlockLayout* sb : selectedBlocks) {
      if (sb && sb->nodeId() == clickTop) {
        inSelection = true;
        break;
      }
    }
    if (!inSelection) {
      return false;
    }
    const bool anchorFirst = blockComesBefore(*layout_, anchorBlock, focusBlock);
    qsizetype start = 0;
    qsizetype end = selectableLength(block);
    const bool isAnchor = clickTop == anchorBlock;
    const bool isFocus = clickTop == focusBlock;
    const bool isTable = block->type() == BlockType::Table;
    if (isAnchor) {
      if (anchorFirst) {
        start = isTable ? 0 : selection_.anchor.text.textOffset;
      } else {
        end = isTable ? selectableLength(block) : selection_.anchor.text.textOffset;
      }
    }
    if (isFocus) {
      if (anchorFirst) {
        end = isTable ? selectableLength(block) : selection_.focus.text.textOffset;
      } else {
        start = isTable ? 0 : selection_.focus.text.textOffset;
      }
    }
    rects = block->selectionRectsForOffsets(start, end, theme_);
  }

  const QPointF documentPos(viewportPos.x(), viewportPos.y() + scrollY());
  for (const QRectF& r : rects) {
    if (r.contains(documentPos)) {
      return true;
    }
  }
  return false;
}

void EditorView::inputMethodEvent(QInputMethodEvent* event) {
  if (!event->commitString().isEmpty()) {
    emit textCommitted(event->commitString());
  }
  event->accept();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle) {
    // effectiveCursorRect matches where the caret is painted (offset-adjusted for a scrolled code
    // fence); without it the IME panel anchored at the natural advance, far from the visible caret.
    QRectF cursor = effectiveCursorRect();
    cursor.translate(0, -scrollY());
    return cursor.toRect();
  }
  return QAbstractScrollArea::inputMethodQuery(query);
}

void EditorView::rebuildLayout() {
  PerfTimer perf("view.rebuildLayout");
  if (!layout_) {
    layout_ = std::make_unique<DocumentLayout>();
  }
  // (Re)wire the code-fence scroll controller. The no-document branch below hands back a fresh
  // layout_, which drops the controller attach() set on the previous one. With a null controller,
  // DocumentLayout::hitTest maps clicks at offset 0 while the paint path (which reads the view's
  // codeFenceScroll_ directly) scrolls the text — so drag selections desynchronized from the text,
  // the shift growing with the horizontal scroll offset.
  layout_->setCodeFenceScroll(codeFenceScroll_);

  if (document_) {
    layout_->setEditingHtmlBlock(editingHtmlBlockId_);
    // Lazy layout builds only cheap height estimates up front; visible and caret blocks are
    // promoted to full detail on demand. Fall back to Eager under the offscreen test platform
    // (no real paint/scroll cycle drives ensureVisibleBuilt), keeping render/view tests unchanged.
    const bool lazy = QApplication::platformName() != QLatin1String("offscreen");
    {
      // Pin by node id: a full Lazy rebuild re-estimates every slot then re-promotes the visible
      // ones, churning totalHeight twice. The captured block's new slotTop re-derives a value that
      // keeps it on screen — pinning the old integer value (the prior approach) was meaningless
      // once every height re-estimated.
      ScopedViewportPin pin(*this);
      layout_->rebuild(*document_, theme_, viewport()->width(), selection_, documentPath_,
                       lazy ? DocumentLayout::BuildPolicy::Lazy : DocumentLayout::BuildPolicy::Eager);
      if (cursorPosition_.isValid()) {
        const qsizetype caretIdx = layout_->topLevelIndexFor(cursorPosition_.blockId);
        if (caretIdx >= 0) {
          layout_->ensureBuilt(caretIdx, caretIdx, theme_);  // caret block needed for hit resolution
        }
      }
    }
    ensureVisibleBuilt();  // promote the visible window; self-pinned so the promotion snap is invisible
    if (cursorPosition_.isValid()) {
      cursorHit_ = hitForCursorPosition(*layout_, theme_, cursorPosition_);
      cursorVisible_ = cursorHit_.isValid();
      ensureCodeFenceCursorVisible();
    }
  } else {
    layout_ = std::make_unique<DocumentLayout>();
    layout_->setCodeFenceScroll(codeFenceScroll_);
    updateScrollBars();
  }
  updateCodeLanguageEditor();
  updateTableToolbar();
  viewport()->update();
}

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

bool EditorView::refreshVisibleBlocks(const MarkdownDocument& document) {
  if (!layout_) {
    return false;
  }
  return refreshBlocks(layout_->promotedTopLevelIds(), document);
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
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
      "QScrollBar:horizontal { background:%1; height:8px; margin:0; }"
      "QScrollBar::handle:horizontal { background:#b7b7b7; min-width:54px; border-radius:3px; margin:2px 1px; }"
      "QScrollBar::handle:horizontal:hover { background:#999999; }"
      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; border:0; background:transparent; }"
      "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }")
                    .arg(background));
}

void EditorView::updateCodeLanguageEditor() {
  if (!codeLanguageEditor_) {
    return;
  }
  codeLanguageEditor_->update(cursorPosition_, cursorHit_, layout_.get(), document_.data(), scrollY(), viewport()->height());
}

void EditorView::updateTableToolbar() {
  if (!tableToolbar_) {
    return;
  }
  tableToolbar_->update(cursorHit_, layout_.get(), scrollY(), viewport()->rect());
}

void EditorView::updateCursorHitFromPosition() {
  cursorHit_ = hitForCursorPosition(*layout_, theme_, cursorPosition_);
  cursorVisible_ = cursorHit_.isValid();
  updateCodeLanguageEditor();
  updateTableToolbar();
}

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

void EditorView::refreshInlineProjectionForSelectionChange(SelectionRange previousSelection) {
  QVector<NodeId> blockIds;
  addSelectionBlocks(blockIds, previousSelection);
  addSelectionBlocks(blockIds, selection_);

  // Revealing/hiding inline markdown markers reflows lines and changes block heights. Pin the
  // focus block (by node id) so the line the user clicked stays put instead of drifting when a
  // rebuilt block above it (e.g. the block the cursor just left) resizes. Captured before the
  // rebuild and restored after; a no-op when nothing above the focus block moved. refreshBlocks
  // also pins internally on the topmost visible block — this explicit focus pin is applied last, so
  // it governs the final position.
  const ViewportAnchor anchor = captureViewportAnchor(cursorPosition_.blockId);

  bool refreshed = false;
  if (document_ && layout_ && !blockIds.isEmpty()) {
    refreshed = refreshBlocks(blockIds, *document_);
  }
  const bool scrolled = refreshed && restoreViewportAnchor(anchor);

  if (!refreshed) {
    updateCursorHitFromPosition();
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
    viewport()->update();
  } else {
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
    if (scrolled) {
      viewport()->update();  // content shifted under a new scroll offset — repaint coherently
    }
    // Erase old heading badge if cursor moved away from a heading block
    if (previousSelection.focus.blockId != cursorPosition_.blockId) {
      const QRectF oldBadge = headingBadgeViewportRectForBlock(previousSelection.focus.blockId);
      if (!oldBadge.isEmpty()) {
        viewport()->update(oldBadge.adjusted(-2, -2, 2, 2).toAlignedRect());
      }
    }
  }
}

void EditorView::addSelectionBlocks(QVector<NodeId>& blockIds, const SelectionRange& selection) const {
  auto add = [&blockIds](NodeId id) {
    if (id.isValid() && !blockIds.contains(id)) {
      blockIds.push_back(id);
    }
  };
  add(selection.anchor.blockId);
  add(selection.focus.blockId);
}

void EditorView::paintSelection(QPainter& painter) const {
  if (!layout_ || selection_.isCollapsed()) {
    return;
  }

  const QColor color(79, 143, 247, 72);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);

  if (selection_.isSingleBlock()) {
    const BlockLayout* block = layout_->blockIfPromoted(selection_.focus.blockId);
    if (block) {
      paintSelectionRectsForBlock(painter, block, block->selectionRects(selection_, theme_));
    }
  } else {
    const bool anchorFirst = blockComesBefore(*layout_, selection_.anchor.blockId, selection_.focus.blockId);
    const QVector<const BlockLayout*> selectedBlocks = blocksBetween(*layout_, selection_.anchor.blockId, selection_.focus.blockId);
    for (qsizetype i = 0; i < selectedBlocks.size(); ++i) {
      const BlockLayout* block = selectedBlocks.at(i);
      if (!block) {
        continue;
      }
      const bool isAnchor = block->nodeId() == selection_.anchor.blockId;
      const bool isFocus = block->nodeId() == selection_.focus.blockId;
      qsizetype start = 0;
      qsizetype end = selectableLength(block);
      if (isAnchor) {
        if (anchorFirst) {
          start = block->type() == BlockType::Table ? 0 : selection_.anchor.text.textOffset;
        } else {
          end = block->type() == BlockType::Table ? selectableLength(block) : selection_.anchor.text.textOffset;
        }
      }
      if (isFocus) {
        if (anchorFirst) {
          end = block->type() == BlockType::Table ? selectableLength(block) : selection_.focus.text.textOffset;
        } else {
          start = block->type() == BlockType::Table ? 0 : selection_.focus.text.textOffset;
        }
      }
      // Self-only rects: blocksBetween already lists every block (containers and descendants) once,
      // so we must NOT recurse here — recursing would repaint each nested item once per owning
      // ancestor (a darker double-highlighted band) and smear this block's offsets onto its children.
      paintSelectionRectsForBlock(painter, block, block->selectionRectsSelfForOffsets(start, end, theme_));
    }
  }
  painter.restore();
}

void EditorView::paintSelectionRectsForBlock(QPainter& painter, const BlockLayout* block, const QVector<QRectF>& documentRects) const {
  if (block == nullptr) {
    return;
  }
  const bool scrollable = isScrollableCodeFence(block);
  qreal offset = 0.0;
  QRectF clipViewport;  // empty unless scrollable; the visible text window minus the scrollbar strip
  if (scrollable && codeFenceScroll_ != nullptr) {
    offset = codeFenceScroll_->offsetFor(block->nodeId());
    const qreal stripH = BlockLayout::scrollBarStripHeight(theme_);
    const QRectF textDocument = block->literalContentRect(theme_).adjusted(0, 0, 0, -stripH);
    clipViewport = textDocument.translated(0, -scrollY());
  }
  for (QRectF rect : documentRects) {
    if (scrollable) {
      rect.translate(-offset, 0.0);  // match the content's translate(-offset) in paintCodeFence
    }
    rect.translate(0, -scrollY());
    if (scrollable) {
      rect = rect.intersected(clipViewport);
      if (rect.isEmpty()) {
        continue;  // selection scrolled out of the visible window
      }
    }
    painter.drawRoundedRect(rect, 2, 2);
  }
}

bool EditorView::isScrollableCodeFence(const BlockLayout* block) const {
  if (block == nullptr || block->type() != BlockType::CodeFence) {
    return false;
  }
  // Mirrors paintCodeFence's predicate: wrap off (implied by maxLineWidth being meaningful) and a
  // line wider than the content area.
  return block->codeMaxLineWidth() > block->literalContentRect(theme_).width() + 0.5;
}

void EditorView::paintCurrentTableCell(QPainter& painter) const {
  if (!layout_ || cursorHit_.zone != HitTestResult::Zone::TableCell || cursorHit_.tableRow < 0 || cursorHit_.tableColumn < 0) {
    return;
  }

  const BlockLayout* table = layout_->blockIfPromoted(cursorHit_.blockId);
  if (!table || table->type() != BlockType::Table) {
    return;
  }

  QRectF rect = table->tableCellRect(cursorHit_.tableRow, cursorHit_.tableColumn);
  if (rect.isEmpty()) {
    return;
  }
  rect.translate(0, -scrollY());

  painter.save();
  painter.setPen(QPen(theme_.linkColor(), 1.4));
  painter.setBrush(QColor(79, 143, 247, 28));
  painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
  painter.restore();
}

void EditorView::paintInsertionCursor(QPainter& painter) const {
  if (!cursorVisible_ || !cursorHit_.isValid()) {
    lastPaintedCaretDocumentRect_ = {};
    return;
  }

  // effectiveCursorRect subtracts a scrollable code fence's horizontal offset, so the caret lands on
  // the translated character: the text is painted with painter.translate(-offset), so without this
  // the caret sat at the natural advance and was clipped out of view whenever the fence scrolled.
  QRectF cursor = effectiveCursorRect();
  lastPaintedCaretDocumentRect_ = cursor;
  cursor.translate(0, -scrollY());

  if (!viewport()->rect().adjusted(-4, -4, 4, 4).intersects(cursor.toAlignedRect())) {
    return;
  }

  // The cap only guards image/preview lines, whose line height can be hundreds
  // of px; text lines — including large headings (a 2.1rem H1 is ~38px) — must
  // use their natural line height, or the caret is clipped short and looks
  // vertically offset from the glyphs.
  const qreal height = qBound<qreal>(14.0, cursor.height(), 96.0);
  QRectF visibleCursor(cursor.left(), cursor.top(), 1.5, height);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(theme_.linkColor());
  painter.drawRect(visibleCursor);
  painter.restore();
}

QRectF EditorView::headingBadgeViewportRectForBlock(NodeId blockId) const {
  return headingBadgeForBlock(blockId).viewportRect;
}

EditorView::HeadingBadge EditorView::headingBadgeForBlock(NodeId blockId) const {
  if (!layout_ || !blockId.isValid()) {
    return {};
  }

  const NodeId topId = layout_->topLevelBlockIdFor(blockId);
  if (!topId.isValid()) {
    return {};
  }

  const BlockLayout* block = layout_->blockIfPromoted(topId);
  if (!block || block->type() != BlockType::Heading || block->headingLevel() < 3 || block->headingLevel() > 6) {
    return {};
  }

  const int level = block->headingLevel();
  QFont badgeFont = theme_.paragraphFont();
  badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.8);
  const QFontMetricsF metrics(badgeFont);
  const qreal badgeWidth = metrics.horizontalAdvance(QStringLiteral("H%1").arg(level)) + 6.0;
  const qreal badgeHeight = metrics.height() + 2.0;
  const QRectF blockRect = block->rect();
  const QFontMetricsF headingMetrics(theme_.headingFont(level));
  const qreal lineCenterY = blockRect.top() + headingMetrics.height() / 2.0;
  const qreal badgeY = lineCenterY - badgeHeight / 2.0 - scrollY();
  return HeadingBadge{topId, QRectF(blockRect.left() - badgeWidth - 4.0, badgeY, badgeWidth, badgeHeight), level};
}

void EditorView::paintHeadingBadge(QPainter& painter) const {
  const HeadingBadge badge = headingBadgeForBlock(cursorPosition_.blockId);
  if (!badge.isValid()) {
    return;
  }

  if (!viewport()->rect().intersects(badge.viewportRect.toAlignedRect())) {
    return;
  }

  const QString badgeText = QStringLiteral("H%1").arg(badge.level);

  painter.save();
  painter.setPen(QPen(theme_.codeBorderColor(), 1));
  painter.setBrush(theme_.backgroundColor());
  painter.drawRoundedRect(badge.viewportRect, 3.0, 3.0);
  QFont badgeFont = theme_.paragraphFont();
  badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.8);
  painter.setFont(badgeFont);
  painter.setPen(QColor(theme_.mutedTextColor().red(), theme_.mutedTextColor().green(), theme_.mutedTextColor().blue(), 140));
  painter.drawText(badge.viewportRect, Qt::AlignCenter, badgeText);
  painter.restore();
}

HtmlBlockHoverController::Inputs EditorView::htmlHoverInputs() const {
  return {layout_.get(), &theme_, editingHtmlBlockId_, scrollY()};
}

QRectF EditorView::htmlHoverButtonViewportRect() const {
  return htmlHover_.buttonViewportRect(htmlHoverInputs());
}

void EditorView::paintHtmlHoverOverlay(QPainter& painter) const {
  htmlHover_.paint(painter, htmlHoverInputs(), viewport()->rect());
}

void EditorView::updateHtmlHover(QPointF viewportPos) {
  const HitTestResult hit = hitTest(viewportPos);
  const NodeId next = hit.isValid() && hit.zone == HitTestResult::Zone::Html ? hit.blockId : NodeId();
  const HtmlBlockHoverController::Dirty dirty = htmlHover_.setHoveredBlock(next, htmlHoverInputs());
  if (!dirty.oldRect.isEmpty()) {
    viewport()->update(dirty.oldRect);
  }
  if (!dirty.newRect.isEmpty()) {
    viewport()->update(dirty.newRect);
  }
}

void EditorView::clearHtmlHover() {
  const HtmlBlockHoverController::Dirty dirty = htmlHover_.clear(htmlHoverInputs());
  if (!dirty.oldRect.isEmpty()) {
    viewport()->update(dirty.oldRect);
  }
}

void EditorView::updateBlockHover(QPointF viewportPos) {
  if (!layout_ || !hoverAnimator_) { return; }
  const HitTestResult hit = hitTest(viewportPos);
  const NodeId next = hit.isValid() ? layout_->topLevelBlockIdFor(hit.blockId) : NodeId();
  if (next == hoveredBlockId_) { return; }
  hoveredBlockId_ = next;
  // Drive the animator only for hosts that actually declare a hover glow.
  qreal duration = 0.0;
  if (next.isValid()) {
    if (const BlockLayout* blk = layout_->blockIfPromoted(next)) {
      duration = hoverTransitionMs(hostKeyForBlock(*blk));
    }
  }
  hoverAnimator_->setHovered(next, duration);
}

void EditorView::repaintHoverBlock(NodeId blockId) {
  if (!layout_ || !blockId.isValid()) { return; }
  const BlockLayout* blk = layout_->blockIfPromoted(blockId);
  if (!blk) { return; }
  const QRectF r = blk->rect().translated(0, -scrollY()).adjusted(-20, -20, 20, 20);
  viewport()->update(r.toAlignedRect());
}

qreal EditorView::hoverTransitionMs(const QString& host) const {
  if (host.isEmpty()) { return 0.0; }
  for (const TransitionSpec& t : theme_.decorations().transitions) {
    if (t.host == host) { return t.durationMs; }
  }
  return 0.0;
}

void EditorView::repaintAnimatedBlocks() {
  if (!layout_ || !keyframeAnimator_) { return; }
  const QRectF visible = documentViewportRect();
  const QVector<const BlockLayout*> blocks = layout_->visibleBlocks(visible.adjusted(0, -40, 0, 40), theme_);
  QRectF dirty;
  for (const BlockLayout* blk : blocks) {
    const QString host = hostKeyForBlock(*blk);
    if (!host.isEmpty() && keyframeAnimator_->sampleFor(host)) {
      dirty = dirty.isNull() ? blk->rect() : dirty.united(blk->rect());
    }
  }
  if (!dirty.isNull()) { viewport()->update(dirty.translated(0, -scrollY()).adjusted(-24, -24, 24, 24).toAlignedRect()); }
}

void EditorView::updateDragSelection(QPointF viewportPos) {
  if (!dragAnchorHit_.isValid()) {
    return;
  }

  const HitTestResult focusHit = hitTest(viewportPos);
  if (!focusHit.isValid() || !isDragSelectableZone(focusHit.zone)) {
    return;
  }

  SelectionRange range;
  range.anchor = dragAnchorHit_.cursorPosition();
  range.focus = focusHit.cursorPosition();
  if (range.isCollapsed()) {
    return;
  }
  applySelectionRange(range);
  emit selectionChanged(range, focusHit);
}

void EditorView::updateMouseCursor(QPointF viewportPos) {
  if (htmlHoverButtonViewportRect().contains(viewportPos)) {
    viewport()->setCursor(Qt::PointingHandCursor);
    return;
  }
  const HitTestResult hit = hitTest(viewportPos);
  if (hit.isValid() && !hit.linkHref.isEmpty()) {
    viewport()->setCursor(Qt::PointingHandCursor);
  } else if (hit.isValid() && !hit.imageSrc.isEmpty()) {
    viewport()->setCursor(Qt::PointingHandCursor);
  } else if (hit.isValid() &&
      (hit.zone == HitTestResult::Zone::Text || hit.zone == HitTestResult::Zone::Code || hit.zone == HitTestResult::Zone::Math ||
       hit.zone == HitTestResult::Zone::Html || hit.zone == HitTestResult::Zone::FrontMatter || hit.zone == HitTestResult::Zone::TableCell ||
       hit.zone == HitTestResult::Zone::BlockAfter)) {
    viewport()->setCursor(Qt::IBeamCursor);
  } else {
    viewport()->unsetCursor();
  }
}

void EditorView::dragEnterEvent(QDragEnterEvent* event) {
  // Accept any local file/folder; dropEvent routes by type. Only image drops are
  // inserted inline — everything else is handed to the main window.
  if (event->mimeData()->hasUrls()) {
    for (const QUrl& url : event->mimeData()->urls()) {
      if (url.isLocalFile()) {
        event->acceptProposedAction();
        return;
      }
    }
  }
  event->ignore();
}

void EditorView::dragMoveEvent(QDragMoveEvent* event) {
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}

void EditorView::dropEvent(QDropEvent* event) {
  if (!event->mimeData()->hasUrls()) {
    event->ignore();
    return;
  }
  const auto urls = event->mimeData()->urls();
  if (urls.isEmpty()) {
    event->ignore();
    return;
  }
  const QUrl url = urls.first();
  if (!url.isLocalFile()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
  const QString filePath = url.toLocalFile();
  const QFileInfo info(filePath);
  const QString suffix = info.suffix().toLower();

  // Folders and non-image files are routed to the main window (open the file /
  // set the sidebar root / future import). Only images are inserted inline.
  if (info.isDir()) {
    emit folderDropped(filePath);
    return;
  }
  if (suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
      suffix == QStringLiteral("jpeg") || suffix == QStringLiteral("gif") ||
      suffix == QStringLiteral("svg") || suffix == QStringLiteral("webp") ||
      suffix == QStringLiteral("bmp") || suffix == QStringLiteral("ico") ||
      suffix == QStringLiteral("tiff") || suffix == QStringLiteral("tif")) {
    const QString alt = info.baseName();
    emit textCommitted(QStringLiteral("![%1](%2)").arg(alt, filePath));
    return;
  }
  if (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") ||
      suffix == QStringLiteral("mdown") || suffix == QStringLiteral("txt")) {
    emit markdownFileDropped(filePath);
    return;
  }
  emit importableFileDropped(filePath);
}

}  // namespace muffin
