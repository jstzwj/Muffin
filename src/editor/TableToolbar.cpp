#include "editor/TableToolbar.h"

#include "editor/CursorPosition.h"
#include "render/BlockLayout.h"
#include "render/DocumentLayout.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>

#include <functional>

namespace muffin {
namespace {

enum class IconKind {
  Resize,
  AlignLeft,
  AlignCenter,
  AlignRight,
  More,
  Delete,
};

QIcon toolbarIcon(IconKind kind) {
  constexpr int iconSize = 16;
  const QColor ink(17, 24, 39);
  const QColor mutedInk(17, 24, 39, 150);
  QPixmap pixmap(iconSize, iconSize);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  auto drawAlignmentLines = [&](const QVector<QLineF>& lines) {
    for (const QLineF& line : lines) {
      painter.drawLine(line);
    }
  };

  switch (kind) {
    case IconKind::Resize:
      painter.drawRoundedRect(QRectF(2.5, 2.5, 11.0, 11.0), 1.2, 1.2);
      painter.setPen(QPen(mutedInk, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(QLineF(6.0, 2.7, 6.0, 13.3));
      painter.drawLine(QLineF(10.0, 2.7, 10.0, 13.3));
      painter.drawLine(QLineF(2.7, 6.0, 13.3, 6.0));
      painter.drawLine(QLineF(2.7, 10.0, 13.3, 10.0));
      painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(QLineF(5.2, 5.2, 3.8, 3.8));
      painter.drawLine(QLineF(3.8, 3.8, 3.8, 5.1));
      painter.drawLine(QLineF(3.8, 3.8, 5.1, 3.8));
      painter.drawLine(QLineF(10.8, 10.8, 12.2, 12.2));
      painter.drawLine(QLineF(12.2, 12.2, 12.2, 10.9));
      painter.drawLine(QLineF(12.2, 12.2, 10.9, 12.2));
      break;
    case IconKind::AlignLeft:
      drawAlignmentLines({QLineF(2.5, 3.0, 13.5, 3.0), QLineF(2.5, 6.0, 10.0, 6.0), QLineF(2.5, 9.0, 13.5, 9.0),
                          QLineF(2.5, 12.0, 9.5, 12.0)});
      break;
    case IconKind::AlignCenter:
      drawAlignmentLines({QLineF(2.5, 3.0, 13.5, 3.0), QLineF(4.5, 6.0, 11.5, 6.0), QLineF(2.5, 9.0, 13.5, 9.0),
                          QLineF(5.0, 12.0, 11.0, 12.0)});
      break;
    case IconKind::AlignRight:
      drawAlignmentLines({QLineF(2.5, 3.0, 13.5, 3.0), QLineF(6.0, 6.0, 13.5, 6.0), QLineF(2.5, 9.0, 13.5, 9.0),
                          QLineF(6.5, 12.0, 13.5, 12.0)});
      break;
    case IconKind::More:
      painter.setPen(Qt::NoPen);
      painter.setBrush(ink);
      painter.drawEllipse(QPointF(8.0, 3.5), 1.35, 1.35);
      painter.drawEllipse(QPointF(8.0, 8.0), 1.35, 1.35);
      painter.drawEllipse(QPointF(8.0, 12.5), 1.35, 1.35);
      break;
    case IconKind::Delete:
      painter.drawLine(QLineF(3.0, 4.5, 13.0, 4.5));
      painter.drawLine(QLineF(6.2, 4.5, 6.2, 3.1));
      painter.drawLine(QLineF(6.2, 3.1, 9.8, 3.1));
      painter.drawLine(QLineF(9.8, 3.1, 9.8, 4.5));
      painter.drawLine(QLineF(4.3, 4.5, 4.9, 12.9));
      painter.drawLine(QLineF(11.7, 4.5, 11.1, 12.9));
      painter.drawLine(QLineF(4.9, 12.9, 6.2, 14.0));
      painter.drawLine(QLineF(11.1, 12.9, 9.8, 14.0));
      painter.drawLine(QLineF(6.2, 14.0, 9.8, 14.0));
      painter.drawLine(QLineF(6.8, 7.2, 6.8, 11.4));
      painter.drawLine(QLineF(9.2, 7.2, 9.2, 11.4));
      break;
  }
  painter.end();

  QIcon icon;
  icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
  icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
  icon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
  icon.addPixmap(pixmap, QIcon::Disabled, QIcon::Off);
  return icon;
}

class TableResizePopup final : public QFrame {
public:
  explicit TableResizePopup(QWidget* parent = nullptr) : QFrame(parent, Qt::Popup) {
    setMouseTracking(true);
    setFrameShape(QFrame::NoFrame);
    setFixedSize(gridColumns_ * cellSize_ + 24, gridRows_ * cellSize_ + 56);
  }

  void setCurrentSize(int rows, int columns) {
    currentRows_ = qMax(1, rows);
    currentColumns_ = qMax(1, columns);
    hoverRows_ = currentRows_;
    hoverColumns_ = currentColumns_;
    gridRows_ = qBound(10, currentRows_ + 3, 20);
    gridColumns_ = qBound(10, currentColumns_ + 3, 20);
    setFixedSize(gridColumns_ * cellSize_ + 24, gridRows_ * cellSize_ + 56);
    update();
  }

  void setResizeCallback(std::function<void(int, int)> callback) {
    callback_ = std::move(callback);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(255, 255, 255));
    painter.setPen(QPen(QColor(215, 220, 226)));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 5, 5);

    const QColor selectedFill(79, 143, 247, 64);
    const QColor currentBorder(150, 150, 150);
    const int left = 12;
    const int top = 12;
    for (int row = 0; row < gridRows_; ++row) {
      for (int column = 0; column < gridColumns_; ++column) {
        const QRect cell(left + column * cellSize_, top + row * cellSize_, cellSize_ - 2, cellSize_ - 2);
        const bool selected = row < hoverRows_ && column < hoverColumns_;
        const bool current = row < currentRows_ && column < currentColumns_;
        painter.setPen(current ? QPen(currentBorder) : QPen(QColor(206, 211, 217)));
        painter.setBrush(selected ? selectedFill : QColor(248, 249, 250));
        painter.drawRect(cell);
      }
    }

    painter.setPen(QColor(68, 68, 68));
    painter.drawText(QRect(0, height() - 34, width(), 24), Qt::AlignCenter, QStringLiteral("%1 x %2").arg(hoverColumns_).arg(hoverRows_));
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    updateHover(event->position().toPoint());
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      QFrame::mousePressEvent(event);
      return;
    }
    if (updateHover(event->position().toPoint()) && callback_) {
      callback_(hoverRows_, hoverColumns_);
      hide();
      event->accept();
      return;
    }
    QFrame::mousePressEvent(event);
  }

