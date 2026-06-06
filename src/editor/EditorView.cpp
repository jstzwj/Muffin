#include "editor/EditorView.h"

#include "document/MarkdownDocument.h"

#include <QApplication>
#include <QCompleter>
#include <QElapsedTimer>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QLineEdit>
#include <QListView>
#include <QLoggingCategory>
#include <QScrollBar>
#include <QStringListModel>
#include <QToolButton>
#include <QVector>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <QFontMetricsF>
#include <QTextLayout>
#include <QTextOption>
#include <QKeyEvent>

#include <functional>

namespace muffin {
namespace {

Q_LOGGING_CATEGORY(viewPerf, "muffin.perf", QtWarningMsg)

bool sameCursorPosition(const CursorPosition& a, const CursorPosition& b) {
  return a.blockId == b.blockId && a.text.nodeId == b.text.nodeId && a.text.textOffset == b.text.textOffset &&
         a.text.sourceOffset == b.text.sourceOffset && a.text.inMeta == b.text.inMeta;
}

bool sameSelectionRange(const SelectionRange& a, const SelectionRange& b) {
  return sameCursorPosition(a.anchor, b.anchor) && sameCursorPosition(a.focus, b.focus);
}

enum class TableToolbarIconKind {
  Resize,
  AlignLeft,
  AlignCenter,
  AlignRight,
  More,
  Delete,
};

QIcon tableToolbarIcon(TableToolbarIconKind kind) {
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
    case TableToolbarIconKind::Resize:
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
    case TableToolbarIconKind::AlignLeft:
      drawAlignmentLines({QLineF(2.5, 3.0, 13.5, 3.0), QLineF(2.5, 6.0, 10.0, 6.0), QLineF(2.5, 9.0, 13.5, 9.0),
                          QLineF(2.5, 12.0, 9.5, 12.0)});
      break;
    case TableToolbarIconKind::AlignCenter:
      drawAlignmentLines({QLineF(2.5, 3.0, 13.5, 3.0), QLineF(4.5, 6.0, 11.5, 6.0), QLineF(2.5, 9.0, 13.5, 9.0),
                          QLineF(5.0, 12.0, 11.0, 12.0)});
      break;
    case TableToolbarIconKind::AlignRight:
      drawAlignmentLines({QLineF(2.5, 3.0, 13.5, 3.0), QLineF(6.0, 6.0, 13.5, 6.0), QLineF(2.5, 9.0, 13.5, 9.0),
                          QLineF(6.5, 12.0, 13.5, 12.0)});
      break;
    case TableToolbarIconKind::More:
      painter.setPen(Qt::NoPen);
      painter.setBrush(ink);
      painter.drawEllipse(QPointF(8.0, 3.5), 1.35, 1.35);
      painter.drawEllipse(QPointF(8.0, 8.0), 1.35, 1.35);
      painter.drawEllipse(QPointF(8.0, 12.5), 1.35, 1.35);
      break;
    case TableToolbarIconKind::Delete:
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

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(viewPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(viewPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

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

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin) {
  const QFontMetricsF metrics(font);
  const qreal lineHeight = qMax<qreal>(14.0, metrics.height());
  offset = qBound<qsizetype>(0, offset, literal.size());

  qsizetype lineStart = 0;
  int line = 0;
  for (qsizetype i = 0; i < offset && i < literal.size(); ++i) {
    if (literal.at(i) == QLatin1Char('\n')) {
      ++line;
      lineStart = i + 1;
    }
  }

  const qreal x = metrics.horizontalAdvance(literal.mid(lineStart, offset - lineStart));
  return QRectF(origin.x() + x, origin.y() + line * lineHeight, 1.0, lineHeight);
}

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin, qreal width, qreal lineHeight) {
  const QFontMetricsF metrics(font);
  const qreal fallbackHeight = qMax<qreal>(14.0, lineHeight);
  offset = qBound<qsizetype>(0, offset, literal.size());

  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

  const QStringList physicalLines = literal.isEmpty() ? QStringList{QString()} : literal.split(QLatin1Char('\n'));
  const qreal lineWidth = qMax<qreal>(1.0, width);
  qreal y = 0.0;
  qsizetype globalStart = 0;
  QRectF fallback(origin.x(), origin.y(), 1.0, fallbackHeight);

  for (const QString& sourceLine : physicalLines) {
    const QString lineText = sourceLine.isEmpty() ? QStringLiteral(" ") : sourceLine;
    QTextLayout layout(lineText, font);
    layout.setTextOption(option);
    layout.beginLayout();
    bool producedLine = false;
    while (true) {
      QTextLine textLine = layout.createLine();
      if (!textLine.isValid()) {
        break;
      }
      textLine.setLineWidth(lineWidth);
      const qreal height = qMax<qreal>(fallbackHeight, textLine.height());
      textLine.setPosition(QPointF(0.0, y + (height - textLine.height()) * 0.5));
      producedLine = true;
      const qsizetype visualStart = globalStart + textLine.textStart();
      const qsizetype visualLength = qMin<qsizetype>(textLine.textLength(), sourceLine.size() - textLine.textStart());
      const qsizetype visualEnd = visualStart + visualLength;
      fallback = QRectF(origin.x() + metrics.horizontalAdvance(sourceLine), origin.y() + y, 1.0, height);
      if (offset >= visualStart && offset <= visualEnd) {
        const qsizetype localOffset = qBound<qsizetype>(visualStart, offset, visualEnd);
        const qreal x = metrics.horizontalAdvance(literal.mid(visualStart, localOffset - visualStart));
        layout.endLayout();
        return QRectF(origin.x() + x, origin.y() + y, 1.0, height);
      }
      y += height;
    }
    layout.endLayout();
    if (!producedLine) {
      if (offset == globalStart) {
        return QRectF(origin.x(), origin.y() + y, 1.0, fallbackHeight);
      }
      y += fallbackHeight;
    }
    globalStart += sourceLine.size() + 1;
  }
  return fallback;
}

bool isSelectableZone(HitTestResult::Zone zone) {
  return zone == HitTestResult::Zone::Text || zone == HitTestResult::Zone::Code || zone == HitTestResult::Zone::Math ||
         zone == HitTestResult::Zone::Html || zone == HitTestResult::Zone::FrontMatter || zone == HitTestResult::Zone::TableCell;
}

QPointF tableCellTextOrigin(const BlockLayout::TableCellLayout& cell, const RenderTheme& theme) {
  const QRectF contentRect = cell.rect.marginsRemoved(theme.tableCellPadding());
  qreal textX = contentRect.left();
  if (cell.alignment == TableAlignment::Right) {
    textX = contentRect.right() - cell.text.size().width();
  } else if (cell.alignment == TableAlignment::Center) {
    textX = contentRect.left() + (contentRect.width() - cell.text.size().width()) / 2.0;
  }
  return QPointF(qMax(contentRect.left(), textX), contentRect.top());
}

qsizetype selectableLength(const BlockLayout* block) {
  if (!block) {
    return 0;
  }
  if (const InlineLayout* inlineLayout = block->inlineLayout()) {
    return inlineLayout->plainText().size();
  }
  switch (block->type()) {
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::MathBlock:
    case BlockType::HtmlBlock:
      return block->literal().size();
    case BlockType::Table:
      return 1;
    default:
      return 0;
  }
}

QRect viewportUpdateRect(QRectF documentRect, qreal scrollY, const QSize& viewportSize) {
  if (documentRect.isNull() || documentRect.isEmpty()) {
    return {};
  }
  documentRect.translate(0, -scrollY);
  return documentRect.adjusted(-4, -4, 4, 4).toAlignedRect().intersected(QRect(QPoint(0, 0), viewportSize));
}

template <typename RebuildResult>
void addRebuildDirtyRect(QRect& dirty, const RebuildResult& result, QRectF documentViewport, qreal scrollY, const QSize& viewportSize) {
  dirty = dirty.united(viewportUpdateRect(result.oldRect.united(result.newRect), scrollY, viewportSize));
  if (!result.shiftedRect.isEmpty()) {
    dirty = dirty.united(viewportUpdateRect(result.shiftedRect.intersected(documentViewport), scrollY, viewportSize));
  }
}

}  // namespace

EditorView::EditorView(QWidget* parent) : QAbstractScrollArea(parent), layout_(std::make_unique<DocumentLayout>()) {
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  viewport()->setMouseTracking(true);
  viewport()->setAutoFillBackground(false);
  setBackgroundRole(QPalette::Base);
  applyScrollBarStyle();
  connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
    updateCodeLanguageEditor();
    updateTableToolbar();
  });
  setCodeLanguageSuggestions({
      QStringLiteral("bash"),
      QStringLiteral("c"),
      QStringLiteral("cpp"),
      QStringLiteral("csharp"),
      QStringLiteral("css"),
      QStringLiteral("go"),
      QStringLiteral("html"),
      QStringLiteral("ini"),
      QStringLiteral("java"),
      QStringLiteral("javascript"),
      QStringLiteral("json"),
      QStringLiteral("kotlin"),
      QStringLiteral("lua"),
      QStringLiteral("markdown"),
      QStringLiteral("mermaid"),
      QStringLiteral("objective-c"),
      QStringLiteral("pascal"),
      QStringLiteral("pegjs"),
      QStringLiteral("perl"),
      QStringLiteral("perl6"),
      QStringLiteral("pgp"),
      QStringLiteral("php"),
      QStringLiteral("powershell"),
      QStringLiteral("python"),
      QStringLiteral("qml"),
      QStringLiteral("r"),
      QStringLiteral("ruby"),
      QStringLiteral("rust"),
      QStringLiteral("sql"),
      QStringLiteral("swift"),
      QStringLiteral("text"),
      QStringLiteral("toml"),
      QStringLiteral("typescript"),
      QStringLiteral("xml"),
      QStringLiteral("yaml"),
  });
}

