#include "render/BlockHeightEstimator.h"
#include "theme/MarkdownFlow.h"
#include "diagnostics/ScopedPerfProbe.h"

#include "render/BlockLayout.h"
#include "render/BlockLayoutRules.h"
#include "render/InlineFormatting.h"
#include "render/RenderMetrics.h"
#include "render/TextLayout.h"

#include <algorithm>
#include <cmath>

namespace muffin {
using namespace layout_rules;

void BlockHeightEstimator::beginPass(quint64 fontGeneration, bool codeBlockWrap, bool showLineNumbers) {
  if (fontGeneration != fontGeneration_) {
    fontMetricsCache_.clear();
    fontGeneration_ = fontGeneration;
  }
  codeBlockWrap_ = codeBlockWrap;
  showLineNumbers_ = showLineNumbers;
  lineHeightCache_.clear();
  avgCharWidthCache_.clear();
  paragraphEstimateCache_.clear();
}

namespace {
Q_LOGGING_CATEGORY(estimateDetailPerf, "muffin.perf.detail", QtWarningMsg)

// Estimate wrapped line count from plain text and an average characters-per-line capacity,
// respecting explicit newlines (a trailing '\n' yields one extra line, matching TextLayout).
qreal estimateWrappedLines(QStringView text, qreal charsPerLine) {
  if (text.isEmpty()) {
    return 1.0;
  }
  const qreal cpl = std::max(charsPerLine, qreal(1.0));
  qreal lines = 0;
  qsizetype segStart = 0;
  while (true) {
    const qsizetype nl = text.indexOf(QLatin1Char('\n'), segStart);
    const qsizetype segEnd = nl < 0 ? text.length() : nl;
    lines += std::max(qreal(1.0), std::ceil(static_cast<qreal>(segEnd - segStart) / cpl));
    if (nl < 0) {
      break;
    }
    segStart = nl + 1;
    if (segStart >= text.length()) {
      lines += 1.0; // trailing newline -> extra empty line
      break;
    }
  }
  return std::max(lines, qreal(1.0));
}

// O(1) wrapped-line estimate from a plain character count and an average characters-per-line
// capacity — no text scan. Paragraph source rarely embeds hard newlines (SoftBreak inlines split
// at parse time), so a pure char-budget count is a close approximation; the visible-window build
// still resolves exact heights on promotion. Used by the estimate path to avoid materializing
// inline text (plainTextForInlines + per-char walks), which was ~20-30s of the open estimate on a
// 250k-block / 2.1M-inline doc.
qreal estimateWrappedLinesFromCharCount(qsizetype charCount, qreal charsPerLine) {
  const qreal cpl = std::max(charsPerLine, qreal(1.0));
  return std::max(qreal(1.0), std::ceil(static_cast<qreal>(std::max<qsizetype>(charCount, 0)) / cpl));
}

} // namespace

qreal BlockHeightEstimator::estimateLineHeight(const QFont& font) const {
  // Matches InlineLayout's fallback per-line height: ceil(TextFontMetrics::height() * 1.16).
  return std::ceil(TextFontMetrics(font).height() * kLineHeightFactor);
}

static qreal estimateLineHeightForElement(const RenderTheme& theme, const QString& elementKey) {
  const QFont font = theme.textFontForElement(elementKey);
  const qreal multiplier = theme.lineHeightMultiplierForElement(elementKey);
  if (multiplier > 0.0) {
    return cssLineHeightPx(font.pointSizeF(), multiplier);
  }
  return std::ceil(TextFontMetrics(font).height() * kLineHeightFactor);
}

qreal BlockHeightEstimator::avgCharWidthForText(QStringView text, const QFont& font) const {
  const QString key = font.key();
  auto it = fontMetricsCache_.constFind(key);
  qreal wideAdvance;
  qreal narrowAdvance;
  if (it != fontMetricsCache_.constEnd()) {
    wideAdvance = it.value().first;
    narrowAdvance = it.value().second;
  } else {
    diag::ScopedPerfProbe perf("estimate.fontAdvance", estimateDetailPerf());
    const TextFontMetrics metrics(font);
    // One representative wide (fullwidth/CJK) glyph and an ASCII average; cached per font so the
    // estimate pass never measures full block text.
    wideAdvance = metrics.horizontalAdvance(QChar(0x5B57)); // '字' (CJK ideograph)
    narrowAdvance =
        metrics.horizontalAdvance(QStringLiteral("abcdefghijklmnopqrstuvwxyz0123456789 ")) / static_cast<qreal>(37);
    fontMetricsCache_.insert(key, {wideAdvance, narrowAdvance});
  }
  if (text.isEmpty()) {
    return wideAdvance; // placeholder; estimateWrappedLines treats empty as one line anyway
  }
  // Cheap per-char classification: East-Asian wide scripts (>= Hangul Jamo) advance ~1em, the rest
  // ~half-em. Tracks CJK vs ASCII density per block without measuring the whole run.
  qreal total = 0;
  qsizetype count = 0;
  for (const QChar c : text) {
    total += (c.unicode() >= 0x1100) ? wideAdvance : narrowAdvance;
    ++count;
  }
  return count > 0 ? total / static_cast<qreal>(count) : wideAdvance;
}

// O(1) variant of avgCharWidthForText: returns the cached narrow advance only, with no per-char
// classification walk. The estimate path assumes narrow-char density (exact for ASCII; for CJK it
// over-estimates chars-per-line, under-estimating wrapped lines). Since the estimate is only a
// scrollbar placeholder — mustMeasure blocks and viewport promotion resolve exact heights — the CJK
// imprecision is acceptable and lets the lazy estimate loop skip inline-text materialization.
qreal BlockHeightEstimator::avgCharWidthForFont(const QFont& font) const {
  const QString key = font.key();
  auto it = fontMetricsCache_.constFind(key);
  if (it != fontMetricsCache_.constEnd()) {
    return it.value().second; // narrowAdvance
  }
  // Fill the shared per-font cache via the classification variant (empty text takes its
  // cache-miss branch) instead of repeating the wide/narrow measurement here.
  avgCharWidthForText(QStringView(), font);
  const auto cached = fontMetricsCache_.constFind(key);
  return cached != fontMetricsCache_.constEnd() ? cached.value().second
                                                : TextFontMetrics(font).horizontalAdvance(QLatin1Char('n'));
}

qreal BlockHeightEstimator::cachedEstimateLineHeight(const RenderTheme& theme, const QString& elementKey,
                                                     int headingLevel) const {
  const QString cacheKey = elementKey + QLatin1Char('|') + QString::number(headingLevel);
  auto it = lineHeightCache_.constFind(cacheKey);
  if (it != lineHeightCache_.constEnd()) {
    return it.value();
  }
  diag::ScopedPerfProbe perf("estimate.lineHeight", estimateDetailPerf());
  const qreal h = estimateLineHeightForElement(theme, elementKey);
  lineHeightCache_.insert(cacheKey, h);
  return h;
}

qreal BlockHeightEstimator::cachedAvgCharWidthForElement(const RenderTheme& theme, const QString& elementKey,
                                                         bool isHeading, int headingLevel) const {
  const QString cacheKey = elementKey + QLatin1Char('|') + QString::number(headingLevel);
  auto it = avgCharWidthCache_.constFind(cacheKey);
  if (it != avgCharWidthCache_.constEnd()) {
    return it.value();
  }
  const QFont font = isHeading ? theme.headingFont(headingLevel) : theme.textFontForElement(elementKey, nullptr);
  const qreal w = avgCharWidthForFont(font);
  avgCharWidthCache_.insert(cacheKey, w);
  return w;
}

BlockHeightEstimator::EstimateResult
BlockHeightEstimator::estimateHeight(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const {
  const auto flow = theme.markdownFlow(node);
  if (flow->container(node))
    return estimateContainer(node, theme, width, depth);
  if (flow->closingBoundary(node))
    return {0, false};
  switch (node.type()) {
  case BlockType::Paragraph:
  case BlockType::Heading:
    return estimateParagraphLike(node, theme, width);
  case BlockType::BlockQuote:
  case BlockType::List:
    return estimateContainer(node, theme, width, depth);
  case BlockType::ListItem:
    return estimateListItem(node, theme, width, depth);
  case BlockType::FrontMatter:
  case BlockType::CodeFence:
  case BlockType::HtmlBlock:
  case BlockType::MathBlock:
    return estimateLiteralBlock(node, theme, width);
  case BlockType::Table:
    return estimateTable(node, theme, width);
  case BlockType::ThematicBreak:
    return {theme.blockSpacing() * 2.0, false};
  case BlockType::LinkDefinition:
  case BlockType::FootnoteDefinition:
    return estimateDefinition(node, theme, width);
  case BlockType::Document:
  default:
    return estimateContainer(node, theme, width, depth);
  }
}

BlockHeightEstimator::EstimateResult
BlockHeightEstimator::estimateParagraphLike(const MarkdownNode& node, const RenderTheme& theme, qreal width) const {
  const bool isHeading = node.type() == BlockType::Heading;
  const QString elementKey = isHeading
                                 ? QStringLiteral("h%1").arg(node.headingLevel())
                                 : (isInsideBlockquote(node) ? QStringLiteral("blockquote p") : QStringLiteral("p"));
  // Estimate resolves the load-time PROTOTYPE style only (nullptr node → elementStyle fast path),
  // skipping the per-node structural cascade. github's structural selectors match only lists/tables,
  // so paragraph estimates are identical to the structural result; the visible-window build
  // (promoteSlot → buildParagraphLike) still resolves structural style for exact heights.
  auto& widths = paragraphEstimateCache_[elementKey];
  auto found = widths.constFind(width);
  if (found == widths.cend()) {
    ParagraphEstimate estimate;
    estimate.box = theme.elementBoxStyle(elementKey, nullptr, width);
    estimate.lineHeight = cachedEstimateLineHeight(theme, elementKey, node.headingLevel());
    const auto& box = estimate.box;
    const qreal borderWidth =
        LayoutBox::borderWidth(box, qMax<qreal>(1, width - box.margin.left() - box.margin.right()));
    const auto inset = LayoutBox::insets(box);
    const qreal average = cachedAvgCharWidthForElement(theme, elementKey, isHeading, node.headingLevel());
    estimate.charsPerLine =
        qMax<qreal>(1, std::floor(qMax<qreal>(1, borderWidth - inset.left() - inset.right()) / average));
    widths.insert(width, std::move(estimate));
    found = widths.constFind(width);
  }
  const auto& estimate = found.value();
  // O(1) estimate: derive the wrapped-line count from the block's source char count (sourceRange is
  // UTF-16 code units ≈ visible chars) + a cached per-font narrow advance, WITHOUT materializing
  // the inline text. plainTextForInlines + per-char walks were ~20-30s of open on a 2.1M-inline doc.
  // sourceRange().byteLength() over-counts inline markup (`**`, `[](url)`) and markers, but the
  // estimate is only a scrollbar placeholder (mustMeasure + viewport promotion resolve exact
  // heights), so the imprecision is harmless.
  const qsizetype charCount = node.sourceRange().byteLength();
  const qreal height = LayoutBox::borderHeight(
      estimate.box, estimateWrappedLinesFromCharCount(charCount, estimate.charsPerLine) * estimate.lineHeight);
  // mustMeasure dropped: DocumentLayout never reads EstimateResult.mustMeasure (promotion is purely
  // viewport-visibility-driven), so the inlinesContainSizedContent walk was pure waste on the
  // estimate path (~2s of the 250k-block open estimate).
  return {height, false};
}

BlockHeightEstimator::EstimateResult BlockHeightEstimator::estimateContainer(const MarkdownNode& node,
                                                                             const RenderTheme& theme, qreal width,
                                                                             int depth) const {
  if (depth > 64) {
    return {estimateLineHeight(theme.paragraphFont()), true};
  }
  const auto* container = theme.markdownFlow(node)->container(node);
  if (node.children().empty() && !container) {
    return {TextFontMetrics(theme.paragraphFont()).height(), false};
  }
  const bool isQuote = node.type() == BlockType::BlockQuote;
  const bool quoteBox = isQuote || container;
  const QString key = container ? container->attributes.tag : QStringLiteral("blockquote");
  const ThemeElementBoxStyle qbox = quoteBox ? theme.elementBoxStyle(key, &node, width) : ThemeElementBoxStyle{};
  const QMarginsF qpad = quoteBox ? qbox.padding : QMarginsF();
  const QMarginsF qborder =
      quoteBox ? QMarginsF(qbox.borderLeftWidth, qbox.borderTopWidth, qbox.borderRightWidth, qbox.borderBottomWidth)
               : QMarginsF();
  const qreal quoteIndent = (isQuote && !quoteBox) ? theme.blockQuoteIndent() : 0.0;
  const qreal childWidth =
      std::max<qreal>(1.0, width - quoteIndent - qborder.left() - qborder.right() - qpad.left() - qpad.right());
  const bool firstChildMarginCollapses = qFuzzyIsNull(qborder.top() + qpad.top());
  const bool lastChildMarginCollapses = qFuzzyIsNull(qborder.bottom() + qpad.bottom());
  qreal total = qborder.top() + qpad.top();
  bool mustMeasure = false;
  const MarkdownNode* previousChild = nullptr;
  bool omittedOnlyRenderChildren = !node.children().empty();
  for (const auto* child : theme.markdownFlow(node)->children(node)) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_)) {
      continue;
    }
    omittedOnlyRenderChildren = false;
    if (previousChild) {
      total += spacingBetweenInFlow(*previousChild, *child, theme, /*fast=*/true, childWidth);
    } else if (!firstChildMarginCollapses) {
      total += spacingBeforeInFlow(*child, theme, /*fast=*/true, childWidth);
    }
    const EstimateResult r = estimateHeight(*child, theme, childWidth, depth + 1);
    total += r.height;
    mustMeasure = mustMeasure || r.mustMeasure;
    previousChild = child;
  }
  if (previousChild && !lastChildMarginCollapses) {
    total += spacingAfterInFlow(*previousChild, theme, /*fast=*/true, childWidth);
  }
  total += qpad.bottom() + qborder.bottom();
  if (!previousChild && !(quoteBox && omittedOnlyRenderChildren)) {
    total += TextFontMetrics(theme.paragraphFont()).height();
  }
  return {total, mustMeasure};
}

