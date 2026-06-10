#include "render/InlineLayout.h"

#include "render/ImageDecoder.h"
#include "render/ImageLoader.h"
#include "render/ImagePlaceholder.h"

#include <QPainter>
#include <QPen>
#include <QTextCharFormat>
#include <QTextLine>
#include <QTextOption>

#include <cmath>

namespace muffin {
namespace {

constexpr QChar kInlineMathPlaceholder(0x00a0);
constexpr QChar kImagePlaceholder(0x2009);  // thin space, distinct from math placeholder
constexpr QChar kTabIndentSourceChar(0x200b);
constexpr QChar kTabIndentLayoutChar(0x00a0);
constexpr qreal kMaxImageDisplayHeight = 200.0;

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

QString layoutTextForDisplayText(QString text) {
  text.replace(kTabIndentSourceChar, kTabIndentLayoutChar);
  return text;
}

bool isImageSourceRevealed(const QVector<InlineProjectionSpan>& spans, const InlineProjectionSpan& imageSpan) {
  for (const InlineProjectionSpan& span : spans) {
    if (span.type != InlineType::Image ||
        (span.kind != InlineSpanKind::OpenMarker && span.kind != InlineSpanKind::HiddenSyntax)) {
      continue;
    }
    if (span.sourceStart >= imageSpan.sourceStart && span.sourceEnd <= imageSpan.sourceEnd) {
      return true;
    }
  }
  return false;
}

QSizeF scaledImageDisplaySize(const QImage& image) {
  if (image.isNull()) {
    return QSizeF();
  }
  if (image.height() > kMaxImageDisplayHeight) {
    const qreal scale = kMaxImageDisplayHeight / image.height();
    return QSizeF(image.width() * scale, kMaxImageDisplayHeight);
  }
  return QSizeF(image.width(), image.height());
}

}  // namespace

struct InlineLayout::TextLayoutPointHit {
  qsizetype displayOffset = 0;
  InlineProjectionBias bias = InlineProjectionBias::Backward;
  QRectF cursorRect;
};

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
  imageAtoms_.clear();
  previewAtoms_.clear();
  previewHeight_ = 0.0;
  htmlFormatSpans_.clear();
  displayOffsetMap_.clear();
  displayText_.clear();
  layoutText_.clear();
  textLayoutCodeBackgroundColor_ = theme.codeBackgroundColor();
  textLayoutCodeBorderColor_ = theme.codeBorderColor();
  projection_ = InlineProjection(inlines, std::move(sourceText), options.projectionState, options.sourceBase, baseFont.pointSizeF());
  buildOffsetMapFromProjection();
  buildMathAtoms(inlines, theme, width);
  buildImageAtoms(inlines, theme, width);
  buildHtmlFormatSpans();
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
  paintTextLayoutHtmlBackgrounds(painter, origin);
  paintTextLayoutCodeSpans(painter, origin);
  paintTextLayoutHtmlKeyboardSpans(painter, origin);
  textLayout_->draw(&painter, origin);
  paintTextLayoutMathAtoms(painter, origin);
  paintTextLayoutImageAtoms(painter, origin);
  paintImagePreview(painter, origin);
  painter.restore();
}

qsizetype InlineLayout::hitTestTextOffset(QPointF localPos) const {
  return visibleOffsetForDisplayOffset(textLayoutDisplayOffsetForPoint(localPos));
}