void EditorView::setDocument(const MarkdownDocument& document) {
  document_ = &document;
  rebuildLayout();
  updateTableToolbar();
}

bool EditorView::refreshBlock(NodeId blockId, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshBlock");
  if (!layout_ || document_ != &document) {
    return false;
  }

  const DocumentLayout::BlockRebuildResult result = layout_->rebuildBlock(blockId, document, theme_, selection_);
  if (!result.rebuilt) {
    return false;
  }
  if (!qFuzzyIsNull(result.heightDelta)) {
    updateScrollBars();
  }
  updateCursorHitFromPosition();
  updateTableToolbar();
  QRect dirty;
  addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  if (dirty.isEmpty()) {
    dirty = viewport()->rect();
  }
  viewport()->update(dirty);
  return true;
}

bool EditorView::refreshBlocks(const QVector<NodeId>& blockIds, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshBlocks");
  if (!layout_ || document_ != &document) {
    return false;
  }

  QRect dirty;
  bool scrollbarDirty = false;
  for (NodeId blockId : blockIds) {
    const DocumentLayout::BlockRebuildResult result = layout_->rebuildBlock(blockId, document, theme_, selection_);
    if (!result.rebuilt) {
      return false;
    }
    scrollbarDirty = scrollbarDirty || !qFuzzyIsNull(result.heightDelta);
    addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  }
  if (scrollbarDirty) {
    updateScrollBars();
  }
  updateCursorHitFromPosition();
  updateTableToolbar();
  viewport()->update(dirty.isEmpty() ? viewport()->rect() : dirty);
  return true;
}

