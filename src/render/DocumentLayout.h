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
  struct BlockRebuildResult {
    bool rebuilt = false;
    NodeId blockId;
    QRectF oldRect;
    QRectF newRect;
    QRectF shiftedRect;
    qreal heightDelta = 0;
  };

  void rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth);
  void rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, SelectionRange selection);
  bool relayoutForViewportWidth(const RenderTheme& theme, qreal viewportWidth);
  BlockRebuildResult rebuildBlock(NodeId blockId, const MarkdownDocument& document, const RenderTheme& theme, SelectionRange selection);

  qreal pageLeft() const;
  qreal pageWidth() const;
  qreal totalHeight() const;

  const std::vector<std::unique_ptr<BlockLayout>>& blocks() const;
  QVector<const BlockLayout*> visibleBlocks(QRectF documentViewport) const;
  const BlockLayout* block(NodeId id) const;
  const BlockLayout* blockAt(QPointF documentPos) const;
  HitTestResult hitTest(QPointF documentPos, const RenderTheme& theme) const;

private:
  const MarkdownNode* topLevelBlockFor(NodeId id, const MarkdownDocument& document) const;
  void indexTopLevelBlock(const BlockLayout& block, qsizetype index);
  void indexLayoutBlock(const BlockLayout& block);
  void rebuildIndexes();

  const MarkdownDocument* document_ = nullptr;
  qreal viewportWidth_ = 0;
  std::vector<std::unique_ptr<BlockLayout>> blocks_;
  QHash<NodeId, qsizetype> topLevelIndex_;
  QHash<NodeId, const BlockLayout*> layoutIndex_;
  qreal pageLeft_ = 0;
  qreal pageWidth_ = 0;
  qreal totalHeight_ = 0;
};

}  // namespace muffin