qsizetype InlineLayout::hitTestSourceOffset(QPointF localPos) const {
  const TextLayoutPointHit hit = textLayoutHitForPoint(localPos);
  const qsizetype position = hit.displayOffset;
  for (const MathAtom& atom : mathAtoms_) {
    if (position < atom.displayStart || position > atom.displayEnd) {
      continue;
    }
    qreal atomLeft = textLayoutCursorRectForDisplayOffset(atom.displayStart).left();
    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const QTextLine line = textLayout_->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      if (atom.displayStart >= lineStart && atom.displayStart <= lineEnd) {
        atomLeft = line.cursorToX(static_cast<int>(atom.displayStart));
        break;
      }
    }
    const qreal atomWidth = atom.layout && atom.layout->valid() ? atom.layout->size.width() : 0.0;
    if (atomWidth <= 0.0 || atom.contentSourceEnd <= atom.contentSourceStart) {
      return localPos.x() > atomLeft ? atom.sourceEnd : atom.sourceStart;
    }
    const qreal ratio = qBound<qreal>(0.0, (localPos.x() - atomLeft) / atomWidth, 1.0);
    const qsizetype contentLength = atom.contentSourceEnd - atom.contentSourceStart;
    const qsizetype contentOffset = static_cast<qsizetype>(qRound(ratio * contentLength));
    return atom.contentSourceStart + qBound<qsizetype>(0, contentOffset, contentLength);
  }
  const qsizetype projPosition = projectionDisplayOffsetForLayoutOffset(position, hit.bias);
  for (const InlineProjectionSpan& span : projection_.spans()) {
    if ((span.type != InlineType::InlineMath && span.type != InlineType::Code) ||
        span.kind != InlineSpanKind::OpenMarker ||
        span.displayEnd <= span.displayStart ||
        projPosition < span.displayStart ||
        projPosition > span.displayEnd) {
      continue;
    }
    const QRectF markerStart = textLayoutCursorRectForDisplayOffset(span.displayStart);
    const QRectF markerEnd = textLayoutCursorRectForDisplayOffset(span.displayEnd);
    const qreal markerLeft = qMin(markerStart.left(), markerEnd.left());
    const qreal markerRight = qMax(markerStart.left(), markerEnd.left());
    if (localPos.x() >= markerLeft && localPos.x() <= markerRight) {
      return span.sourceEnd;
    }
  }
  qsizetype sourceOffset = -1;
  if (projection_.sourceOffsetForDisplayOffset(projPosition, hit.bias, sourceOffset)) {
    return sourceOffset;
  }
  return visibleOffsetForDisplayOffset(position);
}

QRectF InlineLayout::hitTestCursorRect(QPointF localPos) const {
  return textLayoutHitForPoint(localPos).cursorRect;
}

QString InlineLayout::linkHrefAtLocalPos(QPointF localPos) const {
  if (!textLayout_) return {};
  const qsizetype layoutOffset = textLayoutDisplayOffsetForPoint(localPos);
  const qsizetype projOffset = projectionDisplayOffsetForLayoutOffset(layoutOffset, InlineProjectionBias::Backward);
  return projection_.linkHrefAtDisplayOffset(projOffset);
}

QString InlineLayout::imageSrcAtLocalPos(QPointF localPos) const {
  if (!textLayout_) return {};
  const qsizetype position = textLayoutDisplayOffsetForPoint(localPos);
  for (const ImageAtom& atom : imageAtoms_) {
    if (position >= atom.displayStart && position <= atom.displayEnd) {
      return atom.srcUrl;
    }
  }
  return {};
}

