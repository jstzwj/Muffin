#include "render/BlockLayout.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "document/BlockPredicates.h"
#include "document/SourceRangeUtil.h"
#include "render/DecorationPainter.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QSettings>
#include <QTextLayout>
#include <QTextOption>

#include <cmath>
#include <limits>
#include <utility>

namespace muffin {
namespace {

// markdown/codeBlockWrap (default on): whether code-fence source lines soft-wrap. Math/HTML literal
// blocks always wrap regardless of this setting.
bool codeBlockWrapEnabled() {
  return QSettings().value(QStringLiteral("markdown/codeBlockWrap"), true).toBool();
}

struct LiteralVisualLine {
  qsizetype start = 0;
  qsizetype length = 0;
  QRectF rect;
};

QVector<LiteralVisualLine> layoutLiteralVisualLines(const QString& literal, const QFont& font, qreal width, qreal lineHeight, bool wrap) {
  QVector<LiteralVisualLine> visualLines;
  const QStringList physicalLines = literal.isEmpty() ? QStringList{QString()} : literal.split(QLatin1Char('\n'));
  const qreal lineWidth = qMax<qreal>(1.0, width);
  const qreal fallbackHeight = qMax<qreal>(14.0, lineHeight);
  QTextOption option;
  option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);

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

qsizetype literalOffsetForPoint(const QString& literal, QPointF localPos, const QFont& font, qreal width, qreal lineHeight, bool wrap, qreal xOffset) {
  const QFontMetricsF metrics(font);
  // Map the view-space click x into content space. paintCodeFence draws the (NoWrap) line with
  // painter.translate(-offset), so a view-space x reveals content at advance (viewX + offset):
  // undoing that leftward shift means ADDING the offset, not subtracting. (Subtracting mapped a
  // scrolled-right click back toward the line start — a click at the right edge resolved to offset 0.)
  localPos.setX(localPos.x() + xOffset);
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal, font, width, lineHeight, wrap);
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

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin, qreal width, qreal lineHeight, bool wrap) {
  const QFontMetricsF metrics(font);
  lineHeight = qMax<qreal>(14.0, lineHeight);
  offset = qBound<qsizetype>(0, offset, literal.size());
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal, font, width, lineHeight, wrap);
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
    qreal maxWidth,
    bool wrap) {
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
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal, font, maxWidth, lineHeight, wrap);
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

qreal literalTextHeight(const QString& literal, const QFont& font, qreal width, qreal lineHeight, bool wrap = true) {
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal, font, width, lineHeight, wrap);
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

void BlockLayout::setMathDelimiter(MathDelimiter delimiter) {
  mathDelimiter_ = delimiter;
}

MathDelimiter BlockLayout::mathDelimiter() const {
  return mathDelimiter_;
}

void BlockLayout::setHtmlLayout(std::shared_ptr<html::HtmlLayoutResult> layout) {
  htmlLayout_ = std::move(layout);
}

const html::HtmlLayoutResult* BlockLayout::htmlLayout() const {
  return htmlLayout_.get();
}

void BlockLayout::setLiteralEditing(bool editing) {
  literalEditing_ = editing;
}

bool BlockLayout::literalEditing() const {
  return literalEditing_;
}

void BlockLayout::setLineNumberGutterWidth(qreal width) {
  lineNumberGutterWidth_ = width;
}

qreal BlockLayout::lineNumberGutterWidth() const {
  return lineNumberGutterWidth_;
}

void BlockLayout::setCodeMaxLineWidth(qreal width) {
  codeMaxLineWidth_ = width;
}

qreal BlockLayout::codeMaxLineWidth() const {
  return codeMaxLineWidth_;
}

qreal BlockLayout::scrollBarStripHeight(const RenderTheme& theme) {
  return qMax<qreal>(8.0, theme.codeLineHeight() * 0.45);
}