bool EditorView::refreshTopLevelRange(TopLevelRangeChange range, const MarkdownDocument& document) {
  PerfTimer perf("view.refreshTopLevelRange");
  if (!layout_ || document_ != &document) {
    return false;
  }

  const DocumentLayout::RangeRebuildResult result = layout_->rebuildTopLevelRange(range, document, theme_, selection_);
  if (!result.rebuilt) {
    return false;
  }
  if (!qFuzzyIsNull(result.heightDelta)) {
    updateScrollBars();
  }
  updateCursorHitFromPosition();
  updateCodeLanguageEditor();
  updateTableToolbar();
  QRect dirty;
  addRebuildDirtyRect(dirty, result, documentViewportRect(), scrollY(), viewport()->size());
  viewport()->update(dirty.isEmpty() ? viewport()->rect() : dirty);
  return true;
}

void EditorView::setZoomPercent(int percent) {
  theme_.setZoomPercent(percent);
  applyScrollBarStyle();
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  rebuildLayout();
}

void EditorView::setTheme(RenderTheme theme) {
  const int zoom = theme_.zoomPercent();
  theme_ = std::move(theme);
  theme_.setZoomPercent(zoom);
  applyScrollBarStyle();
  viewport()->setPalette(QPalette(theme_.backgroundColor()));
  rebuildLayout();
}

void EditorView::setCursorHit(HitTestResult hit) {
  dragSelectionPending_ = false;
  draggingSelection_ = false;
  const SelectionRange previousSelection = selection_;
  cursorHit_ = hit;
  cursorPosition_ = hit.cursorPosition();
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  refreshInlineProjectionForSelectionChange(previousSelection);
  updateTableToolbar();
}

void EditorView::setCursorPosition(CursorPosition position) {
  dragSelectionPending_ = false;
  draggingSelection_ = false;
  const SelectionRange previousSelection = selection_;
  cursorPosition_ = position;
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  refreshInlineProjectionForSelectionChange(previousSelection);
  updateTableToolbar();
}

void EditorView::setSelectionRange(SelectionRange selection) {
  if ((dragSelectionPending_ || draggingSelection_) && sameSelectionRange(selection_, selection)) {
    return;
  }
  dragSelectionPending_ = false;
  draggingSelection_ = false;
  applySelectionRange(selection);
}

void EditorView::applySelectionRange(SelectionRange selection) {
  const SelectionRange previousSelection = selection_;
  selection_ = selection;
  cursorPosition_ = selection.focus;
  refreshInlineProjectionForSelectionChange(previousSelection);
  updateTableToolbar();
}

void EditorView::clearCursor() {
  cursorPosition_ = {};
  selection_ = {};
  cursorHit_ = {};
  cursorVisible_ = false;
  dragSelectionPending_ = false;
  draggingSelection_ = false;
  updateCodeLanguageEditor();
  updateTableToolbar();
  viewport()->update();
}

void EditorView::setCodeLanguageSuggestions(QStringList languages) {
  languages.removeDuplicates();
  languages.sort(Qt::CaseInsensitive);
  codeLanguageSuggestions_ = std::move(languages);
  if (codeLanguageCompleter_) {
    codeLanguageCompleter_->setModel(new QStringListModel(codeLanguageSuggestions_, codeLanguageCompleter_));
  }
}

QRectF EditorView::nodeRect(NodeId id) const {
  if (!layout_) {
    return {};
  }
  const BlockLayout* block = layout_->block(id);
  return block ? block->rect() : QRectF();
}

void EditorView::scrollToNode(NodeId id) {
  const QRectF rect = nodeRect(id);
  if (rect.isNull() || rect.isEmpty()) {
    return;
  }
  QScrollBar* scrollBar = verticalScrollBar();
  const int target = qBound(scrollBar->minimum(), qRound(rect.top() - 24.0), scrollBar->maximum());
  scrollBar->setValue(target);
}

const BlockLayout* EditorView::blockAtViewportPos(QPointF viewportPos) const {
  if (!layout_) {
    return nullptr;
  }
  return layout_->blockAt(QPointF(viewportPos.x(), viewportPos.y() + scrollY()));
}

HitTestResult EditorView::hitTest(QPointF viewportPos) const {
  if (!layout_) {
    return {};
  }
  return layout_->hitTest(QPointF(viewportPos.x(), viewportPos.y() + scrollY()), theme_);
}

void EditorView::paintEvent(QPaintEvent* event) {
  PerfTimer perf("view.paint");
  Q_UNUSED(event);

  QPainter painter(viewport());
  painter.fillRect(viewport()->rect(), theme_.backgroundColor());

  if (!layout_) {
    return;
  }

  const QRectF visible = documentViewportRect();
  const QVector<const BlockLayout*> blocks = layout_->visibleBlocks(visible.adjusted(0, -80, 0, 80));
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  for (const BlockLayout* block : blocks) {
    block->paint(painter, theme_, scrollY());
  }
  paintSelection(painter);
  paintCurrentTableCell(painter);
  paintInsertionCursor(painter);
}

bool EditorView::event(QEvent* event) {
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_A && keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
      if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
      }
      return QAbstractScrollArea::event(event);
    }
    if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
      event->accept();
      if (event->type() == QEvent::ShortcutOverride) {
        return true;
      }
      return QAbstractScrollArea::event(event);
    }
  }
  return QAbstractScrollArea::event(event);
}

void EditorView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  bool relayouted = false;
  if (layout_ && document_) {
    const int oldValue = verticalScrollBar()->value();
    relayouted = layout_->relayoutForViewportWidth(theme_, viewport()->width());
    if (relayouted) {
      updateScrollBars();
      verticalScrollBar()->setValue(qMin(oldValue, verticalScrollBar()->maximum()));
      updateCursorHitFromPosition();
      viewport()->update();
    }
  }
  if (!relayouted) {
    rebuildLayout();
  }
  updateCodeLanguageEditor();
  updateTableToolbar();
}

void EditorView::wheelEvent(QWheelEvent* event) {
  QAbstractScrollArea::wheelEvent(event);
  updateCodeLanguageEditor();
  updateTableToolbar();
}

