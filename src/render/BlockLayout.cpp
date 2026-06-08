#include "render/BlockLayout.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>

#include <cmath>
#include <limits>
#include <utility>

namespace muffin {
namespace {

struct LiteralVisualLine {
  qsizetype start = 0;
  qsizetype length = 0;
  QRectF rect;
};

QVector<LiteralVisualLine> layoutLiteralVisualLines(const QString& literal, const QFont& font, qreal width, qreal lineHeight) {
  QVector<LiteralVisualLine> visualLines;
  const QStringList physicalLines = literal.isEmpty() ? QStringList{QString()} : literal.split(QLatin1Char('\n'));
  const qreal lineWidth = qMax<qreal>(1.0, width);
  const qreal fallbackHeight = qMax<qreal>(14.0, lineHeight);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

  qreal y = 0.0;
  qsizetype globalStart = 0;
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
      const qreal visualHeight = qMax<qreal>(fallbackHeight, textLine.height());
      textLine.setPosition(QPointF(0.0, y + (visualHeight - textLine.height()) * 0.5));
      producedLine = true;
      visualLines.push_back(LiteralVisualLine{
          globalStart + textLine.textStart(),
          qMin<qsizetype>(textLine.textLength(), sourceLine.size() - textLine.textStart()),
          QRectF(0.0, y, lineWidth, visualHeight)});
      y += visualHeight;
    }
    layout.endLayout();
    if (!producedLine) {
      visualLines.push_back(LiteralVisualLine{globalStart, 0, QRectF(0.0, y, lineWidth, fallbackHeight)});
      y += fallbackHeight;
    }
    globalStart += sourceLine.size() + 1;
  }
  return visualLines;
}

bool isLiteralBlockType(BlockType type) {
  return type == BlockType::FrontMatter || type == BlockType::CodeFence || type == BlockType::HtmlBlock || type == BlockType::MathBlock;
}

qsizetype literalOffsetForPoint(const QString& literal, QPointF localPos, const QFont& font, qreal width, qreal lineHeight) {
  const QFontMetricsF metrics(font);
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal, font, width, lineHeight);
  const LiteralVisualLine* target = lines.isEmpty() ? nullptr : &lines.first();
  for (const LiteralVisualLine& line : lines) {
    if (localPos.y() >= line.rect.top() && localPos.y() <= line.rect.bottom()) {
      target = &line;
      break;
    }
    if (localPos.y() >= line.rect.top()) {
      target = &line;
    }
  }
  if (!target) {
    return 0;
  }

  const qsizetype lineStart = target->start;
  const qsizetype lineEnd = qMin<qsizetype>(literal.size(), target->start + target->length);
  qsizetype offset = lineEnd;
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

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin, qreal width, qreal lineHeight) {
  const QFontMetricsF metrics(font);
  lineHeight = qMax<qreal>(14.0, lineHeight);
  offset = qBound<qsizetype>(0, offset, literal.size());
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal, font, width, lineHeight);
  const LiteralVisualLine* target = lines.isEmpty() ? nullptr : &lines.last();
  for (const LiteralVisualLine& line : lines) {
    const qsizetype lineEnd = line.start + line.length;
    if (offset >= line.start && offset <= lineEnd) {
      target = &line;
      break;
    }
  }
  if (!target) {
    return QRectF(origin.x(), origin.y(), 1.0, lineHeight);
  }
  const qsizetype localOffset = qBound<qsizetype>(target->start, offset, target->start + target->length);
  const qreal x = metrics.horizontalAdvance(literal.mid(target->start, localOffset - target->start));
  return QRectF(origin.x() + x, origin.y() + target->rect.top(), 1.0, qMax(lineHeight, target->rect.height()));
}

void paintUnorderedListMarker(QPainter& painter, BlockLayout::ListMarkerKind kind, QPointF center, qreal fontHeight, const QColor& color) {
  const qreal size = qBound<qreal>(4.2, fontHeight * 0.34, 6.2);
  const QRectF markerRect(center.x() - size * 0.5, center.y() - size * 0.5, size, size);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  switch (kind) {
    case BlockLayout::ListMarkerKind::BulletDisc:
      painter.drawEllipse(markerRect);
      break;
    case BlockLayout::ListMarkerKind::BulletCircle:
      painter.setPen(QPen(color, qMax<qreal>(1.1, size * 0.18)));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(markerRect);
      break;
    case BlockLayout::ListMarkerKind::BulletSquare:
      painter.drawRect(markerRect);
      break;
    default:
      break;
  }
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

QVector<QRectF> literalSelectionRectsForRange(
    const QString& literal,
    qsizetype startOffset,
    qsizetype endOffset,
    const QFont& font,
    qreal lineHeight,
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
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal, font, maxWidth, lineHeight);
  for (const LiteralVisualLine& line : lines) {
    const qsizetype lineEnd = line.start + line.length;
    const qsizetype rangeStart = qMax(startOffset, line.start);
    const qsizetype rangeEnd = qMin(endOffset, lineEnd);
    if (rangeStart < rangeEnd) {
      const qreal x1 = metrics.horizontalAdvance(literal.mid(line.start, rangeStart - line.start));
      const qreal x2 = metrics.horizontalAdvance(literal.mid(line.start, rangeEnd - line.start));
      rects.push_back(QRectF(origin.x() + x1, origin.y() + line.rect.top(), qMax<qreal>(1.0, x2 - x1), line.rect.height()));
    } else if (endOffset > lineEnd && startOffset <= lineEnd && lineEnd < literal.size() && literal.at(lineEnd) == QLatin1Char('\n')) {
      const qreal x = metrics.horizontalAdvance(literal.mid(line.start, line.length));
      rects.push_back(QRectF(origin.x() + x, origin.y() + line.rect.top(), qMax<qreal>(1.0, qMin<qreal>(24.0, maxWidth - x)), line.rect.height()));
    }
  }
  return rects;
}

