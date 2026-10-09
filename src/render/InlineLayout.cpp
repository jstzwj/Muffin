#include "render/InlineLayout.h"
#include "render/CssFormattingContext.h"
#include "render/CssSizing.h"
#include "render/RenderMetrics.h"
#include "render/InlineFormatting.h"

#include "document/ImageSyntaxOps.h"
#include "editor/ResourceUrl.h"
#include "render/Blur.h"
#include "render/DecorationPainter.h"
#include "render/GradientPainter.h"
#include "render/ImageLoader.h"
#include "render/ImagePlaceholder.h"
#include "html/HtmlRenderer.h"
#include <QFileInfo>
#include <QTextDocumentFragment>

#include <QPainter>
#include <QPen>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTextLine>
#include <QTextOption>

#include <cmath>

namespace muffin {
QPair<qreal, qreal> InlineLayout::intrinsicWidths() const {
  if (!textLayout_) return {};
  const auto intrinsic = intrinsicTextWidths(*textLayout_, wrapMode_ == QTextOption::NoWrap, anywhereMinimum_);
  return {intrinsic.minContent, intrinsic.maxContent};
}
namespace {

constexpr QChar kInlineMathPlaceholder(0x00a0);
constexpr QChar kImagePlaceholder(0x2009);  // thin space, distinct from math placeholder
constexpr QChar kLinkBeforePlaceholder(0xe000);  // PUA: flow-reserved slot for a::before icons
constexpr QChar kTabIndentSourceChar(0x200b);
constexpr QChar kTabIndentLayoutChar(0x00a0);

QRectF mathUsedBounds(const math::MathLayoutResult& layout) {
  if (!layout.root) return {};
  auto bounds = layout.root->boundsAt({});
  // KaTeX's outer inline CSS box has a fixed strut. Its actual child formatting
  // box can extend above/below that strut (fractions, matrices, rules).
  for (const auto& child : layout.root->children) {
    bounds = bounds.united(child->boundsAt(QPointF(child->xOffset, child->yOffset + child->shift)));
  }
  return bounds;
}

QString flattenPlainText(const QVector<InlineNode>& inlines, bool breakOnSingleNewline, bool& hasHtml) {
  QString text;
  for (const InlineNode& node : inlines) {
    switch (node.type()) {
      case InlineType::Text:
      case InlineType::Code:
      case InlineType::InlineMath:
        text += node.text();
        break;
      case InlineType::HtmlInline:
        hasHtml = true;
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
        text += flattenPlainText(node.children(), breakOnSingleNewline, hasHtml);
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
  hasHtmlContent_ = false;
  plainText_ = flattenPlainText(inlines, options.breakOnSingleNewline, hasHtmlContent_);
  offsetMap_.clear();
  mathAtoms_.clear();
  imageAtoms_.clear();
  htmlAtoms_.clear();
  previewAtoms_.clear();
  previewHeight_ = 0.0;
  htmlFormatSpans_.clear();
  displayOffsetMap_.clear();
  displayText_.clear();
  layoutText_.clear();
  lineBoxes_.clear();
  isMisspelled_ = options.isMisspelled;
  const auto sharedSnapshot = [&](const ThemeElementStyle& style, const QFont& font) {
    const auto used = theme.usedBoxForStyle(style, width);
    return options.styleCache ? options.styleCache->snapshot(style, used, font, width)
                              : std::make_shared<const LayoutBox>(LayoutBox::place(style.key, style, used, {}, font));
  };
  const auto snapshot = [&](const QString& key) {
    const auto* style = options.styleNode ? theme.elementStyleForNode(*options.styleNode, key) : theme.elementStyle(key);
    return sharedSnapshot(style ? *style : ThemeElementStyle{}, theme.textFontForElement(key, options.styleNode));
  };
  codeStyle_ = snapshot(QStringLiteral("code"));
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
  focusTextColor_ = options.focusTextColor;
  preeditText_ = options.preeditText;
  preeditFormats_ = options.preeditFormats;
  preeditCursor_ = options.preeditCursor;
  preeditInsertAtSourceOffset_ = options.preeditInsertAtSourceOffset;
  lineHeightMultiplier_ = options.lineHeightMultiplier;
  zoomScale_ = theme.zoomPercent() / 100.0;
  wordSpacing_ = options.wordSpacing;
  textShadow_ = options.textShadow;
  alignment_ = options.alignment;
  wrapMode_ = options.wrapMode;
  anywhereMinimum_ = options.anywhereMinimum;
  html::HtmlColorPalette htmlPalette = html::HtmlColorPalette::defaultLight();
  htmlPalette.documentStyleSheet = theme.documentStyleSheet();
  if (options.styleNode) {
    htmlPalette.documentParent = theme.cssElementForNode(*options.styleNode);
    htmlPalette.documentParentForOffset = [&theme, owner = options.styleNode, base = qMax<qsizetype>(0, options.sourceBase)](
                                              qsizetype offset) { return theme.cssParentForInlineHtml(*owner, offset + base); };
  }
  htmlPalette.fontAliases = theme.fontAliases();
  htmlPalette.cssEnvironment = theme.documentCssEnvironment();
  htmlPalette.cssZoom = theme.zoomPercent() / 100.0;
  htmlPalette.parentFontPx = baseFont.pointSizeF() * 96.0 / 72.0 / htmlPalette.cssZoom / htmlPalette.cssEnvironment.textScale;
  htmlPalette.text = options.baseTextColor.isValid() ? options.baseTextColor : theme.textColor();
  projection_ = InlineProjection(inlines, std::move(sourceText), options.projectionState, options.sourceBase, baseFont.pointSizeF(),
                                 options.pendingPrefixLength, options.smartPunct, options.breakOnSingleNewline, options.textTransform,
                                 options.renderEmoji, std::move(htmlPalette));
  spanStyles_.clear();
  styleSourceBase_ = qMax<qsizetype>(0, options.sourceBase);
  if (options.styleNode) {
    for (const auto& span : projection_.spans()) {
      if (span.kind != InlineSpanKind::Text && span.type != InlineType::Image) continue;
      const auto style = theme.inlineStyleForNode(*options.styleNode, span.contentSourceStart + qMax<qsizetype>(0, options.sourceBase));
      const QFont fallback = span.type == InlineType::Code ? codeStyle_->font : baseFont;
      spanStyles_.insert(span.displayStart, sharedSnapshot(style, theme.fontForStyle(style, fallback)));
    }
  }
  buildOffsetMapFromProjection();
  buildMathAtoms(inlines, theme, width);
  buildImageAtoms(inlines, theme, width, options.documentPath);
  buildHtmlAtoms(width, baseFont, options.documentPath);
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
  buildHtmlFormatSpans(theme, width);
  buildInlineBoxSpacing();
  buildHtmlFormatSpans(theme, width);
  buildTextLayout(theme, width, baseFont);
  buildInlineBoxes();
  // Genuinely empty: no text glyphs and no rendered image. Checking the image
  // atoms (not just plainText) matters because an image with blank alt text
  // flattens to empty text but is still visible content.
  isEmpty_ = plainText_.isEmpty() && imageAtoms_.isEmpty() && htmlAtoms_.isEmpty();
}

QSizeF InlineLayout::size() const {
  return size_;
}

bool InlineLayout::stylesMatch(const RenderTheme& theme, const MarkdownNode& owner) const {
  // Embedded HTML has its own descendant tree. Until that tree is retained,
  // conservatively rebuild promoted HTML fragments after structural edits.
  if (!projection_.htmlFormatData().isEmpty()) return false;
  for (const auto& span : projection_.spans()) {
    const auto found = spanStyles_.constFind(span.displayStart);
    if (found != spanStyles_.cend() &&
        theme.inlineStyleForNode(owner, span.contentSourceStart + styleSourceBase_).fingerprint != found.value()->style.fingerprint)
      return false;
  }
  return true;
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
  for (const auto& box : inlineBoxes_) bounds = bounds.united(box.visualOverflow);
  for (const auto& rect : mathAtomRects({})) bounds = bounds.united(rect);
  for (const auto& atom : htmlAtoms_) bounds = bounds.united(atom.rect);
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

qreal InlineLayout::lastLineBaselineY() const {
  if (!textLayout_ || textLayout_->lineCount() == 0) return 0;
  const auto line = textLayout_->lineAt(textLayout_->lineCount() - 1);
  return line.y() + line.ascent();
}

void InlineLayout::paint(QPainter& painter, QPointF origin, qreal hoverPhase, qreal focusPhase) const {
  if (!textLayout_) {
    return;
  }

  painter.save();
  paintTextLayoutHtmlBackgrounds(painter, origin);
  paintTextLayoutCodeSpans(painter, origin);
  paintTextLayoutInlineDecorations(painter, origin);
  paintTextLayoutHtmlKeyboardSpans(painter, origin);
  // CSS `text-shadow`: render the laid-out text to an offscreen image, recolour it
  // to the shadow colour (alpha mask), blur, and composite behind the crisp text.
  if (textShadow_.present && textShadow_.color.isValid() && textLayout_) {
    paintTextShadow(painter, origin);
  }
  // CSS `:hover`/`:focus { color }` recolours only the runs that inherit the element
  // colour (pre-computed in hoverRecolourRanges_), animated by the HoverAnimator/
  // FocusAnimator phases. Applied as foreground-only `selection`s passed to draw() —
  // a draw-time override of the foreground that needs NO setFormats() (broken after
  // endLayout in this Qt build) and NO second render, so glyph edges stay crisp and
  // styled spans (links/code/del/kbd) keep their own colours. The selection replaces
  // (not blends) the per-run colour, so the target is the lerped colour: focus is
  // applied first (base→focus), then hover on top (→hover) — the two orthogonal
  // states compose, with hover visually winning when both are at full phase.
  const bool hasFocus = focusTextColor_.isValid() && focusPhase > 0.0;
  const bool hasHover = hoverTextColor_.isValid() && hoverPhase > 0.0;
  if ((hasFocus || hasHover) && !hoverRecolourRanges_.isEmpty()) {
    QColor target = baseRunColor_;
    if (hasFocus) { target = lerpColor(target, focusTextColor_, qBound(0.0, focusPhase, 1.0)); }
    if (hasHover) { target = lerpColor(target, hoverTextColor_, qBound(0.0, hoverPhase, 1.0)); }
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
  for (const auto& atom : htmlAtoms_)
    atom.layout->paintBoxFragment(painter, *atom.layout->root()->children().front(), origin + atom.rect.topLeft() - atom.crop.topLeft());
  paintImagePreview(painter, origin);
  painter.restore();
}

qsizetype InlineLayout::hitTestTextOffset(QPointF localPos) const {
  for (const auto& atom : htmlAtoms_)
    if (atom.rect.contains(localPos))
      return atom.visibleStart + atom.layout->textOffsetAtPoint(localPos - atom.rect.topLeft() + atom.crop.topLeft());
  return visibleOffsetForDisplayOffset(textLayoutDisplayOffsetForPoint(localPos));
}

qsizetype InlineLayout::hitTestSourceOffset(QPointF localPos) const {
  for (const auto& atom : htmlAtoms_)
    if (atom.rect.contains(localPos)) {
      const auto offset = atom.layout->textOffsetAtPoint(localPos - atom.rect.topLeft() + atom.crop.topLeft());
      return atom.sourceOffsets.value(offset, atom.sourceEnd);
    }
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
  for (const auto& atom : htmlAtoms_)
    if (atom.rect.contains(localPos)) {
      const auto point = localPos - atom.rect.topLeft() + atom.crop.topLeft();
      return atom.layout->cursorRectForTextOffset(atom.layout->textOffsetAtPoint(point))
          .translated(atom.rect.topLeft() - atom.crop.topLeft());
    }
  return textLayoutHitForPoint(localPos).cursorRect;
}

QString InlineLayout::linkHrefAtLocalPos(QPointF localPos) const {
  for (const auto& atom : htmlAtoms_)
    if (atom.rect.contains(localPos)) return atom.layout->hitTest(localPos - atom.rect.topLeft() + atom.crop.topLeft()).linkHref;
  if (!textLayout_) return {};
  const qsizetype layoutOffset = textLayoutDisplayOffsetForPoint(localPos);
  const qsizetype projOffset = projectionDisplayOffsetForLayoutOffset(layoutOffset, InlineProjectionBias::Backward);
  return projection_.linkHrefAtDisplayOffset(projOffset);
}

QString InlineLayout::imageSrcAtLocalPos(QPointF localPos) const {
  for (const auto& atom : htmlAtoms_)
    if (atom.rect.contains(localPos)) return atom.layout->hitTest(localPos - atom.rect.topLeft() + atom.crop.topLeft()).imageSrc;
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
  for (const auto& atom : htmlAtoms_)
    if (textOffset > atom.visibleStart && textOffset < atom.visibleEnd)
      return atom.layout->cursorRectForTextOffset(textOffset - atom.visibleStart).translated(atom.rect.topLeft() - atom.crop.topLeft());
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
  for (const auto& atom : htmlAtoms_)
    if (sourceOffset > atom.sourceStart && sourceOffset < atom.sourceEnd) {
      const auto found = std::lower_bound(atom.sourceOffsets.begin(), atom.sourceOffsets.end(), sourceOffset);
      const auto offset = int(found - atom.sourceOffsets.begin());
      const auto rect = atom.layout->cursorRectForTextOffset(offset);
      if (!rect.isNull()) return rect.translated(atom.rect.topLeft() - atom.crop.topLeft());
    }
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

int InlineLayout::visualLineCount() const {
  return textLayout_ ? textLayout_->lineCount() : 0;
}

int InlineLayout::visualLineIndexForTextOffset(qsizetype textOffset) const {
  if (!textLayout_) {
    return -1;
  }
  const qsizetype displayOffset = displayOffsetForVisibleOffset(textOffset);
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const QTextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) { continue; }
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    if (displayOffset >= lineStart && displayOffset <= lineEnd) {
      return i;
    }
  }
  return textLayout_->lineCount() > 0 ? textLayout_->lineCount() - 1 : -1;
}

int InlineLayout::visualLineIndexForSourceOffset(qsizetype sourceOffset) const {
  if (!textLayout_) {
    return -1;
  }
  qsizetype displayOffset = -1;
  if (!layoutDisplayOffsetForSourceOffset(sourceOffset, InlineProjectionBias::Forward, displayOffset)) {
    return visualLineIndexForTextOffset(sourceOffset);
  }
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const QTextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) { continue; }
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    if (displayOffset >= lineStart && displayOffset <= lineEnd) {
      return i;
    }
  }
  return textLayout_->lineCount() > 0 ? textLayout_->lineCount() - 1 : -1;
}

QRectF InlineLayout::visualLineRect(int lineIndex) const {
  if (!textLayout_ || lineIndex < 0 || lineIndex >= textLayout_->lineCount()) {
    return {};
  }
  const QTextLine line = textLayout_->lineAt(lineIndex);
  if (!line.isValid()) {
    return {};
  }
  const int lineStart = line.textStart();
  const int lineEnd = lineStart + line.textLength();
  const qreal x1 = line.cursorToX(lineStart);
  const qreal x2 = line.cursorToX(lineEnd);
  return QRectF(qMin(x1, x2), line.y(), qAbs(x2 - x1), line.height());
}

qsizetype InlineLayout::textOffsetAtVisualLineX(int lineIndex, qreal localX) const {
  if (!textLayout_ || lineIndex < 0 || lineIndex >= textLayout_->lineCount()) {
    return 0;
  }
  const QTextLine line = textLayout_->lineAt(lineIndex);
  if (!line.isValid()) {
    return 0;
  }
  return hitTestTextOffset(QPointF(localX, line.y() + line.height() * 0.5));
}

qsizetype InlineLayout::sourceOffsetAtVisualLineX(int lineIndex, qreal localX) const {
  if (!textLayout_ || lineIndex < 0 || lineIndex >= textLayout_->lineCount()) {
    return 0;
  }
  const QTextLine line = textLayout_->lineAt(lineIndex);
  if (!line.isValid()) {
    return 0;
  }
  return hitTestSourceOffset(QPointF(localX, line.y() + line.height() * 0.5));
}

int InlineLayout::toLayoutOffset(int displayOffset) const {
  // layoutText_ has the preedit spliced in at preeditSpliceInsertAt_; a displayText_-space offset
  // strictly after that point addresses preeditSpliceLength_ chars later in layoutText_. The splice
  // point itself maps to itself (it is the caret boundary). Identity when no preedit is spliced.
  if (preeditSpliceLength_ <= 0 || displayOffset <= preeditSpliceInsertAt_) {
    return displayOffset;
  }
  return displayOffset + preeditSpliceLength_;
}

QRectF InlineLayout::preeditCursorRect(QPointF origin) const {
  if (!textLayout_ || preeditSpliceLength_ <= 0 || preeditCursor_ < 0) {
    return {};
  }
  const int pos = preeditSpliceInsertAt_ + qBound(0, preeditCursor_, preeditSpliceLength_);
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const QTextLine line = textLayout_->lineAt(i);
    if (line.isValid() && pos >= line.textStart() && pos <= line.textStart() + line.textLength()) {
      return QRectF(origin.x() + line.cursorToX(pos), origin.y() + line.y(), 1.5, line.height());
    }
  }
  return {};
}

QVector<QRectF> InlineLayout::mathAtomRects(QPointF origin) const {
  QVector<QRectF> rects;
  if (!textLayout_) {
    return rects;
  }
  for (const MathAtom& atom : mathAtoms_) {
    if (!atom.layout || !atom.layout->valid()) {
      continue;
    }
    const int atomStart = toLayoutOffset(static_cast<int>(atom.displayStart));
    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const QTextLine line = textLayout_->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      if (atomStart >= lineStart && atomStart < lineEnd) {
        const qreal x = line.cursorToX(atomStart);
        const qreal baseline = origin.y() + line.y() + line.ascent();
        const auto used = mathUsedBounds(*atom.layout);
        rects.append(QRectF(origin.x() + x, baseline + used.top(), atom.layout->size.width(), used.height()));
        break;
      }
    }
  }
  return rects;
}

QVector<QRectF> InlineLayout::selectionRects(qsizetype startOffset, qsizetype endOffset) const {
  const qsizetype startDisplayOffset = displayOffsetForVisibleOffset(qMin(startOffset, endOffset));
  const qsizetype endDisplayOffset = displayOffsetForVisibleOffset(qMax(startOffset, endOffset));
  auto rects = selectionRectsForDisplayOffsets(startDisplayOffset, endDisplayOffset);
  for (const auto& atom : htmlAtoms_) {
    if (qMax(startOffset, endOffset) <= atom.visibleStart || qMin(startOffset, endOffset) >= atom.visibleEnd) continue;
    for (const auto& rect : atom.layout->selectionRects(qMax<qsizetype>(0, qMin(startOffset, endOffset) - atom.visibleStart),
                                                        qMin(atom.visibleEnd, qMax(startOffset, endOffset)) - atom.visibleStart))
      rects.push_back(rect.translated(atom.rect.topLeft() - atom.crop.topLeft()));
  }
  return rects;
}

QVector<QRectF> InlineLayout::selectionRectsForSourceOffsets(qsizetype startSourceOffset, qsizetype endSourceOffset) const {
  qsizetype startDisplayOffset = -1;
  qsizetype endDisplayOffset = -1;
  if (!layoutDisplayOffsetForSourceOffset(qMin(startSourceOffset, endSourceOffset), InlineProjectionBias::Backward, startDisplayOffset) ||
      !layoutDisplayOffsetForSourceOffset(qMax(startSourceOffset, endSourceOffset), InlineProjectionBias::Forward, endDisplayOffset)) {
    return {};
  }
  auto rects = selectionRectsForDisplayOffsets(startDisplayOffset, endDisplayOffset);
  for (const auto& atom : htmlAtoms_) {
    const auto first = qMin(startSourceOffset, endSourceOffset), last = qMax(startSourceOffset, endSourceOffset);
    if (last <= atom.sourceStart || first >= atom.sourceEnd) continue;
    if (first <= atom.sourceStart && last >= atom.sourceEnd)
      rects.push_back(atom.rect);
    else {
      const auto begin = std::lower_bound(atom.sourceOffsets.begin(), atom.sourceOffsets.end(), first) - atom.sourceOffsets.begin();
      const auto end = std::lower_bound(atom.sourceOffsets.begin(), atom.sourceOffsets.end(), last) - atom.sourceOffsets.begin();
      for (const auto& rect : atom.layout->selectionRects(begin, end))
        rects.push_back(rect.translated(atom.rect.topLeft() - atom.crop.topLeft()));
    }
  }
  return rects;
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
  for (const auto& box : inlineBoxes_)
    if (box.hostKey == QStringLiteral("code")) paintLayoutBox(painter, box, origin);
}

QString InlineLayout::plainText() const { return plainText_; }
QString InlineLayout::visibleText() const { return projection_.visibleText(); }
QString InlineLayout::displayText() const { return displayText_; }
int InlineLayout::mathAtomCount() const { return static_cast<int>(mathAtoms_.size()); }
QVector<QTextLayout::FormatRange> InlineLayout::debugTextFormats(const RenderTheme& theme, const QFont& baseFont) const {
  return textLayoutFormats(theme, baseFont);
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
        const int rs = qMax(lineStart, toLayoutOffset(static_cast<int>(lr.start)));
        const int re = qMin(lineEnd, toLayoutOffset(static_cast<int>(lr.end)));
        if (rs >= re) { continue; }
        const qreal x1 = line.cursorToX(rs);
        const qreal x2 = line.cursorToX(re);
        const QRectF rect(origin.x() + qMin(x1, x2), origin.y() + line.y(), qAbs(x2 - x1), line.height());
        painter.fillRect(rect, GradientPainter::makeBrush(markGradient_, rect, zoomScale_));
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
        const int atomStart = toLayoutOffset(static_cast<int>(atom.displayStart));
        const int atomEnd = toLayoutOffset(static_cast<int>(atom.displayEnd));
        if (atomStart < lineStart || atomStart >= lineEnd) { continue; }
        const qreal x1 = line.cursorToX(atomStart);
        const qreal x2 = line.cursorToX(atomEnd);
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
      const int rangeStart = qMax(lineStart, toLayoutOffset(hs.layoutStart));
      const int rangeEnd = qMin(lineEnd, toLayoutOffset(hs.layoutEnd));
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
  for (const auto& box : inlineBoxes_)
    if (box.hostKey == QStringLiteral("kbd")) paintLayoutBox(painter, box, origin);
}

void InlineLayout::buildInlineBoxes() {
  inlineBoxes_.clear();
  if (!textLayout_) return;
  const auto add = [&](qsizetype start, qsizetype end, const LayoutBox& style) {
    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const auto line = textLayout_->lineAt(i);
      const int first = qMax(line.textStart(), toLayoutOffset(start));
      const int last = qMin(line.textStart() + line.textLength(), toLayoutOffset(end));
      if (first >= last) continue;
      const qreal left = line.cursorToX(first), right = line.cursorToX(last);
      inlineBoxes_.push_back(LayoutBox::inlineFragment(style, left, right, line.y() + line.ascent(),
                                                       first == toLayoutOffset(start), last == toLayoutOffset(end)));
    }
  };
  for (const auto& span : projection_.spans()) {
    if (span.type != InlineType::Code || span.kind != InlineSpanKind::Text) continue;
    const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
    if (range.valid) add(range.start, range.end, *spanStyles_.value(span.displayStart, codeStyle_));
  }
  for (const auto& run : htmlInlineBoxRuns_) add(run.start, run.end, run.box);
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
  for (auto& atom : htmlAtoms_) {
    const auto s = atomShift(atom.displayStart);
    atom.displayStart += s;
    atom.displayEnd += s;
  }
}

