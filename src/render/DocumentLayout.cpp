#include "render/DocumentLayout.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "render/BlockLayoutBuilder.h"
#include "theme/CssStyleDebug.h"

#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QLoggingCategory>

#include <algorithm>
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
      case BlockType::FrontMatter:
        codeFenceNs += elapsedNs;
        ++codeFenceCount;
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
      case BlockType::LinkDefinition:
      case BlockType::FootnoteDefinition:
        paragraphNs += elapsedNs;
        ++paragraphCount;
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

qreal spacingAfterBlock(const MarkdownNode& node, const RenderTheme& theme, bool fast = false) {
  // Paragraphs honour CSS margin-collapsing: the top margin is dropped in
  // spacingBeforeBlock, so the single bottom gap IS the whole inter-paragraph
  // separation (max(prev.bottom, next.top) collapses to bottom when top=0).
  // When a theme declares no paragraph margin, keep the legacy tight floor
  // (slightly more than a soft break) so such themes are unchanged.
  // `fast` (Lazy estimate path) resolves the prototype style only — skips the per-node structural
  // cascade, which is the difference between an O(n) and O(n²) rebuild on a flat block list.
  const MarkdownNode* styleNode = fast ? nullptr : &node;
  if (node.type() == BlockType::Paragraph) {
    const QMarginsF pm = theme.blockMargin(BlockType::Paragraph, 0, styleNode);
    if (pm.bottom() > 0.0) { return pm.bottom(); }
    return theme.blockSpacing() * 0.4;
  }
  const QMarginsF css = theme.blockMargin(node.type(), node.headingLevel(), styleNode);
  if (!css.isNull()) { return css.bottom(); }
  if (node.type() == BlockType::Heading) { return theme.blockSpacing() * 0.65; }
  return theme.blockSpacing();
}

qreal spacingBeforeBlock(const MarkdownNode& node, const RenderTheme& theme, qreal cursorY, bool fast = false) {
  // CSS paragraph top margins participate in adjacent margin collapse. Themes with
  // no paragraph margin keep the legacy no-before-spacing path.
  const MarkdownNode* styleNode = fast ? nullptr : &node;
  if (node.type() == BlockType::Paragraph) {
    const QMarginsF pm = theme.blockMargin(BlockType::Paragraph, 0, styleNode);
    return !pm.isNull() ? pm.top() : 0.0;
  }
  const QMarginsF css = theme.blockMargin(node.type(), node.headingLevel(), styleNode);
  if (!css.isNull()) { return css.top(); }
  if (node.type() != BlockType::Heading || cursorY <= theme.topMargin()) {
    return 0;
  }
  if (node.headingLevel() == 2) {
    return theme.blockSpacing() * 1.1;
  }
  return node.headingLevel() < 2 ? theme.blockSpacing() * 1.25 : theme.blockSpacing() * 0.7;
}

bool hasCssBlockMargin(const MarkdownNode& node, const RenderTheme& theme, bool fast = false) {
  return !theme.blockMargin(node.type(), node.headingLevel(), fast ? nullptr : &node).isNull();
}

qreal spacingBetweenBlocks(const MarkdownNode& prev, const MarkdownNode& next, const RenderTheme& theme, bool fast = false) {
  const qreal after = spacingAfterBlock(prev, theme, fast);
  const qreal before = spacingBeforeBlock(next, theme, theme.topMargin() + 1.0, fast);
  // CSS adjacent vertical margins collapse: the gap is the larger positive margin,
  // not bottom+top. Keep the legacy additive rhythm only for blocks with no CSS
  // margins at all.
  if (hasCssBlockMargin(prev, theme, fast) || hasCssBlockMargin(next, theme, fast)) {
    return qMax(after, before);
  }
  return after + before;
}

struct PageMetrics {
  qreal outerLeft = 0;
  qreal outerWidth = 0;
  qreal contentLeft = 0;
  qreal contentWidth = 0;
};

PageMetrics pageMetricsFor(const RenderTheme& theme, qreal viewportWidth) {
  const QMarginsF padding = theme.pagePadding();
  const bool cssPageBox = !padding.isNull() || theme.pageBorderRadius() > 0.0 || theme.pageBorderWidth() > 0.0;
  // The card's horizontal gap to the viewport edge is the theme's OWN #write
  // margin — this is what `margin: 0 auto` means: the column fills the window
  // up to its max-width (margin 0 → no app-imposed gutter), and a theme that
  // declares a real margin (e.g. mist-blue's 32px) gets that as its minimum
  // gap. A fixed 32px app gutter (the previous behaviour) made the column
  // narrower than intended and pushed text further from the edges than the theme
  // asked for — the "huge L/R margins" on a margin-0 theme versus the tight 0 it specified.
  const QMarginsF margin = theme.pageMargin();
  const qreal horizontalInset =
      cssPageBox ? qMax(margin.left(), margin.right())
                 : qMin<qreal>(64.0, qMax<qreal>(16.0, viewportWidth * 0.08));
  const qreal maxOuter = qMax<qreal>(320.0, viewportWidth - horizontalInset * 2.0);
  const qreal outerWidth = qMin(theme.pageWidth(), maxOuter);
  const qreal outerLeft = cssPageBox ? qMax<qreal>(0.0, (viewportWidth - outerWidth) / 2.0)
                                    : qMax<qreal>(16.0, (viewportWidth - outerWidth) / 2.0 - 12.0);
  const qreal contentLeft = outerLeft + padding.left();
  const qreal contentWidth = qMax<qreal>(120.0, outerWidth - padding.left() - padding.right());
  return {outerLeft, outerWidth, contentLeft, contentWidth};
}

