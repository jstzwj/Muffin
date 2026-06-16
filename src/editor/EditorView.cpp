#include "editor/EditorView.h"

#include "document/MarkdownDocument.h"
#include "editor/CodeLanguageEditor.h"
#include "editor/EditorViewGeometry.h"
#include "editor/HtmlBlockHoverController.h"
#include "editor/TableToolbar.h"
#include "render/ImageLoader.h"
#include "spellcheck/SpellChecker.h"
#include "unicode/WordBoundary.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QList>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
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

bool sameCursorPosition(const CursorPosition& a, const CursorPosition& b) {
  return a.blockId == b.blockId && a.text.nodeId == b.text.nodeId && a.text.textOffset == b.text.textOffset &&
         a.text.sourceOffset == b.text.sourceOffset && a.text.inMeta == b.text.inMeta;
}

bool sameSelectionRange(const SelectionRange& a, const SelectionRange& b) {
  return sameCursorPosition(a.anchor, b.anchor) && sameCursorPosition(a.focus, b.focus);
}

QUrl resolvedUrlForDocumentResource(const QString& value, const QString& documentPath) {
  const QFileInfo info(value);
  if (info.isAbsolute()) {
    return QUrl::fromLocalFile(info.absoluteFilePath());
  }

  const QUrl url(value);
  if (url.isLocalFile()) {
    return QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absoluteFilePath());
  }
  if (url.isValid() && !url.scheme().isEmpty()) {
    return url;
  }
  if (value.startsWith(QLatin1Char('#'))) {
    return url;
  }

  if (!documentPath.isEmpty()) {
    const QString baseDirectory = QFileInfo(documentPath).absolutePath();
    return QUrl::fromLocalFile(QFileInfo(QDir(baseDirectory).absoluteFilePath(value)).absoluteFilePath());
  }

  return QUrl(value);
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
  setBackgroundRole(QPalette::Base);
  applyScrollBarStyle();

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

  const DocumentLayout::BlockRebuildResult result = layout_->rebuildBlock(blockId, document, theme_, selection_);
  if (!result.rebuilt) {
    return false;
  }
  if (!qFuzzyIsNull(result.heightDelta)) {
    updateScrollBars();
  }
  updateCursorHitFromPosition();
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
  dirty = uniteDocumentRectDirty(dirty, cursorHit_.cursorRect, scrollY(), viewport()->size());
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

  QRect dirty;
  bool scrollbarDirty = false;
  for (NodeId blockId : blockIds) {
    const DocumentLayout::BlockRebuildResult result = layout_->rebuildBlock(blockId, document, theme_, selection_);
    if (!result.rebuilt) {
      return false;
    }
    scrollbarDirty = scrollbarDirty || !qFuzzyIsNull(result.heightDelta);
    addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  }
  const QRectF badgeRect = headingBadgeViewportRectForBlock(cursorPosition_.blockId);
  if (!badgeRect.isEmpty()) {
    dirty = dirty.united(badgeRect.adjusted(-2, -2, 2, 2).toAlignedRect());
  }
  if (scrollbarDirty) {
    updateScrollBars();
  }
  updateCursorHitFromPosition();
  updateTableToolbar();
  // Include the caret so a caret outside the refreshed blocks (e.g. on the
  // virtual trailing paragraph below the last block) repaints too — both the
  // new position (to draw) and the previous one (to erase the ghost on move).
  dirty = uniteDocumentRectDirty(dirty, cursorHit_.cursorRect, scrollY(), viewport()->size());
  dirty = uniteDocumentRectDirty(dirty, lastPaintedCaretDocumentRect_, scrollY(), viewport()->size());
  viewport()->update(dirty.isEmpty() ? viewport()->rect() : dirty);
  return true;
}

