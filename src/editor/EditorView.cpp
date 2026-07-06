#include "editor/EditorView.h"

#include "diagnostics/ScopedPerfProbe.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "document/MarkdownDocument.h"
#include "editor/CodeLanguageEditor.h"
#include "editor/EditorViewGeometry.h"
#include "editor/HtmlBlockHoverController.h"
#include "editor/HoverAnimator.h"
#include "editor/FocusAnimator.h"
#include "editor/KeyframeAnimator.h"
#include "editor/ResourceUrl.h"
#include "editor/TableToolbar.h"
#include "io/FilePathOps.h"
#include "io/MuffinMime.h"
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
#include <QTimer>
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

// Image extensions whose drop inserts an inline ![alt](path) rather than a [name](path)
// link. Shared by the file-tree-drop and external-image-drop paths.
bool isImageSuffix(QStringView suffix) {
  static const QSet<QString> kImageSuffixes = {
      QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
      QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("webp"),
      QStringLiteral("bmp"), QStringLiteral("ico"), QStringLiteral("tiff"),
      QStringLiteral("tif")};
  return kImageSuffixes.contains(suffix.toString().toLower());
}

struct PerfTimer : diag::ScopedPerfProbe {
  explicit PerfTimer(const char* label) : diag::ScopedPerfProbe(label, viewPerf()) {}
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
  focusAnimator_ = new FocusAnimator(this);
  focusAnimator_->repaintBlock = [this](NodeId id) { repaintFocusBlock(id); };
  keyframeAnimator_ = new KeyframeAnimator(this);
  keyframeAnimator_->repaintAnimated = [this]() { repaintAnimatedBlocks(); };
  // Idle-gated spinner driver: runs only while loading_ (set by setLoading), advancing the
  // bright wave around the dot ring and repainting. Costs nothing on a loaded document.
  loadingTimer_ = new QTimer(this);
  loadingTimer_->setInterval(33);  // ~30 fps — smooth for a flowing dot ring
  QObject::connect(loadingTimer_, &QTimer::timeout, this, [this] {
    loadingPhase_ += 1.0 / 36.0;  // ~1.2 s per revolution at 30 fps
    if (loadingPhase_ >= 1.0) loadingPhase_ -= 1.0;
    viewport()->update();
  });

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
    if (document_ && !loading_) {
      // Skip while an async open parse is in flight: document_ still points at
      // the pre-open (stale) document, so rebuilding here would lay out stale
      // content right before the worker's result lands. The freshly parsed
      // document triggers its own rebuild via the parsed-signal path.
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
  blockBuiltAt_.clear();  // fresh document: every block's build stamp is stale
  rebuildLayout();
  updateTableToolbar();
}

void EditorView::setLoading(bool loading) {
  if (loading_ != loading) {
    loading_ = loading;
    if (loading_) {
      loadingPhase_ = 0.0;        // each load animates from the top of the ring
      loadingTimer_->start();
    } else {
      loadingTimer_->stop();
    }
    viewport()->update();
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
    const bool current = it != blockBuiltAt_.constEnd()
                         && sameSelectionRange(it.value().selection, selection_)
                         && it.value().revision == document.revision();
    if (!current) {
      result = layout_->rebuildBlock(blockId, document, theme_, selection_);
      if (!result.rebuilt) {
        return false;  // pin reconciles (nothing moved); caller falls back to setDocument
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
    const quint64 revision = document.revision();
    for (NodeId blockId : blockIds) {
      const auto it = blockBuiltAt_.constFind(blockId);
      if (it != blockBuiltAt_.constEnd() && sameSelectionRange(it.value().selection, selection_)
          && it.value().revision == revision) {
        continue;  // already built with the current selection + revision — skip the O(block) rebuild
      }
      const DocumentLayout::BlockRebuildResult result = layout_->rebuildBlock(blockId, document, theme_, selection_);
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
  PerfTimer perf("view.setCursorHit");
  dragState_ = DragState::Idle;
  const SelectionRange previousSelection = selection_;
  cursorHit_ = hit;
  cursorPosition_ = hit.cursorPosition();
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  refreshInlineProjectionForSelectionChange(previousSelection);
  ensureCodeFenceCursorVisible();
  updateTableToolbar();
  updateBlockFocus();
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
  {
    PerfTimer t("view.setSelection.updateBlockFocus");
    updateBlockFocus();
  }
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
  updateBlockFocus();
  if (dragState_ == DragState::Dragging) {
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
  dragState_ = DragState::Idle;
  updateCodeLanguageEditor();
  updateTableToolbar();
  updateBlockFocus();
  viewport()->update();
}

void EditorView::setCodeLanguageSuggestions(QStringList languages) {
  if (codeLanguageEditor_) {
    codeLanguageEditor_->setSuggestions(std::move(languages));
  }
}

void EditorView::setFocusMode(bool enabled) {
  focusMode_ = enabled;
  viewport()->update();
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
  PerfTimer perf("view.mousePress");
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
      dragState_ = DragState::Pending;
      dragStartViewportPos_ = event->position();
      dragAnchorHit_ = hit;
    }
  }
  QAbstractScrollArea::mousePressEvent(event);
}

void EditorView::mouseMoveEvent(QMouseEvent* event) {
  PerfTimer perf("view.mouseMove");
  if (codeFenceScrollDragId_.isValid() && (event->buttons() & Qt::LeftButton)) {
    dragCodeFenceScrollBarTo(codeFenceScrollDragId_, event->position());
    event->accept();
    return;
  }
  if (dragState_ != DragState::Idle && (event->buttons() & Qt::LeftButton)) {
    if (dragState_ == DragState::Pending) {
      dragState_ = DragState::Dragging;
    }
    if (dragState_ == DragState::Dragging) {
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
  if (event->button() == Qt::LeftButton && dragState_ != DragState::Idle) {
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
  dragState_ = DragState::Idle;

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
      cursorHit_ = editor_geometry::hitForCursorPosition(*layout_, theme_, cursorPosition_);
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

bool EditorView::refreshVisibleBlocks(const MarkdownDocument& document) {
  if (!layout_) {
    return false;
  }
  return refreshBlocks(layout_->promotedTopLevelIds(), document);
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
  const QPointF documentPos(viewportPos.x(), viewportPos.y() + scrollY());
  const HitTestResult hit = hitTest(viewportPos);
  NodeId next = hit.isValid() ? layout_->topLevelBlockIdFor(hit.blockId) : NodeId();
  if (next.isValid()) {
    if (const BlockLayout* blk = layout_->blockIfPromoted(next)) {
      const QString host = hostKeyForBlock(*blk);
      if (!host.isEmpty() && !blk->cssBorderBox(theme_).contains(documentPos)) { next = NodeId(); }
    }
  }
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
  PerfTimer perf("view.repaintHover");
  if (!layout_ || !blockId.isValid()) { return; }
  const BlockLayout* blk = layout_->blockIfPromoted(blockId);
  if (!blk) { return; }
  const QRectF r = blk->visualOverflowRect(theme_).translated(0, -scrollY()).adjusted(-2, -2, 2, 2);
  viewport()->update(r.toAlignedRect());
}

qreal EditorView::hoverTransitionMs(const QString& host) const {
  if (host.isEmpty()) { return 0.0; }
  for (const TransitionSpec& t : theme_.decorations().transitions) {
    if (t.host == host) { return t.durationMs; }
  }
  return 0.0;
}

void EditorView::updateBlockFocus() {
  if (!layout_ || !focusAnimator_) { return; }
  // The focused top-level block is the one holding the caret (mirrors focus-mode's
  // activeTopLevel derivation). Scoped to headings/blockquotes — the only hosts
  // with a CSS :focus style — so focusing body text does not light up a paragraph.
  NodeId next;
  if (cursorPosition_.isValid()) {
    next = layout_->topLevelBlockIdFor(cursorPosition_.blockId);
    if (next.isValid()) {
      if (const BlockLayout* blk = layout_->blockIfPromoted(next)) {
        if (hostKeyForBlock(*blk).isEmpty()) { next = NodeId(); }
      }
    }
  }
  if (next == focusedBlockId_) { return; }
  focusedBlockId_ = next;
  qreal duration = 0.0;
  if (next.isValid()) {
    if (const BlockLayout* blk = layout_->blockIfPromoted(next)) {
      duration = focusTransitionMs(hostKeyForBlock(*blk));
    }
  }
  focusAnimator_->setFocused(next, duration);
}

void EditorView::repaintFocusBlock(NodeId blockId) {
  PerfTimer perf("view.repaintFocus");
  if (!layout_ || !blockId.isValid()) { return; }
  const BlockLayout* blk = layout_->blockIfPromoted(blockId);
  if (!blk) { return; }
  const QRectF r = blk->visualOverflowRect(theme_).translated(0, -scrollY()).adjusted(-2, -2, 2, 2);
  viewport()->update(r.toAlignedRect());
}

qreal EditorView::focusTransitionMs(const QString& host) const {
  // Transitions are host-keyed (not state-keyed), so a `transition` on `h1` applies
  // to both :hover and :focus — the fade duration is shared.
  if (host.isEmpty()) { return 0.0; }
  for (const TransitionSpec& t : theme_.decorations().transitions) {
    if (t.host == host) { return t.durationMs; }
  }
  return 0.0;
}

void EditorView::repaintAnimatedBlocks() {
  PerfTimer perf("view.repaintAnim");
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

void EditorView::moveCaretToViewportPos(QPointF viewportPos) {
  // Mirror mousePressEvent: resolve the hit under the point and drive the selection
  // controller through blockClicked so the caret lands where the user dropped. setCursorHit
  // is non-emitting; the caret move only propagates via the explicit blockClicked signal.
  const HitTestResult hit = hitTest(viewportPos);
  if (!hit.isValid()) {
    return;  // dropped on empty space — keep the existing caret and insert there.
  }
  setCursorHit(hit);
  emit blockClicked(hit);
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
      const QString target = FilePathOps::linkTargetForPath(filePath, documentPath_);
      emit textCommitted(QStringLiteral("![%1](%2)").arg(alt, target));
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
    emit textCommitted(QStringLiteral("![%1](%2)").arg(alt, filePath));
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

}  // namespace muffin
