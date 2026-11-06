#include "editor/EditorView.h"
#include "document/MarkdownDocument.h"
#include "render/BlockLayout.h"
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QCoreApplication>

namespace muffin {
namespace {
const BlockLayout* nestedBlockAt(const BlockLayout* block, QPointF point) {
  if (!block)
    return nullptr;
  // Use the actual child rectangles, not text hit-testing: a handle also owns
  // code, images, empty items and the whitespace inside its list item.
  for (const auto& child : block->children()) {
    const auto rect = child->rect();
    if (point.y() >= rect.top() && point.y() < rect.bottom() && point.x() >= rect.left())
      return nestedBlockAt(child.get(), point);
  }
  return block;
}
}
void EditorView::setBlockHandlesEnabled(bool enabled) {
  blockHandlesEnabled_ = enabled;
  cancelBlockInteraction();
}
bool EditorView::blockHandlesAvailable() const {
  return blockHandlesEnabled_ && !editingLocked_ && !livePreview_ && !loading_ && !isLayoutPending() && document_ &&
         layout_;
}
void EditorView::cancelBlockInteraction() {
  if (!blockHandleTarget_.isValid() && !blockHandlePressed_ && !blockHandleDragging_)
    return;
  blockHandleTarget_ = {};
  blockDragSource_ = {};
  blockDropTarget_ = {};
  blockHandlePressed_ = blockHandleDragging_ = false;
  if (blockDragScrollTimer_)
    blockDragScrollTimer_->stop();
  viewport()->setToolTip({});
  viewport()->unsetCursor();
  viewport()->update();
}
BlockTarget EditorView::blockTargetAt(QPointF point) const {
  if (!blockHandlesAvailable())
    return {};
  const qreal y = qBound<qreal>(0, point.y(), viewport()->height() - 1) + scrollY();
  const auto range = layout_->slotRangeOverlappingY(y, y + 1);
  if (range.first < 0 || range.first >= layout_->slotCount())
    return {};
  const auto& structure = document_->blockStructure();
  const auto top = layout_->slotNodeId(range.first);
  const auto* block = nestedBlockAt(layout_->blockIfPromoted(top), {point.x(), y});
  const auto id = block ? block->nodeId() : top;
  return blockHandleDragging_ ? structure.targetInScope(id, blockDragSource_) : structure.targetFor(id);
}
QRectF EditorView::blockHandleRect() const {
  if (!blockHandlesAvailable() || !document_->blockStructure().resolve(blockHandleTarget_))
    return {};
  const auto* block = layout_->blockIfPromoted(blockHandleTarget_.id);
  if (!block)
    return {};
  const auto top = mapDocumentToViewport(block->rect().topLeft()).y();
  const qreal left = block->rect().left() - 26;
  return left >= 0 ? QRectF(left, qMax<qreal>(3, top), 22, 24) : QRectF{};
}
void EditorView::updateBlockHandleHover(QPointF point) {
  if (blockHandlePressed_)
    return;
  if (!blockHandlesAvailable()) {
    if (blockHandleTarget_.isValid())
      cancelBlockInteraction();
    return;
  }
  const auto handle = blockHandleRect();
  if (!handle.isEmpty() && handle.adjusted(-3, -4, 8, 4).contains(point)) {
    blockDragPosition_ = point;
    viewport()->setCursor(Qt::OpenHandCursor);
    viewport()->setToolTip(QCoreApplication::translate(
        "muffin::PrefsEditorPage", "Hover to open a block menu or drag to move the complete block."));
    viewport()->update();
    return;
  }
  const auto target = blockTargetAt(point);
  const auto* unit = document_->blockStructure().resolve(target);
  const auto next = unit && unit->movable && unit->contentEnd > unit->start ? target : BlockTarget{};
  if (next.id != blockHandleTarget_.id || next.revision != blockHandleTarget_.revision) {
    blockHandleTarget_ = next;
    viewport()->update();
  }
  blockDragPosition_ = point;
  viewport()->setToolTip({});
  if (blockHandleRect().contains(point))
    viewport()->setCursor(Qt::OpenHandCursor);
}
bool EditorView::blockHandlePress(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !blockHandleRect().contains(event->position()))
    return false;
  // Commit an IME before capturing the revision. A changed AST invalidates the
  // old target rather than letting a menu or drag mutate a different block.
  QGuiApplication::inputMethod()->commit();
  if (!document_->blockStructure().resolve(blockHandleTarget_)) {
    cancelBlockInteraction();
    return true;
  }
  stopScrollAnimation();
  blockDragSource_ = blockHandleTarget_;
  blockHandlePressPosition_ = blockDragPosition_ = event->position();
  blockHandlePressed_ = true;
  viewport()->setCursor(Qt::ClosedHandCursor);
  event->accept();
  return true;
}
void EditorView::updateBlockDrop(QPointF point) {
  blockDropTarget_ = {};
  if (!blockHandlesAvailable() || !document_->blockStructure().resolve(blockDragSource_)) {
    cancelBlockInteraction();
    return;
  }
  const auto target = blockTargetAt(point);
  const auto* block = layout_->blockIfPromoted(target.id);
  const auto* unit = document_->blockStructure().resolve(target);
  if (block && unit) {
    const bool after = point.y() + scrollY() > block->rect().center().y();
    if (document_->blockStructure().canMove(blockDragSource_, target, after)) {
      blockDropTarget_ = target;
      blockDropAfter_ = after;
    }
  }
  viewport()->update();
}
bool EditorView::blockHandleMove(QMouseEvent* event) {
  if (!blockHandlePressed_)
    return false;
  if (!(event->buttons() & Qt::LeftButton)) {
    cancelBlockInteraction();
    return false;
  }
  blockDragPosition_ = event->position();
  if (!blockHandleDragging_ &&
      (blockDragPosition_ - blockHandlePressPosition_).manhattanLength() >= QApplication::startDragDistance())
    blockHandleDragging_ = true;
  if (blockHandleDragging_) {
    updateBlockDrop(blockDragPosition_);
    if (!blockDragScrollTimer_) {
      blockDragScrollTimer_ = new QTimer(this);
      blockDragScrollTimer_->setInterval(30);
      connect(blockDragScrollTimer_, &QTimer::timeout, this, [this] {
        if (!blockHandleDragging_)
          return;
        const qreal y = blockDragPosition_.y();
        const int direction = y < 24 ? -1 : y > viewport()->height() - 24 ? 1 : 0;
        if (direction) {
          verticalScrollBar()->setValue(verticalScrollBar()->value() + direction * verticalScrollBar()->singleStep());
          updateBlockDrop(blockDragPosition_);
        }
      });
    }
    if (blockHandleDragging_ && (blockDragPosition_.y() < 24 || blockDragPosition_.y() > viewport()->height() - 24))
      blockDragScrollTimer_->start();
    else
      blockDragScrollTimer_->stop();
  }
  event->accept();
  return true;
}
bool EditorView::blockHandleRelease(QMouseEvent* event) {
  if (!blockHandlePressed_ || event->button() != Qt::LeftButton)
    return false;
  if (blockHandleDragging_)
    updateBlockDrop(event->position());
  const auto source = blockDragSource_, destination = blockDropTarget_;
  const bool dragging = blockHandleDragging_, after = blockDropAfter_;
  const auto global = event->globalPosition().toPoint();
  cancelBlockInteraction();
  if (dragging) {
    if (source.isValid() && destination.isValid())
      emit blockMoveRequested(source, destination, after);
  } else if (source.isValid()) {
    emit blockMenuRequested(source, global);
  }
  event->accept();
  return true;
}
void EditorView::paintBlockHandle(QPainter& painter) const {
  const auto handle = blockHandleRect();
  if (handle.isEmpty())
    return;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  QColor muted = theme_.textColor();
  muted.setAlphaF(.55f);
  if (blockHandleDragging_) {
    if (const auto* block = layout_->blockIfPromoted(blockDragSource_.id)) {
      auto color = theme_.selectionColor();
      color.setAlphaF(.16f);
      painter.fillRect(block->rect().translated(0, -scrollY()), color);
    }
  }
  if (handle.contains(blockDragPosition_) || blockHandlePressed_) {
    auto color = muted;
    color.setAlphaF(.10f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(handle, 4, 4);
  }
  painter.setPen(Qt::NoPen);
  painter.setBrush(muted);
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 2; ++col)
      painter.drawEllipse(handle.center() + QPointF(col * 5 - 2.5, row * 5 - 5), 1.25, 1.25);
  if (blockHandleDragging_ && blockDropTarget_.isValid()) {
    if (const auto* block = layout_->blockIfPromoted(blockDropTarget_.id)) {
      const auto rect = block->rect().translated(0, -scrollY());
      const auto y = blockDropAfter_ ? rect.bottom() : rect.top();
      painter.setPen(QPen(theme_.linkColor(), 2));
      painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }
  }
  painter.restore();
}
} // namespace muffin
