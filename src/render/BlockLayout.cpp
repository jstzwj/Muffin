#include "render/BlockLayout.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>

#include <cmath>

namespace muffin {
namespace {

qsizetype literalOffsetForPoint(const QString& literal, QPointF localPos, const QFont& font) {
  const QFontMetricsF metrics(font);
  const qreal lineHeight = qMax<qreal>(1.0, metrics.height());
  const int targetLine = qMax(0, static_cast<int>(std::floor(localPos.y() / lineHeight)));

  qsizetype line = 0;
  qsizetype lineStart = 0;
  while (line < targetLine && lineStart < literal.size()) {
    const qsizetype newline = literal.indexOf(QLatin1Char('\n'), lineStart);
    if (newline < 0) {
      lineStart = literal.size();
      break;
    }
    lineStart = newline + 1;
    ++line;
  }

  qsizetype lineEnd = literal.indexOf(QLatin1Char('\n'), lineStart);
  if (lineEnd < 0) {
    lineEnd = literal.size();
  }

  qsizetype offset = lineStart;
  qreal bestDistance = std::numeric_limits<qreal>::max();
  for (qsizetype candidate = lineStart; candidate <= lineEnd; ++candidate) {
    const qreal x = metrics.horizontalAdvance(literal.mid(lineStart, candidate - lineStart));
    const qreal distance = std::abs(localPos.x() - x);
    if (distance <= bestDistance) {
      bestDistance = distance;
      offset = candidate;
    }
  }
  return qBound<qsizetype>(0, offset, literal.size());
}

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

QVector<QRectF> literalSelectionRectsForRange(
    const QString& literal,
    qsizetype startOffset,
    qsizetype endOffset,
    const QFont& font,
    QPointF origin,
    qreal maxWidth) {
  QVector<QRectF> rects;
  startOffset = qBound<qsizetype>(0, startOffset, literal.size());
  endOffset = qBound<qsizetype>(0, endOffset, literal.size());
  if (startOffset > endOffset) {
    qSwap(startOffset, endOffset);
  }
  if (startOffset == endOffset) {
    return rects;
  }

  const QFontMetricsF metrics(font);
  const qreal lineHeight = qMax<qreal>(14.0, metrics.height());
  qsizetype lineStart = 0;
  int line = 0;
  while (lineStart <= literal.size()) {
    qsizetype lineEnd = literal.indexOf(QLatin1Char('\n'), lineStart);
    const bool hasNewline = lineEnd >= 0;
    if (!hasNewline) {
      lineEnd = literal.size();
    }

    const qsizetype rangeStart = qMax(startOffset, lineStart);
    const qsizetype rangeEnd = qMin(endOffset, lineEnd);
    if (rangeStart < rangeEnd) {
      const qreal x1 = metrics.horizontalAdvance(literal.mid(lineStart, rangeStart - lineStart));
      const qreal x2 = metrics.horizontalAdvance(literal.mid(lineStart, rangeEnd - lineStart));
      rects.push_back(QRectF(origin.x() + x1, origin.y() + line * lineHeight, qMax<qreal>(1.0, x2 - x1), lineHeight));
    } else if (endOffset > lineEnd && startOffset <= lineEnd && hasNewline) {
      const qreal x = metrics.horizontalAdvance(literal.mid(lineStart, lineEnd - lineStart));
      rects.push_back(QRectF(origin.x() + x, origin.y() + line * lineHeight, qMax<qreal>(1.0, qMin<qreal>(24.0, maxWidth - x)), lineHeight));
    }

    if (!hasNewline) {
      break;
    }
    lineStart = lineEnd + 1;
    ++line;
  }
  return rects;
}

}  // namespace

BlockLayout::BlockLayout(NodeId id) : id_(std::move(id)) {}

NodeId BlockLayout::nodeId() const {
  return id_;
}

BlockType BlockLayout::type() const {
  return type_;
}

void BlockLayout::setType(BlockType type) {
  type_ = type;
}

QRectF BlockLayout::rect() const {
  return rect_;
}

void BlockLayout::setRect(QRectF rect) {
  rect_ = rect;
}

void BlockLayout::translateY(qreal dy) {
  rect_.translate(0, dy);
  for (auto& child : children_) {
    child->translateY(dy);
  }
  for (TableRowLayout& row : tableRows_) {
    row.rect.translate(0, dy);
    for (TableCellLayout& cell : row.cells) {
      cell.rect.translate(0, dy);
    }
  }
}

qreal BlockLayout::height() const {
  return rect_.height();
}

qreal BlockLayout::bottom() const {
  return rect_.bottom();
}

void BlockLayout::setInlineLayout(std::unique_ptr<InlineLayout> layout) {
  inlineLayout_ = std::move(layout);
}

InlineLayout* BlockLayout::inlineLayout() {
  return inlineLayout_.get();
}

const InlineLayout* BlockLayout::inlineLayout() const {
  return inlineLayout_.get();
}

void BlockLayout::setLiteral(QString literal) {
  literal_ = std::move(literal);
}

QString BlockLayout::literal() const {
  return literal_;
}

void BlockLayout::setCodeLanguage(QString language) {
  codeLanguage_ = std::move(language);
}

QString BlockLayout::codeLanguage() const {
  return codeLanguage_;
}

void BlockLayout::setCodeHighlightSpans(QVector<CodeHighlightSpan> spans) {
  codeHighlightSpans_ = std::move(spans);
}

const QVector<CodeHighlightSpan>& BlockLayout::codeHighlightSpans() const {
  return codeHighlightSpans_;
}

void BlockLayout::setHeadingLevel(int level) {
  headingLevel_ = level;
}

int BlockLayout::headingLevel() const {
  return headingLevel_;
}

void BlockLayout::setListMarker(QString marker) {
  listMarker_ = std::move(marker);
}

QString BlockLayout::listMarker() const {
  return listMarker_;
}

void BlockLayout::setContentSourceStart(qsizetype sourceStart) {
  contentSourceStart_ = sourceStart;
}

qsizetype BlockLayout::contentSourceStart() const {
  return contentSourceStart_;
}

void BlockLayout::setTaskListItem(bool taskListItem, bool checked) {
  taskListItem_ = taskListItem;
  taskChecked_ = checked;
}

bool BlockLayout::isTaskListItem() const {
  return taskListItem_;
}

bool BlockLayout::taskChecked() const {
  return taskChecked_;
}

void BlockLayout::setDepth(int depth) {
  depth_ = depth;
}

int BlockLayout::depth() const {
  return depth_;
}

void BlockLayout::setChildren(std::vector<std::unique_ptr<BlockLayout>> children) {
  children_ = std::move(children);
}

std::vector<std::unique_ptr<BlockLayout>>& BlockLayout::children() {
  return children_;
}

const std::vector<std::unique_ptr<BlockLayout>>& BlockLayout::children() const {
  return children_;
}

void BlockLayout::setTableRows(std::vector<TableRowLayout> rows) {
  tableRows_ = std::move(rows);
}

std::vector<BlockLayout::TableRowLayout>& BlockLayout::tableRows() {
  return tableRows_;
}

const std::vector<BlockLayout::TableRowLayout>& BlockLayout::tableRows() const {
  return tableRows_;
}

QRectF BlockLayout::tableCellRect(int row, int column) const {
  if (row < 0 || row >= static_cast<int>(tableRows_.size())) {
    return {};
  }
  const TableRowLayout& tableRow = tableRows_.at(static_cast<size_t>(row));
  if (column < 0 || column >= static_cast<int>(tableRow.cells.size())) {
    return {};
  }
  return tableRow.cells.at(static_cast<size_t>(column)).rect;
}

void BlockLayout::paint(QPainter& painter, const RenderTheme& theme, qreal scrollY) const {
  paintSelf(painter, theme, scrollY);
  for (const auto& child : children_) {
    child->paint(painter, theme, scrollY);
  }
}

bool BlockLayout::intersects(const QRectF& documentViewport) const {
  return rect_.intersects(documentViewport);
}

HitTestResult BlockLayout::hitTest(QPointF documentPos, const RenderTheme& theme) const {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    const BlockLayout& child = **it;
    if (child.rect().adjusted(-theme.blockSpacing(), -theme.blockSpacing(), theme.blockSpacing(), theme.blockSpacing()).contains(documentPos)) {
      HitTestResult childHit = child.hitTest(documentPos, theme);
      if (childHit.isValid()) {
        return childHit;
      }
    }
  }