// A paragraph that already serves as the "append content here" target: one
// with no inline content at all. This is checked on the inline set (via
// InlineLayout::isEmpty), not on flattened plain text — a paragraph holding
// only an image with empty alt text flattens to empty text but is real content,
// so the virtual trailing append line below it must still appear.
bool isTrailingEmptyParagraph(const BlockLayout& block) {
  if (block.type() != BlockType::Paragraph) {
    return false;
  }
  const InlineLayout* inlineLayout = block.inlineLayout();
  return inlineLayout == nullptr || inlineLayout->isEmpty();
}

// Vertical space reserved for the virtual trailing paragraph below the last
// block. Suppressed when the last block is itself an empty paragraph. When the
// last block is not yet promoted (lazy), we cannot check its inline set, so we
// return the default non-zero trailing — conservative and self-correcting once
// it scrolls into view and is measured.
qreal trailingHeightForLastBlock(const BlockLayout* lastBlock, const RenderTheme& theme) {
  if (lastBlock && isTrailingEmptyParagraph(*lastBlock)) {
    return 0.0;
  }
  return DocumentLayout::trailingSpaceForVirtualParagraph(theme);
}

}  // namespace

QRectF DocumentLayout::trailingParagraphCursorRect(const BlockLayout& lastBlock, const RenderTheme& theme, qreal pageLeft) {
  const qreal lineHeight = QFontMetricsF(theme.paragraphFont()).height();
  const qreal x = pageLeft > 0 ? pageLeft : lastBlock.rect().left();
  return QRectF(x, lastBlock.rect().bottom() + theme.blockSpacing(), 1.0, lineHeight);
}

qreal DocumentLayout::trailingSpaceForVirtualParagraph(const RenderTheme& theme) {
  return theme.blockSpacing() + QFontMetricsF(theme.paragraphFont()).height();
}

void DocumentLayout::rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, QString documentPath) {
  rebuild(document, theme, viewportWidth, SelectionRange(), std::move(documentPath));
}

void DocumentLayout::rebuild(const MarkdownDocument& document, const RenderTheme& theme, qreal viewportWidth, SelectionRange selection, QString documentPath) {
  rebuild(document, theme, viewportWidth, std::move(selection), std::move(documentPath), BuildPolicy::Eager);
}

void DocumentLayout::rebuild(
    const MarkdownDocument& document,
    const RenderTheme& theme,
    qreal viewportWidth,
    SelectionRange selection,
    QString documentPath,
    BuildPolicy policy) {
  QElapsedTimer totalTimer;
  const bool collectPerf = layoutPerf().isDebugEnabled();
  if (collectPerf) {
    totalTimer.start();
  }

  // A structural-selector theme resolves each node's style against its live
  // position; drop the cache so edited structure (a sibling added/removed) is
  // re-evaluated. Cheap when the theme has no structural rules (empty cache).
  theme.clearStructuralCache();
  theme.dropStructuralBuilder();  // full rebuild: node tree replaced → CSS element pointers dangle
  document_ = &document;
  documentPath_ = std::move(documentPath);
  viewportWidth_ = viewportWidth;
  buildPolicy_ = policy;
  selection_ = selection;

  slots_.clear();
  tops_.clear();
  topLevelIndex_.clear();
  layoutIndex_.clear();
  nestedToTopLevel_.clear();

  const PageMetrics metrics = pageMetricsFor(theme, viewportWidth);
  pageOuterLeft_ = metrics.outerLeft;
  pageOuterWidth_ = metrics.outerWidth;
  pageLeft_ = metrics.contentLeft;
  pageWidth_ = metrics.contentWidth;

  configureBuilder(selection);

  const auto& children = document.root().children();
  slots_.reserve(children.size());
  tops_.reserve(children.size());

  RebuildPerfStats perf;
  qreal estimateMs = 0.0;

  const QMarginsF pagePadding = theme.pagePadding();
  const QMarginsF pageMargin = theme.pageMargin();
  qreal cursorY = pageMargin.top() + pagePadding.top();
  if (policy == BuildPolicy::Lazy) {
    QElapsedTimer estTimer;
    if (collectPerf) {
      estTimer.start();
    }
    const MarkdownNode* previous = nullptr;
    for (const auto& child : children) {
      cursorY += previous ? spacingBetweenBlocks(*previous, *child, theme, /*fast=*/true)
                          : spacingBeforeBlock(*child, theme, cursorY, /*fast=*/true);
      const BlockLayoutBuilder::EstimateResult est = builder_.estimateHeight(*child, theme, pageWidth_);
      BlockSlot slot;
      slot.nodeId = child->id();
      slot.type = child->type();
      slot.top = cursorY;
      slot.height = est.height;
      slot.measured = false;
      cursorY += est.height;
      previous = child.get();
      tops_.push_back(slot.top);
      slots_.push_back(std::move(slot));
    }
    if (collectPerf) {
      estimateMs = nsToMs(estTimer.nsecsElapsed());
    }
  } else {
    const MarkdownNode* previous = nullptr;
    for (const auto& child : children) {
      cursorY += previous ? spacingBetweenBlocks(*previous, *child, theme)
                          : spacingBeforeBlock(*child, theme, cursorY);
      QElapsedTimer buildTimer;
      if (collectPerf) {
        buildTimer.start();
      }
      auto block = builder_.build(*child, theme, pageLeft_, cursorY, pageWidth_);
      if (collectPerf) {
        const qint64 elapsedNs = buildTimer.nsecsElapsed();
        perf.buildNs += elapsedNs;
        perf.addBlock(child->type(), elapsedNs);
      }
      BlockSlot slot;
      slot.nodeId = child->id();
      slot.type = child->type();
      slot.top = cursorY;
      slot.height = block->height();
      slot.measured = true;
      cursorY = block->rect().bottom();
      previous = child.get();
      indexLayoutBlock(*block);
      tops_.push_back(slot.top);
      slot.detail = std::move(block);
      slots_.push_back(std::move(slot));
    }
  }

  buildNestedIndex(document);
  recomputeTotalHeight(theme);

  if (renderLayoutDebugLog().isDebugEnabled() && slots_.size() >= 5) {
    const QMarginsF pm = theme.blockMargin(BlockType::Paragraph, 0);
    const QMarginsF hm = theme.blockMargin(BlockType::Heading, 1);
    qCDebug(renderLayoutDebugLog).nospace()
        << "layout.theme zoom=" << theme.zoomPercent()
        << " fontPx=" << theme.fontSizePx()
        << " blockSpacing=" << theme.blockSpacing()
        << " paraMargin(top/bot)=" << pm.top() << "/" << pm.bottom()
        << " h1Margin(top/bot)=" << hm.top() << "/" << hm.bottom();
    const int n = qMin<int>(int(slots_.size()), 20);
    for (int i = 0; i < n; ++i) {
      const BlockSlot& sl = slots_[size_t(i)];
      qCDebug(renderLayoutDebugLog).nospace()
          << "layout.slot[" << i << "] type=" << static_cast<int>(sl.type)
          << " top=" << sl.top << " h=" << sl.height
          << " bottom=" << (sl.top + sl.height);
    }
  }

  if (collectPerf) {
    if (policy == BuildPolicy::Lazy) {
      qCDebug(layoutPerf).nospace() << "layout.estimate " << estimateMs << " ms";
      qCDebug(layoutPerf).nospace() << "layout.rebuild.summary blocks=" << slots_.size() << " total=" << nsToMs(totalTimer.nsecsElapsed())
                                    << " ms build=0 ms policy=lazy totalHeight=" << totalHeight_;
    } else {
      perf.totalNs = totalTimer.nsecsElapsed();
      logRebuildPerf(perf, viewportWidth_, pageWidth_, totalHeight_);
      builder_.dumpBuildBreakdown();
    }
  }
}