BlockHeightEstimator::EstimateResult BlockHeightEstimator::estimateListItem(const MarkdownNode& node,
                                                                            const RenderTheme& theme, qreal width,
                                                                            int depth) const {
  const MarkdownNode* listParent = node.parent();
  qreal contentIndent = theme.listIndent();
  const bool ordered = listParent && listParent->listKind() == ListKind::Ordered;
  if (ordered && listParent) {
    // O(1) marker width: an ordered marker is at most "<itemCount>.", so its digit count bounds the
    // width. The old loop measured EVERY sibling's marker here (O(N) per item, and estimateContainer
    // calls this once per item → O(N²) per list — ~5s on the dense-file estimate).
    const TextFontMetrics metrics(theme.paragraphFont());
    const qsizetype itemCount = listParent->children().size();
    const int digits = itemCount <= 0 ? 1 : static_cast<int>(std::log10(static_cast<qreal>(itemCount))) + 1;
    const qreal widestMarker =
        metrics.horizontalAdvance(QLatin1Char('0')) * digits + metrics.horizontalAdvance(QStringLiteral("."));
    contentIndent = std::max(theme.listIndent(), widestMarker + theme.listMarkerGap());
  }
  const qreal contentWidth = std::max<qreal>(1.0, width - contentIndent);

  const QVector<InlineNode> primary = primaryInlinesForListItem(node);
  const QString elementKey = isInsideBlockquote(node) ? QStringLiteral("blockquote p") : QStringLiteral("li");
  // Estimate uses the prototype style only (see estimateParagraphLike) — no per-node structural cascade.
  const qreal lineHeight = cachedEstimateLineHeight(theme, elementKey, 0);
  qreal inlineHeight = lineHeight;
  bool mustMeasure = false;
  if (!primary.isEmpty()) {
    // O(1) estimate from the item's source char count (see estimateParagraphLike). byteLength
    // includes the list marker and any nested-block source, so it over-counts, but the estimate is
    // only a scrollbar placeholder (promotion resolves the exact height).
    const qsizetype charCount = node.sourceRange().byteLength();
    const qreal charsPerLine =
        std::max(qreal(1.0), std::floor(contentWidth / cachedAvgCharWidthForElement(theme, elementKey, false, 0)));
    inlineHeight = estimateWrappedLinesFromCharCount(charCount, charsPerLine) * lineHeight;
    // mustMeasure dropped (see estimateParagraphLike) — unconsumed by DocumentLayout.
  }
  qreal height = inlineHeight;

  bool skippedPrimary = false;
  const MarkdownNode* previousChild = nullptr;
  for (const auto& child : node.children()) {
    if (omitVirtualEmptyParagraphInRenderFlow(*child, selection_)) {
      continue;
    }
    if (!skippedPrimary && child->type() == BlockType::Paragraph) {
      skippedPrimary = true;
      previousChild = child.get();
      continue;
    }
    const EstimateResult r = estimateHeight(*child, theme, contentWidth, depth + 1);
    height += (previousChild ? spacingBetweenInFlow(*previousChild, *child, theme, /*fast=*/true, contentWidth)
                             : theme.blockSpacing()) +
              r.height;
    mustMeasure = mustMeasure || r.mustMeasure;
    previousChild = child.get();
  }
  return {height, mustMeasure};
}