void InlineLayout::buildInlineBoxSpacing() {
  inlineSpacers_.clear();
  struct Edge {
    qsizetype position;
    qreal width;
  };
  QVector<Edge> edges;
  const auto addBox = [&](qsizetype start, qsizetype end, const LayoutBox& box) {
    if (end <= start) return;
    const auto inset = LayoutBox::insets(box.usedBox);
    if (inset.left() > 0) edges.push_back({start, inset.left()});
    if (inset.right() > 0) edges.push_back({end, inset.right()});
  };
  for (const auto& span : projection_.spans()) {
    if (span.type != InlineType::Code || span.kind != InlineSpanKind::Text) continue;
    const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
    if (range.valid) addBox(range.start, range.end, *spanStyles_.value(span.displayStart, codeStyle_));
  }
  for (const auto& run : htmlInlineBoxRuns_) addBox(run.start, run.end, run.box);
  if (edges.isEmpty()) return;
  std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
  QVector<Edge> unique;
  for (const auto& edge : edges) {
    if (!unique.isEmpty() && unique.back().position == edge.position)
      unique.back().width += edge.width;
    else
      unique.push_back(edge);
  }
  edges = std::move(unique);
  const auto before = [&](qsizetype p) {
    return p + std::count_if(edges.begin(), edges.end(), [&](const auto& e) { return e.position < p; });
  };
  const auto after = [&](qsizetype p) {
    return p + std::count_if(edges.begin(), edges.end(), [&](const auto& e) { return e.position <= p; });
  };
  QVector<OffsetMapEntry> offsets;
  QVector<DisplayOffsetMapEntry> displayOffsets;
  for (const auto& e : offsetMap_) {
    qsizetype start = e.displayStart;
    const auto visible = [&](qsizetype p) {
      return e.visibleStart +
             (e.displayEnd > e.displayStart ? (e.visibleEnd - e.visibleStart) * (p - e.displayStart) / (e.displayEnd - e.displayStart) : 0);
    };
    for (const auto& edge : edges)
      if (edge.position > start && edge.position < e.displayEnd) {
        offsets.push_back({after(start), before(edge.position), visible(start), visible(edge.position)});
        start = edge.position;
      }
    offsets.push_back({after(start), e.displayEnd > start ? before(e.displayEnd) : after(e.displayEnd), visible(start), e.visibleEnd});
  }
  for (const auto& e : displayOffsetMap_) {
    qsizetype start = e.layoutStart;
    const auto projected = [&](qsizetype p) {
      return e.projectionStart + (e.layoutEnd > e.layoutStart
                                      ? (e.projectionEnd - e.projectionStart) * (p - e.layoutStart) / (e.layoutEnd - e.layoutStart)
                                      : 0);
    };
    for (const auto& edge : edges)
      if (edge.position > start && edge.position < e.layoutEnd) {
        displayOffsets.push_back({projected(start), projected(edge.position), after(start), before(edge.position)});
        start = edge.position;
      }
    displayOffsets.push_back(
        {projected(start), e.projectionEnd, after(start), e.layoutEnd > start ? before(e.layoutEnd) : after(e.layoutEnd)});
  }
  for (const auto& edge : edges) {
    const qsizetype pos = before(edge.position);
    const qsizetype visible = visibleOffsetForDisplayOffset(edge.position);
    const qsizetype projected = projectionDisplayOffsetForLayoutOffset(edge.position, InlineProjectionBias::Backward);
    offsets.push_back({pos, pos + 1, visible, visible});
    displayOffsets.push_back({projected, projected, pos, pos + 1});
    inlineSpacers_.push_back({pos, edge.width});
  }
  for (auto it = edges.rbegin(); it != edges.rend(); ++it) displayText_.insert(it->position, kInlineBoxSpacer);
  std::stable_sort(offsets.begin(), offsets.end(), [](const auto& a, const auto& b) { return a.displayStart < b.displayStart; });
  std::stable_sort(displayOffsets.begin(), displayOffsets.end(),
                   [](const auto& a, const auto& b) { return a.layoutStart < b.layoutStart; });
  offsetMap_ = std::move(offsets);
  displayOffsetMap_ = std::move(displayOffsets);
  for (auto& atom : mathAtoms_) {
    atom.displayEnd = before(atom.displayEnd);
    atom.displayStart = after(atom.displayStart);
  }
  for (auto& atom : imageAtoms_) {
    atom.displayEnd = before(atom.displayEnd);
    atom.displayStart = after(atom.displayStart);
  }
  for (auto& atom : htmlAtoms_) {
    atom.displayEnd = before(atom.displayEnd);
    atom.displayStart = after(atom.displayStart);
  }
  for (auto& atom : linkBeforeAtoms_) {
    atom.displayEnd = before(atom.displayEnd);
    atom.displayStart = after(atom.displayStart);
  }
}

