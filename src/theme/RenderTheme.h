#pragma once

#include "document/MarkdownTypes.h"
#include "render/CodeHighlight.h"
#include "theme/ThemeDefinition.h"

#include <QColor>
#include <QFont>
#include <QMarginsF>
#include <Qt>

namespace muffin {

class RenderTheme {
public:
  static RenderTheme defaultTheme(int zoomPercent = 100);
  static RenderTheme github(int zoomPercent = 100);
  static RenderTheme newsprint(int zoomPercent = 100);
  static RenderTheme night(int zoomPercent = 100);
  static RenderTheme pixyll(int zoomPercent = 100);
  static RenderTheme whitey(int zoomPercent = 100);

  // Build a theme from a unified ThemeDefinition. The five built-in definitions
  // reproduce github()/newsprint()/night()/pixyll()/whitey() exactly, so custom
  // themes (loaded from JSON by ThemeManager) drive the editor through the same
  // path as the built-ins — no separate code path for custom themes.
  static RenderTheme fromDefinition(const ThemeDefinition& definition, int zoomPercent = 100, int fontSizePx = 16);

  int zoomPercent() const;
  void setZoomPercent(int percent);
  int fontSizePx() const;
  void setFontSizePx(int px);

  qreal pageWidth() const;
  qreal topMargin() const;
  qreal bottomMargin() const;
  qreal blockSpacing() const;
  qreal listIndent() const;
  qreal blockQuoteIndent() const;
  QColor viewportBackgroundColor() const;
  QColor pageBackgroundColor() const;
  QColor pageBorderColor() const;
  qreal pageBorderWidth() const;
  qreal pageBorderRadius() const;
  QMarginsF pagePadding() const;
  QMarginsF pageMargin() const;
  QColor pageShadowColor() const;
  qreal pageShadowBlur() const;
  qreal pageShadowOffsetY() const;
  QMarginsF blockMargin(BlockType type, int headingLevel = 0) const;
  QMarginsF headingPadding(int level) const;
  // Phase 4a: CSS `blockquote` box (flow-aware). blockquoteBoxThemed() is the
  // switch from the legacy accent-bar + 16px indent to the CSS-driven path.
  bool blockquoteBoxThemed() const;
  QMarginsF blockquotePadding() const;
  qreal blockquoteBorderWidth() const;
  QColor blockquoteBorderColor() const;
  qreal blockquoteBorderRadius() const;
  // Phase 4b: CSS `pre`/`.md-fences` box. codePadding() returns the CSS padding
  // when themed, else the legacy scaled(12/10). codeBlockBorderRadius() is 0
  // (sharp) when not themed.
  bool codeBlockBoxThemed() const;
  qreal codeBlockBorderRadius() const;
  // Phase 4c: CSS `td`/`th` padding + `table` radius. tableCellPadding() returns
  // the CSS padding when themed, else legacy scaled(12/6).
  bool tableBoxThemed() const;
  qreal tableBorderRadius() const;
  QColor headingBorderBottomColor(int level) const;
  qreal headingBorderBottomWidth(int level) const;
  QColor headingBorderLeftColor(int level) const;
  qreal headingBorderLeftWidth(int level) const;
  // True when the heading declares `width: fit-content` (or max/min-content) —
  // its background/decoration box shrinks to the text. Paint-only; level 1..6.
  bool headingFitContent(int level) const;
  // Px reserved left of the heading text for an inline `::before` marker
  // (h4/h5/h6). The block rect stays full width; only the text origin shifts.
  qreal headingBeforeAdvance(int level) const;
  qreal lineHeightMultiplier(BlockType type, int headingLevel = 0) const;
  Qt::Alignment textAlignment(BlockType type, int headingLevel = 0) const;

  QFont paragraphFont() const;
  QFont headingFont(int level) const;
  QFont codeFont() const;
  qreal codeLineHeight() const;
  QFont mathFont() const;