qreal literalTextHeight(const QString& literal, const QFont& font, qreal width, qreal lineHeight) {
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal, font, width, lineHeight);
  if (lines.isEmpty()) {
    return qMax<qreal>(14.0, lineHeight);
  }
  const LiteralVisualLine& last = lines.last();
  return last.rect.bottom();
}

QVector<CodeHighlightSpan> highlightMathTex(const QString& text) {
  QVector<CodeHighlightSpan> spans;
  qsizetype i = 0;
  while (i < text.size()) {
    const QChar ch = text.at(i);
    if (ch == QLatin1Char('\\')) {
      qsizetype end = i + 1;
      while (end < text.size() && text.at(end).isLetter()) {
        ++end;
      }
      if (end == i + 1 && end < text.size()) {
        ++end;
      }
      spans.push_back(CodeHighlightSpan{i, end, CodeHighlightRole::Property});
      i = end;
      continue;
    }
    if (ch.isDigit()) {
      qsizetype end = i + 1;
      while (end < text.size() && (text.at(end).isDigit() || text.at(end) == QLatin1Char('.'))) {
        ++end;
      }
      spans.push_back(CodeHighlightSpan{i, end, CodeHighlightRole::Number});
      i = end;
      continue;
    }
    if (QStringView(QStringLiteral("{}[]()")).contains(ch)) {
      spans.push_back(CodeHighlightSpan{i, i + 1, CodeHighlightRole::Punctuation});
      ++i;
      continue;
    }
    if (QStringView(QStringLiteral("^_+-=*/,:;|&<>")).contains(ch)) {
      spans.push_back(CodeHighlightSpan{i, i + 1, CodeHighlightRole::Operator});
      ++i;
      continue;
    }
    ++i;
  }
  return spans;
}

HitTestResult::DefinitionField definitionHitFieldFor(BlockLayout::DefinitionSlotLayout::Field field) {
  switch (field) {
    case BlockLayout::DefinitionSlotLayout::Field::Label:
      return HitTestResult::DefinitionField::Label;
    case BlockLayout::DefinitionSlotLayout::Field::Destination:
      return HitTestResult::DefinitionField::Destination;
    case BlockLayout::DefinitionSlotLayout::Field::Title:
      return HitTestResult::DefinitionField::Title;
    case BlockLayout::DefinitionSlotLayout::Field::Note:
      return HitTestResult::DefinitionField::Note;
  }
  return HitTestResult::DefinitionField::None;
}

const BlockLayout::DefinitionTokenLayout* firstDefinitionToken(const QVector<BlockLayout::DefinitionTokenLayout>& tokens) {
  return tokens.isEmpty() ? nullptr : &tokens.first();
}

const BlockLayout::DefinitionTokenLayout* lastDefinitionToken(const QVector<BlockLayout::DefinitionTokenLayout>& tokens) {
  return tokens.isEmpty() ? nullptr : &tokens.last();
}

const BlockLayout::DefinitionTokenLayout* nearestEditableDefinitionToken(
    const QVector<BlockLayout::DefinitionTokenLayout>& tokens,
    qreal x) {
  const BlockLayout::DefinitionTokenLayout* target = nullptr;
  qreal bestDistance = std::numeric_limits<qreal>::max();
  for (const BlockLayout::DefinitionTokenLayout& token : tokens) {
    if (!token.editable) {
      continue;
    }
    const qreal distance = x < token.rect.left() ? token.rect.left() - x
                           : x > token.rect.right() ? x - token.rect.right()
                                                     : 0.0;
    if (distance < bestDistance) {
      bestDistance = distance;
      target = &token;
    }
  }
  return target;
}

qreal horizontalDistanceToDefinitionToken(const BlockLayout::DefinitionTokenLayout& token, qreal x) {
  return x < token.rect.left() ? token.rect.left() - x
         : x > token.rect.right() ? x - token.rect.right()
                                  : 0.0;
}

const BlockLayout::DefinitionTokenLayout* zeroWidthEditableDefinitionTokenAtSourceOffset(
    const QVector<BlockLayout::DefinitionTokenLayout>& tokens,
    qsizetype sourceOffset) {
  const BlockLayout::DefinitionTokenLayout* target = nullptr;
  for (const BlockLayout::DefinitionTokenLayout& token : tokens) {
    if (!token.editable || token.sourceStart != sourceOffset || token.sourceEnd != sourceOffset) {
      continue;
    }
    if (!target) {
      target = &token;
    }
    if (token.field == BlockLayout::DefinitionSlotLayout::Field::Title && token.placeholder.isEmpty()) {
      target = &token;
    }
  }
  return target;
}