void DocumentLayout::setEditingHtmlBlock(NodeId id) {
  editingHtmlBlockId_ = id;
}

void DocumentLayout::setCodeFenceScroll(CodeFenceScrollController* controller) {
  codeFenceScroll_ = controller;
}

CodeFenceScrollController* DocumentLayout::codeFenceScroll() const {
  return codeFenceScroll_;
}

bool DocumentLayout::relayoutForViewportWidth(const RenderTheme& theme, qreal viewportWidth) {
  if (!document_ || slots_.empty()) {
    return false;
  }
  const PageMetrics metrics = pageMetricsFor(theme, viewportWidth);
  if (!qFuzzyCompare(metrics.contentWidth + 1.0, pageWidth_ + 1.0)) {
    return false;
  }
  viewportWidth_ = viewportWidth;
  pageOuterLeft_ = metrics.outerLeft;
  pageOuterWidth_ = metrics.outerWidth;
  const qreal dx = metrics.contentLeft - pageLeft_;
  if (qFuzzyIsNull(dx)) {
    pageLeft_ = metrics.contentLeft;
    pageWidth_ = metrics.contentWidth;
    return true;
  }
  pageLeft_ = metrics.contentLeft;
  pageWidth_ = metrics.contentWidth;
  for (BlockSlot& slot : slots_) {
    if (slot.detail) {
      slot.detail->translate(dx, 0);
    }
  }
  return true;
}