BlockHeightEstimator::EstimateResult
BlockHeightEstimator::estimateLiteralBlock(const MarkdownNode& node, const RenderTheme& theme, qreal width) const {
  const QString literal = displayLiteralFor(node);
  const bool isMath = node.type() == BlockType::MathBlock;
  const QFont font = isMath ? theme.mathFont() : theme.codeFont();
  const qreal lineHeight =
      isMath ? std::max<qreal>(14.0, TextFontMetrics(theme.mathFont()).height()) : theme.codeLineHeight();
  const QMarginsF padding = LayoutBox::insets(theme.elementBoxStyle(QStringLiteral("pre"), nullptr, width));
  const qreal lineNumberGutter = (node.type() == BlockType::CodeFence && showLineNumbers_)
                                     ? codeLineNumberGutterWidth(literal, theme.codeFont())
                                     : 0.0;
  const qreal innerWidth = std::max<qreal>(1.0, width - padding.left() - padding.right() - lineNumberGutter);
  const qreal avgCharWidth = avgCharWidthForFont(font);
  qreal lines;
  qreal reservedStrip = 0.0;
  if (node.type() == BlockType::CodeFence && !codeBlockWrap_) {
    // NoWrap: one visual line per physical line; reserve the scrollbar strip if the widest line
    // (estimated) overflows, matching the build path so the scrollbar range doesn't jump on promotion.
    lines = literal.isEmpty() ? 1.0 : qreal(literal.count(QLatin1Char('\n'))) + 1.0;
    int maxLineChars = 1;
    qsizetype start = 0;
    while (start <= literal.size()) {
      const qsizetype nl = literal.indexOf(QLatin1Char('\n'), start);
      const qsizetype end = nl < 0 ? literal.size() : nl;
      maxLineChars = std::max(maxLineChars, int(end - start));
      if (nl < 0) {
        break;
      }
      start = nl + 1;
    }
    if (avgCharWidth * maxLineChars > innerWidth + 0.5) {
      reservedStrip = BlockLayout::scrollBarStripHeight(theme);
    }
  } else {
    const qreal charsPerLine = std::max(qreal(1.0), std::floor(innerWidth / avgCharWidth));
    lines = estimateWrappedLines(QStringView(literal), charsPerLine);
  }
  const qreal height = std::ceil(lines * lineHeight + padding.top() + padding.bottom() + 2.0 + reservedStrip);
  // Math/HTML rendered size is not derivable from metrics; tree-sitter only colors code.
  const bool mustMeasure = isMath || node.type() == BlockType::HtmlBlock;
  return {height, mustMeasure};
}

