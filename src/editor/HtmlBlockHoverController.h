#pragma once

// Owns the rendered-HTML block hover/overlay state that EditorView used to track
// inline. A rendered HTML block shows a faint "HTML" badge plus a "</>" toggle
// button; hovering a block marks it as the visible overlay, clicking the button
// (or the block) toggles source-editing. While a block is being edited the
// overlay is fixed to that block and hover tracking is suppressed.
//
// The controller holds only its own state (the currently hovered block id). It
// does not own the layout, theme, editing id, or scroll position — those are
// EditorView's concerns and are passed in via Inputs on every query, so the
// repaints stay driven by the view.

#include "document/NodeId.h"

#include <QRect>
#include <QRectF>

class QPainter;
class QWidget;

namespace muffin {

class DocumentLayout;
class RenderTheme;

class HtmlBlockHoverController {
public:
  // Read-only dependencies resolved per call. Pointers are non-owning and may be
  // null (queries return empty rects in that case).
  struct Inputs {
    const DocumentLayout* layout = nullptr;
    const RenderTheme* theme = nullptr;
    NodeId editingHtmlBlockId;
    qreal scrollY = 0.0;
  };

  // Dirty rects (old + new hover position) the caller must repaint after a state
  // change. Either may be empty (no change / nothing to repaint).
  struct Dirty {
    QRect oldRect;
    QRect newRect;
  };

  // Viewport-coordinate rect of the block the overlay currently covers (the
  // editing block when editing, otherwise the hovered block).
  QRectF overlayViewportRect(const Inputs& inputs) const;
  // Viewport-coordinate rect of the "</>" toggle button, anchored top-right of
  // the overlay.
  QRectF buttonViewportRect(const Inputs& inputs) const;
  // Paint the overlay (badge + label + button) for the current overlay rect,
  // clipped to the viewport.
  void paint(QPainter& painter, const Inputs& inputs, const QRect& viewportRect) const;

  // Track a hovered block (NodeId() to mean "no HTML block under cursor").
  // Returns the dirty rects to repaint. No-op (returns empty) when the hover is
  // unchanged. Hover state is only advanced when no block is being edited.
  Dirty setHoveredBlock(NodeId blockId, const Inputs& inputs);
  // Drop hover tracking. Returns the dirty rect of the previous overlay.
  Dirty clear(const Inputs& inputs);

  NodeId visibleBlockId() const { return visibleHtmlHoverBlockId_; }

private:
  NodeId visibleHtmlHoverBlockId_;
};

}  // namespace muffin
