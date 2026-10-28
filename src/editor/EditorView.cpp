#include "editor/EditorView.h"
#include "editor/ShortcutSettings.h"
#include "diagnostics/ViewportUpdate.h"

#include "diagnostics/ScopedPerfProbe.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "document/MarkdownDocument.h"
#include "editor/CodeLanguageEditor.h"
#include "editor/EditorKeyRouting.h"
#include "editor/EditorViewGeometry.h"
#include "editor/HtmlBlockHoverController.h"
#include "editor/ResourceUrl.h"
#include "editor/TableToolbar.h"
#include "io/FilePathOps.h"
#include "document/ImageSyntaxOps.h"
#include "document/LinkSyntaxOps.h"
#include "io/MuffinMime.h"
#include "render/DecorationPainter.h"
#include "render/ImageLoader.h"
#include "render/LayoutResources.h"
#include "render/KeyframeSampler.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include "render/TextLayout.h"
#include <QList>
#include <QMimeData>
#include <QPainter>
#include <QSet>
#include <QLoggingCategory>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <QTextCharFormat>
#include <QUrl>
#include <QKeyEvent>

namespace muffin {

// Pull the stateless geometry/hit-test helpers into scope so call sites read as
// before (isSelectableZone, selectableLength, …). The moved logic lives in
// editor_geometry; EditorView keeps only the stateful orchestration.
using namespace editor_geometry;

namespace {

Q_LOGGING_CATEGORY(viewPerf, "muffin.perf", QtWarningMsg)

bool sameCursorPosition(const CursorPosition& a, const CursorPosition& b) {
  return a.blockId == b.blockId && a.text.nodeId == b.text.nodeId && a.text.textOffset == b.text.textOffset &&
         a.text.sourceOffset == b.text.sourceOffset && a.text.inMeta == b.text.inMeta;
}

bool sameSelectionRange(const SelectionRange& a, const SelectionRange& b) {
  return sameCursorPosition(a.anchor, b.anchor) && sameCursorPosition(a.focus, b.focus);
}

// Image extensions whose drop inserts an inline ![alt](path) rather than a [name](path)
// link. Shared by the file-tree-drop and external-image-drop paths.
bool isImageSuffix(QStringView suffix) {
  static const QSet<QString> kImageSuffixes = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                               QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
                                               QStringLiteral("bmp"), QStringLiteral("ico"), QStringLiteral("tiff"),
                                               QStringLiteral("tif")};
  return kImageSuffixes.contains(suffix.toString().toLower());
}

struct PerfTimer : diag::ScopedPerfProbe {
  explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, viewPerf()) {}
};

} // namespace

EditorView::EditorView(QWidget* parent) : QAbstractScrollArea(parent), layout_(std::make_unique<DocumentLayout>()) {
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  retranslateAccessibility();
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  viewport()->setMouseTracking(true);
  viewport()->setAutoFillBackground(false);
  syncViewportBackground();
  layout_->setMermaidRenderCache(&mermaidCache_);
  connect(&LayoutResources::instance(), &LayoutResources::resourceChanged, this,
          [this](const QString&) { queueResourceRefresh(); });
  // Enable external drag-and-drop. acceptDrops defaults to false, so without
  // this the overridden drag*/dropEvent handlers are never delivered and drops
  // of folders/.md/.txt/images onto the window are silently ignored. The
  // viewport is the real drop target for a QAbstractScrollArea (its events are
  // forwarded to these handlers), so it must accept drops too.
  setAcceptDrops(true);
  viewport()->setAcceptDrops(true);
  setBackgroundRole(QPalette::Base);
  applyScrollBarStyle();
  cssPaintClock_.start();
  cssPaintTimer_ = new QTimer(this);
  cssPaintTimer_->setObjectName(QStringLiteral("cssAnimation"));
  cssPaintTimer_->setInterval(16);
  connect(cssPaintTimer_, &QTimer::timeout, this, [this] { diag::updateViewport(viewport()); });
  mermaidAnimationTimer_ = new QTimer(this);
  mermaidAnimationTimer_->setInterval(33);
  QObject::connect(mermaidAnimationTimer_, &QTimer::timeout, this, [this] { repaintAnimatedMermaidBlocks(); });
  // Idle-gated spinner driver: runs only while loading_ (set by setLoading), advancing the
  // bright wave around the dot ring and repainting. Costs nothing on a loaded document.
  loadingTimer_ = new QTimer(this);
  loadingTimer_->setInterval(33); // ~30 fps — smooth for a flowing dot ring
  QObject::connect(loadingTimer_, &QTimer::timeout, this, [this] {
    loadingPhase_ += 1.0 / 36.0; // ~1.2 s per revolution at 30 fps
    if (loadingPhase_ >= 1.0)
      loadingPhase_ -= 1.0;
    diag::updateViewport(viewport());
  });
  // Caret blink, unified with the source editor's cadence (cursorFlashTime/2). The timer only
  // runs while focused with an active caret and no composition; each toggle dirties nothing but
  // the caret rect. Offscreen tests never gain focus, so the caret stays deterministically on.
  cursorTimer_ = new QTimer(this);
  cursorTimer_->setObjectName(QStringLiteral("caretBlink"));
  cursorTimer_->setInterval(qMax(0, QApplication::cursorFlashTime() / 2));
  QObject::connect(cursorTimer_, &QTimer::timeout, this, [this] {
    caretBlinkOn_ = !caretBlinkOn_;
    QRect dirty = uniteDocumentRectDirty({}, lastPaintedCaretDocumentRect_, scrollY(), viewport()->size());
    dirty = uniteDocumentRectDirty(dirty, effectiveCursorRect(), scrollY(), viewport()->size());
    if (!dirty.isEmpty()) {
      diag::updateViewport(viewport(), dirty);
    }
  });
  // Selection drag auto-scroll: while the mouse is held beyond the viewport's edge, scroll by a
  // speed proportional to the overshoot and extend the selection at the clamped edge coordinate.
  dragScrollTimer_ = new QTimer(this);
  dragScrollTimer_->setInterval(50);
  QObject::connect(dragScrollTimer_, &QTimer::timeout, this, [this] {
    if (dragState_ != DragState::Dragging) {
      dragScrollTimer_->stop();
      return;
    }
    constexpr qreal kEdgeMargin = 16.0;
    const qreal height = static_cast<qreal>(viewport()->height());
    const qreal overshoot = lastDragViewportPos_.y() < height / 2.0
                                ? qMax(0.0, kEdgeMargin - lastDragViewportPos_.y())
                                : qMax(0.0, lastDragViewportPos_.y() - (height - kEdgeMargin));
    if (overshoot <= 0.0) {
      dragScrollTimer_->stop();
      return;
    }
    const qreal direction = lastDragViewportPos_.y() < height / 2.0 ? -1.0 : 1.0;
    const qreal step = qBound<qreal>(6.0, overshoot * 0.25, 28.0);
    verticalScrollBar()->setValue(static_cast<int>(qRound(verticalScrollBar()->value() + direction * step)));
    // Hit-test at the clamped edge: hitTest interprets viewport coordinates, so the selection
    // extends to whatever now sits at the edge.
    const QPointF clamped(qBound<qreal>(0.0, lastDragViewportPos_.x(), static_cast<qreal>(viewport()->width())),
                          qBound<qreal>(0.0, lastDragViewportPos_.y(), height));
    updateDragSelection(clamped);
  });

  codeLanguageEditor_ = new CodeLanguageEditor(viewport(), this);
  codeLanguageEditor_->setSuggestions({
      QStringLiteral("bash"),        QStringLiteral("c"),          QStringLiteral("cpp"),
      QStringLiteral("csharp"),      QStringLiteral("css"),        QStringLiteral("go"),
      QStringLiteral("html"),        QStringLiteral("ini"),        QStringLiteral("java"),
      QStringLiteral("javascript"),  QStringLiteral("json"),       QStringLiteral("kotlin"),
      QStringLiteral("lua"),         QStringLiteral("markdown"),   QStringLiteral("mermaid"),
      QStringLiteral("objective-c"), QStringLiteral("pascal"),     QStringLiteral("pegjs"),
      QStringLiteral("perl"),        QStringLiteral("perl6"),      QStringLiteral("pgp"),
      QStringLiteral("php"),         QStringLiteral("powershell"), QStringLiteral("python"),
      QStringLiteral("qml"),         QStringLiteral("r"),          QStringLiteral("ruby"),
      QStringLiteral("rust"),        QStringLiteral("sql"),        QStringLiteral("swift"),
      QStringLiteral("text"),        QStringLiteral("toml"),       QStringLiteral("typescript"),
      QStringLiteral("xml"),         QStringLiteral("yaml"),
  });
  connect(codeLanguageEditor_, &CodeLanguageEditor::languageCommitted, this, &EditorView::codeLanguageCommitted);

  tableToolbar_ = new TableToolbar(viewport(), this);
  connect(tableToolbar_, &TableToolbar::columnAlignmentRequested, this, &EditorView::tableColumnAlignmentRequested);
  connect(tableToolbar_, &TableToolbar::moreActionsRequested, this, &EditorView::tableMoreActionsRequested);
  connect(tableToolbar_, &TableToolbar::deleteRequested, this, &EditorView::tableDeleteRequested);
  connect(tableToolbar_, &TableToolbar::resizeRequested, this, &EditorView::tableResizeRequested);

  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
    clearHtmlHover();
    updateCodeLanguageEditor();
    updateTableToolbar();
  });
  connect(verticalScrollBar(), &QScrollBar::sliderPressed, this, [this] { stopScrollAnimation(); });

  viewportEstimateTimer_ = new QTimer(this);
  viewportEstimateTimer_->setObjectName(QStringLiteral("viewportEstimates"));
  viewportEstimateTimer_->setSingleShot(true);
  connect(viewportEstimateTimer_, &QTimer::timeout, this, &EditorView::refreshViewportEstimates);
  viewportPrefetchTimer_ = new QTimer(this);
  viewportPrefetchTimer_->setObjectName(QStringLiteral("viewportPrefetch"));
  viewportPrefetchTimer_->setSingleShot(true);
  connect(viewportPrefetchTimer_, &QTimer::timeout, this, &EditorView::prefetchViewport);
}