void InlineLayout::buildHtmlFormatSpans(const RenderTheme& theme, qreal width) {
  htmlFormatSpans_.clear();
  htmlInlineBoxRuns_.clear();
  const auto& data = projection_.htmlFormatData();
  for (const auto& hd : data) {
    if (!hd.atomicHtml.isEmpty()) continue;
    quintptr previousBoxId = 0;
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
      hs.lineHeight = fs.lineHeight;
      hs.verticalAlignment = fs.verticalAlignment;
      hs.keyboard = fs.keyboard;
      hs.fontFamilies = fs.fontFamilies;
      hs.font = fs.font;
      hs.fontSet = fs.fontSet;
      if (fs.inlineBoxId) {
        if (previousBoxId == fs.inlineBoxId && !htmlInlineBoxRuns_.isEmpty() && htmlInlineBoxRuns_.back().end == hs.layoutStart) {
          htmlInlineBoxRuns_.back().end = hs.layoutEnd;
        } else {
          const auto& style = fs.inlineBoxStyle;
          htmlInlineBoxRuns_.push_back(
              {hs.layoutStart, hs.layoutEnd, LayoutBox::place(style.key, style, theme.usedBoxForStyle(style, width), {}, fs.font)});
        }
      }
      previousBoxId = fs.inlineBoxId;
      htmlFormatSpans_.push_back(hs);
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

void InlineLayout::buildImageAtoms(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width, const QString& documentPath) {
  Q_UNUSED(inlines);
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
      const auto previous = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
      const QString spanText = previous.valid ? displayText_.mid(previous.start, previous.end - previous.start)
                                              : projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      for (auto& atom : mathAtoms_)
        if (previous.valid && atom.displayStart == previous.start) {
          atom.displayStart = displayStart;
          atom.displayEnd = rebuiltDisplay.size();
        }
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

    const QUrl resolved = resolvedUrlForDocumentResource(srcUrl, documentPath);
    const QString resourceKey = resolved.toString(QUrl::FullyEncoded);
    const bool isRemote = resolved.scheme() == QLatin1String("http") || resolved.scheme() == QLatin1String("https");
    QImage image = ImageLoader::instance().image(resourceKey);

    if (image.isNull()) {
      // Image not yet available — show a placeholder icon inline.
      const bool isLoading = isRemote && ImageLoader::instance().isPending(resourceKey);
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
        atom.resourceUrl = resourceKey;
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

    const auto imageBox = [&] {
      const auto snapshot = spanStyles_.value(span.displayStart);
      CssFormattingItem item;
      item.style = snapshot ? snapshot->style : ThemeElementStyle{};
      item.style.box = theme.usedBoxForStyle(item.style, width);
      // usedBoxForStyle has resolved padding against this line's containing
      // width. Freeze it so image sizing uses the same values as its snapshot.
      item.style.box.paddingLengths = {};
      item.style.box.marginLengths = {};
      item.naturalSize = QSizeF(image.size()) * zoom * theme.zoomPercent() / 100.0;
      const auto size = cssReplacedSize(item, width);
      return std::make_shared<const LayoutBox>(LayoutBox::place("img", item.style, item.style.box, QRectF({}, size)));
    }();
    const auto imageDisplaySize = imageBox->borderBox.size();
    if (collapsed) {
      // Inactive: replace alt text with placeholder, render image inline.
      const QSizeF displaySize = imageDisplaySize;

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
      atom.resourceUrl = resourceKey;
      atom.displaySize = displaySize;
      atom.cssBox = imageBox;
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
      preview.displaySize = imageDisplaySize;
      preview.cssBox = imageBox;
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

void InlineLayout::buildHtmlAtoms(qreal width, const QFont& font, const QString& documentPath) {
  html::HtmlRenderer renderer;
  for (auto it = projection_.htmlFormatData().crbegin(); it != projection_.htmlFormatData().crend(); ++it) {
    const auto& data = *it;
    if (data.atomicHtml.isEmpty()) continue;
    for (const auto& span : projection_.spans()) {
      if (span.kind != InlineSpanKind::HtmlContent || span.displayStart != data.displayStart) continue;
      const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
      if (!range.valid || range.end <= range.start) continue;
      auto layout = std::make_shared<html::HtmlLayoutResult>(
          renderer.render(data.atomicHtml, font.pointSizeF(), width, QFileInfo(documentPath).absolutePath(), data.palette));
      if (!layout->valid() || !layout->root() || layout->root()->children().empty()) continue;
      const auto& box = *layout->root()->children().front();
      const auto& geo = box.geometry();
      HtmlAtom atom;
      atom.displayStart = range.start;
      atom.displayEnd = range.start + 1;
      atom.sourceStart = span.sourceStart;
      atom.sourceEnd = span.sourceEnd;
      atom.visibleStart = span.visibleStart;
      atom.visibleEnd = span.visibleEnd;
      atom.crop = QRectF(geo.left, geo.top, geo.width, geo.height);
      atom.baseline = box.firstBaseline >= 0 ? box.firstBaseline : geo.height;
      atom.margin = box.style().margin;
      atom.layout = std::move(layout);
      // Match DOM text runs in source order, including hidden runs, before
      // projecting their source positions into the visible fragment.
      QString decoded;
      QVector<qsizetype> decodedSource;
      for (qsizetype p = 0; p < data.atomicHtml.size();) {
        if (data.atomicHtml.mid(p, 4) == "<!--") {
          const auto end = data.atomicHtml.indexOf("-->", p + 4);
          p = end < 0 ? data.atomicHtml.size() : end + 3;
        } else if (data.atomicHtml[p] == QLatin1Char('<')) {
          const auto start = p++;
          QChar quote;
          for (; p < data.atomicHtml.size(); ++p) {
            const auto c = data.atomicHtml[p];
            if (!quote.isNull()) {
              if (c == quote) quote = {};
            } else if (c == QLatin1Char('\'') || c == QLatin1Char('"'))
              quote = c;
            else if (c == QLatin1Char('>')) {
              ++p;
              break;
            }
          }
          if (isStandaloneBrTag(data.atomicHtml.mid(start, p - start))) {
            decoded += QChar::LineSeparator;
            decodedSource += atom.sourceStart + start;
          }
        } else {
          const auto start = p;
          QString text = data.atomicHtml.mid(p++, 1);
          bool literal = true;
          if (text == "&") {
            const auto end = data.atomicHtml.indexOf(';', p);
            if (end >= p && end - p < 32) {
              text = QTextDocumentFragment::fromHtml(data.atomicHtml.mid(start, end - start + 1)).toPlainText();
              p = end + 1;
              literal = text == data.atomicHtml.mid(start, p - start);
            }
          }
          if (text == "\r") {
            if (p < data.atomicHtml.size() && data.atomicHtml[p] == QLatin1Char('\n')) ++p;
            text = "\n";
            literal = false;
          }
          decoded += text;
          for (qsizetype n = 0; n < text.size(); ++n) decodedSource += atom.sourceStart + start + (literal ? n : 0);
        }
      }
      atom.sourceOffsets.fill(atom.sourceEnd, qMax<qsizetype>(1, span.visibleEnd - span.visibleStart) + 1);
      qsizetype search = 0;
      const auto mapText = [&](const auto& self, const html::HtmlBox& child, bool visible) -> void {
        visible = visible && child.style().visible && child.style().display != html::HtmlDisplay::None;
        const auto text = child.isTextRun()                     ? child.text()
                          : child.tag() == html::HtmlTag::Break ? QString(QChar::LineSeparator)
                                                                : QString();
        if (!text.isEmpty()) {
          const auto at = decoded.indexOf(text, search);
          if (at >= 0) {
            if (visible)
              for (qsizetype n = 0; n < text.size() && child.plainTextStart + n < atom.sourceOffsets.size() - 1; ++n)
                atom.sourceOffsets[child.plainTextStart + n] = decodedSource[at + n];
            search = at + text.size();
          }
        }
        for (const auto& nested : child.children()) self(self, *nested, visible);
      };
      mapText(mapText, *atom.layout->root(), true);
      const qsizetype delta = 1 - (range.end - range.start);
      displayText_.replace(range.start, range.end - range.start, QChar(0xfffc));
      for (auto& entry : offsetMap_) {
        if (entry.displayStart == range.start && entry.displayEnd == range.end)
          entry.displayEnd = range.start + 1;
        else if (entry.displayStart >= range.end) {
          entry.displayStart += delta;
          entry.displayEnd += delta;
        }
      }
      for (auto& entry : displayOffsetMap_) {
        if (entry.layoutStart == range.start && entry.layoutEnd == range.end)
          entry.layoutEnd = range.start + 1;
        else if (entry.layoutStart >= range.end) {
          entry.layoutStart += delta;
          entry.layoutEnd += delta;
        }
      }
      const auto shift = [&](auto& atoms) {
        for (auto& other : atoms)
          if (other.displayStart >= range.end) {
            other.displayStart += delta;
            other.displayEnd += delta;
          }
      };
      shift(mathAtoms_);
      shift(imageAtoms_);
      shift(htmlAtoms_);
      htmlAtoms_.push_back(std::move(atom));
    }
  }
}

void InlineLayout::buildTextLayout(const RenderTheme& theme, qreal width, const QFont& baseFont) {
  layoutText_ = layoutTextForDisplayText(displayText_);
  QVector<QTextLayout::FormatRange> formats = textLayoutFormats(theme, baseFont);

  // Splice the active IME preedit into layoutText_ at the caret so following text shifts/wraps
  // naturally instead of being overlapped. displayText_ and the projection offset maps stay
  // pristine — only the rendered QTextLayout carries the preedit (consumed by paint and
  // preeditCursorRect). The format ranges above are in displayText_/layoutText_ offsets (1:1
  // pre-splice); split any range straddling the splice point so no span styles the preedit, then
  // add the preedit's own formats on top.
  const int preeditLen = static_cast<int>(preeditText_.length());
  int insertAt = -1;
  if (!preeditText_.isEmpty() && preeditLen > 0 && preeditInsertAtSourceOffset_ >= 0) {
    // Anchor the splice at the caret's rendered position via its SOURCE offset — the same mapping
    // the blinking caret uses (cursorRectForSourceOffset). A visible-offset anchor would collapse
    // for a caret inside revealed syntax (a link's URL), splicing at the span boundary instead.
    qsizetype displayOffset = -1;
    if (layoutDisplayOffsetForSourceOffset(preeditInsertAtSourceOffset_, InlineProjectionBias::Forward, displayOffset)) {
      insertAt = qBound(0, static_cast<int>(displayOffset), static_cast<int>(layoutText_.size()));
    }
  }
  if (insertAt >= 0) {
    const QColor baseForeground = baseTextColorOverride_.isValid() ? baseTextColorOverride_ : theme.textColor();
    QVector<QTextLayout::FormatRange> shifted;
    shifted.reserve(formats.size() + preeditFormats_.size() + 1);
    for (const QTextLayout::FormatRange& fr : formats) {
      const int s = fr.start;
      const int end = s + fr.length;
      if (fr.length <= 0 || end <= insertAt) {
        shifted.append(fr);
      } else if (s >= insertAt) {
        shifted.append({s + preeditLen, fr.length, fr.format});
      } else {  // straddle: split — keep [s, insertAt) and [insertAt+P, end); never cover the preedit
        shifted.append({s, insertAt - s, fr.format});
        shifted.append({insertAt + preeditLen, end - insertAt, fr.format});
      }
    }
    if (preeditFormats_.isEmpty()) {
      QTextCharFormat underline;
      underline.setForeground(baseForeground);
      underline.setFontUnderline(true);
      shifted.append({insertAt, preeditLen, underline});
    } else {
      for (const QTextLayout::FormatRange& pf : preeditFormats_) {
        QTextCharFormat fmt = pf.format;
        if (!fmt.hasProperty(QTextFormat::ForegroundBrush)) {
          fmt.setForeground(baseForeground);
        }
        shifted.append({insertAt + pf.start, pf.length, fmt});
      }
    }
    formats = std::move(shifted);
    layoutText_.insert(insertAt, preeditText_);
    preeditSpliceInsertAt_ = insertAt;
    preeditSpliceLength_ = preeditLen;
  } else {
    preeditSpliceInsertAt_ = -1;
    preeditSpliceLength_ = 0;
  }

  textLayout_ = std::make_unique<QTextLayout>(layoutText_.isEmpty() ? QStringLiteral(" ") : layoutText_, baseFont);
  // Retain the shaping snapshot together with the line geometry. Qt otherwise
  // frees it at endLayout() and reshapes during paint/caret queries, which can
  // resolve a different font-engine state after an incremental pass.
  textLayout_->setCacheEnabled(true);
  QTextOption option;
  option.setWrapMode(wrapMode_);
  if (alignment_ != Qt::Alignment()) {
    option.setAlignment(alignment_);
  }
  textLayout_->setTextOption(option);
  textLayout_->setFormats(formats);
  // Pre-compute the runs a `:hover { color }` should recolour (only when the
  // element actually declares a hover colour). Derived from the same `formats`
  // the layout draws, so it stays in lock-step with span colouring.
  if (hoverTextColor_.isValid() || focusTextColor_.isValid()) {
    computeHoverRecolourRanges(formats, theme);
  } else {
    hoverRecolourRanges_.clear();
  }

  struct LineRun { int start, end; QFont font; qreal lineHeight; bool shifted; };
  QVector<LineRun> lineRuns;
  for (const auto& span : projection_.spans()) {
    if (span.kind == InlineSpanKind::Atom || span.kind == InlineSpanKind::HtmlContent) continue;
    const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
    if (!range.valid || range.end <= range.start) continue;
    QFont font = span.type == InlineType::Code ? codeStyle_->font : baseFont;
    if (span.bold) font.setWeight(QFont::Bold);
    if (span.italic) font.setItalic(true);
    qreal multiplier = lineHeightMultiplier_;
    if (const auto snapshot = spanStyles_.value(span.displayStart); snapshot && span.kind == InlineSpanKind::Text) {
      font = snapshot->font;
      multiplier = snapshot->style.text.lineHeight;
    }
    lineRuns.push_back({toLayoutOffset(range.start), toLayoutOffset(range.end), font,
                       multiplier > 0 ? cssLineHeightPx(font.pointSizeF(), multiplier) : 0,
                       span.subscript || span.superscript});
  }
  for (const auto& span : htmlFormatSpans_) {
    QFont font = span.fontSet ? span.font : baseFont;
    if (span.fontSize > 0) font.setPointSizeF(span.fontSize);
    lineRuns.push_back({toLayoutOffset(span.layoutStart), toLayoutOffset(span.layoutEnd), font, span.lineHeight,
                       span.verticalAlignment != QTextCharFormat::AlignNormal});
  }
  std::sort(lineRuns.begin(), lineRuns.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
  qsizetype firstRun = 0;
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
    InlineLineBox lineBox(baseFont, lineHeightMultiplier_ > 0 ? cssLineHeightPx(baseFont.pointSizeF(), lineHeightMultiplier_) : 0);
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    while (firstRun < lineRuns.size() && lineRuns[firstRun].end <= lineStart) ++firstRun;
    for (qsizetype i = firstRun; i < lineRuns.size() && lineRuns[i].start < lineEnd; ++i) {
      if (lineRuns[i].end <= lineStart) continue;
      lineBox.includeText(lineRuns[i].font, lineRuns[i].lineHeight);
      if (lineRuns[i].shifted) lineBox.includeAtomic(line.ascent(), line.height());
    }
    for (const auto& atom : htmlAtoms_) {
      const auto start = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (start >= lineStart && start < lineEnd) lineBox.includeAtomic(atom.baseline, atom.crop.height(), atom.margin);
    }
    for (const ImageAtom& atom : imageAtoms_) {
      const int start = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (atom.loaded && start >= lineStart && start < lineEnd)
        lineBox.includeAtomic(atom.displaySize.height(), atom.displaySize.height());
    }
    for (const MathAtom& atom : mathAtoms_) {
      const int start = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (atom.layout && atom.layout->valid() && start >= lineStart && start < lineEnd) {
        const auto used = mathUsedBounds(*atom.layout);
        lineBox.includeAtomic(-used.top(), used.height());
      }
    }
    const qreal allocatedHeight = lineBox.placeLine(line, height);
    lineBoxes_.push_back(QRectF(0, height, lineWidth, allocatedHeight));
    for (auto& atom : htmlAtoms_) {
      const auto start = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (start >= lineStart && start < lineEnd)
        atom.rect = QRectF(QPointF(line.cursorToX(start) + atom.margin.left(), line.y() + line.ascent() - atom.baseline), atom.crop.size());
    }
    height += allocatedHeight;
    maxWidth = qMax(maxWidth, line.naturalTextWidth());
  }
  textLayout_->endLayout();

  // Reserve space for block-level image previews (shown when cursor is on an image).
  constexpr qreal kPreviewSpacing = 6.0;
  previewHeight_ = 0.0;
  for (const ImageAtom& atom : previewAtoms_) {
    previewHeight_ += atom.displaySize.height() + kPreviewSpacing;
  }
  if (!previewAtoms_.isEmpty()) {
    previewHeight_ += kPreviewSpacing;  // leading gap: paintImagePreview starts one spacing below the text
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

void InlineLayout::paintTextShadow(QPainter& painter, QPointF origin) const {
  if (!textLayout_ || size_.width() <= 0 || size_.height() <= 0) { return; }
  const qreal blur = qMax(qreal(0.0), textShadow_.blur);
  const qreal maxOff = qMax(qAbs(textShadow_.offset.x()), qAbs(textShadow_.offset.y()));
  const int pad = qCeil(blur + maxOff + 2.0);
  const QSize size(qCeil(size_.width()) + pad * 2, qCeil(size_.height()) + pad * 2);

  // Render the text once as an alpha mask (only its coverage shape matters).
  QImage mask(size, QImage::Format_ARGB32_Premultiplied);
  mask.fill(Qt::transparent);
  { QPainter mp(&mask); mp.setRenderHint(QPainter::TextAntialiasing, true); textLayout_->draw(&mp, QPointF(pad, pad)); }

  // Recolour the mask's alpha to the shadow colour: fill the colour, then keep it
  // only where the mask had coverage (DestinationIn).
  QImage shadow(size, QImage::Format_ARGB32_Premultiplied);
  shadow.fill(Qt::transparent);
  { QPainter sp(&shadow);
    sp.fillRect(shadow.rect(), textShadow_.color);
    sp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    sp.drawImage(0, 0, mask);
  }
  if (blur > 0.0) { boxBlur(shadow, qMax(1, qRound(blur))); }
  painter.drawImage(QPointF(origin.x() - pad + textShadow_.offset.x(),
                            origin.y() - pad + textShadow_.offset.y()), shadow);
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
      const int atomStart = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (atomStart < lineStart || atomStart >= lineEnd) {
        continue;
      }
      const qreal x = line.cursorToX(atomStart);
      const qreal baseline = origin.y() + line.y() + line.ascent();
      atom.layout->paint(painter, QPointF(origin.x() + x, baseline - atom.layout->baseline));
      break;
    }
  }
}

void InlineLayout::refreshImageResources() {
  for (auto* atoms : {&imageAtoms_, &previewAtoms_})
    for (auto& atom : *atoms)
      if (!atom.resourceUrl.isEmpty()) {
        const auto current = ImageLoader::instance().cached(atom.resourceUrl);
        if (!current.isNull()) atom.image = current;
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
      const int atomStart = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (atomStart < lineStart || atomStart >= lineEnd) {
        continue;
      }
      const qreal x = line.cursorToX(atomStart);
      const qreal y = origin.y() + line.y() + line.ascent() - atom.displaySize.height();
      const QRectF targetRect(origin.x() + x, y, atom.displaySize.width(), atom.displaySize.height());
      if (atom.cssBox) paintLayoutBox(painter, *atom.cssBox, targetRect.topLeft());
      painter.drawImage(atom.cssBox ? atom.cssBox->contentBox.translated(targetRect.topLeft()) : targetRect, atom.image,
                        QRectF(atom.image.rect()));
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
    if (atom.cssBox) paintLayoutBox(painter, *atom.cssBox, targetRect.topLeft());
    painter.drawImage(atom.cssBox ? atom.cssBox->contentBox.translated(targetRect.topLeft()) : targetRect, atom.image,
                      QRectF(atom.image.rect()));
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
        format.setFont(codeStyle_->font);
        applyLetterSpacing(format, codeStyle_->font);
        if (codeStyle_->style.paint.color.isValid()) {
          format.setForeground(codeStyle_->style.paint.color);
        }
        if (span.kind == InlineSpanKind::Text) {
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
      // CSS `a { text-decoration }`: underline + style + colour, and optional overline.
      if (theme.linkUnderlined()) {
        format.setFontUnderline(true);
        const int style = theme.linkUnderlineStyle();
        format.setUnderlineStyle(style >= 0 ? static_cast<QTextCharFormat::UnderlineStyle>(style)
                                            : QTextCharFormat::SingleUnderline);
        if (const QColor uc = theme.linkUnderlineColor(); uc.isValid()) {
          format.setUnderlineColor(uc);
        }
      }
      if (theme.linkOverline()) { format.setFontOverline(true); }
    }
    const auto computedRun = spanStyles_.constFind(span.displayStart);
    if (computedRun != spanStyles_.cend() && span.kind == InlineSpanKind::Text && span.type != InlineType::InlineMath) {
      const auto& run = *computedRun.value();
      format.setFont(run.font);
      applyLetterSpacing(format, run.font);
      const auto& textStyle = run.style.text;
      format.setFontUnderline(textStyle.decorationLines & 1);
      format.setFontOverline(textStyle.decorationLines & 2);
      format.setFontStrikeOut(textStyle.decorationLines & 4);
      if (textStyle.decorationLines & 1) {
        format.setUnderlineStyle(static_cast<QTextCharFormat::UnderlineStyle>(textStyle.underlineStyle));
        if (textStyle.decorationColor.isValid()) format.setUnderlineColor(textStyle.decorationColor);
      }
      if (run.style.paint.color.isValid()) format.setForeground(run.style.paint.color);
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

  for (const auto& atom : htmlAtoms_) {
    QFont font = baseFont;
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    font.setLetterSpacing(QFont::AbsoluteSpacing, atom.crop.width() + atom.margin.left() + atom.margin.right() -
                                                      QFontMetricsF(font).horizontalAdvance(QChar(0xfffc)));
    QTextCharFormat format;
    format.setFont(font);
    format.setForeground(Qt::transparent);
    formats.push_back({static_cast<int>(atom.displayStart), 1, format});
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
    if (hs.fontSet) format.setFont(hs.font);
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
      // HTML <a> shares the Markdown link's text-decoration (underline + style +
      // colour + overline). phycat sets `a { text-decoration: none }`; this keeps
      // HTML links in step with Markdown links under the same CSS rule.
      if (theme.linkUnderlined()) {
        format.setFontUnderline(true);
        const int style = theme.linkUnderlineStyle();
        format.setUnderlineStyle(style >= 0 ? static_cast<QTextCharFormat::UnderlineStyle>(style)
                                            : QTextCharFormat::SingleUnderline);
        if (const QColor uc = theme.linkUnderlineColor(); uc.isValid()) {
          format.setUnderlineColor(uc);
        }
      }
      if (theme.linkOverline()) { format.setFontOverline(true); }
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

  for (const InlineSpacer& spacer : inlineSpacers_) {
    QFont font = baseFont;
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacer.width - QFontMetricsF(font).horizontalAdvance(kInlineBoxSpacer));
    QTextCharFormat format;
    format.setFont(font);
    format.setForeground(Qt::transparent);
    formats.push_back({static_cast<int>(spacer.start), 1, format});
  }
  return formats;
}

qsizetype InlineLayout::visibleOffsetForDisplayOffset(qsizetype displayOffset) const {
  if (offsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, displayOffset, projection_.visibleText().size());
  }
  displayOffset = qBound<qsizetype>(0, displayOffset, displayText_.size());
  for (const MathAtom& atom : mathAtoms_) {
    if (displayOffset > atom.displayStart && displayOffset <= atom.displayEnd) {
      return atom.visibleEnd;
    }
  }
  for (const ImageAtom& atom : imageAtoms_) {
    if (displayOffset > atom.displayStart && displayOffset <= atom.displayEnd) {
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
  return projection_.visibleText().size();
}

qsizetype InlineLayout::displayOffsetForVisibleOffset(qsizetype visibleOffset) const {
  if (offsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, visibleOffset, projection_.visibleText().size());
  }
  visibleOffset = qBound<qsizetype>(0, visibleOffset, projection_.visibleText().size());
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
  // Select content entries, excluding generated spacing at either boundary.
  // Shared boundary offsets can denote both the preceding run and the spacer;
  // mapping a whole span through one caret bias would include padding as text.
  bool found = false;
  for (const auto& entry : displayOffsetMap_) {
    if (entry.projectionEnd <= entry.projectionStart || entry.projectionEnd <= projectionStart || entry.projectionStart >= projectionEnd)
      continue;
    const qsizetype a = qMax(projectionStart, entry.projectionStart), b = qMin(projectionEnd, entry.projectionEnd);
    const qsizetype length = entry.projectionEnd - entry.projectionStart;
    const qsizetype start = entry.layoutStart + (a - entry.projectionStart) * (entry.layoutEnd - entry.layoutStart) / length;
    const qsizetype end = entry.layoutStart + (b - entry.projectionStart) * (entry.layoutEnd - entry.layoutStart) / length;
    if (!found) {
      range.start = start;
      range.end = end;
      found = true;
    } else {
      range.start = qMin(range.start, start);
      range.end = qMax(range.end, end);
    }
  }
  if (!found) return range;
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
    const auto bounds = lineBoxes_[i];
    if (localPos.y() < bounds.top()) {
      break;
    }
    targetLine = line;
    if (localPos.y() < bounds.bottom()) {
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
    if (displayOffset < lineStart || displayOffset > lineEnd ||
        (displayOffset == lineEnd && i + 1 < textLayout_->lineCount())) {
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
