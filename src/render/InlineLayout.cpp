#include "render/InlineLayout.h"

#include "document/ImageSyntaxOps.h"
#include "render/DecorationPainter.h"
#include "render/GradientPainter.h"
#include "render/ImageDecoder.h"
#include "render/ImageLoader.h"
#include "render/ImagePlaceholder.h"

#include <QPainter>
#include <QPen>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTextLine>
#include <QTextOption>

#include <cmath>

namespace muffin {
namespace {

constexpr QChar kInlineMathPlaceholder(0x00a0);
constexpr QChar kImagePlaceholder(0x2009);  // thin space, distinct from math placeholder
constexpr QChar kLinkBeforePlaceholder(0xe000);  // PUA: flow-reserved slot for a::before icons
constexpr QChar kTabIndentSourceChar(0x200b);
constexpr QChar kTabIndentLayoutChar(0x00a0);
constexpr qreal kMaxImageDisplayHeight = 200.0;

QString flattenPlainText(const QVector<InlineNode>& inlines, bool breakOnSingleNewline) {
  QString text;
  for (const InlineNode& node : inlines) {
    switch (node.type()) {
      case InlineType::Text:
      case InlineType::Code:
      case InlineType::InlineMath:
        text += node.text();
        break;
      case InlineType::HtmlInline:
        // <br> is a line break in plain text; other inline HTML keeps its literal text.
        if (isStandaloneBrTag(node.text())) {
          text += QLatin1Char('\n');
        } else {
          text += node.text();
        }
        break;
      case InlineType::SoftBreak:
        text += breakOnSingleNewline ? QLatin1Char('\n') : QLatin1Char(' ');
        break;
      case InlineType::LineBreak:
        text += QLatin1Char('\n');
        break;
      case InlineType::Image:
        text += node.alt();
        break;
      default:
        text += flattenPlainText(node.children(), breakOnSingleNewline);
        break;
    }
  }
  return text;
}

QString layoutTextForDisplayText(QString text) {
  text.replace(kTabIndentSourceChar, kTabIndentLayoutChar);
  // This Qt build's QTextLayout createLine() treats only U+2028 (Line Separator) as a hard
  // break, not '\n' (0x0a) — so a '\n' in the display text (a <br> or a Markdown hard break)
  // would not wrap. Convert '\n' to U+2028 for layout only. Length-preserving (1:1), so every
  // offset/coordinate computed from displayText_ stays valid; displayText_ keeps the '\n'.
  text.replace(QLatin1Char('\n'), QChar(0x2028));
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

QSizeF scaledImageDisplaySize(const QImage& image, qreal zoom) {
  if (image.isNull()) {
    return QSizeF();
  }
  // Apply the image's own zoom (style="zoom:N%") to its natural size, then enforce the
  // layout height cap so a zoomed-up or naturally tall image cannot blow out the line.
  QSizeF size(image.width() * zoom, image.height() * zoom);
  if (size.height() > kMaxImageDisplayHeight) {
    const qreal scale = kMaxImageDisplayHeight / size.height();
    return QSizeF(size.width() * scale, kMaxImageDisplayHeight);
  }
  return size;
}

// Linear RGB-A blend of two opaque theme colours, used to animate a `:hover { color }`
// between the element base colour and the hover target as the HoverAnimator phase ramps.
QColor lerpColor(const QColor& from, const QColor& to, qreal t) {
  t = qBound(0.0, t, 1.0);
  return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                qRound(from.green() + (to.green() - from.green()) * t),
                qRound(from.blue() + (to.blue() - from.blue()) * t),
                qRound(from.alpha() + (to.alpha() - from.alpha()) * t));
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
  plainText_ = flattenPlainText(inlines, options.breakOnSingleNewline);
  offsetMap_.clear();
  mathAtoms_.clear();
  imageAtoms_.clear();
  previewAtoms_.clear();
  previewHeight_ = 0.0;
  htmlFormatSpans_.clear();
  displayOffsetMap_.clear();
  displayText_.clear();
  layoutText_.clear();
  isMisspelled_ = options.isMisspelled;
  textLayoutCodeBackgroundColor_ = theme.codeBackgroundColor();
  textLayoutCodeBorderColor_ = theme.codeBorderColor();
  textLayoutCodeTextColor_ = theme.inlineCodeTextColor();
  darkTheme_ = theme.backgroundColor().lightness() < 128;
  kbdFill_ = theme.kbdBackgroundColor();
  kbdText_ = theme.kbdTextColor();
  kbdFont_ = theme.kbdFont();
  kbdPadH_ = theme.kbdPaddingH();
  kbdPadV_ = theme.kbdPaddingV();
  kbdRadius_ = theme.kbdBorderRadius();
  kbdBorder_ = theme.kbdBorderColor();
  kbdBorderWidth_ = theme.kbdBorderWidth();
  kbdBorderBottomWidth_ = theme.kbdBorderBottomWidth();
  kbdBorderBottomColor_ = theme.kbdBorderBottomColor();
  kbdShadow_ = theme.kbdShadowColor();
  codeBoxPaddingH_ = theme.inlineCodePaddingH();
  codeBoxPaddingV_ = theme.inlineCodePaddingV();
  codeBoxRadius_ = theme.inlineCodeBorderRadius();
  codeBoxBorderWidth_ = theme.inlineCodeBorderWidth();
  // CSS inline decorations: link ::before icon (mask-tinted SVG) + mark gradient.
  linkBeforeIcon_.clear();
  linkBeforeIconTint_ = QColor();
  linkBeforeIconFromMask_ = false;
  linkBeforeIconSize_ = QSizeF();
  linkBeforeIconMarginRight_ = 0.0;
  markGradient_ = GradientSpec{};
  for (const PseudoElementRule& r : theme.decorations().pseudos) {
    if (r.host == QStringLiteral("a") && r.pseudo == QStringLiteral("before") && !r.svgData.isEmpty()) {
      linkBeforeIcon_ = r.svgData;
      linkBeforeIconFromMask_ = r.svgFromMask;
      linkBeforeIconTint_ = r.color.isValid() ? r.color : r.backgroundColor;
      linkBeforeIconSize_ = r.size;
      linkBeforeIconMarginRight_ = r.marginRight;
    }
  }
  for (const ElementBackground& eb : theme.decorations().backgrounds) {
    if (eb.host == QStringLiteral("mark")) { markGradient_ = eb.gradient; }
  }
  baseTextColorOverride_ = options.baseTextColor;
  hoverTextColor_ = options.hoverTextColor;
  lineHeightMultiplier_ = options.lineHeightMultiplier;
  wordSpacing_ = options.wordSpacing;
  alignment_ = options.alignment;
  projection_ = InlineProjection(inlines, std::move(sourceText), options.projectionState, options.sourceBase, baseFont.pointSizeF(),
                                 options.pendingPrefixLength, options.smartPunct, options.breakOnSingleNewline);
  buildOffsetMapFromProjection();
  buildMathAtoms(inlines, theme, width);
  buildImageAtoms(inlines, theme, width);
  // Phase 3c: reserve inline flow for `a::before` icons (must run before the
  // HTML-span / text-layout passes, which consume the shifted offset maps).
  {
    const QFontMetricsF metrics(baseFont);
    const qreal em = qMax<qreal>(1.0, metrics.height());
    linkBeforeIconHeight_ = linkBeforeIconSize_.isValid() && linkBeforeIconSize_.height() > 0.0 ? linkBeforeIconSize_.height() : em;
    const qreal iconW = linkBeforeIconSize_.isValid() && linkBeforeIconSize_.width() > 0.0 ? linkBeforeIconSize_.width() : em;
    linkBeforeIconAdvance_ = !linkBeforeIcon_.isEmpty() ? (iconW + linkBeforeIconMarginRight_) : 0.0;
  }
  buildLinkBeforeAtoms();
  buildHtmlFormatSpans();
  buildTextLayout(theme, width, baseFont);
  // Genuinely empty: no text glyphs and no rendered image. Checking the image
  // atoms (not just plainText) matters because an image with blank alt text
  // flattens to empty text but is still visible content.
  isEmpty_ = plainText_.isEmpty() && imageAtoms_.isEmpty();
}

QSizeF InlineLayout::size() const {
  return size_;
}

qreal InlineLayout::height() const {
  return size_.height();
}

QRectF InlineLayout::visualTextBounds() const {
  if (!textLayout_ || textLayout_->lineCount() == 0) {
    return QRectF();
  }
  QRectF bounds;
  bool have = false;
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const QTextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) { continue; }
    const int start = line.textStart();
    const int end = start + line.textLength();
    const qreal x1 = line.cursorToX(start);
    const qreal x2 = line.cursorToX(end);
    const QRectF lineRect(qMin(x1, x2), line.y(), qAbs(x2 - x1), line.height());
    bounds = have ? bounds.united(lineRect) : lineRect;
    have = true;
  }
  return bounds;
}