  void leaveEvent(QEvent*) override {
    hoverRows_ = currentRows_;
    hoverColumns_ = currentColumns_;
    update();
  }

private:
  bool updateHover(QPoint pos) {
    const int left = 12;
    const int top = 12;
    const int column = (pos.x() - left) / cellSize_;
    const int row = (pos.y() - top) / cellSize_;
    if (pos.x() < left || pos.y() < top || column < 0 || row < 0 || column >= gridColumns_ || row >= gridRows_) {
      return false;
    }
    hoverRows_ = row + 1;
    hoverColumns_ = column + 1;
    update();
    return true;
  }

  int currentRows_ = 1;
  int currentColumns_ = 1;
  int hoverRows_ = 1;
  int hoverColumns_ = 1;
  int gridRows_ = 10;
  int gridColumns_ = 10;
  const int cellSize_ = 15;
  std::function<void(int, int)> callback_;
};

}  // namespace

TableToolbar::TableToolbar(QWidget* viewport, QObject* parent)
    : QObject(parent), viewport_(viewport) {}

void TableToolbar::update(const HitTestResult& hit, const DocumentLayout* layout,
                          qreal scrollY, const QRect& viewportRect) {
  if (updating_) {
    return;
  }
  const BlockLayout* table = activeTableLayout(hit, layout);
  if (!table) {
    forceHide();
    return;
  }
  const auto [rows, cols] = activeTableSize(table);
  cachedRows_ = rows;
  cachedColumns_ = cols;
  show(*table, scrollY, viewportRect);
}

void TableToolbar::forceHide() {
  if (toolbar_) {
    toolbar_->hide();
  }
  if (resizePopup_) {
    resizePopup_->hide();
  }
}

