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
class CssComputedStyle;
class CssThemeSheet;
class NodeCssElementBuilder;  // sparse live-tree adapter for structural selectors
struct CssElement;
struct CssEnvironment;
struct ThemeElementStyle;

class RenderTheme {
public:
 RenderTheme();
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
 // Resolve retained responsive CSS for the editor viewport. Returns true when
 // the environment changed and existing block estimates need rebuilding.
 bool updateForViewport(qreal width, qreal height = 768.0);
 bool hasDocumentCss() const { return bool(sourceSheet_); }
 bool pageUsesBorderBox() const { return pageBorderBox_; }
 std::shared_ptr<const CssThemeSheet> documentStyleSheet() const { return sourceSheet_; }
 CssEnvironment documentCssEnvironment() const;
 const QHash<QString, QString>& fontAliases() const { return fontAliases_; }

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
 QMarginsF blockMargin(BlockType type, int headingLevel = 0, const MarkdownNode* node = nullptr, qreal containingWidth = -1) const;
 bool hasBlockMargin(BlockType type, int headingLevel = 0, const MarkdownNode* node = nullptr) const;
 // Space reserved by generated inline heading content.
 qreal headingBeforeAdvance(int level) const;

 QFont paragraphFont() const;
 // Live nodes always resolve against the sparse document tree. Prototypes
 // (no node) are used for estimates and preview tokens.
 QFont textFontForElement(const QString& key, const MarkdownNode* node = nullptr) const;
 QColor textColorForElement(const QString& key, const MarkdownNode* node = nullptr) const;
 qreal lineHeightMultiplierForElement(const QString& key, const MarkdownNode* node = nullptr) const;
 qreal wordSpacingForElement(const QString& key, const MarkdownNode* node = nullptr) const;
 Qt::Alignment textAlignmentForElement(const QString& key, const MarkdownNode* node = nullptr) const;
 // CSS text-transform for an element (0=none, 1=upper, 2=lower, 3=capitalize).
 int textTransformForElement(const QString& key, const MarkdownNode* node = nullptr) const;
 // CSS `text-shadow` for an element (present=false ⇒ none).
 TextShadow textShadowForElement(const QString& key, const MarkdownNode* node = nullptr) const;
 // Structural-selector support: true when the theme needs the live tree. The
 // builder passes the node into the getters above so structural rules resolve.
 bool hasStructuralRules() const { return hasStructuralRules_; }
 // Node-resolved immutable snapshots, cached by generation and object identity.
 QFont fontForStyle(const ThemeElementStyle& style, QFont fallback) const;
 ThemeElementStyle inlineStyleForNode(const MarkdownNode& owner, qsizetype sourceOffset) const;
 const CssElement* cssElementForNode(const MarkdownNode& node) const;
 const CssElement* cssParentForInlineHtml(const MarkdownNode& owner, qsizetype sourceOffset) const;
 const ThemeElementStyle* elementStyleForNode(const MarkdownNode& node, const QString& key) const;
 // Drop resolved styles and the sparse live-tree adapter. Recreating the adapter
 // is proportional to the selector paths queried, so every edit gets fresh data.
 void invalidateDocumentStyles() const;
 QFont headingFont(int level) const;
 QFont codeFont() const;
 QFont inlineCodeFont() const;
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
 // Phase 5: CSS `del { color }` (deleted-text colour). Invalid → inherit prose.
 QColor delColor() const;
 // Phase 3c: HTML <kbd> keycap box (CSS-driven). Invalid/zero getters signal
 // the caller to fall back to the legacy light/dark keycap heuristic.
 // Phase 4: per-side bottom border (phycat's 3D keycap). Zero/invalid → uniform.
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
 ThemeElementBoxStyle elementBoxStyle(const QString& key, const MarkdownNode* node = nullptr, qreal containingWidth = -1) const;
 ThemeElementBoxStyle usedBoxForStyle(const ThemeElementStyle& style, qreal containingWidth = -1) const;
 // Per-heading text colour from the theme; invalid when the theme doesn't set
 // one (caller falls back to textColor). level is 1..6.
 // P5 cheap decorations the paint engine can already draw; invalid → unused.
 QColor headingAccentColor() const;  // h2 left accent bar
 QColor blockquoteBackgroundColor() const;
 QColor codeHighlightColor(CodeHighlightRole role) const;

 // ::before/::after decorations keyed by host ("h2","blockquote","#write",…).
 // Painters filter the vector for the host they are drawing. Empty for themes
 // that declare none.
 const ThemeDecorations& decorations() const;

private:
 explicit RenderTheme(std::nullptr_t) {}
 qreal scaled(qreal value) const;
 qreal scaledFont(qreal value) const;

 int zoomPercent_ = 100;
 int fontSizePx_ = 16;
 bool serifBody_ = false;

 // Math retains its semantic font fallback; document text uses computed styles.
 QString mathFont_;
 qreal mathSizePt_ = 0.0;
 bool linkUnderlined_ = true;
 int linkUnderlineStyle_ = -1;
 QColor linkUnderlineColor_;
 bool linkOverline_ = false;
 QColor delColor_;
 QColor viewportBackgroundColor_;
 QColor pageBackgroundColor_;
 QColor pageBorderColor_;
 qreal pageBorderWidth_ = 0.0;
 qreal pageBorderRadius_ = 0.0;
 QMarginsF pagePadding_;
 QMarginsF pageMargin_;
 bool pageMarginExplicit_ = false;  // theme declared a #write margin (or padding→default 0)
 qreal pageMaxWidth_ = 0.0;
 bool pageBorderBox_ = false;
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
 // Structural capability controls edit dependency checks, not style resolution.
 bool hasStructuralRules_ = false;
 bool hasNthOfType_ = false;  // some selector reads typeIndex (:*-of-type); else skip typeCounts
 qreal bodyFontPx_ = 16.0;
 std::shared_ptr<const CssThemeSheet> sourceSheet_;
 QString sourceId_;
 QHash<QString, QString> fontAliases_;
 qreal cssViewportWidth_ = -1.0;
 qreal cssViewportHeight_ = -1.0;
 std::shared_ptr<CssComputedStyleEngine> styleEngine_;
 // Sparse live-node adapter for structural selector navigation. Shared (not unique)
 // so RenderTheme remains copyable; reset with the computed style cache on edits.
 mutable std::shared_ptr<NodeCssElementBuilder> styleTree_;
 mutable QHash<QString, std::shared_ptr<const ThemeElementStyle>> nodeStyleCache_;
 mutable quint64 styleGeneration_ = 1;
 // Prototype (load-time) QFont per element key, used by the Lazy estimate path so it doesn't
 // rebuild a QFont per block (~80µs each on Windows). Cleared by invalidateDocumentStyles() since
 // zoom/fontSize can change between rebuilds.
 mutable QHash<QString, QFont> prototypeFontCache_;
 ThemeElementStyle projectStyle(const QString& key, const CssComputedStyle& computed) const;
 mutable QHash<QString, ThemeElementStyle> projectedStyleCache_;
 mutable QHash<QString, QFont> computedFontCache_;

 qreal headingBeforeAdvance_[6] = {};
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
 QColor codeBlockBackground_;   // invalid → codeBackgroundColor()
 QColor headingAccentColor_;    // invalid → no h2 accent bar
 QColor blockquoteBackground_;  // invalid → no quote fill
};

}  // namespace muffin
