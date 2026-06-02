#include "render/InlineLayout.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLine>
#include <QTextOption>

namespace muffin {
namespace {

QString escaped(QString value) {
  return value.toHtmlEscaped();
}

QString flattenPlainText(const QVector<InlineNode>& inlines) {
  QString text;
  for (const InlineNode& node : inlines) {
    switch (node.type()) {
      case InlineType::Text:
      case InlineType::Code:
      case InlineType::InlineMath:
      case InlineType::HtmlInline:
        text += node.text();
        break;
      case InlineType::SoftBreak:
        text += QLatin1Char(' ');
        break;
      case InlineType::LineBreak:
        text += QLatin1Char('\n');
        break;
      case InlineType::Image:
        text += node.alt();
        break;
      default:
        text += flattenPlainText(node.children());
        break;
    }
  }
  return text;
}

}  // namespace

void InlineLayout::build(
    const QVector<InlineNode>& inlines,
    const RenderTheme& theme,
    qreal width,
    const QFont& baseFont) {
  build(inlines, theme, width, baseFont, BuildOptions{});
}

void InlineLayout::build(
    const QVector<InlineNode>& inlines,
    const RenderTheme& theme,
    qreal width,
    const QFont& baseFont,
    BuildOptions options) {
  build(inlines, InlineProjection::markdownForInlines(inlines), theme, width, baseFont, options);
}

void InlineLayout::build(
    const QVector<InlineNode>& inlines,
    QString sourceText,
    const RenderTheme& theme,
    qreal width,
    const QFont& baseFont,
    BuildOptions options) {
  plainText_ = flattenPlainText(inlines);
  offsetMap_.clear();
  displayText_.clear();
  geometryBackend_ = options.geometryBackend;
  textLayoutCodeBackgroundColor_ = theme.codeBackgroundColor();
  textLayoutCodeBorderColor_ = theme.codeBorderColor();
  projection_ = InlineProjection(inlines, std::move(sourceText), options.projectionState);
  qsizetype visibleOffset = 0;
  html_ = renderInlines(inlines, theme, visibleOffset, options);
  buildOffsetMapFromProjection();
  buildTextLayout(theme, width, baseFont);
  if (geometryBackend_ == InlineGeometryBackend::QTextLayout) {
    document_.reset();
    size_ = textLayoutSize_;
    return;
  }

  document_ = std::make_unique<QTextDocument>();
  document_->setDefaultFont(baseFont);
  document_->setDocumentMargin(0);
  document_->setTextWidth(qMax<qreal>(1.0, width));
  document_->setHtml(QStringLiteral(
                         "<html><head><style>"
                         "body { margin:0; color:%1; }"
                         "p { margin:0; line-height:1.45; }"
                         "a { color:%2; text-decoration:none; }"
                         "code { font-family:'Cascadia Mono','Consolas',monospace; background:%3;"
                         " border:1px solid %4; padding:1px 4px; }"
                         ".math { font-family:'Cambria Math','Times New Roman',serif; }"
                         "</style></head><body><p>%5</p></body></html>")
                         .arg(
                             cssColor(theme.textColor()),
                             cssColor(theme.linkColor()),
                             cssColor(theme.codeBackgroundColor()),
                             cssColor(theme.codeBorderColor()),
                             html_));
  size_ = document_->size();
}

QSizeF InlineLayout::size() const {
  return size_;
}

qreal InlineLayout::height() const {
  return size_.height();
}

void InlineLayout::paint(QPainter& painter, QPointF origin) const {
  if (geometryBackend_ == InlineGeometryBackend::QTextLayout) {
    if (!textLayout_) {
      return;
    }
    painter.save();
    paintTextLayoutCodeSpans(painter, origin);
    textLayout_->draw(&painter, origin);
    painter.restore();
    return;
  }

  if (!document_) {
    return;
  }

  painter.save();
  painter.translate(origin);
  QAbstractTextDocumentLayout::PaintContext context;
  document_->documentLayout()->draw(&painter, context);
  painter.restore();
}

qsizetype InlineLayout::hitTestTextOffset(QPointF localPos) const {
  if (geometryBackend_ == InlineGeometryBackend::QTextLayout) {
    return textLayoutHitTestTextOffset(localPos);
  }
  if (!document_) {
    return 0;
  }
  const int position = document_->documentLayout()->hitTest(localPos, Qt::FuzzyHit);
  return visibleOffsetForDisplayOffset(position);
}

qsizetype InlineLayout::hitTestSourceOffset(QPointF localPos) const {
  int position = 0;
  if (geometryBackend_ == InlineGeometryBackend::QTextLayout) {
    position = static_cast<int>(textLayoutDisplayOffsetForPoint(localPos));
  } else {
    if (!document_) {
      return 0;
    }
    position = document_->documentLayout()->hitTest(localPos, Qt::FuzzyHit);
  }
  qsizetype sourceOffset = -1;
  if (projection_.sourceOffsetForDisplayOffset(position, sourceOffset)) {
    return sourceOffset;
  }
  return visibleOffsetForDisplayOffset(position);
}

QRectF InlineLayout::cursorRect(qsizetype textOffset) const {
  if (geometryBackend_ == InlineGeometryBackend::QTextLayout) {
    return textLayoutCursorRect(textOffset);
  }
  if (!document_) {
    return {};
  }
  const int displayOffset = static_cast<int>(displayOffsetForVisibleOffset(textOffset));
  const int position = qBound(0, displayOffset, qMax(0, document_->characterCount() - 1));
  const QTextBlock block = document_->findBlock(position);
  if (!block.isValid() || !block.layout()) {
    return {};
  }

  const int relativePosition = qBound(0, position - block.position(), block.length());
  const QTextLine line = block.layout()->lineForTextPosition(relativePosition);
  if (!line.isValid()) {
    const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
    return QRectF(blockRect.left(), blockRect.top(), 1.0, blockRect.height());
  }

  const qreal x = line.cursorToX(relativePosition);
  const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
  return QRectF(blockRect.left() + x, blockRect.top() + line.y(), 1.0, line.height());
}

QRectF InlineLayout::cursorRectForSourceOffset(qsizetype sourceOffset) const {
  qsizetype displayOffset = -1;
  if (!projection_.displayOffsetForSourceOffset(sourceOffset, displayOffset)) {
    return cursorRect(sourceOffset);
  }
  if (geometryBackend_ == InlineGeometryBackend::QTextLayout) {
    return textLayoutCursorRectForDisplayOffset(displayOffset);
  }
  if (!document_) {
    return {};
  }
  const int position = qBound(0, static_cast<int>(displayOffset), qMax(0, document_->characterCount() - 1));
  const QTextBlock block = document_->findBlock(position);
  if (!block.isValid() || !block.layout()) {
    return {};
  }

  const int relativePosition = qBound(0, position - block.position(), block.length());
  const QTextLine line = block.layout()->lineForTextPosition(relativePosition);
  if (!line.isValid()) {
    const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
    return QRectF(blockRect.left(), blockRect.top(), 1.0, blockRect.height());
  }

  const qreal x = line.cursorToX(relativePosition);
  const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
  return QRectF(blockRect.left() + x, blockRect.top() + line.y(), 1.0, line.height());
}

QVector<QRectF> InlineLayout::selectionRects(qsizetype startOffset, qsizetype endOffset) const {
  if (geometryBackend_ == InlineGeometryBackend::QTextLayout) {
    return textLayoutSelectionRects(startOffset, endOffset);
  }
  QVector<QRectF> rects;
  if (!document_) {
    return rects;
  }

  const int start = qBound(0, static_cast<int>(displayOffsetForVisibleOffset(qMin(startOffset, endOffset))), static_cast<int>(displayText_.size()));
  const int end = qBound(0, static_cast<int>(displayOffsetForVisibleOffset(qMax(startOffset, endOffset))), static_cast<int>(displayText_.size()));
  if (start == end) {
    return rects;
  }

  for (QTextBlock block = document_->begin(); block.isValid(); block = block.next()) {
    QTextLayout* layout = block.layout();
    if (!layout) {
      continue;
    }
    const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
    const int blockStart = block.position();
    const int blockEnd = blockStart + block.length();
    const int localStart = qMax(0, start - blockStart);
    const int localEnd = qMin(block.length(), end - blockStart);
    if (localStart >= localEnd || end <= blockStart || start >= blockEnd) {
      continue;
    }

    for (int i = 0; i < layout->lineCount(); ++i) {
      const QTextLine line = layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      const int rangeStart = qMax(localStart, lineStart);
      const int rangeEnd = qMin(localEnd, lineEnd);
      if (rangeStart >= rangeEnd) {
        continue;
      }

      const qreal x1 = line.cursorToX(rangeStart);
      const qreal x2 = line.cursorToX(rangeEnd);
      rects.push_back(QRectF(blockRect.left() + qMin(x1, x2), blockRect.top() + line.y(), qAbs(x2 - x1), line.height()));
    }
  }
  return rects;
}

QSizeF InlineLayout::textLayoutSize() const {
  return textLayoutSize_;
}

QRectF InlineLayout::textLayoutCursorRect(qsizetype textOffset) const {
  return textLayoutCursorRectForDisplayOffset(displayOffsetForVisibleOffset(textOffset));
}

qsizetype InlineLayout::textLayoutHitTestTextOffset(QPointF localPos) const {
  return visibleOffsetForDisplayOffset(textLayoutDisplayOffsetForPoint(localPos));
}

QVector<QRectF> InlineLayout::textLayoutSelectionRects(qsizetype startOffset, qsizetype endOffset) const {
  QVector<QRectF> rects;
  if (!textLayout_) {
    return rects;
  }

  const int start = qBound(0, static_cast<int>(displayOffsetForVisibleOffset(qMin(startOffset, endOffset))), static_cast<int>(displayText_.size()));
  const int end = qBound(0, static_cast<int>(displayOffsetForVisibleOffset(qMax(startOffset, endOffset))), static_cast<int>(displayText_.size()));
  if (start == end) {
    return rects;
  }

  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const QTextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) {
      continue;
    }
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    const int rangeStart = qMax(start, lineStart);
    const int rangeEnd = qMin(end, lineEnd);
    if (rangeStart >= rangeEnd) {
      continue;
    }
    const qreal x1 = line.cursorToX(rangeStart);
    const qreal x2 = line.cursorToX(rangeEnd);
    rects.push_back(QRectF(qMin(x1, x2), line.y(), qAbs(x2 - x1), line.height()));
  }
  return rects;
}