QRectF InlineLayout::cursorRect(qsizetype textOffset) const {
  for (const MathAtom& atom : mathAtoms_) {
    if (textOffset > atom.visibleStart && textOffset < atom.visibleEnd) {
      textOffset = textOffset - atom.visibleStart < atom.visibleEnd - textOffset ? atom.visibleStart : atom.visibleEnd;
      break;
    }
  }
  for (const ImageAtom& atom : imageAtoms_) {
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
  for (const ImageAtom& atom : imageAtoms_) {
    if (sourceOffset > atom.sourceStart && sourceOffset < atom.sourceEnd) {
      const qsizetype displayOffset = sourceOffset - atom.sourceStart < atom.sourceEnd - sourceOffset ? atom.displayStart : atom.displayEnd;
      return textLayoutCursorRectForDisplayOffset(displayOffset);
    }
  }
  qsizetype displayOffset = -1;
  if (!layoutDisplayOffsetForSourceOffset(sourceOffset, InlineProjectionBias::Forward, displayOffset)) {
    return cursorRect(sourceOffset);
  }
  return textLayoutCursorRectForDisplayOffset(displayOffset);
}

QVector<QRectF> InlineLayout::selectionRects(qsizetype startOffset, qsizetype endOffset) const {
  const qsizetype startDisplayOffset = displayOffsetForVisibleOffset(qMin(startOffset, endOffset));
  const qsizetype endDisplayOffset = displayOffsetForVisibleOffset(qMax(startOffset, endOffset));
  return selectionRectsForDisplayOffsets(startDisplayOffset, endDisplayOffset);
}

QVector<QRectF> InlineLayout::selectionRectsForSourceOffsets(qsizetype startSourceOffset, qsizetype endSourceOffset) const {
  qsizetype startDisplayOffset = -1;
  qsizetype endDisplayOffset = -1;
  if (!layoutDisplayOffsetForSourceOffset(qMin(startSourceOffset, endSourceOffset), InlineProjectionBias::Backward, startDisplayOffset) ||
      !layoutDisplayOffsetForSourceOffset(qMax(startSourceOffset, endSourceOffset), InlineProjectionBias::Forward, endDisplayOffset)) {
    return {};
  }
  return selectionRectsForDisplayOffsets(startDisplayOffset, endDisplayOffset);
}

QVector<QRectF> InlineLayout::selectionRectsForDisplayOffsets(qsizetype startDisplayOffset, qsizetype endDisplayOffset) const {
  QVector<QRectF> rects;
  if (!textLayout_) {
    return rects;
  }

  const int start = qBound(0, static_cast<int>(qMin(startDisplayOffset, endDisplayOffset)), static_cast<int>(displayText_.size()));
  const int end = qBound(0, static_cast<int>(qMax(startDisplayOffset, endDisplayOffset)), static_cast<int>(displayText_.size()));
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
      const DisplayOffsetRange layoutRange = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
      if (!layoutRange.valid) {
        continue;
      }
      const int rangeStart = qMax(lineStart, static_cast<int>(layoutRange.start));
      const int rangeEnd = qMin(lineEnd, static_cast<int>(layoutRange.end));
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

void InlineLayout::paintTextLayoutHtmlBackgrounds(QPainter& painter, QPointF origin) const {
  if (!textLayout_ || htmlFormatSpans_.isEmpty()) {
    return;
  }

  painter.save();
  for (const HtmlFormatSpan& hs : htmlFormatSpans_) {
    if (!hs.backgroundColor.isValid() || hs.backgroundColor.alpha() == 0 || hs.layoutEnd <= hs.layoutStart) {
      continue;
    }

    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const QTextLine line = textLayout_->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      const int rangeStart = qMax(lineStart, hs.layoutStart);
      const int rangeEnd = qMin(lineEnd, hs.layoutEnd);
      if (rangeStart >= rangeEnd) {
        continue;
      }
      const qreal x1 = line.cursorToX(rangeStart);
      const qreal x2 = line.cursorToX(rangeEnd);
      const QRectF rect(
          origin.x() + qMin(x1, x2) - 1.0,
          origin.y() + line.y() + 1.0,
          qAbs(x2 - x1) + 2.0,
          qMax<qreal>(1.0, line.height() - 2.0));
      painter.setPen(Qt::NoPen);
      painter.setBrush(hs.backgroundColor);
      painter.drawRoundedRect(rect, 2.0, 2.0);
    }
  }
  painter.restore();
}

void InlineLayout::paintTextLayoutHtmlKeyboardSpans(QPainter& painter, QPointF origin) const {
  if (!textLayout_ || htmlFormatSpans_.isEmpty()) {
    return;
  }

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (const HtmlFormatSpan& hs : htmlFormatSpans_) {
    if (!hs.keyboard || hs.layoutEnd <= hs.layoutStart) {
      continue;
    }

    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const QTextLine line = textLayout_->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      const int rangeStart = qMax(lineStart, hs.layoutStart);
      const int rangeEnd = qMin(lineEnd, hs.layoutEnd);
      if (rangeStart >= rangeEnd) {
        continue;
      }

      const qreal x1 = line.cursorToX(rangeStart);
      const qreal x2 = line.cursorToX(rangeEnd);
      const QRectF rect(
          origin.x() + qMin(x1, x2) - 4.0,
          origin.y() + line.y() + 1.0,
          qAbs(x2 - x1) + 8.0,
          qMax<qreal>(1.0, line.height() - 3.0));

      painter.setPen(QPen(QColor(196, 201, 209), 1.0));
      painter.setBrush(QColor(250, 251, 252));
      painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);

      painter.setPen(QPen(QColor(181, 186, 194), 1.0));
      painter.drawLine(rect.bottomLeft() + QPointF(2.0, -0.5), rect.bottomRight() + QPointF(-2.0, -0.5));
    }
  }
  painter.restore();
}

QString InlineLayout::plainText() const {
  return plainText_;
}

QString InlineLayout::visibleText() const {
  return projection_.visibleText();
}

QString InlineLayout::displayText() const {
  return displayText_;
}

int InlineLayout::mathAtomCount() const {
  return mathAtoms_.size();
}

QVector<QTextLayout::FormatRange> InlineLayout::debugTextFormats(const RenderTheme& theme, const QFont& baseFont) const {
  return textLayoutFormats(theme, baseFont);
}

void InlineLayout::buildOffsetMapFromProjection() {
  displayText_ = projection_.displayText();
  offsetMap_.clear();
  displayOffsetMap_.clear();
  offsetMap_.reserve(projection_.spans().size());
  displayOffsetMap_.reserve(projection_.spans().size());
  for (const InlineProjectionSpan& span : projection_.spans()) {
    offsetMap_.push_back(OffsetMapEntry{span.displayStart, span.displayEnd, span.visibleStart, span.visibleEnd});
    displayOffsetMap_.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, span.displayStart, span.displayEnd});
  }
}

