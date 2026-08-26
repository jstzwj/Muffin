#pragma once

#include "document/InlineNode.h"
#include "projection/InlineProjection.h"
#include "editor/CursorPosition.h"
#include "html/HtmlTextMeasurer.h"
#include "math/MathRenderer.h"
#include "math/MathRenderNode.h"
#include "theme/RenderTheme.h"

#include <QColor>
#include <QImage>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QStringView>
#include <QTextLayout>
#include <QString>
#include <Qt>
#include <QVector>

#include <functional>
#include <memory>

class QPainter;

namespace muffin {

class InlineLayout {
public:
  struct BuildOptions {
    InlineProjectionState projectionState;
    qsizetype sourceBase = -1;
    qsizetype pendingPrefixLength = 0;
    // When set, misspelled prose words within Text spans receive a SpellCheck underline.
    // Supplied by the builder from the SpellChecker; InlineLayout itself stays free of any
    // spell-check dependency.
    std::function<bool(QStringView)> isMisspelled;
    // Display-only smart punctuation (Convert on Rendering); no-op by default.
    SmartPunctRenderOptions smartPunct;
    // markdown/renderEmoji (default on): decode `:shortcode:` emoji at display
    // time via the projection's decode-span path. Supplied by the builder from
    // the same setting so the painted projection and the editing projection agree.
    bool renderEmoji = true;
    // Base text colour override (e.g. a theme's per-heading colour). Invalid →
    // the layout falls back to theme.textColor() for plain Text runs. Span-level
    // colours (links, code, highlight) still take precedence per span.
    QColor baseTextColor;
    // CSS `:hover { color }` target for the owning element (e.g. a heading).
    // Invalid → no hover recolour. Only the runs that INHERIT the element colour
    // are recoloured at paint time; links/code/del/kbd keep their own colours.
    QColor hoverTextColor;
    // CSS `:focus { color }` target — same mechanism as hoverTextColor, blended
    // on top of the hover target (focus first, then hover) so the two orthogonal
    // states compose. Invalid → no focus recolour.
    QColor focusTextColor;
    qreal lineHeightMultiplier = 0.0;
    qreal wordSpacing = 0.0;
    Qt::Alignment alignment;
    // CSS text-transform (uppercase/lowercase/capitalize) applied to the projected
    // display text. Length-preserving per-code-point mapping ⇒ offsets stay exact.
    TextTransform textTransform = TextTransform::None;
    // CSS `text-shadow` on the element; present=false ⇒ none.
    TextShadow textShadow;
    // Render a single '\n' soft break as a line break instead of joining it
    // into the paragraph (CommonMark). Defaults off so standalone/test layouts stay CommonMark.
    bool breakOnSingleNewline = false;
    // Active IME composition to splice into the laid-out text at the caret, so the in-progress
    // glyphs push following text right (and wrap) instead of overlapping it. Set only on the caret
    // block. All offsets are PREEDIT-relative (over [0, preeditText.length()]); buildTextLayout
    // shifts them by the caret's display offset when splicing into layoutText_.
    QString preeditText;
    QVector<QTextLayout::FormatRange> preeditFormats;
    int preeditCursor = -1;                       // composition caret within the preedit (-1 = none)
    qsizetype preeditInsertAtSourceOffset = -1;   // caret CONTENT-LOCAL source offset in this block
  };

  InlineLayout() = default;
  InlineLayout(const InlineLayout&) = delete;
  InlineLayout& operator=(const InlineLayout&) = delete;
  InlineLayout(InlineLayout&&) noexcept = default;
  InlineLayout& operator=(InlineLayout&&) noexcept = default;

  void build(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width, const QFont& baseFont);
  void build(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width, const QFont& baseFont, BuildOptions options);
  void build(const QVector<InlineNode>& inlines, QString sourceText, const RenderTheme& theme, qreal width, const QFont& baseFont,
             BuildOptions options);

