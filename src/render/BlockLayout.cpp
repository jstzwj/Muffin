#include "render/BlockLayout.h"
#include "render/RenderMetrics.h"

#include "blocks/code/CodeFenceScrollController.h"
#include "blocks/html/HtmlUrlSafety.h"
#include "document/BlockPredicates.h"
#include "document/SourceRangeUtil.h"
#include "mermaid/scene/FlowScenePainter.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/classdiagram/ClassScenePainter.h"
#include "mermaid/sequence/SequenceScenePainter.h"
#include "mermaid/state/StateScenePainter.h"
#include "render/DecorationPainter.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QSettings>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
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

qreal mermaidDiagnosticGap(const RenderTheme& theme) {
  return qMax<qreal>(6.0, theme.codeLineHeight() * 0.3);
}

qreal mermaidDiagnosticPadding(const RenderTheme& theme) {
  return qMax<qreal>(8.0, theme.codeLineHeight() * 0.4);
}

qreal mermaidDiagnosticIconSize(const RenderTheme& theme) {
  return qMax<qreal>(16.0, theme.codeLineHeight() * 0.9);
}

struct MermaidDiagnosticTextMetrics {
  QString header;
  QString body;
  qreal headerHeight = 0.0;
  qreal bodyHeight = 0.0;
  qreal gap = 0.0;

  qreal height() const { return headerHeight + gap + bodyHeight; }
};

MermaidDiagnosticTextMetrics measureMermaidDiagnosticText(
    const mermaid::MermaidDiagnostic& diagnostic, const RenderTheme& theme,
    qreal width) {
  MermaidDiagnosticTextMetrics result;
  result.header = mermaid::formatMermaidDiagnosticHeader(diagnostic);
  result.body = mermaid::formatMermaidDiagnosticBody(diagnostic);
  const QFont bodyFont = theme.codeFont();
  QFont headerFont = bodyFont;
  headerFont.setBold(true);
  const QFontMetricsF headerMetrics(headerFont);
  const QFontMetricsF bodyMetrics(bodyFont);
  const int flags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
  if (!result.header.isEmpty()) {
    const qreal measured = headerMetrics.boundingRect(
        QRectF(0.0, 0.0, qMax<qreal>(1.0, width), 100000.0),
        flags, result.header).height();
    result.headerHeight = qBound<qreal>(
        headerMetrics.lineSpacing(), qCeil(measured),
        headerMetrics.lineSpacing() * 2.0);
  }
  if (!result.body.isEmpty()) {
    const qreal measured = bodyMetrics.boundingRect(
        QRectF(0.0, 0.0, qMax<qreal>(1.0, width), 100000.0),
        flags, result.body).height();
    result.bodyHeight = qBound<qreal>(
        bodyMetrics.lineSpacing(), qCeil(measured),
        bodyMetrics.lineSpacing() * 6.0);
  }
  if (result.headerHeight > 0.0 && result.bodyHeight > 0.0) {
    result.gap = qMax<qreal>(2.0, bodyMetrics.lineSpacing() * 0.18);
  }
  return result;
}

QPair<qsizetype, qsizetype> mermaidHighlightRange(
    const mermaid::MermaidDiagnostic& diagnostic, const QString& literal) {
  if (!diagnostic.span.hasLocation() || literal.isEmpty()) return {-1, -1};
  const qsizetype caret = qBound<qsizetype>(
      0, diagnostic.span.offset, literal.size());
  const qsizetype start = caret == literal.size()
      ? literal.size() - 1
      : caret;
  const qsizetype requestedLength = qMax<qsizetype>(1, diagnostic.span.length);
  const qsizetype end = qMax<qsizetype>(
      start + 1,
      qMin<qsizetype>(literal.size(), caret + requestedLength));
  return {start, end};
}

struct LiteralVisualLine {
  qsizetype start = 0;
  qsizetype length = 0;
  QRectF rect;
};

// Single-entry memoization for layoutLiteralVisualLines. It is a pure function of
// (literal, font, width, lineHeight, wrap) — no member/theme state — but four helpers
// (literalOffsetForPoint / literalCursorRectForOffset / literalSelectionRectsForRange /
// literalTextHeight) call it for the SAME block within a single frame, each redoing the full
// per-line QTextLayout (beginLayout/createLine/endLayout). Cache the most recent result: the
// common case (repeated calls for one block) is an O(1) hit because QString == short-circuits
// on implicit-shared identity (the same literal_ object across calls shares its d-ptr). Full
// input equality (no hash) means there is zero collision risk — a stale entry is impossible.
// GUI-thread only: the literal render/hit-test path is single-threaded.
struct LiteralLayoutCache {
  QString literal;
  QFont font;
  qreal width = -1.0;
  qreal lineHeight = -1.0;
  bool wrap = false;
  QVector<LiteralVisualLine> visualLines;
  bool valid = false;
};
LiteralLayoutCache g_literalLayoutCache;