  QColor backgroundColor() const;
  QColor textColor() const;
  QColor mutedTextColor() const;
  QColor linkColor() const;
  // Phase 3: `a` underline follows CSS `text-decoration` (false for `none`).
  bool linkUnderlined() const;
  // Phase 3b: inline-code chip geometry from CSS `code` (zoom-scaled). Paint-only.
  qreal inlineCodePaddingH() const;
  qreal inlineCodePaddingV() const;
  qreal inlineCodeBorderRadius() const;
  qreal inlineCodeBorderWidth() const;
  QColor inlineCodeTextColor() const;
  // Phase 3c: HTML <kbd> keycap box (CSS-driven). Invalid/zero getters signal
  // the caller to fall back to the legacy light/dark keycap heuristic.
  QColor kbdBackgroundColor() const;
  QColor kbdTextColor() const;
  QString kbdFont() const;
  qreal kbdPaddingH() const;
  qreal kbdPaddingV() const;
  qreal kbdBorderRadius() const;
  QColor kbdBorderColor() const;
  qreal kbdBorderWidth() const;
  QColor kbdShadowColor() const;
  QColor codeBackgroundColor() const;
  // Fenced code-block fill. Distinct from inline code when the theme sets it;
  // otherwise identical to codeBackgroundColor() (preserving legacy behaviour).
  QColor codeBlockBackgroundColor() const;
  // Background wash behind `==highlighted==` inline text (Pandoc-style marker).
  QColor highlightBackgroundColor() const;
  QColor codeBorderColor() const;
  QColor quoteBorderColor() const;
  // GitHub-style alert accent per kind (Note/Tip/Important/Warning/Caution). A fixed palette that
  // reads on both light and dark themes; the card tint is derived from it with low alpha at the
  // paint site so it adapts to the page background automatically.
  QColor alertAccent(AlertKind kind) const;
  QColor tableBorderColor() const;
  QColor tableHeaderBackgroundColor() const;
  QColor tableAlternateBackgroundColor() const;
  QColor selectionColor() const;
  QColor spellCheckColor() const;
  // Per-heading text colour from the theme; invalid when the theme doesn't set
  // one (caller falls back to textColor). level is 1..6.
  QColor headingColor(int level) const;
  // P5 cheap decorations the paint engine can already draw; invalid → unused.
  QColor headingAccentColor() const;     // h2 left accent bar
  QColor blockquoteBackgroundColor() const;
  QColor codeHighlightColor(CodeHighlightRole role) const;

  // ::before/::after decorations keyed by host ("h2","blockquote","#write",…).
  // Painters filter the vector for the host they are drawing. Empty for themes
  // that declare none.
  const ThemeDecorations& decorations() const;

  QMarginsF codePadding() const;
  QMarginsF tableCellPadding() const;

private:
  qreal scaled(qreal value) const;
  qreal scaledFont(qreal value) const;

  int zoomPercent_ = 100;
  int fontSizePx_ = 16;
  bool serifBody_ = false;

  // Theme-supplied typography (CSS themes). Empty/zero → fall back to the
  // per-platform families / built-in sizes below, so built-in themes are unchanged.
  QString bodyFont_, headingFont_, codeFont_, mathFont_;
  qreal bodySizePt_ = 0.0;
  qreal lineHeight_ = 0.0;
  qreal letterSpacing_ = 0.0;
  qreal codeLetterSpacing_ = 0.0;
  bool linkUnderlined_ = true;
  qreal inlineCodePaddingH_ = 3.0;
  qreal inlineCodePaddingV_ = 1.0;
  qreal inlineCodeBorderRadius_ = 3.0;
  qreal inlineCodeBorderWidth_ = 1.0;
  QColor inlineCodeTextColor_;
  QColor kbdBackground_, kbdTextColor_, kbdBorderColor_, kbdShadowColor_;
  QString kbdFont_;
  qreal kbdPaddingH_ = 0.0;
  qreal kbdPaddingV_ = 0.0;
  qreal kbdBorderRadius_ = 0.0;
  qreal kbdBorderWidth_ = 0.0;
  qreal headingSizePt_[6] = {};
  qreal headingLineHeight_[6] = {};
  QColor headingColor_[6];
  Qt::Alignment bodyAlignment_;
  Qt::Alignment headingAlignment_[6];
  int headingFontWeight_[6] = {};
  bool headingFontWeightSet_[6] = {};
  bool headingItalic_[6] = {};
  bool headingItalicSet_[6] = {};