void InlineLayout::paintTextLayoutCodeSpans(QPainter& painter, QPointF origin) const {
  if (!textLayout_) {
    return;
  }

  painter.save();
  painter.setPen(QPen(textLayoutCodeBorderColor_, 1.0));
  painter.setBrush(textLayoutCodeBackgroundColor_);
  for (const InlineProjectionSpan& span : projection_.spans()) {
    if (span.type != InlineType::Code || span.kind != InlineSpanKind::Text || span.displayEnd <= span.displayStart) {
      continue;
    }

    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const QTextLine line = textLayout_->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      const int rangeStart = qMax(lineStart, static_cast<int>(span.displayStart));
      const int rangeEnd = qMin(lineEnd, static_cast<int>(span.displayEnd));
      if (rangeStart >= rangeEnd) {
        continue;
      }
      const qreal x1 = line.cursorToX(rangeStart);
      const qreal x2 = line.cursorToX(rangeEnd);
      const QRectF rect(
          origin.x() + qMin(x1, x2) - 3.0,
          origin.y() + line.y() + 1.0,
          qAbs(x2 - x1) + 6.0,
          qMax<qreal>(1.0, line.height() - 2.0));
      painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
    }
  }
  painter.restore();
}

QString InlineLayout::plainText() const {
  return plainText_;
}

