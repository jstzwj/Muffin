#pragma once

#include "document/MarkdownTypes.h"
#include "document/NodeId.h"
#include "render/CodeHighlight.h"
#include "theme/ThemeDefinition.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QMarginsF>
#include <Qt>

namespace muffin {

class MarkdownNode;
class CssComputedStyleEngine;
class CssThemeSheet;
class NodeCssElementBuilder;  // sparse live-tree adapter for structural selectors
struct CssElement;
struct ThemeElementStyle;

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
  // User-level page-column override. 0 keeps the active theme's #write
  // max-width, -1 fills the viewport, and a positive value is a CSS-pixel
  // maximum. This is deliberately independent from text size so users can
  // reduce wrapping without shrinking the type.
  int contentWidthPx() const;
  void setContentWidthPx(int px);

  qreal pageWidth() const;
  qreal topMargin() const;
  qreal bottomMargin() const;
  qreal blockSpacing() const;
  qreal listIndent() const;
  qreal listMarkerGap() const;
  // CSS `list-style-type` for a list item: the ordered/unordered list's declared
  // type, falling back to a direct `li` declaration. Empty ⇒ legacy marker.
  QString listStyleTypeForItem(bool ordered) const;
  qreal blockQuoteIndent() const;
  // Nested-list guide line with geometry scaled to the current zoom (colour and
  // the `present` flag pass through unchanged). Invalid when the theme styled no
  // li::before guide; painters should no-op in that case.
  ListGuide listGuide() const;
  QColor viewportBackgroundColor() const;
  QColor pageBackgroundColor() const;
  QColor pageBorderColor() const;
  qreal pageBorderWidth() const;
  qreal pageBorderRadius() const;
  QMarginsF pagePadding() const;
  QMarginsF pageMargin() const;
  QColor pageShadowColor() const;
  qreal pageShadowOffsetX() const;
  qreal pageShadowBlur() const;
  qreal pageShadowOffsetY() const;
  qreal pageShadowSpread() const;
  QMarginsF blockMargin(BlockType type, int headingLevel = 0, const MarkdownNode* node = nullptr) const;
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
  // Element-level getters. When the theme uses structural selectors and `node` is
  // supplied, the node's live position is matched first (so `li:first-child`,
  // `p:has(img)`, `li + li`, … override the base); otherwise the load-time
  // prototype style (keyed by `key`) is used.
  QFont textFontForElement(const QString& key, const MarkdownNode* node = nullptr) const;
  QColor textColorForElement(const QString& key, const MarkdownNode* node = nullptr) const;
  qreal lineHeightMultiplierForElement(const QString& key, BlockType fallbackType, int headingLevel = 0, const MarkdownNode* node = nullptr) const;
  qreal wordSpacingForElement(const QString& key, const MarkdownNode* node = nullptr) const;
  Qt::Alignment textAlignmentForElement(const QString& key, BlockType fallbackType, int headingLevel = 0, const MarkdownNode* node = nullptr) const;
  // CSS text-transform for an element (0=none, 1=upper, 2=lower, 3=capitalize).
  int textTransformForElement(const QString& key, const MarkdownNode* node = nullptr) const;
  // CSS `text-shadow` for an element (present=false ⇒ none).
  TextShadow textShadowForElement(const QString& key, const MarkdownNode* node = nullptr) const;
  // Structural-selector support: true when the theme needs the live tree. The
  // builder passes the node into the getters above so structural rules resolve.
  bool hasStructuralRules() const { return hasStructuralRules_; }
  // Node-resolved element style (cached per rebuild). Returns the precomputed base
  // when the theme has no structural rules.
  const ThemeElementStyle* elementStyleForNode(const MarkdownNode& node, const QString& key) const;
  // Drop resolved styles and the sparse live-tree adapter. Recreating the adapter
  // is proportional to the selector paths queried, so every edit gets fresh data.
  void clearStructuralCache() const;
  // Compatibility hooks for layout rebuild paths; both discard the sparse adapter.
  void dropStructuralBuilder() const;
  void invalidateStructuralSiblingLinks() const;
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
  // CSS `a` text-decoration style/colour/overline. Underline style is a
  // QTextCharFormat::UnderlineStyle; returns -1 when unset (caller applies Single).
  int linkUnderlineStyle() const;
  QColor linkUnderlineColor() const;  // invalid → caller uses the link text colour
  bool linkOverline() const;
  // Phase 3b: inline-code chip geometry from CSS `code` (zoom-scaled). Paint-only.
  qreal inlineCodePaddingH() const;
  qreal inlineCodePaddingV() const;
  qreal inlineCodeBorderRadius() const;
  qreal inlineCodeBorderWidth() const;
  QColor inlineCodeShadowColor() const;
  qreal inlineCodeShadowOffsetX() const;
  qreal inlineCodeShadowOffsetY() const;
  qreal inlineCodeShadowBlur() const;
  qreal inlineCodeShadowSpread() const;
  QColor inlineCodeTextColor() const;
  // Phase 5: CSS `del { color }` (deleted-text colour). Invalid → inherit prose.
  QColor delColor() const;
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
  // Phase 4: per-side bottom border (phycat's 3D keycap). Zero/invalid → uniform.
  qreal kbdBorderBottomWidth() const;
  QColor kbdBorderBottomColor() const;
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
  QColor listMarkerColor() const;
  const ThemeElementStyle* elementStyle(const QString& key) const;
  ThemeElementBoxStyle elementBoxStyle(const QString& key, const MarkdownNode* node = nullptr) const;
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
  qreal mathSizePt_ = 0.0;
  qreal lineHeight_ = 0.0;
  qreal letterSpacing_ = 0.0;
  qreal codeLetterSpacing_ = 0.0;
  bool linkUnderlined_ = true;
  int linkUnderlineStyle_ = -1;
  QColor linkUnderlineColor_;
  bool linkOverline_ = false;
  qreal inlineCodePaddingH_ = 3.0;
  qreal inlineCodePaddingV_ = 1.0;
  qreal inlineCodeBorderRadius_ = 3.0;
  qreal inlineCodeBorderWidth_ = 0.0;
  QColor inlineCodeShadowColor_;
  qreal inlineCodeShadowOffsetX_ = 0.0;
  qreal inlineCodeShadowOffsetY_ = 0.0;
  qreal inlineCodeShadowBlur_ = 0.0;
  qreal inlineCodeShadowSpread_ = 0.0;
  QColor inlineCodeTextColor_;
  QColor delColor_;
  QColor kbdBackground_, kbdTextColor_, kbdBorderColor_, kbdShadowColor_;
  QString kbdFont_;
  qreal kbdPaddingH_ = 0.0;
  qreal kbdPaddingV_ = 0.0;
  qreal kbdBorderRadius_ = 0.0;
  qreal kbdBorderWidth_ = 0.0;
  qreal kbdBorderBottomWidth_ = 0.0;
  QColor kbdBorderBottomColor_;
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
  int contentWidthPx_ = 0;
  QColor pageShadowColor_;
  qreal pageShadowOffsetX_ = 0.0;
  qreal pageShadowBlur_ = 0.0;
  qreal pageShadowOffsetY_ = 0.0;
  qreal pageShadowSpread_ = 0.0;

