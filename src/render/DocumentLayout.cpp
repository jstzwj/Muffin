#include "render/DocumentLayout.h"

#include "render/BlockLayoutBuilder.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

#include <cmath>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(layoutPerf, "muffin.perf", QtWarningMsg)

struct RebuildPerfStats {
  qint64 totalNs = 0;
  qint64 buildNs = 0;
  qint64 indexNs = 0;
  qint64 paragraphNs = 0;
  qint64 headingNs = 0;
  qint64 listNs = 0;
  qint64 blockQuoteNs = 0;
  qint64 codeFenceNs = 0;
  qint64 htmlNs = 0;
  qint64 mathNs = 0;
  qint64 tableNs = 0;
  qint64 otherNs = 0;
  qsizetype paragraphCount = 0;
  qsizetype headingCount = 0;
  qsizetype listCount = 0;
  qsizetype blockQuoteCount = 0;
  qsizetype codeFenceCount = 0;
  qsizetype htmlCount = 0;
  qsizetype mathCount = 0;
  qsizetype tableCount = 0;
  qsizetype otherCount = 0;

  void addBlock(BlockType type, qint64 elapsedNs) {
    switch (type) {
      case BlockType::Paragraph:
        paragraphNs += elapsedNs;
        ++paragraphCount;
        break;
      case BlockType::Heading:
        headingNs += elapsedNs;
        ++headingCount;
        break;
      case BlockType::List:
        listNs += elapsedNs;
        ++listCount;
        break;
      case BlockType::BlockQuote:
        blockQuoteNs += elapsedNs;
        ++blockQuoteCount;
        break;
      case BlockType::CodeFence:
        codeFenceNs += elapsedNs;
        ++codeFenceCount;
        break;
      case BlockType::HtmlBlock:
        htmlNs += elapsedNs;
        ++htmlCount;
        break;
      case BlockType::MathBlock:
        mathNs += elapsedNs;
        ++mathCount;
        break;
      case BlockType::Table:
        tableNs += elapsedNs;
        ++tableCount;
        break;
      default:
        otherNs += elapsedNs;
        ++otherCount;
        break;
    }
  }
};

qreal nsToMs(qint64 ns) {
  return ns / 1000000.0;
}

void logTypeTiming(const char* label, qsizetype count, qint64 elapsedNs) {
  if (count <= 0) {
    return;
  }
  qCDebug(layoutPerf).nospace() << "layout.rebuild." << label << " count=" << count << " total=" << nsToMs(elapsedNs)
                                << " ms avg=" << nsToMs(elapsedNs) / count << " ms";
}

void logRebuildPerf(const RebuildPerfStats& stats, qreal viewportWidth, qreal pageWidth, qreal totalHeight) {
  if (!layoutPerf().isDebugEnabled()) {
    return;
  }
  const qsizetype blockCount = stats.paragraphCount + stats.headingCount + stats.listCount + stats.blockQuoteCount + stats.codeFenceCount +
                               stats.htmlCount + stats.mathCount + stats.tableCount + stats.otherCount;
  qCDebug(layoutPerf).nospace() << "layout.rebuild.summary blocks=" << blockCount << " total=" << nsToMs(stats.totalNs) << " ms build="
                                << nsToMs(stats.buildNs) << " ms index=" << nsToMs(stats.indexNs) << " ms viewportWidth=" << viewportWidth
                                << " pageWidth=" << pageWidth << " totalHeight=" << totalHeight;
  logTypeTiming("paragraph", stats.paragraphCount, stats.paragraphNs);
  logTypeTiming("heading", stats.headingCount, stats.headingNs);
  logTypeTiming("list", stats.listCount, stats.listNs);
  logTypeTiming("blockquote", stats.blockQuoteCount, stats.blockQuoteNs);
  logTypeTiming("codeFence", stats.codeFenceCount, stats.codeFenceNs);
  logTypeTiming("html", stats.htmlCount, stats.htmlNs);
  logTypeTiming("math", stats.mathCount, stats.mathNs);
  logTypeTiming("table", stats.tableCount, stats.tableNs);
  logTypeTiming("other", stats.otherCount, stats.otherNs);
}

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

struct PageMetrics {
  qreal left = 0;
  qreal width = 0;
};