const BlockLayout::DefinitionTokenLayout* editableDefinitionTokenForSourceOffset(
    const QVector<BlockLayout::DefinitionTokenLayout>& tokens,
    qsizetype sourceOffset) {
  if (const BlockLayout::DefinitionTokenLayout* token = zeroWidthEditableDefinitionTokenAtSourceOffset(tokens, sourceOffset)) {
    return token;
  }
  for (const BlockLayout::DefinitionTokenLayout& token : tokens) {
    if (token.editable && token.sourceStart <= sourceOffset && sourceOffset <= token.sourceEnd) {
      return &token;
    }
  }
  return nullptr;
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

void BlockLayout::translate(qreal dx, qreal dy) {
  rect_.translate(dx, dy);
  for (auto& child : children_) {
    child->translate(dx, dy);
  }
  for (TableRowLayout& row : tableRows_) {
    row.rect.translate(dx, dy);
    for (TableCellLayout& cell : row.cells) {
      cell.rect.translate(dx, dy);
    }
  }
  for (DefinitionSlotLayout& slot : definitionSlots_) {
    slot.rect.translate(dx, dy);
  }
  for (DefinitionTokenLayout& token : definitionTokens_) {
    token.rect.translate(dx, dy);
  }
}

void BlockLayout::translateY(qreal dy) {
  translate(0, dy);
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

void BlockLayout::setMathLayout(std::shared_ptr<math::MathLayoutResult> layout) {
  mathLayout_ = std::move(layout);
}

const math::MathLayoutResult* BlockLayout::mathLayout() const {
  return mathLayout_.get();
}

void BlockLayout::setLiteralEditing(bool editing) {
  literalEditing_ = editing;
}

bool BlockLayout::literalEditing() const {
  return literalEditing_;
}

QRectF BlockLayout::literalContentRect(const RenderTheme& theme) const {
  if (type_ == BlockType::MathBlock && literalEditing_) {
    return mathEditorSourceRect(theme);
  }
  return rect_.marginsRemoved(theme.codePadding());
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

void BlockLayout::setListMarkerKind(ListMarkerKind kind) {
  listMarkerKind_ = kind;
}

BlockLayout::ListMarkerKind BlockLayout::listMarkerKind() const {
  return listMarkerKind_;
}

bool BlockLayout::hasListMarker() const {
  return listMarkerKind_ != ListMarkerKind::None;
}

void BlockLayout::setContentSourceStart(qsizetype sourceStart) {
  contentSourceStart_ = sourceStart;
}

qsizetype BlockLayout::contentSourceStart() const {
  return contentSourceStart_;
}

void BlockLayout::setPlaceholderText(QString text) {
  placeholderText_ = std::move(text);
}

QString BlockLayout::placeholderText() const {
  return placeholderText_;
}

void BlockLayout::setDefinition(const DefinitionBlock& definition) {
  definition_ = definition;
}

DefinitionBlock BlockLayout::definition() const {
  return definition_;
}

void BlockLayout::setDefinitionSlots(QVector<DefinitionSlotLayout> definitionSlots) {
  definitionSlots_ = std::move(definitionSlots);
}

const QVector<BlockLayout::DefinitionSlotLayout>& BlockLayout::definitionSlots() const {
  return definitionSlots_;
}

void BlockLayout::setDefinitionTokens(QVector<DefinitionTokenLayout> definitionTokens) {
  definitionTokens_ = std::move(definitionTokens);
}

const QVector<BlockLayout::DefinitionTokenLayout>& BlockLayout::definitionTokens() const {
  return definitionTokens_;
}

QRectF BlockLayout::definitionCursorRectForSourceOffset(qsizetype sourceOffset, const RenderTheme& theme) const {
  if (definitionTokens_.isEmpty() || sourceOffset < 0) {
    return {};
  }

  const qsizetype sourceStart = definition_.sourceRange.isValid()
                                    ? definition_.sourceRange.start
                                    : definition_.markerRange.start;
  const qsizetype sourceEnd = definition_.sourceRange.isValid()
                                  ? definition_.sourceRange.end
                                  : qMax(definition_.markerRange.end,
                                         qMax(definition_.destinationRange.end,
                                              qMax(definition_.titleRange.end, definition_.noteRange.end)));
  const DefinitionTokenLayout* firstToken = firstDefinitionToken(definitionTokens_);
  if (sourceOffset <= sourceStart) {
    return firstToken ? QRectF(firstToken->rect.left(), rect_.top(), 1.0, rect_.height()) : QRectF();
  }

  const DefinitionTokenLayout* target = editableDefinitionTokenForSourceOffset(definitionTokens_, sourceOffset);
  const DefinitionTokenLayout* lastToken = lastDefinitionToken(definitionTokens_);
  if (!target && sourceOffset >= sourceEnd) {
    return lastToken ? QRectF(lastToken->rect.right(), rect_.top(), 1.0, rect_.height()) : QRectF();
  }

  if (!target) {
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (const DefinitionTokenLayout& token : definitionTokens_) {
      if (!token.editable) {
        continue;
      }
      const qreal distance = sourceOffset < token.sourceStart ? token.sourceStart - sourceOffset
                             : sourceOffset > token.sourceEnd ? sourceOffset - token.sourceEnd
                                                             : 0.0;
      if (distance < bestDistance) {
        bestDistance = distance;
        target = &token;
      }
    }
  }
  if (!target) {
    return {};
  }

  const qsizetype slotSourceStart = target->sourceStart >= 0 ? target->sourceStart : 0;
  const qsizetype localOffset = qBound<qsizetype>(0, sourceOffset - slotSourceStart, target->text.size());
  const QFontMetricsF metrics(theme.paragraphFont());
  const qreal cursorX = target->rect.left() + metrics.horizontalAdvance(target->text.left(localOffset));
  return QRectF(cursorX, rect_.top(), 1.0, rect_.height());
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

bool BlockLayout::containsNode(NodeId id) const {
  if (id_ == id) {
    return true;
  }
  for (const auto& child : children_) {
    if (child->containsNode(id)) {
      return true;
    }
  }
  return false;
}

bool BlockLayout::containsInteractiveContent(QPointF documentPos, const RenderTheme& theme) const {
  if (isLiteralBlockType(type_)) {
    return rect_.adjusted(-2, 0, 2, 0).contains(documentPos);
  }
  if (type_ == BlockType::LinkDefinition || type_ == BlockType::FootnoteDefinition) {
    return rect_.adjusted(-2, -theme.blockSpacing() * 0.5, 2, theme.blockSpacing() * 0.5).contains(documentPos);
  }
  return rect_.adjusted(-2, -theme.blockSpacing() * 0.5, 2, theme.blockSpacing() * 0.5).contains(documentPos);
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
        if (hasListMarker()) {
          painter.save();
          painter.setFont(theme.paragraphFont());
          painter.setPen(theme.textColor());
          const QFontMetricsF metrics(painter.font());
          const qreal markerX = viewRect.left() + theme.listIndent() * 0.45;
          if (taskListItem_) {
            const QRectF box(
                markerX,
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
          } else if (listMarkerKind_ == ListMarkerKind::OrderedText) {
            painter.drawText(QPointF(markerX, viewRect.top() + metrics.ascent()), listMarker_);
          } else {
            const QPointF markerCenter(markerX + metrics.horizontalAdvance(QStringLiteral("0")) * 0.35, viewRect.top() + metrics.ascent() - metrics.xHeight() * 0.45);
            paintUnorderedListMarker(painter, listMarkerKind_, markerCenter, metrics.height(), theme.textColor());
          }
          painter.restore();
          inlineLayout_->paint(painter, QPointF(viewRect.left() + theme.listIndent(), viewRect.top()));
        } else {
          inlineLayout_->paint(painter, viewRect.topLeft());
        }
        if (!placeholderText_.isEmpty()) {
          painter.save();
          painter.setFont(theme.paragraphFont());
          painter.setPen(theme.mutedTextColor());
          const QFontMetricsF metrics(painter.font());
          painter.drawText(QPointF(viewRect.left(), viewRect.top() + metrics.ascent()), placeholderText_);
          painter.restore();
        }
        if (type_ == BlockType::Heading && headingLevel_ <= 2) {
          painter.save();
          painter.setPen(QPen(theme.codeBorderColor(), 1));
          const qreal y = viewRect.top() + inlineLayout_->height() + theme.blockSpacing() * 0.15;
          painter.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
          painter.restore();
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
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
      paintCodeFence(painter, theme, viewRect);
      break;
    case BlockType::MathBlock: {
      painter.save();
      painter.setPen(theme.codeBorderColor());
      painter.setBrush(theme.codeBackgroundColor());
      painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
      if (literalEditing_) {
        const QRectF sourceRect = mathEditorSourceRect(theme).translated(0, -scrollY);
        const QRectF previewRect = mathPreviewContentRect(theme).translated(0, -scrollY);
        const QMarginsF padding = theme.codePadding();
        const QFont codeFont = theme.codeFont();
        const QFontMetricsF codeMetrics(codeFont);
        const qreal markerHeight = qMax<qreal>(14.0, codeMetrics.height());
        const qreal sourcePanelBottom = sourceRect.bottom() + markerHeight + padding.bottom();
        const QRectF sourcePanel(viewRect.left(), viewRect.top(), viewRect.width(), qMax<qreal>(1.0, sourcePanelBottom - viewRect.top()));
        const QRectF previewPanel(viewRect.left(), sourcePanel.bottom(), viewRect.width(), qMax<qreal>(1.0, viewRect.bottom() - sourcePanel.bottom()));

        painter.fillRect(sourcePanel, theme.codeBackgroundColor());
        painter.fillRect(previewPanel, theme.backgroundColor());
        painter.setPen(theme.textColor());
        painter.setFont(codeFont);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        painter.drawText(QPointF(sourceRect.left(), viewRect.top() + padding.top() + codeMetrics.ascent()), QStringLiteral("$$"));
        paintLiteralSource(painter, theme, sourceRect, highlightMathTex(literal_));
        painter.drawText(QPointF(sourceRect.left(), sourceRect.bottom() + codeMetrics.ascent()), QStringLiteral("$$"));
        painter.setPen(QPen(theme.codeBorderColor(), 1));
        const qreal dividerY = sourcePanel.bottom() + 0.5;
        painter.drawLine(QPointF(viewRect.left(), dividerY), QPointF(viewRect.right(), dividerY));
        if (mathLayout_ && mathLayout_->valid()) {
          const qreal x = previewRect.left() + qMax<qreal>(0.0, (previewRect.width() - mathLayout_->size.width()) / 2.0);
          const qreal y = previewRect.top() + qMax<qreal>(0.0, (previewRect.height() - mathLayout_->size.height()) / 2.0);
          mathLayout_->paint(painter, QPointF(x, y));
        }
      } else if (!mathLayout_ || !mathLayout_->valid()) {
        painter.setPen(theme.textColor());
        painter.setFont(theme.mathFont());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        painter.drawText(viewRect.marginsRemoved(theme.codePadding()), literal_, option);
      } else {
        const QRectF contentRect = viewRect.marginsRemoved(theme.codePadding());
        const qreal x = contentRect.left() + qMax<qreal>(0.0, (contentRect.width() - mathLayout_->size.width()) / 2.0);
        const qreal y = contentRect.top() + qMax<qreal>(0.0, (contentRect.height() - mathLayout_->size.height()) / 2.0);
        mathLayout_->paint(painter, QPointF(x, y));
      }
      painter.restore();
      break;
    }
    case BlockType::HtmlBlock: {
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
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      paintDefinition(painter, theme, viewRect);
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
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
      return literalSelectionRects(selection.startOffset(), selection.endOffset(), theme);
    case BlockType::MathBlock:
      if (literalEditing_) {
        return literalSelectionRects(selection.startOffset(), selection.endOffset(), theme);
      }
      rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
      return rects;
    case BlockType::Table:
      for (const TableRowLayout& row : tableRows_) {
        for (const TableCellLayout& cell : row.cells) {
          if (cell.nodeId != selection.focus.text.nodeId) {
            continue;
          }
          const qsizetype localAnchorSourceOffset =
              selection.anchor.text.sourceOffset >= 0 && cell.contentSourceStart >= 0 ? selection.anchor.text.sourceOffset - cell.contentSourceStart : -1;
          const qsizetype localFocusSourceOffset =
              selection.focus.text.sourceOffset >= 0 && cell.contentSourceStart >= 0 ? selection.focus.text.sourceOffset - cell.contentSourceStart : -1;
          const QVector<QRectF> inlineRects =
              localAnchorSourceOffset >= 0 && localFocusSourceOffset >= 0
                  ? cell.text.selectionRectsForSourceOffsets(localAnchorSourceOffset, localFocusSourceOffset)
                  : cell.text.selectionRects(selection.startOffset(), selection.endOffset());
          const QPointF origin = tableCellTextOrigin(cell, theme);
          for (QRectF rect : inlineRects) {
            rect.translate(origin);
            rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
          }
          return rects;
        }
      }
      return rects;
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      return definitionSelectionRects(selection.startOffset(), selection.endOffset(), theme);
    default:
      return rects;
  }

  if (!inlineLayout_) {
    return rects;
  }

  const qreal textLeft = hasListMarker() ? rect_.left() + theme.listIndent() : rect_.left();
  const QPointF origin(textLeft, rect_.top());
  const qsizetype localAnchorSourceOffset =
      selection.anchor.text.sourceOffset >= 0 && contentSourceStart_ >= 0 ? selection.anchor.text.sourceOffset - contentSourceStart_ : -1;
  const qsizetype localFocusSourceOffset =
      selection.focus.text.sourceOffset >= 0 && contentSourceStart_ >= 0 ? selection.focus.text.sourceOffset - contentSourceStart_ : -1;
  const QVector<QRectF> inlineRects =
      localAnchorSourceOffset >= 0 && localFocusSourceOffset >= 0
          ? inlineLayout_->selectionRectsForSourceOffsets(localAnchorSourceOffset, localFocusSourceOffset)
          : inlineLayout_->selectionRects(selection.startOffset(), selection.endOffset());
  for (QRectF rect : inlineRects) {
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
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::HtmlBlock:
      return literalSelectionRects(startOffset, endOffset, theme);
    case BlockType::MathBlock:
      if (literalEditing_) {
        return literalSelectionRects(startOffset, endOffset, theme);
      }
      if (startOffset != endOffset) {
        rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
      }
      return rects;
    case BlockType::Table:
      if (startOffset != endOffset) {
        rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
      }
      return rects;
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      return definitionSelectionRects(startOffset, endOffset, theme);
    default:
      return rects;
  }

  if (!inlineLayout_) {
    return rects;
  }

  const qreal textLeft = hasListMarker() ? rect_.left() + theme.listIndent() : rect_.left();
  const QPointF origin(textLeft, rect_.top());
  for (QRectF rect : inlineLayout_->selectionRects(startOffset, endOffset)) {
    rect.translate(origin);
    rects.push_back(rect.adjusted(-1.0, 0, 1.0, 0));
  }
  return rects;
}

QVector<QRectF> BlockLayout::literalSelectionRects(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const {
  QFont font = theme.codeFont();
  const QRectF contentRect = literalContentRect(theme);
  QVector<QRectF> rects =
      literalSelectionRectsForRange(literal_, startOffset, endOffset, font, theme.codeLineHeight(), contentRect.topLeft(), contentRect.width());
  for (QRectF& rect : rects) {
    rect = rect.adjusted(-1.0, 0, 1.0, 0).intersected(contentRect.adjusted(0, 0, 1, 0));
  }
  return rects;
}

QRectF BlockLayout::mathEditorSourceRect(const RenderTheme& theme) const {
  const QMarginsF padding = theme.codePadding();
  const QFontMetricsF metrics(theme.codeFont());
  const qreal markerHeight = qMax<qreal>(14.0, metrics.height());
  const qreal contentWidth = qMax<qreal>(1.0, rect_.width() - padding.left() - padding.right());
  return QRectF(rect_.left() + padding.left(),
                rect_.top() + padding.top() + markerHeight,
                contentWidth,
                literalTextHeight(literal_, theme.codeFont(), contentWidth, theme.codeLineHeight()));
}

QRectF BlockLayout::mathPreviewContentRect(const RenderTheme& theme) const {
  if (!literalEditing_) {
    return rect_.marginsRemoved(theme.codePadding());
  }
  const QMarginsF padding = theme.codePadding();
  const QFontMetricsF metrics(theme.codeFont());
  const qreal markerHeight = qMax<qreal>(14.0, metrics.height());
  const QRectF sourceRect = mathEditorSourceRect(theme);
  const qreal previewTop = sourceRect.bottom() + markerHeight + padding.bottom() + padding.top();
  return QRectF(rect_.left() + padding.left(),
                previewTop,
                qMax<qreal>(1.0, rect_.width() - padding.left() - padding.right()),
                qMax<qreal>(1.0, rect_.bottom() - padding.bottom() - previewTop));
}

void BlockLayout::paintCodeFence(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const {
  painter.save();
  painter.setPen(theme.codeBorderColor());
  painter.setBrush(theme.codeBackgroundColor());
  painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
  paintLiteralSource(painter, theme, viewRect.marginsRemoved(theme.codePadding()), codeHighlightSpans_);
  painter.restore();
}

void BlockLayout::paintLiteralSource(QPainter& painter, const RenderTheme& theme, QRectF contentRect, const QVector<CodeHighlightSpan>& spans) const {
  const QStringList lines = literal_.isEmpty() ? QStringList{QString()} : literal_.split(QLatin1Char('\n'));
  QTextCharFormat baseFormat;
  baseFormat.setForeground(theme.textColor());
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

  qreal y = contentRect.top();
  qsizetype lineStartOffset = 0;
  const qreal codeLineHeight = theme.codeLineHeight();
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

    for (const CodeHighlightSpan& span : spans) {
      const qsizetype lineEndOffset = lineStartOffset + sourceLine.size();
      const qsizetype start = qMax(span.start, lineStartOffset);
      const qsizetype end = qMin(span.end, lineEndOffset);
      if (end <= start) {
        continue;
      }
      QTextCharFormat format;
      format.setForeground(theme.codeHighlightColor(span.role));
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
      const qreal visualHeight = qMax<qreal>(codeLineHeight, textLine.height());
      textLine.setPosition(QPointF(0, lineY + (visualHeight - textLine.height()) * 0.5));
      lineY += visualHeight;
    }
    layout.endLayout();
    layout.draw(&painter, QPointF(contentRect.left(), y));
    y += qMax<qreal>(lineY, codeLineHeight);
    lineStartOffset += sourceLine.size() + 1;
  }
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
        const qreal textLeft = hasListMarker() ? rect_.left() + theme.listIndent() : rect_.left();
        const QRectF textRect(textLeft, rect_.top(), qMax<qreal>(1.0, rect_.right() - textLeft), rect_.height());
        if (hasListMarker() && documentPos.x() < textLeft) {
          result.zone = HitTestResult::Zone::Marker;
          result.cursorRect = QRectF(textLeft, rect_.top(), 1.0, rect_.height());
          return result;
        }
        result.zone = HitTestResult::Zone::Text;
        const QPointF localPos = documentPos - textRect.topLeft();
        result.textOffset = inlineLayout_->hitTestTextOffset(localPos);
        const qsizetype localSourceOffset = inlineLayout_->hitTestSourceOffset(localPos);
        result.sourceOffset = contentSourceStart_ >= 0 ? contentSourceStart_ + localSourceOffset : localSourceOffset;
        result.cursorRect = inlineLayout_->hitTestCursorRect(localPos).translated(textRect.topLeft());
        result.linkHref = inlineLayout_->linkHrefAtLocalPos(localPos);
        result.imageSrc = inlineLayout_->imageSrcAtLocalPos(localPos);
      }
      break;
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
      result.zone = type_ == BlockType::FrontMatter ? HitTestResult::Zone::FrontMatter : HitTestResult::Zone::Code;
      {
        const QRectF contentRect = rect_.marginsRemoved(theme.codePadding());
        result.textOffset =
            literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(), theme.codeFont(), contentRect.width(), theme.codeLineHeight());
        result.cursorRect =
            literalCursorRectForOffset(literal_, result.textOffset, theme.codeFont(), contentRect.topLeft(), contentRect.width(), theme.codeLineHeight());
      }
      break;
    case BlockType::MathBlock:
      result.zone = HitTestResult::Zone::Math;
      {
        if (literalEditing_) {
          const QRectF contentRect = mathEditorSourceRect(theme);
          result.textOffset =
              literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(), theme.codeFont(), contentRect.width(), theme.codeLineHeight());
          result.cursorRect =
              literalCursorRectForOffset(literal_, result.textOffset, theme.codeFont(), contentRect.topLeft(), contentRect.width(), theme.codeLineHeight());
        } else {
          result.textOffset = documentPos.x() < rect_.center().x() ? 0 : literal_.size();
          const qreal x = result.textOffset == 0 ? rect_.left() : rect_.right();
          result.cursorRect = QRectF(x, rect_.top(), 1.0, rect_.height());
        }
      }
      break;
    case BlockType::HtmlBlock:
      result.zone = HitTestResult::Zone::Html;
      {
        const QRectF contentRect = rect_.marginsRemoved(theme.codePadding());
        result.textOffset =
            literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(), theme.codeFont(), contentRect.width(), theme.codeLineHeight());
        result.cursorRect =
            literalCursorRectForOffset(literal_, result.textOffset, theme.codeFont(), contentRect.topLeft(), contentRect.width(), theme.codeLineHeight());
      }
      break;
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      return hitDefinition(documentPos, theme);
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
        const QPointF textOrigin = tableCellTextOrigin(cell, theme);
        const QPointF localPos = documentPos - textOrigin;
        result.textOffset = cell.text.hitTestTextOffset(localPos);
        const qsizetype localSourceOffset = cell.text.hitTestSourceOffset(localPos);
        result.sourceOffset = cell.contentSourceStart >= 0 ? cell.contentSourceStart + localSourceOffset : localSourceOffset;
        result.cursorRect = cell.text.cursorRectForSourceOffset(localSourceOffset).translated(textOrigin);
        result.linkHref = cell.text.linkHrefAtLocalPos(localPos);
        result.imageSrc = cell.text.imageSrcAtLocalPos(localPos);
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
      cell.text.paint(painter, tableCellTextOrigin(cell, theme) + QPointF(0, -scrollY));
    }
    Q_UNUSED(rowRect);
  }
  painter.restore();
}

void BlockLayout::paintDefinition(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const {
  painter.save();
  const QFont font = theme.paragraphFont();
  const QFontMetricsF metrics(font);
  painter.setFont(font);

  for (const DefinitionTokenLayout& token : definitionTokens_) {
    if (token.kind == DefinitionTokenLayout::Kind::Slot && token.text.isEmpty() && token.focused) {
      continue;
    }
    const bool slotToken = token.kind == DefinitionTokenLayout::Kind::Slot;
    painter.setPen(slotToken && !token.text.isEmpty() ? theme.textColor() : theme.mutedTextColor());
    const QString text = slotToken && token.text.isEmpty() ? token.placeholder : token.text;
    if (!text.isEmpty()) {
      painter.drawText(QPointF(token.rect.left(), viewRect.top() + metrics.ascent()), text);
    }
  }

  // Paint continuation lines below the token model for multi-line footnotes
  if (!literal_.isEmpty() && type_ == BlockType::FootnoteDefinition) {
    const qreal lineHeightF = std::ceil(metrics.height() * 1.16);
    qreal noteX = viewRect.right();
    for (const DefinitionTokenLayout& token : definitionTokens_) {
      if (token.field == DefinitionSlotLayout::Field::Note) {
        noteX = token.rect.left();
        break;
      }
    }
    const qreal continuationWidth = qMax<qreal>(1.0, viewRect.right() - noteX);
    const qreal continuationTop = viewRect.top() + lineHeightF;
    painter.setPen(theme.mutedTextColor());
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QTextLayout layout(literal_, font);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal lineY = continuationTop;
    while (true) {
      QTextLine textLine = layout.createLine();
      if (!textLine.isValid()) {
        break;
      }
      textLine.setLineWidth(continuationWidth);
      textLine.setPosition(QPointF(noteX, lineY));
      lineY += qMax<qreal>(lineHeightF, textLine.height());
    }
    layout.endLayout();
    layout.draw(&painter, QPointF(0, 0));
  }

  painter.restore();
}

HitTestResult BlockLayout::hitDefinition(QPointF documentPos, const RenderTheme& theme) const {
  HitTestResult result;
  result.blockId = id_;
  result.textNodeId = id_;
  result.blockRect = rect_;
  result.zone = HitTestResult::Zone::Text;

  const QFontMetricsF metrics(theme.paragraphFont());
  const qsizetype sourceStart = definition_.sourceRange.isValid()
                                    ? definition_.sourceRange.start
                                    : definition_.markerRange.start;
  const qsizetype sourceEnd = definition_.sourceRange.isValid()
                                  ? definition_.sourceRange.end
                                  : qMax(definition_.markerRange.end,
                                         qMax(definition_.destinationRange.end,
                                              qMax(definition_.titleRange.end, definition_.noteRange.end)));
  const DefinitionTokenLayout* firstToken = firstDefinitionToken(definitionTokens_);
  const DefinitionTokenLayout* lastToken = lastDefinitionToken(definitionTokens_);
  if (firstToken && lastToken) {
    if (documentPos.x() <= firstToken->rect.left()) {
      result.sourceOffset = sourceStart;
      result.textOffset = definition_.markerRange.isValid() ? sourceStart - definition_.markerRange.start : 0;
      result.cursorRect = QRectF(firstToken->rect.left(), rect_.top(), 1.0, rect_.height());
      return result;
    }
    if (documentPos.x() >= lastToken->rect.right()) {
      result.sourceOffset = sourceEnd;
      result.textOffset = definition_.markerRange.isValid() ? sourceEnd - definition_.markerRange.start : 0;
      result.cursorRect = QRectF(lastToken->rect.right(), rect_.top(), 1.0, rect_.height());
      return result;
    }
  }

  const DefinitionTokenLayout* target = nullptr;
  qreal bestDistance = std::numeric_limits<qreal>::max();
  for (const DefinitionTokenLayout& token : definitionTokens_) {
    if (token.editable &&
        token.rect.adjusted(-4.0, -theme.blockSpacing() * 0.25, 4.0, theme.blockSpacing() * 0.25).contains(documentPos)) {
      const qreal distance = horizontalDistanceToDefinitionToken(token, documentPos.x());
      if (distance <= bestDistance) {
        bestDistance = distance;
        target = &token;
      }
    }
  }
  if (!target) {
    target = nearestEditableDefinitionToken(definitionTokens_, documentPos.x());
  }

  if (!target) {
    result.cursorRect = QRectF(rect_.topLeft(), QSizeF(1.0, rect_.height()));
    return result;
  }
  result.definitionField = definitionHitFieldFor(target->field);

  const QString text = target->text;
  qsizetype localOffset = 0;
  qreal cursorX = target->rect.left();
  if (!text.isEmpty()) {
    const QFontMetricsF metrics(theme.paragraphFont());
    qreal best = std::numeric_limits<qreal>::max();
    for (qsizetype i = 0; i <= text.size(); ++i) {
      const qreal x = target->rect.left() + metrics.horizontalAdvance(text.left(i));
      const qreal distance = std::abs(documentPos.x() - x);
      if (distance <= best) {
        best = distance;
        localOffset = i;
        cursorX = x;
      }
    }
  }

  const qsizetype slotSourceStart = target->sourceStart >= 0 ? target->sourceStart : 0;
  const qsizetype slotSourceEnd = target->sourceEnd >= target->sourceStart ? target->sourceEnd : slotSourceStart;
  result.sourceOffset = qBound<qsizetype>(slotSourceStart, slotSourceStart + localOffset, slotSourceEnd);
  const qsizetype blockStart = definition_.markerRange.isValid() ? definition_.markerRange.start : 0;
  result.textOffset = qMax<qsizetype>(0, result.sourceOffset - blockStart);
  result.cursorRect = definitionCursorRectForSourceOffset(result.sourceOffset, theme);
  if (result.cursorRect.isEmpty()) {
    result.cursorRect = QRectF(cursorX, rect_.top(), 1.0, rect_.height());
  }
  return result;
}

QVector<QRectF> BlockLayout::definitionSelectionRects(qsizetype startOffset, qsizetype endOffset, const RenderTheme& theme) const {
  QVector<QRectF> rects;
  if (definitionSlots_.isEmpty() || startOffset == endOffset || !definition_.markerRange.isValid()) {
    return rects;
  }

  const qsizetype blockStart = definition_.markerRange.start;
  const qsizetype sourceStart = blockStart + qMin(startOffset, endOffset);
  const qsizetype sourceEnd = blockStart + qMax(startOffset, endOffset);
  const QFontMetricsF metrics(theme.paragraphFont());
  for (const DefinitionSlotLayout& slot : definitionSlots_) {
    const qsizetype rangeStart = qMax(sourceStart, slot.sourceStart);
    const qsizetype rangeEnd = qMin(sourceEnd, slot.sourceEnd);
    if (rangeEnd <= rangeStart) {
      continue;
    }
    const QString text = slot.text;
    const qsizetype localStart = qBound<qsizetype>(0, rangeStart - slot.sourceStart, text.size());
    const qsizetype localEnd = qBound<qsizetype>(localStart, rangeEnd - slot.sourceStart, text.size());
    const qreal x1 = slot.rect.left() + metrics.horizontalAdvance(text.left(localStart));
    const qreal x2 = slot.rect.left() + metrics.horizontalAdvance(text.left(localEnd));
    rects.push_back(QRectF(x1, slot.rect.top(), qMax<qreal>(1.0, x2 - x1), slot.rect.height()));
  }

  // Fill gaps between slot rects so that syntax tokens between selected
  // slots also receive a continuous selection highlight.
  if (rects.size() > 1) {
    std::sort(rects.begin(), rects.end(), [](const QRectF& a, const QRectF& b) {
      return a.left() < b.left();
    });
    QVector<QRectF> continuous;
    continuous.push_back(rects.first());
    for (int i = 1; i < rects.size(); ++i) {
      QRectF& prev = continuous.last();
      const QRectF& curr = rects[i];
      if (curr.left() <= prev.right() + 0.5) {
        prev.setRight(qMax(prev.right(), curr.right()));
      } else {
        prev.setRight(curr.left());  // Extend to cover syntax tokens between slots
        continuous.push_back(curr);
      }
    }
    rects = std::move(continuous);
  }

  return rects;
}

}  // namespace muffin
