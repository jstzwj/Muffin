#include "render/DocumentLayout.h"

#include "render/BlockLayoutBuilder.h"

#include <cmath>

namespace muffin {
namespace {

qreal spacingAfterBlock(const MarkdownNode& node, const RenderTheme& theme) {
  return node.type() == BlockType::Heading ? theme.blockSpacing() * 0.65 : theme.blockSpacing();
}

qreal spacingBeforeBlock(const MarkdownNode& node, const RenderTheme& theme, qreal cursorY) {
  if (node.type() != BlockType::Heading || cursorY <= theme.topMargin()) {
    return 0;
  }
  if (node.headingLevel() == 2) {
    return theme.blockSpacing() * 1.1;
  }
  return node.headingLevel() < 2 ? theme.blockSpacing() * 1.25 : theme.blockSpacing() * 0.7;
}

}  // namespace

void DocumentLayout::rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth) {
  rebuild(document, theme, viewportWidth, SelectionRange());
}

void DocumentLayout::rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, SelectionRange selection) {
  document_ = &document;
  viewportWidth_ = viewportWidth;
  blocks_.clear();
  topLevelIndex_.clear();
  layoutIndex_.clear();

  const qreal horizontalInset = qMin<qreal>(64.0, qMax<qreal>(16.0, viewportWidth * 0.08));
  pageWidth_ = qMin(theme.pageWidth(), qMax<qreal>(320.0, viewportWidth - horizontalInset * 2.0));
  pageLeft_ = qMax<qreal>(16.0, (viewportWidth - pageWidth_) / 2.0 - 12.0);

  BlockLayoutBuilder builder;
  builder.setMarkdownText(document.markdownText());
  builder.setSelection(selection);
  qreal cursorY = theme.topMargin();
  for (const auto& child : document.root().children()) {
    cursorY += spacingBeforeBlock(*child, theme, cursorY);
    auto block = builder.build(*child, theme, pageLeft_, cursorY, pageWidth_);
    cursorY = block->rect().bottom() + spacingAfterBlock(*child, theme);
    indexTopLevelBlock(*block, static_cast<qsizetype>(blocks_.size()));
    blocks_.push_back(std::move(block));
  }

  totalHeight_ = qMax(cursorY + theme.bottomMargin(), theme.topMargin() + theme.bottomMargin());
}

DocumentLayout::BlockRebuildResult DocumentLayout::rebuildBlock(
    NodeId blockId,
    const MarkdownDocument& document,
    const RenderTheme& theme,
    SelectionRange selection) {
  BlockRebuildResult result;
  if (!blockId.isValid() || document_ != &document || blocks_.empty() || viewportWidth_ <= 0) {
    return result;
  }

  const MarkdownNode* node = topLevelBlockFor(blockId, document);
  if (!node) {
    return result;
  }
  auto indexIt = topLevelIndex_.constFind(node->id());
  if (indexIt == topLevelIndex_.constEnd()) {
    return result;
  }
  const qsizetype index = indexIt.value();

  BlockLayoutBuilder builder;
  builder.setMarkdownText(document.markdownText());
  builder.setSelection(selection);
  std::unique_ptr<BlockLayout>& slot = blocks_.at(static_cast<size_t>(index));
  result.blockId = node->id();
  result.oldRect = slot->rect();
  auto replacement = builder.build(*node, theme, slot->rect().left(), slot->rect().top(), pageWidth_);
  result.newRect = replacement->rect();

  qreal newNextTop = replacement->rect().bottom() + spacingAfterBlock(*node, theme);
  const auto& documentBlocks = document.root().children();
  if (index + 1 < static_cast<qsizetype>(documentBlocks.size())) {
    newNextTop += spacingBeforeBlock(*documentBlocks.at(static_cast<size_t>(index + 1)), theme, newNextTop);
  }
  const qreal delta =
      index + 1 < static_cast<qsizetype>(blocks_.size())
          ? newNextTop - blocks_.at(static_cast<size_t>(index + 1))->rect().top()
          : qMax(newNextTop + theme.bottomMargin(), theme.topMargin() + theme.bottomMargin()) - totalHeight_;
  result.heightDelta = delta;
  slot = std::move(replacement);

  QRectF shiftedRect;
  for (qsizetype i = index + 1; i < static_cast<qsizetype>(blocks_.size()); ++i) {
    BlockLayout& block = *blocks_.at(static_cast<size_t>(i));
    const QRectF oldRect = block.rect();
    block.translateY(delta);
    shiftedRect = shiftedRect.united(oldRect).united(block.rect());
  }
  result.shiftedRect = shiftedRect;
  if (index + 1 < static_cast<qsizetype>(blocks_.size())) {
    totalHeight_ += delta;
  } else {
    totalHeight_ = qMax(newNextTop + theme.bottomMargin(), theme.topMargin() + theme.bottomMargin());
  }

  rebuildIndexes();
  result.rebuilt = true;
  return result;
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
  return layoutIndex_.value(id, nullptr);
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

const MarkdownNode* DocumentLayout::topLevelBlockFor(NodeId id, const MarkdownDocument& document) const {
  const MarkdownNode* node = document.node(id);
  if (!node) {
    return nullptr;
  }
  while (node->parent() && node->parent()->type() != BlockType::Document) {
    node = node->parent();
  }
  return node && node->parent() && node->parent()->type() == BlockType::Document ? node : nullptr;
}

void DocumentLayout::indexTopLevelBlock(const BlockLayout& block, qsizetype index) {
  if (block.nodeId().isValid()) {
    topLevelIndex_.insert(block.nodeId(), index);
  }
  indexLayoutBlock(block);
}

void DocumentLayout::indexLayoutBlock(const BlockLayout& block) {
  if (block.nodeId().isValid()) {
    layoutIndex_.insert(block.nodeId(), &block);
  }
  for (const BlockLayout::TableRowLayout& row : block.tableRows()) {
    for (const BlockLayout::TableCellLayout& cell : row.cells) {
      if (cell.nodeId.isValid()) {
        layoutIndex_.insert(cell.nodeId, &block);
      }
    }
  }
  for (const auto& child : block.children()) {
    indexLayoutBlock(*child);
  }
}

void DocumentLayout::rebuildIndexes() {
  topLevelIndex_.clear();
  layoutIndex_.clear();
  for (qsizetype i = 0; i < static_cast<qsizetype>(blocks_.size()); ++i) {
    indexTopLevelBlock(*blocks_.at(static_cast<size_t>(i)), i);
  }
}

}  // namespace muffin