qreal InlineLayout::firstLineBaselineY() const {
  if (!textLayout_ || textLayout_->lineCount() == 0) {
    return 0.0;
  }
  const QTextLine line = textLayout_->lineAt(0);
  // line.y() is the centering offset applied for line-height (see buildTextLayout);
  // line.ascent() is the font ascent. Their sum is where the first line's text
  // baseline sits relative to the layout origin.
  return line.isValid() ? (line.y() + line.ascent()) : 0.0;
}

void InlineLayout::paint(QPainter& painter, QPointF origin, qreal hoverPhase) const {
  if (!textLayout_) {
    return;
  }

  painter.save();
  paintTextLayoutHtmlBackgrounds(painter, origin);
  paintTextLayoutCodeSpans(painter, origin);
  paintTextLayoutInlineDecorations(painter, origin);
  paintTextLayoutHtmlKeyboardSpans(painter, origin);
  // CSS `:hover { color }` recolours only the runs that inherit the element colour
  // (pre-computed in hoverRecolourRanges_), animated by the HoverAnimator phase. It
  // is applied as foreground-only `selection`s passed to draw() — a draw-time
  // override of the foreground that needs NO setFormats() (broken after endLayout
  // in this Qt build) and a NO second render, so glyph edges stay crisp and styled
  // spans (links/code/del/kbd) keep their own colours. The selection replaces (not
  // blends) the per-run colour, so the target is the lerped base→hover colour.
  if (hoverTextColor_.isValid() && hoverPhase > 0.0 && !hoverRecolourRanges_.isEmpty()) {
    const QColor target = lerpColor(baseRunColor_, hoverTextColor_, qBound(0.0, hoverPhase, 1.0));
    QVector<QTextLayout::FormatRange> selections;
    selections.reserve(hoverRecolourRanges_.size());
    for (const QPair<int, int>& range : hoverRecolourRanges_) {
      QTextCharFormat format;
      format.setForeground(target);
      QTextLayout::FormatRange selection;
      selection.start = range.first;
      selection.length = range.second - range.first;
      selection.format = format;
      selections.append(selection);
    }
    textLayout_->draw(&painter, origin, selections);
  } else {
    textLayout_->draw(&painter, origin);
  }
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
  // Declared-only border: a width of 0 means the theme's CSS set no `border` on
  // `code` — paint no edge. QPen(color, 0) is a cosmetic 1px line in Qt, so an
  // explicit NoPen is required to actually suppress it. The background fill
  // (brush) is independent and always applies.
  if (codeBoxBorderWidth_ > 0.0) {
    painter.setPen(QPen(textLayoutCodeBorderColor_, codeBoxBorderWidth_));
  } else {
    painter.setPen(Qt::NoPen);
  }
  painter.setBrush(textLayoutCodeBackgroundColor_);
  // Phase 3b: chip geometry from CSS (defaults reproduce the legacy -3/+6 / r=3
  // chip). Advance stays = text advance (paint-only), so editing/cursor/hit-test
  // are unaffected; only the painted box grows with the theme's padding/radius.
  const qreal padH = codeBoxPaddingH_;
  const qreal padV = codeBoxPaddingV_;
  const qreal radius = codeBoxRadius_;
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
      // The chip wraps the code glyphs + CSS padding (paint-only, line-bounded).
      // Vertical padding GROWS the box around the glyph height (naturalTextRect),
      // mirroring the horizontal growth — sizing against the full line height and
      // then subtracting padding collapses the chip to nothing when padV is large
      // relative to the line (e.g. `code { padding:10px 14px }`).
      const QRectF glyphs = line.naturalTextRect();
      const QRectF rect(
          origin.x() + qMin(x1, x2) - padH,
          origin.y() + glyphs.top() - padV,
          qAbs(x2 - x1) + padH * 2.0,
          qMax<qreal>(1.0, glyphs.height() + padV * 2.0));
      painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }
  }
  painter.restore();
}

