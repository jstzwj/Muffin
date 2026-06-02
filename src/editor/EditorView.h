#pragma once

#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QAbstractScrollArea>
#include <QPointer>
#include <QStringList>

#include <memory>

class QCompleter;
class QLineEdit;

namespace muffin {

class MarkdownDocument;

class EditorView final : public QAbstractScrollArea {
  Q_OBJECT

public:
  explicit EditorView(QWidget* parent = nullptr);

  void setDocument(const MarkdownDocument& document);
  bool refreshBlock(NodeId blockId, const MarkdownDocument& document);
  bool refreshBlocks(const QVector<NodeId>& blockIds, const MarkdownDocument& document);
  void setZoomPercent(int percent);
  void setTheme(RenderTheme theme);
  void setCursorHit(HitTestResult hit);
  void setCursorPosition(CursorPosition position);
  void setSelectionRange(SelectionRange selection);
  void clearCursor();
  void setCodeLanguageSuggestions(QStringList languages);
  void setInlineGeometryBackend(InlineLayout::InlineGeometryBackend backend);
  InlineLayout::InlineGeometryBackend inlineGeometryBackend() const;

  QRectF nodeRect(NodeId id) const;
  const BlockLayout* blockAtViewportPos(QPointF viewportPos) const;
  HitTestResult hitTest(QPointF viewportPos) const;

signals:
  void blockClicked(HitTestResult result);
  void selectionChanged(SelectionRange selection, HitTestResult focusHit);
  void textCommitted(QString text);
  void codeLanguageCommitted(NodeId codeId, QString language);

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
  void ensureCodeLanguageEditor();
  void updateCodeLanguageEditor();
  void showCodeLanguageEditor(const BlockLayout& block);
  void hideCodeLanguageEditor();
  void commitCodeLanguageEditor();
  void updateCursorHitFromPosition();
  void refreshInlineProjectionForSelectionChange(SelectionRange previousSelection);
  void addSelectionBlocks(QVector<NodeId>& blockIds, const SelectionRange& selection) const;
  void paintCurrentTableCell(QPainter& painter) const;
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
  InlineLayout::InlineGeometryBackend inlineGeometryBackend_ = InlineLayout::InlineGeometryBackend::QTextLayout;
  CursorPosition cursorPosition_;
  SelectionRange selection_;
  HitTestResult cursorHit_;
  bool cursorVisible_ = false;
  bool draggingSelection_ = false;
  HitTestResult dragAnchorHit_;
  QLineEdit* codeLanguageEditor_ = nullptr;
  QCompleter* codeLanguageCompleter_ = nullptr;
  QStringList codeLanguageSuggestions_;
  NodeId codeLanguageNodeId_;
  bool updatingCodeLanguageEditor_ = false;
};

}  // namespace muffin