PageMetrics pageMetricsFor(const RenderTheme& theme, qreal viewportWidth) {
  const qreal horizontalInset = qMin<qreal>(64.0, qMax<qreal>(16.0, viewportWidth * 0.08));
  const qreal width = qMin(theme.pageWidth(), qMax<qreal>(320.0, viewportWidth - horizontalInset * 2.0));
  return {qMax<qreal>(16.0, (viewportWidth - width) / 2.0 - 12.0), width};
}

}  // namespace

void DocumentLayout::rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth) {
  rebuild(document, theme, viewportWidth, SelectionRange());
}

void DocumentLayout::rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, SelectionRange selection) {
  QElapsedTimer totalTimer;
  const bool collectPerf = layoutPerf().isDebugEnabled();
  RebuildPerfStats perf;
  if (collectPerf) {
    totalTimer.start();
  }

  document_ = &document;
  viewportWidth_ = viewportWidth;
  blocks_.clear();
  topLevelIndex_.clear();
  layoutIndex_.clear();

  const PageMetrics metrics = pageMetricsFor(theme, viewportWidth);
  pageWidth_ = metrics.width;
  pageLeft_ = metrics.left;

  BlockLayoutBuilder builder;
  builder.setMarkdownText(document.markdownText(), document.lineOffsets());
  builder.setSelection(selection);
  qreal cursorY = theme.topMargin();
  for (const auto& child : document.root().children()) {
    cursorY += spacingBeforeBlock(*child, theme, cursorY);
    QElapsedTimer buildTimer;
    if (collectPerf) {
      buildTimer.start();
    }
    auto block = builder.build(*child, theme, pageLeft_, cursorY, pageWidth_);
    if (collectPerf) {
      const qint64 elapsedNs = buildTimer.nsecsElapsed();
      perf.buildNs += elapsedNs;
      perf.addBlock(child->type(), elapsedNs);
    }
    cursorY = block->rect().bottom() + spacingAfterBlock(*child, theme);
    QElapsedTimer indexTimer;
    if (collectPerf) {
      indexTimer.start();
    }
    indexTopLevelBlock(*block, static_cast<qsizetype>(blocks_.size()));
    if (collectPerf) {
      perf.indexNs += indexTimer.nsecsElapsed();
    }
    blocks_.push_back(std::move(block));
  }

  totalHeight_ = qMax(cursorY + theme.bottomMargin(), theme.topMargin() + theme.bottomMargin());
  if (collectPerf) {
    perf.totalNs = totalTimer.nsecsElapsed();
    logRebuildPerf(perf, viewportWidth_, pageWidth_, totalHeight_);
  }
}

bool DocumentLayout::relayoutForViewportWidth(const RenderTheme& theme, qreal viewportWidth) {
  if (!document_ || blocks_.empty()) {
    return false;
  }
  const PageMetrics metrics = pageMetricsFor(theme, viewportWidth);
  if (!qFuzzyCompare(metrics.width + 1.0, pageWidth_ + 1.0)) {
    return false;
  }
  viewportWidth_ = viewportWidth;
  const qreal dx = metrics.left - pageLeft_;
  if (qFuzzyIsNull(dx)) {
    pageLeft_ = metrics.left;
    return true;
  }
  pageLeft_ = metrics.left;
  for (auto& block : blocks_) {
    block->translate(dx, 0);
  }
  return true;
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
  const auto& documentBlocks = document.root().children();
  if (static_cast<qsizetype>(blocks_.size()) != static_cast<qsizetype>(documentBlocks.size())) {
    return result;
  }
  auto indexIt = topLevelIndex_.constFind(node->id());
  if (indexIt == topLevelIndex_.constEnd()) {
    return result;
  }
  const qsizetype index = indexIt.value();
  if (index < 0 || index >= static_cast<qsizetype>(documentBlocks.size()) || documentBlocks.at(static_cast<size_t>(index))->id() != node->id()) {
    return result;
  }

  BlockLayoutBuilder builder;
  builder.setMarkdownText(document.markdownText(), document.lineOffsets());
  builder.setSelection(selection);
  std::unique_ptr<BlockLayout>& slot = blocks_.at(static_cast<size_t>(index));
  result.blockId = node->id();
  result.oldRect = slot->rect();
  auto replacement = builder.build(*node, theme, slot->rect().left(), slot->rect().top(), pageWidth_);
  result.newRect = replacement->rect();

  qreal newNextTop = replacement->rect().bottom() + spacingAfterBlock(*node, theme);
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
