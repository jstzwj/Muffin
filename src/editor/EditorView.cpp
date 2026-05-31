#include "editor/EditorView.h"

#include "document/MarkdownDocument.h"

#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <QFontMetricsF>

#include <cmath>

namespace muffin {
namespace {

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

bool isSelectableZone(HitTestResult::Zone zone) {
  return zone == HitTestResult::Zone::Text || zone == HitTestResult::Zone::Code || zone == HitTestResult::Zone::Math ||
         zone == HitTestResult::Zone::Html || zone == HitTestResult::Zone::TableCell;
}

qsizetype selectableLength(const BlockLayout* block) {
  if (!block) {
    return 0;
  }
  if (const InlineLayout* inlineLayout = block->inlineLayout()) {
    return inlineLayout->plainText().size();
  }
  switch (block->type()) {
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
}

void EditorView::setDocument(const MarkdownDocument& document) {
  document_ = &document;
  rebuildLayout();
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
  cursorHit_ = hit;
  cursorPosition_ = hit.cursorPosition();
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  cursorVisible_ = hit.isValid();
  viewport()->update();
}

void EditorView::setCursorPosition(CursorPosition position) {
  cursorPosition_ = position;
  selection_.anchor = cursorPosition_;
  selection_.focus = cursorPosition_;
  cursorHit_ = hitForCursorPosition(position);
  cursorVisible_ = cursorHit_.isValid();
  viewport()->update();
}

void EditorView::setSelectionRange(SelectionRange selection) {
  selection_ = selection;
  cursorPosition_ = selection.focus;
  cursorHit_ = hitForCursorPosition(selection.focus);
  cursorVisible_ = selection.isCollapsed() && cursorHit_.isValid();
  viewport()->update();
}

void EditorView::clearCursor() {
  cursorPosition_ = {};
  selection_ = {};
  cursorHit_ = {};
  cursorVisible_ = false;
  viewport()->update();
}

QRectF EditorView::nodeRect(NodeId id) const {
  if (!layout_) {
    return {};
  }
  const BlockLayout* block = layout_->block(id);
  return block ? block->rect() : QRectF();
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

void EditorView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  rebuildLayout();
}

void EditorView::wheelEvent(QWheelEvent* event) {
  QAbstractScrollArea::wheelEvent(event);
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
    if (hit.isValid() && isSelectableZone(hit.zone)) {
      draggingSelection_ = true;
      dragAnchorHit_ = hit;
    }
  }
  QAbstractScrollArea::mousePressEvent(event);
}

void EditorView::mouseMoveEvent(QMouseEvent* event) {
  if (draggingSelection_ && (event->buttons() & Qt::LeftButton)) {
    updateDragSelection(event->position());
    event->accept();
    return;
  }
  updateMouseCursor(event->position());
  QAbstractScrollArea::mouseMoveEvent(event);
}

void EditorView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && draggingSelection_) {
    updateDragSelection(event->position());
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
  if (!layout_) {
    layout_ = std::make_unique<DocumentLayout>();
  }

  if (document_) {
    const int oldValue = verticalScrollBar()->value();
    layout_->rebuild(*document_, theme_, viewport()->width());
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
  hit.blockRect = block->rect();
  hit.zone = HitTestResult::Zone::Block;

  switch (block->type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::ListItem:
      hit.zone = position.text.inMeta ? HitTestResult::Zone::Marker : HitTestResult::Zone::Text;
      if (const InlineLayout* inlineLayout = block->inlineLayout()) {
        const qreal textLeft = !block->listMarker().isEmpty() ? block->rect().left() + theme_.listIndent() : block->rect().left();
        hit.cursorRect = inlineLayout->cursorRect(position.text.textOffset).translated(QPointF(textLeft, block->rect().top()));
      }
      break;
    case BlockType::CodeFence:
      hit.zone = HitTestResult::Zone::Code;
      hit.cursorRect = literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(),
                                                  block->rect().marginsRemoved(theme_.codePadding()).topLeft());
      break;
    case BlockType::MathBlock:
      hit.zone = HitTestResult::Zone::Math;
      hit.cursorRect = literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.mathFont(),
                                                  block->rect().marginsRemoved(theme_.codePadding()).topLeft());
      break;
    case BlockType::HtmlBlock:
      hit.zone = HitTestResult::Zone::Html;
      hit.cursorRect = literalCursorRectForOffset(block->literal(), position.text.textOffset, theme_.codeFont(),
                                                  block->rect().marginsRemoved(theme_.codePadding()).topLeft());
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
              const QRectF contentRect = cell.rect.marginsRemoved(theme_.tableCellPadding());
              hit.cursorRect = cell.text.cursorRect(position.text.textOffset).translated(contentRect.topLeft());
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
  setSelectionRange(range);
  emit selectionChanged(range, focusHit);
}

void EditorView::updateMouseCursor(QPointF viewportPos) {
  const HitTestResult hit = hitTest(viewportPos);
  if (hit.isValid() &&
      (hit.zone == HitTestResult::Zone::Text || hit.zone == HitTestResult::Zone::Code || hit.zone == HitTestResult::Zone::Math ||
       hit.zone == HitTestResult::Zone::Html)) {
    viewport()->setCursor(Qt::IBeamCursor);
  } else {
    viewport()->unsetCursor();
  }
}

}  // namespace muffin
