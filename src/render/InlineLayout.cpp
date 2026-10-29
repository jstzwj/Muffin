#include "render/InlineLayout.h"
#include "render/CssPaintAnimation.h"
#include "diagnostics/ScopedPerfProbe.h"
#include "render/Filter.h"
#include <QPainterPath>
#include "render/CssFormattingContext.h"
#include "render/CssSizing.h"
#include "render/GeneratedContent.h"
#include "render/RenderMetrics.h"
#include "render/InlineFormatting.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/CssThemeMapper.h"
#include "theme/CssContent.h"
#include "theme/DocumentStyleTree.h"
#include "theme/NodeCssElement.h"
#include "theme/CssImageResources.h"
#include "theme/CssValueParser.h"

#include "document/ImageSyntaxOps.h"
#include "editor/ResourceUrl.h"
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
#include "render/TextLayout.h"
#include <QTextOption>

#include <cmath>

namespace muffin {
QPair<qreal, qreal> InlineLayout::intrinsicWidths() const {
  if (!textLayout_)
    return {};
  const auto intrinsic = intrinsicTextWidths(*textLayout_, wrapMode_ == QTextOption::NoWrap, anywhereMinimum_);
  return {intrinsic.minContent, intrinsic.maxContent};
}
namespace {
Q_LOGGING_CATEGORY(inlineBuildPerf, "muffin.perf.detail", QtWarningMsg)
Q_LOGGING_CATEGORY(inlinePaintPerf, "muffin.perf.paint", QtWarningMsg)

// Use the same object-replacement code point as HTML inline atoms.  Its format
// range supplies the measured math advance while keeping the formula
// indivisible for wrapping, painting and hit testing; the normal space before
// the atom remains the browser-compatible break opportunity.
constexpr QChar kInlineMathPlaceholder(0x00a0);
constexpr QChar kImagePlaceholder = kInlineObjectPlaceholder;
// Reserve atomic boxes with an ink-free glyph, including font fallback paths.
constexpr QChar kGeneratedPseudoPlaceholder(0x00a0);
constexpr QChar kTabIndentSourceChar(0x200b);
constexpr QChar kTabIndentLayoutChar(0x00a0);

QRectF mathUsedBounds(const math::MathLayoutResult& layout) {
  if (!layout.root)
    return {};
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
  // This Qt build's TextLayout createLine() treats only U+2028 (Line Separator) as a hard
  // break, not '\n' (0x0a) — so a '\n' in the display text (a <br> or a Markdown hard break)
  // would not wrap. Convert '\n' to U+2028 for layout only. Length-preserving (1:1), so every
  // offset/coordinate computed from displayMap_.text() stays valid; displayMap_.text() keeps the '\n'.
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
  return QColor(
      qRound(from.red() + (to.red() - from.red()) * t), qRound(from.green() + (to.green() - from.green()) * t),
      qRound(from.blue() + (to.blue() - from.blue()) * t), qRound(from.alpha() + (to.alpha() - from.alpha()) * t));
}

} // namespace

struct InlineLayout::TextLayoutPointHit {
  qsizetype displayOffset = 0;
  InlineProjectionBias bias = InlineProjectionBias::Backward;
  QRectF cursorRect;
};

void InlineLayout::build(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width,
                         const QFont& baseFont) {
  build(inlines, theme, width, baseFont, BuildOptions{});
}

void InlineLayout::build(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width,
                         const QFont& baseFont, BuildOptions options) {
  build(inlines, InlineProjection::markdownForInlines(inlines), theme, width, baseFont, options);
}

void InlineLayout::build(const QVector<InlineNode>& inlines, QString sourceText, const RenderTheme& theme, qreal width,
                         const QFont& baseFont, BuildOptions options) {
  // Keep old leases until the replacement layout has acquired its own, avoiding
  // a decode/download gap when rebuilding the same image after a resize/edit.
  const auto previousImageResources = std::move(imageResources_);
  imageResources_.clear();
  hasHtmlContent_ = false;
  shadowPainter_.reset();
  diag::ScopedPerfProbe perf("inline.build", inlineBuildPerf());
  plainText_ = flattenPlainText(inlines, options.breakOnSingleNewline, hasHtmlContent_);
  mathAtoms_.clear();
  imageAtoms_.clear();
  htmlAtoms_.clear();
  previewAtoms_.clear();
  previewHeight_ = 0.0;
  htmlFormatSpans_.clear();
  generatedPseudoAtoms_.clear();
  generatedPseudoFormats_.clear();
  inlinePseudoFingerprints_.clear();
  inlineImageFingerprints_.clear();
  displayMap_ = {};
  composition_ = {};
  lineBoxes_.clear();
  isMisspelled_ = options.isMisspelled;
  const auto sharedSnapshot = [&](const ThemeElementStyle& style, const QFont& font) {
    const auto used = theme.usedBoxForStyle(style, width);
    return options.styleCache ? options.styleCache->snapshot(style, used, font, width)
                              : std::make_shared<const LayoutBox>(LayoutBox::place(style.key, style, used, {}, font));
  };
  const auto snapshot = [&](const QString& key) {
    const auto* style =
        options.styleNode ? theme.elementStyleForNode(*options.styleNode, key) : theme.elementStyle(key);
    return sharedSnapshot(style ? *style : ThemeElementStyle{}, theme.textFontForElement(key, options.styleNode));
  };
  codeStyle_ = snapshot(QStringLiteral("code"));
  markGradient_ = GradientSpec{};
  for (const ElementBackground& eb : theme.decorations().backgrounds) {
    if (eb.host == QStringLiteral("mark")) {
      markGradient_ = eb.gradient;
    }
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
  textShadow_ = options.textShadow;
  alignment_ = options.alignment;
  wrapMode_ = options.wrapMode;
  anywhereMinimum_ = options.anywhereMinimum;
  html::HtmlColorPalette htmlPalette = html::HtmlColorPalette::defaultLight();
  htmlPalette.documentStyleSheet = theme.documentStyleSheet();
  if (options.styleNode) {
    htmlPalette.documentParent = theme.cssElementForNode(*options.styleNode);
    htmlPalette.documentParentForOffset = [&theme, owner = options.styleNode,
                                           base = qMax<qsizetype>(0, options.sourceBase)](qsizetype offset) {
      return theme.cssParentForInlineHtml(*owner, offset + base);
    };
  }
  htmlPalette.fontAliases = theme.fontAliases();
  htmlPalette.cssEnvironment = theme.documentCssEnvironment();
  htmlPalette.cssZoom = theme.zoomPercent() / 100.0;
  htmlPalette.parentFontPx =
      baseFont.pointSizeF() * 96.0 / 72.0 / htmlPalette.cssZoom / htmlPalette.cssEnvironment.textScale;
  htmlPalette.text = options.baseTextColor.isValid() ? options.baseTextColor : theme.textColor();
  {
    diag::ScopedPerfProbe perf("inline.projection", inlineBuildPerf());
    projection_ = InlineProjection(inlines, std::move(sourceText), options.projectionState, options.sourceBase,
                                   baseFont.pointSizeF(), options.pendingPrefixLength, options.smartPunct,
                                   options.breakOnSingleNewline, options.textTransform, options.renderEmoji,
                                   std::move(htmlPalette), options.sourceReplacements);
  }
  if (!options.sourceReplacements.isEmpty())
    plainText_ = projection_.visibleText();
  spanStyles_.clear();
  elementRuns_.clear();
  styleSourceBase_ = qMax<qsizetype>(0, options.sourceBase);
  if (options.styleNode) {
    QHash<const CssElement*, int> runs;
    diag::ScopedPerfProbe perf("inline.semanticStyles", inlineBuildPerf());
    const auto* owner = theme.cssElementForNode(*options.styleNode);
    for (const auto& span : projection_.spans()) {
      if (span.kind != InlineSpanKind::Text && span.type != InlineType::Image)
        continue;
      const auto style = theme.inlineStyleForNode(*options.styleNode,
                                                  span.contentSourceStart + qMax<qsizetype>(0, options.sourceBase));
      const QFont fallback = span.type == InlineType::Code ? codeStyle_->font : baseFont;
      spanStyles_.insert(span.displayStart, sharedSnapshot(style, theme.fontForStyle(style, fallback)));
      if (span.kind != InlineSpanKind::Text || span.displayEnd <= span.displayStart)
        continue;
      QVector<const CssElement*> ancestors;
      for (auto* element = theme.cssInlineElement(*options.styleNode, span.contentSourceStart + styleSourceBase_);
           element && element != owner; element = element->parent)
        ancestors.prepend(element);
      int parentRun = -1;
      for (const auto* element : ancestors) {
        auto found = runs.constFind(element);
        if (found == runs.cend()) {
          const auto css = theme.styleForElement(*element);
          ElementRun run;
          run.parent = parentRun;
          run.ancestorDepth = static_cast<int>(ancestors.size() - 1 - ancestors.indexOf(element));
          run.start = span.displayStart;
          run.end = span.displayEnd;
          run.sourceStart = span.contentSourceStart;
          run.sourceEnd = span.contentSourceEnd;
          run.identity = element->tag + '/' + QString::number(span.contentSourceStart) + '/' +
                         QString::number(ancestors.indexOf(element));
          run.style = sharedSnapshot(css, theme.fontForStyle(css, fallback));
          runs.insert(element, elementRuns_.size());
          elementRuns_.push_back(std::move(run));
        } else {
          auto& run = elementRuns_[found.value()];
          run.end = qMax(run.end, span.displayEnd);
          run.sourceEnd = qMax(run.sourceEnd, span.contentSourceEnd);
        }
        parentRun = runs.value(element);
      }
    }
  }
  buildOffsetMapFromProjection();
  buildMathAtoms(inlines, theme, width);
  buildImageAtoms(inlines, theme, width, options.documentPath, options.imageResources);
  buildHtmlAtoms(width, baseFont, options.documentPath);
  buildHtmlFormatSpans(theme, width);
  buildInlineBoxSpacing();
  buildGeneratedPseudoAtoms(theme, baseFont, width, options);
  buildHtmlFormatSpans(theme, width);
  buildTextLayout(theme, width, baseFont);
  buildInlineBoxes();
  // Genuinely empty: no text glyphs and no rendered image. Checking the image
  // atoms (not just plainText) matters because an image with blank alt text
  // flattens to empty text but is still visible content.
  isEmpty_ = plainText_.isEmpty() && imageAtoms_.isEmpty() && htmlAtoms_.isEmpty() && generatedPseudoAtoms_.isEmpty();
}

QSizeF InlineLayout::size() const {
  return size_;
}

bool InlineLayout::stylesMatch(const RenderTheme& theme, const MarkdownNode& owner) const {
  const auto fingerprint = [&](const QString& pseudo) {
    const auto rule = theme.pseudoForNode(owner, pseudo);
    return generatedContentFingerprint(rule);
  };
  if (fingerprint(QStringLiteral("before")) != pseudoBeforeFingerprint_ ||
      fingerprint(QStringLiteral("after")) != pseudoAfterFingerprint_)
    return false;
  for (const auto& stored : inlinePseudoFingerprints_) {
    const auto* origin =
        theme.cssInlineElement(owner, stored.sourceOffset, stored.ancestorDepth < 0 ? stored.tag : QString());
    for (int i = 0; origin && i < stored.ancestorDepth; ++i)
      origin = origin->parent;
    const auto rule = origin ? theme.pseudoForElement(*origin, stored.pseudo) : std::nullopt;
    if ((generatedContentFingerprint(rule)) != stored.fingerprint)
      return false;
  }
  const auto images = theme.cssInlineImages(owner);
  if (images.size() != inlineImageFingerprints_.size())
    return false;
  for (int i = 0; i < images.size(); ++i)
    if (qHashMulti(size_t(0), theme.computedStyleForElement(*images[i].element).fingerprint(),
                   theme.computedStyleForElement(*images[i].path).fingerprint()) != inlineImageFingerprints_[i])
      return false;
  // Embedded HTML has its own descendant tree. Until that tree is retained,
  // conservatively rebuild promoted HTML fragments after structural edits.
  if (!projection_.htmlFormatData().isEmpty())
    return false;
  for (const auto& span : projection_.spans()) {
    const auto found = spanStyles_.constFind(span.displayStart);
    if (found != spanStyles_.cend() &&
        theme.inlineStyleForNode(owner, span.contentSourceStart + styleSourceBase_).fingerprint !=
            found.value()->style.fingerprint)
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
    const TextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) {
      continue;
    }
    const int start = line.textStart();
    const int end = start + line.textLength();
    const qreal x1 = line.cursorToX(start);
    const qreal x2 = line.cursorToX(end);
    const QRectF lineRect(qMin(x1, x2), line.y(), qAbs(x2 - x1), line.height());
    bounds = have ? bounds.united(lineRect) : lineRect;
    have = true;
  }
  for (const auto& box : inlineBoxes_)
    bounds = bounds.united(box.visualOverflow);
  for (const auto& rect : mathAtomRects({}))
    bounds = bounds.united(rect);
  for (const auto& atom : htmlAtoms_)
    bounds = bounds.united(atom.rect);
  for (const auto& atom : generatedPseudoAtoms_)
    if (atom.kind != GeneratedPseudoAtom::Kind::Text)
      bounds = bounds.united(atom.box.visualOverflow);
  return bounds;
}

qreal InlineLayout::firstLineBaselineY() const {
  if (!textLayout_ || textLayout_->lineCount() == 0) {
    return 0.0;
  }
  const TextLine line = textLayout_->lineAt(0);
  // line.y() is the centering offset applied for line-height (see buildTextLayout);
  // line.ascent() is the font ascent. Their sum is where the first line's text
  // baseline sits relative to the layout origin.
  return line.isValid() ? (line.y() + line.ascent()) : 0.0;
}

qreal InlineLayout::lastLineBaselineY() const {
  if (!textLayout_ || textLayout_->lineCount() == 0)
    return 0;
  const auto line = textLayout_->lineAt(textLayout_->lineCount() - 1);
  return line.y() + line.ascent();
}

void InlineLayout::paint(QPainter& painter, QPointF origin, qreal hoverPhase, qreal focusPhase,
                         CssPaintContext* context, const QString& identity, qsizetype focusSourceOffset,
                         const ThemeElementStyle* hostStyle) const {
  diag::ScopedPerfProbe paintPerf("paint.inline", inlinePaintPerf());
  if (!textLayout_) {
    return;
  }

  painter.save();
  paintTextLayoutHtmlBackgrounds(painter, origin);
  QHash<int, ThemeElementStyle> animated;
  bool compositeInline = false;
  QVector<QTextLayout::FormatRange> selections;
  for (const auto& run : elementRuns_) {
    if (!context || !run.style->paintRecipe)
      continue;
    bool hover = false;
    for (const int index : run.fragments)
      hover = hover || (context->pointer && inlineBoxes_[index].borderBox.translated(origin).contains(
                                                painter.worldTransform().inverted().map(*context->pointer)));
    if (context->pointer)
      hover = hover || run.contentPath.contains(painter.worldTransform().inverted().map(*context->pointer) - origin);
    const bool focus = focusSourceOffset >= run.sourceStart && focusSourceOffset <= run.sourceEnd;
    const int state = (hover ? 1 : 0) | (focus ? 2 : 0) | (hover && context->pressed ? 4 : 0);
    const auto style = context->sample(*run.style->paintRecipe, identity + "/inline/" + run.identity, state);
    for (const int index : run.fragments)
      animated.insert(index, style);
  }
  for (const auto& atom : generatedPseudoAtoms_) {
    if (!context || atom.kind != GeneratedPseudoAtom::Kind::Text || !atom.box.paintRecipe)
      continue;
    const auto style = context->sample(
        *atom.box.paintRecipe, identity + "/pseudo/" + QString::number(atom.rangeStart),
        generatedPseudoState(atom, painter, origin, *context, hoverPhase, focusPhase, focusSourceOffset));
    for (const int index : atom.fragments)
      animated.insert(index, style);
    QTextCharFormat format;
    auto color = style.paint.color;
    format.setForeground(color);
    selections.append({toLayoutOffset(atom.displayStart),
                       int(toLayoutOffset(atom.displayEnd) - toLayoutOffset(atom.displayStart)), format});
  }
  for (const auto& span : projection_.spans()) {
    if (span.kind != InlineSpanKind::Text || span.displayEnd <= span.displayStart)
      continue;
    const auto snapshot = spanStyles_.value(span.displayStart);
    if (!snapshot)
      continue;
    auto color = snapshot->style.paint.color;
    qreal strokeWidth = snapshot->style.text.strokeWidth;
    auto strokeColor = snapshot->style.text.strokeColor;
    bool changed = false;
    if (hostStyle && color == baseRunColor_ && hostStyle->paint.color.isValid())
      color = hostStyle->paint.color;
    for (const auto& run : elementRuns_) {
      if (span.displayStart < run.start || span.displayEnd > run.end)
        continue;
      const auto found = run.fragments.isEmpty() ? animated.cend() : animated.constFind(run.fragments.front());
      const auto& style = found == animated.cend() ? run.style->style : found.value();
      if (snapshot->style.paint.color == run.style->style.paint.color &&
          style.paint.color != run.style->style.paint.color) {
        color = style.paint.color;
        changed = true;
      }
      if (style.text.strokeWidth != run.style->style.text.strokeWidth ||
          style.text.strokeColor != run.style->style.text.strokeColor) {
        strokeWidth = style.text.strokeWidth;
        strokeColor = style.text.strokeColor;
        changed = true;
      }
    }
    if (!changed)
      continue;
    QTextCharFormat format;
    format.setForeground(color);
    format.setTextOutline(strokeWidth > 0 ? QPen(strokeColor, strokeWidth * zoomScale_) : QPen(Qt::NoPen));
    const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
    if (range.valid)
      selections.append(
          {toLayoutOffset(range.start), int(toLayoutOffset(range.end) - toLayoutOffset(range.start)), format});
  }
  for (const auto& run : elementRuns_) {
    const auto found = run.fragments.isEmpty() ? animated.cend() : animated.constFind(run.fragments.front());
    const auto& paint = found == animated.cend() ? run.style->style.paint : found->paint;
    compositeInline = compositeInline || paint.opacity != 1 || hasElementFilter(paint) || !paint.transforms.empty() ||
                      !paint.maskLayers.empty();
  }
  for (const auto& atom : generatedPseudoAtoms_)
    if (atom.kind == GeneratedPseudoAtom::Kind::Text) {
      const auto found = atom.fragments.isEmpty() ? animated.cend() : animated.constFind(atom.fragments.front());
      const auto& paint = found == animated.cend() ? atom.box.style.paint : found->paint;
      compositeInline = compositeInline || paint.opacity != 1 || hasElementFilter(paint) || !paint.transforms.empty() ||
                        !paint.maskLayers.empty();
      QTextCharFormat format;
      format.setForeground(paint.color);
      selections.append({toLayoutOffset(atom.displayStart),
                         int(toLayoutOffset(atom.displayEnd) - toLayoutOffset(atom.displayStart)), format});
    }
  if (!compositeInline)
    for (int i = 0; i < inlineBoxes_.size(); ++i) {
      const auto found = animated.constFind(i);
      paintLayoutBox(painter, inlineBoxes_[i], origin, found == animated.cend() ? nullptr : &found.value());
    }
  if (elementRuns_.isEmpty())
    paintTextLayoutInlineDecorations(painter, origin);
  // CSS `text-shadow`: render the laid-out text to an offscreen image, recolour it
  // to the shadow colour (alpha mask), blur, and composite behind the crisp text.
  const auto shadow = hostStyle ? hostStyle->text.textShadow : textShadow_;
  if (!compositeInline && shadow.present && shadow.color.isValid())
    paintTextShadow(painter, origin, shadow);
  if (!compositeInline)
    for (const auto& run : elementRuns_) {
      const auto style = run.fragments.isEmpty() ? animated.cend() : animated.constFind(run.fragments.front());
      const auto& shadow = style == animated.cend() ? run.style->style.text.textShadow : style->text.textShadow;
      // Inherited text shadows are drawn once by the host. A run draws its own
      // shadow only when it differs from that inherited value.
      if (shadow.present && shadow != (hostStyle ? hostStyle->text.textShadow : textShadow_)) {
        const auto range = layoutDisplayRangeForProjectionRange(run.start, run.end);
        if (range.valid)
          paintTextShadow(painter, origin, shadow, selectionRectsForDisplayOffsets(range.start, range.end));
      }
    }
  // Foreground and stroke overrides reuse the shaped text. Paint changes cannot
  // change wrapping, hit testing or caret geometry. Legacy phase callers still
  // work; the editor supplies the sampled element style.
  const bool hasFocus = focusTextColor_.isValid() && focusPhase > 0.0;
  const bool hasHover = hoverTextColor_.isValid() && hoverPhase > 0.0;
  if ((hostStyle && hostStyle->paint.color.isValid()) || ((hasFocus || hasHover) && !hoverRecolourRanges_.isEmpty())) {
    QColor target = baseRunColor_;
    if (hasFocus) {
      target = lerpColor(target, focusTextColor_, qBound(0.0, focusPhase, 1.0));
    }
    if (hasHover) {
      target = lerpColor(target, hoverTextColor_, qBound(0.0, hoverPhase, 1.0));
    }
    if (hostStyle && hostStyle->paint.color.isValid())
      target = hostStyle->paint.color;
    // The retained formats already contain the base color. QTextLayout draws
    // its lines again for each foreground selection, even when that selection
    // sets the same color. Only submit actual paint deltas; keep independent
    // inline/stroke overrides below and preserve real host color animations.
    if (target != baseRunColor_) {
      QVector<QTextLayout::FormatRange> hostSelections;
      for (const QPair<int, int>& range : hoverRecolourRanges_) {
        QTextCharFormat format;
        format.setForeground(target);
        QTextLayout::FormatRange selection;
        selection.start = range.first;
        selection.length = range.second - range.first;
        selection.format = format;
        hostSelections.append(selection);
      }
      hostSelections.append(selections);
      selections = std::move(hostSelections);
    }
  }
  const auto content = [&](QPainter& target, const QVector<QTextLayout::FormatRange>& formats) {
    diag::ScopedPerfProbe contentPerf("paint.inlineContent", inlinePaintPerf());
    textLayout_->draw(&target, origin, formats);
    paintTextLayoutMathAtoms(target, origin);
    paintTextLayoutImageAtoms(target, origin);
    for (const auto& atom : htmlAtoms_)
      atom.layout->paintBoxFragment(target, *atom.layout->root()->children().front(),
                                    origin + atom.rect.topLeft() - atom.crop.topLeft());
    paintGeneratedPseudoAtoms(target, origin, context, identity, hoverPhase, focusPhase, focusSourceOffset);
  };
  if (!compositeInline)
    content(painter, selections);
  else {
    diag::ScopedPerfProbe compositePerf("paint.inlineComposite", inlinePaintPerf());
    auto runs = elementRuns_;
    for (const auto& atom : generatedPseudoAtoms_)
      if (atom.kind == GeneratedPseudoAtom::Kind::Text) {
        ElementRun run;
        run.parent = atom.hostRun;
        run.paintStart = atom.displayStart;
        run.paintEnd = atom.displayEnd;
        run.contentPath.setFillRule(Qt::WindingFill);
        run.style = std::make_shared<const LayoutBox>(atom.box);
        run.fragments = atom.fragments;
        for (const int fragment : atom.fragments)
          run.contentPath.addRect(inlineBoxes_[fragment].borderBox);
        for (const auto& rect : selectionRectsForDisplayOffsets(atom.displayStart, atom.displayEnd))
          run.contentPath.addRect(rect);
        runs.append(std::move(run));
      }
    // Each semantic inline owns its fragments and descendants. Composite them
    // together, so opacity/filter/transform affect glyphs and background once,
    // including nested elements and fragments on multiple visual lines.
    QVector<QPainterPath> ranges(runs.size());
    QSet<int> semanticBoxes;
    for (int i = 0; i < runs.size(); ++i) {
      const auto& run = runs[i];
      ranges[i] = run.contentPath.translated(origin);
      for (const int index : run.fragments)
        semanticBoxes.insert(index);
    }
    for (int i = 0; i < inlineBoxes_.size(); ++i)
      if (!semanticBoxes.contains(i))
        paintLayoutBox(painter, inlineBoxes_[i], origin);
    std::function<void(QPainter&, int, const TextShadow&)> paintRun;
    paintRun = [&](QPainter& target, int index, const TextShadow& inheritedShadow) {
      const auto draw = [&](QPainter& layer, const ThemeElementPaintStyle& neutral) {
        QPainterPath own;
        auto ownFormats = selections;
        TextShadow currentShadow = inheritedShadow;
        {
          diag::ScopedPerfProbe partitionPerf("paint.compositePartition", inlinePaintPerf());
          const auto hide = [&](int start, int end) {
            if (end <= start)
              return;
            QTextCharFormat format;
            format.setForeground(Qt::transparent);
            format.setTextOutline(QPen(Qt::NoPen));
            format.setFontUnderline(false);
            format.setFontOverline(false);
            format.setFontStrikeOut(false);
            ownFormats.append({start, end - start, format});
          };
          if (index < 0)
            own.addRect(visualTextBounds().united(QRectF(QPointF(), size_)).translated(origin));
          else {
            const auto& run = runs[index];
            const auto found = run.fragments.isEmpty() ? animated.cend() : animated.constFind(run.fragments.front());
            auto style = found == animated.cend() ? run.style->style : found.value();
            style.paint = neutral;
            currentShadow = style.text.textShadow;
            for (const int fragment : run.fragments)
              paintLayoutBox(layer, inlineBoxes_[fragment], origin, &style);
            own = ranges[index];
            hide(0, toLayoutOffset(run.paintStart));
            hide(toLayoutOffset(run.paintEnd), textLayout_->text().size());
          }
          for (int child = 0; child < runs.size(); ++child)
            if (runs[child].parent == index) {
              own = own.subtracted(ranges[child]);
              hide(toLayoutOffset(runs[child].paintStart), toLayoutOffset(runs[child].paintEnd));
            }
        }
        layer.save();
        {
          diag::ScopedPerfProbe clipPerf("paint.compositeClip", inlinePaintPerf());
          layer.setClipPath(own, Qt::IntersectClip);
        }
        if (currentShadow.present && currentShadow.color.isValid())
          paintTextShadow(layer, origin, currentShadow, {}, ownFormats);
        content(layer, ownFormats);
        layer.restore();
        for (int child = 0; child < runs.size(); ++child)
          if (runs[child].parent == index)
            paintRun(layer, child, currentShadow);
      };
      if (index < 0)
        draw(target, {});
      else {
        const auto& run = runs[index];
        const auto found = run.fragments.isEmpty() ? animated.cend() : animated.constFind(run.fragments.front());
        const auto& style = found == animated.cend() ? run.style->style : found.value();
        QRectF border, visual;
        for (const int fragment : run.fragments) {
          border = border.united(inlineBoxes_[fragment].borderBox.translated(origin));
          visual = visual.united(inlineBoxes_[fragment].visualOverflow.translated(origin));
        }
        const auto geometry = LayoutBox::place(style.key, style, run.style->usedBox, border, run.style->font);
        paintCssElement(target, border, visual.united(ranges[index].boundingRect()), style.paint, zoomScale_, context,
                        draw, &geometry);
      }
    };
    paintRun(painter, -1, shadow);
  }
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
      const TextLine line = textLayout_->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      const int atomStart = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (atomStart >= lineStart && atomStart <= lineEnd) {
        atomLeft = line.cursorToX(atomStart);
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
        span.kind != InlineSpanKind::OpenMarker || span.displayEnd <= span.displayStart ||
        projPosition < span.displayStart || projPosition > span.displayEnd) {
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
    if (hit.bias == InlineProjectionBias::Backward) {
      for (const auto& span : projection_.spans()) {
        if (span.type != InlineType::LineBreak || projPosition != span.displayStart)
          continue;
        const QString source = projection_.sourceText();
        qsizetype markerStart = span.sourceStart;
        // cmark can include hard-break markers in the preceding text node's
        // source range, even though they are absent from its displayed text.
        if (markerStart < source.size() && source.at(markerStart) == QLatin1Char('\n')) {
          qsizetype spacesStart = markerStart;
          while (spacesStart > 0 && source.at(spacesStart - 1) == QLatin1Char(' '))
            --spacesStart;
          if (markerStart - spacesStart >= 2)
            markerStart = spacesStart;
          else if (markerStart > 0 && source.at(markerStart - 1) == QLatin1Char('\\'))
            --markerStart;
        }
        sourceOffset = qMin(sourceOffset, markerStart);
        break;
      }
    }
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
  const auto hit = textLayoutHitForPoint(localPos);
  const auto display = hit.displayOffset;
  for (const auto& atom : generatedPseudoAtoms_)
    if (display >= atom.rangeStart && display <= atom.rangeEnd)
      return cursorRect(atom.visibleAnchor);
  return hit.cursorRect;
}

QString InlineLayout::linkHrefAtLocalPos(QPointF localPos) const {
  for (const auto& atom : htmlAtoms_)
    if (atom.rect.contains(localPos))
      return atom.layout->hitTest(localPos - atom.rect.topLeft() + atom.crop.topLeft()).linkHref;
  if (!textLayout_)
    return {};
  // A click in the right half of the last generated glyph can resolve to its
  // ending caret. Use the painted fragment geometry for the link target.
  for (const auto& atom : generatedPseudoAtoms_) {
    if (atom.href.isEmpty())
      continue;
    if (atom.kind != GeneratedPseudoAtom::Kind::Text) {
      if (atom.box.borderBox.contains(localPos))
        return atom.href;
    } else
      for (const auto& rect : selectionRectsForDisplayOffsets(atom.displayStart, atom.displayEnd))
        if (rect.contains(localPos))
          return atom.href;
  }
  const qsizetype layoutOffset = textLayoutDisplayOffsetForPoint(localPos);
  for (const auto& atom : generatedPseudoAtoms_)
    if (layoutOffset >= atom.rangeStart && layoutOffset < atom.rangeEnd && !atom.href.isEmpty())
      return atom.href;
  const qsizetype projOffset = projectionDisplayOffsetForLayoutOffset(layoutOffset, InlineProjectionBias::Backward);
  return projection_.linkHrefAtDisplayOffset(projOffset);
}

QString InlineLayout::imageSrcAtLocalPos(QPointF localPos) const {
  for (const auto& atom : htmlAtoms_)
    if (atom.rect.contains(localPos))
      return atom.layout->hitTest(localPos - atom.rect.topLeft() + atom.crop.topLeft()).imageSrc;
  if (!textLayout_)
    return {};
  for (const ImageAtom& atom : imageAtoms_) {
    if (imageAtomRect(atom).contains(localPos)) {
      return atom.srcUrl;
    }
  }
  return {};
}

QRectF InlineLayout::cursorRect(qsizetype textOffset) const {
  for (const auto& atom : htmlAtoms_)
    if (textOffset > atom.visibleStart && textOffset < atom.visibleEnd)
      return atom.layout->cursorRectForTextOffset(textOffset - atom.visibleStart)
          .translated(atom.rect.topLeft() - atom.crop.topLeft());
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
      if (!rect.isNull())
        return rect.translated(atom.rect.topLeft() - atom.crop.topLeft());
    }
  for (const MathAtom& atom : mathAtoms_) {
    if (sourceOffset > atom.sourceStart && sourceOffset < atom.sourceEnd) {
      const qsizetype displayOffset =
          sourceOffset - atom.sourceStart < atom.sourceEnd - sourceOffset ? atom.displayStart : atom.displayEnd;
      return textLayoutCursorRectForDisplayOffset(displayOffset);
    }
  }
  for (const ImageAtom& atom : imageAtoms_) {
    if (sourceOffset > atom.sourceStart && sourceOffset < atom.sourceEnd) {
      const qsizetype displayOffset =
          sourceOffset - atom.sourceStart < atom.sourceEnd - sourceOffset ? atom.displayStart : atom.displayEnd;
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
  const int displayOffset = toLayoutOffset(static_cast<int>(displayOffsetForVisibleOffset(textOffset)));
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const TextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) {
      continue;
    }
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
  displayOffset = toLayoutOffset(static_cast<int>(displayOffset));
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const TextLine line = textLayout_->lineAt(i);
    if (!line.isValid()) {
      continue;
    }
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
  const TextLine line = textLayout_->lineAt(lineIndex);
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
  const TextLine line = textLayout_->lineAt(lineIndex);
  if (!line.isValid()) {
    return 0;
  }
  return hitTestTextOffset(QPointF(localX, line.y() + line.height() * 0.5));
}

qsizetype InlineLayout::sourceOffsetAtVisualLineX(int lineIndex, qreal localX) const {
  if (!textLayout_ || lineIndex < 0 || lineIndex >= textLayout_->lineCount()) {
    return 0;
  }
  const TextLine line = textLayout_->lineAt(lineIndex);
  if (!line.isValid()) {
    return 0;
  }
  return hitTestSourceOffset(QPointF(localX, line.y() + line.height() * 0.5));
}

int InlineLayout::toLayoutOffset(int displayOffset) const {
  return composition_.toLayoutOffset(displayOffset);
}

QRectF InlineLayout::preeditCursorRect(QPointF origin) const {
  if (!textLayout_ || !composition_.active() || preeditCursor_ < 0) {
    return {};
  }
  const int pos = composition_.cursorOffset(preeditCursor_);
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const TextLine line = textLayout_->lineAt(i);
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
      const TextLine line = textLayout_->lineAt(i);
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
    if (qMax(startOffset, endOffset) <= atom.visibleStart || qMin(startOffset, endOffset) >= atom.visibleEnd)
      continue;
    for (const auto& rect :
         atom.layout->selectionRects(qMax<qsizetype>(0, qMin(startOffset, endOffset) - atom.visibleStart),
                                     qMin(atom.visibleEnd, qMax(startOffset, endOffset)) - atom.visibleStart))
      rects.push_back(rect.translated(atom.rect.topLeft() - atom.crop.topLeft()));
  }
  return rects;
}

QVector<QRectF> InlineLayout::selectionRectsForSourceOffsets(qsizetype startSourceOffset,
                                                             qsizetype endSourceOffset) const {
  qsizetype startDisplayOffset = -1;
  qsizetype endDisplayOffset = -1;
  if (!layoutDisplayOffsetForSourceOffset(qMin(startSourceOffset, endSourceOffset), InlineProjectionBias::Backward,
                                          startDisplayOffset) ||
      !layoutDisplayOffsetForSourceOffset(qMax(startSourceOffset, endSourceOffset), InlineProjectionBias::Forward,
                                          endDisplayOffset)) {
    return {};
  }
  auto rects = selectionRectsForDisplayOffsets(startDisplayOffset, endDisplayOffset);
  for (const auto& atom : htmlAtoms_) {
    const auto first = qMin(startSourceOffset, endSourceOffset), last = qMax(startSourceOffset, endSourceOffset);
    if (last <= atom.sourceStart || first >= atom.sourceEnd)
      continue;
    if (first <= atom.sourceStart && last >= atom.sourceEnd)
      rects.push_back(atom.rect);
    else {
      const auto begin =
          std::lower_bound(atom.sourceOffsets.begin(), atom.sourceOffsets.end(), first) - atom.sourceOffsets.begin();
      const auto end =
          std::lower_bound(atom.sourceOffsets.begin(), atom.sourceOffsets.end(), last) - atom.sourceOffsets.begin();
      for (const auto& rect : atom.layout->selectionRects(begin, end))
        rects.push_back(rect.translated(atom.rect.topLeft() - atom.crop.topLeft()));
    }
  }
  return rects;
}

QVector<QRectF> InlineLayout::selectionRectsForDisplayOffsets(qsizetype startDisplayOffset,
                                                              qsizetype endDisplayOffset) const {
  QVector<QRectF> rects;
  if (!textLayout_) {
    return rects;
  }

  const int start = toLayoutOffset(qBound(0, static_cast<int>(qMin(startDisplayOffset, endDisplayOffset)),
                                          static_cast<int>(displayMap_.text().size())));
  const int end = toLayoutOffset(qBound(0, static_cast<int>(qMax(startDisplayOffset, endDisplayOffset)),
                                        static_cast<int>(displayMap_.text().size())));
  if (start == end) {
    return rects;
  }

  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const TextLine line = textLayout_->lineAt(i);
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
    if (box.hostKey == QStringLiteral("code") || box.hostKey.contains(QStringLiteral("::")))
      paintLayoutBox(painter, box, origin);
}

QString InlineLayout::plainText() const {
  return plainText_;
}
QString InlineLayout::visibleText() const {
  return projection_.visibleText();
}
QString InlineLayout::displayText() const {
  return displayMap_.text();
}
int InlineLayout::mathAtomCount() const {
  return static_cast<int>(mathAtoms_.size());
}
QVector<QTextLayout::FormatRange> InlineLayout::debugTextFormats(const RenderTheme& theme,
                                                                 const QFont& baseFont) const {
  return textLayoutFormats(theme, baseFont, shapingText());
}

void InlineLayout::paintTextLayoutInlineDecorations(QPainter& painter, QPointF origin) const {
  if (!textLayout_) {
    return;
  }
  const bool hasMark = markGradient_.kind != GradientSpec::Kind::None;
  if (!hasMark)
    return;

  // mark background-image gradient, per occupied line (mirrors code-span rects).
  if (hasMark) {
    painter.save();
    for (const InlineProjectionSpan& span : projection_.spans()) {
      if (!span.highlight || span.kind != InlineSpanKind::Text || span.displayEnd <= span.displayStart) {
        continue;
      }
      for (int i = 0; i < textLayout_->lineCount(); ++i) {
        const TextLine line = textLayout_->lineAt(i);
        if (!line.isValid()) {
          continue;
        }
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        const DisplayOffsetRange lr = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
        if (!lr.valid) {
          continue;
        }
        const int rs = qMax(lineStart, toLayoutOffset(static_cast<int>(lr.start)));
        const int re = qMin(lineEnd, toLayoutOffset(static_cast<int>(lr.end)));
        if (rs >= re) {
          continue;
        }
        const qreal x1 = line.cursorToX(rs);
        const qreal x2 = line.cursorToX(re);
        const QRectF rect(origin.x() + qMin(x1, x2), origin.y() + line.y(), qAbs(x2 - x1), line.height());
        painter.fillRect(rect, GradientPainter::makeBrush(markGradient_, rect, zoomScale_));
      }
    }
    painter.restore();
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
      const TextLine line = textLayout_->lineAt(i);
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
      const QRectF rect(origin.x() + qMin(x1, x2) - 1.0, origin.y() + line.y() + 1.0, qAbs(x2 - x1) + 2.0,
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
    if (box.hostKey == QStringLiteral("kbd"))
      paintLayoutBox(painter, box, origin);
}

void InlineLayout::buildInlineBoxes() {
  diag::ScopedPerfProbe perf("inline.boxes", inlineBuildPerf());
  inlineBoxes_.clear();
  if (!textLayout_)
    return;
  const auto composite = [](const ThemeElementStyle& style) {
    return style.paint.opacity != 1 || hasElementFilter(style.paint) || !style.paint.transforms.empty() ||
           !style.paint.maskLayers.empty();
  };
  const bool needsInkBounds = std::any_of(elementRuns_.cbegin(), elementRuns_.cend(), [&](const auto& run) {
    return composite(run.style->style) ||
           (run.style->paintRecipe &&
            std::any_of(run.style->paintRecipe->states.cbegin(), run.style->paintRecipe->states.cend(), composite));
  });
  const auto add = [&](qsizetype start, qsizetype end, const LayoutBox& style, QVector<int>* fragments = nullptr) {
    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const auto line = textLayout_->lineAt(i);
      const int first = qMax(line.textStart(), toLayoutOffset(start));
      const int last = qMin(line.textStart() + line.textLength(), toLayoutOffset(end));
      if (first >= last)
        continue;
      const qreal left = line.cursorToX(first), right = line.cursorToX(last);
      if (fragments)
        fragments->push_back(inlineBoxes_.size());
      inlineBoxes_.push_back(LayoutBox::inlineFragment(style, left, right, line.y() + line.ascent(),
                                                       first == toLayoutOffset(start), last == toLayoutOffset(end)));
    }
  };
  for (auto& run : elementRuns_) {
    run.fragments.clear();
    run.contentPath = {};
    const auto range = layoutDisplayRangeForProjectionRange(run.start, run.end);
    if (range.valid) {
      run.paintStart = range.start;
      run.paintEnd = range.end;
      add(range.start, range.end, *run.style, &run.fragments);
      run.contentPath.setFillRule(Qt::WindingFill);
      for (const auto& rect : selectionRectsForDisplayOffsets(range.start, range.end))
        run.contentPath.addRect(rect);
      if (!needsInkBounds)
        continue;
      for (int i = 0; i < textLayout_->lineCount(); ++i) {
        const auto line = textLayout_->lineAt(i);
        const int first = qMax(line.textStart(), toLayoutOffset(range.start));
        const int last = qMin(line.textStart() + line.textLength(), toLayoutOffset(range.end));
        if (first >= last)
          continue;
        for (const auto& glyphRun : line.glyphRuns(first, last - first)) {
          const auto glyphs = glyphRun.glyphIndexes();
          const auto positions = glyphRun.positions();
          const auto font = glyphRun.rawFont();
          for (int glyph = 0; glyph < glyphs.size(); ++glyph)
            run.contentPath.addRect(
                font.boundingRect(glyphs[glyph]).translated(positions[glyph]).adjusted(-1, -1, 1, 1));
        }
      }
    }
  }
  if (elementRuns_.isEmpty())
    for (const auto& span : projection_.spans()) {
      if (span.type != InlineType::Code || span.kind != InlineSpanKind::Text)
        continue;
      const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
      if (range.valid)
        add(range.start, range.end, *spanStyles_.value(span.displayStart, codeStyle_));
    }
  for (const auto& run : htmlInlineBoxRuns_)
    add(run.start, run.end, run.box);
  for (auto& atom : generatedPseudoAtoms_) {
    atom.fragments.clear();
    if (atom.kind == GeneratedPseudoAtom::Kind::Text && atom.box.valid)
      add(atom.displayStart, atom.displayEnd, atom.box, &atom.fragments);
    QPainterPath path;
    if (atom.kind == GeneratedPseudoAtom::Kind::Text)
      for (const auto& rect : selectionRectsForDisplayOffsets(atom.displayStart, atom.displayEnd))
        path.addRect(rect);
    else
      path.addRect(atom.box.visualOverflow.united(atom.box.borderBox));
    for (int parent = atom.hostRun; parent >= 0; parent = elementRuns_[parent].parent) {
      auto& run = elementRuns_[parent];
      run.contentPath.addPath(path);
      run.paintStart = qMin(run.paintStart, atom.rangeStart);
      run.paintEnd = qMax(run.paintEnd, atom.rangeEnd);
    }
  }
}

void InlineLayout::buildOffsetMapFromProjection() {
  displayMap_.reset(projection_);
}

void InlineLayout::buildInlineBoxSpacing() {
  diag::ScopedPerfProbe perf("inline.spacing", inlineBuildPerf());
  inlineSpacers_.clear();
  struct Edge {
    qsizetype position;
    qreal width;
  };
  QVector<Edge> edges;
  const auto addBox = [&](qsizetype start, qsizetype end, const LayoutBox& box) {
    if (end <= start)
      return;
    const auto inset = LayoutBox::insets(box.usedBox);
    if (inset.left() > 0)
      edges.push_back({start, inset.left()});
    if (inset.right() > 0)
      edges.push_back({end, inset.right()});
  };
  for (const auto& run : elementRuns_) {
    const auto range = layoutDisplayRangeForProjectionRange(run.start, run.end);
    if (range.valid)
      addBox(range.start, range.end, *run.style);
  }
  if (elementRuns_.isEmpty())
    for (const auto& span : projection_.spans()) {
      if (span.type != InlineType::Code || span.kind != InlineSpanKind::Text)
        continue;
      const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
      if (range.valid)
        addBox(range.start, range.end, *spanStyles_.value(span.displayStart, codeStyle_));
    }
  for (const auto& run : htmlInlineBoxRuns_)
    addBox(run.start, run.end, run.box);
  if (edges.isEmpty())
    return;
  std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
  QVector<Edge> unique;
  for (const auto& edge : edges) {
    if (!unique.isEmpty() && unique.back().position == edge.position)
      unique.back().width += edge.width;
    else
      unique.push_back(edge);
  }
  edges = std::move(unique);
  QVector<qsizetype> positions;
  for (const auto& edge : edges)
    positions.push_back(edge.position);
  const auto inserted = displayMap_.insertSpacing(std::move(positions), kInlineBoxSpacer);
  const auto before = [&](qsizetype p) { return inserted.before(p); };
  const auto after = [&](qsizetype p) { return inserted.after(p); };
  for (const auto& edge : edges)
    inlineSpacers_.push_back({before(edge.position), edge.width});
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
}

void InlineLayout::insertGeneratedPseudoText(const QString& text, const PseudoElementRule& rule,
                                             GeneratedPseudoAtom::Kind kind, const QFont& font, qreal width,
                                             qreal height, qreal marginLeft, qreal marginRight, bool before,
                                             qsizetype projectionAnchor, qsizetype visibleAnchor, QString href,
                                             qsizetype layoutPosition) {
  if (text.isEmpty())
    return;
  QString inserted = text;
  const bool leftSpacer = kind == GeneratedPseudoAtom::Kind::Text && !qFuzzyIsNull(marginLeft);
  const bool rightSpacer = kind == GeneratedPseudoAtom::Kind::Text && !qFuzzyIsNull(marginRight);
  if (leftSpacer)
    inserted.prepend(kInlineBoxSpacer);
  if (rightSpacer)
    inserted.append(kInlineBoxSpacer);
  const bool block = rule.computed && rule.computed->resolvedValue("display") == "block";
  const bool atomicBreak = !block && kind != GeneratedPseudoAtom::Kind::Text;
  // TextLayout reserves U+FFFC for QTextDocument's object handlers. Keep the
  // calibrated glyph and explicitly supply the atomic box's UAX #14 break
  // opportunity at its adjoining text edge instead of joining it to a word.
  if (atomicBreak) {
    inserted.append(QChar(0x200b));
    inserted.prepend(QChar(0x200b));
  }
  if (block) {
    if (before)
      inserted.append(QChar::LineSeparator);
    else
      inserted.prepend(QChar::LineSeparator);
  }
  const qsizetype position =
      layoutPosition >= 0
          ? layoutPosition
          : layoutDisplayOffsetForProjectionOffset(projectionAnchor, before ? InlineProjectionBias::Forward
                                                                            : InlineProjectionBias::Backward);
  const qsizetype length = inserted.size();

  const auto shift = [&](qsizetype& start, qsizetype& end) {
    if (start >= position) {
      start += length;
      end += length;
    } else if (end > position) {
      end += length;
    }
  };
  for (auto& atom : mathAtoms_)
    shift(atom.displayStart, atom.displayEnd);
  for (auto& atom : imageAtoms_)
    shift(atom.displayStart, atom.displayEnd);
  for (auto& atom : htmlAtoms_)
    shift(atom.displayStart, atom.displayEnd);
  for (auto& atom : generatedPseudoAtoms_) {
    shift(atom.displayStart, atom.displayEnd);
    shift(atom.rangeStart, atom.rangeEnd);
  }
  for (auto& spacer : inlineSpacers_) {
    if (spacer.start >= position)
      spacer.start += length;
  }
  for (auto& format : generatedPseudoFormats_) {
    if (format.start >= position)
      format.start += static_cast<int>(length);
  }

  displayMap_.insertGenerated(position, inserted, projectionAnchor, visibleAnchor);

  GeneratedPseudoAtom atom;
  atom.rangeStart = position;
  atom.rangeEnd = position + length;
  atom.displayStart = position + (leftSpacer ? 1 : 0) + ((block && !before) || atomicBreak ? 1 : 0);
  atom.displayEnd = atom.displayStart + text.size();
  atom.advance = kind != GeneratedPseudoAtom::Kind::Text ? width + marginLeft + marginRight : 0.0;
  atom.width = width;
  atom.height = height;
  atom.marginLeft = marginLeft;
  atom.marginRight = marginRight;
  atom.baseline = height;
  atom.kind = kind;
  atom.text = kind == GeneratedPseudoAtom::Kind::Text ? text : QString();
  atom.rule = rule;
  atom.font = font;
  atom.block = block;
  atom.before = before;
  atom.visibleAnchor = visibleAnchor;
  atom.href = std::move(href);

  QTextCharFormat format;
  if (kind == GeneratedPseudoAtom::Kind::Text) {
    format.setFont(font);
    if (rule.color.isValid()) {
      auto color = rule.color;
      color.setAlphaF(color.alphaF() * rule.opacity);
      format.setForeground(color);
    }
  } else {
    QFont markerFont = font;
    markerFont.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    const qreal glyphWidthWithoutSpacing = TextFontMetrics(markerFont).horizontalAdvance(kGeneratedPseudoPlaceholder);
    markerFont.setLetterSpacing(QFont::AbsoluteSpacing, atom.advance - glyphWidthWithoutSpacing);
    format.setFont(markerFont);
    format.setForeground(Qt::transparent);
  }
  generatedPseudoFormats_.push_back({static_cast<int>(atom.displayStart), static_cast<int>(text.size()), format});
  if (leftSpacer)
    inlineSpacers_.push_back({atom.displayStart - 1, marginLeft});
  if (rightSpacer)
    inlineSpacers_.push_back({atom.displayEnd, marginRight});
  generatedPseudoAtoms_.push_back(std::move(atom));
}

void InlineLayout::buildGeneratedPseudoAtoms(const RenderTheme& theme, const QFont& baseFont, qreal width,
                                             const BuildOptions& options) {
  pseudoBeforeFingerprint_ = generatedContentFingerprint(options.pseudoBefore);
  pseudoAfterFingerprint_ = generatedContentFingerprint(options.pseudoAfter);
  const auto add = [&](const PseudoElementRule* rule, const QString& resolvedText, bool before,
                       qsizetype projectionAnchor, qsizetype visibleAnchor, const QString& href = QString(),
                       const CssElement* origin = nullptr, qsizetype hostStart = -1, qsizetype hostEnd = -1,
                       qsizetype position = -1, const QFont* hostFont = nullptr) {
    if (!rule || rule->absolute)
      return;
    const auto value = generatedContentStyle(theme, *rule, hostFont ? *hostFont : baseFont, {width, -1}, resolvedText);
    if (!value)
      return;
    auto v = *value;
    // Collapsible whitespace leaves an empty inline-block. Preserve its CSS
    // dimensions when a mask supplies the visible shape instead of text ink.
    if (v.text.trimmed().isEmpty() && rule->computed && rule->computed->resolvedValue("display") == "inline-block")
      v.atomic = true;
    const auto inset = LayoutBox::insets(v.used);
    if (v.atomic) {
      insertGeneratedPseudoText(QString(kGeneratedPseudoPlaceholder), *rule,
                                v.icon ? GeneratedPseudoAtom::Kind::Icon : GeneratedPseudoAtom::Kind::Shape, v.font,
                                v.width, v.height, v.used.margin.left(), v.used.margin.right(), before,
                                projectionAnchor, visibleAnchor, href, position);
    } else {
      insertGeneratedPseudoText(v.text, *rule, GeneratedPseudoAtom::Kind::Text, v.font, 0, 0,
                                v.used.margin.left() + inset.left(), v.used.margin.right() + inset.right(), before,
                                projectionAnchor, visibleAnchor, href, position);
    }
    auto& atom = generatedPseudoAtoms_.last();
    auto style = v.style;
    if (!origin && options.styleNode)
      origin = theme.cssElementForNode(*options.styleNode);
    if (origin)
      style.paintRecipe = theme.paintRecipeForElement(*origin, rule->pseudo);
    atom.box = LayoutBox::place(v.style.key, std::move(style), v.used, {}, v.font);
    atom.hostRangeStart = hostStart;
    atom.hostRangeEnd = hostEnd;
    if (hostStart >= 0) {
      projection_.sourceOffsetForDisplayOffset(hostStart, InlineProjectionBias::Forward, atom.hostSourceStart);
      projection_.sourceOffsetForDisplayOffset(hostEnd, InlineProjectionBias::Backward, atom.hostSourceEnd);
    }
    if (v.icon && rule->svgFromMask)
      atom.box.style.paint.backgroundColor = QColor();
  };

  struct LinkRun {
    const CssElement* origin;
    qsizetype start, end, source;
    QString href;
    int ancestorDepth = -1;
    int parent = -1;
    QFont font;
  };
  QVector<LinkRun> links;
  const auto collect = [&](qsizetype start, qsizetype end, qsizetype source, const QString& href,
                           const CssElement* knownOrigin = nullptr) {
    if (end <= start || href.isEmpty())
      return;
    const auto* origin = knownOrigin         ? knownOrigin
                         : options.styleNode ? theme.cssInlineElement(*options.styleNode, source, QStringLiteral("a"))
                                             : nullptr;
    for (auto& link : links) {
      if (origin ? link.origin == origin : link.end == start && link.href == href) {
        link.start = qMin(link.start, start);
        link.end = qMax(link.end, end);
        return;
      }
    }
    links.push_back({origin, start, end, source, href, -1, -1, baseFont});
  };
  for (const auto& span : projection_.spans()) {
    if (!span.link || span.kind == InlineSpanKind::OpenMarker || span.kind == InlineSpanKind::CloseMarker ||
        span.kind == InlineSpanKind::HiddenSyntax || span.kind == InlineSpanKind::EmptyContentSlot)
      continue;
    collect(span.displayStart, span.displayEnd, span.contentSourceStart + styleSourceBase_,
            projection_.linkHrefAtDisplayOffset(span.displayStart));
  }
  for (const auto& html : projection_.htmlFormatData()) {
    if (!html.atomicHtml.isEmpty())
      continue;
    for (const auto& link : html.links) {
      qsizetype source = 0;
      const auto start = html.displayStart + link.start;
      if (projection_.sourceOffsetForDisplayOffset(start, InlineProjectionBias::Forward, source)) {
        const CssElement* origin = nullptr;
        if (options.styleNode)
          for (const auto& span : projection_.spans())
            if (span.displayStart <= start && start < span.displayEnd)
              origin = theme.cssLinkInSourceRange(*options.styleNode, span.sourceStart + styleSourceBase_,
                                                  span.sourceEnd + styleSourceBase_, link.href);
        collect(start, start + link.length, source + styleSourceBase_, link.href, origin);
      }
    }
  }
  // Retain every semantic ancestor, including Markdown wrappers and alert
  // title containers. A tag alone cannot distinguish two nested span origins.
  QVector<LinkRun> subjects;
  for (const auto& run : elementRuns_) {
    auto* origin = theme.cssInlineElement(*options.styleNode, run.sourceStart + styleSourceBase_);
    for (int i = 0; origin && i < run.ancestorDepth; ++i)
      origin = origin->parent;
    if (!origin)
      continue;
    QString href;
    for (auto* ancestor = origin; ancestor; ancestor = ancestor->parent)
      if (ancestor->tag == "a") {
        href = ancestor->attributes.value("href");
        break;
      }
    subjects.push_back({origin, run.start, run.end, run.sourceStart + styleSourceBase_, href, run.ancestorDepth,
                        run.parent, run.style->font});
  }
  for (const auto& link : links)
    if (!link.origin ||
        std::none_of(subjects.cbegin(), subjects.cend(), [&](const auto& run) { return run.origin == link.origin; }))
      subjects.push_back(link);

  struct Event {
    PseudoElementRule rule;
    const CssElement* origin;
    qsizetype anchor, position, visible, hostStart, hostEnd;
    int order;
    bool before, semanticImage = false;
    QString href;
    QFont font;
    QString fill;
    int hostRun = -1;
  };
  QVector<Event> events;
  const auto images = options.styleNode ? theme.cssInlineImages(*options.styleNode) : QVector<CssInlineImage>{};
  for (const auto& image : images)
    inlineImageFingerprints_.push_back(qHashMulti(size_t(0),
                                                  theme.computedStyleForElement(*image.element).fingerprint(),
                                                  theme.computedStyleForElement(*image.path).fingerprint()));
  QVector<QVector<int>> children(subjects.size());
  for (int i = 0; i < subjects.size(); ++i)
    if (subjects[i].parent >= 0)
      children[subjects[i].parent].push_back(i);
  int order = 0;
  const auto enqueue = [&](const PseudoElementRule& rule, const LinkRun& run, bool before, bool image = false) {
    if (rule.absolute)
      return;
    const auto anchor = before ? run.start : run.end;
    const auto position = layoutDisplayOffsetForProjectionOffset(anchor, before ? InlineProjectionBias::Forward
                                                                                : InlineProjectionBias::Backward);
    events.push_back({rule, run.origin, anchor, position, visibleOffsetForDisplayOffset(position), run.start, run.end,
                      order++, before, image, run.href, run.font});
  };
  // Traverse the semantic tree in DOM order. Parent ::before precedes child
  // ::before; child ::after precedes parent ::after at a shared boundary.
  std::function<void(int)> visit = [&](int index) {
    const auto& link = subjects[index];
    const auto resolve = [&](const QString& pseudo) {
      std::optional<PseudoElementRule> rule;
      if (link.origin)
        rule = theme.pseudoForElement(*link.origin, pseudo);
      else {
        DocumentStylePrototypes prototypes;
        CssElement origin;
        origin.tag = QStringLiteral("a");
        origin.attributes.insert(QStringLiteral("href"), link.href);
        origin.parent = &prototypes.element(QStringLiteral("p"));
        rule = theme.pseudoForElement(origin, pseudo);
      }
      inlinePseudoFingerprints_.push_back({link.source, link.origin ? link.origin->tag : QStringLiteral("a"), pseudo,
                                           generatedContentFingerprint(rule), link.ancestorDepth});
      const bool before = pseudo == "before";
      if (rule && !rule->absolute) {
        enqueue(*rule, link, before);
        if (index < elementRuns_.size())
          events.last().hostRun = index;
      }
    };
    resolve(QStringLiteral("before"));
    for (const auto& image : images) {
      if (image.element->parent != link.origin)
        continue;
      auto computed = theme.computedStyleForElement(*image.element);
      computed.properties_.insert(QStringLiteral("content"), QStringLiteral("''"));
      PseudoElementRule rule;
      rule.present = true;
      rule.host = image.element->tag;
      rule.computed = std::make_shared<const CssComputedStyle>(std::move(computed));
      rule.svgData = image.svg;
      auto run = link;
      run.origin = image.element;
      enqueue(rule, run, true, true);
      events.last().fill = theme.computedStyleForElement(*image.path).resolvedValue("fill");
      events.last().hostRun = index;
    }
    for (const int child : children[index])
      visit(child);
    resolve(QStringLiteral("after"));
  };
  for (int i = 0; i < subjects.size(); ++i)
    if (subjects[i].parent < 0)
      visit(i);
  // All positions refer to the unmodified layout. Insert backwards, including
  // equal-position events, without asking a mutated offset map for a boundary.
  std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
    return a.position != b.position ? a.position > b.position : a.order > b.order;
  });
  for (const auto& event : events) {
    const auto previous = generatedPseudoAtoms_.size();
    add(&event.rule, {}, event.before, event.anchor, event.visible, event.href, event.origin, event.hostStart,
        event.hostEnd, event.position, &event.font);
    if (generatedPseudoAtoms_.size() > previous)
      generatedPseudoAtoms_.last().hostRun = event.hostRun;
    if (event.semanticImage && generatedPseudoAtoms_.size() > previous) {
      auto& atom = generatedPseudoAtoms_.last();
      atom.semanticImage = true;
      const auto& resources = event.rule.computed->imageResources;
      if (resources)
        atom.imageResource =
            resources->load("data:image/svg+xml;base64," + QString::fromLatin1(event.rule.svgData.toBase64()));
      const auto fill = event.fill;
      atom.semanticFillCurrentColor = fill.compare("currentColor", Qt::CaseInsensitive) == 0;
      atom.rule.color = atom.semanticFillCurrentColor ? atom.box.style.paint.color : cssColor(fill);
      if (fill == "none")
        atom.rule.color = Qt::transparent;
    }
  }
  add(options.pseudoBefore ? &*options.pseudoBefore : nullptr, options.pseudoBeforeText, true, 0, 0);
  add(options.pseudoAfter ? &*options.pseudoAfter : nullptr, options.pseudoAfterText, false,
      projection_.displayText().size(), projection_.visibleText().size());
}

