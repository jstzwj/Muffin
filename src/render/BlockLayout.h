#pragma once

#include "document/MarkdownNode.h"
#include "editor/CursorPosition.h"
#include "math/MathRenderNode.h"
#include "render/CodeHighlight.h"
#include "render/InlineLayout.h"
#include "theme/RenderTheme.h"

#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

namespace muffin {

class BlockLayout {
public:
  enum class ListMarkerKind {
    None,
    OrderedText,
    BulletDisc,
    BulletCircle,
    BulletSquare,
  };

  struct TableCellLayout {
    NodeId nodeId;
    QRectF rect;
    InlineLayout text;
    qsizetype contentSourceStart = -1;
    TableAlignment alignment = TableAlignment::None;
    bool header = false;
    bool alternate = false;
  };

  struct TableRowLayout {
    QRectF rect;
    std::vector<TableCellLayout> cells;
  };

  explicit BlockLayout(NodeId id = {});

  NodeId nodeId() const;
  BlockType type() const;
  void setType(BlockType type);

  QRectF rect() const;
  void setRect(QRectF rect);
  void translate(qreal dx, qreal dy);
  void translateY(qreal dy);

  qreal height() const;
  qreal bottom() const;

  void setInlineLayout(std::unique_ptr<InlineLayout> layout);
  InlineLayout* inlineLayout();
  const InlineLayout* inlineLayout() const;

  void setLiteral(QString literal);
  QString literal() const;
  void setCodeLanguage(QString language);
  QString codeLanguage() const;
  void setCodeHighlightSpans(QVector<CodeHighlightSpan> spans);
  const QVector<CodeHighlightSpan>& codeHighlightSpans() const;
  void setMathLayout(std::shared_ptr<math::MathLayoutResult> layout);
  const math::MathLayoutResult* mathLayout() const;
  void setLiteralEditing(bool editing);
  bool literalEditing() const;
  QRectF literalContentRect(const RenderTheme& theme) const;

  void setHeadingLevel(int level);
  int headingLevel() const;

  void setListMarker(QString marker);
  QString listMarker() const;
  void setListMarkerKind(ListMarkerKind kind);
  ListMarkerKind listMarkerKind() const;
  bool hasListMarker() const;
  void setContentSourceStart(qsizetype sourceStart);
  qsizetype contentSourceStart() const;
  void setPlaceholderText(QString text);
  QString placeholderText() const;
  void setTaskListItem(bool taskListItem, bool checked);
  bool isTaskListItem() const;
  bool taskChecked() const;

  void setDepth(int depth);
  int depth() const;

  void setChildren(std::vector<std::unique_ptr<BlockLayout>> children);
  std::vector<std::unique_ptr<BlockLayout>>& children();
  const std::vector<std::unique_ptr<BlockLayout>>& children() const;

  void setTableRows(std::vector<TableRowLayout> rows);
  std::vector<TableRowLayout>& tableRows();
  const std::vector<TableRowLayout>& tableRows() const;
  QRectF tableCellRect(int row, int column) const;

  void paint(QPainter& painter, const RenderTheme& theme, qreal scrollY) const;
  bool intersects(const QRectF& documentViewport) const;
  bool containsNode(NodeId id) const;
  bool containsInteractiveContent(QPointF documentPos, const RenderTheme& theme) const;
  HitTestResult hitTest(QPointF documentPos, const RenderTheme& theme) const;
  QVector<QRectF> selectionRects(const SelectionRange& selection, const RenderTheme& theme) const;
  QVector<QRectF> selectionRectsForOffsets(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const;

private:
  void paintSelf(QPainter& painter, const RenderTheme& theme, qreal scrollY) const;
  void paintTable(QPainter& painter, const RenderTheme& theme, qreal scrollY) const;
  HitTestResult hitSelf(QPointF documentPos, const RenderTheme& theme) const;
  HitTestResult hitTable(QPointF documentPos, const RenderTheme& theme) const;
  QVector<QRectF> selectionRectsSelf(const SelectionRange& selection, const RenderTheme& theme) const;
  QVector<QRectF> selectionRectsSelfForOffsets(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const;
  QVector<QRectF> literalSelectionRects(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const;
  QRectF mathEditorSourceRect(const RenderTheme& theme) const;
  QRectF mathPreviewContentRect(const RenderTheme& theme) const;
  void paintCodeFence(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const;
  void paintLiteralSource(QPainter& painter, const RenderTheme& theme, QRectF contentRect, const QVector<CodeHighlightSpan>& spans) const;

  NodeId id_;
  BlockType type_ = BlockType::Unknown;
  QRectF rect_;
  std::unique_ptr<InlineLayout> inlineLayout_;
  QString literal_;
  QString codeLanguage_;
  QVector<CodeHighlightSpan> codeHighlightSpans_;
  std::shared_ptr<math::MathLayoutResult> mathLayout_;
  bool literalEditing_ = false;
  int headingLevel_ = 0;
  QString listMarker_;
  ListMarkerKind listMarkerKind_ = ListMarkerKind::None;
  qsizetype contentSourceStart_ = -1;
  QString placeholderText_;
  bool taskListItem_ = false;
  bool taskChecked_ = false;
  int depth_ = 0;
  std::vector<std::unique_ptr<BlockLayout>> children_;
  std::vector<TableRowLayout> tableRows_;
};

}  // namespace muffin