void EditorView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    setFocus(Qt::MouseFocusReason);
    const HitTestResult hit = hitTest(event->position());
    if (event->modifiers().testFlag(Qt::ShiftModifier) && hit.isValid() && isSelectableZone(hit.zone) && cursorPosition_.isValid()) {
      SelectionRange range;
      range.anchor = selection_.anchor.isValid() ? selection_.anchor : cursorPosition_;
      range.focus = hit.cursorPosition();
      setSelectionRange(range);
      emit selectionChanged(range, hit);
      event->accept();
      return;
    }
    setCursorHit(hit);
    emit blockClicked(hit);
    updateCodeLanguageEditor();
    if (hit.isValid() && isSelectableZone(hit.zone)) {
      dragSelectionPending_ = true;
      draggingSelection_ = false;
      dragStartViewportPos_ = event->position();
      dragAnchorHit_ = hit;
    }
  }
  QAbstractScrollArea::mousePressEvent(event);
}

void EditorView::mouseMoveEvent(QMouseEvent* event) {
  if ((dragSelectionPending_ || draggingSelection_) && (event->buttons() & Qt::LeftButton)) {
    if (!draggingSelection_) {
      draggingSelection_ = true;
      dragSelectionPending_ = false;
    }
    if (draggingSelection_) {
      updateDragSelection(event->position());
      event->accept();
      return;
    }
  }
  updateMouseCursor(event->position());
  QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && (dragSelectionPending_ || draggingSelection_)) {
    if (draggingSelection_) {
      updateDragSelection(event->position());
    }
    dragSelectionPending_ = false;
    draggingSelection_ = false;
    event->accept();
    return;
  }
  QAbstractScrollArea::mouseReleaseEvent(event);
}

void EditorView::inputMethodEvent(QInputMethodEvent* event) {
  if (!event->commitString().isEmpty()) {
    emit textCommitted(event->commitString());
  }
  event->accept();
}

QVariant EditorView::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle) {
    QRectF cursor = cursorHit_.cursorRect;
    if (cursor.isEmpty()) {
      cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
    }
    cursor.translate(0, -scrollY());
    return cursor.toRect();
  }
  return QAbstractScrollArea::inputMethodQuery(query);
}

void EditorView::rebuildLayout() {
  PerfTimer perf("view.rebuildLayout");
  if (!layout_) {
    layout_ = std::make_unique<DocumentLayout>();
  }

  if (document_) {
    const int oldValue = verticalScrollBar()->value();
    layout_->rebuild(*document_, theme_, viewport()->width(), selection_);
    updateScrollBars();
    verticalScrollBar()->setValue(qMin(oldValue, verticalScrollBar()->maximum()));
    if (cursorPosition_.isValid()) {
      cursorHit_ = hitForCursorPosition(cursorPosition_);
      cursorVisible_ = cursorHit_.isValid();
    }
  } else {
    layout_ = std::make_unique<DocumentLayout>();
    updateScrollBars();
  }
  updateCodeLanguageEditor();
  updateTableToolbar();
  viewport()->update();
}

void EditorView::updateScrollBars() {
  const int pageStep = qMax(1, viewport()->height());
  const int maximum = layout_ ? qMax(0, static_cast<int>(std::ceil(layout_->totalHeight() - viewport()->height()))) : 0;
  verticalScrollBar()->setPageStep(pageStep);
  verticalScrollBar()->setSingleStep(qMax(16, pageStep / 12));
  verticalScrollBar()->setRange(0, maximum);
}

QRectF EditorView::documentViewportRect() const {
  return QRectF(0, scrollY(), viewport()->width(), viewport()->height());
}

qreal EditorView::scrollY() const {
  return static_cast<qreal>(verticalScrollBar()->value());
}

void EditorView::applyScrollBarStyle() {
  const QString background = theme_.backgroundColor().name(QColor::HexRgb);
  setStyleSheet(QStringLiteral(
      "EditorView { background:%1; border:0; }"
      "QScrollBar:vertical { background:%1; width:8px; margin:0; }"
      "QScrollBar::handle:vertical { background:#b7b7b7; min-height:54px; border-radius:3px; margin:1px 2px; }"
      "QScrollBar::handle:vertical:hover { background:#999999; }"
      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; border:0; background:transparent; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
      "QScrollBar:horizontal { background:%1; height:8px; margin:0; }"
      "QScrollBar::handle:horizontal { background:#b7b7b7; min-width:54px; border-radius:3px; margin:2px 1px; }"
      "QScrollBar::handle:horizontal:hover { background:#999999; }"
      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; border:0; background:transparent; }"
      "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }")
                    .arg(background));
}