void InlineLayout::paintTextLayoutInlineDecorations(QPainter& painter, QPointF origin) const {
  if (!textLayout_) { return; }
  const bool hasMark = markGradient_.kind != GradientSpec::Kind::None;
  const bool hasLinkIcon = !linkBeforeIcon_.isEmpty();
  if (!hasMark && !hasLinkIcon) { return; }

  // mark background-image gradient, per occupied line (mirrors code-span rects).
  if (hasMark) {
    painter.save();
    for (const InlineProjectionSpan& span : projection_.spans()) {
      if (!span.highlight || span.kind != InlineSpanKind::Text || span.displayEnd <= span.displayStart) { continue; }
      for (int i = 0; i < textLayout_->lineCount(); ++i) {
        const QTextLine line = textLayout_->lineAt(i);
        if (!line.isValid()) { continue; }
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        const DisplayOffsetRange lr = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
        if (!lr.valid) { continue; }
        const int rs = qMax(lineStart, static_cast<int>(lr.start));
        const int re = qMin(lineEnd, static_cast<int>(lr.end));
        if (rs >= re) { continue; }
        const qreal x1 = line.cursorToX(rs);
        const qreal x2 = line.cursorToX(re);
        const QRectF rect(origin.x() + qMin(x1, x2), origin.y() + line.y(), qAbs(x2 - x1), line.height());
        painter.fillRect(rect, GradientPainter::makeBrush(markGradient_, rect));
      }
    }
    painter.restore();
  }

  // link ::before icon, painted over its flow-reserved placeholder (Phase 3c).
  // The placeholder already participates in QTextLayout advance/wrap, so the icon
  // sits at a real inline position and the link text follows after the gap — no
  // more hanging off the left edge of a line-start link.
  if (hasLinkIcon) {
    const qreal iconH = linkBeforeIconHeight_;
    const qreal iconW = linkBeforeIconSize_.isValid() && linkBeforeIconSize_.width() > 0.0
                            ? linkBeforeIconSize_.width() : iconH;
    for (const LinkBeforeAtom& atom : linkBeforeAtoms_) {
      if (atom.displayEnd <= atom.displayStart) { continue; }
      for (int i = 0; i < textLayout_->lineCount(); ++i) {
        const QTextLine line = textLayout_->lineAt(i);
        if (!line.isValid()) { continue; }
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        if (atom.displayStart < lineStart || atom.displayStart >= lineEnd) { continue; }
        const qreal x1 = line.cursorToX(static_cast<int>(atom.displayStart));
        const qreal x2 = line.cursorToX(static_cast<int>(atom.displayEnd));
        const QRectF target(origin.x() + qMin(x1, x2),
                            origin.y() + line.y() + (line.height() - iconH) / 2.0, iconW, iconH);
        DecorationPainter::paintIcon(painter, linkBeforeIcon_, target, linkBeforeIconTint_, linkBeforeIconFromMask_);
        break;  // one icon per link run, on its first line
      }
    }
  }
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

  // Phase 3c: prefer the theme's `kbd` CSS; fall back to the legacy light/dark
  // keycap heuristic when the theme declares no `kbd` rule (built-ins unchanged).
  const bool themed = kbdFill_.isValid();
  const QColor fill = themed ? kbdFill_ : (darkTheme_ ? QColor(QStringLiteral("#333333")) : QColor(250, 251, 252));
  const QColor border = kbdBorder_.isValid() ? kbdBorder_
      : (darkTheme_ ? QColor(QStringLiteral("#444444")) : QColor(196, 201, 209));
  const QColor bottom = kbdShadow_.isValid() ? kbdShadow_
      : (darkTheme_ ? QColor(QStringLiteral("#222222")) : QColor(181, 186, 194));
  const qreal padH = kbdPadH_ > 0.0 ? kbdPadH_ : (themed ? 4.0 : (darkTheme_ ? 8.0 : 4.0));
  const qreal radius = kbdRadius_ > 0.0 ? kbdRadius_ : (themed ? 4.0 : (darkTheme_ ? 6.0 : 2.0));
  const qreal borderWidth = kbdBorderWidth_ > 0.0 ? kbdBorderWidth_ : 1.0;

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
          origin.x() + qMin(x1, x2) - padH,
          origin.y() + line.y() + 1.0,
          qAbs(x2 - x1) + padH * 2.0,
          qMax<qreal>(1.0, line.height() - 3.0));

      const QRectF box = rect.adjusted(0.5, 0.5, -0.5, -0.5);
      // 3D depth: phycat gives kbd a hard `box-shadow: 0 2px 0 <tint>` — a flush
      // strip below the key in the shadow colour makes it look raised. Drawn first
      // so the keycap body sits on top. (CSS shadow offset isn't captured; 2px
      // matches phycat.) Only when the theme declares a kbd shadow.
      if (themed && bottom.isValid()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(bottom);
        painter.drawRect(QRectF(box.left() + radius, box.bottom(), box.width() - radius * 2.0, 2.0));
      }
      // Keycap body: fill + uniform border.
      painter.setPen(QPen(border, borderWidth));
      painter.setBrush(fill);
      painter.drawRoundedRect(box, radius, radius);
      // Bottom edge: phycat declares `border-bottom-width: 3px` for a chunky bottom.
      if (themed && kbdBorderBottomWidth_ > 0.0) {
        const QColor bc = kbdBorderBottomColor_.isValid() ? kbdBorderBottomColor_ : border;
        painter.setPen(QPen(bc, kbdBorderBottomWidth_));
        painter.drawLine(box.bottomLeft() + QPointF(radius, 0.0), box.bottomRight() + QPointF(-radius, 0.0));
      } else if (!themed) {
        // Legacy heuristic (built-ins): thin shadow-coloured bottom emphasis.
        painter.setPen(QPen(bottom, darkTheme_ ? 2.0 : 1.0));
        painter.drawLine(rect.bottomLeft() + QPointF(2.0, -0.5), rect.bottomRight() + QPointF(-2.0, -0.5));
      }
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