bool EditorView::refreshTopLevelRange(TopLevelRangeChange range, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshTopLevelRange");
  if (!layout_ || document_ != &document) {
    return false;
  }

  const DocumentLayout::RangeRebuildResult result = layout_->rebuildTopLevelRange(range, document, theme_, selection_);
  if (!result.rebuilt) {
    return false;
  }
  if (!qFuzzyIsNull(result.heightDelta)) {
    updateScrollBars();
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
  const BlockLayout* block = layout_->block(id);
  return block ? block->rect() : QRectF();
}

void EditorView::scrollToNode(NodeId id) {
  const QRectF rect = nodeRect(id);
  if (rect.isNull() || rect.isEmpty()) {
    return;
  }
  QScrollBar* scrollBar = verticalScrollBar();
  const int target = qBound(scrollBar->minimum(), qRound(rect.top() - 24.0), scrollBar->maximum());
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
  return layout_->blockAt(QPointF(viewportPos.x(), viewportPos.y() + scrollY()));
}

const BlockLayout* EditorView::blockLayoutForNode(NodeId id) const {
  if (!layout_) {
    return nullptr;
  }
  return layout_->block(id);
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
    if (const BlockLayout* block = layout_->block(hit.blockId)) {
      hit.textOffset = selectableLength(block);
    }
  }
  return hit;
}