QVector<LiteralVisualLine> layoutLiteralVisualLines(const QString& literal, const QFont& font, qreal width, qreal lineHeight, bool wrap) {
  if (g_literalLayoutCache.valid && g_literalLayoutCache.width == width &&
      g_literalLayoutCache.lineHeight == lineHeight && g_literalLayoutCache.wrap == wrap &&
      g_literalLayoutCache.font == font && g_literalLayoutCache.literal == literal) {
    return g_literalLayoutCache.visualLines;
  }
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
  g_literalLayoutCache = {literal, font, width, lineHeight, wrap, visualLines, true};
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

void paintCssBox(QPainter& painter, const ThemeElementBoxStyle& box, const QColor& background, const QRectF& borderBox) {
  if (!borderBox.isValid()) { return; }
  painter.save();
  if (background.isValid()) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    if (box.borderRadius > 0.0) { painter.drawRoundedRect(borderBox, box.borderRadius, box.borderRadius); }
    else { painter.drawRect(borderBox); }
  }
  const auto drawSide = [&](qreal width, const QColor& color, const QLineF& line) {
    if (width <= 0.0 || !color.isValid()) { return; }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::SquareCap));
    painter.drawLine(line);
  };
  drawSide(box.borderTopWidth, box.borderTopColor,
           QLineF(QPointF(borderBox.left(), borderBox.top() + box.borderTopWidth * 0.5),
                  QPointF(borderBox.right(), borderBox.top() + box.borderTopWidth * 0.5)));
  drawSide(box.borderRightWidth, box.borderRightColor,
           QLineF(QPointF(borderBox.right() - box.borderRightWidth * 0.5, borderBox.top()),
                  QPointF(borderBox.right() - box.borderRightWidth * 0.5, borderBox.bottom())));
  drawSide(box.borderBottomWidth, box.borderBottomColor,
           QLineF(QPointF(borderBox.left(), borderBox.bottom() - box.borderBottomWidth * 0.5),
                  QPointF(borderBox.right(), borderBox.bottom() - box.borderBottomWidth * 0.5)));
  drawSide(box.borderLeftWidth, box.borderLeftColor,
           QLineF(QPointF(borderBox.left() + box.borderLeftWidth * 0.5, borderBox.top()),
                  QPointF(borderBox.left() + box.borderLeftWidth * 0.5, borderBox.bottom())));
  painter.restore();
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
  // A new rect invalidates any previously-set CSS box geometry. Today the builder
  // always re-sets both on a fresh block, but resetting here keeps the cached box
  // from going stale if a future path re-rects an existing block.
  cssBoxGeometry_.valid = false;
}

void BlockLayout::setCssBoxGeometry(CssBoxGeometry geometry) {
  cssBoxGeometry_ = std::move(geometry);
}

BlockLayout::CssBoxGeometry BlockLayout::cssBoxGeometry(const RenderTheme& theme) const {
  if (cssBoxGeometry_.valid) { return cssBoxGeometry_; }
  CssBoxGeometry g;
  g.flowRect = rect_;
  g.borderBox = rect_;
  g.paddingBox = rect_;
  g.contentBox = rect_;
  g.visualOverflow = rect_;
  g.inlineTextOrigin = QPointF(
      hasListMarker() ? rect_.left() + listContentIndent_
                      : (type_ == BlockType::Heading
                             ? rect_.left() + theme.headingPadding(headingLevel_).left() + theme.headingBeforeAdvance(headingLevel_)
                             : rect_.left()),
      rect_.top());
  g.valid = true;
  return g;
}

QRectF BlockLayout::cssBorderBox(const RenderTheme& theme) const {
  return cssBoxGeometry(theme).borderBox;
}

QPointF BlockLayout::inlineTextOrigin(const RenderTheme& theme) const {
  return cssBoxGeometry(theme).inlineTextOrigin;
}

QRectF BlockLayout::visualOverflowRect(const RenderTheme& theme) const {
  CssBoxGeometry g = cssBoxGeometry(theme);
  return g.visualOverflow.isValid() ? g.visualOverflow : g.borderBox;
}

