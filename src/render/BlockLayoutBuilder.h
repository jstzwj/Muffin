#pragma once

#include "document/LineStartOffsetCache.h"
#include "document/MarkdownNode.h"
#include "document/PieceTable.h"
#include "html/HtmlRenderer.h"
#include "math/MathRenderer.h"
#include "render/BlockLayout.h"
#include "render/TreeSitterHighlighter.h"
#include "theme/RenderTheme.h"

#include <QHash>
#include <QPair>
#include <QStringView>
#include <QTextLayout>
#include <memory>

namespace muffin {

class CodeFenceScrollController;

class BlockLayoutBuilder {
public:
  // Holds a non-owning view onto the document's piece-table text (the document outlives every
  // build, and configureBuilder refreshes it before each build/estimate). Avoids materializing the
  // whole document text into the builder on every per-keystroke rebuildBlock.
  void setMarkdownText(const PieceTable& markdownText, const LineStartOffsetCache& lineOffsets);
  void setSelection(SelectionRange selection);
  // The active IME composition (paint-layer only) to splice into the caret block's inline layout,
  // so the in-progress text shifts following text instead of overlapping it. Empty when none.
  void setPreedit(QString text, QVector<QTextLayout::FormatRange> formats, int cursor);
  void setEditingHtmlBlock(NodeId id);
  void setDocumentPath(QString path);
  // Per-code-fence horizontal scroll state (offset + longest-line width), keyed by NodeId and
  // owned outside the rebuilt BlockLayouts. The builder writes the measured line width so the
  // paint path and scrollbar thumb agree on scrollability.
  void setCodeFenceScroll(CodeFenceScrollController* controller);
  // DocumentLayout's heading-counter map (NodeId → resolved ::before text, e.g.
  // "1. "). Owned by the layout; the builder only reads it. nullptr/empty for
  // non-counter themes. Set every pass by configureBuilder so single-block and
  // range rebuilds share the same map the full/range pass just recomputed.
  void setHeadingCounterText(const QHash<NodeId, QString>* map);

  // Read the render-affecting markdown/* settings ONCE per layout pass (call from configureBuilder)
  // into the members below, so the per-block estimate/build loops don't hit QSettings (Windows
  // registry, ~100µs/read) hundreds of thousands of times — that alone was ~25s of the open/re-layout
  // estimate on a 250k-block doc. Settings change only via the prefs dialog, which forces a full
  // refresh anyway, so per-pass caching is always fresh enough.
  void refreshRenderSettings();

  BlockLayoutBuilder();

  // Emits aggregate per-bucket timings for the build() work accumulated across one
  // rebuild (inline text shaping / code highlight / math render / html render / literal
  // text height). No-op when muffin.perf debug is disabled.
  void dumpBuildBreakdown() const;

  // Cheap height estimate WITHOUT QTextLayout / tree-sitter / math / html rendering.
  // Used by the lazy layout to size offscreen blocks so the scrollbar/scroll mapping
  // stay valid before a block is promoted to full detail. mustMeasure=true means the
  // rendered size is not cheaply knowable (math/html rendered blocks, or text blocks
  // containing inline math/images) and the estimate is a placeholder.
  struct EstimateResult {
    qreal height = 0;
    bool mustMeasure = false;
  };
  EstimateResult estimateHeight(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth = 0) const;

  std::unique_ptr<BlockLayout> build(const MarkdownNode& node, const RenderTheme& theme, qreal x, qreal y, qreal width, int depth = 0);

private:
  // Copy the active preedit (if any) onto `options` when `options.projectionState` marks this block
  // as the caret block. Called at every inline-build site (paragraph / list item / table cell).
  void applyPreedit(InlineLayout::BuildOptions& options) const;
  std::unique_ptr<BlockLayout> buildParagraphLike(
      const MarkdownNode& node,
      const RenderTheme& theme,
      qreal x,
      qreal y,
      qreal width,
      int depth);
  std::unique_ptr<BlockLayout> buildContainer(
      const MarkdownNode& node,
      const RenderTheme& theme,
      qreal x,
      qreal y,
      qreal width,
      int depth);
  std::unique_ptr<BlockLayout> buildListItem(
      const MarkdownNode& node,
      const RenderTheme& theme,
      qreal x,
      qreal y,
      qreal width,
      int depth);
  std::unique_ptr<BlockLayout> buildLiteralBlock(
      const MarkdownNode& node,
      const RenderTheme& theme,
      qreal x,
      qreal y,
      qreal width,
      int depth);
  std::unique_ptr<BlockLayout> buildTable(
      const MarkdownNode& node,
      const RenderTheme& theme,
      qreal x,
      qreal y,
      qreal width,
      int depth);
  std::unique_ptr<BlockLayout> buildThematicBreak(
      const MarkdownNode& node,
      const RenderTheme& theme,
      qreal x,
      qreal y,
      qreal width,
      int depth);
  std::unique_ptr<BlockLayout> buildDefinition(
      const MarkdownNode& node,
      const RenderTheme& theme,
      qreal x,
      qreal y,
      qreal width,
      int depth);

