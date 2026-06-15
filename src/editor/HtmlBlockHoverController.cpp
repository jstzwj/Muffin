#include "editor/HtmlBlockHoverController.h"

#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QFont>
#include <QPainter>

namespace muffin {

QRectF HtmlBlockHoverController::overlayViewportRect(const Inputs& inputs) const {
  if (!inputs.layout) {
    return {};
  }
  const NodeId id = inputs.editingHtmlBlockId.isValid() ? inputs.editingHtmlBlockId : visibleHtmlHoverBlockId_;
  const BlockLayout* block = inputs.layout->block(id);
  if (!block || block->type() != BlockType::HtmlBlock) {
    return {};
  }
  return block->rect().translated(0, -inputs.scrollY);
}

QRectF HtmlBlockHoverController::buttonViewportRect(const Inputs& inputs) const {
  const QRectF overlay = overlayViewportRect(inputs);
  if (overlay.isEmpty()) {
    return {};
  }
  return QRectF(overlay.right() - 30.0, overlay.top(), 24.0, 20.0);
}

void HtmlBlockHoverController::paint(QPainter& painter, const Inputs& inputs, const QRect& viewportRect) const {
  const QRectF overlay = overlayViewportRect(inputs);
  if (overlay.isEmpty() || !viewportRect.intersects(overlay.toAlignedRect())) {
    return;
  }

  const QRectF buttonRect = buttonViewportRect(inputs);

  painter.save();

  // Only paint the gray overlay and "HTML" label in rendered (non-editing) mode.
  // In editing mode, the overlay would obscure the syntax-highlighted source.
  if (!inputs.editingHtmlBlockId.isValid()) {
    const QRectF labelRect(overlay.right() - 76.0, overlay.top(), 70.0, 20.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(240, 242, 245, 96));
    painter.drawRect(overlay.adjusted(0, 0, 0, 0));

    painter.setBrush(inputs.theme ? inputs.theme->backgroundColor() : QColor());
    painter.drawRect(labelRect.adjusted(-2, 0, 2, 0));

    QFont badgeFont = inputs.theme ? inputs.theme->paragraphFont() : QFont();
    badgeFont.setPointSizeF(qMax<qreal>(8.0, badgeFont.pointSizeF() * 0.75));
    painter.setFont(badgeFont);
    painter.setPen(QColor(132, 138, 148));
    painter.drawText(labelRect.adjusted(4, 0, -28, 0), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("HTML"));
  }

  painter.setPen(QColor(87, 96, 110));
  painter.drawText(buttonRect, Qt::AlignCenter, QStringLiteral("</>"));
  painter.restore();
}

HtmlBlockHoverController::Dirty HtmlBlockHoverController::setHoveredBlock(NodeId blockId, const Inputs& inputs) {
  if (blockId == visibleHtmlHoverBlockId_) {
    return {};
  }
  const QRect oldRect = overlayViewportRect(inputs).adjusted(-2, -2, 2, 2).toAlignedRect();
  if (!inputs.editingHtmlBlockId.isValid()) {
    visibleHtmlHoverBlockId_ = blockId;
  }
  // Only the new position is repainted when something is actually hovered —
  // moving the mouse off every block (blockId cleared) repaints just the old
  // overlay to erase it.
  const QRect newRect =
      visibleHtmlHoverBlockId_.isValid() ? overlayViewportRect(inputs).adjusted(-2, -2, 2, 2).toAlignedRect() : QRect();
  return {oldRect, newRect};
}

HtmlBlockHoverController::Dirty HtmlBlockHoverController::clear(const Inputs& inputs) {
  const QRect oldRect = overlayViewportRect(inputs).adjusted(-2, -2, 2, 2).toAlignedRect();
  if (!inputs.editingHtmlBlockId.isValid()) {
    visibleHtmlHoverBlockId_ = {};
  }
  return {oldRect, {}};
}

}  // namespace muffin
