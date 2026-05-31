#include "editor/EditorView.h"

#include "document/MarkdownDocument.h"

#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <QMouseEvent>

#include <cmath>

namespace muffin {

EditorView::EditorView(QWidget* parent) : QAbstractScrollArea(parent), layout_(std::make_unique<DocumentLayout>()) {
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  viewport()->setAutoFillBackground(false);
  setBackgroundRole(QPalette::Base);
  applyScrollBarStyle();
}

void EditorView::setDocument(const MarkdownDocument& document) {
  document_ = &document;
  rebuildLayout();
}

void EditorView::setZoomPercent(int percent) {
  theme_.setZoomPercent(percent);
  applyScrollBarStyle();
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  rebuildLayout();
}

void EditorView::setTheme(RenderTheme theme) {
  const int zoom = theme_.zoomPercent();
  theme_ = std::move(theme);
  theme_.setZoomPercent(zoom);
  applyScrollBarStyle();
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  rebuildLayout();
}

QRectF EditorView::nodeRect(NodeId id) const {
  if (!layout_) {
    return {};
  }
  const BlockLayout* block = layout_->block(id);
  return block ? block->rect() : QRectF();
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
  for (const BlockLayout* block : blocks) {
    block->paint(painter, theme_, scrollY());
  }
}

void EditorView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  rebuildLayout();
}

void EditorView::wheelEvent(QWheelEvent* event) {
  QAbstractScrollArea::wheelEvent(event);
}

void EditorView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    emit blockClicked(hitTest(event->position()));
  }
  QAbstractScrollArea::mousePressEvent(event);
}

void EditorView::rebuildLayout() {
  if (!layout_) {
    layout_ = std::make_unique<DocumentLayout>();
  }

  if (document_) {
    const int oldValue = verticalScrollBar()->value();
    layout_->rebuild(*document_, theme_, viewport()->width());
    updateScrollBars();
    verticalScrollBar()->setValue(qMin(oldValue, verticalScrollBar()->maximum()));
  } else {
    layout_ = std::make_unique<DocumentLayout>();
    updateScrollBars();
  }
  viewport()->update();
}

void EditorView::updateScrollBars() {
  const int pageStep = qMax(1, viewport()->height());
  const int maximum = layout_ ? qMax(0, static_cast<int>(std::ceil(layout_->totalHeight() - viewport()->height()))) : 0;
  verticalScrollBar()->setPageStep(pageStep);
  verticalScrollBar()->setSingleStep(qMax(16, pageStep / 12));
  verticalScrollBar()->setRange(0, maximum);
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

}  // namespace muffin
