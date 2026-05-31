#include "render/DocumentLayout.h"

#include "render/BlockLayoutBuilder.h"

#include <cmath>

namespace muffin {

void DocumentLayout::rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth) {
  blocks_.clear();
  index_.clear();

  const qreal horizontalInset = qMin<qreal>(64.0, qMax<qreal>(16.0, viewportWidth * 0.08));
  pageWidth_ = qMin(theme.pageWidth(), qMax<qreal>(320.0, viewportWidth - horizontalInset * 2.0));
  pageLeft_ = qMax<qreal>(0.0, (viewportWidth - pageWidth_) / 2.0);

  BlockLayoutBuilder builder;
  qreal cursorY = theme.topMargin();
  for (const auto& child : document.root().children()) {
    if (child->type() == BlockType::Heading && cursorY > theme.topMargin()) {
      cursorY += child->headingLevel() <= 2 ? theme.blockSpacing() * 1.4 : theme.blockSpacing() * 0.7;
    }
    auto block = builder.build(*child, theme, pageLeft_, cursorY, pageWidth_);
    const qreal afterSpacing = child->type() == BlockType::Heading ? theme.blockSpacing() * 0.65 : theme.blockSpacing();
    cursorY = block->rect().bottom() + afterSpacing;
    indexBlock(*block);
    blocks_.push_back(std::move(block));
  }

  totalHeight_ = qMax(cursorY + theme.bottomMargin(), theme.topMargin() + theme.bottomMargin());
}

qreal DocumentLayout::pageLeft() const {
  return pageLeft_;
}

qreal DocumentLayout::pageWidth() const {
  return pageWidth_;
}

qreal DocumentLayout::totalHeight() const {
  return totalHeight_;
}

const std::vector<std::unique_ptr<BlockLayout>>& DocumentLayout::blocks() const {
  return blocks_;
}

QVector<const BlockLayout*> DocumentLayout::visibleBlocks(QRectF documentViewport) const {
  QVector<const BlockLayout*> result;
  for (const auto& block : blocks_) {
    if (block->intersects(documentViewport)) {
      result.push_back(block.get());
    }
  }
  return result;
}

const BlockLayout* DocumentLayout::block(NodeId id) const {
  return index_.value(id, nullptr);
}

const BlockLayout* DocumentLayout::blockAt(QPointF documentPos) const {
  for (const auto& block : blocks_) {
    if (block->rect().contains(documentPos)) {
      return block.get();
    }
  }
  return nullptr;
}

HitTestResult DocumentLayout::hitTest(QPointF documentPos, const RenderTheme& theme) const {
  for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
    const BlockLayout& block = **it;
    if (block.rect().adjusted(0, -theme.blockSpacing(), 0, theme.blockSpacing()).contains(documentPos)) {
      HitTestResult result = block.hitTest(documentPos, theme);
      if (result.isValid()) {
        return result;
      }
    }
  }

  if (blocks_.empty()) {
    return {};
  }

  const BlockLayout* nearest = blocks_.front().get();
  qreal bestDistance = std::abs(nearest->rect().center().y() - documentPos.y());
  for (const auto& block : blocks_) {
    const qreal distance = std::abs(block->rect().center().y() - documentPos.y());
    if (distance < bestDistance) {
      nearest = block.get();
      bestDistance = distance;
    }
  }
  return nearest->hitTest(QPointF(qBound(nearest->rect().left(), documentPos.x(), nearest->rect().right()), nearest->rect().center().y()), theme);
}

void DocumentLayout::indexBlock(const BlockLayout& block) {
  if (block.nodeId().isValid()) {
    index_.insert(block.nodeId(), &block);
  }
  for (const auto& child : block.children()) {
    indexBlock(*child);
  }
}

}  // namespace muffin