DocumentLayout::BlockRebuildResult DocumentLayout::rebuildBlock(
    NodeId blockId,
    const MarkdownDocument& document,
    const RenderTheme& theme,
    SelectionRange selection) {
  BlockRebuildResult result;
  if (!blockId.isValid() || document_ != &document || slots_.empty() || viewportWidth_ <= 0) {
    return result;
  }

  theme.clearStructuralCache();
  const MarkdownNode* node = topLevelBlockFor(blockId, document);
  if (!node) {
    return result;
  }
  const auto& documentBlocks = document.root().children();
  if (static_cast<qsizetype>(slots_.size()) != static_cast<qsizetype>(documentBlocks.size())) {
    return result;
  }
  auto indexIt = topLevelIndex_.constFind(node->id());
  if (indexIt == topLevelIndex_.constEnd()) {
    return result;
  }
  const qsizetype index = indexIt.value();
  if (index < 0 || index >= static_cast<qsizetype>(documentBlocks.size()) ||
      documentBlocks.at(static_cast<size_t>(index))->id() != node->id()) {
    return result;
  }

  configureBuilder(selection);
  BlockSlot& slot = slots_.at(static_cast<size_t>(index));
  result.blockId = node->id();
  result.oldRect = slot.detail ? slot.detail->rect() : QRectF(pageLeft_, slot.top, pageWidth_, slot.height);

  // Drop the old subtree's layout-index entries before the detail is replaced.
  if (slot.detail) {
    removeLayoutIndexFor(*slot.detail);
  }
  auto replacement = builder_.build(*node, theme, pageLeft_, slot.top, pageWidth_);
  result.newRect = replacement->rect();

  qreal newNextTop = replacement->rect().bottom();
  if (index + 1 < static_cast<qsizetype>(documentBlocks.size())) {
    newNextTop += spacingBetweenBlocks(*node, *documentBlocks.at(static_cast<size_t>(index + 1)), theme);
  } else {
    newNextTop += spacingAfterBlock(*node, theme);
  }
  const qreal trailingHeight = trailingHeightForLastBlock(replacement.get(), theme);
  const qreal delta =
      index + 1 < static_cast<qsizetype>(slots_.size())
          ? newNextTop - slots_.at(static_cast<size_t>(index + 1)).top
          : qMax(newNextTop + theme.bottomMargin() + trailingHeight, theme.topMargin() + theme.bottomMargin()) - totalHeight_;
  result.heightDelta = delta;

  slot.height = replacement->height();
  slot.measured = true;
  slot.top = replacement->rect().top();
  tops_.at(static_cast<size_t>(index)) = slot.top;
  indexLayoutBlock(*replacement);
  slot.detail = std::move(replacement);

  // Capture suffix rects, shift, then union for the shifted dirty rect. For promoted slots
  // (all of them under Eager) this is identical to pre-virtualization behavior; for
  // un-promoted slots (Lazy runtime) the slot rect is an acceptable dirty approximation.
  QVector<QRectF> suffixOld;
  for (qsizetype i = index + 1; i < static_cast<qsizetype>(slots_.size()); ++i) {
    const BlockSlot& s = slots_.at(static_cast<size_t>(i));
    suffixOld.append(s.detail ? s.detail->rect() : QRectF(pageLeft_, s.top, pageWidth_, s.height));
  }
  shiftSuffixFrom(index + 1, delta);
  QRectF shiftedRect;
  for (qsizetype i = index + 1, k = 0; i < static_cast<qsizetype>(slots_.size()); ++i, ++k) {
    const BlockSlot& s = slots_.at(static_cast<size_t>(i));
    const QRectF newRect = s.detail ? s.detail->rect() : QRectF(pageLeft_, s.top, pageWidth_, s.height);
    shiftedRect = shiftedRect.isNull() ? suffixOld.at(k).united(newRect) : shiftedRect.united(suffixOld.at(k)).united(newRect);
  }
  result.shiftedRect = shiftedRect;

  if (index + 1 < static_cast<qsizetype>(slots_.size())) {
    totalHeight_ += delta;
  } else {
    totalHeight_ = qMax(newNextTop + theme.bottomMargin() + trailingHeight, theme.topMargin() + theme.bottomMargin());
  }

  result.rebuilt = true;
  return result;
}

