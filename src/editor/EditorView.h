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

  QRectF nodeRect(NodeId id) const;
  const BlockLayout* blockAtViewportPos(QPointF viewportPos) const;
  HitTestResult hitTest(QPointF viewportPos) const;

signals:
  void blockClicked(HitTestResult result);

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

private:
  void rebuildLayout();
  void updateScrollBars();
  QRectF documentViewportRect() const;
  qreal scrollY() const;
  void applyScrollBarStyle();

  QPointer<const MarkdownDocument> document_;
  RenderTheme theme_ = RenderTheme::typoraLike();
  std::unique_ptr<DocumentLayout> layout_;
};

}  // namespace muffin
