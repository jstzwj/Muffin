#include "render/InlineLayout.h"

#include <QPainter>
#include <QPen>
#include <QTextCharFormat>
#include <QTextLine>
#include <QTextOption>

#include <cmath>

namespace muffin {
namespace {

constexpr QChar kInlineMathPlaceholder(0x00a0);

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
  mathAtoms_.clear();
  displayText_.clear();
  textLayoutCodeBackgroundColor_ = theme.codeBackgroundColor();
  textLayoutCodeBorderColor_ = theme.codeBorderColor();
  projection_ = InlineProjection(inlines, std::move(sourceText), options.projectionState);
  buildOffsetMapFromProjection();
  buildMathAtoms(inlines, theme);
  buildTextLayout(theme, width, baseFont);
}

QSizeF InlineLayout::size() const {
  return size_;
}

qreal InlineLayout::height() const {
  return size_.height();
}

void InlineLayout::paint(QPainter& painter, QPointF origin) const {
  if (!textLayout_) {
    return;
  }

  painter.save();
  paintTextLayoutCodeSpans(painter, origin);
  textLayout_->draw(&painter, origin);
  paintTextLayoutMathAtoms(painter, origin);
  painter.restore();
}

qsizetype InlineLayout::hitTestTextOffset(QPointF localPos) const {
  return visibleOffsetForDisplayOffset(textLayoutDisplayOffsetForPoint(localPos));
}

qsizetype InlineLayout::hitTestSourceOffset(QPointF localPos) const {
  const qsizetype position = textLayoutDisplayOffsetForPoint(localPos);
  for (const MathAtom& atom : mathAtoms_) {
    if (position < atom.displayStart || position > atom.displayEnd) {
      continue;
    }
    const QRectF atomRect = textLayoutCursorRectForDisplayOffset(atom.displayStart)
                                .united(textLayoutCursorRectForDisplayOffset(atom.displayEnd));
    if (!atomRect.isNull() && localPos.x() > atomRect.center().x()) {
      return atom.sourceEnd;
    }
    return atom.sourceStart;
  }
  qsizetype sourceOffset = -1;
  if (projection_.sourceOffsetForDisplayOffset(position, sourceOffset)) {
    return sourceOffset;
  }
  return visibleOffsetForDisplayOffset(position);
}

QRectF InlineLayout::cursorRect(qsizetype textOffset) const {
  for (const MathAtom& atom : mathAtoms_) {
    if (textOffset > atom.visibleStart && textOffset < atom.visibleEnd) {
      textOffset = textOffset - atom.visibleStart < atom.visibleEnd - textOffset ? atom.visibleStart : atom.visibleEnd;
      break;
    }
  }
  return textLayoutCursorRectForDisplayOffset(displayOffsetForVisibleOffset(textOffset));
}

QRectF InlineLayout::cursorRectForSourceOffset(qsizetype sourceOffset) const {
  for (const MathAtom& atom : mathAtoms_) {
    if (sourceOffset > atom.sourceStart && sourceOffset < atom.sourceEnd) {
      const qsizetype displayOffset = sourceOffset - atom.sourceStart < atom.sourceEnd - sourceOffset ? atom.displayStart : atom.displayEnd;
      return textLayoutCursorRectForDisplayOffset(displayOffset);
    }
  }
  qsizetype displayOffset = -1;
  if (!projection_.displayOffsetForSourceOffset(sourceOffset, displayOffset)) {
    return cursorRect(sourceOffset);
  }
  return textLayoutCursorRectForDisplayOffset(displayOffset);
}

QVector<QRectF> InlineLayout::selectionRects(qsizetype startOffset, qsizetype endOffset) const {
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

int InlineLayout::mathAtomCount() const {
  return mathAtoms_.size();
}

void InlineLayout::buildOffsetMapFromProjection() {
  displayText_ = projection_.displayText();
  offsetMap_.clear();
  offsetMap_.reserve(projection_.spans().size());
  for (const InlineProjectionSpan& span : projection_.spans()) {
    offsetMap_.push_back(OffsetMapEntry{span.displayStart, span.displayEnd, span.visibleStart, span.visibleEnd});
  }
}

void InlineLayout::buildMathAtoms(const QVector<InlineNode>& inlines, const RenderTheme& theme) {
  const QString projectedDisplay = projection_.displayText();
  QString rebuiltDisplay;
  QVector<OffsetMapEntry> rebuiltMap;
  for (const InlineProjectionSpan& span : projection_.spans()) {
    if (span.displayEnd <= span.displayStart) {
      continue;
    }

    const bool mathText = span.type == InlineType::InlineMath && span.kind == InlineSpanKind::Text;
    bool hasVisibleMarker = false;
    if (mathText) {
      for (const InlineProjectionSpan& marker : projection_.spans()) {
        if (marker.type == InlineType::InlineMath &&
            (marker.kind == InlineSpanKind::OpenMarker || marker.kind == InlineSpanKind::CloseMarker) &&
            marker.sourceStart >= span.sourceStart && marker.sourceEnd <= span.sourceEnd) {
          hasVisibleMarker = true;
          break;
        }
      }
    }
    const bool collapsed = mathText && !hasVisibleMarker;
    const QString spanText = projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
    if (!collapsed) {
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      continue;
    }

    const QString tex = texForInlineMathVisibleRange(inlines, span.visibleStart, span.visibleEnd);
    if (tex.isEmpty()) {
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      continue;
    }
    auto layout = std::make_shared<math::MathLayoutResult>(mathRenderer_.render(tex, theme, false));
    if (!layout->valid()) {
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      continue;
    }

    const qsizetype displayStart = rebuiltDisplay.size();
    rebuiltDisplay += kInlineMathPlaceholder;
    MathAtom atom;
    atom.displayStart = displayStart;
    atom.displayEnd = rebuiltDisplay.size();
    atom.sourceStart = span.sourceStart;
    atom.sourceEnd = span.sourceEnd;
    atom.contentSourceStart = span.contentSourceStart;
    atom.contentSourceEnd = span.contentSourceEnd;
    atom.visibleStart = span.visibleStart;
    atom.visibleEnd = span.visibleEnd;
    atom.layout = std::move(layout);
    rebuiltMap.push_back(OffsetMapEntry{atom.displayStart, atom.displayEnd, atom.visibleStart, atom.visibleEnd});
    mathAtoms_.push_back(std::move(atom));
  }
  if (!rebuiltDisplay.isEmpty()) {
    displayText_ = std::move(rebuiltDisplay);
    offsetMap_ = std::move(rebuiltMap);
  }
}

QString InlineLayout::texForInlineMathVisibleRange(const QVector<InlineNode>& inlines, qsizetype visibleStart, qsizetype visibleEnd) const {
  qsizetype offset = 0;
  const auto visit = [&](const auto& self, const QVector<InlineNode>& nodes) -> QString {
    for (const InlineNode& node : nodes) {
      const QString plain = InlineProjection::plainTextForInlines(QVector<InlineNode>{node});
      const qsizetype start = offset;
      const qsizetype end = start + plain.size();
      if (node.type() == InlineType::InlineMath && start == visibleStart && end == visibleEnd) {
        return node.text();
      }
      offset = end;
      if (!node.children().isEmpty()) {
        qsizetype childOffset = start;
        Q_UNUSED(childOffset);
        const QString found = self(self, node.children());
        if (!found.isEmpty()) {
          return found;
        }
      }
    }
    return {};
  };
  return visit(visit, inlines);
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
    const qreal lineHeight = std::ceil(line.height() * 1.16);
    line.setPosition(QPointF(0.0, height + (lineHeight - line.height()) * 0.5));
    height += lineHeight;
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
  }
  textLayout_->endLayout();
  size_ = QSizeF(qMin(lineWidth, qMax<qreal>(maxWidth, 1.0)), height);
}

void InlineLayout::paintTextLayoutMathAtoms(QPainter& painter, QPointF origin) const {
  if (!textLayout_) {
    return;
  }
  for (const MathAtom& atom : mathAtoms_) {
    if (!atom.layout || !atom.layout->valid()) {
      continue;
    }
    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const QTextLine line = textLayout_->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      if (atom.displayStart < lineStart || atom.displayStart > lineEnd) {
        continue;
      }
      const qreal x = line.cursorToX(static_cast<int>(atom.displayStart));
      const qreal baseline = origin.y() + line.y() + line.ascent();
      atom.layout->paint(painter, QPointF(origin.x() + x, baseline - atom.layout->baseline));
      break;
    }
  }
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
        if (span.kind != InlineSpanKind::OpenMarker && span.kind != InlineSpanKind::CloseMarker &&
            span.kind != InlineSpanKind::HiddenSyntax && span.kind != InlineSpanKind::EmptyContentSlot) {
          format.setForeground(theme.linkColor());
          format.setFontUnderline(true);
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

  for (const MathAtom& atom : mathAtoms_) {
    if (!atom.layout || !atom.layout->valid() || atom.displayEnd <= atom.displayStart) {
      continue;
    }
    QFont placeholderFont = baseFont;
    const QFontMetricsF baseMetrics(baseFont);
    if (baseMetrics.height() > 0.0 && atom.layout->size.height() > baseMetrics.height() && baseFont.pointSizeF() > 0.0) {
      placeholderFont.setPointSizeF(baseFont.pointSizeF() * atom.layout->size.height() / baseMetrics.height());
    }
    const QFontMetricsF placeholderMetrics(placeholderFont);
    const qreal placeholderAdvance = placeholderMetrics.horizontalAdvance(kInlineMathPlaceholder);
    placeholderFont.setLetterSpacing(QFont::AbsoluteSpacing, atom.layout->size.width() - placeholderAdvance);

    QTextCharFormat format = baseFormat;
    format.setFont(placeholderFont);
    format.setForeground(QColor(Qt::transparent));
    QTextLayout::FormatRange range;
    range.start = static_cast<int>(atom.displayStart);
    range.length = static_cast<int>(atom.displayEnd - atom.displayStart);
    range.format = format;
    formats.push_back(range);
  }
  return formats;
}

qsizetype InlineLayout::visibleOffsetForDisplayOffset(qsizetype displayOffset) const {
  if (offsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, displayOffset, plainText_.size());
  }
  displayOffset = qBound<qsizetype>(0, displayOffset, displayText_.size());
  for (const MathAtom& atom : mathAtoms_) {
    if (displayOffset > atom.displayStart && displayOffset < atom.displayEnd) {
      return atom.visibleEnd;
    }
  }
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
  for (const MathAtom& atom : mathAtoms_) {
    if (visibleOffset > atom.visibleStart && visibleOffset < atom.visibleEnd) {
      return atom.displayEnd;
    }
  }
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
    const QRectF fullLineRect(0.0, line.y(), qMax(size_.width(), naturalRect.width()), line.height());
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