BlockHeightEstimator::EstimateResult BlockHeightEstimator::estimateTable(const MarkdownNode& node,
                                                                         const RenderTheme& theme, qreal width) const {
  const int rowCount = static_cast<int>(node.children().size());
  int columnCount = 0;
  for (const auto& row : node.children()) {
    columnCount = std::max(columnCount, static_cast<int>(row->children().size()));
  }
  if (rowCount == 0 || columnCount == 0) {
    return {TextFontMetrics(theme.paragraphFont()).height(), false};
  }
  // O(1) per cell: equal-split column widths + each cell's source char count, instead of
  // tableColumnWidths (which materializes every cell's inline text) and per-cell plainTextForInlines.
  // The build path (buildTable) still resolves exact column widths via tableColumnWidths; this
  // estimate only sizes the scrollbar (mustMeasure/promotion resolve exact heights).
  const qreal columnWidth = width / columnCount;
  qreal total = 0;
  for (const auto& row : node.children()) {
    const QString key = row->tableRowIsHeader() ? QStringLiteral("th") : QStringLiteral("td");
    const auto used = theme.elementBoxStyle(key, nullptr, columnWidth);
    const auto padding = LayoutBox::insets(used);
    const auto font = theme.textFontForElement(key);
    const qreal lineHeight = cachedEstimateLineHeight(theme, key, 0);
    const qreal charsPerLine = qMax<qreal>(
        1, std::floor(qMax<qreal>(1, columnWidth - padding.left() - padding.right()) / avgCharWidthForFont(font)));
    qreal rowHeight = LayoutBox::borderHeight(used, lineHeight);
    for (const auto& cell : row->children()) {
      const qreal lines = estimateWrappedLinesFromCharCount(cell->sourceRange().byteLength(), charsPerLine);
      rowHeight = qMax(rowHeight, LayoutBox::borderHeight(used, lines * lineHeight));
    }
    total += rowHeight;
  }
  return {total, false}; // mustMeasure dropped — unconsumed by DocumentLayout
}

BlockHeightEstimator::EstimateResult
BlockHeightEstimator::estimateDefinition(const MarkdownNode& node, const RenderTheme& theme, qreal width) const {
  Q_UNUSED(width);
  // Definition blocks are rare (footnote/link defs) and render precise token rects;
  // measure them when they come into view. One line is a safe placeholder.
  const qreal lineHeight = estimateLineHeight(theme.paragraphFont());
  qreal height = lineHeight;
  if (node.type() == BlockType::FootnoteDefinition && !node.literal().isEmpty()) {
    // Multi-line footnote continuation: rough estimate from source line count.
    height += lineHeight * (static_cast<qreal>(node.literal().count(QLatin1Char('\n'))) + 1.0);
  }
  return {height, true};
}

} // namespace muffin