QRectF BlockLayout::literalContentRect(const RenderTheme& theme) const {
  if (type_ == BlockType::MathBlock && literalEditing_) {
    return mathEditorSourceRect(theme);
  }
  QRectF content = rect_.marginsRemoved(theme.codePadding());
  // Reserve a left gutter for line numbers in code fences (set at build time); 0 for other blocks.
  if (type_ == BlockType::CodeFence && lineNumberGutterWidth_ > 0.0) {
    content.adjust(lineNumberGutterWidth_, 0, 0, 0);
  }
  return content;
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

void BlockLayout::setListContentIndent(qreal indent) {
  listContentIndent_ = indent;
}

qreal BlockLayout::listContentIndent() const {
  return listContentIndent_;
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

void BlockLayout::setAlertKind(AlertKind kind) {
  alertKind_ = kind;
}

AlertKind BlockLayout::alertKind() const {
  return alertKind_;
}

QRectF BlockLayout::taskCheckboxRect(const RenderTheme& theme) const {
  const qreal markerX = rect_.left() + theme.listIndent() * 0.45;
  const QFontMetricsF metrics(theme.paragraphFont());
  const qreal top = rect_.top() + qMax<qreal>(2.0, (metrics.height() - 13.0) / 2.0);
  return QRectF(markerX, top, 13.0, 13.0);
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

void BlockLayout::paint(QPainter& painter, const RenderTheme& theme, qreal scrollY, const CodeFenceScrollController* scroll) const {
  paintSelf(painter, theme, scrollY, scroll);
  for (const auto& child : children_) {
    child->paint(painter, theme, scrollY, scroll);
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

HitTestResult BlockLayout::hitTest(QPointF documentPos, const RenderTheme& theme, const CodeFenceScrollController* scroll) const {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    const BlockLayout& child = **it;
    if (child.rect().adjusted(-theme.blockSpacing(), -theme.blockSpacing(), theme.blockSpacing(), theme.blockSpacing()).contains(documentPos)) {
      HitTestResult childHit = child.hitTest(documentPos, theme, scroll);
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
  return hitSelf(documentPos, theme, scroll);
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

void BlockLayout::paintSelf(QPainter& painter, const RenderTheme& theme, qreal scrollY, const CodeFenceScrollController* scroll) const {
  const QRectF viewRect = rect_.translated(0, -scrollY);

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      if (inlineLayout_) {
        if (type_ == BlockType::Heading) {
          // CSS element background gradient (e.g. phycat's h2 radial "fusion glass"
          // glow) sits behind the heading text. When the heading declares
          // `width: fit-content`, the glow/tint box shrinks to the text + heading
          // padding (a left-aligned pill, or centred for `margin: auto` + center
          // text) instead of spanning the full block — matching how a browser
          // sizes an inline-shrink heading. Block rect stays full width, so this
          // is paint-only: layout, hit-test and selection are unaffected.
          const QMarginsF hpad = theme.headingPadding(headingLevel_);
          const qreal headingPaintX = viewRect.left() + hpad.left();
          const QRectF textBounds = inlineLayout_->visualTextBounds().translated(headingPaintX, viewRect.top());
          QRectF bgRect = viewRect;
          if (theme.headingFitContent(headingLevel_) && textBounds.isValid() && inlineLayout_->height() > 0.0) {
            const qreal left = qBound(viewRect.left(), textBounds.left() - hpad.left(), viewRect.right());
            const qreal right = qBound(left, textBounds.right() + hpad.right(), viewRect.right());
            bgRect = QRectF(left, viewRect.top(), qMax<qreal>(1.0, right - left), inlineLayout_->height());
          }
          DecorationPainter::paintElementBackground(
              painter, theme, QStringLiteral("h%1").arg(headingLevel_), bgRect);
        }
        if (hasListMarker()) {
          painter.save();
          painter.setFont(theme.paragraphFont());
          painter.setPen(theme.textColor());
          const QFontMetricsF metrics(painter.font());
          // Baseline of the first text line, including the line-height centering
          // offset — markers must ride this baseline or they drift above the text
          // (and the caret) under a large theme line-height.
          const qreal firstBaseline = inlineLayout_->firstLineBaselineY();
          const qreal markerX = viewRect.left() + theme.listIndent() * 0.45;
          const qreal contentX = viewRect.left() + listContentIndent_;
          // Gap between a marker glyph and the content, scaled with the theme indent.
          const qreal markerGap = theme.listIndent() * 0.2;
          if (taskListItem_) {
            const QRectF box = taskCheckboxRect(theme).translated(0, -scrollY);
            painter.setBrush(theme.backgroundColor());
            painter.setPen(QPen(theme.tableBorderColor(), 1));
            painter.drawRoundedRect(box, 2, 2);
            if (taskChecked_) {
              painter.setPen(QPen(theme.linkColor(), 1.8));
              painter.drawLine(QPointF(box.left() + 3, box.center().y()), QPointF(box.left() + 5.5, box.bottom() - 3));
              painter.drawLine(QPointF(box.left() + 5.5, box.bottom() - 3), QPointF(box.right() - 3, box.top() + 3));
            }
          } else if (listMarkerKind_ == ListMarkerKind::OrderedText) {
            // Right-align the number to the content gutter so wide multi-digit markers
            // (e.g. "34.", "100.") never overlap the content or the caret.
            const qreal orderedX = contentX - markerGap - metrics.horizontalAdvance(listMarker_);
            painter.drawText(QPointF(orderedX, viewRect.top() + firstBaseline), listMarker_);
          } else {
            const QPointF markerCenter(markerX + metrics.horizontalAdvance(QStringLiteral("0")) * 0.35, viewRect.top() + firstBaseline - metrics.xHeight() * 0.45);
            paintUnorderedListMarker(painter, listMarkerKind_, markerCenter, metrics.height(), theme.textColor());
          }
          painter.restore();
          inlineLayout_->paint(painter, QPointF(contentX, viewRect.top()));
        } else {
          // For headings with left padding (+ inline ::before advance), offset the
          // paint position so text aligns with the wrapped width used during layout.
          // Paragraph/List items have no padding/advance.
          const qreal paintX = type_ == BlockType::Heading
                                   ? viewRect.left() + theme.headingPadding(headingLevel_).left() + theme.headingBeforeAdvance(headingLevel_)
                                   : viewRect.left();
          inlineLayout_->paint(painter, QPointF(paintX, viewRect.top()));
        }
        if (!placeholderText_.isEmpty()) {
          painter.save();
          painter.setFont(theme.paragraphFont());
          painter.setPen(theme.mutedTextColor());
          const qreal placeholderX = type_ == BlockType::Heading
                                         ? viewRect.left() + theme.headingPadding(headingLevel_).left() + theme.headingBeforeAdvance(headingLevel_)
                                         : viewRect.left();
          // Align with the first text line's baseline (line-height aware) so the
          // placeholder sits where the caret and typed text will, not at the raw
          // block top + ascent.
          painter.drawText(QPointF(placeholderX, viewRect.top() + inlineLayout_->firstLineBaselineY()), placeholderText_);
          painter.restore();
        }
        if (type_ == BlockType::Heading) {
          painter.save();
          const QMarginsF pad = theme.headingPadding(headingLevel_);
          const QColor leftColor = theme.headingBorderLeftColor(headingLevel_);
          const qreal leftWidth = theme.headingBorderLeftWidth(headingLevel_);
          if (leftColor.isValid() && leftWidth > 0.0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(leftColor);
            painter.drawRect(QRectF(viewRect.left() - pad.left(), viewRect.top(), leftWidth, inlineLayout_->height()));
          }
          const QColor bottomColor = theme.headingBorderBottomColor(headingLevel_);
          const qreal bottomWidth = theme.headingBorderBottomWidth(headingLevel_);
          if (bottomColor.isValid() && bottomWidth > 0.0) {
            painter.setPen(QPen(bottomColor, bottomWidth));
            const qreal y = viewRect.top() + inlineLayout_->height() + theme.blockSpacing() * 0.15;
            painter.drawLine(QPointF(viewRect.left() - pad.left(), y), QPointF(viewRect.right(), y));
          }
          painter.restore();
          // CSS ::before/::after decorations: a trailing SVG icon after the heading
          // text, an underline-gradient bar, etc.
          DecorationPainter::PaintContext dctx;
          dctx.headingLevel = headingLevel_;
          dctx.font = theme.headingFont(headingLevel_);
          const qreal headingPaintX = viewRect.left() + theme.headingPadding(headingLevel_).left();
          const qreal beforeAdvance = theme.headingBeforeAdvance(headingLevel_);
          // textBounds reflects the shifted text origin so ::after anchors to the
          // real text end; contentLeftX (== headingPaintX) lets an inline ::before
          // marker place itself in the reserved [contentLeftX, textStart) zone.
          const QRectF textBounds = inlineLayout_->visualTextBounds().translated(headingPaintX + beforeAdvance, viewRect.top());
          dctx.textBounds = textBounds;
          dctx.contentLeftX = headingPaintX;
          dctx.textStart = textBounds.isValid() ? textBounds.topLeft() : QPointF(headingPaintX + beforeAdvance, viewRect.top());
          dctx.textEnd = textBounds.isValid() ? QPointF(textBounds.right(), textBounds.top())
                                              : QPointF(headingPaintX + beforeAdvance + inlineLayout_->size().width(), viewRect.top());
          DecorationPainter::paintPseudoDecorations(
              painter, theme, QStringLiteral("h%1").arg(headingLevel_), viewRect, dctx);
        }
      }
      break;
    case BlockType::BlockQuote: {
      painter.save();
      painter.setPen(Qt::NoPen);
      if (alertKind_ != AlertKind::None) {
        // GitHub-style alert card: tinted background (semi-transparent accent, so it adapts to the
        // page background of any theme) with a solid accent left bar. v1 keeps the `[!KIND]` first
        // line visible; an icon/title row is a later enhancement.
        QColor tint = theme.alertAccent(alertKind_);
        tint.setAlphaF(0.10f);
        painter.setBrush(tint);
        painter.drawRoundedRect(viewRect, 6.0, 6.0);
        painter.setBrush(theme.alertAccent(alertKind_));
        painter.drawRoundedRect(QRectF(viewRect.left(), viewRect.top() + 3.0, 4.0, viewRect.height() - 6.0),
                                2.0, 2.0);
      } else if (theme.blockquoteBoxThemed()) {
        // Phase 4a: CSS-driven quote box — themed radius/background/border. The
        // content is already inset by the theme's padding (buildContainer), so the
        // box paints edge-to-edge over the block rect.
        const qreal r = theme.blockquoteBorderRadius();
        if (theme.blockquoteBackgroundColor().isValid()) {
          painter.setBrush(theme.blockquoteBackgroundColor());
          painter.setPen(Qt::NoPen);
          painter.drawRoundedRect(viewRect, r, r);
        }
        if (theme.blockquoteBorderWidth() > 0.0 && theme.blockquoteBorderColor().isValid()) {
          painter.setBrush(Qt::NoBrush);
          painter.setPen(QPen(theme.blockquoteBorderColor(), theme.blockquoteBorderWidth()));
          painter.drawRoundedRect(viewRect, r, r);
        }
        // CSS ::before content (e.g. a 💡 glyph) at the quote's top-left.
        DecorationPainter::PaintContext qctx;
        qctx.font = theme.paragraphFont();
        DecorationPainter::paintPseudoDecorations(
            painter, theme, QStringLiteral("blockquote"), viewRect, qctx);
      } else {
        // Optional quote fill (CSS themes that tint blockquote backgrounds).
        if (theme.blockquoteBackgroundColor().isValid()) {
          painter.setBrush(theme.blockquoteBackgroundColor());
          painter.drawRoundedRect(viewRect, 6.0, 6.0);
        }
        painter.setBrush(theme.quoteBorderColor());
        painter.drawRect(QRectF(viewRect.left(), viewRect.top(), 4, viewRect.height()));
        // CSS ::before content (e.g. a 💡 glyph) at the quote's top-left, clear of
        // the 4px accent border.
        DecorationPainter::PaintContext qctx;
        qctx.font = theme.paragraphFont();
        DecorationPainter::paintPseudoDecorations(
            painter, theme, QStringLiteral("blockquote"), viewRect.adjusted(8, 0, -4, 0), qctx);
      }
      painter.restore();
      break;
    }
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
      paintCodeFence(painter, theme, viewRect, scroll);
      break;
    case BlockType::MathBlock: {
      painter.save();
      painter.setPen(theme.codeBorderColor());
      painter.setBrush(theme.codeBackgroundColor());
      if (theme.codeBlockBoxThemed() && theme.codeBlockBorderRadius() > 0.0) {
        painter.drawRoundedRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5), theme.codeBlockBorderRadius(), theme.codeBlockBorderRadius());
      } else {
        painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
      }
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
        const QString openMarker = mathOpeningDelimiter(mathDelimiter_);
        const QString closeMarker = mathClosingDelimiter(mathDelimiter_);
        painter.drawText(QPointF(sourceRect.left(), viewRect.top() + padding.top() + codeMetrics.ascent()), openMarker);
        paintLiteralSource(painter, theme, sourceRect, highlightMathTex(literal_), true);
        painter.drawText(QPointF(sourceRect.left(), sourceRect.bottom() + codeMetrics.ascent()), closeMarker);
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
      if (literalEditing_) {
        painter.save();
        painter.setPen(theme.codeBorderColor());
        painter.setBrush(theme.codeBackgroundColor());
        if (theme.codeBlockBoxThemed() && theme.codeBlockBorderRadius() > 0.0) {
          painter.drawRoundedRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5), theme.codeBlockBorderRadius(), theme.codeBlockBorderRadius());
        } else {
          painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
        }
        // HTML literal source always wraps (unlike code fences, which honour
        // markdown/codeBlockWrap and gain a horizontal scrollbar instead). This
        // must match the build/estimate/selection/hit-test paths, all of which
        // treat HtmlBlock as wrap=true — otherwise the reserved height (wrapped)
        // and the painted text (NoWrap, clipped, no scrollbar) disagree.
        paintLiteralSource(painter, theme, viewRect.marginsRemoved(theme.codePadding()), codeHighlightSpans_, true);
        painter.restore();
      } else if (htmlLayout_ && htmlLayout_->valid()) {
        htmlLayout_->paint(painter, viewRect.marginsRemoved(theme.codePadding()).topLeft());
      } else {
        // Fallback: the HTML did not render (invalid) or rendered with no visible content
        // (just <div>/<br>/whitespace). Show the source, syntax-highlighted like edit mode.
        // codeHighlightSpans_ is populated for this case by buildLiteralBlock().
        painter.save();
        painter.setPen(theme.codeBorderColor());
        painter.setBrush(theme.codeBackgroundColor());
        painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
        paintLiteralSource(painter, theme, viewRect.marginsRemoved(theme.codePadding()), codeHighlightSpans_, true);
        painter.restore();
      }
      break;
    }
    case BlockType::ThematicBreak: {
      painter.save();
      if (DecorationPainter::hasElementBackground(theme, QStringLiteral("hr"))) {
        // CSS hr background-image gradient (e.g. phycat's fading rule).
        DecorationPainter::paintHrGradient(painter, theme, viewRect);
      } else {
        painter.setPen(QPen(theme.codeBorderColor(), 1));
        const qreal y = viewRect.center().y();
        painter.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
      }
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
      return literalSelectionRects(selection.startOffset(), selection.endOffset(), theme);
    case BlockType::HtmlBlock:
      if (literalEditing_) {
        return literalSelectionRects(selection.startOffset(), selection.endOffset(), theme);
      }
      rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
      return rects;
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

  const qreal textLeft = hasListMarker() ? rect_.left() + listContentIndent_
                                         : (type_ == BlockType::Heading ? rect_.left() + theme.headingPadding(headingLevel_).left()
                                                                           : rect_.left());
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
      return literalSelectionRects(startOffset, endOffset, theme);
    case BlockType::HtmlBlock:
      if (literalEditing_) {
        return literalSelectionRects(startOffset, endOffset, theme);
      }
      if (startOffset != endOffset) {
        rects.push_back(rect_.adjusted(-1.0, -1.0, 1.0, 1.0));
      }
      return rects;
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

  const qreal textLeft = hasListMarker() ? rect_.left() + listContentIndent_
                                         : (type_ == BlockType::Heading ? rect_.left() + theme.headingPadding(headingLevel_).left()
                                                                           : rect_.left());
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
  // Code fences honor the wrap setting; FrontMatter always wraps (matching hit-test/paint). The
  // rects are returned in document space at the content's natural x — when a code fence scrolls
  // horizontally the view applies the offset and clips to the visible text window (just like the
  // caret via effectiveCursorRect). Previously this hard-coded wrap=true, so a selection inside an
  // overflowing NoWrap line wrapped to a second visual row that didn't exist in the paint.
  const bool wrap = type_ == BlockType::CodeFence ? codeBlockWrapEnabled() : true;
  QVector<QRectF> rects =
      literalSelectionRectsForRange(literal_, startOffset, endOffset, font, theme.codeLineHeight(), contentRect.topLeft(), contentRect.width(), wrap);
  for (QRectF& rect : rects) {
    rect = rect.adjusted(-1.0, 0, 1.0, 0);
    if (wrap) {
      rect = rect.intersected(contentRect.adjusted(0, 0, 1, 0));
    }
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

void BlockLayout::paintCodeFence(QPainter& painter, const RenderTheme& theme, QRectF viewRect, const CodeFenceScrollController* scroll) const {
  painter.save();
  painter.setPen(theme.codeBorderColor());
  painter.setBrush(theme.codeBlockBackgroundColor());
  // Phase 4b: a CSS-themed code fence rounds its box; legacy fences stay square.
  if (theme.codeBlockBoxThemed() && theme.codeBlockBorderRadius() > 0.0) {
    painter.drawRoundedRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5), theme.codeBlockBorderRadius(), theme.codeBlockBorderRadius());
  } else {
    painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
  }
  // literalContentRect is document-space (rect_-based); shift it into view space the same way the
  // caller built viewRect (rect_.translated(0, -scrollY)) so the code text lands inside the box.
  const QRectF contentRect = literalContentRect(theme).translated(0, viewRect.top() - rect_.top());
  const bool wrap = codeBlockWrapEnabled();
  const qreal maxLineWidth = codeMaxLineWidth_;
  // Horizontal scroll applies only to code fences (not FrontMatter) with wrap off and a line wider
  // than the content area. The controller holds the per-block offset, surviving layout rebuilds.
  const bool scrollable = type_ == BlockType::CodeFence && !wrap && maxLineWidth > contentRect.width() + 0.5;
  const qreal offset = (scroll != nullptr && scrollable) ? scroll->offsetFor(id_) : 0.0;
  const qreal stripH = scrollable ? scrollBarStripHeight(theme) : 0.0;
  const QRectF textRect = contentRect.adjusted(0, 0, 0, -stripH);

  if (lineNumberGutterWidth_ > 0.0) {
    paintCodeLineNumbers(painter, theme, textRect);
  }

  if (scrollable) {
    painter.save();
    // Clip in view space FIRST, then translate: NoWrap lines are drawn at natural width and must
    // be cut at the block's right edge instead of overflowing into the page margin.
    painter.setClipRect(textRect);
    painter.translate(QPointF(-offset, 0.0));
    // Draw at the base text rect (NOT +offset). The painter's translate(-offset) is what actually
    // moves the content: a char at world x appears at device x-offset, so as `offset` grows the line
    // scrolls left and the clip reveals its right portion. (Earlier code passed textRect.translated
    // (offset,0), which drew at textRect.left()+offset — that +offset cancelled the translate and
    // froze the content in place, so scrolling moved only the thumb, never the text.)
    paintLiteralSource(painter, theme, textRect, codeHighlightSpans_, false);
    painter.restore();
    paintCodeFenceScrollBar(painter, theme, contentRect, offset, maxLineWidth);
  } else {
    paintLiteralSource(painter, theme, contentRect, codeHighlightSpans_, wrap);
  }
  painter.restore();
}

void BlockLayout::paintCodeFenceScrollBar(QPainter& painter, const RenderTheme& theme, QRectF contentRect, qreal offset, qreal maxLineWidth) const {
  const qreal stripH = scrollBarStripHeight(theme);
  const QRectF strip(contentRect.left(), contentRect.bottom() - stripH, contentRect.width(), stripH);
  const qreal visibleW = contentRect.width();
  const qreal totalW = qMax(maxLineWidth, visibleW);
  const qreal maxOff = totalW - visibleW;
  const qreal ratio = visibleW / totalW;
  const qreal thumbW = qMax<qreal>(24.0, strip.width() * ratio);
  const qreal thumbX = strip.left() + (maxOff > 0.0 ? (offset / maxOff) * (strip.width() - thumbW) : 0.0);
  const QRectF track = strip.adjusted(0.5, 0.5, -0.5, -0.5);
  const QRectF thumb(thumbX, strip.top() + 1.0, thumbW, strip.height() - 2.0);
  painter.setPen(Qt::NoPen);
  painter.setBrush(theme.codeBorderColor());
  painter.drawRoundedRect(track, 3.0, 3.0);
  painter.setBrush(theme.mutedTextColor());
  painter.drawRoundedRect(thumb, 3.0, 3.0);
}

void BlockLayout::paintLiteralSource(QPainter& painter, const RenderTheme& theme, QRectF contentRect, const QVector<CodeHighlightSpan>& spans, bool wrap) const {
  const QStringList lines = literal_.isEmpty() ? QStringList{QString()} : literal_.split(QLatin1Char('\n'));
  QTextCharFormat baseFormat;
  baseFormat.setForeground(theme.textColor());
  QTextOption option;
  option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);

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

void BlockLayout::paintCodeLineNumbers(QPainter& painter, const RenderTheme& theme, const QRectF& codeRect) const {
  const QStringList lines = literal_.isEmpty() ? QStringList{QString()} : literal_.split(QLatin1Char('\n'));
  const QFont codeFont = theme.codeFont();
  const QFontMetricsF metrics(codeFont);
  const qreal codeLineHeight = theme.codeLineHeight();
  const qreal digitWidth = metrics.horizontalAdvance(QStringLiteral("8"));
  QTextOption option;
  option.setWrapMode(codeBlockWrapEnabled() ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);

  painter.setFont(codeFont);
  painter.setPen(theme.mutedTextColor());
  // Right-align the number leaving a 2-char gap before the code. The gutter
  // (codeLineNumberGutterWidth) reserves digits + 3 chars so this fits with a 1-char left padding.
  const qreal numRightX = codeRect.left() - 2.0 * digitWidth;
  qreal y = codeRect.top();
  int number = 1;
  for (const QString& sourceLine : lines) {
    // Mirror paintLiteralSource's per-line layout so a number stays aligned to its source line even
    // when that line soft-wraps across multiple visual lines.
    const QString lineText = sourceLine.isEmpty() ? QStringLiteral(" ") : sourceLine;
    QTextLayout layout(lineText, codeFont);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal lineY = 0;
    while (true) {
      QTextLine textLine = layout.createLine();
      if (!textLine.isValid()) {
        break;
      }
      textLine.setLineWidth(qMax<qreal>(1.0, codeRect.width()));
      const qreal visualHeight = qMax<qreal>(codeLineHeight, textLine.height());
      textLine.setPosition(QPointF(0, lineY + (visualHeight - textLine.height()) * 0.5));
      lineY += visualHeight;
    }
    layout.endLayout();
    const qreal slotHeight = qMax<qreal>(lineY, codeLineHeight);
    const QString num = QString::number(number++);
    const qreal numWidth = metrics.horizontalAdvance(num);
    painter.drawText(QRectF(numRightX - numWidth, y, numWidth, slotHeight), Qt::AlignVCenter | Qt::AlignRight, num);
    y += slotHeight;
  }
}

HitTestResult BlockLayout::hitSelf(QPointF documentPos, const RenderTheme& theme, const CodeFenceScrollController* scroll) const {
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
        const qreal textLeft = hasListMarker() ? rect_.left() + listContentIndent_
                                               : (type_ == BlockType::Heading ? rect_.left() + theme.headingPadding(headingLevel_).left()
                                                                                 : rect_.left());
        const QRectF textRect(textLeft, rect_.top(), qMax<qreal>(1.0, rect_.right() - textLeft), rect_.height());
        if (hasListMarker() && documentPos.x() < textLeft) {
          result.zone = HitTestResult::Zone::Marker;
          result.cursorRect = QRectF(textLeft, rect_.top(), 1.0, rect_.height());
          // A task item's whole gutter is the checkbox affordance; widening the
          // hit rect a little makes the 13px box comfortably clickable.
          if (taskListItem_ && taskCheckboxRect(theme).adjusted(-3.0, -3.0, 3.0, 3.0).contains(documentPos)) {
            result.taskCheckboxHit = true;
          }
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
    case BlockType::CodeFence: {
      const QRectF contentRect = literalContentRect(theme);
      const bool wrap = codeBlockWrapEnabled();
      const bool scrollable = type_ == BlockType::CodeFence && !wrap && codeMaxLineWidth_ > contentRect.width() + 0.5;
      const qreal offset = (scroll != nullptr && scrollable) ? scroll->offsetFor(id_) : 0.0;
      // A click on the reserved bottom scrollbar strip drives the horizontal thumb instead of
      // placing the caret.
      if (scrollable) {
        const qreal stripH = scrollBarStripHeight(theme);
        const QRectF strip(contentRect.left(), contentRect.bottom() - stripH, contentRect.width(), stripH);
        if (strip.contains(documentPos)) {
          result.zone = HitTestResult::Zone::CodeHorizontalBar;
          result.cursorRect = strip;  // document-space strip; EditorView maps it to the viewport for dragging
          return result;
        }
      }
      result.zone = type_ == BlockType::FrontMatter ? HitTestResult::Zone::FrontMatter : HitTestResult::Zone::Code;
      // Code fences honor the wrap setting (and subtract the horizontal scroll offset when mapping
      // a click); FrontMatter always wraps. Previously both hardcoded wrap, so click mapping was
      // wrong whenever code-block wrap was off.
      const bool codeWrap = type_ == BlockType::CodeFence ? wrap : true;
      result.textOffset = literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(),
                                                theme.codeFont(), contentRect.width(), theme.codeLineHeight(), codeWrap, offset);
      result.cursorRect = literalCursorRectForOffset(literal_, result.textOffset, theme.codeFont(),
                                                     contentRect.topLeft(), contentRect.width(), theme.codeLineHeight(), codeWrap);
      break;
    }
    case BlockType::MathBlock:
      result.zone = HitTestResult::Zone::Math;
      {
        if (literalEditing_) {
          const QRectF contentRect = mathEditorSourceRect(theme);
          result.textOffset =
              literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(), theme.codeFont(), contentRect.width(), theme.codeLineHeight(), true, 0.0);
          result.cursorRect =
              literalCursorRectForOffset(literal_, result.textOffset, theme.codeFont(), contentRect.topLeft(), contentRect.width(), theme.codeLineHeight(), true);
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
        if (literalEditing_) {
          const QRectF contentRect = rect_.marginsRemoved(theme.codePadding());
          result.textOffset =
              literalOffsetForPoint(literal_, documentPos - contentRect.topLeft(), theme.codeFont(), contentRect.width(), theme.codeLineHeight(), true, 0.0);
          result.cursorRect =
              literalCursorRectForOffset(literal_, result.textOffset, theme.codeFont(), contentRect.topLeft(), contentRect.width(), theme.codeLineHeight(), true);
        } else {
          if (htmlLayout_ && htmlLayout_->valid()) {
            const QRectF contentRect = rect_.marginsRemoved(theme.codePadding());
            const html::HtmlLayoutResult::HitResult htmlHit = htmlLayout_->hitTest(documentPos - contentRect.topLeft());
            result.linkHref = htmlHit.linkHref;
            result.imageSrc = htmlHit.imageSrc;
          }
          result.textOffset = documentPos.x() < rect_.center().x() ? 0 : literal_.size();
          result.cursorRect = QRectF(result.textOffset == 0 ? rect_.left() : rect_.right(), rect_.top(), 1.0, rect_.height());
        }
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
      // Phase 4c: a CSS-themed table rounds its cells; legacy tables stay square.
      if (theme.tableBoxThemed() && theme.tableBorderRadius() > 0.0) {
        painter.drawRoundedRect(cellRect.adjusted(0.5, 0.5, -0.5, -0.5), theme.tableBorderRadius(), theme.tableBorderRadius());
      } else {
        painter.drawRect(cellRect.adjusted(0.5, 0.5, -0.5, -0.5));
      }
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
