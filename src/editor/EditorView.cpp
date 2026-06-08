#include "editor/EditorView.h"

#include "document/MarkdownDocument.h"
#include "editor/CodeLanguageEditor.h"
#include "editor/TableToolbar.h"

#include <QApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QPainter>
#include <QLoggingCategory>
#include <QScrollBar>
#include <QPropertyAnimation>
#include <QVector>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <QFontMetricsF>
#include <QTextLayout>
#include <QTextOption>
#include <QUrl>
#include <QKeyEvent>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(viewPerf, "muffin.perf", QtWarningMsg)

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

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin) {
  const QFontMetricsF metrics(font);
  const qreal lineHeight = qMax<qreal>(14.0, metrics.height());
  offset = qBound<qsizetype>(0, offset, literal.size());

  qsizetype lineStart = 0;
  int line = 0;
  for (qsizetype i = 0; i < offset && i < literal.size(); ++i) {
    if (literal.at(i) == QLatin1Char('\n')) {
      ++line;
      lineStart = i + 1;
    }
  }

  const qreal x = metrics.horizontalAdvance(literal.mid(lineStart, offset - lineStart));
  return QRectF(origin.x() + x, origin.y() + line * lineHeight, 1.0, lineHeight);
}

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin, qreal width, qreal lineHeight) {
  const QFontMetricsF metrics(font);
  const qreal fallbackHeight = qMax<qreal>(14.0, lineHeight);
  offset = qBound<qsizetype>(0, offset, literal.size());

  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

  const QStringList physicalLines = literal.isEmpty() ? QStringList{QString()} : literal.split(QLatin1Char('\n'));
  const qreal lineWidth = qMax<qreal>(1.0, width);
  qreal y = 0.0;
  qsizetype globalStart = 0;
  QRectF fallback(origin.x(), origin.y(), 1.0, fallbackHeight);

  for (const QString& sourceLine : physicalLines) {
    const QString lineText = sourceLine.isEmpty() ? QStringLiteral(" ") : sourceLine;
    QTextLayout layout(lineText, font);
    layout.setTextOption(option);
    layout.beginLayout();
    bool producedLine = false;
    while (true) {
      QTextLine textLine = layout.createLine();
      if (!textLine.isValid()) {
        break;
      }
      textLine.setLineWidth(lineWidth);
      const qreal height = qMax<qreal>(fallbackHeight, textLine.height());
      textLine.setPosition(QPointF(0.0, y + (height - textLine.height()) * 0.5));
      producedLine = true;
      const qsizetype visualStart = globalStart + textLine.textStart();
      const qsizetype visualLength = qMin<qsizetype>(textLine.textLength(), sourceLine.size() - textLine.textStart());
      const qsizetype visualEnd = visualStart + visualLength;
      fallback = QRectF(origin.x() + metrics.horizontalAdvance(sourceLine), origin.y() + y, 1.0, height);
      if (offset >= visualStart && offset <= visualEnd) {
        const qsizetype localOffset = qBound<qsizetype>(visualStart, offset, visualEnd);
        const qreal x = metrics.horizontalAdvance(literal.mid(visualStart, localOffset - visualStart));
        layout.endLayout();
        return QRectF(origin.x() + x, origin.y() + y, 1.0, height);
      }
      y += height;
    }
    layout.endLayout();
    if (!producedLine) {
      if (offset == globalStart) {
        return QRectF(origin.x(), origin.y() + y, 1.0, fallbackHeight);
      }
      y += fallbackHeight;
    }
    globalStart += sourceLine.size() + 1;
  }
  return fallback;
}

bool isSelectableZone(HitTestResult::Zone zone) {
  return zone == HitTestResult::Zone::Text || zone == HitTestResult::Zone::Code || zone == HitTestResult::Zone::Math ||
         zone == HitTestResult::Zone::Html || zone == HitTestResult::Zone::FrontMatter || zone == HitTestResult::Zone::TableCell;
}

QPointF tableCellTextOrigin(const BlockLayout::TableCellLayout& cell, const RenderTheme& theme) {
  const QRectF contentRect = cell.rect.marginsRemoved(theme.tableCellPadding());
  qreal textX = contentRect.left();
  if (cell.alignment == TableAlignment::Right) {
    textX = contentRect.right() - cell.text.size().width();
  } else if (cell.alignment == TableAlignment::Center) {
    textX = contentRect.left() + (contentRect.width() - cell.text.size().width()) / 2.0;
  }
  return QPointF(qMax(contentRect.left(), textX), contentRect.top());
}

