#pragma once

#include "document/TopLevelRangeChange.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QPointer>
#include <QStringList>

#include <memory>

class QCompleter;
class QLineEdit;
class QToolButton;
class QWidget;

namespace muffin {

class MarkdownDocument;

class EditorView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit EditorView(QWidget* parent = nullptr);

  void setDocument(const MarkdownDocument& document);
  bool refreshBlock(NodeId blockId, const MarkdownDocument& document);
  bool refreshBlocks(const QVector<NodeId>& blockIds, const MarkdownDocument& document);
  bool refreshTopLevelRange(TopLevelRangeChange range, const MarkdownDocument& document);
  void setZoomPercent(int percent);
  void setFontSizePx(int px);
  void setTheme(RenderTheme theme);
  void setCursorHit(HitTestResult hit);
  void setCursorPosition(CursorPosition position);
  void setSelectionRange(SelectionRange selection);
  void clearCursor();
  void setCodeLanguageSuggestions(QStringList languages);

  QRectF nodeRect(NodeId id) const;
  void scrollToNode(NodeId id);
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

private:
  void rebuildLayout();
  void updateScrollBars();
  QRectF documentViewportRect() const;
  qreal scrollY() const;
  void applyScrollBarStyle();
  void ensureCodeLanguageEditor();
  void updateCodeLanguageEditor();
  void showCodeLanguageEditor(const BlockLayout& block);
  void hideCodeLanguageEditor();
  void commitCodeLanguageEditor();
  void ensureTableToolbar();
  void updateTableToolbar();
  void showTableToolbar(const BlockLayout& table);
  void hideTableToolbar();
  void showTableResizePopup();
  QPair<int, int> activeTableSize() const;
  const BlockLayout* activeTableLayout() const;
  void updateCursorHitFromPosition();
  void refreshInlineProjectionForSelectionChange(SelectionRange previousSelection);
  void addSelectionBlocks(QVector<NodeId>& blockIds, const SelectionRange& selection) const;
  void paintCurrentTableCell(QPainter& painter) const;
  void paintSelection(QPainter& painter) const;
  void paintInsertionCursor(QPainter& painter) const;
  HitTestResult hitForCursorPosition(CursorPosition position) const;
  QVector<const BlockLayout*> blocksBetween(NodeId first, NodeId last) const;
  bool blockComesBefore(NodeId first, NodeId second) const;
  void applySelectionRange(SelectionRange selection);
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
  bool dragSelectionPending_ = false;
  QPointF dragStartViewportPos_;
  HitTestResult dragAnchorHit_;
  QLineEdit* codeLanguageEditor_ = nullptr;
  QCompleter* codeLanguageCompleter_ = nullptr;
  QStringList codeLanguageSuggestions_;
  NodeId codeLanguageNodeId_;
  bool updatingCodeLanguageEditor_ = false;
  QWidget* tableToolbar_ = nullptr;
  QToolButton* tableResizeButton_ = nullptr;
  QToolButton* tableAlignLeftButton_ = nullptr;
  QToolButton* tableAlignCenterButton_ = nullptr;
  QToolButton* tableAlignRightButton_ = nullptr;
  QToolButton* tableMoreButton_ = nullptr;
  QToolButton* tableDeleteButton_ = nullptr;
  QWidget* tableResizePopup_ = nullptr;
  bool updatingTableToolbar_ = false;
};

}  // namespace muffin
