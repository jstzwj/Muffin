#pragma once

#include "document/MarkdownTypes.h"
#include "document/NodeId.h"

#include <QObject>
#include <QPair>

class QToolButton;
class QWidget;

namespace muffin {

class BlockLayout;
struct HitTestResult;
class DocumentLayout;

class TableToolbar : public QObject {
  Q_OBJECT

public:
  explicit TableToolbar(QWidget* viewport, QObject* parent = nullptr);

  void update(const HitTestResult& hit, const DocumentLayout* layout,
              qreal scrollY, const QRect& viewportRect);
  void forceHide();

signals:
  void columnAlignmentRequested(TableAlignment alignment);
  void moreActionsRequested(QPoint globalPos);
  void deleteRequested();
  void resizeRequested(int rows, int columns);

private:
  void ensureToolbar();
  void show(const BlockLayout& table, qreal scrollY, const QRect& viewportRect);
  void showResizePopup();

  const BlockLayout* activeTableLayout(const HitTestResult& hit, const DocumentLayout* layout) const;
  QPair<int, int> activeTableSize(const BlockLayout* table) const;

  QWidget* viewport_;
  QWidget* toolbar_ = nullptr;
  QToolButton* resizeButton_ = nullptr;
  QToolButton* alignLeftButton_ = nullptr;
  QToolButton* alignCenterButton_ = nullptr;
  QToolButton* alignRightButton_ = nullptr;
  QToolButton* moreButton_ = nullptr;
  QToolButton* deleteButton_ = nullptr;
  QWidget* resizePopup_ = nullptr;
  int cachedRows_ = 1;
  int cachedColumns_ = 1;
  bool updating_ = false;
};

}  // namespace muffin
