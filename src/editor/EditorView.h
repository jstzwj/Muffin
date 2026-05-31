#pragma once

#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QAbstractScrollArea>
#include <QPointer>

#include <memory>

namespace muffin {

class MarkdownDocument;

class EditorView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit EditorView(QWidget* parent = nullptr);

  void setDocument(const MarkdownDocument& document);
  void setZoomPercent(int percent);
  void setTheme(RenderTheme theme);
  void setCursorHit(HitTestResult hit);
  void setCursorPosition(CursorPosition position);
  void setSelectionRange(SelectionRange selection);
  void clearCursor();

  QRectF nodeRect(NodeId id) const;
  const BlockLayout* blockAtViewportPos(QPointF viewportPos) const;
  HitTestResult hitTest(QPointF viewportPos) const;

signals:
  void blockClicked(HitTestResult result);
  void selectionChanged(SelectionRange selection, HitTestResult focusHit);
  void textCommitted(QString text);

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
  void rebuildLayout();
  void updateScrollBars();
  QRectF documentViewportRect() const;
  qreal scrollY() const;
  void applyScrollBarStyle();
  void paintSelection(QPainter& painter) const;
  void paintInsertionCursor(QPainter& painter) const;
  HitTestResult hitForCursorPosition(CursorPosition position) const;
  QVector<const BlockLayout*> blocksBetween(NodeId first, NodeId last) const;
  bool blockComesBefore(NodeId first, NodeId second) const;
  void updateDragSelection(QPointF viewportPos);
  void updateMouseCursor(QPointF viewportPos);

  QPointer<const MarkdownDocument> document_;
  RenderTheme theme_ = RenderTheme::typoraLike();
  std::unique_ptr<DocumentLayout> layout_;
  CursorPosition cursorPosition_;
  SelectionRange selection_;
  HitTestResult cursorHit_;
  bool cursorVisible_ = false;
  bool draggingSelection_ = false;
  HitTestResult dragAnchorHit_;
};

}  // namespace muffin