  if (!rect_.adjusted(-2, -theme.blockSpacing() * 0.5, 2, theme.blockSpacing() * 0.5).contains(documentPos)) {
    return {};
  }

  if (type_ == BlockType::Table) {
    return hitTable(documentPos, theme);
  }
  return hitSelf(documentPos, theme);
}

QVector<QRectF> BlockLayout::selectionRects(const SelectionRange& selection, const RenderTheme& theme) const {
  QVector<QRectF> rects = selectionRectsSelf(selection, theme);
  for (const auto& child : children_) {
    rects += child->selectionRects(selection, theme);
  }
  return rects;
}

QVector<QRectF> BlockLayout::selectionRectsForOffsets(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const {
  QVector<QRectF> rects = selectionRectsSelfForOffsets(startOffset, endOffset, theme);
  for (const auto& child : children_) {
    rects += child->selectionRectsForOffsets(startOffset, endOffset, theme);
  }
  return rects;
}

void BlockLayout::paintSelf(QPainter& painter, const RenderTheme& theme, qreal scrollY) const {
  const QRectF viewRect = rect_.translated(0, -scrollY);

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      if (inlineLayout_) {
        if (!listMarker_.isEmpty()) {
          painter.save();
          painter.setFont(theme.paragraphFont());
          painter.setPen(theme.textColor());
          const QFontMetricsF metrics(painter.font());
          if (taskListItem_) {
            const QRectF box(
                viewRect.left() + 1,
                viewRect.top() + qMax<qreal>(2.0, (metrics.height() - 13.0) / 2.0),
                13,
                13);
            painter.setBrush(theme.backgroundColor());
            painter.setPen(QPen(theme.tableBorderColor(), 1));
            painter.drawRoundedRect(box, 2, 2);
            if (taskChecked_) {
              painter.setPen(QPen(theme.linkColor(), 1.8));
              painter.drawLine(QPointF(box.left() + 3, box.center().y()), QPointF(box.left() + 5.5, box.bottom() - 3));
              painter.drawLine(QPointF(box.left() + 5.5, box.bottom() - 3), QPointF(box.right() - 3, box.top() + 3));
            }
          } else {
            painter.drawText(QPointF(viewRect.left(), viewRect.top() + metrics.ascent()), listMarker_);
          }
          painter.restore();
          inlineLayout_->paint(painter, QPointF(viewRect.left() + theme.listIndent(), viewRect.top()));
        } else {
          inlineLayout_->paint(painter, viewRect.topLeft());
        }
      }
      break;
    case BlockType::BlockQuote: {
      painter.save();
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme.quoteBorderColor());
      painter.drawRect(QRectF(viewRect.left(), viewRect.top(), 4, viewRect.height()));
      painter.restore();
      break;
    }
    case BlockType::CodeFence:
      paintCodeFence(painter, theme, viewRect);
      break;
    case BlockType::HtmlBlock:
    case BlockType::MathBlock: {
      painter.save();
      painter.setPen(theme.codeBorderColor());
      painter.setBrush(theme.codeBackgroundColor());
      painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
      painter.setPen(theme.textColor());
      painter.setFont(type_ == BlockType::MathBlock ? theme.mathFont() : theme.codeFont());
      QTextOption option;
      option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
      const QMarginsF padding = theme.codePadding();
      painter.drawText(viewRect.marginsRemoved(padding), literal_, option);
      painter.restore();
      break;
    }
    case BlockType::ThematicBreak: {
      painter.save();
      painter.setPen(QPen(theme.codeBorderColor(), 1));
      const qreal y = viewRect.center().y();
      painter.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
      painter.restore();
      break;
    }
    case BlockType::Table:
      paintTable(painter, theme, scrollY);
      break;
    default:
      break;
  }
}