void EditorView::queueResourceRefresh() {
  resourcesDirty_ = true;
  scheduleResourceRefresh();
}

void EditorView::scheduleResourceRefresh() {
  if (!resourcesDirty_ || resourceRefreshQueued_ || !isVisible() || loading_ || !document_ || !layout_)
    return;
  resourceRefreshQueued_ = true;
  // Show must not synchronously rebuild the old theme: QStackedWidget shows a
  // page before currentChanged lets its owner install the deferred snapshot.
  // Keep one callback at most, and recheck visibility when it is dispatched.
  QTimer::singleShot(0, this, [this] {
    resourceRefreshQueued_ = false;
    refreshPendingResources();
  });
}

void EditorView::refreshPendingResources() {
  if (!resourcesDirty_ || !isVisible() || loading_ || !document_ || !layout_)
    return;
  PerfTimer perf("view.refreshResources");
  // A resource can finish while rebuilding (e.g. first diagram/font use). Clear
  // before work so a new notification survives and schedules another pass.
  resourcesDirty_ = false;
  const auto previousScroll = scrollY();
  DocumentLayout::ResourceRefreshResult result;
  {
    ScopedViewportPin pin(*this);
    result = layout_->refreshResources(theme_, selection_);
  }
  if (!result.updatedBlocks)
    return;
  blockBuiltAt_.clear();
  updateCursorHitFromPosition();
  updateTableToolbar();
  if (previousScroll != scrollY()) {
    diag::updateViewport(viewport());
  } else {
    auto dirty = result.dirty.translated(0, -scrollY()).adjusted(-3, -3, 3, 3).toAlignedRect();
    dirty = uniteDocumentRectDirty(dirty, lastPaintedCaretDocumentRect_, scrollY(), viewport()->size());
    dirty = uniteDocumentRectDirty(dirty, effectiveCursorRect(), scrollY(), viewport()->size());
    diag::updateViewport(viewport(), dirty);
  }
}

void EditorView::setDocument(const MarkdownDocument& document, QString documentPath) {
  cssPaintContext_.clear();
  cssPaintTimer_->stop();
  if (document_ != &document || documentPath_ != documentPath) {
    openSequenceMenus_.clear();
    viewportScrollRemainder_ = 0.0;
  }
  document_ = &document;
  documentPath_ = std::move(documentPath);
  blockBuiltAt_.clear(); // fresh document: every block's build stamp is stale
  rebuildLayout();
  updateTableToolbar();
}

void EditorView::setLoading(bool loading) {
  if (loading_ != loading) {
    loading_ = loading;
    if (loading_) {
      loadingPhase_ = 0.0; // each load animates from the top of the ring
      loadingTimer_->start();
    } else {
      loadingTimer_->stop();
      queueResourceRefresh(); // Also consume deferred updates if an open was cancelled.
    }
    diag::updateViewport(viewport());
  }
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
    const auto it = blockBuiltAt_.constFind(blockId);
    const bool current = it != blockBuiltAt_.constEnd() && sameSelectionRange(it.value().selection, selection_) &&
                         it.value().revision == document.revision();
    if (!current) {
      result = layout_->rebuildBlock(blockId, document, theme_, selection_);
      if (!result.rebuilt) {
        return false; // pin reconciles (nothing moved); caller falls back to setDocument
      }
      blockBuiltAt_[blockId] = {selection_, document.revision()};
    }
    // else: already built with the current selection + revision (the immediate selection refresh,
    // same keystroke) — skip the O(block) rebuildBlock; the caret/fence/dirty work below still runs.
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
  diag::updateViewport(viewport(), dirty);
  return true;
}

bool EditorView::refreshBlocks(const QVector<NodeId>& blockIds, const MarkdownDocument& document) {
  return refreshBlocksInternal(blockIds, document, /*forceRebuild=*/false);
}