DocumentLayout::RangeRebuildResult DocumentLayout::rebuildTopLevelRange(
    TopLevelRangeChange range,
    const MarkdownDocument& document,
    const RenderTheme& theme,
    SelectionRange selection) {
  RangeRebuildResult result;
  result.first = range.first;
  result.oldCount = range.oldCount;
  result.newCount = range.newCount;
  theme.clearStructuralCache();
  theme.invalidateStructuralSiblingLinks();  // splice changed the root child list → re-link sibling chains, but KEEP the per-node CssElement cache (O(n) cache hits vs dropStructuralBuilder's O(n) recreation ~1s @ 375k blocks)
  if (!range.isValid() || document_ != &document || viewportWidth_ <= 0) {
    return result;
  }

  const auto& documentBlocks = document.root().children();
  const qsizetype layoutCount = static_cast<qsizetype>(slots_.size());
  const qsizetype documentCount = static_cast<qsizetype>(documentBlocks.size());
  if (range.documentRevision != document.revision() || range.first < 0 || range.oldCount < 0 || range.newCount < 0 || range.first > layoutCount ||
      range.first > documentCount || range.first + range.oldCount > layoutCount || range.first + range.newCount > documentCount ||
      layoutCount - range.oldCount + range.newCount != documentCount) {
    return result;
  }

  for (qsizetype i = 0; i < range.first; ++i) {
    if (slots_.at(static_cast<size_t>(i)).nodeId != documentBlocks.at(static_cast<size_t>(i))->id()) {
      return result;
    }
  }
  const qsizetype oldSuffixFirst = range.first + range.oldCount;
  const qsizetype newSuffixFirst = range.first + range.newCount;
  for (qsizetype oldIndex = oldSuffixFirst, newIndex = newSuffixFirst; oldIndex < layoutCount && newIndex < documentCount; ++oldIndex, ++newIndex) {
    if (slots_.at(static_cast<size_t>(oldIndex)).nodeId != documentBlocks.at(static_cast<size_t>(newIndex))->id()) {
      return result;
    }
  }

  {
    QRectF oldRectUnion;
    for (qsizetype i = range.first; i < range.first + range.oldCount; ++i) {
      const BlockSlot& s = slots_.at(static_cast<size_t>(i));
      const QRectF r = s.detail ? s.detail->rect() : QRectF(pageLeft_, s.top, pageWidth_, s.height);
      oldRectUnion = oldRectUnion.isNull() ? r : oldRectUnion.united(r);
    }
    result.oldRect = oldRectUnion;
  }

  // Drop layout-index entries for the about-to-be-removed slice BEFORE building replacements: an
  // unchanged block at the edit boundary (e.g. a heading) keeps its NodeId, so removing by id after
  // the replacements are indexed would wipe the freshly-added entry too.
  for (qsizetype i = range.first; i < range.first + range.oldCount; ++i) {
    if (slots_.at(static_cast<size_t>(i)).detail) {
      removeLayoutIndexFor(*slots_.at(static_cast<size_t>(i)).detail);
    }
  }

  configureBuilder(selection);
  std::vector<BlockSlot> replacements;
  replacements.reserve(static_cast<size_t>(range.newCount));

  qreal cursorY = theme.pageMargin().top() + theme.pagePadding().top();
  const MarkdownNode* previousNode = nullptr;
  if (range.first > 0) {
    previousNode = documentBlocks.at(static_cast<size_t>(range.first - 1)).get();
    const BlockSlot& prev = slots_.at(static_cast<size_t>(range.first - 1));
    cursorY = prev.top + prev.height;
  }

  QRectF newRectUnion;
  QElapsedTimer buildTimer;
  const bool dbg = layoutPerf().isDebugEnabled();
  if (dbg) {
    buildTimer.start();
  }
  for (qsizetype i = 0; i < range.newCount; ++i) {
    const MarkdownNode& node = *documentBlocks.at(static_cast<size_t>(range.first + i));
    cursorY += previousNode ? spacingBetweenBlocks(*previousNode, node, theme)
                            : spacingBeforeBlock(node, theme, cursorY);
    auto block = builder_.build(node, theme, pageLeft_, cursorY, pageWidth_);
    cursorY = block->rect().bottom();
    newRectUnion = newRectUnion.isNull() ? block->rect() : newRectUnion.united(block->rect());
    BlockSlot slot;
    slot.nodeId = node.id();
    slot.type = node.type();
    slot.top = block->rect().top();
    slot.height = block->height();
    slot.measured = true;
    indexLayoutBlock(*block);
    slot.detail = std::move(block);
    previousNode = &node;
    replacements.push_back(std::move(slot));
  }
  if (dbg) {
    qCDebug(layoutPerf).nospace() << "layout.range.build n=" << range.newCount << " "
                                  << buildTimer.nsecsElapsed() / 1000000.0 << " ms";
  }
  result.newRect = newRectUnion;

  qreal newNextTop = cursorY;
  if (newSuffixFirst < documentCount) {
    newNextTop += previousNode ? spacingBetweenBlocks(*previousNode, *documentBlocks.at(static_cast<size_t>(newSuffixFirst)), theme)
                               : spacingBeforeBlock(*documentBlocks.at(static_cast<size_t>(newSuffixFirst)), theme, newNextTop);
  } else if (previousNode) {
    newNextTop += spacingAfterBlock(*previousNode, theme);
  }

  const qreal oldNextTop = oldSuffixFirst < layoutCount ? slots_.at(static_cast<size_t>(oldSuffixFirst)).top : totalHeight_;
  const BlockLayout* newLastBlock = nullptr;
  if (newSuffixFirst < documentCount) {
    // last block is in the unchanged suffix — may be un-promoted; trailing uses default then
    newLastBlock = slots_.back().detail ? slots_.back().detail.get() : nullptr;
  } else if (!replacements.empty()) {
    newLastBlock = replacements.back().detail.get();
  } else if (range.first > 0) {
    newLastBlock = slots_.at(static_cast<size_t>(range.first - 1)).detail ? slots_.at(static_cast<size_t>(range.first - 1)).detail.get() : nullptr;
  }
  const qreal trailingHeight = trailingHeightForLastBlock(newLastBlock, theme);
  const qreal newTotalHeight = qMax(newNextTop + theme.bottomMargin() + trailingHeight, theme.topMargin() + theme.bottomMargin());
  result.heightDelta = oldSuffixFirst < layoutCount ? newNextTop - oldNextTop : newTotalHeight - totalHeight_;

  // Capture the unchanged suffix's old rects (for the shifted dirty rect) before the structural edit.
  QVector<QRectF> suffixOld;
  for (qsizetype i = oldSuffixFirst; i < layoutCount; ++i) {
    const BlockSlot& s = slots_.at(static_cast<size_t>(i));
    suffixOld.append(s.detail ? s.detail->rect() : QRectF(pageLeft_, s.top, pageWidth_, s.height));
  }

  // Erase the old slice, insert the replacements.
  slots_.erase(slots_.begin() + range.first, slots_.begin() + range.first + range.oldCount);
  slots_.insert(slots_.begin() + range.first, std::make_move_iterator(replacements.begin()), std::make_move_iterator(replacements.end()));

  // ids changed -> rebuild doc-tree indexes; recompute tops over the new slot vector; shift suffix.
  if (dbg) {
    buildTimer.start();
  }
  buildNestedIndex(document);
  rebuildTops();
  if (dbg) {
    qCDebug(layoutPerf).nospace() << "layout.range.nestedIndex+tops " << buildTimer.nsecsElapsed() / 1000000.0 << " ms";
    buildTimer.start();
  }
  if (newSuffixFirst < static_cast<qsizetype>(slots_.size())) {
    shiftSuffixFrom(newSuffixFirst, result.heightDelta);
  }
  if (dbg) {
    qCDebug(layoutPerf).nospace() << "layout.range.shift from=" << newSuffixFirst << " "
                                  << buildTimer.nsecsElapsed() / 1000000.0 << " ms";
  }

  QRectF shiftedRect;
  for (qsizetype i = newSuffixFirst, k = 0; i < static_cast<qsizetype>(slots_.size()); ++i, ++k) {
    const BlockSlot& s = slots_.at(static_cast<size_t>(i));
    const QRectF r = s.detail ? s.detail->rect() : QRectF(pageLeft_, s.top, pageWidth_, s.height);
    const QRectF oldR = k < suffixOld.size() ? suffixOld.at(k) : r;
    shiftedRect = shiftedRect.isNull() ? oldR.united(r) : shiftedRect.united(oldR).united(r);
  }
  result.shiftedRect = shiftedRect;
  totalHeight_ = oldSuffixFirst < layoutCount ? totalHeight_ + result.heightDelta : newTotalHeight;

  result.rebuilt = true;
  return result;
}