QVector<QRectF> BlockLayout::selectionRectsSelf(const SelectionRange& selection, const RenderTheme& theme) const {
  QVector<QRectF> rects;
  if (!selection.isSingleBlock() || selection.isCollapsed() || selection.anchor.blockId != id_) {
    return rects;
  }

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      break;
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
    case BlockType::MathBlock:
      return literalSelectionRects(selection.startOffset(), selection.endOffset(), theme);
    case BlockType::Table:
      rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
      return rects;
    default:
      return rects;
  }

  if (!inlineLayout_) {
    return rects;
  }

  const qreal textLeft = !listMarker_.isEmpty() ? rect_.left() + theme.listIndent() : rect_.left();
  const QPointF origin(textLeft, rect_.top());
  for (QRectF rect : inlineLayout_->selectionRects(selection.startOffset(), selection.endOffset())) {
    rect.translate(origin);
    rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
  }
  return rects;
}

QVector<QRectF> BlockLayout::selectionRectsSelfForOffsets(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const {
  QVector<QRectF> rects;

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      break;
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
    case BlockType::MathBlock:
      return literalSelectionRects(startOffset, endOffset, theme);
    case BlockType::Table:
      if (startOffset != endOffset) {
        rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
      }
      return rects;
    default:
      return rects;
  }

  if (!inlineLayout_) {
    return rects;
  }

  const qreal textLeft = !listMarker_.isEmpty() ? rect_.left() + theme.listIndent() : rect_.left();
  const QPointF origin(textLeft, rect_.top());
  for (QRectF rect : inlineLayout_->selectionRects(startOffset, endOffset)) {
    rect.translate(origin);
    rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
  }
  return rects;
}