qsizetype selectableLength(const BlockLayout* block) {
  if (!block) {
    return 0;
  }
  if (const InlineLayout* inlineLayout = block->inlineLayout()) {
    return inlineLayout->plainText().size();
  }
  switch (block->type()) {
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::MathBlock:
    case BlockType::HtmlBlock:
      return block->literal().size();
    case BlockType::Table:
      return 1;
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition: {
      const DefinitionBlock& def = block->definition();
      if (!def.markerRange.isValid()) {
        return 0;
      }
      const qsizetype end = def.sourceRange.isValid()
                                ? def.sourceRange.end
                                : qMax(def.markerRange.end,
                                       qMax(def.destinationRange.end,
                                            qMax(def.titleRange.end, def.noteRange.end)));
      return qMax<qsizetype>(0, end - def.markerRange.start);
    }
    default:
      return 0;
  }
}

QRect viewportUpdateRect(QRectF documentRect, qreal scrollY, const QSize& viewportSize) {
  if (documentRect.isNull() || documentRect.isEmpty()) {
    return {};
  }
  documentRect.translate(0, -scrollY);
  return documentRect.adjusted(-4, -4, 4, 4).toAlignedRect().intersected(QRect(QPoint(0, 0), viewportSize));
}

template <typename RebuildResult>
void addRebuildDirtyRect(QRect& dirty, const RebuildResult& result, QRectF documentViewport, qreal scrollY, const QSize& viewportSize) {
  dirty = dirty.united(viewportUpdateRect(result.oldRect.united(result.newRect), scrollY, viewportSize));
  if (!result.shiftedRect.isEmpty()) {
    dirty = dirty.united(viewportUpdateRect(result.shiftedRect.intersected(documentViewport), scrollY, viewportSize));
  }
}

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

  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
    updateCodeLanguageEditor();
    updateTableToolbar();
  });
  connect(verticalScrollBar(), &QScrollBar::sliderPressed, this, [this] {
    stopScrollAnimation();
  });
}