qreal DocumentLayout::pageLeft() const {
  return pageLeft_;
}

qreal DocumentLayout::pageWidth() const {
  return pageWidth_;
}

qreal DocumentLayout::pageOuterLeft() const {
  return pageOuterLeft_;
}

qreal DocumentLayout::pageOuterWidth() const {
  return pageOuterWidth_;
}

QRectF DocumentLayout::pageRect(const RenderTheme& theme, qreal viewportHeight) const {
  const QMarginsF margin = theme.pageMargin();
  const qreal top = margin.top();
  const qreal minHeight = viewportHeight > 0 ? qMax<qreal>(0.0, viewportHeight - margin.top() - margin.bottom()) : 0.0;
  const qreal height = qMax(totalHeight_ - margin.top() - margin.bottom(), minHeight);
  return QRectF(pageOuterLeft_, top, pageOuterWidth_, height);
}

qreal DocumentLayout::totalHeight() const {
  return totalHeight_;
}

DocumentLayout::BuildPolicy DocumentLayout::buildPolicy() const {
  return buildPolicy_;
}

qsizetype DocumentLayout::slotCount() const {
  return static_cast<qsizetype>(slots_.size());
}

qreal DocumentLayout::slotTop(qsizetype index) const {
  return slots_.at(static_cast<size_t>(index)).top;
}

qreal DocumentLayout::slotHeight(qsizetype index) const {
  return slots_.at(static_cast<size_t>(index)).height;
}

NodeId DocumentLayout::slotNodeId(qsizetype index) const {
  return slots_.at(static_cast<size_t>(index)).nodeId;
}

qsizetype DocumentLayout::topLevelIndexFor(NodeId id) const {
  const NodeId topId = nestedToTopLevel_.value(id);
  if (!topId.isValid()) {
    return -1;
  }
  return topLevelIndex_.value(topId, -1);
}

qsizetype DocumentLayout::slotIndexAtY(qreal y) const {
  if (tops_.empty()) {
    return -1;
  }
  // Last slot whose top <= y.
  auto it = std::upper_bound(tops_.begin(), tops_.end(), y);
  qsizetype idx = static_cast<qsizetype>(it - tops_.begin()) - 1;
  if (idx < 0) {
    idx = 0;
  }
  if (idx >= static_cast<qsizetype>(slots_.size())) {
    idx = static_cast<qsizetype>(slots_.size()) - 1;
  }
  return idx;
}

QPair<qsizetype, qsizetype> DocumentLayout::slotRangeOverlappingY(qreal yTop, qreal yBottom) const {
  if (slots_.empty()) {
    return {-1, -1};
  }
  qsizetype first = slotIndexAtY(yTop);
  // Skip slots entirely above the window.
  while (first < static_cast<qsizetype>(slots_.size()) && slots_.at(static_cast<size_t>(first)).top + slots_.at(static_cast<size_t>(first)).height <= yTop) {
    ++first;
  }
  qsizetype last = slotIndexAtY(yBottom);
  if (last < first) {
    return {-1, -1};
  }
  return {first, last};
}

void DocumentLayout::ensureBuilt(qsizetype first, qsizetype last, const RenderTheme& theme) {
  if (slots_.empty()) {
    return;
  }
  first = qBound(qsizetype(0), first, static_cast<qsizetype>(slots_.size()) - 1);
  last = qBound(qsizetype(0), last, static_cast<qsizetype>(slots_.size()) - 1);
  for (qsizetype i = first; i <= last; ++i) {
    if (!slots_.at(static_cast<size_t>(i)).detail) {
      promoteSlot(i, theme);
    }
  }
}

void DocumentLayout::buildAll(const RenderTheme& theme) {
  for (qsizetype i = 0; i < static_cast<qsizetype>(slots_.size()); ++i) {
    if (!slots_.at(static_cast<size_t>(i)).detail) {
      promoteSlot(i, theme);
    }
  }
}

QVector<NodeId> DocumentLayout::promotedTopLevelIds() const {
  QVector<NodeId> ids;
  for (const BlockSlot& slot : slots_) {
    if (slot.detail) {
      ids.append(slot.nodeId);
    }
  }
  return ids;
}