void BlockLayout::translate(qreal dx, qreal dy) {
  rect_.translate(dx, dy);
  if (cssBoxGeometry_.valid) {
    cssBoxGeometry_.flowRect.translate(dx, dy);
    cssBoxGeometry_.borderBox.translate(dx, dy);
    cssBoxGeometry_.paddingBox.translate(dx, dy);
    cssBoxGeometry_.contentBox.translate(dx, dy);
    cssBoxGeometry_.visualOverflow.translate(dx, dy);
    cssBoxGeometry_.inlineTextOrigin += QPointF(dx, dy);
  }
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

void BlockLayout::setMermaidScene(
    std::shared_ptr<const muffin::mermaid::MermaidScene> scene,
    QSizeF naturalSize, mermaid::MermaidRenderMetadata metadata) {
  mermaidScene_ = std::move(scene);
  mermaidNaturalSize_ = naturalSize;
  mermaidMetadata_ = std::move(metadata);
}

void BlockLayout::setMermaidViewportCullingEnabled(bool enabled) {
  mermaidViewportCullingEnabled_ = enabled;
}

bool BlockLayout::mermaidViewportCullingEnabled() const {
  return mermaidViewportCullingEnabled_;
}

const muffin::mermaid::MermaidScene* BlockLayout::mermaidScene() const {
  return mermaidScene_.get();
}

QSizeF BlockLayout::mermaidNaturalSize() const {
  return mermaidNaturalSize_;
}

const mermaid::MermaidRenderMetadata& BlockLayout::mermaidMetadata() const {
  return mermaidMetadata_;
}

bool BlockLayout::hasAnimatedMermaid() const {
  if (mermaidScene_ && mermaidScene_->hasAnimation()) return true;
  return std::any_of(children_.cbegin(), children_.cend(),
                     [](const auto& child) {
                       return child && child->hasAnimatedMermaid();
                     });
}

void BlockLayout::setMermaidState(MermaidState state) {
  mermaidState_ = state;
}

BlockLayout::MermaidState BlockLayout::mermaidState() const {
  return mermaidState_;
}

void BlockLayout::setMermaidDiagnostic(
    mermaid::MermaidDiagnostic diagnostic) {
  mermaidDiagnostic_ = std::move(diagnostic);
  mermaidDiagnosticMessage_ =
      mermaid::formatMermaidDiagnostic(mermaidDiagnostic_).trimmed();
}

const mermaid::MermaidDiagnostic& BlockLayout::mermaidDiagnostic() const {
  return mermaidDiagnostic_;
}

const QString& BlockLayout::mermaidDiagnosticMessage() const {
  return mermaidDiagnosticMessage_;
}

qreal BlockLayout::mermaidDiagnosticFootprint(
    const mermaid::MermaidDiagnostic& diagnostic,
    const RenderTheme& theme, qreal width) {
  if (diagnostic.isEmpty()) return 0.0;
  const qreal padding = mermaidDiagnosticPadding(theme);
  const qreal iconSize = mermaidDiagnosticIconSize(theme);
  const qreal textWidth = qMax<qreal>(
      1.0, width - padding * 3.0 - iconSize);
  const MermaidDiagnosticTextMetrics text =
      measureMermaidDiagnosticText(diagnostic, theme, textWidth);
  const qreal panelHeight =
      qCeil(padding * 2.0 + qMax(iconSize, text.height()));
  return qCeil(mermaidDiagnosticGap(theme) + panelHeight);
}

bool BlockLayout::hasMermaidDiagnostic() const {
  return (mermaidState_ == MermaidState::Error ||
          mermaidState_ == MermaidState::Unsupported) &&
         !mermaidDiagnosticMessage_.isEmpty();
}

QRectF BlockLayout::mermaidCodeFenceRect(const RenderTheme& theme) const {
  if (!hasMermaidDiagnostic()) return rect_;
  QRectF sourceRect = rect_;
  sourceRect.setHeight(qMax<qreal>(
      1.0, rect_.height() - mermaidDiagnosticFootprint(
                                mermaidDiagnostic_, theme, rect_.width())));
  return sourceRect;
}

QRectF BlockLayout::mermaidDiagnosticRect(const RenderTheme& theme) const {
  if (!hasMermaidDiagnostic()) return {};
  const qreal footprint = mermaidDiagnosticFootprint(
      mermaidDiagnostic_, theme, rect_.width());
  const qreal panelHeight = qMax<qreal>(
      1.0, footprint - mermaidDiagnosticGap(theme));
  return QRectF(rect_.left(), rect_.bottom() - panelHeight,
                rect_.width(), panelHeight);
}

QVector<QRectF> BlockLayout::mermaidDiagnosticSourceRects(
    const RenderTheme& theme) const {
  if (mermaidState_ != MermaidState::Error) return {};
  const auto [start, end] = mermaidHighlightRange(
      mermaidDiagnostic_, literal_);
  if (start < 0 || end <= start) return {};
  return literalSelectionRects(start, end, theme);
}

bool BlockLayout::isMermaidRendered() const {
  return mermaidState_ == MermaidState::Ready && mermaidScene_ != nullptr;
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
  const QRectF literalRect =
      type_ == BlockType::CodeFence ? mermaidCodeFenceRect(theme) : rect_;
  QRectF content = literalRect.marginsRemoved(theme.codePadding());
  // Reserve a left gutter for line numbers in code fences (set at build time); 0 for other blocks.
  if (type_ == BlockType::CodeFence && lineNumberGutterWidth_ > 0.0) {
    content.adjust(lineNumberGutterWidth_, 0, 0, 0);
  }
  return content;
}

BlockLayout::LiteralLayoutParams BlockLayout::literalLayoutParams(const RenderTheme& theme) const {
  // Mirror buildLiteralBlock's per-type choices (BlockLayoutBuilder.cpp) so the visual-line queries
  // match the RENDERED lines. Note Math uses mathFont (not codeFont) and a metrics-derived line
  // height, exactly as the builder lays it out.
  LiteralLayoutParams params;
  params.width = literalContentRect(theme).width();
  params.wrap = type_ == BlockType::CodeFence ? codeBlockWrapEnabled() : true;
  if (type_ == BlockType::MathBlock) {
    params.font = theme.mathFont();
    params.lineHeight = qMax<qreal>(14.0, QFontMetricsF(theme.mathFont()).height());
  } else {
    params.font = theme.codeFont();
    params.lineHeight = theme.codeLineHeight();
  }
  return params;
}

int BlockLayout::literalVisualLineCount(const RenderTheme& theme) const {
  const LiteralLayoutParams params = literalLayoutParams(theme);
  return layoutLiteralVisualLines(literal_, params.font, params.width, params.lineHeight, params.wrap).size();
}

int BlockLayout::literalVisualLineIndexForOffset(qsizetype localOffset, const RenderTheme& theme) const {
  const LiteralLayoutParams params = literalLayoutParams(theme);
  localOffset = qBound<qsizetype>(0, localOffset, literal_.size());
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal_, params.font, params.width, params.lineHeight, params.wrap);
  for (int i = 0; i < lines.size(); ++i) {
    const qsizetype lineEnd = lines.at(i).start + lines.at(i).length;
    if (localOffset >= lines.at(i).start && localOffset <= lineEnd) {
      return i;
    }
  }
  return lines.isEmpty() ? -1 : lines.size() - 1;
}

qsizetype BlockLayout::literalOffsetAtVisualLineX(int lineIndex, qreal localX, const RenderTheme& theme) const {
  const LiteralLayoutParams params = literalLayoutParams(theme);
  const QVector<LiteralVisualLine> lines = layoutLiteralVisualLines(literal_, params.font, params.width, params.lineHeight, params.wrap);
  if (lineIndex < 0 || lineIndex >= lines.size()) {
    return 0;
  }
  const LiteralVisualLine& line = lines.at(lineIndex);
  // localX is already un-scrolled content-local, so pass xOffset = 0; seed the point's y with the
  // target line's centre so literalOffsetForPoint resolves to that line regardless of x.
  return literalOffsetForPoint(literal_, QPointF(localX, line.rect.center().y()), params.font, params.width,
                               params.lineHeight, params.wrap, /*xOffset=*/0.0);
}

QRectF BlockLayout::literalVisualCursorRect(qsizetype localOffset, const RenderTheme& theme) const {
  const LiteralLayoutParams params = literalLayoutParams(theme);
  const QPointF origin = literalContentRect(theme).topLeft();
  return literalCursorRectForOffset(literal_, localOffset, params.font, origin, params.width, params.lineHeight, params.wrap);
}

void BlockLayout::setHeadingLevel(int level) {
  headingLevel_ = level;
}

int BlockLayout::headingLevel() const {
  return headingLevel_;
}

void BlockLayout::setHeadingBeforeText(QString text) {
  headingBeforeText_ = std::move(text);
}

