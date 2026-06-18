#pragma once

#include "document/LineStartOffsetCache.h"
#include "document/MarkdownNode.h"
#include "html/HtmlRenderer.h"
#include "math/MathRenderer.h"
#include "render/BlockLayout.h"
#include "render/TreeSitterHighlighter.h"
#include "theme/RenderTheme.h"

#include <QHash>
#include <QPair>
#include <QStringView>
#include <memory>

namespace muffin {

class CodeFenceScrollController;

class BlockLayoutBuilder {
public:
  void setMarkdownText(QString markdownText);
  void setMarkdownText(QString markdownText, const LineStartOffsetCache& lineOffsets);
  void setSelection(SelectionRange selection);
  void setEditingHtmlBlock(NodeId id);
  void setDocumentPath(QString path);
  // Per-code-fence horizontal scroll state (offset + longest-line width), keyed by NodeId and
  // owned outside the rebuilt BlockLayouts. The builder writes the measured line width so the
  // paint path and scrollbar thumb agree on scrollability.
  void setCodeFenceScroll(CodeFenceScrollController* controller);

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
  QVector<InlineNode> primaryInlinesForListItem(const MarkdownNode& node) const;
  QString sourceTextForEditableNode(const MarkdownNode& node) const;
  qsizetype sourceContentStartForEditableNode(const MarkdownNode& node) const;
  qsizetype sourceContentEndForEditableNode(const MarkdownNode& node) const;
  qsizetype sourceOffsetForLineColumn(int line, int column) const;
  qsizetype sourceOffsetForLineEnd(int line) const;
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

  QString markdownText_;
  QString documentPath_;
  LineStartOffsetCache ownedLineOffsets_;
  const LineStartOffsetCache* lineOffsets_ = &ownedLineOffsets_;
  SelectionRange selection_;
  NodeId editingHtmlBlockId_;
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
};

}  // namespace muffin