QVector<const BlockLayout*> DocumentLayout::promotedBlocks() const {
  QVector<const BlockLayout*> result;
  for (const BlockSlot& slot : slots_) {
    if (slot.detail) {
      result.push_back(slot.detail.get());
    }
  }
  return result;
}

QVector<const BlockLayout*> DocumentLayout::visibleBlocks(QRectF documentViewport, const RenderTheme& theme) {
  QVector<const BlockLayout*> result;
  if (slots_.empty()) {
    return result;
  }
  const auto [first, last] = slotRangeOverlappingY(documentViewport.top(), documentViewport.bottom());
  if (first < 0) {
    return result;
  }
  ensureBuilt(first, last, theme);
  for (qsizetype i = first; i <= last; ++i) {
    const BlockSlot& slot = slots_.at(static_cast<size_t>(i));
    if (slot.detail && slot.detail->intersects(documentViewport)) {
      result.push_back(slot.detail.get());
    }
  }
  return result;
}

const BlockLayout* DocumentLayout::block(NodeId id, const RenderTheme& theme) {
  if (!id.isValid()) {
    return nullptr;
  }
  const NodeId topId = nestedToTopLevel_.value(id);
  if (!topId.isValid()) {
    return nullptr;
  }
  const auto indexIt = topLevelIndex_.constFind(topId);
  if (indexIt == topLevelIndex_.constEnd()) {
    return nullptr;
  }
  const qsizetype index = indexIt.value();
  if (!slots_.at(static_cast<size_t>(index)).detail) {
    promoteSlot(index, theme);
  }
  return layoutIndex_.value(id, nullptr);
}

const BlockLayout* DocumentLayout::blockIfPromoted(NodeId id) const {
  return layoutIndex_.value(id, nullptr);
}

const BlockLayout* DocumentLayout::block(NodeId id) const {
  return blockIfPromoted(id);
}

NodeId DocumentLayout::topLevelBlockIdFor(NodeId id) const {
  if (!id.isValid()) {
    return {};
  }
  return nestedToTopLevel_.value(id);
}

const BlockLayout* DocumentLayout::blockAt(QPointF documentPos, const RenderTheme& theme) {
  if (slots_.empty()) {
    return nullptr;
  }
  const qsizetype idx = slotIndexAtY(documentPos.y());
  if (idx < 0) {
    return nullptr;
  }
  ensureBuilt(idx, idx, theme);
  const BlockLayout* block = slots_.at(static_cast<size_t>(idx)).detail.get();
  return block && block->rect().contains(documentPos) ? block : nullptr;
}