void InlineLayout::buildHtmlFormatSpans() {
  htmlFormatSpans_.clear();
  const auto& data = projection_.htmlFormatData();
  for (const auto& hd : data) {
    for (const auto& fs : hd.formatSpans) {
      // Map projection display offsets → layout display offsets
      const DisplayOffsetRange layoutRange =
          layoutDisplayRangeForProjectionRange(hd.displayStart + fs.start, hd.displayStart + fs.start + fs.length);
      if (!layoutRange.valid) {
        continue;
      }
      HtmlFormatSpan hs;
      hs.layoutStart = static_cast<int>(layoutRange.start);
      hs.layoutEnd = static_cast<int>(layoutRange.end);
      hs.bold = fs.bold;
      hs.italic = fs.italic;
      hs.monospace = fs.monospace;
      hs.decoration = fs.decoration;
      hs.color = fs.color;
      hs.backgroundColor = fs.backgroundColor;
      hs.fontSize = fs.fontSize;
      hs.verticalAlignment = fs.verticalAlignment;
      hs.keyboard = fs.keyboard;
      htmlFormatSpans_.push_back(hs);
    }
    // Register link ranges from inline HTML <a> tags
    for (const auto& link : hd.links) {
      // Links are already registered in InlineProjection via linkRanges_
    }
  }
}

void InlineLayout::buildMathAtoms(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width) {
  const QString projectedDisplay = projection_.displayText();
  QString rebuiltDisplay;
  QVector<OffsetMapEntry> rebuiltMap;
  QVector<DisplayOffsetMapEntry> rebuiltDisplayMap;
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
      rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }

    const QString tex = texForInlineMathVisibleRange(inlines, span.visibleStart, span.visibleEnd);
    if (tex.isEmpty()) {
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }
    auto layout = std::make_shared<math::MathLayoutResult>(mathRenderer_.render(tex, theme, false, qMax<qreal>(1.0, width)));
    if (!layout->valid()) {
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
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
    rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, atom.displayStart, atom.displayEnd});
    mathAtoms_.push_back(std::move(atom));
  }
  if (!rebuiltDisplay.isEmpty()) {
    displayText_ = std::move(rebuiltDisplay);
    offsetMap_ = std::move(rebuiltMap);
    displayOffsetMap_ = std::move(rebuiltDisplayMap);
  }
}

QString InlineLayout::texForInlineMathVisibleRange(const QVector<InlineNode>& inlines, qsizetype visibleStart, qsizetype visibleEnd) const {
  // Extract the expected math content from the projected display text
  const QString expected = projection_.displayText().mid(visibleStart, visibleEnd - visibleStart);
  // Find the matching InlineMath node by DFS — text content must match
  const auto visit = [&](const auto& self, const QVector<InlineNode>& nodes) -> QString {
    for (const InlineNode& node : nodes) {
      if (node.type() == InlineType::InlineMath && node.text() == expected) {
        return node.text();
      }
      if (!node.children().isEmpty()) {
        const QString found = self(self, node.children());
        if (!found.isEmpty()) {
          return found;
        }
      }
    }
    return QString();
  };
  return visit(visit, inlines);
}