  QSizeF size() const;
  qreal height() const;
  QRectF visualTextBounds() const;
  // Baseline Y of the first text line, relative to the layout origin. Includes
  // the line-height centering offset (line.y()), so callers drawing decoration
  // that must align with the first line (placeholder text, list markers) land on
  // the same baseline as the painted text and the caret — which otherwise drift
  // apart under a large theme line-height.
  qreal firstLineBaselineY() const;
  // Paint the laid-out text + atoms at `origin`. When a hover and/or focus colour
  // was supplied at build and the corresponding phase > 0, the heading's OWN text
  // runs are recoloured toward a blended target (focus applied first, then hover)
  // — CSS `:hover`/`:focus { color }` animated by the HoverAnimator/FocusAnimator.
  // Implemented by passing foreground-only `selection`s to QTextLayout::draw (a
  // draw-time override), NOT a format swap: setFormats() after endLayout() is
  // broken in this Qt build. Only the runs that inherit the element colour are
  // recoloured; links/code/del/kbd keep their own colours, so the recolour never
  // bleeds into styled spans.
  void paint(QPainter& painter, QPointF origin, qreal hoverPhase = 0.0, qreal focusPhase = 0.0) const;
  qsizetype hitTestTextOffset(QPointF localPos) const;
  qsizetype hitTestSourceOffset(QPointF localPos) const;
  QRectF hitTestCursorRect(QPointF localPos) const;
  QString linkHrefAtLocalPos(QPointF localPos) const;
  QString imageSrcAtLocalPos(QPointF localPos) const;
  QRectF cursorRect(qsizetype textOffset) const;
  QRectF cursorRectForSourceOffset(qsizetype sourceOffset) const;
  // Rect of the composition caret within the spliced preedit (empty when no preedit is active).
  // Origin is the layout's top-left, same as cursorRect. EditorView draws the caret from this.
  QRectF preeditCursorRect(QPointF origin) const;
  // True when this layout has an IME preedit spliced into its laid-out text (the composition is
  // rendered by the normal text paint, so EditorView draws only the caret — not overlay glyphs).
  bool hasPreedit() const { return preeditSpliceLength_ > 0; }
  // Painted rects (document space, origin-relative) of the inline math atoms — the same rects
  // paintTextLayoutMathAtoms draws. Exposed so tests can verify atoms shift with the spliced preedit.
  QVector<QRectF> mathAtomRects(QPointF origin) const;
  int visualLineCount() const;
  int visualLineIndexForTextOffset(qsizetype textOffset) const;
  int visualLineIndexForSourceOffset(qsizetype sourceOffset) const;
  QRectF visualLineRect(int lineIndex) const;
  qsizetype textOffsetAtVisualLineX(int lineIndex, qreal localX) const;
  qsizetype sourceOffsetAtVisualLineX(int lineIndex, qreal localX) const;
  QVector<QRectF> selectionRects(qsizetype startOffset, qsizetype endOffset) const;
  // contiguousFill variant: when fillRight >= 0, a visual line whose text the selection covers to
  // the line's end is emitted as a full-width band [line.x(), fillRight] instead of its glyph run —
  // except the boundary line when selectionEndsInBlock (the line holding the selection's focus in
  // document direction keeps the Typora-style partial glyph run).
  QVector<QRectF> selectionRects(qsizetype startOffset, qsizetype endOffset, qreal fillRight, bool selectionEndsInBlock) const;
  QVector<QRectF> selectionRectsForSourceOffsets(qsizetype startSourceOffset, qsizetype endSourceOffset) const;

  QString plainText() const;
  QString displayText() const;
  QString visibleText() const;
  // True when the block has no visible content: empty text and no rendered
  // image. Distinct from plainText().isEmpty(), which is also empty for a
  // paragraph holding only an image with blank alt text (real content).
  bool isEmpty() const { return isEmpty_; }
  int mathAtomCount() const;
  QVector<QTextLayout::FormatRange> debugTextFormats(const RenderTheme& theme, const QFont& baseFont) const;

private:
  struct OffsetMapEntry {
    qsizetype displayStart = 0;
    qsizetype displayEnd = 0;
    qsizetype visibleStart = 0;
    qsizetype visibleEnd = 0;
  };

  struct MathAtom {
    qsizetype displayStart = 0;
    qsizetype displayEnd = 0;
    qsizetype sourceStart = 0;
    qsizetype sourceEnd = 0;
    qsizetype contentSourceStart = 0;
    qsizetype contentSourceEnd = 0;
    qsizetype visibleStart = 0;
    qsizetype visibleEnd = 0;
    std::shared_ptr<math::MathLayoutResult> layout;
  };

  struct ImageAtom {
    qsizetype displayStart = 0;
    qsizetype displayEnd = 0;
    qsizetype sourceStart = 0;
    qsizetype sourceEnd = 0;
    qsizetype visibleStart = 0;
    qsizetype visibleEnd = 0;
    QString srcUrl;
    QSizeF displaySize;
    QImage image;
    bool loaded = false;
  };

  struct DisplayOffsetMapEntry {
    qsizetype projectionStart = 0;
    qsizetype projectionEnd = 0;
    qsizetype layoutStart = 0;
    qsizetype layoutEnd = 0;
  };