  // ::before/::after decorations (gradients, SVG icons, text content, texture
  // masks) keyed by host. Empty for themes that declare none (all built-ins).
  ThemeDecorations decorations_;
  std::vector<ThemeElementStyle> elementStyles_;
  // O(1) lookup of elementStyles_ by key, built once alongside the vector in fromDefinition.
  // The vector is the source of truth (returned pointers index into it); this hash mirrors
  // first-occurrence positions so elementStyle() matches the previous linear "first match".
  QHash<QString, qsizetype> elementStyleIndex_;
  QColor listMarkerColor_;
  // Structural-selector layout path. Populated only when the theme declares rules
  // that need the live tree; the per-node cache is mutable (lazily filled, cleared
  // per rebuild) so the const getters can populate it.
  bool hasStructuralRules_ = false;
  bool hasNthOfType_ = false;  // some selector reads typeIndex (:*-of-type); else skip typeCounts
  qreal bodyFontPx_ = 16.0;
  std::shared_ptr<const CssThemeSheet> structuralSheet_;
  std::shared_ptr<CssComputedStyleEngine> structuralEngine_;
  // Sparse live-node adapter for structural selector navigation. Shared (not unique)
  // so RenderTheme remains copyable; reset with the computed style cache on edits.
  mutable std::shared_ptr<NodeCssElementBuilder> structuralBuilder_;
  mutable QHash<NodeId, ThemeElementStyle> nodeStyleCache_;
  // Prototype (load-time) QFont per element key, used by the Lazy estimate path so it doesn't
  // rebuild a QFont per block (~80µs each on Windows). Cleared by clearStructuralCache() since
  // zoom/fontSize can change between rebuilds.
  mutable QHash<QString, QFont> prototypeFontCache_;

  qreal headingBeforeAdvance_[6] = {};
  QMarginsF codeBlockPadding_;
  qreal codeBlockBorderRadius_ = 0.0;
  bool codeBlockBoxThemed_ = false;
  QMarginsF tableCellPadding_;
  qreal tableBorderRadius_ = 0.0;
  bool tableBoxThemed_ = false;
  QMarginsF codeBlockMargin_;
  QMarginsF tableMargin_;
  QMarginsF listMargin_;
  qreal listMarkerGap_ = 0.0;
  QString ulListStyleType_;
  QString olListStyleType_;
  QString liListStyleType_;

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