void InlineLayout::buildImageAtoms(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width) {
  Q_UNUSED(inlines);
  Q_UNUSED(theme);
  Q_UNUSED(width);

  // Quick check: are there any image Atom spans at all?
  bool hasImageAtom = false;
  for (const InlineProjectionSpan& span : projection_.spans()) {
    if (span.type == InlineType::Image && span.kind == InlineSpanKind::Atom && span.displayEnd > span.displayStart) {
      hasImageAtom = true;
      break;
    }
  }
  if (!hasImageAtom) {
    return;
  }

  // Iterate projection spans directly (same pattern as buildMathAtoms).
  const QString projectedDisplay = projection_.displayText();
  QString rebuiltDisplay;
  QVector<OffsetMapEntry> rebuiltMap;
  QVector<DisplayOffsetMapEntry> rebuiltDisplayMap;

  for (const InlineProjectionSpan& span : projection_.spans()) {
    if (span.displayEnd <= span.displayStart) {
      continue;
    }

    const bool isImageAtom = span.type == InlineType::Image && span.kind == InlineSpanKind::Atom;
    if (!isImageAtom) {
      const QString spanText = projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }

    const bool collapsed = !isImageSourceRevealed(projection_.spans(), span);

    const QString srcUrl = span.href;
    if (srcUrl.isEmpty()) {
      const QString spanText = projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }

    // Try to load the image
    QImage image;
    const bool isRemote = srcUrl.startsWith(QStringLiteral("http:")) || srcUrl.startsWith(QStringLiteral("https:"));
    if (isRemote) {
      image = ImageLoader::instance().cached(srcUrl);
      if (image.isNull()) {
        ImageLoader::instance().request(srcUrl);
      }
    } else {
      if (!image.load(srcUrl)) {
        image = image_decoder::decodeFileFallback(srcUrl);
      }
    }

    if (image.isNull()) {
      // Image not yet available — show a placeholder icon inline.
      const bool isLoading = isRemote && ImageLoader::instance().isPending(srcUrl);
      constexpr qreal kPlaceholderSize = 24.0;
      QImage placeholder = isLoading
          ? image_placeholder::loading(QSizeF(kPlaceholderSize, kPlaceholderSize))
          : image_placeholder::broken(QSizeF(kPlaceholderSize, kPlaceholderSize));

      if (!placeholder.isNull() && collapsed) {
        const qsizetype displayStart = rebuiltDisplay.size();
        rebuiltDisplay += kImagePlaceholder;

        ImageAtom atom;
        atom.displayStart = displayStart;
        atom.displayEnd = rebuiltDisplay.size();
        atom.sourceStart = span.sourceStart;
        atom.sourceEnd = span.sourceEnd;
        atom.visibleStart = span.visibleStart;
        atom.visibleEnd = span.visibleEnd;
        atom.srcUrl = srcUrl;
        atom.displaySize = QSizeF(kPlaceholderSize, kPlaceholderSize);
        atom.image = placeholder;
        atom.loaded = true;

        rebuiltMap.push_back(OffsetMapEntry{atom.displayStart, atom.displayEnd, atom.visibleStart, atom.visibleEnd});
        rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, atom.displayStart, atom.displayEnd});
        imageAtoms_.push_back(std::move(atom));
        continue;
      }

      // Fallback: keep the alt text as-is (placeholder render failed or cursor is on the image)
      const QString spanText = projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }

    if (collapsed) {
      // Inactive: replace alt text with placeholder, render image inline.
      const QSizeF displaySize = scaledImageDisplaySize(image);

      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += kImagePlaceholder;

      ImageAtom atom;
      atom.displayStart = displayStart;
      atom.displayEnd = rebuiltDisplay.size();
      atom.sourceStart = span.sourceStart;
      atom.sourceEnd = span.sourceEnd;
      atom.visibleStart = span.visibleStart;
      atom.visibleEnd = span.visibleEnd;
      atom.srcUrl = srcUrl;
      atom.displaySize = displaySize;
      atom.image = image;
      atom.loaded = true;

      rebuiltMap.push_back(OffsetMapEntry{atom.displayStart, atom.displayEnd, atom.visibleStart, atom.visibleEnd});
      rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, atom.displayStart, atom.displayEnd});
      imageAtoms_.push_back(std::move(atom));
    } else {
      // Active (cursor on image): show source text as-is, add block preview below.
      const QString spanText = projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});

      ImageAtom preview;
      preview.srcUrl = srcUrl;
      preview.displaySize = scaledImageDisplaySize(image);
      preview.image = image;
      preview.loaded = true;
      previewAtoms_.push_back(std::move(preview));
    }
  }

  if (!rebuiltDisplay.isEmpty()) {
    displayText_ = std::move(rebuiltDisplay);
    offsetMap_ = std::move(rebuiltMap);
    displayOffsetMap_ = std::move(rebuiltDisplayMap);
  }
}

void InlineLayout::buildTextLayout(const RenderTheme& theme, qreal width, const QFont& baseFont) {
  layoutText_ = layoutTextForDisplayText(displayText_);
  textLayout_ = std::make_unique<QTextLayout>(layoutText_.isEmpty() ? QStringLiteral(" ") : layoutText_, baseFont);
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
    // Ensure the line is tall enough for any image atoms on this line.
    qreal minLineHeight = line.height();
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    for (const ImageAtom& atom : imageAtoms_) {
      if (atom.loaded && atom.displayStart >= lineStart && atom.displayStart <= lineEnd) {
        minLineHeight = qMax(minLineHeight, atom.displaySize.height());
      }
    }
    const qreal lineHeight = std::ceil(minLineHeight * 1.16);
    line.setPosition(QPointF(0.0, height + (lineHeight - minLineHeight) * 0.5));
    height += lineHeight;
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
  }
  textLayout_->endLayout();

  // Reserve space for block-level image previews (shown when cursor is on an image).
  constexpr qreal kPreviewSpacing = 6.0;
  previewHeight_ = 0.0;
  for (const ImageAtom& atom : previewAtoms_) {
    previewHeight_ += atom.displaySize.height() + kPreviewSpacing;
  }
  size_ = QSizeF(qMin(lineWidth, qMax<qreal>(maxWidth, 1.0)), height + previewHeight_);
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