QVector<QRectF> BlockLayout::literalSelectionRects(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const {
  QFont font = theme.codeFont();
  if (type_ == BlockType::MathBlock) {
    font = theme.mathFont();
  }
  const QRectF contentRect = rect_.marginsRemoved(theme.codePadding());
  QVector<QRectF> rects = literalSelectionRectsForRange(literal_, startOffset, endOffset, font, contentRect.topLeft(), contentRect.width());
  for (QRectF& rect : rects) {
    rect = rect.adjusted(-1.0, 0, 1.0, 0).intersected(contentRect.adjusted(0, 0, 1, 0));
  }
  return rects;
}

void BlockLayout::paintCodeFence(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const {
  painter.save();
  painter.setPen(theme.codeBorderColor());
  painter.setBrush(theme.codeBackgroundColor());
  painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));

  const QRectF contentRect = viewRect.marginsRemoved(theme.codePadding());
  const QStringList lines = literal_.isEmpty() ? QStringList{QString()} : literal_.split(QLatin1Char('\n'));
  QTextCharFormat baseFormat;
  baseFormat.setForeground(theme.textColor());
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

  qreal y = contentRect.top();
  qsizetype lineStartOffset = 0;
  for (const QString& sourceLine : lines) {
    const QString lineText = sourceLine.isEmpty() ? QStringLiteral(" ") : sourceLine;
    QTextLayout layout(lineText, theme.codeFont());
    layout.setTextOption(option);

    QVector<QTextLayout::FormatRange> formats;
    QTextLayout::FormatRange baseRange;
    baseRange.start = 0;
    baseRange.length = sourceLine.size();
    baseRange.format = baseFormat;
    formats.push_back(baseRange);

    for (const CodeHighlightSpan& span : codeHighlightSpans_) {
      const qsizetype lineEndOffset = lineStartOffset + sourceLine.size();
      const qsizetype start = qMax(span.start, lineStartOffset);
      const qsizetype end = qMin(span.end, lineEndOffset);
      if (end <= start) {
        continue;
      }
      QTextCharFormat format;
      format.setForeground(theme.codeHighlightColor(span.role));
      if (span.role == CodeHighlightRole::Keyword || span.role == CodeHighlightRole::Function || span.role == CodeHighlightRole::Type) {
        format.setFontWeight(QFont::DemiBold);
      }
      QTextLayout::FormatRange range;
      range.start = static_cast<int>(start - lineStartOffset);
      range.length = static_cast<int>(end - start);
      range.format = format;
      formats.push_back(range);
    }
    layout.setFormats(formats);

    layout.beginLayout();
    qreal lineY = 0;
    while (true) {
      QTextLine textLine = layout.createLine();
      if (!textLine.isValid()) {
        break;
      }
      textLine.setLineWidth(qMax<qreal>(1.0, contentRect.width()));
      textLine.setPosition(QPointF(0, lineY));
      lineY += textLine.height();
    }
    layout.endLayout();
    layout.draw(&painter, QPointF(contentRect.left(), y));
    y += qMax<qreal>(lineY, QFontMetricsF(theme.codeFont()).height());
    lineStartOffset += sourceLine.size() + 1;
  }
  painter.restore();
}