QString InlineLayout::displayText() const {
  return displayText_;
}

QString InlineLayout::documentText() const {
  return document_ ? document_->toPlainText() : QString();
}

QString InlineLayout::html() const {
  return html_;
}

void InlineLayout::buildOffsetMapFromProjection() {
  displayText_ = projection_.displayText();
  offsetMap_.clear();
  offsetMap_.reserve(projection_.spans().size());
  for (const InlineProjectionSpan& span : projection_.spans()) {
    offsetMap_.push_back(OffsetMapEntry{span.displayStart, span.displayEnd, span.visibleStart, span.visibleEnd});
  }
}

void InlineLayout::buildTextLayout(const RenderTheme& theme, qreal width, const QFont& baseFont) {
  textLayout_ = std::make_unique<QTextLayout>(displayText_.isEmpty() ? QStringLiteral(" ") : displayText_, baseFont);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  textLayout_->setTextOption(option);
  textLayout_->setFormats(textLayoutFormats(theme, baseFont));

  const qreal lineWidth = qMax<qreal>(1.0, width);
  qreal height = 0.0;
  qreal maxWidth = 0.0;
  textLayout_->beginLayout();
  while (true) {
    QTextLine line = textLayout_->createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(lineWidth);
    line.setPosition(QPointF(0.0, height));
    height += line.height();
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
  }
  textLayout_->endLayout();
  textLayoutSize_ = QSizeF(qMin(lineWidth, qMax<qreal>(maxWidth, 1.0)), height);
}