  struct DisplayOffsetRange {
    qsizetype start = 0;
    qsizetype end = 0;
    bool valid = false;
  };

  // Phase 3c: a flow-reserved placeholder for `a::before` generated content. The
  // placeholder char lives in displayText_ (so QTextLayout measures/wraps it),
  // is painted transparent and widened to the icon size via letter spacing, and
  // maps to the link run's start source offset (zero-width, non-editable) — the
  // same trick math/image atoms use.
  struct LinkBeforeAtom {
    qsizetype displayStart = 0;
    qsizetype displayEnd = 0;
    qsizetype visibleStart = 0;  // link run's first visible offset (caret target)
  };

  struct HtmlFormatSpan {
    int layoutStart = 0;
    int layoutEnd = 0;
    bool bold = false;
    bool italic = false;
    bool monospace = false;
    html::HtmlTextDecoration decoration = html::HtmlTextDecoration::None;
    QColor color;
    QColor backgroundColor;
    qreal fontSize = 0;
    QTextCharFormat::VerticalAlignment verticalAlignment = QTextCharFormat::AlignNormal;
    bool keyboard = false;
    QString href;
  };

  void buildOffsetMapFromProjection();
  void buildLinkBeforeAtoms();
  void buildHtmlFormatSpans();
  void buildMathAtoms(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width);
  void buildImageAtoms(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width);
  QString texForInlineMathSpan(const QVector<InlineNode>& inlines, const InlineProjectionSpan& span) const;
  void buildTextLayout(const RenderTheme& theme, qreal width, const QFont& baseFont);
  // Map a displayText_-space offset to a layoutText_ offset, accounting for the spliced preedit
  // (offsets strictly after the splice point advance by the preedit length; the splice point itself
  // and everything before are unchanged). Identity when no preedit is spliced.
  int toLayoutOffset(int displayOffset) const;
  // From the layout's own format ranges, collect the contiguous display-offset
  // runs whose effective foreground is the element's base colour (i.e. they
  // inherit it) — exactly the runs a `:hover { color }` should recolour. Spans
  // with their own foreground (links/code/del/kbd/HTML-colour) and transparent
  // atom placeholders are excluded by construction. Computed once at build.
  void computeHoverRecolourRanges(const QVector<QTextLayout::FormatRange>& formats, const RenderTheme& theme);
  void paintTextLayoutCodeSpans(QPainter& painter, QPointF origin) const;
  void paintTextLayoutInlineDecorations(QPainter& painter, QPointF origin) const;
  void paintTextLayoutHtmlBackgrounds(QPainter& painter, QPointF origin) const;
  void paintTextLayoutHtmlKeyboardSpans(QPainter& painter, QPointF origin) const;
  // Offscreen-render the text as an alpha mask, recolour to the shadow colour,
  // blur, and composite at origin + offset (behind the crisp text).
  void paintTextShadow(QPainter& painter, QPointF origin) const;
  void paintTextLayoutMathAtoms(QPainter& painter, QPointF origin) const;
  void paintTextLayoutImageAtoms(QPainter& painter, QPointF origin) const;
  void paintImagePreview(QPainter& painter, QPointF origin) const;
  QVector<QTextLayout::FormatRange> textLayoutFormats(const RenderTheme& theme, const QFont& baseFont) const;
  qsizetype visibleOffsetForDisplayOffset(qsizetype displayOffset) const;
  qsizetype displayOffsetForVisibleOffset(qsizetype visibleOffset) const;
  qsizetype projectionDisplayOffsetForLayoutOffset(qsizetype layoutOffset, InlineProjectionBias bias) const;
  qsizetype layoutDisplayOffsetForProjectionOffset(qsizetype projectionOffset, InlineProjectionBias bias) const;
  bool layoutDisplayOffsetForSourceOffset(qsizetype sourceOffset, InlineProjectionBias bias, qsizetype& layoutOffset) const;
  DisplayOffsetRange layoutDisplayRangeForProjectionRange(qsizetype projectionStart, qsizetype projectionEnd) const;
  struct TextLayoutPointHit;
  TextLayoutPointHit textLayoutHitForPoint(QPointF localPos) const;
  qsizetype textLayoutDisplayOffsetForPoint(QPointF localPos) const;
  QRectF textLayoutCursorRectForDisplayOffset(qsizetype displayOffset) const;
  QVector<QRectF> selectionRectsForDisplayOffsets(qsizetype startDisplayOffset, qsizetype endDisplayOffset) const;
  QVector<QRectF> selectionRectsForDisplayOffsets(
      qsizetype startDisplayOffset, qsizetype endDisplayOffset, qreal fillRight, bool selectionEndsInBlock) const;