  QString textForListMarker(const MarkdownNode& listNode, qsizetype index) const;
  BlockLayout::ListMarkerKind markerKindForListItem(const MarkdownNode& itemNode) const;
  // CSS `list-style-type`-aware marker: kind + text from the node's resolved li
  // style, falling back to the legacy depth/kind-based marker when unset.
  struct ResolvedMarker { BlockLayout::ListMarkerKind kind = BlockLayout::ListMarkerKind::None; QString text; };
  ResolvedMarker resolveListMarker(const MarkdownNode& itemNode, const RenderTheme& theme, qsizetype itemIndex) const;
  QVector<InlineNode> primaryInlinesForListItem(const MarkdownNode& node) const;
  QString sourceTextForEditableNode(const MarkdownNode& node) const;
  qsizetype sourceContentStartForEditableNode(const MarkdownNode& node) const;
  qsizetype sourceContentEndForEditableNode(const MarkdownNode& node) const;
  qsizetype sourceOffsetForLineColumn(int line, int column) const;
  qsizetype sourceOffsetForLineEnd(int line) const;
  // The live document text (non-owning piece-table view set by configureBuilder). Always non-null:
  // it defaults to emptyText_ and is refreshed with the live document before every build/estimate,
  // so a stray build without configureBuilder yields an empty layout instead of a null dereference.
  const PieceTable& md() const;
  qreal textHeight(const QString& text, const QFont& font, qreal lineHeight, qreal width, const QMarginsF& padding, bool wrap = true) const;

  // Height-estimate helpers mirroring the build* dispatch. Never touch QTextLayout.
  EstimateResult estimateParagraphLike(const MarkdownNode& node, const RenderTheme& theme, qreal width) const;
  EstimateResult estimateContainer(const MarkdownNode& node, const RenderTheme& theme, qreal width, int depth) const;
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
  // Cached estimateLineHeightForElement per element key — avoids re-creating QFont + QFontMetricsF
  // every block (~14µs/block otherwise, dominating the estimate loop on 250k-block docs).
  qreal cachedEstimateLineHeight(const RenderTheme& theme, const QString& elementKey, BlockType type, int headingLevel) const;
  // Cached avgCharWidthForFont per element key — avoids creating QFont + QFontMetricsF per block.
  qreal cachedAvgCharWidthForElement(const RenderTheme& theme, const QString& elementKey, bool isHeading, int headingLevel) const;

  QString documentPath_;
  // Stable empty fallbacks so the non-owning views below are NEVER null. A build/estimate that runs
  // before configureBuilder (or after the document has been cleared) reads empty text + an empty
  // line-offset cache — a graceful no-op — instead of dereferencing a null pointer. The owning-
  // text overload of setMarkdownText was removed, so these no longer get repopulated; they exist
  // purely as the safe default pointees.
  PieceTable emptyText_;
  LineStartOffsetCache emptyLineOffsets_;
  const LineStartOffsetCache* lineOffsets_ = &emptyLineOffsets_;
  SelectionRange selection_;
  QString preeditText_;
  QVector<QTextLayout::FormatRange> preeditFormats_;
  int preeditCursor_ = -1;
  NodeId editingHtmlBlockId_;
  const QHash<NodeId, QString>* headingCounterText_ = nullptr;
  // Cached once per layout pass by refreshRenderSettings() (configureBuilder). The per-block
  // estimate/build loops read these instead of hitting QSettings per block.
  bool breakOnSingleNewline_ = true;
  bool codeBlockWrap_ = true;
  bool showLineNumbers_ = false;
  CodeFenceScrollController* codeFenceScroll_ = nullptr;
  TreeSitterHighlighter codeHighlighter_;
  math::MathRenderer mathRenderer_;
  html::HtmlRenderer htmlRenderer_;

  // Aggregate build-time accumulators (nanoseconds), reported via dumpBuildBreakdown().
  qint64 inlineLayoutNs_ = 0;
  qint64 codeHighlightNs_ = 0;
  qint64 mathRenderNs_ = 0;
  qint64 htmlRenderNs_ = 0;
  qint64 literalTextNs_ = 0;
  bool perfEnabled_ = false;
  mutable QHash<QString, QPair<qreal, qreal>> fontMetricsCache_;  // QFont::key() -> {wideAdvance, narrowAdvance}
  mutable QHash<QString, qreal> lineHeightCache_;  // "elementKey|headingLevel" -> estimated line height
  mutable QHash<QString, qreal> avgCharWidthCache_;  // "elementKey|headingLevel" -> cached narrow advance
  // Widest ordered-list marker width per list (keyed by the list NodeId), so buildListItem
  // doesn't re-measure every sibling on every item (O(N²) per list). Cleared per pass in
  // refreshRenderSettings — a list's marker width can change across a theme switch or an
  // item add/remove (both trigger a rebuild pass); within one pass the first item of each
  // list measures once and the rest reuse it.
  QHash<NodeId, qreal> widestOrderedMarkerCache_;

  const PieceTable* markdownText_ = &emptyText_;  // non-owning; refreshed by configureBuilder before each build
};

}  // namespace muffin
