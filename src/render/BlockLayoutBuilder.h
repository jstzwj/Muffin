#pragma once

#include "document/MarkdownNode.h"
#include "render/BlockLayout.h"
#include "theme/RenderTheme.h"

#include <memory>

namespace muffin {

class BlockLayoutBuilder {
public:
  void setMarkdownText(QString markdownText);
  void setActiveCursor(CursorPosition cursor);
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

  QString textForListMarker(const MarkdownNode& listNode, qsizetype index) const;
  QVector<InlineNode> primaryInlinesForListItem(const MarkdownNode& node) const;
  QString sourceTextForEditableNode(const MarkdownNode& node) const;
  qsizetype sourceContentStartForEditableNode(const MarkdownNode& node) const;
  qsizetype sourceOffsetForLineColumn(int line, int column) const;
  qsizetype sourceOffsetForLineEnd(int line) const;
  qreal textHeight(const QString& text, const QFont& font, qreal width, const QMarginsF& padding) const;

  QString markdownText_;
  CursorPosition activeCursor_;
};

}  // namespace muffin