void TableToolbar::ensureToolbar() {
  if (toolbar_) {
    return;
  }

  toolbar_ = new QWidget(viewport_);
  toolbar_->setObjectName(QStringLiteral("tableFloatingToolbar"));
  toolbar_->setFocusPolicy(Qt::NoFocus);
  toolbar_->hide();
  auto* layout = new QHBoxLayout(toolbar_);
  layout->setContentsMargins(4, 3, 4, 3);
  layout->setSpacing(1);

  auto makeButton = [this, layout](IconKind iconKind, const QString& tooltip) {
    auto* button = new QToolButton(toolbar_);
    button->setIcon(toolbarIcon(iconKind));
    button->setIconSize(QSize(16, 16));
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(24, 22);
    layout->addWidget(button);
    return button;
  };

  resizeButton_ = makeButton(IconKind::Resize, QStringLiteral("调整表格大小"));
  alignLeftButton_ = makeButton(IconKind::AlignLeft, QStringLiteral("左对齐"));
  alignCenterButton_ = makeButton(IconKind::AlignCenter, QStringLiteral("居中对齐"));
  alignRightButton_ = makeButton(IconKind::AlignRight, QStringLiteral("右对齐"));
  moreButton_ = makeButton(IconKind::More, QStringLiteral("更多操作"));
  deleteButton_ = makeButton(IconKind::Delete, QStringLiteral("删除表格"));

  toolbar_->setStyleSheet(QStringLiteral(
      "QWidget#tableFloatingToolbar {"
      "  background:rgba(244,246,248,248);"
      "  border:1px solid #c9d0d8;"
      "  border-radius:5px;"
      "}"
      "QToolButton {"
      "  background:rgba(255,255,255,0);"
      "  border:0;"
      "  color:#111827;"
      "  font-size:13px;"
      "}"
      "QToolButton:hover {"
      "  background:#dbeafe;"
      "  border-radius:3px;"
      "}"
      "QToolButton:pressed {"
      "  background:#bfdbfe;"
      "  border-radius:3px;"
      "}"));
  auto* shadow = new QGraphicsDropShadowEffect(toolbar_);
  shadow->setBlurRadius(12.0);
  shadow->setOffset(0.0, 2.0);
  shadow->setColor(QColor(15, 23, 42, 28));
  toolbar_->setGraphicsEffect(shadow);

  connect(resizeButton_, &QToolButton::clicked, this, &TableToolbar::showResizePopup);
  connect(alignLeftButton_, &QToolButton::clicked, this, [this] { emit columnAlignmentRequested(TableAlignment::Left); });
  connect(alignCenterButton_, &QToolButton::clicked, this, [this] { emit columnAlignmentRequested(TableAlignment::Center); });
  connect(alignRightButton_, &QToolButton::clicked, this, [this] { emit columnAlignmentRequested(TableAlignment::Right); });
  connect(moreButton_, &QToolButton::clicked, this, [this] {
    emit moreActionsRequested(moreButton_->mapToGlobal(QPoint(0, moreButton_->height())));
  });
  connect(deleteButton_, &QToolButton::clicked, this, [this] { emit deleteRequested(); });
}

void TableToolbar::show(const BlockLayout& table, qreal scrollY, const QRect& viewportRect) {
  ensureToolbar();
  if (!toolbar_) {
    return;
  }

  QRectF tableRect = table.rect().translated(0, -scrollY);
  if (!tableRect.intersects(QRectF(viewportRect).adjusted(0, -40, 0, 40))) {
    forceHide();
    return;
  }

  updating_ = true;
  toolbar_->adjustSize();
  int x = qRound(tableRect.left());
  int y = qRound(tableRect.top()) - toolbar_->height() - 6;
  if (y < 4) {
    y = qRound(tableRect.top()) + 4;
  }
  x = qBound(4, x, qMax(4, viewportRect.width() - toolbar_->width() - 4));
  y = qBound(4, y, qMax(4, viewportRect.height() - toolbar_->height() - 4));
  toolbar_->move(x, y);
  toolbar_->show();
  toolbar_->raise();
  updating_ = false;
}

void TableToolbar::showResizePopup() {
  ensureToolbar();
  if (!resizeButton_) {
    return;
  }

  if (!resizePopup_) {
    auto* popup = new TableResizePopup(viewport_->parentWidget());
    popup->setResizeCallback([this](int rows, int columns) { emit resizeRequested(rows, columns); });
    resizePopup_ = popup;
  }

  auto* popup = static_cast<TableResizePopup*>(resizePopup_);
  popup->setCurrentSize(cachedRows_, cachedColumns_);
  popup->move(resizeButton_->mapToGlobal(QPoint(0, resizeButton_->height() + 3)));
  popup->show();
  popup->raise();
}

const BlockLayout* TableToolbar::activeTableLayout(const HitTestResult& hit, const DocumentLayout* layout) const {
  if (!layout || hit.zone != HitTestResult::Zone::TableCell || hit.tableRow < 0 || hit.tableColumn < 0) {
    return nullptr;
  }
  const BlockLayout* table = layout->block(hit.blockId);
  if (!table || table->type() != BlockType::Table || hit.tableRow >= static_cast<int>(table->tableRows().size())) {
    return nullptr;
  }
  const auto& row = table->tableRows().at(static_cast<size_t>(hit.tableRow));
  if (hit.tableColumn >= static_cast<int>(row.cells.size())) {
    return nullptr;
  }
  return table;
}

QPair<int, int> TableToolbar::activeTableSize(const BlockLayout* table) const {
  if (!table) {
    return {1, 1};
  }
  int rows = static_cast<int>(table->tableRows().size());
  int columns = 1;
  for (const BlockLayout::TableRowLayout& row : table->tableRows()) {
    columns = qMax(columns, static_cast<int>(row.cells.size()));
  }
  return {qMax(1, rows), columns};
}

}  // namespace muffin