// forceRebuild skips the BuiltStamp coalescing check. Keystroke refreshes (force=false) rely on the
// stamp to avoid re-running the O(block) rebuild for the same {selection, revision}. But
// refreshVisibleBlocks exists precisely for callers whose visible output depends on state OUTSIDE
// the stamp's key — spell-check enablement, markdown/convertOnRendering, codeBlockWrap, an async
// mermaid render arriving — so its blocks must rebuild even when the stamp says "current".
bool EditorView::refreshBlocksInternal(const QVector<NodeId>& blockIds, const MarkdownDocument& document,
                                       bool forceRebuild, DocumentLayout::BlockUpdateReason reason) {
  PerfTimer perf("view.refreshBlocks");
  if (!layout_ || document_ != &document) {
    return false;
  }

  // Rebuild every block inside one viewport pin (the suffix shift from each rebuild accumulates),
  // then compute dirty rects against the reconciled scroll offset.
  QVector<DocumentLayout::BlockRebuildResult> results;
  {
    ScopedViewportPin pin(*this);
    const quint64 revision = document.revision();
    for (NodeId blockId : blockIds) {
      const auto it = blockBuiltAt_.constFind(blockId);
      if (!forceRebuild && it != blockBuiltAt_.constEnd() && sameSelectionRange(it.value().selection, selection_) &&
          it.value().revision == revision) {
        continue; // already built with the current selection + revision — skip the O(block) rebuild
      }
      const DocumentLayout::BlockRebuildResult result =
          layout_->rebuildBlock(blockId, document, theme_, selection_, reason);
      if (!result.rebuilt) {
        return false;
      }
      results.append(result);
      blockBuiltAt_[blockId] = {selection_, revision};
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
  ensureCursorVisible();
  // Include the caret so a caret outside the refreshed blocks (e.g. on the
  // virtual trailing paragraph below the last block) repaints too — both the
  // new position (to draw) and the previous one (to erase the ghost on move).
  dirty = uniteDocumentRectDirty(dirty, effectiveCursorRect(), scrollY(), viewport()->size());
  dirty = uniteDocumentRectDirty(dirty, lastPaintedCaretDocumentRect_, scrollY(), viewport()->size());
  diag::updateViewport(viewport(), dirty.isEmpty() ? viewport()->rect() : dirty);
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
  ensureCursorVisible();
  QRect dirty;
  addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  const QRectF badgeRect = headingBadgeViewportRectForBlock(cursorPosition_.blockId);
  if (!badgeRect.isEmpty()) {
    dirty = dirty.united(badgeRect.adjusted(-2, -2, 2, 2).toAlignedRect());
  }
  // Structural range rebuilds (undo/redo of a block move/delete) can relocate the caret well
  // outside the range's {old, new, shifted} rects — e.g. onto the trailing paragraph after the
  // caret block was removed. Dirty both caret positions like refreshBlock does, or the old caret
  // pixels survive as a ghost.
  dirty = uniteDocumentRectDirty(dirty, effectiveCursorRect(), scrollY(), viewport()->size());
  dirty = uniteDocumentRectDirty(dirty, lastPaintedCaretDocumentRect_, scrollY(), viewport()->size());
  diag::updateViewport(viewport(), dirty.isEmpty() ? viewport()->rect() : dirty);
  return true;
}

void EditorView::setZoomPercent(int percent) {
  theme_.setZoomPercent(percent);
  applyScrollBarStyle();
  syncViewportBackground();
  rebuildLayout();
}

void EditorView::setFontSizePx(int px) {
  theme_.setFontSizePx(px);
  rebuildLayout();
}

void EditorView::setContentWidthPx(int px) {
  theme_.setContentWidthPx(px);
  rebuildLayout();
}

void EditorView::syncViewportBackground() {
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  // paintEvent fills every dirty pixel before drawing the page. Solid themes
  // need no repeated paint of the splitter/stack/window below a caret update.
  viewport()->setAttribute(Qt::WA_OpaquePaintEvent, theme_.viewportBackgroundColor().alpha() == 255);
}

void EditorView::setTheme(RenderTheme theme) {
  PerfTimer perf("theme.setView");
  cssPaintContext_.clear();
  cssPaintTimer_->stop();
  const int zoom = theme_.zoomPercent();
  const int fontSize = theme_.fontSizePx();
  const int contentWidth = theme_.contentWidthPx();
  theme.reuseDocumentSemantics(theme_);
  theme_ = std::move(theme);
  theme_.setZoomPercent(zoom);
  theme_.setFontSizePx(fontSize);
  theme_.setContentWidthPx(contentWidth);
  applyScrollBarStyle();
  syncViewportBackground();
  rebuildLayout();
}

void EditorView::setCursorHit(HitTestResult hit) {
  PerfTimer perf("view.setCursorHit");
  dragState_ = DragState::Idle;
  const SelectionRange previousSelection = selection_;
  cursorHit_ = hit;
  cursorPosition_ = hit.cursorPosition();
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  resetCaretBlink();
  refreshInlineProjectionForSelectionChange(previousSelection);
  ensureCodeFenceCursorVisible();
  ensureCursorVisible();
  updateTableToolbar();
}

void EditorView::setCursorPosition(CursorPosition position) {
  dragState_ = DragState::Idle;
  const SelectionRange previousSelection = selection_;
  cursorPosition_ = position;
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  {
    PerfTimer t("view.setSelection.refreshInlineProjection");
    refreshInlineProjectionForSelectionChange(previousSelection);
  }
  {
    PerfTimer t("view.setSelection.updateTableToolbar");
    updateTableToolbar();
  }
  resetCaretBlink();
  ensureCursorVisible();
}

void EditorView::setSelectionRange(SelectionRange selection) {
  if (dragState_ != DragState::Idle && sameSelectionRange(selection_, selection)) {
    return;
  }
  dragState_ = DragState::Idle;
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
  if (dragState_ == DragState::Dragging) {
    cursorVisible_ = false;
    cursorTimer_->stop();
    diag::updateViewport(viewport());
    updateTableToolbar();
    return;
  }
  resetCaretBlink();
  refreshInlineProjectionForSelectionChange(previousSelection);
  updateTableToolbar();
  ensureCursorVisible();
}

void EditorView::setEditingLocked(bool locked) {
  if (editingLocked_ == locked) {
    return;
  }
  editingLocked_ = locked;
  if (locked) {
    // No blinking caret, no inline popups: the page becomes read-only chrome-wise,
    // while selection (mouse/keyboard) and the context menu stay available.
    cursorVisible_ = false;
    caretBlinkOn_ = false;
    if (cursorTimer_) {
      cursorTimer_->stop();
    }
    if (tableToolbar_) {
      tableToolbar_->forceHide();
    }
    if (codeLanguageEditor_) {
      codeLanguageEditor_->forceHide();
    }
  } else {
    updateCursorHitFromPosition();
  }
  viewport()->update();
}

void EditorView::resetCaretBlink() {
  caretBlinkOn_ = true;
  if (editingLocked_) {
    cursorTimer_->stop(); // 锁定编辑: the caret stays hidden even when the view has focus
    return;
  }
  // Flash time 0 means "no blinking" (accessibility); no focus means no blink (offscreen tests
  // stay deterministic); an active composition replaces the caret entirely.
  if (hasFocus() && QApplication::cursorFlashTime() > 0 && preedit_.isEmpty()) {
    cursorTimer_->start();
  } else {
    cursorTimer_->stop();
  }
}

void EditorView::clearCursor() {
  cursorPosition_ = {};
  selection_ = {};
  preDragSelection_ = {};
  cursorHit_ = {};
  cursorVisible_ = false;
  cursorTimer_->stop();
  lastPaintedCaretDocumentRect_ = {};
  dragState_ = DragState::Idle;
  updateCodeLanguageEditor();
  updateTableToolbar();
  diag::updateViewport(viewport());
}

void EditorView::setCodeLanguageSuggestions(QStringList languages) {
  if (codeLanguageEditor_) {
    codeLanguageEditor_->setSuggestions(std::move(languages));
  }
}

void EditorView::setFocusMode(bool enabled) {
  focusMode_ = enabled;
  diag::updateViewport(viewport());
}

void EditorView::retranslateAccessibility() {
  // Explicit contexts keep lupdate independent of the namespace wrapper.
  setAccessibleName(QCoreApplication::translate("muffin::EditorView", "Markdown editor"));
  setAccessibleDescription(QCoreApplication::translate("muffin::EditorView", "Rendered Markdown document"));
}

void EditorView::changeEvent(QEvent* event) {
  QAbstractScrollArea::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    retranslateAccessibility();
  }
}

bool EditorView::event(QEvent* event) {
  if (event->type() == QEvent::Show)
    scheduleResourceRefresh();
  if (event->type() == QEvent::Hide && cssPaintTimer_)
    cssPaintTimer_->stop();
  if (event->type() == QEvent::Hide && viewportPrefetchTimer_)
    viewportPrefetchTimer_->stop();
  if (event->type() == QEvent::Leave) {
    cssPaintContext_.pointer.reset();
    cssPaintContext_.pressed = false;
    diag::updateViewport(viewport());
    clearHtmlHover();
    viewport()->setToolTip(QString());
  }
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (ShortcutSettings::instance().matches(*keyEvent, QStringLiteral("edit.select_all"), QKeySequence::SelectAll)) {
      if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
      }
      return QAbstractScrollArea::event(event);
    }
    if (keyEvent->modifiers().testFlag(Qt::ControlModifier) && !keyEvent->modifiers().testFlag(Qt::AltModifier) &&
        editor_keys::isEditorOwnedCtrlKey(keyEvent->key(), keyEvent->modifiers().testFlag(Qt::ShiftModifier))) {
      if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
      }
      return QAbstractScrollArea::event(event);
    }
    if ((keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) &&
        keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
      // Ctrl+Tab / Ctrl+Shift+Tab: the keyboard escape hatch — leave the editor for the
      // next/previous widget in the focus chain instead of editing content.
      if (event->type() == QEvent::ShortcutOverride) {
        return false;
      }
      return focusNextPrevChild(keyEvent->key() == Qt::Key_Tab);
    }
    if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
      // Plain Tab/Backtab is editor content (indent, cell navigation).
      event->accept();
      if (event->type() == QEvent::ShortcutOverride) {
        return true;
      }
      return QAbstractScrollArea::event(event);
    }
  }
  return QAbstractScrollArea::event(event);
}