QVector<QTextLayout::FormatRange> InlineLayout::textLayoutFormats(const RenderTheme& theme, const QFont& baseFont) const {
  QVector<QTextLayout::FormatRange> formats;
  if (displayText_.isEmpty()) {
    return formats;
  }

  QTextCharFormat baseFormat;
  baseFormat.setFont(baseFont);
  baseFormat.setForeground(theme.textColor());
  QTextLayout::FormatRange baseRange;
  baseRange.start = 0;
  baseRange.length = displayText_.size();
  baseRange.format = baseFormat;
  formats.push_back(baseRange);

  for (const InlineProjectionSpan& span : projection_.spans()) {
    if (span.displayEnd <= span.displayStart) {
      continue;
    }
    QTextCharFormat format = baseFormat;
    if (span.kind == InlineSpanKind::OpenMarker || span.kind == InlineSpanKind::CloseMarker ||
        span.kind == InlineSpanKind::HiddenSyntax || span.kind == InlineSpanKind::EmptyContentSlot) {
      format.setForeground(theme.mutedTextColor());
    }
    switch (span.type) {
      case InlineType::Code:
        format.setFont(theme.codeFont());
        if (span.kind == InlineSpanKind::Text) {
          format.setBackground(theme.codeBackgroundColor());
        }
        break;
      case InlineType::InlineMath:
        format.setFont(theme.mathFont());
        break;
      case InlineType::Link:
        if (span.kind == InlineSpanKind::Text) {
          format.setForeground(theme.linkColor());
        }
        break;
      case InlineType::Strong:
        format.setFontWeight(QFont::Bold);
        break;
      case InlineType::Emphasis:
        format.setFontItalic(true);
        break;
      case InlineType::Strikethrough:
        format.setFontStrikeOut(true);
        break;
      default:
        break;
    }

    QTextLayout::FormatRange range;
    range.start = static_cast<int>(span.displayStart);
    range.length = static_cast<int>(span.displayEnd - span.displayStart);
    range.format = format;
    formats.push_back(range);
  }
  return formats;
}

QString InlineLayout::renderInlines(const QVector<InlineNode>& inlines, const RenderTheme& theme, qsizetype& visibleOffset, BuildOptions options) {
  QString html;
  for (const InlineNode& node : inlines) {
    html += renderInline(node, theme, visibleOffset, options);
  }
  return html;
}