void EditorView::paintEvent(QPaintEvent* event) {
  PerfTimer perf("view.paint");
  Q_UNUSED(event);

  QPainter painter(viewport());
  painter.fillRect(viewport()->rect(), theme_.backgroundColor());

  if (!layout_) {
    return;
  }

  const QRectF visible = documentViewportRect();
  const QVector<const BlockLayout*> blocks = layout_->visibleBlocks(visible.adjusted(0, -80, 0, 80));
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const NodeId activeTopLevel =
      (focusMode_ && cursorPosition_.isValid()) ? layout_->topLevelBlockIdFor(cursorPosition_.blockId) : NodeId();

  for (const BlockLayout* block : blocks) {
    if (focusMode_ && activeTopLevel.isValid() && block->nodeId() != activeTopLevel) {
      painter.save();
      painter.setOpacity(0.35);
      block->paint(painter, theme_, scrollY());
      painter.restore();
    } else {
      block->paint(painter, theme_, scrollY());
    }
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
  updateMouseCursor(event->position());
  QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorView::mouseReleaseEvent(QMouseEvent* event) {
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
  const BlockLayout* block = layout_ ? layout_->block(hit.blockId) : nullptr;
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
  // Right-click spelling suggestions in rendered mode. Resolve the word under the cursor
  // via its markdown source offset, and offer replacement/ignore when it's misspelled.
  auto& checker = SpellChecker::instance();
  if (!document_ || !checker.isEnabled()) {
    QAbstractScrollArea::contextMenuEvent(event);
    return;
  }
  const HitTestResult hit = hitTest(QPointF(event->pos()));
  if (hit.zone != HitTestResult::Zone::Text || hit.sourceOffset < 0) {
    QAbstractScrollArea::contextMenuEvent(event);
    return;
  }
  const QString& markdown = document_->markdownText();
  const WordSegment seg = findWordSegment(markdown, hit.sourceOffset);
  if (!seg.isWord || seg.end <= seg.start || seg.start >= markdown.size()) {
    QAbstractScrollArea::contextMenuEvent(event);
    return;
  }
  const QString word = markdown.mid(seg.start, seg.end - seg.start);
  if (word.isEmpty() || checker.isCorrect(word)) {
    QAbstractScrollArea::contextMenuEvent(event);
    return;
  }

  const QStringList suggestions = checker.suggestions(word);
  QMenu menu(this);
  QList<QAction*> spellActions;
  const int cap = 8;
  if (suggestions.isEmpty()) {
    QAction* none = new QAction(QCoreApplication::translate("muffin::EditorView", "(no spelling suggestions)"), &menu);
    none->setEnabled(false);
    spellActions.append(none);
  } else {
    for (int i = 0; i < qMin(suggestions.size(), cap); ++i) {
      const QString suggestion = suggestions.at(i);
      const qsizetype start = seg.start;
      const qsizetype length = seg.end - seg.start;
      QAction* replaceAction = new QAction(suggestion, &menu);
      connect(replaceAction, &QAction::triggered, this, [this, start, length, suggestion]() {
        emit spellCorrectionRequested(start, length, suggestion);
      });
      spellActions.append(replaceAction);
    }
  }
  QAction* ignoreAction =
      new QAction(QCoreApplication::translate("muffin::EditorView", "Ignore \"%1\"").arg(word), &menu);
  connect(ignoreAction, &QAction::triggered, this, [this, word]() {
    SpellChecker::instance().ignoreWord(word);
    if (document_) {
      setDocument(*document_, documentPath_);  // re-layout so the squiggle clears
    }
  });
  spellActions.append(ignoreAction);

  menu.addActions(spellActions);
  menu.exec(event->globalPos());
}

void EditorView::inputMethodEvent(QInputMethodEvent* event) {
  if (!event->commitString().isEmpty()) {
    emit textCommitted(event->commitString());
  }
  event->accept();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle) {
    QRectF cursor = cursorHit_.cursorRect;
    if (cursor.isEmpty()) {
      cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
    }
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

  if (document_) {
    const int oldValue = verticalScrollBar()->value();
    layout_->setEditingHtmlBlock(editingHtmlBlockId_);
    layout_->rebuild(*document_, theme_, viewport()->width(), selection_, documentPath_);
    updateScrollBars();
    verticalScrollBar()->setValue(qBound(verticalScrollBar()->minimum(), oldValue, verticalScrollBar()->maximum()));
    if (cursorPosition_.isValid()) {
      cursorHit_ = hitForCursorPosition(*layout_, theme_, cursorPosition_);
      cursorVisible_ = cursorHit_.isValid();
    }
  } else {
    layout_ = std::make_unique<DocumentLayout>();
    updateScrollBars();
  }
  updateCodeLanguageEditor();
  updateTableToolbar();
  viewport()->update();
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

void EditorView::refreshInlineProjectionForSelectionChange(SelectionRange previousSelection) {
  QVector<NodeId> blockIds;
  addSelectionBlocks(blockIds, previousSelection);
  addSelectionBlocks(blockIds, selection_);

  bool refreshed = false;
  if (document_ && layout_ && !blockIds.isEmpty()) {
    refreshed = refreshBlocks(blockIds, *document_);
  }
  if (!refreshed) {
    updateCursorHitFromPosition();
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
    viewport()->update();
  } else {
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
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
    const BlockLayout* block = layout_->block(selection_.focus.blockId);
    if (block) {
      for (QRectF rect : block->selectionRects(selection_, theme_)) {
        rect.translate(0, -scrollY());
        painter.drawRoundedRect(rect, 2, 2);
      }
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
      for (QRectF rect : block->selectionRectsForOffsets(start, end, theme_)) {
        rect.translate(0, -scrollY());
        painter.drawRoundedRect(rect, 2, 2);
      }
    }
  }
  painter.restore();
}

void EditorView::paintCurrentTableCell(QPainter& painter) const {
  if (!layout_ || cursorHit_.zone != HitTestResult::Zone::TableCell || cursorHit_.tableRow < 0 || cursorHit_.tableColumn < 0) {
    return;
  }

  const BlockLayout* table = layout_->block(cursorHit_.blockId);
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

  QRectF cursor = cursorHit_.cursorRect;
  if (cursor.isEmpty()) {
    cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
  }
  lastPaintedCaretDocumentRect_ = cursor;
  cursor.translate(0, -scrollY());

  if (!viewport()->rect().adjusted(-4, -4, 4, 4).intersects(cursor.toAlignedRect())) {
    return;
  }

  const qreal height = qBound<qreal>(14.0, cursor.height(), 34.0);
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

  const BlockLayout* block = layout_->block(topId);
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
  if (event->mimeData()->hasUrls()) {
    const auto urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
      if (url.isLocalFile()) {
        const QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
        if (suffix == QStringLiteral("png") || suffix == QStringLiteral("jpg") ||
            suffix == QStringLiteral("jpeg") || suffix == QStringLiteral("gif") ||
            suffix == QStringLiteral("svg") || suffix == QStringLiteral("webp") ||
            suffix == QStringLiteral("bmp") || suffix == QStringLiteral("ico") ||
            suffix == QStringLiteral("tiff") || suffix == QStringLiteral("tif")) {
          event->acceptProposedAction();
          return;
        }
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
  const QString filePath = url.toLocalFile();
  const QString suffix = QFileInfo(filePath).suffix().toLower();
  if (suffix != QStringLiteral("png") && suffix != QStringLiteral("jpg") &&
      suffix != QStringLiteral("jpeg") && suffix != QStringLiteral("gif") &&
      suffix != QStringLiteral("svg") && suffix != QStringLiteral("webp") &&
      suffix != QStringLiteral("bmp") && suffix != QStringLiteral("ico") &&
      suffix != QStringLiteral("tiff") && suffix != QStringLiteral("tif")) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
  const QString alt = QFileInfo(filePath).baseName();
  emit textCommitted(QStringLiteral("![%1](%2)").arg(alt, filePath));
}

}  // namespace muffin