void InlineLayout::buildLinkBeforeAtoms() {
  linkBeforeAtoms_.clear();
  if (linkBeforeIcon_.isEmpty() || linkBeforeIconAdvance_ <= 0.0) { return; }
  if (displayText_.isEmpty() || offsetMap_.isEmpty()) { return; }

  // Collect link-run starts in PROJECTION display space: the first content span
  // of each contiguous link run (markers/hidden/atoms are skipped so the icon
  // leads the link's first visible character).
  QVector<qsizetype> projRunStarts;
  {
    qsizetype lastLinkEnd = -1;
    for (const InlineProjectionSpan& span : projection_.spans()) {
      if (!span.link || span.displayEnd <= span.displayStart) { continue; }
      if (span.kind == InlineSpanKind::OpenMarker || span.kind == InlineSpanKind::CloseMarker ||
          span.kind == InlineSpanKind::HiddenSyntax || span.kind == InlineSpanKind::EmptyContentSlot ||
          span.kind == InlineSpanKind::Atom) {
        continue;
      }
      if (span.displayStart != lastLinkEnd) { projRunStarts.push_back(span.displayStart); }
      lastLinkEnd = span.displayEnd;
    }
  }
  if (projRunStarts.isEmpty()) { return; }

  // Map each projection run-start to a layout offset in the current (post-atom)
  // display text. Placeholders insert at span boundaries, which align with
  // offsetMap_ entry boundaries, so no entry is ever split.
  QVector<qsizetype> insertAt;
  insertAt.reserve(projRunStarts.size());
  for (qsizetype p : projRunStarts) {
    insertAt.push_back(layoutDisplayOffsetForProjectionOffset(p, InlineProjectionBias::Backward));
  }
  std::sort(insertAt.begin(), insertAt.end());

  // Insert the placeholder char (descending so earlier offsets stay valid).
  QString newText = displayText_;
  for (auto it = insertAt.rbegin(); it != insertAt.rend(); ++it) {
    newText.insert(qBound<qsizetype>(0, *it, newText.size()), kLinkBeforePlaceholder);
  }

  // Rebuild both offset maps in lockstep: emit placeholders (zero-width on the
  // source/visible side, mapping to the following link entry's start) before the
  // entry whose displayStart they precede, and shift every subsequent entry.
  QVector<OffsetMapEntry> newOffset;
  newOffset.reserve(offsetMap_.size() + static_cast<int>(insertAt.size()));
  qsizetype shift = 0;
  size_t nextInsert = 0;
  for (const OffsetMapEntry& e : offsetMap_) {
    while (nextInsert < insertAt.size() && insertAt[nextInsert] == e.displayStart) {
      const qsizetype pos = e.displayStart + shift;
      newOffset.push_back(OffsetMapEntry{pos, pos + 1, e.visibleStart, e.visibleStart});
      // The placeholder's caret target is the link run's first visible offset,
      // which is exactly this following entry's visibleStart.
      linkBeforeAtoms_.push_back(LinkBeforeAtom{pos, pos + 1, e.visibleStart});
      shift += 1;
      ++nextInsert;
    }
    newOffset.push_back(OffsetMapEntry{e.displayStart + shift, e.displayEnd + shift, e.visibleStart, e.visibleEnd});
  }
  // Rebuild displayOffsetMap_ with the same shift, matching placeholders against
  // entry layout boundaries (projection↔layout is non-1:1 for atom spans, so it
  // must be walked on its own layoutStart field, identical to offsetMap_ order).
  QVector<DisplayOffsetMapEntry> newDisplay;
  newDisplay.reserve(displayOffsetMap_.size() + static_cast<int>(insertAt.size()));
  {
    qsizetype dshift = 0;
    size_t dnext = 0;
    for (const DisplayOffsetMapEntry& e : displayOffsetMap_) {
      while (dnext < insertAt.size() && insertAt[dnext] == e.layoutStart) {
        const qsizetype pos = e.layoutStart + dshift;
        newDisplay.push_back(DisplayOffsetMapEntry{e.projectionStart, e.projectionStart, pos, pos + 1});
        dshift += 1;
        ++dnext;
      }
      newDisplay.push_back(DisplayOffsetMapEntry{e.projectionStart, e.projectionEnd, e.layoutStart + dshift, e.layoutEnd + dshift});
    }
  }

  displayText_ = std::move(newText);
  offsetMap_ = std::move(newOffset);
  displayOffsetMap_ = std::move(newDisplay);

  // The inserted `a::before` placeholders now live in displayText_, so the
  // math/image atoms — built earlier with display offsets into the
  // pre-insertion text — must be shifted by the same accumulated amount the
  // maps received above. Without this, each preceding link's icon leaves an
  // atom's displayStart one icon-width too far left, so cursorToX() (paint)
  // and the force-width format (textLayoutFormats) both resolve at a stale
  // index: the real placeholder is never widened and the atom paints over the
  // preceding text (e.g. inline math covering the 'h' of an adjacent word).
  // Mirrors the offset-map entry shift exactly: an entry at displayStart v is
  // shifted by the count of insertions at positions <= v.
  const auto atomShift = [&insertAt](qsizetype start) {
    return static_cast<qsizetype>(
        std::count_if(insertAt.begin(), insertAt.end(), [start](qsizetype p) { return p <= start; }));
  };
  for (MathAtom& atom : mathAtoms_) {
    const qsizetype s = atomShift(atom.displayStart);
    atom.displayStart += s;
    atom.displayEnd += s;
  }
  for (ImageAtom& atom : imageAtoms_) {
    const qsizetype s = atomShift(atom.displayStart);
    atom.displayStart += s;
    atom.displayEnd += s;
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

    const QString tex = texForInlineMathSpan(inlines, span);
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

QString InlineLayout::texForInlineMathSpan(const QVector<InlineNode>& inlines, const InlineProjectionSpan& span) const {
  const QString source = projection_.sourceText();
  if (span.contentSourceStart >= 0 && span.contentSourceEnd >= span.contentSourceStart && span.contentSourceEnd <= source.size()) {
    const QString tex = source.mid(span.contentSourceStart, span.contentSourceEnd - span.contentSourceStart);
    if (!tex.isEmpty()) {
      return tex;
    }
  }

  const QString expected = projection_.visibleText().mid(span.visibleStart, span.visibleEnd - span.visibleStart);
  const auto visit = [&](const auto& self, const QVector<InlineNode>& nodes) -> QString {
    for (const InlineNode& node : nodes) {
      if (node.type() == InlineType::InlineMath && (expected.isEmpty() || node.text() == expected)) {
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
    const bool isDataUri = srcUrl.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive);
    if (isRemote) {
      image = ImageLoader::instance().cached(srcUrl);
      if (image.isNull()) {
        ImageLoader::instance().request(srcUrl);
      }
    } else if (isDataUri) {
      // Inline data: URI (RFC 2397, base64 or percent-encoded) — decode synchronously.
      image = image_decoder::decodeDataUri(srcUrl);
    } else {
      // Prefer our bundled decoders (png/jpeg/webp/avif/svg) so local image display
      // never depends on Qt's imageformat plugins (qjpeg is absent from this Qt build).
      image = image_decoder::decodeFileFallback(srcUrl);
      if (image.isNull()) {
        image.load(srcUrl);  // last resort for formats we don't ship (tiff/bmp/gif/ico)
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

    // Apply the image's own zoom (style="zoom:N%") — read straight from the source
    // snippet the span covers. Markdown images have no zoom (factor 1.0).
    const QString imgSource = projection_.sourceText().mid(span.sourceStart, span.sourceEnd - span.sourceStart);
    const qreal zoom = image_syntax::zoomFactor(imgSource);

    if (collapsed) {
      // Inactive: replace alt text with placeholder, render image inline.
      const QSizeF displaySize = scaledImageDisplaySize(image, zoom);

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
      preview.displaySize = scaledImageDisplaySize(image, zoom);
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
  if (alignment_ != Qt::Alignment()) {
    option.setAlignment(alignment_);
  }
  textLayout_->setTextOption(option);
  const QVector<QTextLayout::FormatRange> formats = textLayoutFormats(theme, baseFont);
  textLayout_->setFormats(formats);
  // Pre-compute the runs a `:hover { color }` should recolour (only when the
  // element actually declares a hover colour). Derived from the same `formats`
  // the layout draws, so it stays in lock-step with span colouring.
  if (hoverTextColor_.isValid()) {
    computeHoverRecolourRanges(formats, theme);
  } else {
    hoverRecolourRanges_.clear();
  }

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
    // Ensure the line is tall enough for any image / math atoms on this line.
    // Without this, a tall inline atom (image, or math with a superscript /
    // fraction) paints baseline-aligned but overflows the un-grown line box,
    // intruding into the neighbour line (e.g. an inline `$E=mc^2$` covering the
    // line above after a wrap).
    qreal minLineHeight = line.height();
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    for (const ImageAtom& atom : imageAtoms_) {
      if (atom.loaded && atom.displayStart >= lineStart && atom.displayStart <= lineEnd) {
        minLineHeight = qMax(minLineHeight, atom.displaySize.height());
      }
    }
    for (const MathAtom& atom : mathAtoms_) {
      if (atom.layout && atom.layout->valid() && atom.displayStart >= lineStart && atom.displayStart <= lineEnd) {
        minLineHeight = qMax(minLineHeight, atom.layout->size.height());
      }
    }
    qreal lineHeight = std::ceil(minLineHeight * 1.16);
    if (lineHeightMultiplier_ > 0.0) {
      // CSS `line-height: N` is N * font-size, not N * the platform font
      // metrics line box. Multiplying QTextLine::height() made CSS themes too
      // tall compared with other renderers, especially for serif fallback fonts.
      const qreal pointSize = baseFont.pointSizeF() > 0.0 ? baseFont.pointSizeF() : 12.0;
      const qreal cssFontPx = pointSize * (96.0 / 72.0);
      lineHeight = std::ceil(qMax(minLineHeight, cssFontPx * lineHeightMultiplier_));
    }
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

void InlineLayout::computeHoverRecolourRanges(const QVector<QTextLayout::FormatRange>& formats, const RenderTheme& theme) {
  hoverRecolourRanges_.clear();
  const int n = displayText_.size();
  if (n <= 0) { return; }
  baseRunColor_ = baseTextColorOverride_.isValid() ? baseTextColorOverride_ : theme.textColor();
  // Effective foreground per character, derived from the SAME format ranges the
  // layout draws: every span that sets its own foreground (link / code / del /
  // kbd / explicit HTML colour) overrides the base, and atom placeholders render
  // transparent. A run therefore "inherits the element colour" exactly when its
  // effective colour equals the base — those are the runs `:hover { color }`
  // should recolour. (SpellCheck ranges set only an underline, no foreground, so
  // misspelled prose still inherits and recolours — correct.)
  QVector<QColor> effective(n, baseRunColor_);
  for (const QTextLayout::FormatRange& fr : formats) {
    const QColor fg = fr.format.foreground().color();
    if (!fg.isValid()) { continue; }
    const int start = qBound(0, fr.start, n);
    const int end = qBound(0, fr.start + fr.length, n);
    for (int i = start; i < end; ++i) { effective[i] = fg; }
  }
  int i = 0;
  while (i < n) {
    if (effective[i] != baseRunColor_) { ++i; continue; }
    const int start = i;
    while (i < n && effective[i] == baseRunColor_) { ++i; }
    hoverRecolourRanges_.append({start, i});
  }
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
      // Vertically place the image exactly where buildTextLayout allocated the line.
      // line.y() is the centred top of the line's minLineHeight content; the image grew
      // minLineHeight to its own height, so it starts at line.y() when it is the tallest
      // item on the line and is centred only when shorter than the text line box.
      // Recomputing minLineHeight = max(line.height(), image height) — the same expression
      // buildTextLayout uses — keeps paint in lock-step with layout. The previous
      // `ceil(line.height()*1.16)` estimate was only valid for a plain text line and drifted
      // negative under CSS `line-height`, painting the image above its own block rect.
      const qreal minLineHeight = qMax(line.height(), atom.displaySize.height());
      const qreal y = origin.y() + line.y() + (minLineHeight - atom.displaySize.height()) * 0.5;
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

  // QTextCharFormat::setFont does not carry QFont::letterSpacing through to the
  // shaping engine (the per-range font would otherwise override the layout's
  // base font and silently drop CSS letter-spacing). Re-apply it on the format
  // so the theme's letter-spacing survives (Phase 3). No-op at 0 (built-ins).
  const auto applyLetterSpacing = [](QTextCharFormat& fmt, const QFont& src) {
    if (src.letterSpacing() != 0.0) {
      fmt.setFontLetterSpacingType(src.letterSpacingType());
      fmt.setFontLetterSpacing(src.letterSpacing());
    }
  };

  QTextCharFormat baseFormat;
  baseFormat.setFont(baseFont);
  applyLetterSpacing(baseFormat, baseFont);
  if (wordSpacing_ != 0.0) { baseFormat.setFontWordSpacing(wordSpacing_); }
  baseFormat.setForeground(baseTextColorOverride_.isValid() ? baseTextColorOverride_ : theme.textColor());
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
      // Phase 5: CSS `del { color }` mutes deleted text (phycat → #999). The strike
      // line itself can't be recoloured (Qt strikeOut uses the text colour), but the
      // text colour is honoured when the theme declares one.
      if (theme.delColor().isValid()) {
        format.setForeground(theme.delColor());
      }
    }
    if (span.underline) {
      format.setFontUnderline(true);
    }
    if (span.highlight) {
      format.setBackground(theme.highlightBackgroundColor());
    }
    if (span.subscript) {
      format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    } else if (span.superscript) {
      format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    }
    switch (span.type) {
      case InlineType::Code:
        format.setFont(theme.codeFont());
        applyLetterSpacing(format, theme.codeFont());
        if (theme.inlineCodeTextColor().isValid()) {
          format.setForeground(theme.inlineCodeTextColor());
        }
        if (span.kind == InlineSpanKind::Text) {
          format.setBackground(theme.codeBackgroundColor());
        }
        break;
      case InlineType::InlineMath:
        format.setFont(theme.mathFont());
        break;
      default:
        break;
    }
    // Link formatting is an orthogonal wrapping attribute (span.link), decoupled from span.type, so a
    // link composes with any inner node: an image-link renders as a clickable image, `[`code`](url)`
    // keeps its code background, etc. Atom is excluded so an image-link's placeholder isn't underlined.
    if (span.link && span.kind != InlineSpanKind::OpenMarker && span.kind != InlineSpanKind::CloseMarker &&
        span.kind != InlineSpanKind::HiddenSyntax && span.kind != InlineSpanKind::EmptyContentSlot &&
        span.kind != InlineSpanKind::Atom) {
      format.setForeground(theme.linkColor());
      // Phase 3: link underline follows CSS `text-decoration` (phycat sets none).
      format.setFontUnderline(theme.linkUnderlined());
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

  // Phase 3c: `a::before` flow-reserved placeholders. Transparent (the icon is
  // painted over them) and widened to the icon advance via letter spacing, so
  // QTextLayout reserves real horizontal flow and wraps accordingly.
  for (const LinkBeforeAtom& atom : linkBeforeAtoms_) {
    if (atom.displayEnd <= atom.displayStart) { continue; }
    QFont placeholderFont = baseFont;
    const QFontMetricsF placeholderMetrics(placeholderFont);
    const qreal placeholderAdvance = qMax<qreal>(1.0, placeholderMetrics.horizontalAdvance(kLinkBeforePlaceholder));
    placeholderFont.setLetterSpacing(QFont::AbsoluteSpacing, linkBeforeIconAdvance_ - placeholderAdvance);
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
      // Phase 3c: prefer the theme's `kbd { font-family }`; keep Courier New as
      // the legacy default (built-ins + the geometry test's monospace assertion).
      format.setFontFamily(kbdFont_.isEmpty() ? QStringLiteral("Courier New") : kbdFont_);
      format.setForeground(kbdText_.isValid() ? kbdText_
          : (textLayoutCodeTextColor_.isValid() ? textLayoutCodeTextColor_ : theme.textColor()));
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
      // Phase 3c: HTML <a> follows the same text-decoration rule as Markdown
      // links (phycat sets `a { text-decoration: none }`). Previously this was
      // forced on, so HTML links stayed underlined even when the theme opted out.
      format.setFontUnderline(theme.linkUnderlined());
    }
    QTextLayout::FormatRange range;
    range.start = hs.layoutStart;
    range.length = hs.layoutEnd - hs.layoutStart;
    range.format = format;
    formats.push_back(range);
  }

  // Spell-check overlay (rendered mode): append SpellCheckUnderline ranges for misspelled
  // prose words. Appended last so the underline paints over any bold/italic/link styling
  // on the same glyphs (Qt composes FormatRanges per-property, later wins). Only Text spans
  // are scanned — code spans, inline math and atoms are skipped. The misspelled predicate
  // is supplied by the builder; when it is unset, spell checking is off and this is skipped.
  if (isMisspelled_) {
    static const QRegularExpression wordRe(QStringLiteral("[\\p{L}][\\p{L}'\\x{2019}]*"),
                                           QRegularExpression::UseUnicodePropertiesOption);
    const QColor errorColor = theme.spellCheckColor();
    for (const InlineProjectionSpan& span : projection_.spans()) {
      if (span.kind != InlineSpanKind::Text || span.type == InlineType::Code || span.type == InlineType::InlineMath) {
        continue;
      }
      const DisplayOffsetRange spanRange = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
      if (!spanRange.valid || spanRange.end > displayText_.size()) {
        continue;
      }
      const QString spanText = displayText_.mid(spanRange.start, spanRange.end - spanRange.start);
      QRegularExpressionMatchIterator it = wordRe.globalMatch(spanText);
      while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int localStart = static_cast<int>(m.capturedStart());
        const int localLen = static_cast<int>(m.capturedLength());
        if (localLen <= 1) {
          continue;
        }
        const QStringView word(spanText.constData() + localStart, localLen);
        if (!isMisspelled_(word)) {
          continue;
        }
        QTextCharFormat format;
        format.setUnderlineColor(errorColor);
        format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
        QTextLayout::FormatRange spellRange;
        spellRange.start = static_cast<int>(spanRange.start) + localStart;
        spellRange.length = localLen;
        spellRange.format = format;
        formats.push_back(spellRange);
      }
    }
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
  // Phase 3c: a click inside a flow-reserved link-icon placeholder lands the
  // caret at the link run's first visible offset.
  for (const LinkBeforeAtom& atom : linkBeforeAtoms_) {
    if (displayOffset >= atom.displayStart && displayOffset <= atom.displayEnd) {
      return atom.visibleStart;
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
  // Phase 3c: link-icon placeholders have no visible extent (they map to the
  // link run's start), so an interior visible offset has no placeholder display
  // range to return — fall through to the proportional map below.
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