QString InlineLayout::renderInline(const InlineNode& node, const RenderTheme& theme, qsizetype& visibleOffset, BuildOptions options) {
  const QString visible = flattenPlainText(QVector<InlineNode>{node});
  const qsizetype visibleStart = visibleOffset;
  const qsizetype visibleEnd = visibleStart + visible.size();
  const bool active = projectionSyntaxSpan(node.type(), InlineSpanKind::OpenMarker, visibleStart).isValid() ||
                      projectionSyntaxSpan(node.type(), InlineSpanKind::HiddenSyntax, visibleEnd).isValid();

  switch (node.type()) {
    case InlineType::Text: {
      visibleOffset = visibleEnd;
      return escaped(node.text());
    }
    case InlineType::SoftBreak: {
      visibleOffset = visibleEnd;
      return QStringLiteral(" ");
    }
    case InlineType::LineBreak: {
      visibleOffset = visibleEnd;
      return QStringLiteral("<br/>");
    }
    case InlineType::Code: {
      QString html;
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::OpenMarker, visibleStart, theme);
      }
      html += QStringLiteral("<code>%1</code>").arg(escaped(node.text()));
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::CloseMarker, visibleEnd, theme);
      }
      visibleOffset = visibleEnd;
      return html;
    }
    case InlineType::Emphasis:
    case InlineType::Strong:
    case InlineType::Strikethrough: {
      QString html;
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::OpenMarker, visibleStart, theme);
      }
      const QString inner = renderInlines(node.children(), theme, visibleOffset, options);
      const QString tag = node.type() == InlineType::Emphasis ? QStringLiteral("em")
                          : node.type() == InlineType::Strong ? QStringLiteral("strong")
                                                              : QStringLiteral("s");
      html += QStringLiteral("<%1>%2</%1>").arg(tag, inner);
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::CloseMarker, visibleEnd, theme);
      }
      visibleOffset = visibleEnd;
      return html;
    }
    case InlineType::Link: {
      QString html;
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::OpenMarker, visibleStart, theme);
      }
      const QString inner = renderInlines(node.children(), theme, visibleOffset, options);
      html += QStringLiteral("<a href=\"%1\">%2</a>").arg(escaped(node.href()), inner);
      visibleOffset = visibleEnd;
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::HiddenSyntax, visibleEnd, theme);
      }
      return html;
    }
    case InlineType::Image: {
      QString html;
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::OpenMarker, visibleStart, theme);
      }
      html += QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(cssColor(theme.mutedTextColor()), escaped(node.alt()));
      visibleOffset = visibleEnd;
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::HiddenSyntax, visibleEnd, theme);
      }
      return html;
    }
    case InlineType::InlineMath: {
      QString html;
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::OpenMarker, visibleStart, theme);
      }
      html += QStringLiteral("<span class=\"math\">%1</span>").arg(escaped(node.text()));
      if (active) {
        html += renderProjectionSyntaxSpan(node.type(), InlineSpanKind::CloseMarker, visibleEnd, theme);
      }
      visibleOffset = visibleEnd;
      return html;
    }
    case InlineType::HtmlInline: {
      visibleOffset = visibleEnd;
      return escaped(node.text());
    }
    default:
      return renderInlines(node.children(), theme, visibleOffset, options);
  }
}

InlineLayout::ProjectionSyntaxSpanRef InlineLayout::projectionSyntaxSpan(
    InlineType type,
    InlineSpanKind kind,
    qsizetype visiblePosition) const {
  for (const InlineProjectionSpan& span : projection_.spans()) {
    if (span.type == type && span.kind == kind && span.visibleStart == visiblePosition && span.visibleEnd == visiblePosition &&
        span.displayEnd > span.displayStart) {
      return ProjectionSyntaxSpanRef{&span, projection_.displayText().mid(span.displayStart, span.displayEnd - span.displayStart)};
    }
  }
  return {};
}

QString InlineLayout::renderProjectionSyntaxSpan(
    InlineType type,
    InlineSpanKind kind,
    qsizetype visiblePosition,
    const RenderTheme& theme) const {
  const ProjectionSyntaxSpanRef syntaxSpan = projectionSyntaxSpan(type, kind, visiblePosition);
  if (!syntaxSpan.isValid()) {
    return {};
  }
  return QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(cssColor(theme.mutedTextColor()), escaped(syntaxSpan.text));
}