bool EditorView::viewportEvent(QEvent* event) {
  if (event->type() == QEvent::Leave) {
    cssPaintContext_.pointer.reset();
    cssPaintContext_.pressed = false;
    diag::updateViewport(viewport());
  }
  return QAbstractScrollArea::viewportEvent(event);
}

void EditorView::resizeEvent(QResizeEvent* event) {
  PerfTimer perf("view.resize");
  QAbstractScrollArea::resizeEvent(event);
  bool stylesChanged;
  {
    PerfTimer step("view.resize.theme");
    stylesChanged = theme_.updateForViewport(viewport()->width(), viewport()->height());
    if (stylesChanged)
      syncViewportBackground();
  }
  bool relayouted = false;
  if (layout_ && document_) {
    ScopedViewportPin pin(*this);
    layout_->setEditingHtmlBlock(editingHtmlBlockId_);
    {
      PerfTimer step("view.resize.relayout");
      relayouted = layout_->relayoutForViewportWidth(theme_, viewport()->width(), stylesChanged);
    }
    if (relayouted) {
      // Measure the screen, retaining the document's stable slot/index tree.
      // Offscreen measurements from the old width remain temporary estimates.
      const auto range = layout_->slotRangeOverlappingY(scrollY(), scrollY() + viewport()->height());
      {
        PerfTimer step("view.resize.visible");
        if (range.first >= 0)
          layout_->ensureBuilt(range.first, range.second, theme_);
      }
      {
        PerfTimer step("view.resize.cursor");
        updateCursorHitFromPosition();
      }
      diag::updateViewport(viewport());
    }
  }
  if (!relayouted) {
    rebuildLayout(true);
  }
  // New resize events supersede unfinished estimate work. No full-document pass
  // runs in the native resize handler, or in one long pause after mouse release.
  if (layout_ && layout_->hasPendingViewportEstimates())
    viewportEstimateTimer_->start(80);
  updateCodeLanguageEditor();
  updateTableToolbar();
  if (typewriterMode_ && cursorHit_.isValid()) {
    scrollToCursorCentered();
  }
}

void EditorView::refreshViewportEstimates() {
  if (!document_ || loading_ || !layout_ || !layout_->hasPendingViewportEstimates())
    return;
  {
    ScopedViewportPin pin(*this);
    layout_->refreshViewportEstimates(theme_);
  }
  ensureVisibleBuilt();
  updateCursorHitFromPosition();
  updateCodeLanguageEditor();
  updateTableToolbar();
  diag::updateViewport(viewport());
  if (layout_->hasPendingViewportEstimates())
    viewportEstimateTimer_->start(8);
}

void EditorView::wheelEvent(QWheelEvent* event) {
  const bool horizontal = qAbs(event->angleDelta().x()) >= qAbs(event->angleDelta().y());
  // Shift+wheel or a trackpad horizontal gesture scrolls the code fence under the cursor; plain
  // vertical wheel always scrolls the document (unchanged).
  if ((event->modifiers().testFlag(Qt::ShiftModifier) || horizontal) &&
      scrollCodeFenceHorizontally(event, horizontal)) {
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
  if (event->button() == Qt::BackButton || event->button() == Qt::ForwardButton) {
    emit historyNavigationRequested(event->button() == Qt::ForwardButton);
    event->accept();
    return;
  }
  cssPaintContext_.pointer = event->position();
  cssPaintContext_.pressed = event->buttons().testFlag(Qt::LeftButton);
  diag::updateViewport(viewport());
  PerfTimer perf("view.mousePress");
  if (event->button() == Qt::LeftButton) {
    setFocus(Qt::MouseFocusReason);
    const NodeId htmlToggleBlockId = editingHtmlBlockId_.isValid() ? editingHtmlBlockId_ : htmlHover_.visibleBlockId();
    if (!editingLocked_ && htmlHoverButtonViewportRect().contains(event->position()) && htmlToggleBlockId.isValid()) {
      emit htmlEditToggleRequested(htmlToggleBlockId);
      event->accept();
      return;
    }
    const HitTestResult hit = hitTest(event->position());
    // Click anywhere on a rendered HTML block to enter source editing mode.
    // When already editing this block, fall through to normal cursor placement.
    if (!editingLocked_ && hit.isValid() && hit.zone == HitTestResult::Zone::Html && editingHtmlBlockId_ != hit.blockId) {
      emit htmlEditToggleRequested(hit.blockId);
      event->accept();
      return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier) && hit.isValid() && !hit.linkHref.isEmpty()) {
      emit linkActivated(hit.linkHref);
      event->accept();
      return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier) && hit.isValid() && !hit.imageSrc.isEmpty()) {
      QDesktopServices::openUrl(resolvedUrlForDocumentResource(hit.imageSrc, documentPath_));
      event->accept();
      return;
    }
    if (hit.isValid() && !hit.mermaidMenuActorId.isEmpty()) {
      const NodeId hostId = layout_ ? layout_->topLevelBlockIdFor(hit.blockId) : hit.blockId;
      QSet<QString>& openMenus = openSequenceMenus_[hostId];
      if (openMenus.contains(hit.mermaidMenuActorId))
        openMenus.remove(hit.mermaidMenuActorId);
      else
        openMenus.insert(hit.mermaidMenuActorId);
      if (openMenus.isEmpty())
        openSequenceMenus_.remove(hostId);
      if (layout_) {
        if (const BlockLayout* block = layout_->blockIfPromoted(hostId)) {
          diag::updateViewport(viewport(), block->rect().translated(0, -scrollY()).toAlignedRect());
        }
      }
      event->accept();
      return;
    }
    if (hit.isValid() && hit.taskCheckboxHit) {
      stopScrollAnimation();
      const QScopedValueRollback<bool> toggling(togglingTaskCheckbox_, true);
      const ScopedViewportPin pin(*this);
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
    if (event->modifiers().testFlag(Qt::ShiftModifier) && hit.isValid() && isDragSelectableZone(hit.zone) &&
        cursorPosition_.isValid()) {
      SelectionRange range;
      range.anchor = selection_.anchor.isValid() ? selection_.anchor : cursorPosition_;
      range.focus = hit.cursorPosition();
      setSelectionRange(range);
      emit selectionChanged(range, hit);
      event->accept();
      return;
    }
    const int clickCount = registerClick(event->position());
    if (clickCount == 3 && hit.isValid() && isSelectableZone(hit.zone)) {
      // Triple click: select the whole block's text (a table cell's content for cell hits).
      selectBlockForTripleClick(hit);
      dragState_ = DragState::Idle;
      event->accept();
      return;
    }
    setCursorHit(hit);
    emit blockClicked(hit);
    updateCodeLanguageEditor();
    if (hit.isValid() && isDragSelectableZone(hit.zone)) {
      preDragSelection_ = selection_;
      dragState_ = DragState::Pending;
      dragStartViewportPos_ = event->position();
      dragAnchorHit_ = hit;
      dragFromWord_ = false; // a plain press anchors the drag at the press point
    }
  }
  QAbstractScrollArea::mousePressEvent(event);
}

