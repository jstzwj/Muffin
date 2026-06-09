#pragma once

#include "document/TopLevelRangeChange.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QPointer>
#include <QRectF>

#include <memory>

class QPropertyAnimation;
class QWidget;

namespace muffin {

class CodeLanguageEditor;
class MarkdownDocument;
class TableToolbar;

class EditorView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit EditorView(QWidget* parent = nullptr);

  void setDocument(const MarkdownDocument& document, QString documentPath = {});
  bool refreshBlock(NodeId blockId, const MarkdownDocument& document);
  bool refreshBlocks(const QVector<NodeId>& blockIds, const MarkdownDocument& document);
  bool refreshTopLevelRange(TopLevelRangeChange range, const MarkdownDocument& document);
  void setZoomPercent(int percent);
  void setFontSizePx(int px);
  void setTheme(RenderTheme theme);
  void setCursorHit(HitTestResult hit);
  void setCursorPosition(CursorPosition position);
  void setSelectionRange(SelectionRange selection);
  void setEditingHtmlBlock(NodeId id);
  void clearCursor();
  void setCodeLanguageSuggestions(QStringList languages);

  QRectF nodeRect(NodeId id) const;
  void scrollToNode(NodeId id);
  void scrollToCursorCentered();
  void scrollToCursorCenteredAnimated();
  void setTypewriterMode(bool enabled);
  void setFocusMode(bool enabled);
  const BlockLayout* blockAtViewportPos(QPointF viewportPos) const;
  HitTestResult hitTest(QPointF viewportPos) const;

signals:
  void blockClicked(HitTestResult result);
  void selectionChanged(SelectionRange selection, HitTestResult focusHit);
  void textCommitted(QString text);
  void codeLanguageCommitted(NodeId codeId, QString language);
  void tableResizeRequested(int rows, int columns);
  void tableColumnAlignmentRequested(TableAlignment alignment);
  void tableDeleteRequested();
  void tableMoreActionsRequested(QPoint globalPos);
  void htmlEditToggleRequested(NodeId blockId);

protected:
  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private:
  struct HeadingBadge {
    NodeId blockId;
    QRectF viewportRect;
    int level = 0;

    bool isValid() const { return blockId.isValid() && !viewportRect.isEmpty() && level >= 3 && level <= 6; }
  };

  void rebuildLayout();
  void updateScrollBars();
  QRectF documentViewportRect() const;
  qreal scrollY() const;
  void applyScrollBarStyle();
  void updateCodeLanguageEditor();
  void updateTableToolbar();
  void updateCursorHitFromPosition();
  void refreshInlineProjectionForSelectionChange(SelectionRange previousSelection);
  void addSelectionBlocks(QVector<NodeId>& blockIds, const SelectionRange& selection) const;
  void paintCurrentTableCell(QPainter& painter) const;
  void paintSelection(QPainter& painter) const;
  void paintInsertionCursor(QPainter& painter) const;
  void paintHeadingBadge(QPainter& painter) const;
  void paintHtmlHoverOverlay(QPainter& painter) const;
  HeadingBadge headingBadgeForBlock(NodeId blockId) const;
  QRectF headingBadgeViewportRectForBlock(NodeId blockId) const;
  QRectF htmlHoverOverlayViewportRect() const;
  QRectF htmlHoverButtonViewportRect() const;
  void updateHtmlHover(QPointF viewportPos);
  void clearHtmlHover();
  HitTestResult hitForCursorPosition(CursorPosition position) const;
  QVector<const BlockLayout*> blocksBetween(NodeId first, NodeId last) const;
  bool blockComesBefore(NodeId first, NodeId second) const;
  void applySelectionRange(SelectionRange selection);
  void updateDragSelection(QPointF viewportPos);
  void updateMouseCursor(QPointF viewportPos);
  void ensureScrollAnimation();
  void stopScrollAnimation();

  QPointer<const MarkdownDocument> document_;
  QString documentPath_;
  RenderTheme theme_ = RenderTheme::typoraLike();
  std::unique_ptr<DocumentLayout> layout_;
  CursorPosition cursorPosition_;
  SelectionRange selection_;
  HitTestResult cursorHit_;
  bool cursorVisible_ = false;
  bool draggingSelection_ = false;
  bool dragSelectionPending_ = false;
  QPointF dragStartViewportPos_;
  HitTestResult dragAnchorHit_;
  CodeLanguageEditor* codeLanguageEditor_ = nullptr;
  TableToolbar* tableToolbar_ = nullptr;
  bool typewriterMode_ = false;
  bool focusMode_ = false;
  QPropertyAnimation* scrollAnimation_ = nullptr;
  NodeId editingHtmlBlockId_;
  NodeId visibleHtmlHoverBlockId_;
};

}  // namespace muffin
