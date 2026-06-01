#pragma once

#include "document/MarkdownDocument.h"
#include "editor/CursorPosition.h"
#include "render/BlockLayout.h"
#include "theme/RenderTheme.h"

#include <QHash>
#include <QRectF>
#include <QVector>

#include <memory>
#include <vector>

namespace muffin {

class DocumentLayout {
public:
  void rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth);
  void rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, CursorPosition activeCursor);

  qreal pageLeft() const;
  qreal pageWidth() const;
  qreal totalHeight() const;

  const std::vector<std::unique_ptr<BlockLayout>>& blocks() const;
  QVector<const BlockLayout*> visibleBlocks(QRectF documentViewport) const;
  const BlockLayout* block(NodeId id) const;
  const BlockLayout* blockAt(QPointF documentPos) const;
  HitTestResult hitTest(QPointF documentPos, const RenderTheme& theme) const;

private:
  void indexBlock(const BlockLayout& block);

  const MarkdownDocument* document_ = nullptr;
  std::vector<std::unique_ptr<BlockLayout>> blocks_;
  QHash<NodeId, const BlockLayout*> index_;
  qreal pageLeft_ = 0;
  qreal pageWidth_ = 0;
  qreal totalHeight_ = 0;
};

}  // namespace muffin