void EditorView::ensureCodeLanguageEditor() {
  if (codeLanguageEditor_) {
    return;
  }

  codeLanguageEditor_ = new QLineEdit(viewport());
  codeLanguageEditor_->setObjectName(QStringLiteral("codeLanguageEditor"));
  codeLanguageEditor_->setPlaceholderText(QStringLiteral("text"));
  codeLanguageEditor_->setClearButtonEnabled(false);
  codeLanguageEditor_->setFrame(false);
  codeLanguageEditor_->setFixedWidth(116);
  codeLanguageEditor_->hide();
  codeLanguageEditor_->setStyleSheet(QStringLiteral(
      "QLineEdit#codeLanguageEditor {"
      "  background:rgba(255,255,255,235);"
      "  color:#222222;"
      "  border:1px solid #d7dce2;"
      "  border-radius:4px;"
      "  padding:2px 8px;"
      "  selection-background-color:#2f80ed;"
      "  selection-color:#ffffff;"
      "  font-size:12px;"
      "}"));
  auto* shadow = new QGraphicsDropShadowEffect(codeLanguageEditor_);
  shadow->setBlurRadius(14.0);
  shadow->setOffset(0.0, 3.0);
  shadow->setColor(QColor(15, 23, 42, 35));
  codeLanguageEditor_->setGraphicsEffect(shadow);

  codeLanguageCompleter_ = new QCompleter(codeLanguageSuggestions_, codeLanguageEditor_);
  codeLanguageCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
  codeLanguageCompleter_->setFilterMode(Qt::MatchContains);
  codeLanguageCompleter_->setCompletionMode(QCompleter::PopupCompletion);
  auto* popup = new QListView(codeLanguageEditor_);
  popup->setObjectName(QStringLiteral("codeLanguagePopup"));
  popup->setUniformItemSizes(true);
  popup->setAlternatingRowColors(false);
  popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  popup->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  popup->setStyleSheet(QStringLiteral(
      "QListView#codeLanguagePopup {"
      "  background:#ffffff;"
      "  color:#333333;"
      "  border:1px solid #e1e4e8;"
      "  border-radius:5px;"
      "  padding:6px 0;"
      "  outline:0;"
      "  font-size:13px;"
      "}"
      "QListView#codeLanguagePopup::item {"
      "  min-height:28px;"
      "  padding:4px 12px;"
      "}"
      "QListView#codeLanguagePopup::item:selected {"
      "  background:#f1f6ff;"
      "  color:#111111;"
      "}"));
  codeLanguageCompleter_->setPopup(popup);
  codeLanguageEditor_->setCompleter(codeLanguageCompleter_);

  connect(codeLanguageEditor_, &QLineEdit::editingFinished, this, &EditorView::commitCodeLanguageEditor);
  connect(codeLanguageEditor_, &QLineEdit::returnPressed, this, &EditorView::commitCodeLanguageEditor);
  connect(codeLanguageEditor_, &QLineEdit::textEdited, this, [this] {
    if (codeLanguageCompleter_) {
      QRect rect = codeLanguageEditor_->rect();
      rect.setWidth(140);
      codeLanguageCompleter_->complete(rect);
    }
  });
}

void EditorView::updateCodeLanguageEditor() {
  if (updatingCodeLanguageEditor_) {
    return;
  }

  if (!layout_ || !document_ || !cursorPosition_.isValid() || cursorPosition_.blockId != cursorHit_.blockId ||
      cursorHit_.zone != HitTestResult::Zone::Code) {
    hideCodeLanguageEditor();
    return;
  }

  const BlockLayout* block = layout_->block(cursorPosition_.blockId);
  if (!block || block->type() != BlockType::CodeFence) {
    hideCodeLanguageEditor();
    return;
  }
  showCodeLanguageEditor(*block);
}

void EditorView::showCodeLanguageEditor(const BlockLayout& block) {
  ensureCodeLanguageEditor();
  if (!codeLanguageEditor_ || !document_) {
    return;
  }

  const MarkdownNode* node = document_->node(block.nodeId());
  if (!node || node->type() != BlockType::CodeFence) {
    hideCodeLanguageEditor();
    return;
  }

  updatingCodeLanguageEditor_ = true;
  codeLanguageNodeId_ = block.nodeId();
  if (!codeLanguageEditor_->hasFocus()) {
    const QString language = node->codeLanguage().isEmpty() ? QStringLiteral("text") : node->codeLanguage();
    if (codeLanguageEditor_->text() != language) {
      codeLanguageEditor_->setText(language);
    }
    codeLanguageEditor_->deselect();
    codeLanguageEditor_->setCursorPosition(codeLanguageEditor_->text().size());
  }

  const QRectF blockRect = block.rect().translated(0, -scrollY());
  codeLanguageEditor_->resize(116, 26);
  const int margin = 10;
  const int x = qRound(blockRect.right()) - codeLanguageEditor_->width() - margin;
  int y = qRound(blockRect.bottom()) - codeLanguageEditor_->height() / 2;
  if (y + codeLanguageEditor_->height() + margin > viewport()->height()) {
    y = qRound(blockRect.bottom()) - codeLanguageEditor_->height() - margin;
  }
  codeLanguageEditor_->move(qMax(0, x), qMax(0, y));
  codeLanguageEditor_->show();
  codeLanguageEditor_->raise();
  updatingCodeLanguageEditor_ = false;
}

void EditorView::hideCodeLanguageEditor() {
  codeLanguageNodeId_ = {};
  if (codeLanguageEditor_) {
    codeLanguageEditor_->hide();
  }
}

void EditorView::commitCodeLanguageEditor() {
  if (updatingCodeLanguageEditor_ || !codeLanguageEditor_ || !codeLanguageEditor_->isVisible() || !codeLanguageNodeId_.isValid()) {
    return;
  }
  QString language = codeLanguageEditor_->text().trimmed();
  if (language == QStringLiteral("text")) {
    language.clear();
  }
  if (document_) {
    const MarkdownNode* node = document_->node(codeLanguageNodeId_);
    if (node && node->codeLanguage() == language) {
      return;
    }
  }
  emit codeLanguageCommitted(codeLanguageNodeId_, language);
}