QString InlineLayout::cssColor(const QColor& color) const {
  return color.name(QColor::HexRgb);
}

qsizetype InlineLayout::visibleOffsetForDisplayOffset(qsizetype displayOffset) const {
  if (offsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, displayOffset, plainText_.size());
  }
  displayOffset = qBound<qsizetype>(0, displayOffset, displayText_.size());
  for (const OffsetMapEntry& entry : offsetMap_) {
    if (displayOffset <= entry.displayEnd) {
      if (entry.visibleEnd <= entry.visibleStart || entry.displayEnd <= entry.displayStart) {
        return entry.visibleStart;
      }
      const qsizetype delta = qBound<qsizetype>(0, displayOffset - entry.displayStart, entry.visibleEnd - entry.visibleStart);
      return qBound<qsizetype>(entry.visibleStart, entry.visibleStart + delta, entry.visibleEnd);
    }
  }
  return plainText_.size();
}

qsizetype InlineLayout::displayOffsetForVisibleOffset(qsizetype visibleOffset) const {
  if (offsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, visibleOffset, plainText_.size());
  }
  visibleOffset = qBound<qsizetype>(0, visibleOffset, plainText_.size());
  for (const OffsetMapEntry& entry : offsetMap_) {
    if (visibleOffset <= entry.visibleEnd) {
      if (entry.visibleEnd <= entry.visibleStart || entry.displayEnd <= entry.displayStart) {
        continue;
      }
      const qsizetype delta = qBound<qsizetype>(0, visibleOffset - entry.visibleStart, entry.displayEnd - entry.displayStart);
      return qBound<qsizetype>(entry.displayStart, entry.displayStart + delta, entry.displayEnd);
    }
  }
  return displayText_.size();
}

qsizetype InlineLayout::textLayoutDisplayOffsetForPoint(QPointF localPos) const {
  if (!textLayout_) {
    return 0;
  }

  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const QTextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) {
      continue;
    }
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    if (lineEnd <= lineStart) {
      continue;
    }
    const QRectF naturalRect(line.x(), line.y(), line.naturalTextWidth(), line.height());
    const QRectF fullLineRect(0.0, line.y(), qMax(textLayoutSize_.width(), naturalRect.width()), line.height());
    if (!fullLineRect.contains(localPos) && localPos.y() > fullLineRect.bottom()) {
      continue;
    }

    if (localPos.x() <= line.cursorToX(lineStart)) {
      return lineStart;
    }
    if (localPos.x() >= line.cursorToX(lineEnd)) {
      return lineEnd;
    }

    for (int offset = lineStart; offset < lineEnd; ++offset) {
      const qreal left = line.cursorToX(offset);
      const qreal right = line.cursorToX(offset + 1);
      const qreal midpoint = (left + right) / 2.0;
      if (localPos.x() < midpoint) {
        return offset;
      }
      if (localPos.x() < right) {
        return offset + 1;
      }
    }
    return lineEnd;
  }

  return displayText_.size();
}

QRectF InlineLayout::textLayoutCursorRectForDisplayOffset(qsizetype displayOffset) const {
  if (!textLayout_) {
    return {};
  }
  displayOffset = qBound<qsizetype>(0, displayOffset, displayText_.size());
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const QTextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) {
      continue;
    }
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    if (displayOffset < lineStart || displayOffset > lineEnd) {
      continue;
    }
    const qreal x = line.cursorToX(static_cast<int>(displayOffset));
    return QRectF(x, line.y(), 1.0, line.height());
  }
  if (textLayout_->lineCount() > 0) {
    const QTextLine line = textLayout_->lineAt(textLayout_->lineCount() - 1);
    const qreal x = line.cursorToX(line.textStart() + line.textLength());
    return QRectF(x, line.y(), 1.0, line.height());
  }
  return {};
}

}  // namespace muffin