HitTestResult BlockLayout::hitSelf(QPointF documentPos, const RenderTheme& theme) const {
  HitTestResult result;
  result.blockId = id_;
  result.textNodeId = id_;
  result.blockRect = rect_;
  result.zone = HitTestResult::Zone::Block;

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      if (inlineLayout_) {
        const qreal textLeft = !listMarker_.isEmpty() ? rect_.left() + theme.listIndent() : rect_.left();
        const QRectF textRect(textLeft, rect_.top(), qMax<qreal>(1.0, rect_.right() - textLeft), rect_.height());
        if (!listMarker_.isEmpty() && documentPos.x() < textLeft) {
          result.zone = HitTestResult::Zone::Marker;
          result.cursorRect = QRectF(textLeft, rect_.top(), 1.0, rect_.height());
          return result;
        }
        result.zone = HitTestResult::Zone::Text;
        result.textOffset = inlineLayout_->hitTestTextOffset(documentPos - textRect.topLeft());
        const qsizetype localSourceOffset = inlineLayout_->hitTestSourceOffset(documentPos - textRect.topLeft());
        result.sourceOffset = contentSourceStart_ >= 0 ? contentSourceStart_ + localSourceOffset : localSourceOffset;
        result.cursorRect = inlineLayout_->cursorRectForSourceOffset(localSourceOffset).translated(textRect.topLeft());
      }
      break;
    case BlockType::CodeFence:
      result.zone = HitTestResult::Zone::Code;
      {
        const QRectF contentRect = rect_.marginsRemoved(theme.codePadding());
        result.textOffset = literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(), theme.codeFont());
        result.cursorRect = literalCursorRectForOffset(literal_, result.textOffset, theme.codeFont(), contentRect.topLeft());
      }
      break;
    case BlockType::MathBlock:
      result.zone = HitTestResult::Zone::Math;
      {
        const QRectF contentRect = rect_.marginsRemoved(theme.codePadding());
        result.textOffset = literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(), theme.mathFont());
        result.cursorRect = literalCursorRectForOffset(literal_, result.textOffset, theme.mathFont(), contentRect.topLeft());
      }
      break;
    case BlockType::HtmlBlock:
      result.zone = HitTestResult::Zone::Html;
      {
        const QRectF contentRect = rect_.marginsRemoved(theme.codePadding());
        result.textOffset = literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(), theme.codeFont());
        result.cursorRect = literalCursorRectForOffset(literal_, result.textOffset, theme.codeFont(), contentRect.topLeft());
      }
      break;
    default:
      result.cursorRect = QRectF(rect_.topLeft(), QSizeF(1.0, rect_.height()));
      break;
  }

  return result;
}

HitTestResult BlockLayout::hitTable(QPointF documentPos, const RenderTheme& theme) const {
  HitTestResult result;
  result.blockId = id_;
  result.textNodeId = id_;
  result.blockRect = rect_;
  result.zone = HitTestResult::Zone::Block;

  int rowIndex = 0;
  for (const TableRowLayout& row : tableRows_) {
    if (!row.rect.contains(documentPos)) {
      ++rowIndex;
      continue;
    }
    int columnIndex = 0;
    for (const TableCellLayout& cell : row.cells) {
      if (cell.rect.contains(documentPos)) {
        result.zone = HitTestResult::Zone::TableCell;
        result.textNodeId = cell.nodeId.isValid() ? cell.nodeId : id_;
        result.tableRow = rowIndex;
        result.tableColumn = columnIndex;
        result.textOffset = cell.text.hitTestTextOffset(documentPos - cell.rect.marginsRemoved(theme.tableCellPadding()).topLeft());
        const qsizetype localSourceOffset = cell.text.hitTestSourceOffset(documentPos - cell.rect.marginsRemoved(theme.tableCellPadding()).topLeft());
        result.sourceOffset = cell.contentSourceStart >= 0 ? cell.contentSourceStart + localSourceOffset : localSourceOffset;
        if (result.textOffset == 0 && documentPos.x() > cell.rect.center().x()) {
          result.textOffset = 1;
        }
        result.cursorRect = cell.text.cursorRectForSourceOffset(localSourceOffset).translated(cell.rect.marginsRemoved(theme.tableCellPadding()).topLeft());
        return result;
      }
      ++columnIndex;
    }
    ++rowIndex;
  }

  result.cursorRect = QRectF(rect_.topLeft(), QSizeF(1.0, rect_.height()));
  return result;
}

void BlockLayout::paintTable(QPainter& painter, const RenderTheme& theme, qreal scrollY) const {
  painter.save();
  for (const TableRowLayout& row : tableRows_) {
    const QRectF rowRect = row.rect.translated(0, -scrollY);
    for (const TableCellLayout& cell : row.cells) {
      const QRectF cellRect = cell.rect.translated(0, -scrollY);
      painter.setPen(theme.tableBorderColor());
      painter.setBrush(cell.header ? theme.tableHeaderBackgroundColor() : (cell.alternate ? theme.tableAlternateBackgroundColor() : theme.backgroundColor()));
      painter.drawRect(cellRect.adjusted(0.5, 0.5, -0.5, -0.5));
      QRectF contentRect = cellRect.marginsRemoved(theme.tableCellPadding());
      qreal textX = contentRect.left();
      if (cell.alignment == TableAlignment::Right) {
        textX = contentRect.right() - cell.text.size().width();
      } else if (cell.alignment == TableAlignment::Center) {
        textX = contentRect.left() + (contentRect.width() - cell.text.size().width()) / 2.0;
      }
      cell.text.paint(painter, QPointF(qMax(contentRect.left(), textX), contentRect.top()));
    }
    Q_UNUSED(rowRect);
  }
  painter.restore();
}

}  // namespace muffin