void EditorView::ensureTableToolbar() {
  if (tableToolbar_) {
    return;
  }

  tableToolbar_ = new QWidget(viewport());
  tableToolbar_->setObjectName(QStringLiteral("tableFloatingToolbar"));
  tableToolbar_->setFocusPolicy(Qt::NoFocus);
  tableToolbar_->hide();
  auto* layout = new QHBoxLayout(tableToolbar_);
  layout->setContentsMargins(4, 3, 4, 3);
  layout->setSpacing(1);

  auto makeButton = [this, layout](TableToolbarIconKind iconKind, const QString& tooltip) {
    auto* button = new QToolButton(tableToolbar_);
    button->setIcon(tableToolbarIcon(iconKind));
    button->setIconSize(QSize(16, 16));
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(24, 22);
    layout->addWidget(button);
    return button;
  };

  tableResizeButton_ = makeButton(TableToolbarIconKind::Resize, QStringLiteral("调整表格大小"));
  tableAlignLeftButton_ = makeButton(TableToolbarIconKind::AlignLeft, QStringLiteral("左对齐"));
  tableAlignCenterButton_ = makeButton(TableToolbarIconKind::AlignCenter, QStringLiteral("居中对齐"));
  tableAlignRightButton_ = makeButton(TableToolbarIconKind::AlignRight, QStringLiteral("右对齐"));
  tableMoreButton_ = makeButton(TableToolbarIconKind::More, QStringLiteral("更多操作"));
  tableDeleteButton_ = makeButton(TableToolbarIconKind::Delete, QStringLiteral("删除表格"));

  tableToolbar_->setStyleSheet(QStringLiteral(
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
  auto* shadow = new QGraphicsDropShadowEffect(tableToolbar_);
  shadow->setBlurRadius(12.0);
  shadow->setOffset(0.0, 2.0);
  shadow->setColor(QColor(15, 23, 42, 28));
  tableToolbar_->setGraphicsEffect(shadow);

  connect(tableResizeButton_, &QToolButton::clicked, this, &EditorView::showTableResizePopup);
  connect(tableAlignLeftButton_, &QToolButton::clicked, this, [this] { emit tableColumnAlignmentRequested(TableAlignment::Left); });
  connect(tableAlignCenterButton_, &QToolButton::clicked, this, [this] { emit tableColumnAlignmentRequested(TableAlignment::Center); });
  connect(tableAlignRightButton_, &QToolButton::clicked, this, [this] { emit tableColumnAlignmentRequested(TableAlignment::Right); });
  connect(tableMoreButton_, &QToolButton::clicked, this, [this] {
    emit tableMoreActionsRequested(tableMoreButton_->mapToGlobal(QPoint(0, tableMoreButton_->height())));
  });
  connect(tableDeleteButton_, &QToolButton::clicked, this, [this] { emit tableDeleteRequested(); });
}

void EditorView::updateTableToolbar() {
  if (updatingTableToolbar_) {
    return;
  }
  const BlockLayout* table = activeTableLayout();
  if (!table) {
    hideTableToolbar();
    return;
  }
  showTableToolbar(*table);
}

void EditorView::showTableToolbar(const BlockLayout& table) {
  ensureTableToolbar();
  if (!tableToolbar_) {
    return;
  }

  QRectF tableRect = table.rect().translated(0, -scrollY());
  if (!tableRect.intersects(QRectF(viewport()->rect()).adjusted(0, -40, 0, 40))) {
    hideTableToolbar();
    return;
  }

  updatingTableToolbar_ = true;
  tableToolbar_->adjustSize();
  int x = qRound(tableRect.left());
  int y = qRound(tableRect.top()) - tableToolbar_->height() - 6;
  if (y < 4) {
    y = qRound(tableRect.top()) + 4;
  }
  x = qBound(4, x, qMax(4, viewport()->width() - tableToolbar_->width() - 4));
  y = qBound(4, y, qMax(4, viewport()->height() - tableToolbar_->height() - 4));
  tableToolbar_->move(x, y);
  tableToolbar_->show();
  tableToolbar_->raise();
  updatingTableToolbar_ = false;
}

void EditorView::hideTableToolbar() {
  if (tableToolbar_) {
    tableToolbar_->hide();
  }
  if (tableResizePopup_) {
    tableResizePopup_->hide();
  }
}

void EditorView::showTableResizePopup() {
  ensureTableToolbar();
  if (!tableResizeButton_) {
    return;
  }

  if (!tableResizePopup_) {
    auto* popup = new TableResizePopup(this);
    popup->setResizeCallback([this](int rows, int columns) { emit tableResizeRequested(rows, columns); });
    tableResizePopup_ = popup;
  }

  auto* popup = static_cast<TableResizePopup*>(tableResizePopup_);
  const auto [rows, columns] = activeTableSize();
  popup->setCurrentSize(rows, columns);
  popup->move(tableResizeButton_->mapToGlobal(QPoint(0, tableResizeButton_->height() + 3)));
  popup->show();
  popup->raise();
}

QPair<int, int> EditorView::activeTableSize() const {
  const BlockLayout* table = activeTableLayout();
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

const BlockLayout* EditorView::activeTableLayout() const {
  if (!layout_ || !document_ || cursorHit_.zone != HitTestResult::Zone::TableCell || cursorHit_.tableRow < 0 || cursorHit_.tableColumn < 0) {
    return nullptr;
  }
  const BlockLayout* table = layout_->block(cursorHit_.blockId);
  if (!table || table->type() != BlockType::Table || cursorHit_.tableRow >= static_cast<int>(table->tableRows().size())) {
    return nullptr;
  }
  const auto& row = table->tableRows().at(static_cast<size_t>(cursorHit_.tableRow));
  if (cursorHit_.tableColumn >= static_cast<int>(row.cells.size())) {
    return nullptr;
  }
  return table;
}

void EditorView::updateCursorHitFromPosition() {
  cursorHit_ = hitForCursorPosition(cursorPosition_);
  cursorVisible_ = cursorHit_.isValid();
  updateCodeLanguageEditor();
  updateTableToolbar();
}

void EditorView::refreshInlineProjectionForSelectionChange(SelectionRange previousSelection) {
  QVector<NodeId> blockIds;
  addSelectionBlocks(blockIds, previousSelection);
  addSelectionBlocks(blockIds, selection_);

  bool refreshed = false;
  if (document_ && layout_ && !blockIds.isEmpty()) {
    refreshed = refreshBlocks(blockIds, *document_);
  }
  if (!refreshed) {
    updateCursorHitFromPosition();
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
    viewport()->update();
  } else {
    cursorVisible_ = selection_.isCollapsed() && cursorHit_.isValid();
  }
}

void EditorView::addSelectionBlocks(QVector<NodeId>& blockIds, const SelectionRange& selection) const {
  auto add = [&blockIds](NodeId id) {
    if (id.isValid() && !blockIds.contains(id)) {
      blockIds.push_back(id);
    }
  };
  add(selection.anchor.blockId);
  add(selection.focus.blockId);
}

void EditorView::paintSelection(QPainter& painter) const {
  if (!layout_ || selection_.isCollapsed()) {
    return;
  }

  const QColor color(79, 143, 247, 72);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);

  if (selection_.isSingleBlock()) {
    const BlockLayout* block = layout_->block(selection_.focus.blockId);
    if (block) {
      for (QRectF rect : block->selectionRects(selection_, theme_)) {
        rect.translate(0, -scrollY());
        painter.drawRoundedRect(rect, 2, 2);
      }
    }
  } else {
    const bool anchorFirst = blockComesBefore(selection_.anchor.blockId, selection_.focus.blockId);
    const QVector<const BlockLayout*> selectedBlocks = blocksBetween(selection_.anchor.blockId, selection_.focus.blockId);
    for (qsizetype i = 0; i < selectedBlocks.size(); ++i) {
      const BlockLayout* block = selectedBlocks.at(i);
      if (!block) {
        continue;
      }
      const bool isAnchor = block->nodeId() == selection_.anchor.blockId;
      const bool isFocus = block->nodeId() == selection_.focus.blockId;
      qsizetype start = 0;
      qsizetype end = selectableLength(block);
      if (isAnchor) {
        if (anchorFirst) {
          start = block->type() == BlockType::Table ? 0 : selection_.anchor.text.textOffset;
        } else {
          end = block->type() == BlockType::Table ? selectableLength(block) : selection_.anchor.text.textOffset;
        }
      }
      if (isFocus) {
        if (anchorFirst) {
          end = block->type() == BlockType::Table ? selectableLength(block) : selection_.focus.text.textOffset;
        } else {
          start = block->type() == BlockType::Table ? 0 : selection_.focus.text.textOffset;
        }
      }
      for (QRectF rect : block->selectionRectsForOffsets(start, end, theme_)) {
        rect.translate(0, -scrollY());
        painter.drawRoundedRect(rect, 2, 2);
      }
    }
  }
  painter.restore();
}

void EditorView::paintCurrentTableCell(QPainter& painter) const {
  if (!layout_ || cursorHit_.zone != HitTestResult::Zone::TableCell || cursorHit_.tableRow < 0 || cursorHit_.tableColumn < 0) {
    return;
  }

  const BlockLayout* table = layout_->block(cursorHit_.blockId);
  if (!table || table->type() != BlockType::Table) {
    return;
  }

  QRectF rect = table->tableCellRect(cursorHit_.tableRow, cursorHit_.tableColumn);
  if (rect.isEmpty()) {
    return;
  }
  rect.translate(0, -scrollY());

  painter.save();
  painter.setPen(QPen(theme_.linkColor(), 1.4));
  painter.setBrush(QColor(79, 143, 247, 28));
  painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
  painter.restore();
}

void EditorView::paintInsertionCursor(QPainter& painter) const {
  if (!cursorVisible_ || !cursorHit_.isValid()) {
    return;
  }

  QRectF cursor = cursorHit_.cursorRect;
  if (cursor.isEmpty()) {
    cursor = QRectF(cursorHit_.blockRect.left(), cursorHit_.blockRect.top(), 1.0, cursorHit_.blockRect.height());
  }
  cursor.translate(0, -scrollY());

  if (!viewport()->rect().adjusted(-4, -4, 4, 4).intersects(cursor.toAlignedRect())) {
    return;
  }

  const qreal height = qBound<qreal>(14.0, cursor.height(), 34.0);
  QRectF visibleCursor(cursor.left(), cursor.top(), 1.5, height);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(theme_.linkColor());
  painter.drawRect(visibleCursor);
  painter.restore();
}

HitTestResult EditorView::hitForCursorPosition(CursorPosition position) const {
  if (!layout_ || !position.isValid()) {
    return {};
  }

  const BlockLayout* block = layout_->block(position.blockId);
  if (!block) {
    return {};
  }

  HitTestResult hit;
  hit.blockId = position.blockId;
  hit.textNodeId = position.text.nodeId.isValid() ? position.text.nodeId : position.blockId;
  hit.textOffset = position.text.textOffset;
  hit.sourceOffset = position.text.sourceOffset;
  hit.blockRect = block->rect();
  hit.zone = HitTestResult::Zone::Block;

  switch (block->type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::ListItem:
      hit.zone = position.text.inMeta ? HitTestResult::Zone::Marker : HitTestResult::Zone::Text;
      if (const InlineLayout* inlineLayout = block->inlineLayout()) {
        const qreal textLeft = block->hasListMarker() ? block->rect().left() + theme_.listIndent() : block->rect().left();
        const qsizetype localSourceOffset =
            position.text.sourceOffset >= 0 && block->contentSourceStart() >= 0 ? position.text.sourceOffset - block->contentSourceStart() : -1;
        hit.cursorRect = localSourceOffset >= 0
                             ? inlineLayout->cursorRectForSourceOffset(localSourceOffset).translated(QPointF(textLeft, block->rect().top()))
                             : inlineLayout->cursorRect(position.text.textOffset).translated(QPointF(textLeft, block->rect().top()));
      }
      break;
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
      hit.zone = block->type() == BlockType::FrontMatter ? HitTestResult::Zone::FrontMatter : HitTestResult::Zone::Code;
      {
        const QRectF contentRect = block->literalContentRect(theme_);
        hit.cursorRect =
            literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(), contentRect.topLeft(), contentRect.width(), theme_.codeLineHeight());
      }
      break;
    case BlockType::MathBlock:
      hit.zone = HitTestResult::Zone::Math;
      if (block->literalEditing()) {
        const QRectF contentRect = block->literalContentRect(theme_);
        hit.cursorRect =
            literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(), contentRect.topLeft(), contentRect.width(), theme_.codeLineHeight());
      } else {
        const qsizetype offset = qBound<qsizetype>(0, position.text.textOffset, block->literal().size());
        const qreal x = offset <= block->literal().size() / 2 ? block->rect().left() : block->rect().right();
        hit.cursorRect = QRectF(x, block->rect().top(), 1.0, block->rect().height());
      }
      break;
    case BlockType::HtmlBlock:
      hit.zone = HitTestResult::Zone::Html;
      {
        const QRectF contentRect = block->literalContentRect(theme_);
        hit.cursorRect =
            literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(), contentRect.topLeft(), contentRect.width(), theme_.codeLineHeight());
      }
      break;
    case BlockType::Table:
      hit.zone = HitTestResult::Zone::TableCell;
      hit.tableRow = -1;
      hit.tableColumn = -1;
      for (int row = 0; row < static_cast<int>(block->tableRows().size()); ++row) {
        const auto& tableRow = block->tableRows().at(static_cast<size_t>(row));
        for (int column = 0; column < static_cast<int>(tableRow.cells.size()); ++column) {
          if (tableRow.cells.at(static_cast<size_t>(column)).nodeId == hit.textNodeId) {
            hit.tableRow = row;
            hit.tableColumn = column;
            break;
          }
        }
        if (hit.tableRow >= 0) {
          break;
        }
      }
      if (hit.tableRow >= 0 && hit.tableColumn >= 0) {
        const QRectF cellRect = block->tableCellRect(hit.tableRow, hit.tableColumn);
        for (const auto& tableRow : block->tableRows()) {
          for (const auto& cell : tableRow.cells) {
            if (cell.nodeId == hit.textNodeId) {
              const QPointF textOrigin = tableCellTextOrigin(cell, theme_);
              const qsizetype localSourceOffset =
                  position.text.sourceOffset >= 0 && cell.contentSourceStart >= 0 ? position.text.sourceOffset - cell.contentSourceStart : -1;
              hit.cursorRect = localSourceOffset >= 0
                                   ? cell.text.cursorRectForSourceOffset(localSourceOffset).translated(textOrigin)
                                   : cell.text.cursorRect(position.text.textOffset).translated(textOrigin);
              break;
            }
          }
        }
        if (hit.cursorRect.isEmpty()) {
          hit.cursorRect = QRectF(cellRect.left() + 6.0, cellRect.top() + 4.0, 1.0, qMax<qreal>(14.0, cellRect.height() - 8.0));
        }
      }
      break;
    default:
      break;
  }

  if (hit.cursorRect.isEmpty()) {
    hit.cursorRect = QRectF(hit.blockRect.left(), hit.blockRect.top(), 1.0, hit.blockRect.height());
  }
  return hit;
}