  std::unique_ptr<QTextLayout> textLayout_;
  QSizeF size_;
  QColor textLayoutCodeBackgroundColor_;
  QColor baseTextColorOverride_;  // invalid → theme.textColor() for plain runs
  QColor hoverTextColor_;         // invalid → no hover recolour (CSS :hover colour)
  QColor focusTextColor_;         // invalid → no focus recolour (CSS :focus colour)
  QColor baseRunColor_;           // the element base colour the own-text runs render in
  QVector<QPair<int, int>> hoverRecolourRanges_;  // display-offset runs that inherit the base colour (used by both hover and focus recolour)
  qreal lineHeightMultiplier_ = 0.0;
  qreal wordSpacing_ = 0.0;
  Qt::Alignment alignment_;
  TextShadow textShadow_;  // present=false ⇒ no shadow
  QColor textLayoutCodeBorderColor_;
  QColor textLayoutCodeTextColor_;
  bool darkTheme_ = false;
  // Phase 3c: CSS-driven <kbd> keycap. Invalid/zero → legacy heuristic below.
  QColor kbdFill_, kbdText_, kbdBorder_, kbdShadow_;
  QString kbdFont_;
  qreal kbdPadH_ = 0.0;
  qreal kbdPadV_ = 0.0;
  qreal kbdRadius_ = 0.0;
  qreal kbdBorderWidth_ = 0.0;
  // Phase 4: per-side bottom border (phycat 3D keycap). Zero/invalid → uniform.
  qreal kbdBorderBottomWidth_ = 0.0;
  QColor kbdBorderBottomColor_;
  // Phase 3b: inline-code box geometry from CSS (defaults reproduce the legacy
  // -3/+6 / radius-3 / 1px chip so built-ins are unchanged).
  qreal codeBoxPaddingH_ = 3.0;
  qreal codeBoxPaddingV_ = 1.0;
  qreal codeBoxRadius_ = 3.0;
  qreal codeBoxBorderWidth_ = 1.0;
  QColor codeBoxShadowColor_;
  qreal codeBoxShadowOffsetX_ = 0.0;
  qreal codeBoxShadowOffsetY_ = 0.0;
  qreal codeBoxShadowBlur_ = 0.0;
  qreal codeBoxShadowSpread_ = 0.0;
  // CSS inline decorations (Phase 3). link ::before icon (mask-tinted SVG) +
  // mark background-image gradient. Empty/None → nothing painted.
  QByteArray linkBeforeIcon_;
  QColor linkBeforeIconTint_;
  bool linkBeforeIconFromMask_ = false;
  QSizeF linkBeforeIconSize_;       // CSS width/height (invalid → 1em)
  qreal linkBeforeIconMarginRight_ = 0.0;  // CSS margin-right gap before the link text
  qreal linkBeforeIconAdvance_ = 0.0;      // reserved flow width (icon + margin)
  qreal linkBeforeIconHeight_ = 0.0;       // paint height (vertical centering)
  QVector<LinkBeforeAtom> linkBeforeAtoms_;
  GradientSpec markGradient_;
  QString plainText_;
  bool isEmpty_ = true;
  QString displayText_;
  QString layoutText_;
  // Active IME preedit spliced into layoutText_ (NOT displayText_ — the projection/offset maps stay
  // pristine). Empty/-1 when no composition is active. Set from BuildOptions in build(); read in
  // buildTextLayout (splice), the decoration painters (toLayoutOffset), and preeditCursorRect.
  QString preeditText_;
  QVector<QTextLayout::FormatRange> preeditFormats_;
  int preeditCursor_ = -1;                 // composition caret within the preedit
  qsizetype preeditInsertAtSourceOffset_ = -1;
  int preeditSpliceInsertAt_ = -1;         // layoutText_ offset where the preedit was spliced
  int preeditSpliceLength_ = 0;            // preeditText_.length() (0 ⇒ no splice)
  QVector<OffsetMapEntry> offsetMap_;
  QVector<MathAtom> mathAtoms_;
  QVector<ImageAtom> imageAtoms_;
  QVector<ImageAtom> previewAtoms_;   // Active images rendered as block preview below text
  qreal previewHeight_ = 0.0;         // Total height of image previews
  QVector<HtmlFormatSpan> htmlFormatSpans_;
  QVector<DisplayOffsetMapEntry> displayOffsetMap_;
  InlineProjection projection_;
  math::MathRenderer mathRenderer_;
  std::function<bool(QStringView)> isMisspelled_;
};

}  // namespace muffin