void EditorView::mouseMoveEvent(QMouseEvent* event) {
  cssPaintContext_.pointer = event->position();
  cssPaintContext_.pressed = event->buttons().testFlag(Qt::LeftButton);
  diag::updateViewport(viewport());
  PerfTimer perf("view.mouseMove");
  if (codeFenceScrollDragId_.isValid() && (event->buttons() & Qt::LeftButton)) {
    dragCodeFenceScrollBarTo(codeFenceScrollDragId_, event->position());
    event->accept();
    return;
  }
  if (dragState_ != DragState::Idle && (event->buttons() & Qt::LeftButton)) {
    if (dragState_ == DragState::Pending) {
      // Small drag threshold: a sloppy click that jitters a pixel or two stays a click (no
      // accidental one-character selection).
      if ((event->position() - dragStartViewportPos_).manhattanLength() < 3.0) {
        event->accept();
        return;
      }
      dragState_ = DragState::Dragging;
    }
    if (dragState_ == DragState::Dragging) {
      lastDragViewportPos_ = event->position();
      // Auto-scroll when the drag leaves the viewport's vertical edges: a 50ms timer keeps
      // scrolling (and extending the selection at the clamped edge coordinate) until the mouse
      // comes back inside or the button is released.
      constexpr qreal kEdgeMargin = 16.0;
      const bool beyondEdge = event->position().y() < kEdgeMargin ||
                              event->position().y() > static_cast<qreal>(viewport()->height()) - kEdgeMargin;
      if (beyondEdge && !dragScrollTimer_->isActive()) {
        dragScrollTimer_->start();
      } else if (!beyondEdge && dragScrollTimer_->isActive()) {
        dragScrollTimer_->stop();
      }
      updateDragSelection(event->position());
      event->accept();
      return;
    }
  }
  updateHtmlHover(event->position());
  updateMouseCursor(event->position());
  QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorView::mouseReleaseEvent(QMouseEvent* event) {
  cssPaintContext_.pointer = event->position();
  cssPaintContext_.pressed = false;
  diag::updateViewport(viewport());
  if (event->button() == Qt::LeftButton && codeFenceScrollDragId_.isValid()) {
    codeFenceScrollDragId_ = NodeId();
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && dragState_ != DragState::Idle) {
    dragScrollTimer_->stop();
    const bool wasDragging = (dragState_ == DragState::Dragging);
    if (wasDragging) {
      updateDragSelection(event->position());
    }
    dragState_ = DragState::Idle;
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
  registerClick(event->position()); // press(1) → dblclick(2): the third press makes 3
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
    if (hit.tableRow >= 0 && hit.tableRow < static_cast<int>(rows.size()) && hit.tableColumn >= 0 &&
        hit.tableColumn < static_cast<int>(rows[hit.tableRow].cells.size())) {
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

  // Arm drag-extend from the word selection (standard editor behavior): dragging past the word
  // grows the selection word-to-drag-point instead of overriding the double-click.
  preDragSelection_ = range;
  dragState_ = DragState::Pending;
  dragStartViewportPos_ = event->position();
  dragAnchorHit_ = hit;
  dragFromWord_ = true;
  dragWordStart_ = anchor;
  dragWordEnd_ = focus;

  event->accept();
}

int EditorView::registerClick(QPointF viewportPos) {
  // Qt delivers press(1) → dblclick(2) → press(3) for a triple click; count in both handlers.
  const bool sameSpot = (viewportPos - lastClickViewportPos_).manhattanLength() < 4.0;
  if (clickCount_ > 0 && sameSpot && clickClock_.elapsed() < QApplication::doubleClickInterval()) {
    ++clickCount_;
  } else {
    clickCount_ = 1;
  }
  clickClock_.restart();
  lastClickViewportPos_ = viewportPos;
  return clickCount_;
}

void EditorView::selectBlockForTripleClick(const HitTestResult& hit) {
  const BlockLayout* block = layout_ ? layout_->blockIfPromoted(hit.blockId) : nullptr;
  if (!block) {
    return;
  }
  CursorPosition anchor;
  anchor.blockId = hit.blockId;
  anchor.text.nodeId = hit.textNodeId.isValid() ? hit.textNodeId : hit.blockId;
  anchor.text.textOffset = 0;
  anchor.text.sourceOffset = -1;

  qsizetype length = editor_geometry::selectableLength(block);
  if (hit.zone == HitTestResult::Zone::TableCell && hit.tableRow >= 0 &&
      hit.tableRow < static_cast<int>(block->tableRows().size())) {
    const auto& row = block->tableRows().at(static_cast<size_t>(hit.tableRow));
    if (hit.tableColumn >= 0 && hit.tableColumn < static_cast<int>(row.cells.size())) {
      const auto& cell = row.cells.at(static_cast<size_t>(hit.tableColumn));
      anchor.text.nodeId = cell.nodeId;
      length = cell.text.plainText().size();
    }
  }

  CursorPosition focus = anchor;
  focus.text.textOffset = length;
  SelectionRange range;
  range.anchor = anchor;
  range.focus = focus;
  applySelectionRange(range);
  emit selectionChanged(range, hit);
}

void EditorView::focusInEvent(QFocusEvent* event) {
  QAbstractScrollArea::focusInEvent(event);
  // A composition abandoned mid-focus-change may not receive the platform's reset; make sure no
  // stale preedit survives (the caret repaint below covers the composition area).
  if (!preedit_.isEmpty()) {
    resetComposition();
  }
  resetCaretBlink();
  diag::updateViewport(viewport());
}

void EditorView::focusOutEvent(QFocusEvent* event) {
  QAbstractScrollArea::focusOutEvent(event);
  if (!preedit_.isEmpty()) {
    // Reset the composition on focus loss instead of waiting for the platform's reset event —
    // a stale preedit splice at a caret that has since moved is worse than dropping it here.
    resetComposition();
  }
  cursorTimer_->stop();
  caretBlinkOn_ = true; // unfocused: steady caret, no blink
}

void EditorView::resetComposition() {
  if (layout_) {
    layout_->setPreedit({}, {}, -1);
  }
  preedit_.clear();
  preeditFormats_.clear();
  preeditCursor_ = -1;
  if (document_ && cursorPosition_.blockId.isValid()) {
    blockBuiltAt_.remove(cursorPosition_.blockId);
    refreshBlocks({cursorPosition_.blockId}, *document_);
  }
}

void EditorView::contextMenuEvent(QContextMenuEvent* event) {
  const HitTestResult hit = hitTest(QPointF(event->pos()));

  // Standard editor behavior: move the caret to the click so caret-based commands
  // (format / paragraph / table / image) target the clicked location. A click on
  // a link / image / table cell always repositions (the intent is to act on that
  // object); a plain-text click leaves an existing selection intact when it lands
  // on it, so Cut/Copy still act on the selection. The menu itself (spell
  // suggestions + command sections) is assembled by MainWindow from the registry.
  const bool onObject =
      !hit.linkHref.isEmpty() || !hit.imageSrc.isEmpty() || hit.zone == HitTestResult::Zone::TableCell;
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
    // Compare TOP-LEVEL blocks: a selection inside a nested block (a list item, a quoted
    // paragraph) has focus.blockId = the nested node, which never equals the click's top-level
    // block — the historical direct comparison rejected every nested selection, so a right-click
    // inside it collapsed the selection instead of opening Copy/Cut on it.
    if (clickTop != layout_->topLevelBlockIdFor(selection_.focus.blockId)) {
      return false; // click is in a different top-level block than the (single) selected one
    }
    rects = block->selectionRects(selection_, theme_);
  } else {
    // Multi-block: the per-block [start, end] ranges are shared with paintSelection via
    // editor_geometry::forEachMultiBlockSelectionBlock — the two paths cannot drift. The click
    // must land in one of the covered blocks AND inside that block's selected rects.
    bool found = false;
    editor_geometry::forEachMultiBlockSelectionBlock(
        *layout_, selection_, [&](const BlockLayout* covered, qsizetype start, qsizetype end) {
          if (found || covered == nullptr || covered->nodeId() != clickTop) {
            return;
          }
          found = true;
          rects = covered->selectionRectsForOffsets(start, end, theme_);
        });
    if (!found) {
      return false;
    }
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
  if (editingLocked_) {
    event->ignore(); // 锁定编辑: no IME composition, no commit
    return;
  }
  // Composition is a temporary display splice in the caret block's layout.
  // Only the eventual commitString changes the document.
  const QString previousPreedit = preedit_;
  const int previousPreeditCursor = preeditCursor_;
  const auto previousPreeditFormats = preeditFormats_;
  preedit_ = event->preeditString();
  preeditCursor_ = -1;
  preeditFormats_.clear();
  if (!preedit_.isEmpty()) {
    // Default underline so a composition is visible even when the platform IME sends no TextFormat
    // attributes; the IME's own ranges (typed highlight, active segment) then override sub-ranges.
    QTextCharFormat underline;
    underline.setFontUnderline(true);
    preeditFormats_.append({0, static_cast<int>(preedit_.length()), underline});
    for (const QInputMethodEvent::Attribute& attr : event->attributes()) {
      if (attr.type == QInputMethodEvent::TextFormat && attr.length > 0) {
        preeditFormats_.append({attr.start, attr.length, attr.value.value<QTextFormat>().toCharFormat()});
      } else if (attr.type == QInputMethodEvent::Cursor) {
        preeditCursor_ = attr.start;
      }
    }
  }

  if (!event->commitString().isEmpty()) {
    // Clear the splice then commit: the edit-driven refresh rebuilds the caret block without a
    // preedit, so the old composition is erased and the committed text lands at the caret.
    if (layout_) {
      layout_->setPreedit({}, {}, -1);
    }
    emit textCommitted(event->commitString());
  }
  if (preedit_ != previousPreedit || preeditCursor_ != previousPreeditCursor ||
      preeditFormats_ != previousPreeditFormats || !event->commitString().isEmpty()) {
    // Push the preedit to the layout and rebuild the caret block so its
    // text layout re-splices the preedit (following text shifts/wraps) and repaints. The block's
    // BuiltStamp is dropped so the cached build is discarded; refreshBlocks resolves a nested caret
    // (list item / quote paragraph) up to its top-level block. configureBuilder forwards the preedit.
    resetCaretBlink(); // a composition replaces the caret — stop blinking while composing
    if (layout_ && document_ && cursorPosition_.blockId.isValid()) {
      layout_->setPreedit(preedit_, preeditFormats_, preeditCursor_);
      blockBuiltAt_.remove(cursorPosition_.blockId);
      refreshBlocks({cursorPosition_.blockId}, *document_);
      ensureCodeFenceCursorVisible();
    } else {
      QRect dirty = uniteDocumentRectDirty({}, lastPaintedCaretDocumentRect_, scrollY(), viewport()->size());
      dirty = uniteDocumentRectDirty(dirty, effectiveCursorRect(), scrollY(), viewport()->size());
      diag::updateViewport(viewport(), dirty.isEmpty() ? viewport()->rect() : dirty);
    }
  }
  event->accept();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const {
  switch (query) {
  case Qt::ImCursorRectangle: {
    // effectiveCursorRect matches where the caret is painted (offset-adjusted for a scrolled code
    // fence); without it the IME panel anchored at the natural advance, far from the visible caret.
    QRectF cursor = effectiveCursorRect();
    cursor.translate(0, -scrollY());
    return cursor.toRect();
  }
  case Qt::ImEnabled:
    return true;
  case Qt::ImFont:
    return preeditFont();
  case Qt::ImSurroundingText: {
    // The caret block's visible text — the paragraph around the cursor, which is what conversion
    // IMEs consume for context/reconversion. Folded inline syntax reports the VISIBLE projection.
    const BlockLayout* block = layout_ ? layout_->blockIfPromoted(cursorPosition_.blockId) : nullptr;
    if (!block) {
      return QString();
    }
    if (cursorHit_.zone == HitTestResult::Zone::TableCell && cursorHit_.tableRow >= 0 &&
        cursorHit_.tableRow < static_cast<int>(block->tableRows().size())) {
      const auto& row = block->tableRows().at(static_cast<size_t>(cursorHit_.tableRow));
      if (cursorHit_.tableColumn >= 0 && cursorHit_.tableColumn < static_cast<int>(row.cells.size())) {
        return row.cells.at(static_cast<size_t>(cursorHit_.tableColumn)).text.visibleText();
      }
      return QString();
    }
    if (block->inlineLayout() != nullptr) {
      return block->inlineLayout()->visibleText();
    }
    return block->literal();
  }
  case Qt::ImCursorPosition: {
    const QString surrounding = inputMethodQuery(Qt::ImSurroundingText).toString();
    return static_cast<int>(qBound<qsizetype>(0, cursorPosition_.text.textOffset, surrounding.size()));
  }
  case Qt::ImCurrentSelection: {
    // Only a same-text-node selection maps into the surrounding text; cross-block/cross-cell
    // selections have no single surrounding string.
    if (selection_.isCollapsed() || !selection_.isSingleTextNode()) {
      return QString();
    }
    const QString surrounding = inputMethodQuery(Qt::ImSurroundingText).toString();
    const qsizetype start = qBound<qsizetype>(0, selection_.startOffset(), surrounding.size());
    const qsizetype end = qBound<qsizetype>(start, selection_.endOffset(), surrounding.size());
    return surrounding.mid(start, end - start);
  }
  default:
    return QAbstractScrollArea::inputMethodQuery(query);
  }
}

void EditorView::rebuildLayout(bool preserveViewport) {
  // A full theme/document rebuild consumes the current resource versions. Any
  // notification raised during the rebuild marks the new layout dirty again.
  resourcesDirty_ = false;
  blockBuiltAt_.clear();
  theme_.updateForViewport(viewport()->width(), viewport()->height());
  syncViewportBackground();
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
  layout_->setMermaidRenderCache(&mermaidCache_);

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
          layout_->ensureBuilt(caretIdx, caretIdx, theme_); // caret block needed for hit resolution
        }
      }
    }
    ensureVisibleBuilt(); // promote the visible window; self-pinned so the promotion snap is invisible
    if (cursorPosition_.isValid()) {
      cursorHit_ = editor_geometry::hitForCursorPosition(*layout_, theme_, cursorPosition_);
      cursorVisible_ = cursorHit_.isValid();
      ensureCodeFenceCursorVisible();
    }
  } else {
    layout_ = std::make_unique<DocumentLayout>();
    layout_->setCodeFenceScroll(codeFenceScroll_);
    layout_->setMermaidRenderCache(&mermaidCache_);
    updateScrollBars();
  }
  updateCodeLanguageEditor();
  updateTableToolbar();
  if (cursorPosition_.isValid() && !preserveViewport) {
    ensureCursorVisible();
  }
  diag::updateViewport(viewport());
}