  QColor viewportBackgroundColor_;
  QColor pageBackgroundColor_;
  QColor pageBorderColor_;
  qreal pageBorderWidth_ = 0.0;
  qreal pageBorderRadius_ = 0.0;
  QMarginsF pagePadding_;
  QMarginsF pageMargin_;
  bool pageMarginExplicit_ = false;  // theme declared a #write margin (or padding→default 0)
  qreal pageMaxWidth_ = 0.0;
  QColor pageShadowColor_;
  qreal pageShadowBlur_ = 0.0;
  qreal pageShadowOffsetY_ = 0.0;

  // ::before/::after decorations (gradients, SVG icons, text content, texture
  // masks) keyed by host. Empty for themes that declare none (all built-ins).
  ThemeDecorations decorations_;

  QMarginsF paragraphMargin_;
  QMarginsF headingMargin_[6];
  QMarginsF headingPadding_[6];
  QColor headingBorderBottomColor_[6];
  qreal headingBorderBottomWidth_[6] = {};
  QColor headingBorderLeftColor_[6];
  qreal headingBorderLeftWidth_[6] = {};
  bool headingFitContent_[6] = {};
  qreal headingBeforeAdvance_[6] = {};
  QMarginsF blockquoteMargin_;
  QMarginsF blockquotePadding_;
  qreal blockquoteBorderWidth_ = 0.0;
  QColor blockquoteBorderColor_;
  qreal blockquoteBorderRadius_ = 0.0;
  bool blockquoteBoxThemed_ = false;
  QMarginsF codeBlockPadding_;
  qreal codeBlockBorderRadius_ = 0.0;
  bool codeBlockBoxThemed_ = false;
  QMarginsF tableCellPadding_;
  qreal tableBorderRadius_ = 0.0;
  bool tableBoxThemed_ = false;
  QMarginsF codeBlockMargin_;
  QMarginsF tableMargin_;
  QMarginsF listMargin_;
  qreal listPaddingLeft_ = 0.0;

  QColor backgroundColor_ = QColor(QStringLiteral("#ffffff"));
  QColor textColor_ = QColor(QStringLiteral("#202124"));
  QColor mutedTextColor_ = QColor(QStringLiteral("#57606a"));
  QColor linkColor_ = QColor(QStringLiteral("#4183c4"));
  QColor codeBackgroundColor_ = QColor(QStringLiteral("#f6f8fa"));
  QColor highlightBackgroundColor_ = QColor(QStringLiteral("#fff8c5"));
  QColor codeBorderColor_ = QColor(QStringLiteral("#e5e7eb"));
  QColor quoteBorderColor_ = QColor(QStringLiteral("#d0d7de"));
  QColor tableBorderColor_ = QColor(QStringLiteral("#dfe2e5"));
  QColor tableHeaderBackgroundColor_ = QColor(QStringLiteral("#edf4ff"));
  QColor tableAlternateBackgroundColor_ = QColor(QStringLiteral("#f6f8fa"));
  QColor selectionColor_ = QColor(QStringLiteral("#d7e8ff"));
  QColor spellCheckColor_ = QColor(QStringLiteral("#d1242f"));
  QColor codeBlockBackground_;        // invalid → codeBackgroundColor()
  QColor headingAccentColor_;         // invalid → no h2 accent bar
  QColor blockquoteBackground_;       // invalid → no quote fill
};

}  // namespace muffin