HitTestResult DocumentLayout::hitTest(QPointF documentPos, const RenderTheme& theme) {
  if (slots_.empty()) {
    return {};
  }
  const qsizetype idx = slotIndexAtY(documentPos.y());
  // Search a small window around the hit so interactive-content padding (which extends a
  // half-line above/below a block) is covered even when the point sits in a spacing gap.
  const qsizetype first = qBound(qsizetype(0), idx - 1, static_cast<qsizetype>(slots_.size()) - 1);
  const qsizetype last = qBound(qsizetype(0), idx + 1, static_cast<qsizetype>(slots_.size()) - 1);
  ensureBuilt(first, last, theme);

  for (qsizetype i = last; i >= first; --i) {
    const BlockLayout* block = slots_.at(static_cast<size_t>(i)).detail.get();
    if (block && block->containsInteractiveContent(documentPos, theme)) {
      HitTestResult result = block->hitTest(documentPos, theme, codeFenceScroll_);
      if (result.isValid()) {
        return result;
      }
    }
  }

  // Nearest block by center-Y (computed from slot metrics, no promotion needed).
  const qsizetype nearest = qBound(qsizetype(0), idx, static_cast<qsizetype>(slots_.size()) - 1);
  qreal nearestCenter = slots_.at(static_cast<size_t>(nearest)).top + slots_.at(static_cast<size_t>(nearest)).height / 2.0;
  qsizetype nearestIdx = nearest;
  for (qsizetype i = 0; i < static_cast<qsizetype>(slots_.size()); ++i) {
    const qreal center = slots_.at(static_cast<size_t>(i)).top + slots_.at(static_cast<size_t>(i)).height / 2.0;
    if (std::abs(center - documentPos.y()) < std::abs(nearestCenter - documentPos.y())) {
      nearestCenter = center;
      nearestIdx = i;
    }
  }
  ensureBuilt(nearestIdx, nearestIdx, theme);
  const BlockLayout* nearestBlock = slots_.at(static_cast<size_t>(nearestIdx)).detail.get();
  if (!nearestBlock) {
    return {};
  }
  // The trailing BlockAfter zone (click below the document to append a paragraph) must apply ONLY
  // to the last block. A heading's CSS margin-bottom enlarges the gap after it, and the heading's
  // centre-Y is the nearest for clicks in the upper part of that gap — so without this guard, such
  // a click returned BlockAfter(heading) and typing inserted a brand-new paragraph between the
  // heading and the following block (the user's "empty paragraph I can click into and type"). For
  // any non-trailing block, fall through to snapping into the nearest block instead.
  const bool isLastSlot = nearestIdx == static_cast<qsizetype>(slots_.size()) - 1;
  if (isLastSlot && documentPos.y() > nearestBlock->rect().bottom() && !isTrailingEmptyParagraph(*nearestBlock)) {
    HitTestResult result;
    result.blockId = nearestBlock->nodeId();
    result.textNodeId = nearestBlock->nodeId();
    result.zone = HitTestResult::Zone::BlockAfter;
    result.blockRect = nearestBlock->rect();
    result.textOffset = 0;
    result.cursorRect = trailingParagraphCursorRect(*nearestBlock, theme, pageLeft_);
    return result;
  }
  return nearestBlock->hitTest(QPointF(qBound(nearestBlock->rect().left(), documentPos.x(), nearestBlock->rect().right()), nearestBlock->rect().center().y()), theme, codeFenceScroll_);
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

void DocumentLayout::removeLayoutIndexFor(const BlockLayout& block) {
  if (block.nodeId().isValid()) {
    layoutIndex_.remove(block.nodeId());
  }
  for (const BlockLayout::TableRowLayout& row : block.tableRows()) {
    for (const BlockLayout::TableCellLayout& cell : row.cells) {
      if (cell.nodeId.isValid()) {
        layoutIndex_.remove(cell.nodeId);
      }
    }
  }
  for (const auto& child : block.children()) {
    removeLayoutIndexFor(*child);
  }
}

void DocumentLayout::buildNestedIndex(const MarkdownDocument& document) {
  topLevelIndex_.clear();
  nestedToTopLevel_.clear();
  const auto& children = document.root().children();
  for (qsizetype i = 0; i < static_cast<qsizetype>(children.size()); ++i) {
    topLevelIndex_.insert(children.at(static_cast<size_t>(i))->id(), i);
    collectNestedToTopLevel(*children.at(static_cast<size_t>(i)), children.at(static_cast<size_t>(i))->id());
  }
}

void DocumentLayout::collectNestedToTopLevel(const MarkdownNode& node, NodeId topLevelId) {
  nestedToTopLevel_.insert(node.id(), topLevelId);
  for (const auto& child : node.children()) {
    collectNestedToTopLevel(*child, topLevelId);
  }
}

void DocumentLayout::rebuildTops() {
  tops_.clear();
  tops_.reserve(slots_.size());
  for (const BlockSlot& slot : slots_) {
    tops_.push_back(slot.top);
  }
}

void DocumentLayout::configureBuilder(SelectionRange selection) {
  selection_ = selection;
  if (document_) {
    builder_.setMarkdownText(document_->pieceText(), document_->lineOffsets());
  }
  builder_.refreshRenderSettings();  // read markdown/* settings once per pass (avoids per-block QSettings hits)
  builder_.setSelection(selection);
  builder_.setEditingHtmlBlock(editingHtmlBlockId_);
  builder_.setDocumentPath(documentPath_);
  builder_.setCodeFenceScroll(codeFenceScroll_);
}

qreal DocumentLayout::promoteSlot(qsizetype index, const RenderTheme& theme) {
  if (!document_ || index < 0 || index >= static_cast<qsizetype>(slots_.size())) {
    return 0.0;
  }
  BlockSlot& slot = slots_.at(static_cast<size_t>(index));
  if (slot.detail) {
    return 0.0;  // already promoted
  }
  const auto& children = document_->root().children();
  const MarkdownNode& node = *children.at(static_cast<size_t>(index));
  auto built = builder_.build(node, theme, pageLeft_, slot.top, pageWidth_);
  const qreal delta = built->height() - slot.height;
  slot.height = built->height();
  slot.measured = true;
  slot.top = built->rect().top();  // unchanged; keep invariant exact
  tops_.at(static_cast<size_t>(index)) = slot.top;
  indexLayoutBlock(*built);
  slot.detail = std::move(built);
  shiftSuffixFrom(index + 1, delta);
  recomputeTotalHeight(theme);
  return delta;
}

void DocumentLayout::shiftSuffixFrom(qsizetype index, qreal delta) {
  if (qFuzzyIsNull(delta)) {
    return;
  }
  for (qsizetype i = index; i < static_cast<qsizetype>(slots_.size()); ++i) {
    BlockSlot& slot = slots_.at(static_cast<size_t>(i));
    slot.top += delta;
    tops_.at(static_cast<size_t>(i)) = slot.top;
    if (slot.detail) {
      slot.detail->translateY(delta);
    }
  }
}

void DocumentLayout::recomputeTotalHeight(const RenderTheme& theme) {
  const QMarginsF pageMargin = theme.pageMargin();
  const QMarginsF pagePadding = theme.pagePadding();
  qreal cursorY = pageMargin.top() + pagePadding.top();
  qreal trailingHeight = trailingHeightForLastBlock(nullptr, theme);
  if (!slots_.empty()) {
    const BlockSlot& last = slots_.back();
    const int level = last.detail ? last.detail->headingLevel() : 0;
    const QMarginsF css = theme.blockMargin(last.type, level);
    const qreal spacingAfter = !css.isNull() ? css.bottom()
                                             : (last.type == BlockType::Heading ? theme.blockSpacing() * 0.65 : theme.blockSpacing());
    cursorY = last.top + last.height + spacingAfter;
    trailingHeight = trailingHeightForLastBlock(last.detail ? last.detail.get() : nullptr, theme);
  }
  totalHeight_ = qMax(cursorY + pagePadding.bottom() + pageMargin.bottom() + trailingHeight,
                      pageMargin.top() + pagePadding.top() + pagePadding.bottom() + pageMargin.bottom());
}

}  // namespace muffin