void InlineLayout::buildHtmlFormatSpans(const RenderTheme& theme, qreal width) {
  htmlFormatSpans_.clear();
  htmlInlineBoxRuns_.clear();
  const auto& data = projection_.htmlFormatData();
  for (const auto& hd : data) {
    if (!hd.atomicHtml.isEmpty())
      continue;
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
        if (previousBoxId == fs.inlineBoxId && !htmlInlineBoxRuns_.isEmpty() &&
            htmlInlineBoxRuns_.back().end == hs.layoutStart) {
          htmlInlineBoxRuns_.back().end = hs.layoutEnd;
        } else {
          const auto& style = fs.inlineBoxStyle;
          htmlInlineBoxRuns_.push_back(
              {hs.layoutStart, hs.layoutEnd,
               LayoutBox::place(style.key, style, theme.usedBoxForStyle(style, width), {}, fs.font)});
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
      rebuiltDisplayMap.push_back(
          DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }

    const QString tex = texForInlineMathSpan(inlines, span);
    if (tex.isEmpty()) {
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(
          DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }
    // Projection retains both delimiters, including for synthesized AST nodes.
    // $$ uses display-style operators/fractions while remaining in paragraph flow.
    const bool displayMode = span.contentSourceStart - span.sourceStart == 2;
    auto layout = std::make_shared<math::MathLayoutResult>(
        mathRenderer_.render(tex, theme, displayMode, qMax<qreal>(1.0, width)));
    if (!layout->valid()) {
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(
          DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
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
    rebuiltDisplayMap.push_back(
        DisplayOffsetMapEntry{span.displayStart, span.displayEnd, atom.displayStart, atom.displayEnd});
    mathAtoms_.push_back(std::move(atom));
  }
  if (!rebuiltDisplay.isEmpty()) {
    displayMap_.assign(std::move(rebuiltDisplay), std::move(rebuiltMap), std::move(rebuiltDisplayMap));
  }
}

QString InlineLayout::texForInlineMathSpan(const QVector<InlineNode>& inlines, const InlineProjectionSpan& span) const {
  const QString source = projection_.sourceText();
  if (span.contentSourceStart >= 0 && span.contentSourceEnd >= span.contentSourceStart &&
      span.contentSourceEnd <= source.size()) {
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

void InlineLayout::buildImageAtoms(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width,
                                   const QString& documentPath, QHash<QString, std::shared_ptr<void>>* resources) {
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
      const QString spanText = previous.valid
                                   ? displayMap_.text().mid(previous.start, previous.end - previous.start)
                                   : projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      for (auto& atom : mathAtoms_)
        if (previous.valid && atom.displayStart == previous.start) {
          atom.displayStart = displayStart;
          atom.displayEnd = rebuiltDisplay.size();
        }
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(
          DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }

    const bool collapsed = !isImageSourceRevealed(projection_.spans(), span);

    const QString srcUrl = span.href;
    if (srcUrl.isEmpty()) {
      const QString spanText = projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(
          DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
      continue;
    }

    const QUrl resolved = resolvedUrlForDocumentResource(srcUrl, documentPath);
    const QString resourceKey = resolved.toString(QUrl::FullyEncoded);
    imageResources_.append(ImageLoader::instance().retain(resourceKey));
    if (resources)
      resources->insert(resourceKey, imageResources_.last());
    const bool isRemote = resolved.scheme() == QLatin1String("http") || resolved.scheme() == QLatin1String("https");
    QImage image = ImageLoader::instance().image(resourceKey);

    if (image.isNull()) {
      // Image not yet available — show a placeholder icon inline.
      const bool isLoading = isRemote && ImageLoader::instance().isPending(resourceKey);
      constexpr qreal kPlaceholderSize = 24.0;
      QImage placeholder = isLoading ? image_placeholder::loading(QSizeF(kPlaceholderSize, kPlaceholderSize))
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
        rebuiltDisplayMap.push_back(
            DisplayOffsetMapEntry{span.displayStart, span.displayEnd, atom.displayStart, atom.displayEnd});
        imageAtoms_.push_back(std::move(atom));
        continue;
      }

      // Fallback: keep the alt text as-is (placeholder render failed or cursor is on the image)
      const QString spanText = projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(
          DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});
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
      item.naturalSize = image.deviceIndependentSize() * zoom * theme.zoomPercent() / 100.0;
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
      rebuiltDisplayMap.push_back(
          DisplayOffsetMapEntry{span.displayStart, span.displayEnd, atom.displayStart, atom.displayEnd});
      imageAtoms_.push_back(std::move(atom));
    } else {
      // Active (cursor on image): show source text as-is, add block preview below.
      const QString spanText = projectedDisplay.mid(span.displayStart, span.displayEnd - span.displayStart);
      const qsizetype displayStart = rebuiltDisplay.size();
      rebuiltDisplay += spanText;
      rebuiltMap.push_back(OffsetMapEntry{displayStart, rebuiltDisplay.size(), span.visibleStart, span.visibleEnd});
      rebuiltDisplayMap.push_back(
          DisplayOffsetMapEntry{span.displayStart, span.displayEnd, displayStart, rebuiltDisplay.size()});

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
    displayMap_.assign(std::move(rebuiltDisplay), std::move(rebuiltMap), std::move(rebuiltDisplayMap));
  }
}

void InlineLayout::buildHtmlAtoms(qreal width, const QFont& font, const QString& documentPath) {
  html::HtmlRenderer renderer;
  for (auto it = projection_.htmlFormatData().crbegin(); it != projection_.htmlFormatData().crend(); ++it) {
    const auto& data = *it;
    if (data.atomicHtml.isEmpty())
      continue;
    for (const auto& span : projection_.spans()) {
      if (span.kind != InlineSpanKind::HtmlContent || span.displayStart != data.displayStart)
        continue;
      const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
      if (!range.valid || range.end <= range.start)
        continue;
      auto layout = std::make_shared<html::HtmlLayoutResult>(renderer.render(
          data.atomicHtml, font.pointSizeF(), width, QFileInfo(documentPath).absolutePath(), data.palette));
      if (!layout->valid() || !layout->root() || layout->root()->children().empty())
        continue;
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
              if (c == quote)
                quote = {};
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
            if (p < data.atomicHtml.size() && data.atomicHtml[p] == QLatin1Char('\n'))
              ++p;
            text = "\n";
            literal = false;
          }
          decoded += text;
          for (qsizetype n = 0; n < text.size(); ++n)
            decodedSource += atom.sourceStart + start + (literal ? n : 0);
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
        for (const auto& nested : child.children())
          self(self, *nested, visible);
      };
      mapText(mapText, *atom.layout->root(), true);
      const qsizetype delta = 1 - (range.end - range.start);
      displayMap_.collapse(range, QChar(0xfffc));
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

QString InlineLayout::shapingText() const {
  QString shapingText = layoutTextForDisplayText(displayMap_.text());
  // Tab-indent projection markers use U+200B too, but generated object breaks
  // must remain zero-width breaks rather than becoming indentation NBSPs.
  for (const auto& atom : generatedPseudoAtoms_)
    if (!atom.block && atom.kind != GeneratedPseudoAtom::Kind::Text) {
      shapingText[atom.displayEnd] = QChar(0x200b);
      shapingText[atom.displayStart - 1] = QChar(0x200b);
    }
  return shapingText;
}

void InlineLayout::buildTextLayout(const RenderTheme& theme, qreal width, const QFont& baseFont) {
  diag::ScopedPerfProbe perf("inline.textLayout", inlineBuildPerf());
  QString shapingText = this->shapingText();
  auto formats = [&] {
    diag::ScopedPerfProbe perf("inline.textFormats", inlineBuildPerf());
    return textLayoutFormats(theme, baseFont, shapingText);
  }();

  int insertAt = -1;
  if (!preeditText_.isEmpty() && preeditInsertAtSourceOffset_ >= 0) {
    qsizetype displayOffset = -1;
    if (layoutDisplayOffsetForSourceOffset(preeditInsertAtSourceOffset_, InlineProjectionBias::Forward, displayOffset))
      insertAt = qBound(0, static_cast<int>(displayOffset), static_cast<int>(shapingText.size()));
  }
  composition_ = composeInlineText(std::move(shapingText), std::move(formats), insertAt, preeditText_, preeditFormats_,
                                   baseTextColorOverride_.isValid() ? baseTextColorOverride_ : theme.textColor());
  const auto& composedFormats = composition_.formats;

  textLayout_ =
      std::make_unique<TextLayout>(composition_.text.isEmpty() ? QStringLiteral(" ") : composition_.text, baseFont);
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
  textLayout_->setFormats(composedFormats);
  // Pre-compute the runs a `:hover { color }` should recolour (only when the
  // element actually declares a hover colour). Derived from the same `formats`
  // the layout draws, so it stays in lock-step with span colouring.
  if (hoverTextColor_.isValid() || focusTextColor_.isValid()) {
    computeHoverRecolourRanges(composedFormats, theme);
  } else {
    hoverRecolourRanges_.clear();
  }

  struct LineRun {
    int start, end;
    QFont font;
    qreal lineHeight;
    qreal shift;
  };
  QVector<LineRun> lineRuns;
  for (const auto& span : projection_.spans()) {
    if (span.kind == InlineSpanKind::Atom || span.kind == InlineSpanKind::HtmlContent)
      continue;
    const auto range = layoutDisplayRangeForProjectionRange(span.displayStart, span.displayEnd);
    if (!range.valid || range.end <= range.start)
      continue;
    QFont font = span.type == InlineType::Code ? codeStyle_->font : baseFont;
    if (span.bold)
      font.setWeight(QFont::Bold);
    if (span.italic)
      font.setItalic(true);
    qreal multiplier = lineHeightMultiplier_;
    if (const auto snapshot = spanStyles_.value(span.displayStart); snapshot && span.kind == InlineSpanKind::Text) {
      font = snapshot->font;
      multiplier = snapshot->style.text.lineHeight;
    }
    lineRuns.push_back({toLayoutOffset(range.start), toLayoutOffset(range.end), font,
                        multiplier > 0 ? cssLineHeightPx(font.pointSizeF(), multiplier) : 0,
                        scriptBaselineShift(span.superscript ? QTextCharFormat::AlignSuperScript
                                            : span.subscript ? QTextCharFormat::AlignSubScript
                                                             : QTextCharFormat::AlignNormal,
                                            baseFont)});
  }
  for (const auto& span : htmlFormatSpans_) {
    QFont font = span.fontSet ? span.font : baseFont;
    if (span.fontSize > 0)
      font.setPointSizeF(span.fontSize);
    lineRuns.push_back({toLayoutOffset(span.layoutStart), toLayoutOffset(span.layoutEnd), font, span.lineHeight,
                        scriptBaselineShift(span.verticalAlignment, baseFont)});
  }
  for (const auto& atom : generatedPseudoAtoms_) {
    if (atom.kind == GeneratedPseudoAtom::Kind::Text) {
      lineRuns.push_back({toLayoutOffset(static_cast<int>(atom.displayStart)),
                          toLayoutOffset(static_cast<int>(atom.displayEnd)), atom.font,
                          cssLineHeightPx(atom.font.pointSizeF(), atom.box.style.text.lineHeight), false});
    }
  }
  std::sort(lineRuns.begin(), lineRuns.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
  qsizetype firstRun = 0;
  const qreal lineWidth = qMax<qreal>(1.0, width);
  qreal height = 0.0;
  qreal maxWidth = 0.0;
  diag::ScopedPerfProbe wrapPerf("inline.wrapAndMetrics", inlineBuildPerf());
  textLayout_->beginLayout();
  while (true) {
    TextLine line = textLayout_->createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(lineWidth);
    InlineLineBox lineBox(
        baseFont, lineHeightMultiplier_ > 0 ? cssLineHeightPx(baseFont.pointSizeF(), lineHeightMultiplier_) : 0);
    const int lineStart = line.textStart();
    const int lineEnd = lineStart + line.textLength();
    while (firstRun < lineRuns.size() && lineRuns[firstRun].end <= lineStart)
      ++firstRun;
    for (qsizetype i = firstRun; i < lineRuns.size() && lineRuns[i].start < lineEnd; ++i) {
      if (lineRuns[i].end <= lineStart)
        continue;
      lineBox.includeShiftedText(lineRuns[i].font, lineRuns[i].lineHeight, lineRuns[i].shift);
    }
    for (const auto& atom : htmlAtoms_) {
      const auto start = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (start >= lineStart && start < lineEnd)
        lineBox.includeAtomic(atom.baseline, atom.crop.height(), atom.margin);
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
    const GeneratedPseudoAtom* blockAtom = nullptr;
    qreal topAlignedHeight = 0, bottomAlignedHeight = 0;
    for (const GeneratedPseudoAtom& atom : generatedPseudoAtoms_) {
      const auto start = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (atom.kind != GeneratedPseudoAtom::Kind::Text && start >= lineStart && start < lineEnd) {
        if (atom.block) {
          blockAtom = &atom;
          continue;
        }
        const auto align = atom.rule.computed ? atom.rule.computed->resolvedValue("vertical-align") : QString();
        const auto margin = atom.box.usedBox.margin;
        if (align == "top")
          topAlignedHeight = qMax(topAlignedHeight, atom.height + margin.top() + margin.bottom());
        else if (align == "bottom")
          bottomAlignedHeight = qMax(bottomAlignedHeight, atom.height + margin.top() + margin.bottom());
        else if (align == "middle")
          lineBox.includeAtomic((atom.height + TextFontMetrics(baseFont).xHeight()) / 2, atom.height, margin);
        else
          lineBox.includeAtomic(atom.baseline, atom.height, margin);
      }
    }
    // Top/bottom atoms establish the minimum enclosing line height. Let the
    // tallest establish its edge first; otherwise the shorter atom can move
    // the text baseline unnecessarily before the tallest expands the line.
    if (bottomAlignedHeight > topAlignedHeight) {
      lineBox.includeBottomAligned(bottomAlignedHeight);
      lineBox.includeTopAligned(topAlignedHeight);
    } else {
      lineBox.includeTopAligned(topAlignedHeight);
      lineBox.includeBottomAligned(bottomAlignedHeight);
    }
    qreal allocatedHeight;
    if (blockAtom) {
      allocatedHeight =
          blockAtom->height + blockAtom->box.usedBox.margin.top() + blockAtom->box.usedBox.margin.bottom();
      line.setPosition({0, height});
    } else {
      allocatedHeight = lineBox.placeLine(line, height);
    }
    lineBoxes_.push_back(QRectF(0, height, lineWidth, allocatedHeight));
    for (auto& atom : htmlAtoms_) {
      const auto start = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (start >= lineStart && start < lineEnd)
        atom.rect =
            QRectF(QPointF(line.cursorToX(start) + atom.margin.left(), line.y() + line.ascent() - atom.baseline),
                   atom.crop.size());
    }
    for (auto& atom : generatedPseudoAtoms_) {
      const auto start = toLayoutOffset(static_cast<int>(atom.displayStart));
      if (atom.kind != GeneratedPseudoAtom::Kind::Text && start >= lineStart && start < lineEnd) {
        const bool topAligned = atom.rule.computed && atom.rule.computed->resolvedValue("vertical-align") == "top";
        const bool bottomAligned =
            atom.rule.computed && atom.rule.computed->resolvedValue("vertical-align") == "bottom";
        const bool middleAligned =
            atom.rule.computed && atom.rule.computed->resolvedValue("vertical-align") == "middle";
        atom.baseline = middleAligned ? (atom.height + TextFontMetrics(baseFont).xHeight()) / 2 : atom.height;
        atom.rect = QRectF(line.cursorToX(start) + atom.marginLeft,
                           topAligned      ? height + atom.box.usedBox.margin.top()
                           : bottomAligned ? height + allocatedHeight - atom.height - atom.box.usedBox.margin.bottom()
                                           : line.y() + line.ascent() - atom.baseline,
                           atom.width, atom.height);
        if (atom.block) {
          qreal x = atom.marginLeft;
          const qreal spare = qMax<qreal>(0, lineWidth - atom.width - atom.marginLeft - atom.marginRight);
          if (atom.box.usedBox.marginLeftAuto)
            x += spare / (atom.box.usedBox.marginRightAuto ? 2 : 1);
          atom.rect.moveTopLeft({x, height + atom.box.usedBox.margin.top()});
        }
        atom.box = LayoutBox::place(atom.box.hostKey, atom.box.style, atom.box.usedBox, atom.rect, atom.font);
        GeneratedContentStyle generated;
        generated.rule = atom.rule;
        atom.iconRect = generatedIconRect(generated, atom.box.contentBox, zoomScale_);
      }
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
    previewHeight_ += kPreviewSpacing; // leading gap: paintImagePreview starts one spacing below the text
  }
  size_ = QSizeF(qMin(lineWidth, qMax<qreal>(maxWidth, 1.0)), height + previewHeight_);
}

void InlineLayout::computeHoverRecolourRanges(const QVector<QTextLayout::FormatRange>& formats,
                                              const RenderTheme& theme) {
  hoverRecolourRanges_.clear();
  const int n = displayMap_.text().size();
  if (n <= 0) {
    return;
  }
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
    if (!fg.isValid()) {
      continue;
    }
    const int start = qBound(0, fr.start, n);
    const int end = qBound(0, fr.start + fr.length, n);
    for (int i = start; i < end; ++i) {
      effective[i] = fg;
    }
  }
  int i = 0;
  while (i < n) {
    if (effective[i] != baseRunColor_) {
      ++i;
      continue;
    }
    const int start = i;
    while (i < n && effective[i] == baseRunColor_) {
      ++i;
    }
    hoverRecolourRanges_.append({start, i});
  }
}

void InlineLayout::paintTextShadow(QPainter& painter, QPointF origin, const TextShadow& textShadow,
                                   const QVector<QRectF>& rects,
                                   const QVector<QTextLayout::FormatRange>& formats) const {
  if (textLayout_)
    shadowPainter_.paint(painter, *textLayout_, origin, textShadow, zoomScale_, rects, formats);
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
      const TextLine line = textLayout_->lineAt(i);
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
        if (!current.isNull())
          atom.image = current;
      }
}

QRectF InlineLayout::imageAtomRect(const ImageAtom& atom) const {
  if (!textLayout_ || !atom.loaded || atom.image.isNull())
    return {};
  const int start = toLayoutOffset(static_cast<int>(atom.displayStart));
  const auto line = textLayout_->lineForTextPosition(start);
  if (!line.isValid())
    return {};
  return {line.cursorToX(start), line.y() + line.ascent() - atom.displaySize.height(), atom.displaySize.width(),
          atom.displaySize.height()};
}

void InlineLayout::paintTextLayoutImageAtoms(QPainter& painter, QPointF origin) const {
  for (const ImageAtom& atom : imageAtoms_) {
    const auto rect = imageAtomRect(atom);
    if (rect.isEmpty()) {
      continue;
    }
    const auto targetRect = rect.translated(origin);
    if (atom.cssBox)
      paintLayoutBox(painter, *atom.cssBox, targetRect.topLeft());
    painter.drawImage(atom.cssBox ? atom.cssBox->contentBox.translated(targetRect.topLeft()) : targetRect, atom.image,
                      QRectF(atom.image.rect()));
  }
}

int InlineLayout::generatedPseudoState(const GeneratedPseudoAtom& atom, QPainter& painter, QPointF origin,
                                       const CssPaintContext& context, qreal hoverPhase, qreal focusPhase,
                                       qsizetype focusSourceOffset) const {
  bool over = hoverPhase > .5;
  if (atom.hostRangeStart >= 0) {
    over = false;
    const auto range = layoutDisplayRangeForProjectionRange(atom.hostRangeStart, atom.hostRangeEnd);
    if (context.pointer) {
      const auto pointer = painter.worldTransform().inverted().map(*context.pointer);
      if (range.valid)
        for (const auto& rect : selectionRectsForDisplayOffsets(range.start, range.end))
          over = over || rect.translated(origin).contains(pointer);
      if (atom.kind == GeneratedPseudoAtom::Kind::Text) {
        for (const int index : atom.fragments)
          over = over || inlineBoxes_[index].borderBox.translated(origin).contains(pointer);
      } else
        over = over || atom.box.borderBox.translated(origin).contains(pointer);
    }
  }
  const bool focus = atom.hostSourceStart >= 0
                         ? focusSourceOffset >= atom.hostSourceStart && focusSourceOffset <= atom.hostSourceEnd
                         : focusPhase > .5;
  return (over ? 1 : 0) | (focus ? 2 : 0) | (over && context.pressed ? 4 : 0);
}

void InlineLayout::paintGeneratedPseudoAtoms(QPainter& painter, QPointF origin, CssPaintContext* context,
                                             const QString& identity, qreal hoverPhase, qreal focusPhase,
                                             qsizetype focusSourceOffset) const {
  diag::ScopedPerfProbe pseudoPerf("paint.generatedPseudos", inlinePaintPerf());
  if (!textLayout_)
    return;
  const auto paintIcon = [&](QPainter& target, const GeneratedPseudoAtom& atom, const QColor& color) {
    diag::ScopedPerfProbe iconPerf("paint.pseudoIcon", inlinePaintPerf());
    if (atom.semanticImage && atom.imageResource) {
      const auto rect = atom.iconRect.translated(origin);
      const qreal dpr = target.device() ? target.device()->devicePixelRatioF() : 1;
      auto raster = atom.imageResource->raster({qCeil(rect.width() * dpr), qCeil(rect.height() * dpr)});
      if (raster.isNull())
        return;
      QPainter tint(&raster);
      tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
      tint.fillRect(raster.rect(), atom.semanticFillCurrentColor ? color : atom.rule.color);
      tint.end();
      target.drawImage(rect, raster);
    } else {
      DecorationPainter::paintIcon(target, atom.rule.svgData, atom.iconRect.translated(origin), color,
                                   atom.rule.svgFromMask);
    }
  };
  for (const GeneratedPseudoAtom& atom : generatedPseudoAtoms_) {
    if (atom.kind == GeneratedPseudoAtom::Kind::Text)
      continue;
    const int start = toLayoutOffset(static_cast<int>(atom.displayStart));
    for (int i = 0; i < textLayout_->lineCount(); ++i) {
      const TextLine line = textLayout_->lineAt(i);
      if (!line.isValid())
        continue;
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      if (start < lineStart || start >= lineEnd)
        continue;
      auto style = atom.box.style;
      if (context && atom.box.paintRecipe) {
        const int state =
            generatedPseudoState(atom, painter, origin, *context, hoverPhase, focusPhase, focusSourceOffset);
        style = context->sample(*atom.box.paintRecipe, identity + "/pseudo/" + QString::number(atom.rangeStart), state);
      }
      if (context && atom.semanticFillCurrentColor) {
        for (int parent = atom.hostRun; parent >= 0; parent = elementRuns_[parent].parent) {
          const auto& run = elementRuns_[parent];
          if (!run.style->paintRecipe || atom.box.style.paint.color != run.style->style.paint.color)
            continue;
          const auto inherited = context->sample(
              *run.style->paintRecipe, identity + "/inline/" + run.identity,
              generatedPseudoState(atom, painter, origin, *context, hoverPhase, focusPhase, focusSourceOffset));
          if (inherited.paint.color != run.style->style.paint.color) {
            style.paint.color = inherited.paint.color;
            break;
          }
        }
      }
      paintCssElement(
          painter, atom.box.borderBox.translated(origin), atom.box.visualOverflow.translated(origin), style.paint,
          zoomScale_, context,
          [&](QPainter& target, const ThemeElementPaintStyle& paint) {
            auto unfiltered = style;
            unfiltered.paint = paint;
            const auto tint = paint.backgroundColor;
            if (atom.rule.svgFromMask)
              unfiltered.paint.backgroundColor = {};
            paintLayoutBox(target, atom.box, origin, &unfiltered);
            if (atom.kind == GeneratedPseudoAtom::Kind::Icon) {
              target.save();
              target.setClipRect(atom.box.contentBox.translated(origin), Qt::IntersectClip);
              paintIcon(target, atom, atom.rule.svgFromMask ? tint.isValid() ? tint : atom.rule.maskTint : paint.color);
              target.restore();
            }
          },
          &atom.box);
      break;
    }
  }
}

QVector<QRectF> InlineLayout::generatedPseudoRects() const {
  QVector<QRectF> result;
  for (const auto& atom : generatedPseudoAtoms_) {
    if (atom.kind == GeneratedPseudoAtom::Kind::Text)
      result += selectionRectsForDisplayOffsets(atom.displayStart, atom.displayEnd);
    else if (atom.box.borderBox.isValid())
      result.push_back(atom.box.borderBox);
  }
  return result;
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
    if (atom.cssBox)
      paintLayoutBox(painter, *atom.cssBox, targetRect.topLeft());
    painter.drawImage(atom.cssBox ? atom.cssBox->contentBox.translated(targetRect.topLeft()) : targetRect, atom.image,
                      QRectF(atom.image.rect()));
    y += atom.displaySize.height() + kPreviewSpacing;
  }
}

QVector<QTextLayout::FormatRange> InlineLayout::textLayoutFormats(const RenderTheme& theme, const QFont& baseFont,
                                                                  const QString& shapingText) const {
  InlineFormatInput input{projection_,
                          displayMap_,
                          shapingText,
                          theme,
                          baseFont,
                          *codeStyle_,
                          spanStyles_,
                          htmlFormatSpans_,
                          inlineSpacers_,
                          generatedPseudoFormats_,
                          isMisspelled_,
                          baseTextColorOverride_,
                          {}};
  for (const auto& atom : mathAtoms_) {
    if (atom.layout && atom.layout->valid() && atom.displayEnd > atom.displayStart)
      input.objects.push_back(
          {atom.displayStart, atom.displayEnd - atom.displayStart, atom.layout->size.width(), kInlineMathPlaceholder});
  }
  for (const auto& atom : imageAtoms_) {
    if (atom.loaded && atom.displayEnd > atom.displayStart)
      input.objects.push_back(
          {atom.displayStart, atom.displayEnd - atom.displayStart, atom.displaySize.width(), kImagePlaceholder});
  }
  for (const auto& atom : htmlAtoms_)
    input.objects.push_back({atom.displayStart, 1, atom.crop.width() + atom.margin.left() + atom.margin.right(),
                             QChar(0xfffc), false, true});
  return buildInlineTextFormats(input);
}

qsizetype InlineLayout::visibleOffsetForDisplayOffset(qsizetype displayOffset) const {
  if (displayMap_.empty()) {
    return qBound<qsizetype>(0, displayOffset, projection_.visibleText().size());
  }
  displayOffset = qBound<qsizetype>(0, displayOffset, displayMap_.text().size());
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
  return displayMap_.visibleOffsetForDisplayOffset(displayOffset);
}

qsizetype InlineLayout::displayOffsetForVisibleOffset(qsizetype visibleOffset) const {
  if (displayMap_.empty()) {
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
  return displayMap_.displayOffsetForVisibleOffset(visibleOffset);
}

qsizetype InlineLayout::projectionDisplayOffsetForLayoutOffset(qsizetype layoutOffset,
                                                               InlineProjectionBias bias) const {
  return displayMap_.projectionDisplayOffsetForLayoutOffset(layoutOffset, bias);
}

qsizetype InlineLayout::layoutDisplayOffsetForProjectionOffset(qsizetype projectionOffset,
                                                               InlineProjectionBias bias) const {
  return displayMap_.layoutDisplayOffsetForProjectionOffset(projectionOffset, bias);
}

bool InlineLayout::layoutDisplayOffsetForSourceOffset(qsizetype sourceOffset, InlineProjectionBias bias,
                                                      qsizetype& layoutOffset) const {
  qsizetype projectionOffset = -1;
  if (!projection_.displayOffsetForSourceOffset(sourceOffset, bias, projectionOffset)) {
    return false;
  }
  layoutOffset = layoutDisplayOffsetForProjectionOffset(projectionOffset, bias);
  return true;
}

InlineLayout::DisplayOffsetRange InlineLayout::layoutDisplayRangeForProjectionRange(qsizetype projectionStart,
                                                                                    qsizetype projectionEnd) const {
  return displayMap_.layoutDisplayRangeForProjectionRange(projectionStart, projectionEnd);
}

qsizetype InlineLayout::textLayoutDisplayOffsetForPoint(QPointF localPos) const {
  return textLayoutHitForPoint(localPos).displayOffset;
}

InlineLayout::TextLayoutPointHit InlineLayout::textLayoutHitForPoint(QPointF localPos) const {
  TextLayoutPointHit hit;
  if (!textLayout_) {
    return hit;
  }

  TextLine targetLine;
  for (int i = 0; i < textLayout_->lineCount(); ++i) {
    const TextLine line = textLayout_->lineAt(i);
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
    const TextLine line = targetLine;
    const int lineStart = line.textStart();
    const int lineTextEnd = lineStart + line.textLength();
    int lineEnd = lineTextEnd;
    // Qt includes the line separator in textLength(). A click after the ink
    // belongs before that separator; including it places the caret at the
    // next line's start once its geometry is rebuilt from the source offset.
    if (lineEnd > lineStart && textLayout_->text().at(lineEnd - 1) == QChar::LineSeparator) {
      --lineEnd;
    }
    // Stay on the preceding text side of a hard break's hidden syntax (two
    // spaces or a backslash), so typing does not consume its markers.
    const auto lineEndBias = lineEnd < lineTextEnd ? InlineProjectionBias::Backward : InlineProjectionBias::Forward;
    if (lineEnd <= lineStart) {
      hit.displayOffset = composition_.toDisplayOffset(lineStart);
      hit.bias = InlineProjectionBias::Backward;
      hit.cursorRect = QRectF(line.cursorToX(lineStart), line.y(), 1.0, line.height());
      return hit;
    }

    if (localPos.x() <= line.cursorToX(lineStart)) {
      hit.displayOffset = composition_.toDisplayOffset(lineStart);
      hit.cursorRect = QRectF(line.cursorToX(lineStart), line.y(), 1.0, line.height());
      return hit;
    }
    if (localPos.x() >= line.cursorToX(lineEnd)) {
      hit.displayOffset = composition_.toDisplayOffset(lineEnd);
      hit.bias = lineEndBias;
      hit.cursorRect = QRectF(line.cursorToX(lineEnd), line.y(), 1.0, line.height());
      return hit;
    }

    for (int offset = lineStart; offset < lineEnd; ++offset) {
      const qreal left = line.cursorToX(offset);
      const qreal right = line.cursorToX(offset + 1);
      const qreal midpoint = (left + right) / 2.0;
      if (localPos.x() < midpoint) {
        hit.displayOffset = composition_.toDisplayOffset(offset);
        hit.bias = InlineProjectionBias::Backward;
        hit.cursorRect = QRectF(line.cursorToX(offset), line.y(), 1.0, line.height());
        return hit;
      }
      if (localPos.x() < right) {
        hit.displayOffset = composition_.toDisplayOffset(offset + 1);
        hit.bias = offset + 1 == lineEnd ? lineEndBias : InlineProjectionBias::Forward;
        hit.cursorRect = QRectF(line.cursorToX(offset + 1), line.y(), 1.0, line.height());
        return hit;
      }
    }
    hit.displayOffset = composition_.toDisplayOffset(lineEnd);
    hit.bias = lineEndBias;
    hit.cursorRect = QRectF(line.cursorToX(lineEnd), line.y(), 1.0, line.height());
    return hit;
  }

  hit.displayOffset = displayMap_.text().size();
  hit.bias = InlineProjectionBias::Forward;
  hit.cursorRect = textLayoutCursorRectForDisplayOffset(hit.displayOffset);
  return hit;
}

QRectF InlineLayout::textLayoutCursorRectForDisplayOffset(qsizetype displayOffset) const {
  if (!textLayout_) {
    return {};
  }
  return textLayout_->cursorRect(toLayoutOffset(int(qBound<qsizetype>(0, displayOffset, displayMap_.text().size()))));
}

} // namespace muffin
