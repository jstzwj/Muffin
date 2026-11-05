#pragma once
#include <utility>

#include "document/MarkdownNode.h"
#include "editor/CursorPosition.h"
#include "theme/RenderTheme.h"

namespace muffin {

struct BlockHeightEstimate {
  qreal height = 0;
  bool mustMeasure = false;
};

// Deliberately independent of retained layouts and rendering resources: the
// offscreen estimate path must never shape or materialize entire paragraphs.
class BlockHeightEstimator {
public:
  using EstimateResult = BlockHeightEstimate;
  void beginPass(quint64 fontGeneration, bool codeBlockWrap, bool showLineNumbers);
  void setSelection(SelectionRange selection) { selection_ = std::move(selection); }
  EstimateResult estimateHeight(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth = 0) const;
  CooperativeTask estimateHeightSteps(const MarkdownNode& node, const RenderTheme& theme, qreal width,
                                      EstimateResult& result, int depth = 0) const;

private:
  SelectionRange selection_;
  bool codeBlockWrap_ = true;
  bool showLineNumbers_ = false;
  quint64 fontGeneration_ = 0;
  // Height-estimate helpers mirroring the build* dispatch. Never touch TextLayout.
  EstimateResult estimateParagraphLike(const MarkdownNode& node, const RenderTheme& theme, qreal width) const;
  EstimateResult estimateContainer(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const;
  CooperativeTask estimateContainerSteps(const MarkdownNode& node, const RenderTheme& theme, qreal width,
                                         EstimateResult& result, int depth) const;
  EstimateResult estimateListItem(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const;
  EstimateResult estimateLiteralBlock(const MarkdownNode& node, const RenderTheme& theme, qreal width) const;
  EstimateResult estimateTable(const MarkdownNode& node, const RenderTheme& theme, qreal width) const;
  EstimateResult estimateDefinition(const MarkdownNode& node, const RenderTheme& theme, qreal width) const;
  qreal estimateLineHeight(const QFont& font) const;
  // Average advance per character for `text` under `font`, classifying each char as wide
  // (CJK/fullwidth ~1em) or narrow (~half-em) using cached per-font metrics. Lets the estimate
  // track CJK vs ASCII density per block without measuring full text widths.
  qreal avgCharWidthForText(QStringView text, const QFont& font) const;
  // O(1) variant: cached narrow advance only, no per-char walk. Used by the estimate path to skip
  // inline-text materialization (see estimateParagraphLike). Exact for ASCII; CJK imprecision is
  // acceptable since the estimate is only a scrollbar placeholder.
  qreal avgCharWidthForFont(const QFont& font) const;
  // Cached estimateLineHeightForElement per element key — avoids re-creating QFont + TextFontMetrics
  // every block (~14µs/block otherwise, dominating the estimate loop on 250k-block docs).
  qreal cachedEstimateLineHeight(const RenderTheme& theme, const QString& elementKey, int headingLevel) const;
  // Cached avgCharWidthForFont per element key — avoids creating QFont + TextFontMetrics per block.
  qreal cachedAvgCharWidthForElement(const RenderTheme& theme, const QString& elementKey, bool isHeading,
                                     int headingLevel) const;
  mutable QHash<QString, QPair<qreal, qreal>> fontMetricsCache_; // QFont::key() -> {wideAdvance, narrowAdvance}
  mutable QHash<QString, qreal> lineHeightCache_;                // "elementKey|headingLevel" -> estimated line height
  mutable QHash<QString, qreal> avgCharWidthCache_;              // "elementKey|headingLevel" -> cached narrow advance
  struct ParagraphEstimate {
    qreal charsPerLine = 1;
    qreal lineHeight = 0;
    ThemeElementBoxStyle box;
  };
  // One used box/wrap capacity per prototype and containing width per pass.
  mutable QHash<QString, QHash<qreal, ParagraphEstimate>> paragraphEstimateCache_;
};

} // namespace muffin
