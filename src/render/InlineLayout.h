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
    // Base text colour override (e.g. a theme's per-heading colour). Invalid →
    // the layout falls back to theme.textColor() for plain Text runs. Span-level
    // colours (links, code, highlight) still take precedence per span.
    QColor baseTextColor;
    qreal lineHeightMultiplier = 0.0;
    Qt::Alignment alignment;
    // Render a single '\n' soft break as a line break instead of joining it
    // into the paragraph (CommonMark). Defaults off so standalone/test layouts stay CommonMark.
    bool breakOnSingleNewline = false;
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
  void paint(QPainter& painter, QPointF origin) const;
  qsizetype hitTestTextOffset(QPointF localPos) const;
  qsizetype hitTestSourceOffset(QPointF localPos) const;
  QRectF hitTestCursorRect(QPointF localPos) const;
  QString linkHrefAtLocalPos(QPointF localPos) const;
  QString imageSrcAtLocalPos(QPointF localPos) const;
  QRectF cursorRect(qsizetype textOffset) const;
  QRectF cursorRectForSourceOffset(qsizetype sourceOffset) const;
  QVector<QRectF> selectionRects(qsizetype startOffset, qsizetype endOffset) const;
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
  void paintTextLayoutCodeSpans(QPainter& painter, QPointF origin) const;
  void paintTextLayoutInlineDecorations(QPainter& painter, QPointF origin) const;
  void paintTextLayoutHtmlBackgrounds(QPainter& painter, QPointF origin) const;
  void paintTextLayoutHtmlKeyboardSpans(QPainter& painter, QPointF origin) const;
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

  std::unique_ptr<QTextLayout> textLayout_;
  QSizeF size_;
  QColor textLayoutCodeBackgroundColor_;
  QColor baseTextColorOverride_;  // invalid → theme.textColor() for plain runs
  qreal lineHeightMultiplier_ = 0.0;
  Qt::Alignment alignment_;
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
  // Phase 3b: inline-code box geometry from CSS (defaults reproduce the legacy
  // -3/+6 / radius-3 / 1px chip so built-ins are unchanged).
  qreal codeBoxPaddingH_ = 3.0;
  qreal codeBoxPaddingV_ = 1.0;
  qreal codeBoxRadius_ = 3.0;
  qreal codeBoxBorderWidth_ = 1.0;
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