QString BlockLayout::headingBeforeText() const {
  return headingBeforeText_;
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

void BlockLayout::setIsToc(bool isToc) {
  isToc_ = isToc;
}

bool BlockLayout::isToc() const {
  return isToc_;
}

void BlockLayout::setTocEntries(QVector<TocEntryLayout> entries) {
  tocEntries_ = std::move(entries);
}

const QVector<BlockLayout::TocEntryLayout>& BlockLayout::tocEntries() const {
  return tocEntries_;
}

QRectF BlockLayout::taskCheckboxRect(const RenderTheme& theme) const {
  const qreal markerX = rect_.left() + theme.listIndent() * 0.45;
  const QFontMetricsF metrics(theme.paragraphFont());
  qreal top = rect_.top() + qMax<qreal>(2.0, (metrics.height() - 13.0) / 2.0);
  if (inlineLayout_) {
    const qreal firstBaseline = inlineLayout_->firstLineBaselineY();
    const qreal textCenter = rect_.top() + firstBaseline + (metrics.descent() - metrics.ascent()) * 0.5;
    top = textCenter - 13.0 * 0.5;
  }
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

void BlockLayout::paint(QPainter& painter, const RenderTheme& theme, qreal scrollY, const CodeFenceScrollController* scroll, BlockPaintState hover) const {
  paintSelf(painter, theme, scrollY, scroll, hover);
  for (const auto& child : children_) {
    child->paint(painter, theme, scrollY, scroll, hover);
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

HitTestResult BlockLayout::hitTest(
    QPointF documentPos, const RenderTheme& theme,
    const CodeFenceScrollController* scroll,
    const QSet<QString>* openSequenceMenus) const {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    const BlockLayout& child = **it;
    if (child.rect().adjusted(-theme.blockSpacing(), -theme.blockSpacing(), theme.blockSpacing(), theme.blockSpacing()).contains(documentPos)) {
      HitTestResult childHit =
          child.hitTest(documentPos, theme, scroll, openSequenceMenus);
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
  return hitSelf(documentPos, theme, scroll, openSequenceMenus);
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

void BlockLayout::paintSelf(QPainter& painter, const RenderTheme& theme, qreal scrollY, const CodeFenceScrollController* scroll, BlockPaintState hover) const {
  const QRectF viewRect = rect_.translated(0, -scrollY);

  switch (type_) {
    case BlockType::Heading:
    case BlockType::ListItem:
      paintInlineBlock(painter, theme, viewRect, scrollY, hover);
      break;
    case BlockType::Paragraph:
      // A `[TOC]` paragraph paints a generated heading list while the caret is
      // elsewhere; when the caret is in the block the builder rebuilds it as a
      // normal paragraph (isToc_ false), so the literal "[TOC]" shows for editing.
      if (isToc_) {
        paintToc(painter, theme, viewRect);
      } else {
        paintInlineBlock(painter, theme, viewRect, scrollY, hover);
      }
      break;
    case BlockType::BlockQuote:
      paintBlockQuote(painter, theme, viewRect, scrollY);
      break;
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
      if (isMermaidRendered())
        paintMermaidDiagram(painter, theme, viewRect, hover);
      else {
        const QRectF codeRect =
            mermaidCodeFenceRect(theme).translated(0, -scrollY);
        paintCodeFence(painter, theme, codeRect, scroll);
        if (hasMermaidDiagnostic())
          paintMermaidDiagnostic(painter, theme, viewRect);
      }
      break;
    case BlockType::MathBlock:
      paintMathBlock(painter, theme, viewRect, scrollY);
      break;
    case BlockType::HtmlBlock:
      paintHtmlBlock(painter, theme, viewRect);
      break;
    case BlockType::ThematicBreak:
      paintThematicBreak(painter, theme, viewRect);
      break;
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

void BlockLayout::paintInlineBlock(QPainter& painter, const RenderTheme& theme, QRectF viewRect, qreal scrollY, BlockPaintState hover) const {
  if (inlineLayout_) {
    if (type_ == BlockType::Heading) {
      // CSS element background gradient (e.g. phycat's h2 radial "fusion glass"
      // glow) sits behind the heading text. Its rect comes from the same shared
      // CSS border box that hover/hit-test/selection use, so fit-content pills do
      // not drift into full-row effects.
      DecorationPainter::paintElementBackground(
          painter, theme, QStringLiteral("h%1").arg(headingLevel_), cssBorderBox(theme).translated(0, -scrollY));
    }
    // CSS `:hover`/`:focus { color }` on a heading (phycat h1 → accent) is baked
    // into the inline layout at build time (it knows which runs inherit the
    // colour); paint just needs the shared hover + focus phases, blended there.
    // Box-shadow glow / bg / scale effects still use their own DecorationPainter
    // paths below.
    const qreal hoverPhase = hover.hoverActive ? hover.hoverPhase : 0.0;
    const qreal focusPhase = hover.focusActive ? hover.focusPhase : 0.0;
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
      // Gap between a marker glyph and the content. Theme-overridable; floored
      // for small-indent themes so the marker never crowds the text.
      const qreal markerGap = theme.listMarkerGap();
      const QColor markerColor = theme.listMarkerColor();
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
        painter.setPen(markerColor);
        // Right-align the number to the content gutter so wide multi-digit markers
        // (e.g. "34.", "100.") never overlap the content or the caret.
        const qreal orderedX = contentX - markerGap - metrics.horizontalAdvance(listMarker_);
        painter.drawText(QPointF(orderedX, viewRect.top() + firstBaseline), listMarker_);
      } else {
        QPointF markerCenter(markerX + metrics.horizontalAdvance(QStringLiteral("0")) * 0.35, viewRect.top() + firstBaseline - metrics.xHeight() * 0.45);
        // Non-regressive clamp: with a small indent the default marker column
        // would land inside the content gutter. Shift the bullet left so its
        // right edge honours listMarkerGap() — only moves it when it would
        // otherwise crowd the text (large-indent themes are untouched).
        const qreal bulletRadius = qBound<qreal>(4.2, metrics.height() * 0.34, 6.2) / 2.0;
        const qreal maxX = contentX - markerGap - bulletRadius;
        if (markerCenter.x() > maxX) { markerCenter.setX(qMax<qreal>(viewRect.left(), maxX)); }
        paintUnorderedListMarker(painter, listMarkerKind_, markerCenter, metrics.height(), markerColor);
      }
      painter.restore();
      // Nested-list guide line (phycat's li::before border-left). Each item
      // draws its own vertical segment at the CSS-declared `left` offset from
      // the item's left edge (the li padding box == viewRect.left()); deeper
      // nesting shifts each item's left edge right by one indent, so the
      // per-item segments stack into the tree automatically. No-op when the
      // theme styled no li::before guide.
      const ListGuide guide = theme.listGuide();
      if (guide.present) {
        const qreal lineX = viewRect.left() + guide.leftOffset;
        const qreal y1 = viewRect.top() + guide.topInset;
        const qreal y2 = viewRect.bottom() - guide.bottomInset;
        if (y2 > y1) {
          painter.save();
          painter.setPen(QPen(guide.color, guide.width));
          painter.drawLine(QPointF(lineX, y1), QPointF(lineX, y2));
          painter.restore();
        }
      }
      inlineLayout_->paint(painter, QPointF(contentX, viewRect.top()), hoverPhase, focusPhase);
    } else {
      const QPointF textOrigin = inlineTextOrigin(theme) + QPointF(0, -scrollY);
      inlineLayout_->paint(painter, textOrigin, hoverPhase, focusPhase);
    }
    if (!placeholderText_.isEmpty()) {
      painter.save();
      painter.setFont(theme.paragraphFont());
      painter.setPen(theme.mutedTextColor());
      const QPointF textOrigin = inlineTextOrigin(theme) + QPointF(0, -scrollY);
      // Align with the first text line's baseline (line-height aware) so the
      // placeholder sits where the caret and typed text will, not at the raw
      // block top + ascent.
      painter.drawText(QPointF(textOrigin.x(), textOrigin.y() + inlineLayout_->firstLineBaselineY()), placeholderText_);
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
      dctx.beforeContent = headingBeforeText_;
      dctx.font = theme.headingFont(headingLevel_);
      const BlockLayout::CssBoxGeometry box = cssBoxGeometry(theme);
      const QPointF textOrigin = box.inlineTextOrigin + QPointF(0, -scrollY);
      const QRectF hostRect = box.borderBox.translated(0, -scrollY);
      const qreal beforeAdvance = qMax<qreal>(
          0.0, box.inlineTextOrigin.x() - box.flowRect.left() - theme.headingPadding(headingLevel_).left());
      // textBounds reflects the shifted text origin so ::after anchors to the
      // real text end; contentLeftX lets an inline ::before marker place itself
      // in the reserved zone immediately before the shared text origin.
      const QRectF textBounds = inlineLayout_->visualTextBounds().translated(textOrigin);
      dctx.textBounds = textBounds;
      dctx.contentLeftX = textOrigin.x() - beforeAdvance;
      dctx.textStart = textBounds.isValid() ? textBounds.topLeft() : textOrigin;
      dctx.textEnd = textBounds.isValid() ? QPointF(textBounds.right(), textBounds.top())
                                          : QPointF(textOrigin.x() + inlineLayout_->size().width(), textOrigin.y());
      dctx.hoverPhase = hoverPhase;
      dctx.focusPhase = focusPhase;
      DecorationPainter::paintPseudoDecorations(
          painter, theme, QStringLiteral("h%1").arg(headingLevel_), hostRect, dctx);
    }
  }
}

void BlockLayout::paintBlockQuote(QPainter& painter, const RenderTheme& theme, QRectF viewRect, qreal scrollY) const {
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
    // CSS-driven quote box: paint the real per-side box model. A declaration
    // like `border-left` is only a left edge; only `border` produces four sides.
    const ThemeElementBoxStyle boxStyle = theme.elementBoxStyle(QStringLiteral("blockquote"));
    const QRectF boxRect = cssBorderBox(theme).translated(0, -scrollY);
    QColor background = theme.blockquoteBackgroundColor();
    if (const ThemeElementStyle* style = theme.elementStyle(QStringLiteral("blockquote"))) {
      if (style->paint.backgroundColor.isValid()) { background = style->paint.backgroundColor; }
    }
    paintCssBox(painter, boxStyle, background, boxRect);
    // CSS ::before content (e.g. a 💡 glyph) at the quote's top-left.
    DecorationPainter::PaintContext qctx;
    qctx.font = theme.paragraphFont();
    DecorationPainter::paintPseudoDecorations(
        painter, theme, QStringLiteral("blockquote"), boxRect, qctx);
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
}

void BlockLayout::paintMathBlock(QPainter& painter, const RenderTheme& theme, QRectF viewRect, qreal scrollY) const {
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
}

void BlockLayout::paintMermaidDiagram(QPainter& painter, const RenderTheme& theme,
                                      QRectF viewRect,
                                      BlockPaintState state) const {
  if (!mermaidScene_) return;
  painter.save();
  painter.setPen(theme.codeBorderColor());
  painter.setBrush(theme.codeBackgroundColor());
  if (theme.codeBlockBoxThemed() && theme.codeBlockBorderRadius() > 0.0) {
    painter.drawRoundedRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5), theme.codeBlockBorderRadius(), theme.codeBlockBorderRadius());
  } else {
    painter.drawRect(viewRect.adjusted(0.5, 0.5, -0.5, -0.5));
  }
  // Scale the scene's natural size to fit the content width (never upscale beyond 1:1),
  // then center it in the code box — mirrors paintMathBlock's centered layout.
  const QRectF content = viewRect.marginsRemoved(theme.codePadding());
  const qreal natW = mermaidNaturalSize_.width();
  const qreal natH = mermaidNaturalSize_.height();
  if (natW > 0.0 && natH > 0.0) {
    const qreal scale = qMin<qreal>(1.0, content.width() / natW);
    const qreal drawW = natW * scale;
    const qreal drawH = natH * scale;
    const qreal dx = content.left() + qMax<qreal>(0.0, (content.width() - drawW) / 2.0);
    const qreal dy = content.top() + qMax<qreal>(0.0, (content.height() - drawH) / 2.0);
    // Client-box families use the SAME mapping as the exported SVG/PNG: the
    // fractional client box fills the canvas, content painted through
    // translate(-clientBox.topLeft()), title at its scene-absolute anchor.
    // Piecewise families keep the centered contentOffset model.
    const QRectF clientBox = muffin::mermaid::editor::mermaidClientBox(
        mermaidScene_, mermaidMetadata_);
    qreal contentOffsetX = qMax<qreal>(
        0.0, (natW - mermaidMetadata_.contentSize.width()) / 2.0);
    qreal contentOffsetY =
        mermaidMetadata_.titleHeight + mermaidMetadata_.diagramPadding;
    QRectF titleRect(0.0, 0.0, natW, mermaidMetadata_.titleHeight);
    if (clientBox.isValid()) {
      contentOffsetX = 0.0;
      contentOffsetY = 0.0;
      const qreal titleWidth =
          muffin::mermaid::measureMermaidTitleWidth(mermaidMetadata_);
      const qreal centerX = mermaidScene_->svgClientViewBox().center().x();
      titleRect = QRectF(centerX - titleWidth / 2.0,
                         -mermaidMetadata_.titleHeight, titleWidth,
                         mermaidMetadata_.titleHeight);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const QRectF sceneBounds = mermaidScene_->sceneBounds();
    muffin::mermaid::MermaidPaintOptions paintOptions;
    paintOptions.animationTimeSeconds = state.mermaidAnimationTimeSeconds;
    paintOptions.openSequenceMenus = state.openSequenceMenus;
    painter.translate(dx, dy);
    painter.scale(scale, scale);
    if (!clientBox.isValid())
      muffin::mermaid::paintMermaidTitle(mermaidMetadata_, painter, titleRect);
    if (mermaidViewportCullingEnabled_ && painter.hasClipping()) {
      const QRectF visibleCanvas = painter.clipBoundingRect().intersected(
          clientBox.isValid()
              ? QRectF(0.0, 0.0, natW, natH)
              : QRectF(contentOffsetX, contentOffsetY,
                       sceneBounds.width(), sceneBounds.height()));
      if (!visibleCanvas.isValid()) {
        painter.restore();
        return;
      }
      paintOptions.cullToVisibleRect = true;
      paintOptions.visibleSceneRect = clientBox.isValid()
          ? visibleCanvas.translated(clientBox.topLeft())
          : QRectF(
                sceneBounds.left() + visibleCanvas.left() - contentOffsetX,
                sceneBounds.top() + visibleCanvas.top() - contentOffsetY,
                visibleCanvas.width(), visibleCanvas.height());
    }
    if (clientBox.isValid()) painter.translate(-clientBox.topLeft());
    else
      painter.translate(contentOffsetX - sceneBounds.left(),
                        contentOffsetY - sceneBounds.top());
    if (clientBox.isValid())
      muffin::mermaid::paintMermaidTitle(mermaidMetadata_, painter, titleRect);
    mermaidScene_->paint(painter, paintOptions);
  }
  painter.restore();
}

void BlockLayout::paintMermaidDiagnostic(
    QPainter& painter, const RenderTheme& theme, QRectF viewRect) const {
  if (!hasMermaidDiagnostic()) return;
  painter.save();
  const qreal viewOffsetY = viewRect.top() - rect_.top();
  const QRectF panel =
      mermaidDiagnosticRect(theme).translated(0.0, viewOffsetY);
  const QColor accent = theme.alertAccent(
      mermaidState_ == MermaidState::Error ? AlertKind::Caution
                                           : AlertKind::Warning);
  QColor tint = accent;
  tint.setAlpha(24);

  constexpr qreal radius = 4.0;
  const QRectF borderRect = panel.adjusted(0.5, 0.5, -0.5, -0.5);
  painter.setPen(QPen(accent, 1.0));
  painter.setBrush(theme.codeBackgroundColor());
  painter.drawRoundedRect(borderRect, radius, radius);
  painter.setPen(Qt::NoPen);
  painter.setBrush(tint);
  painter.drawRoundedRect(borderRect, radius, radius);

  const qreal stripeWidth = 4.0;
  painter.setBrush(accent);
  painter.drawRoundedRect(
      QRectF(panel.left() + 1.0, panel.top() + 1.0, stripeWidth,
             qMax<qreal>(1.0, panel.height() - 2.0)),
      2.0, 2.0);

  const QFont font = theme.codeFont();
  painter.setFont(font);
  const qreal padding = mermaidDiagnosticPadding(theme);
  const qreal iconSize = mermaidDiagnosticIconSize(theme);
  const QRectF iconRect(panel.left() + padding,
                        panel.center().y() - iconSize / 2.0,
                        iconSize, iconSize);
  painter.setPen(Qt::NoPen);
  painter.setBrush(accent);
  painter.drawEllipse(iconRect);
  QFont iconFont = font;
  iconFont.setBold(true);
  painter.setFont(iconFont);
  painter.setPen(Qt::white);
  painter.drawText(iconRect, Qt::AlignCenter, QStringLiteral("!"));

  const QRectF textRect(
      iconRect.right() + padding, panel.top() + padding,
      qMax<qreal>(1.0, panel.right() - padding - iconRect.right() - padding),
      qMax<qreal>(1.0, panel.height() - padding * 2.0));
  painter.setClipRect(textRect);
  const MermaidDiagnosticTextMetrics text =
      measureMermaidDiagnosticText(
          mermaidDiagnostic_, theme, textRect.width());
  const qreal textTop = textRect.top() +
      qMax<qreal>(0.0, (textRect.height() - text.height()) / 2.0);
  const int textFlags = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;
  QFont headerFont = font;
  headerFont.setBold(true);
  painter.setFont(headerFont);
  painter.setPen(accent);
  painter.drawText(
      QRectF(textRect.left(), textTop, textRect.width(), text.headerHeight),
      textFlags, text.header);
  if (text.bodyHeight > 0.0) {
    painter.setFont(font);
    painter.setPen(theme.textColor());
    painter.drawText(
        QRectF(textRect.left(), textTop + text.headerHeight + text.gap,
               textRect.width(), text.bodyHeight),
        textFlags, text.body);
  }
  painter.restore();
}

bool BlockLayout::mermaidScenePointAt(
    QPointF documentPos, const RenderTheme& theme,
    const QRectF& sceneBounds, QPointF& scenePos) const {
  if (!isMermaidRendered()) return false;
  const QRectF content = rect_.marginsRemoved(theme.codePadding());
  const qreal natW = mermaidNaturalSize_.width();
  const qreal natH = mermaidNaturalSize_.height();
  if (natW <= 0.0 || natH <= 0.0) return false;
  const qreal s = qMin<qreal>(1.0, content.width() / natW);
  const qreal drawW = natW * s;
  const qreal drawH = natH * s;
  const qreal dx = content.left() + qMax<qreal>(0.0, (content.width() - drawW) / 2.0);
  const qreal dy = content.top() + qMax<qreal>(0.0, (content.height() - drawH) / 2.0);
  // Inverse of the paint transform: document → scene coordinates. Client-box
  // families paint through translate(-clientBox.topLeft()) (the SVG/PNG
  // mapping); piecewise families keep the centered contentOffset model.
  const QRectF clientBox = muffin::mermaid::editor::mermaidClientBox(
      mermaidScene_, mermaidMetadata_);
  if (clientBox.isValid()) {
    scenePos = QPointF((documentPos.x() - dx) / s + clientBox.left(),
                       (documentPos.y() - dy) / s + clientBox.top());
    return true;
  }
  const qreal contentOffsetX = qMax<qreal>(
      0.0, (natW - mermaidMetadata_.contentSize.width()) / 2.0);
  scenePos = QPointF(
      (documentPos.x() - dx) / s - contentOffsetX +
          sceneBounds.left(),
      (documentPos.y() - dy) / s - mermaidMetadata_.titleHeight -
          mermaidMetadata_.diagramPadding +
          sceneBounds.top());
  return true;
}

BlockLayout::MermaidInteractionHit BlockLayout::mermaidInteractionAt(
    QPointF documentPos, const RenderTheme& theme,
    const QSet<QString>* openSequenceMenus) const {
  MermaidInteractionHit result;
  if (!mermaidScene_) return result;
  QPointF scenePos;
  if (!mermaidScenePointAt(documentPos, theme, mermaidScene_->sceneBounds(), scenePos)) {
    return result;
  }
  // Iterate regions in reverse (topmost wins). Sequence scenes append actors
  // before items, so items win over actor-toggles at the same point.
  const bool force = mermaidScene_->menusAlwaysOpen();
  const auto& regions = mermaidScene_->interactionRegions();
  for (auto it = regions.crbegin(); it != regions.crend(); ++it) {
    const auto& r = *it;
    if (!r.requiresOpenMenu.isEmpty() && !force &&
        !(openSequenceMenus && openSequenceMenus->contains(r.requiresOpenMenu)))
      continue;  // sequence menu item whose menu is closed
    if (!r.togglesMenu.isEmpty() && force) continue;  // actor toggle inactive under forceMenus
    if (!r.bounds.contains(scenePos)) continue;
    result.toolTip = r.toolTip;  // empty for sequence items/actors (matches prior behavior)
    if (!r.href.isEmpty() && isSafeUrl(r.href, false)) result.linkHref = r.href;
    result.menuActorId = r.togglesMenu;
    return result;
  }
  return result;
}

void BlockLayout::paintHtmlBlock(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const {
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
}

void BlockLayout::paintThematicBreak(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const {
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

  const QPointF origin = inlineTextOrigin(theme);
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

  const QPointF origin = inlineTextOrigin(theme);
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
  // spans is sorted by start (TreeSitterHighlighter::normalizeSpans sorts tree-sitter output;
  // highlightMathTex walks the text monotonically). Walk it with a two-pointer so each span is
  // considered only across the lines it overlaps, instead of re-scanning every span for every
  // line — O(spans×lines) → O(spans+lines). Long code fences are repainted on every scroll/
  // caret/hover, so this matters on big blocks.
  qsizetype spanIdx = 0;
  const qreal codeLineHeight = theme.codeLineHeight();
  const auto [diagnosticStart, diagnosticEnd] =
      mermaidState_ == MermaidState::Error
          ? mermaidHighlightRange(mermaidDiagnostic_, literal_)
          : QPair<qsizetype, qsizetype>{-1, -1};
  for (const QString& sourceLine : lines) {
    const QString lineText = sourceLine.isEmpty() ? QStringLiteral(" ") : sourceLine;
    QTextLayout layout(lineText, theme.codeFont());
    layout.setTextOption(option);

    const qsizetype lineEndOffset = lineStartOffset + sourceLine.size();
    // Drop spans that ended at/before this line's start — once end <= lineStart they can't
    // overlap this or any later line.
    while (spanIdx < spans.size() && spans[spanIdx].end <= lineStartOffset) {
      ++spanIdx;
    }

    QVector<QTextLayout::FormatRange> formats;
    QTextLayout::FormatRange baseRange;
    baseRange.start = 0;
    baseRange.length = sourceLine.size();
    baseRange.format = baseFormat;
    formats.push_back(baseRange);

    for (qsizetype s = spanIdx; s < spans.size(); ++s) {
      const CodeHighlightSpan& span = spans[s];
      if (span.start >= lineEndOffset) {
        break;  // sorted by start: the rest begin after this line
      }
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
    const qsizetype errorStart = qMax(diagnosticStart, lineStartOffset);
    const qsizetype errorEnd = qMin(diagnosticEnd, lineEndOffset);
    if (diagnosticStart >= 0 && errorEnd > errorStart) {
      QTextCharFormat format;
      const QColor accent = theme.alertAccent(AlertKind::Caution);
      QColor background = accent;
      background.setAlpha(32);
      format.setBackground(background);
      format.setUnderlineColor(accent);
      format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
      QTextLayout::FormatRange range;
      range.start = static_cast<int>(errorStart - lineStartOffset);
      range.length = static_cast<int>(errorEnd - errorStart);
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

HitTestResult BlockLayout::hitSelf(
    QPointF documentPos, const RenderTheme& theme,
    const CodeFenceScrollController* scroll,
    const QSet<QString>* openSequenceMenus) const {
  HitTestResult result;
  result.blockId = id_;
  result.textNodeId = id_;
  result.blockRect = rect_;
  result.zone = HitTestResult::Zone::Block;

  switch (type_) {
    case BlockType::Heading:
    case BlockType::Paragraph:
    case BlockType::ListItem:
      // A `[TOC]` preview block is non-editable: no caret is placed. A click on an
      // entry's row resolves to a `#toc:<nodeId>` href so the existing Ctrl+click
      // link path scrolls to the heading; a click elsewhere selects the block.
      if (isToc_) {
        result.zone = HitTestResult::Zone::SelectBlock;
        for (const TocEntryLayout& entry : tocEntries_) {
          if (entry.rect.contains(documentPos)) {
            result.linkHref = QStringLiteral("#toc:") + entry.target.toString();
            break;
          }
        }
        return result;
      }
      if (inlineLayout_) {
        const QPointF origin = inlineTextOrigin(theme);
        const qreal textLeft = origin.x();
        const QRectF textRect(origin, QSizeF(qMax<qreal>(1.0, rect_.right() - textLeft), rect_.height()));
        if (hasListMarker() && documentPos.x() < textLeft) {
          result.zone = HitTestResult::Zone::Marker;
          result.cursorRect = QRectF(textLeft, origin.y(), 1.0, rect_.height());
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
      if (type_ == BlockType::CodeFence &&
          mermaidState_ == MermaidState::Error &&
          mermaidDiagnostic_.span.hasLocation() &&
          mermaidDiagnosticRect(theme).contains(documentPos)) {
        result.zone = HitTestResult::Zone::Code;
        result.textOffset = qBound<qsizetype>(
            0, mermaidDiagnostic_.span.offset, literal_.size());
        result.cursorRect = literalCursorRectForOffset(
            literal_, result.textOffset, theme.codeFont(),
            contentRect.topLeft(), contentRect.width(),
            theme.codeLineHeight(), wrap);
        return result;
      }
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
      // Rendered mermaid diagram: a click on a node carrying a safe link follows it
      // (Ctrl+click in EditorView); otherwise fall through to caret placement below.
      result.mermaidRendered = isMermaidRendered();
      if (result.mermaidRendered) {
        const MermaidInteractionHit interaction =
            mermaidInteractionAt(documentPos, theme, openSequenceMenus);
        result.linkHref = interaction.linkHref;
        result.toolTip = interaction.toolTip;
        result.mermaidMenuActorId = interaction.menuActorId;
        if (!result.linkHref.isEmpty() ||
            !result.mermaidMenuActorId.isEmpty()) {
          return result;
        }
      }
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
    case BlockType::ThematicBreak: {
      // A thematic break hosts no editable text, so clicking it SELECTS the whole break
      // (Typora-style: a thin outline is drawn around it and Del/Backspace/Enter remove it). Only a
      // click INSIDE the rule's rect selects it; a click in the half-blockSpacing padded margin
      // above/below returns invalid ({}), so DocumentLayout::hitTest's window loop moves on and the
      // neighbouring block claims the gap — preserving "click above → caret before the rule" and
      // "click below → caret after it / into the empty paragraph beneath".
      if (rect_.contains(documentPos)) {
        result.zone = HitTestResult::Zone::SelectBlock;
      } else {
        return {};
      }
      break;
    }
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
      // Resolve the cell background with validity guards: a theme that declares no
      // header/stripe background (e.g. whitey — only padding/borders, no `th`/`tr:nth-
      // child` bg) leaves those tokens invalid. Painting an invalid QColor fills SOLID
      // BLACK, so fall back to the page background instead — undeclared ⇒ transparent
      // over the page, matching Typora. Header bg applies only to header cells, stripe
      // bg only to alternate rows, each only when the theme actually declared it.
      QColor cellBg = theme.backgroundColor();
      if (cell.header) {
        const QColor headerBg = theme.tableHeaderBackgroundColor();
        if (headerBg.isValid()) { cellBg = headerBg; }
      } else if (cell.alternate) {
        const QColor stripeBg = theme.tableAlternateBackgroundColor();
        if (stripeBg.isValid()) { cellBg = stripeBg; }
      }
      painter.setPen(theme.tableBorderColor());
      painter.setBrush(cellBg);
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
    const qreal lineHeightF = std::ceil(metrics.height() * kLineHeightFactor);
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

void BlockLayout::paintToc(QPainter& painter, const RenderTheme& theme, QRectF viewRect) const {
  // Each entry's row rect was laid out at build time (document-absolute); here we
  // only translate it into view coordinates and draw the title indented by level,
  // in the theme's link colour so the entries read as clickable (matching how real
  // links render). Long titles elide to the row width.
  if (tocEntries_.isEmpty()) {
    return;
  }
  const qreal scrollOffset = viewRect.top() - rect_.top();  // = -scrollY
  painter.save();
  const QFont font = theme.paragraphFont();
  painter.setFont(font);
  const QFontMetricsF fm(font);
  const QColor linkColor = theme.linkColor();
  const qreal indentStep = theme.listIndent();
  const qreal topInset = 2.0;
  for (const TocEntryLayout& entry : tocEntries_) {
    const QRectF row = entry.rect.translated(0, scrollOffset);
    const qreal dx = static_cast<qreal>(entry.level - 1) * indentStep;
    const qreal textX = row.left() + dx;
    const qreal baselineY = row.top() + topInset + fm.ascent();
    const qreal avail = row.right() - textX - 8.0;
    QString title = entry.title;
    if (avail > 0.0 && fm.horizontalAdvance(title) > avail) {
      title = fm.elidedText(title, Qt::ElideRight, avail);
    }
    painter.setPen(linkColor);
    painter.drawText(QPointF(textX, baselineY), title);
    if (theme.linkUnderlined()) {
      const qreal w = fm.horizontalAdvance(title);
      const qreal underlineY = baselineY + 1.0;
      painter.drawLine(QPointF(textX, underlineY), QPointF(textX + w, underlineY));
    }
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