QVector<const BlockLayout*> EditorView::blocksBetween(NodeId first, NodeId last) const {
  QVector<const BlockLayout*> result;
  if (!layout_ || !first.isValid() || !last.isValid()) {
    return result;
  }

  const BlockLayout* firstBlock = layout_->block(first);
  const BlockLayout* lastBlock = layout_->block(last);
  if (!firstBlock || !lastBlock) {
    return result;
  }

  NodeId startId = first;
  NodeId endId = last;
  if (firstBlock->rect().top() > lastBlock->rect().top() ||
      (qFuzzyCompare(firstBlock->rect().top(), lastBlock->rect().top()) && firstBlock->rect().left() > lastBlock->rect().left())) {
    qSwap(startId, endId);
  }

  bool collecting = false;
  const auto collect = [&](const auto& self, const BlockLayout& block) -> void {
    if (block.nodeId() == startId) {
      collecting = true;
    }
    if (collecting) {
      result.push_back(&block);
    }
    if (block.nodeId() == endId) {
      collecting = false;
      return;
    }
    for (const auto& child : block.children()) {
      self(self, *child);
      if (!collecting && !result.isEmpty() && result.last()->nodeId() == endId) {
        return;
      }
    }
  };

  for (const auto& block : layout_->blocks()) {
    collect(collect, *block);
    if (!result.isEmpty() && result.last()->nodeId() == endId) {
      break;
    }
  }
  return result;
}