void EditorView::setDocument(const MarkdownDocument& document) {
  document_ = &document;
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

void EditorView::applySelectionRange(SelectionRange selection) {
  const SelectionRange previousSelection = selection_;
  selection_ = selection;
  cursorPosition_ = selection.focus;
  refreshInlineProjectionForSelectionChange(previousSelection);
  updateTableToolbar();
}

void EditorView::clearCursor() {
  cursorPosition_ = {};
  selection_ = {};
  cursorHit_ = {};
  cursorVisible_ = false;
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

void EditorView::scrollToCursorCentered() {
  if (!cursorHit_.isValid()) {
    return;
  }

  QRectF cursor = cursorHit_.cursorRect;
  if (cursor.isEmpty()) {
    cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
  }
  if (cursor.isEmpty()) {
    return;
  }

  QScrollBar* scrollBar = verticalScrollBar();
  const qreal cursorCenterY = cursor.center().y();
  const qreal viewportHeight = static_cast<qreal>(viewport()->height());
  const int target = qBound(scrollBar->minimum(), qRound(cursorCenterY - viewportHeight / 2.0), scrollBar->maximum());
  scrollBar->setValue(target);
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
  if (!cursorHit_.isValid()) {
    return;
  }

  QRectF cursor = cursorHit_.cursorRect;
  if (cursor.isEmpty()) {
    cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
  }
  if (cursor.isEmpty()) {
    return;
  }

  QScrollBar* scrollBar = verticalScrollBar();
  const qreal cursorCenterY = cursor.center().y();
  const qreal viewportHeight = static_cast<qreal>(viewport()->height());
  const int target = qBound(scrollBar->minimum(), qRound(cursorCenterY - viewportHeight / 2.0), scrollBar->maximum());

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

HitTestResult EditorView::hitTest(QPointF viewportPos) const {
  if (!layout_) {
    return {};
  }
  return layout_->hitTest(QPointF(viewportPos.x(), viewportPos.y() + scrollY()), theme_);
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
  paintInsertionCursor(painter);
  paintHeadingBadge(painter);
}

bool EditorView::event(QEvent* event) {
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
    const HitTestResult hit = hitTest(event->position());
    if (event->modifiers().testFlag(Qt::ControlModifier) && hit.isValid() && !hit.linkHref.isEmpty()) {
      QDesktopServices::openUrl(QUrl(hit.linkHref));
      event->accept();
      return;
    }
    if (event->modifiers().testFlag(Qt::ShiftModifier) && hit.isValid() && isSelectableZone(hit.zone) && cursorPosition_.isValid()) {
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
    if (hit.isValid() && isSelectableZone(hit.zone)) {
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
  updateMouseCursor(event->position());
  QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && (dragSelectionPending_ || draggingSelection_)) {
    if (draggingSelection_) {
      updateDragSelection(event->position());
    }
    dragSelectionPending_ = false;
    draggingSelection_ = false;
    event->accept();
    return;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

namespace {

auto isWordChar(QChar ch) -> bool {
  return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

QPair<qsizetype, qsizetype> wordRangeAtOffset(const QString& text, qsizetype offset) {
  if (text.isEmpty() || offset < 0 || offset >= text.size()) {
    return {qMax<qsizetype>(0, offset), qMax<qsizetype>(0, offset)};
  }

  const QChar c = text[offset];
  qsizetype start = offset;
  qsizetype end = offset + 1;

  if (isWordChar(c)) {
    while (start > 0 && isWordChar(text[start - 1])) {
      --start;
    }
    while (end < text.size() && isWordChar(text[end])) {
      ++end;
    }
  } else if (c.isSpace()) {
    while (start > 0 && text[start - 1].isSpace()) {
      --start;
    }
    while (end < text.size() && text[end].isSpace()) {
      ++end;
    }
  }

  return {start, end};
}

}  // namespace

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
    layout_->rebuild(*document_, theme_, viewport()->width(), selection_);
    updateScrollBars();
    verticalScrollBar()->setValue(qBound(verticalScrollBar()->minimum(), oldValue, verticalScrollBar()->maximum()));
    if (cursorPosition_.isValid()) {
      cursorHit_ = hitForCursorPosition(cursorPosition_);
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
  if (typewriterMode_) {
    // Allow scrolling past document boundaries so the cursor can always be
    // centered: negative scroll (empty space above) and extra range (empty
    // space below).  Half a viewport on each side is enough to center the
    // cursor at the very first or last line.
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
  cursorHit_ = hitForCursorPosition(cursorPosition_);
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
    const bool anchorFirst = blockComesBefore(selection_.anchor.blockId, selection_.focus.blockId);
    const QVector<const BlockLayout*> selectedBlocks = blocksBetween(selection_.anchor.blockId, selection_.focus.blockId);
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
    return;
  }

  QRectF cursor = cursorHit_.cursorRect;
  if (cursor.isEmpty()) {
    cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
  }
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

HitTestResult EditorView::hitForCursorPosition(CursorPosition position) const {
  if (!layout_ || !position.isValid()) {
    return {};
  }

  const BlockLayout* block = layout_->block(position.blockId);
  if (!block) {
    return {};
  }

  HitTestResult hit;
  hit.blockId = position.blockId;
  hit.textNodeId = position.text.nodeId.isValid() ? position.text.nodeId : position.blockId;
  hit.textOffset = position.text.textOffset;
  hit.sourceOffset = position.text.sourceOffset;
  hit.blockRect = block->rect();
  hit.zone = HitTestResult::Zone::Block;

  switch (block->type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::ListItem:
      hit.zone = position.text.inMeta ? HitTestResult::Zone::Marker : HitTestResult::Zone::Text;
      if (const InlineLayout* inlineLayout = block->inlineLayout()) {
        const qreal textLeft = block->hasListMarker() ? block->rect().left() + theme_.listIndent() : block->rect().left();
        const qsizetype localSourceOffset =
            position.text.sourceOffset >= 0 && block->contentSourceStart() >= 0 ? position.text.sourceOffset - block->contentSourceStart() : -1;
        hit.cursorRect = localSourceOffset >= 0
                             ? inlineLayout->cursorRectForSourceOffset(localSourceOffset).translated(QPointF(textLeft, block->rect().top()))
                             : inlineLayout->cursorRect(position.text.textOffset).translated(QPointF(textLeft, block->rect().top()));
      }
      break;
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
      hit.zone = block->type() == BlockType::FrontMatter ? HitTestResult::Zone::FrontMatter : HitTestResult::Zone::Code;
      {
        const QRectF contentRect = block->literalContentRect(theme_);
        hit.cursorRect =
            literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(), contentRect.topLeft(), contentRect.width(), theme_.codeLineHeight());
      }
      break;
    case BlockType::MathBlock:
      hit.zone = HitTestResult::Zone::Math;
      if (block->literalEditing()) {
        const QRectF contentRect = block->literalContentRect(theme_);
        hit.cursorRect =
            literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(), contentRect.topLeft(), contentRect.width(), theme_.codeLineHeight());
      } else {
        const qsizetype offset = qBound<qsizetype>(0, position.text.textOffset, block->literal().size());
        const qreal x = offset <= block->literal().size() / 2 ? block->rect().left() : block->rect().right();
        hit.cursorRect = QRectF(x, block->rect().top(), 1.0, block->rect().height());
      }
      break;
    case BlockType::HtmlBlock:
      hit.zone = HitTestResult::Zone::Html;
      {
        const QRectF contentRect = block->literalContentRect(theme_);
        hit.cursorRect =
            literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(), contentRect.topLeft(), contentRect.width(), theme_.codeLineHeight());
      }
      break;
    case BlockType::Table:
      hit.zone = HitTestResult::Zone::TableCell;
      hit.tableRow = -1;
      hit.tableColumn = -1;
      for (int row = 0; row < static_cast<int>(block->tableRows().size()); ++row) {
        const auto& tableRow = block->tableRows().at(static_cast<size_t>(row));
        for (int column = 0; column < static_cast<int>(tableRow.cells.size()); ++column) {
          if (tableRow.cells.at(static_cast<size_t>(column)).nodeId == hit.textNodeId) {
            hit.tableRow = row;
            hit.tableColumn = column;
            break;
          }
        }
        if (hit.tableRow >= 0) {
          break;
        }
      }
      if (hit.tableRow >= 0 && hit.tableColumn >= 0) {
        const QRectF cellRect = block->tableCellRect(hit.tableRow, hit.tableColumn);
        for (const auto& tableRow : block->tableRows()) {
          for (const auto& cell : tableRow.cells) {
            if (cell.nodeId == hit.textNodeId) {
              const QPointF textOrigin = tableCellTextOrigin(cell, theme_);
              const qsizetype localSourceOffset =
                  position.text.sourceOffset >= 0 && cell.contentSourceStart >= 0 ? position.text.sourceOffset - cell.contentSourceStart : -1;
              hit.cursorRect = localSourceOffset >= 0
                                   ? cell.text.cursorRectForSourceOffset(localSourceOffset).translated(textOrigin)
                                   : cell.text.cursorRect(position.text.textOffset).translated(textOrigin);
              break;
            }
          }
        }
        if (hit.cursorRect.isEmpty()) {
          hit.cursorRect = QRectF(cellRect.left() + 6.0, cellRect.top() + 4.0, 1.0, qMax<qreal>(14.0, cellRect.height() - 8.0));
        }
      }
      break;
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      hit.zone = HitTestResult::Zone::Text;
      hit.cursorRect = block->definitionCursorRectForSourceOffset(position.text.sourceOffset, theme_);
      break;
    default:
      break;
  }

  if (hit.cursorRect.isEmpty()) {
    hit.cursorRect = QRectF(hit.blockRect.left(), hit.blockRect.top(), 1.0, hit.blockRect.height());
  }
  return hit;
}

QVector<const BlockLayout*> EditorView::blocksBetween(NodeId first, NodeId last) const {
  QVector<const BlockLayout*> result;
  if (!layout_ || !first.isValid() || !last.isValid()) {
    return result;
  }

  const BlockLayout* firstBlock = layout_->block(first);
  const BlockLayout* lastBlock = layout_->block(last);
  if (!firstBlock || !lastBlock) {
    return result;
  }

  NodeId startId = first;
  NodeId endId = last;
  if (firstBlock->rect().top() > lastBlock->rect().top() ||
      (qFuzzyCompare(firstBlock->rect().top(), lastBlock->rect().top()) && firstBlock->rect().left() > lastBlock->rect().left())) {
    qSwap(startId, endId);
  }

  bool collecting = false;
  const auto collect = [&](const auto& self, const BlockLayout& block) -> void {
    if (block.nodeId() == startId) {
      collecting = true;
    }
    if (collecting) {
      result.push_back(&block);
    }
    if (block.nodeId() == endId) {
      collecting = false;
      return;
    }
    for (const auto& child : block.children()) {
      self(self, *child);
      if (!collecting && !result.isEmpty() && result.last()->nodeId() == endId) {
        return;
      }
    }
  };

  for (const auto& block : layout_->blocks()) {
    collect(collect, *block);
    if (!result.isEmpty() && result.last()->nodeId() == endId) {
      break;
    }
  }
  return result;
}

bool EditorView::blockComesBefore(NodeId first, NodeId second) const {
  if (first == second) {
    return true;
  }

  const QVector<const BlockLayout*> range = blocksBetween(first, second);
  return !range.isEmpty() && range.first()->nodeId() == first;
}

void EditorView::updateDragSelection(QPointF viewportPos) {
  if (!dragAnchorHit_.isValid()) {
    return;
  }

  const HitTestResult focusHit = hitTest(viewportPos);
  if (!focusHit.isValid() || !isSelectableZone(focusHit.zone)) {
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
  const HitTestResult hit = hitTest(viewportPos);
  if (hit.isValid() && !hit.linkHref.isEmpty()) {
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

}  // namespace muffin