bool EditorView::refreshVisibleBlocks(const MarkdownDocument& document) {
  if (!layout_) {
    return false;
  }
  layout_->invalidateRenderState();
  return refreshBlocksInternal(layout_->promotedTopLevelIds(), document, /*forceRebuild=*/true);
}

void EditorView::updateCodeLanguageEditor() {
  if (!codeLanguageEditor_) {
    return;
  }
  if (editingLocked_) {
    codeLanguageEditor_->forceHide(); // 锁定编辑: no floating code-fence language editor
    return;
  }
  codeLanguageEditor_->update(cursorPosition_, cursorHit_, layout_.get(), document_.data(), scrollY(),
                              viewport()->height());
}

void EditorView::updateTableToolbar() {
  if (!tableToolbar_) {
    return;
  }
  if (editingLocked_) {
    tableToolbar_->forceHide(); // 锁定编辑: no “调整表格大小” toolbar over a table cell
    return;
  }
  tableToolbar_->update(cursorHit_, layout_.get(), scrollY(), viewport()->rect());
}

void EditorView::updateCursorHitFromPosition() {
  cursorHit_ = editor_geometry::hitForCursorPosition(*layout_, theme_, cursorPosition_);
  cursorVisible_ = cursorHit_.isValid();
  updateCodeLanguageEditor();
  updateTableToolbar();
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
    refreshed = refreshBlocksInternal(blockIds, *document_, false, DocumentLayout::BlockUpdateReason::Selection);
  }
  const bool scrolled = refreshed && restoreViewportAnchor(anchor);

  // The selection overlay (paintSelection) spans every block between anchor and focus,
  // but refreshBlocks only rebuilds — and dirties — the anchor/focus endpoint blocks.
  // Without this, a programmatic multi-block change (Ctrl+A, click-to-deselect) left
  // the covered blocks showing stale pixels until an unrelated repaint (hover) ran.
  // Invalidate the overlay region of BOTH the old and the new selection; Qt unions
  // this with whatever update refreshBlocks already scheduled, so it is a no-op when
  // a full repaint is pending.
  const QRectF overlayDirty =
      selectionOverlayDocumentRect(previousSelection).united(selectionOverlayDocumentRect(selection_));
  if (!overlayDirty.isEmpty()) {
    const QRect viewportRect =
        overlayDirty.translated(0.0, -scrollY()).adjusted(-3.0, -3.0, 3.0, 3.0).toAlignedRect() & viewport()->rect();
    if (!viewportRect.isEmpty()) {
      diag::updateViewport(viewport(), viewportRect);
    }
  }

  if (!refreshed) {
    updateCursorHitFromPosition();
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
    diag::updateViewport(viewport());
  } else {
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
    if (scrolled) {
      diag::updateViewport(viewport()); // content shifted under a new scroll offset — repaint coherently
    }
    // Erase old heading badge if cursor moved away from a heading block
    if (previousSelection.focus.blockId != cursorPosition_.blockId) {
      const QRectF oldBadge = headingBadgeViewportRectForBlock(previousSelection.focus.blockId);
      if (!oldBadge.isEmpty()) {
        diag::updateViewport(viewport(), oldBadge.adjusted(-2, -2, 2, 2).toAlignedRect());
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

QRectF EditorView::selectionOverlayDocumentRect(const SelectionRange& selection) const {
  if (!layout_ || selection.isCollapsed()) {
    return {};
  }
  // paintSelection draws its bands inside the flow rects of the blocks between anchor
  // and focus (scrollable code fences clip their shifted rects to the block's text
  // window), so uniting those rects is a safe superset of what it actually paints.
  QRectF bounds;
  const QVector<const BlockLayout*> covered =
      blocksBetween(*layout_, selection.anchor.blockId, selection.focus.blockId);
  for (const BlockLayout* block : covered) {
    if (block) {
      bounds = bounds.united(block->rect());
    }
  }
  return bounds;
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
  const TextFontMetrics metrics(badgeFont);
  const qreal badgeWidth = metrics.horizontalAdvance(QStringLiteral("H%1").arg(level)) + 6.0;
  const qreal badgeHeight = metrics.height() + 2.0;
  const QRectF blockRect = block->rect();
  const TextFontMetrics headingMetrics(theme_.headingFont(level));
  const qreal lineCenterY = blockRect.top() + headingMetrics.height() / 2.0;
  const qreal badgeY = lineCenterY - badgeHeight / 2.0 - scrollY();
  return HeadingBadge{topId, QRectF(blockRect.left() - badgeWidth - 4.0, badgeY, badgeWidth, badgeHeight), level};
}

HtmlBlockHoverController::Inputs EditorView::htmlHoverInputs() const {
  return {layout_.get(), &theme_, editingHtmlBlockId_, scrollY()};
}

QRectF EditorView::htmlHoverButtonViewportRect() const {
  return htmlHover_.buttonViewportRect(htmlHoverInputs());
}

void EditorView::updateHtmlHover(QPointF viewportPos) {
  const HitTestResult hit = hitTest(viewportPos);
  const NodeId next = hit.isValid() && hit.zone == HitTestResult::Zone::Html ? hit.blockId : NodeId();
  const HtmlBlockHoverController::Dirty dirty = htmlHover_.setHoveredBlock(next, htmlHoverInputs());
  if (!dirty.oldRect.isEmpty()) {
    diag::updateViewport(viewport(), dirty.oldRect);
  }
  if (!dirty.newRect.isEmpty()) {
    diag::updateViewport(viewport(), dirty.newRect);
  }
}

void EditorView::clearHtmlHover() {
  const HtmlBlockHoverController::Dirty dirty = htmlHover_.clear(htmlHoverInputs());
  if (!dirty.oldRect.isEmpty()) {
    diag::updateViewport(viewport(), dirty.oldRect);
  }
}

void EditorView::repaintAnimatedMermaidBlocks() {
  PerfTimer perf("view.repaintMermaidAnim");
  if (!layout_ || !mermaidAnimationTimer_ || !mermaidAnimationTimer_->isActive())
    return;
  const auto blocks = layout_->visibleBlocks(documentViewportRect().adjusted(0, -40, 0, 40), theme_);
  QRectF dirty;
  for (const auto* block : blocks) {
    if (!block || !block->hasAnimatedMermaid())
      continue;
    dirty = dirty.isNull() ? block->rect() : dirty.united(block->rect());
  }
  if (!dirty.isNull())
    diag::updateViewport(viewport(), dirty.translated(0, -scrollY()).adjusted(-2, -2, 2, 2).toAlignedRect());
}

void EditorView::updateMermaidAnimationDriver(bool hasVisibleAnimation) {
  if (!mermaidAnimationTimer_)
    return;
  if (hasVisibleAnimation) {
    if (!mermaidAnimationClock_.isValid())
      mermaidAnimationClock_.start();
    if (!mermaidAnimationTimer_->isActive())
      mermaidAnimationTimer_->start();
  } else if (mermaidAnimationTimer_->isActive()) {
    // Keep the elapsed clock so scrolling away and back matches CSS animation
    // time; only the repaint wakeups are idle-gated.
    mermaidAnimationTimer_->stop();
  }
}

void EditorView::updateDragSelection(QPointF viewportPos) {
  PerfTimer perf("view.dragSel");
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
  if (dragFromWord_) {
    // Dragging off a double-click word selection: the anchor flips to the FAR end of the word —
    // drag right of the word and the selection starts at the word start; drag left and it starts
    // at the drag point (anchored at the word end). Standard editor word-drag semantics.
    const CursorPosition focusPos = focusHit.cursorPosition();
    const auto focusBeforeWordStart = [&]() {
      if (focusPos.blockId == dragWordStart_.blockId) {
        return focusPos.text.textOffset < dragWordStart_.text.textOffset;
      }
      return editor_geometry::blockComesBefore(*layout_, focusPos.blockId, dragWordStart_.blockId) &&
             focusPos.blockId != dragWordStart_.blockId;
    }();
    range.anchor = focusBeforeWordStart ? dragWordEnd_ : dragWordStart_;
    range.focus = focusPos;
  }
  if (range.isCollapsed()) {
    return;
  }
  applySelectionRange(range);
  emit selectionChanged(range, focusHit);
}

void EditorView::updateMouseCursor(QPointF viewportPos) {
  if (htmlHoverButtonViewportRect().contains(viewportPos)) {
    viewport()->setToolTip(QString());
    viewport()->setCursor(Qt::PointingHandCursor);
    return;
  }
  const HitTestResult hit = hitTest(viewportPos);
  const QString toolTip =
      hit.toolTip.isEmpty() ? QString() : Qt::convertFromPlainText(hit.toolTip, Qt::WhiteSpaceNormal);
  if (viewport()->toolTip() != toolTip)
    viewport()->setToolTip(toolTip);
  if (hit.isValid() && !hit.linkHref.isEmpty()) {
    viewport()->setCursor(Qt::PointingHandCursor);
  } else if (hit.isValid() && !hit.imageSrc.isEmpty()) {
    viewport()->setCursor(Qt::PointingHandCursor);
  } else if (hit.isValid() && !hit.mermaidMenuActorId.isEmpty()) {
    viewport()->setCursor(Qt::PointingHandCursor);
  } else if (hit.isValid() &&
             (hit.zone == HitTestResult::Zone::Text || hit.zone == HitTestResult::Zone::Code ||
              hit.zone == HitTestResult::Zone::Math || hit.zone == HitTestResult::Zone::Html ||
              hit.zone == HitTestResult::Zone::FrontMatter || hit.zone == HitTestResult::Zone::TableCell ||
              hit.zone == HitTestResult::Zone::BlockAfter)) {
    viewport()->setCursor(Qt::IBeamCursor);
  } else {
    viewport()->unsetCursor();
  }
}

void EditorView::dragEnterEvent(QDragEnterEvent* event) {
  if (editingLocked_) {
    event->ignore(); // 锁定编辑: no file/image drops inserted into the document
    return;
  }
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

void EditorView::moveCaretToViewportPos(QPointF viewportPos) {
  // Mirror mousePressEvent: resolve the hit under the point and drive the selection
  // controller through blockClicked so the caret lands where the user dropped. setCursorHit
  // is non-emitting; the caret move only propagates via the explicit blockClicked signal.
  const HitTestResult hit = hitTest(viewportPos);
  if (!hit.isValid()) {
    return; // dropped on empty space — keep the existing caret and insert there.
  }
  setCursorHit(hit);
  emit blockClicked(hit);
}

void EditorView::dropEvent(QDropEvent* event) {
  if (editingLocked_) {
    event->ignore(); // 锁定编辑 (defence in depth; dragEnterEvent already refuses)
    return;
  }
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
  const QString suffix = info.suffix();

  // Folders always go to the main window (open as sidebar root), whether the drag came from
  // the file tree or an external source.
  if (info.isDir()) {
    emit folderDropped(filePath);
    return;
  }

  // A drag that originated in Muffin's sidebar file tree carries the kMuffinFileTreeDragMime
  // marker. Route it as "insert at the drop position": a markdown link for any file, or an
  // inline image for image files. The path is resolved relative to the current document dir
  // when the file lives inside it (portable), otherwise absolute. External file:// drops
  // keep their existing open-as-document / open-as-folder behaviour.
  if (event->mimeData()->hasFormat(kMuffinFileTreeDragMime)) {
    moveCaretToViewportPos(event->position());
    const QString alt = info.baseName();
    if (isImageSuffix(suffix)) {
      const QString target = link_syntax::localPathHref(FilePathOps::linkTargetForPath(filePath, documentPath_));
      emit textCommitted(image_syntax::markdownImage(alt, target));
    } else {
      emit textCommitted(FilePathOps::markdownLinkForFile(filePath, documentPath_));
    }
    return;
  }

  // External drop of an image file: insert inline at the drop position. (Pre-move-caret fix:
  // this used to land at the stale caret because the drop position was never resolved.)
  if (isImageSuffix(suffix)) {
    moveCaretToViewportPos(event->position());
    const QString alt = info.baseName();
    emit textCommitted(image_syntax::markdownImage(alt, link_syntax::localPathHref(filePath)));
    return;
  }
  // Markdown is Muffin's native format, so a dropped .md/.txt opens. Other files are routed
  // through the importable-file preference.
  if (suffix.compare(QStringLiteral("md"), Qt::CaseInsensitive) == 0 ||
      suffix.compare(QStringLiteral("markdown"), Qt::CaseInsensitive) == 0 ||
      suffix.compare(QStringLiteral("mdown"), Qt::CaseInsensitive) == 0 ||
      suffix.compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0) {
    emit markdownFileDropped(filePath);
    return;
  }
  emit importableFileDropped(filePath);
}

NodeId EditorView::specialAnchorTarget(const QString& href) const {
  if (href.startsWith(QLatin1String("#toc:")))
    return NodeId::fromString(href.mid(5));
  if (href.startsWith(QLatin1String("#fn:")))
    return layout_ ? layout_->footnoteDefinitionIdForLabel(href.mid(4)) : NodeId();
  return {};
}

} // namespace muffin