bool EditorView::blockComesBefore(NodeId first, NodeId second) const {
  if (first == second) {
    return true;
  }

  const QVector<const BlockLayout*> range = blocksBetween(first, second);
  return !range.isEmpty() && range.first()->nodeId() == first;
}

void EditorView::updateDragSelection(QPointF viewportPos) {
  if (!dragAnchorHit_.isValid()) {
    return;
  }

  const HitTestResult focusHit = hitTest(viewportPos);
  if (!focusHit.isValid() || !isSelectableZone(focusHit.zone)) {
    return;
  }

  SelectionRange range;
  range.anchor = dragAnchorHit_.cursorPosition();
  range.focus = focusHit.cursorPosition();
  if (range.isCollapsed()) {
    return;
  }
  applySelectionRange(range);
  emit selectionChanged(range, focusHit);
}

void EditorView::updateMouseCursor(QPointF viewportPos) {
  const HitTestResult hit = hitTest(viewportPos);
  if (hit.isValid() &&
      (hit.zone == HitTestResult::Zone::Text || hit.zone == HitTestResult::Zone::Code || hit.zone == HitTestResult::Zone::Math ||
       hit.zone == HitTestResult::Zone::Html || hit.zone == HitTestResult::Zone::FrontMatter || hit.zone == HitTestResult::Zone::TableCell ||
       hit.zone == HitTestResult::Zone::BlockAfter)) {
    viewport()->setCursor(Qt::IBeamCursor);
  } else {
    viewport()->unsetCursor();
  }
}

}  // namespace muffin