void InlineLayout::paintTextLayoutImageAtoms(QPainter& painter, QPointF origin) const {
  if (!textLayout_) {
    return;
  }
  for (const ImageAtom& atom : imageAtoms_) {
    if (!atom.loaded || atom.image.isNull()) {
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
      // Vertically center the image within the line's allocated height.
      // line.y() already includes the vertical centering offset applied in buildTextLayout(),
      // so we measure from the line's natural top (before centering) to get the correct position.
      const qreal lineTop = origin.y() + line.y() - (std::ceil(line.height() * 1.16) - line.height()) * 0.5;
      const qreal y = lineTop;
      const QRectF targetRect(origin.x() + x, y, atom.displaySize.width(), atom.displaySize.height());
      painter.drawImage(targetRect, atom.image, QRectF(atom.image.rect()));
      break;
    }
  }
}

void InlineLayout::paintImagePreview(QPainter& painter, QPointF origin) const {
  if (previewAtoms_.isEmpty() || !textLayout_) {
    return;
  }
  // Paint preview images below the text layout, left-aligned.
  constexpr qreal kPreviewSpacing = 6.0;
  const qreal textHeight = size_.height() - previewHeight_;
  qreal y = origin.y() + textHeight + kPreviewSpacing;
  for (const ImageAtom& atom : previewAtoms_) {
    if (!atom.loaded || atom.image.isNull()) {
      continue;
    }
    const QRectF targetRect(origin.x(), y, atom.displaySize.width(), atom.displaySize.height());
    painter.drawImage(targetRect, atom.image, QRectF(atom.image.rect()));
    y += atom.displaySize.height() + kPreviewSpacing;
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
    if (span.bold) {
      format.setFontWeight(QFont::Bold);
    }
    if (span.italic) {
      format.setFontItalic(true);
    }
    if (span.strike) {
      format.setFontStrikeOut(true);
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
      default:
        break;
    }
    const DisplayOffsetRange layoutRange = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
    if (!layoutRange.valid || layoutRange.end > displayText_.size()) {
      continue;
    }
    QTextLayout::FormatRange range;
    range.start = static_cast<int>(layoutRange.start);
    range.length = static_cast<int>(layoutRange.end - layoutRange.start);
    range.format = format;
    formats.push_back(range);
  }

  const QFontMetricsF tabMetrics(baseFont);
  const qreal tabIndentTargetWidth = qMax<qreal>(1.0, tabMetrics.horizontalAdvance(QStringLiteral("汉汉")));
  const qreal tabIndentPlaceholderWidth = qMax<qreal>(0.0, tabMetrics.horizontalAdvance(QString(kTabIndentLayoutChar)));
  for (qsizetype i = 0; i < displayText_.size(); ++i) {
    if (displayText_.at(i) != kTabIndentSourceChar) {
      continue;
    }
    QFont tabFont = baseFont;
    tabFont.setLetterSpacing(QFont::AbsoluteSpacing, tabIndentTargetWidth - tabIndentPlaceholderWidth);
    QTextCharFormat format = baseFormat;
    format.setFont(tabFont);
    format.setForeground(QColor(Qt::transparent));
    QTextLayout::FormatRange range;
    range.start = static_cast<int>(i);
    range.length = 1;
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

  for (const ImageAtom& atom : imageAtoms_) {
    if (!atom.loaded || atom.displayEnd <= atom.displayStart) {
      continue;
    }
    QFont placeholderFont = baseFont;
    const QFontMetricsF baseMetrics(baseFont);
    if (atom.displaySize.height() > baseMetrics.height() && baseFont.pointSizeF() > 0.0) {
      placeholderFont.setPointSizeF(baseFont.pointSizeF() * atom.displaySize.height() / baseMetrics.height());
    }
    const QFontMetricsF placeholderMetrics(placeholderFont);
    const qreal placeholderAdvance = placeholderMetrics.horizontalAdvance(kImagePlaceholder);
    placeholderFont.setLetterSpacing(QFont::AbsoluteSpacing, atom.displaySize.width() - placeholderAdvance);

    QTextCharFormat format = baseFormat;
    format.setFont(placeholderFont);
    format.setForeground(QColor(Qt::transparent));
    QTextLayout::FormatRange range;
    range.start = static_cast<int>(atom.displayStart);
    range.length = static_cast<int>(atom.displayEnd - atom.displayStart);
    range.format = format;
    formats.push_back(range);
  }

  // Apply HTML inline format spans (from <b>, <i>, <span style="...">, etc.)
  for (const HtmlFormatSpan& hs : htmlFormatSpans_) {
    if (hs.layoutEnd <= hs.layoutStart) {
      continue;
    }
    QTextCharFormat format = baseFormat;
    if (hs.bold) {
      format.setFontWeight(QFont::Bold);
    }
    if (hs.italic) {
      format.setFontItalic(true);
    }
    if (hs.monospace) {
      format.setFontFamily(QStringLiteral("Courier New"));
    }
    if (hs.keyboard) {
      format.setFontFamily(QStringLiteral("Courier New"));
    }
    if (hs.color.isValid()) {
      format.setForeground(hs.color);
    }
    if (hs.fontSize > 0 && baseFont.pointSizeF() > 0) {
      format.setFontPointSize(hs.fontSize);
    }
    if (hs.verticalAlignment != QTextCharFormat::AlignNormal) {
      format.setVerticalAlignment(hs.verticalAlignment);
    }
    if (hasDecoration(hs.decoration, html::HtmlTextDecoration::Underline)) {
      format.setFontUnderline(true);
    }
    if (hasDecoration(hs.decoration, html::HtmlTextDecoration::LineThrough)) {
      format.setFontStrikeOut(true);
    }
    if (!hs.href.isEmpty()) {
      format.setForeground(theme.linkColor());
      format.setFontUnderline(true);
    }
    QTextLayout::FormatRange range;
    range.start = hs.layoutStart;
    range.length = hs.layoutEnd - hs.layoutStart;
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
  for (const ImageAtom& atom : imageAtoms_) {
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
  for (const ImageAtom& atom : imageAtoms_) {
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

qsizetype InlineLayout::projectionDisplayOffsetForLayoutOffset(qsizetype layoutOffset, InlineProjectionBias bias) const {
  if (displayOffsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, layoutOffset, projection_.displayText().size());
  }

  layoutOffset = qBound<qsizetype>(0, layoutOffset, displayText_.size());
  for (const DisplayOffsetMapEntry& entry : displayOffsetMap_) {
    if (layoutOffset < entry.layoutStart || layoutOffset > entry.layoutEnd) {
      continue;
    }
    if (entry.layoutEnd <= entry.layoutStart || entry.projectionEnd <= entry.projectionStart) {
      return bias == InlineProjectionBias::Forward ? entry.projectionEnd : entry.projectionStart;
    }
    if (layoutOffset <= entry.layoutStart) {
      return entry.projectionStart;
    }
    if (layoutOffset >= entry.layoutEnd) {
      return entry.projectionEnd;
    }
    const qsizetype projectionLength = entry.projectionEnd - entry.projectionStart;
    const qsizetype layoutLength = entry.layoutEnd - entry.layoutStart;
    const qsizetype delta = (layoutOffset - entry.layoutStart) * projectionLength / layoutLength;
    return qBound<qsizetype>(entry.projectionStart, entry.projectionStart + delta, entry.projectionEnd);
  }

  return projection_.displayText().size();
}

qsizetype InlineLayout::layoutDisplayOffsetForProjectionOffset(qsizetype projectionOffset, InlineProjectionBias bias) const {
  if (displayOffsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, projectionOffset, displayText_.size());
  }

  projectionOffset = qBound<qsizetype>(0, projectionOffset, projection_.displayText().size());
  for (const DisplayOffsetMapEntry& entry : displayOffsetMap_) {
    if (projectionOffset < entry.projectionStart || projectionOffset > entry.projectionEnd) {
      continue;
    }
    if (entry.layoutEnd <= entry.layoutStart || entry.projectionEnd <= entry.projectionStart) {
      return bias == InlineProjectionBias::Forward ? entry.layoutEnd : entry.layoutStart;
    }
    if (projectionOffset <= entry.projectionStart) {
      return entry.layoutStart;
    }
    if (projectionOffset >= entry.projectionEnd) {
      return entry.layoutEnd;
    }
    const qsizetype projectionLength = entry.projectionEnd - entry.projectionStart;
    const qsizetype layoutLength = entry.layoutEnd - entry.layoutStart;
    const qsizetype delta = (projectionOffset - entry.projectionStart) * layoutLength / projectionLength;
    const qsizetype snapped = bias == InlineProjectionBias::Forward && delta == 0 ? 1 : delta;
    return qBound<qsizetype>(entry.layoutStart, entry.layoutStart + snapped, entry.layoutEnd);
  }

  return displayText_.size();
}

bool InlineLayout::layoutDisplayOffsetForSourceOffset(qsizetype sourceOffset, InlineProjectionBias bias, qsizetype& layoutOffset) const {
  qsizetype projectionOffset = -1;
  if (!projection_.displayOffsetForSourceOffset(sourceOffset, bias, projectionOffset)) {
    return false;
  }
  layoutOffset = layoutDisplayOffsetForProjectionOffset(projectionOffset, bias);
  return true;
}

InlineLayout::DisplayOffsetRange InlineLayout::layoutDisplayRangeForProjectionRange(qsizetype projectionStart, qsizetype projectionEnd) const {
  DisplayOffsetRange range;
  if (projectionEnd <= projectionStart) {
    return range;
  }
  range.start = layoutDisplayOffsetForProjectionOffset(projectionStart, InlineProjectionBias::Backward);
  range.end = layoutDisplayOffsetForProjectionOffset(projectionEnd, InlineProjectionBias::Forward);
  range.start = qBound<qsizetype>(0, range.start, displayText_.size());
  range.end = qBound<qsizetype>(0, range.end, displayText_.size());
  range.valid = range.end > range.start;
  return range;
}

qsizetype InlineLayout::textLayoutDisplayOffsetForPoint(QPointF localPos) const {
  return textLayoutHitForPoint(localPos).displayOffset;
}

InlineLayout::TextLayoutPointHit InlineLayout::textLayoutHitForPoint(QPointF localPos) const {
  TextLayoutPointHit hit;
  if (!textLayout_) {
    return hit;
  }

  QTextLine targetLine;
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const QTextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) {
      continue;
    }
    if (localPos.y() < line.y()) {
      break;
    }
    targetLine = line;
    if (localPos.y() <= line.y() + line.height()) {
      break;
    }
  }

  if (!targetLine.isValid() && textLayout_->lineCount() > 0) {
    targetLine = textLayout_->lineAt(0);
  }

  if (targetLine.isValid()) {
    const QTextLine line = targetLine;
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    if (lineEnd <= lineStart) {
      hit.displayOffset = lineStart;
      hit.bias = InlineProjectionBias::Backward;
      hit.cursorRect = QRectF(line.cursorToX(lineStart), line.y(), 1.0, line.height());
      return hit;
    }

    if (localPos.x() <= line.cursorToX(lineStart)) {
      hit.displayOffset = lineStart;
      hit.cursorRect = QRectF(line.cursorToX(lineStart), line.y(), 1.0, line.height());
      return hit;
    }
    if (localPos.x() >= line.cursorToX(lineEnd)) {
      hit.displayOffset = lineEnd;
      hit.bias = InlineProjectionBias::Forward;
      hit.cursorRect = QRectF(line.cursorToX(lineEnd), line.y(), 1.0, line.height());
      return hit;
    }

    for (int offset = lineStart; offset < lineEnd; ++offset) {
      const qreal left = line.cursorToX(offset);
      const qreal right = line.cursorToX(offset + 1);
      const qreal midpoint = (left + right) / 2.0;
      if (localPos.x() < midpoint) {
        hit.displayOffset = offset;
        hit.bias = InlineProjectionBias::Backward;
        hit.cursorRect = QRectF(line.cursorToX(offset), line.y(), 1.0, line.height());
        return hit;
      }
      if (localPos.x() < right) {
        hit.displayOffset = offset + 1;
        hit.bias = InlineProjectionBias::Forward;
        hit.cursorRect = QRectF(line.cursorToX(offset + 1), line.y(), 1.0, line.height());
        return hit;
      }
    }
    hit.displayOffset = lineEnd;
    hit.bias = InlineProjectionBias::Forward;
    hit.cursorRect = QRectF(line.cursorToX(lineEnd), line.y(), 1.0, line.height());
    return hit;
  }

  hit.displayOffset = displayText_.size();
  hit.bias = InlineProjectionBias::Forward;
  hit.cursorRect = textLayoutCursorRectForDisplayOffset(hit.displayOffset);
  return hit;
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
