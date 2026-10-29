#pragma once

#include "document/MarkdownTypes.h"
#include "document/NodeId.h"
#include "document/TopLevelRangeChange.h"
#include "render/CodeHighlight.h"
#include "theme/ThemeDefinition.h"
#include "theme/CssComputedStyleEngine.h"

#include <QColor>
#include <QFont>
#include <QHash>
#include <QMarginsF>
#include <Qt>
#include <array>

namespace muffin {
namespace diag {
struct MemoryAccounting;
}

class MarkdownNode;
class MarkdownFlow;
class MarkdownDocument;
class CssComputedStyleEngine;
class CssComputedStyle;
class CssThemeSheet;
class NodeCssElementBuilder; // sparse live-tree adapter for structural selectors
class DocumentCounterCache;
struct DocumentCounterStats;
struct CssInlineImage;
struct CssElement;
struct CssEnvironment;
struct ThemeElementStyle;

class RenderTheme {
  friend struct diag::MemoryAccounting;

public:
  RenderTheme();
  std::shared_ptr<const MarkdownFlow> markdownFlow(const MarkdownNode& root) const;
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
  // Fork a document-free prototype, sharing only prepared viewport projections.
  RenderTheme detachedPrototype() const;
  // Carry exact semantic shapes across theme changes; never carries CSS results.
  void reuseDocumentSemantics(const RenderTheme& previous);

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
  // Always update the viewport environment. Return true when theme projections
  // change; false permits translation-only layout when the column width agrees.
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
  QMarginsF blockMargin(BlockType type, int headingLevel = 0, const MarkdownNode* node = nullptr,
                        qreal containingWidth = -1) const;
  bool hasBlockMargin(BlockType type, int headingLevel = 0, const MarkdownNode* node = nullptr) const;

  QFont paragraphFont() const;
  // Live nodes always resolve against the sparse document tree. Prototypes
  // (no node) are used for estimates and preview tokens.
  QFont textFontForElement(const QString& key, const MarkdownNode* node = nullptr) const;
  QColor textColorForElement(const QString& key, const MarkdownNode* node = nullptr) const;
  qreal lineHeightMultiplierForElement(const QString& key, const MarkdownNode* node = nullptr) const;
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
  ThemeElementStyle styleForElement(const CssElement& element) const;
  CssComputedStyle computedStyleForElement(const CssElement& element) const;
  QVector<CssInlineImage> cssInlineImages(const MarkdownNode& owner) const;
  std::shared_ptr<const CssPaintRecipe> paintRecipeForElement(const CssElement& element,
                                                              const QString& pseudo = {}) const;
  const CssElement* cssElementForNode(const MarkdownNode& node) const;
  const CssElement* cssParentForInlineHtml(const MarkdownNode& owner, qsizetype sourceOffset) const;
  const ThemeElementStyle* elementStyleForNode(const MarkdownNode& node, const QString& key) const;
  std::optional<PseudoElementRule> pseudoForNode(const MarkdownNode& node, const QString& pseudo) const;
  std::optional<PseudoElementRule> pseudoForElement(const CssElement& origin, const QString& pseudo,
                                                    CssElementState state = {}) const;
  QHash<NodeId, QPair<QString, QString>> resolveDocumentCounters(const MarkdownDocument& document) const;
  quint64 generatedCounterFingerprint(NodeId node) const { return counterFingerprints_.value(node); }
  bool hasGeneratedCounters() const { return !counterChains_.isEmpty(); }
  DocumentCounterStats documentCounterStats() const;
  struct StyleCacheStats {
    qsizetype elements = 0, nodeStyles = 0, recipes = 0, sharedRecipes = 0;
    CssComputedStyleEngine::CacheStats engine;
  };
  StyleCacheStats styleCacheStats() const;
  void releaseInlineStyles(const QSet<NodeId>& topLevelIds) const;
  const CssElement* cssInlineElement(const MarkdownNode& owner, qsizetype sourceOffset, const QString& tag = {}) const;
  const CssElement* cssLinkInSourceRange(const MarkdownNode& owner, qsizetype start, qsizetype end,
                                         const QString& href) const;
  // Preserve identities/recipes when a precise edit has identical CSS semantics;
  // otherwise invalidate the document style snapshot and its sparse adapter.
  void invalidateDocumentStyles(TopLevelRangeChange change = {}, const MarkdownDocument* document = nullptr) const;
  // Last replacement preserved all CSS-visible topology and attributes.
  bool hasLocalStyleChange() const { return localStyleChange_; }
  // Keep static Markdown snapshots when only embedded HTML's viewport changes.
  void invalidateInlineViewportStyles() const;
  bool hasInlineViewportUnits() const;
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
  QColor linkUnderlineColor() const; // invalid → caller uses the link text colour
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
  ThemeElementBoxStyle elementBoxStyle(const QString& key, const MarkdownNode* node = nullptr,
                                       qreal containingWidth = -1) const;
  ThemeElementBoxStyle usedBoxForStyle(const ThemeElementStyle& style, qreal containingWidth = -1) const;
  // Per-heading text colour from the theme; invalid when the theme doesn't set
  // one (caller falls back to textColor). level is 1..6.
  // P5 cheap decorations the paint engine can already draw; invalid → unused.
  QColor headingAccentColor() const; // h2 left accent bar
  QColor blockquoteBackgroundColor() const;
  QColor codeHighlightColor(CodeHighlightRole role) const;

  // ::before/::after decorations keyed by host ("h2","blockquote","#write",…).
  // Painters filter the vector for the host they are drawing. Empty for themes
  // that declare none.
  const ThemeDecorations& decorations() const;

private:
  static RenderTheme fromDefinitionWithEngine(const ThemeDefinition& definition, int zoomPercent, int fontSizePx,
                                              std::shared_ptr<CssComputedStyleEngine> engine);
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
  bool pageMarginExplicit_ = false; // theme declared a #write margin (or padding→default 0)
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
  bool hasNthOfType_ = false; // some selector reads typeIndex (:*-of-type); else skip typeCounts
  qreal bodyFontPx_ = 16.0;
  std::shared_ptr<const CssThemeSheet> sourceSheet_;
  QString sourceId_;
  QHash<QString, QString> fontAliases_;
  qreal cssViewportWidth_ = -1.0;
  qreal cssViewportHeight_ = -1.0;
  bool viewportUnits_ = false;
  bool percentagePageBox_ = false;
  std::shared_ptr<CssComputedStyleEngine> styleEngine_;
  struct ProjectionCache;
  std::shared_ptr<ProjectionCache> projectionCache_;
  // Sparse live-node adapter for structural selector navigation. Shared (not unique)
  // so RenderTheme remains copyable; reset with the computed style cache on edits.
  mutable std::shared_ptr<NodeCssElementBuilder> styleTree_;
  mutable QHash<QString, std::shared_ptr<const ThemeElementStyle>> nodeStyleCache_;
  // Bounded final inline snapshots. Identity keys contain no AST pointers and
  // are retired with the sparse adapter; theme/environment invalidation clears
  // them together with the computed styles.
  mutable QHash<quint64, ThemeElementStyle> elementStyleCache_;
  mutable QHash<QPair<quint64, quint64>, ThemeElementStyle> inlineStyleCache_;
  mutable QHash<QString, std::optional<PseudoElementRule>> pseudoStyleCache_;
  mutable QHash<QString, QHash<QString, QVector<int>>> counterChains_;
  mutable QHash<NodeId, quint64> counterFingerprints_;
  mutable const MarkdownDocument* counterDocument_ = nullptr;
  mutable quint64 counterRevision_ = 0;
  mutable QHash<NodeId, QPair<QString, QString>> counterText_;
  mutable std::shared_ptr<DocumentCounterCache> counterCache_;
  mutable std::shared_ptr<const DocumentCounterCache> semanticSeed_;
  mutable const MarkdownDocument* semanticDocument_ = nullptr;
  mutable quint64 semanticRevision_ = 0;
  mutable TopLevelRangeChange counterChange_;
  mutable bool localStyleChange_ = false;
  mutable QHash<QString, std::shared_ptr<const CssPaintRecipe>> paintRecipeCache_;
  struct SharedPaintRecipe {
    QString key;
    std::array<CssComputedStyle, 8> states;
    std::shared_ptr<const CssPaintRecipe> recipe;
  };
  mutable QHash<quint64, std::vector<SharedPaintRecipe>> sharedPaintRecipes_;
  mutable qsizetype sharedPaintRecipeCount_ = 0;
  // Prototype (load-time) QFont per element key, used by the Lazy estimate path so it doesn't
  // rebuild a QFont per block (~80µs each on Windows). Cleared by structural invalidation since
  // zoom/fontSize can change between rebuilds.
  mutable QHash<QString, QFont> prototypeFontCache_;
  ThemeElementStyle projectStyle(const QString& key, const CssComputedStyle& computed) const;
  mutable QHash<QString, ThemeElementStyle> projectedStyleCache_;
  mutable QHash<QString, QFont> computedFontCache_;

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
  QColor codeBlockBackground_;  // invalid → codeBackgroundColor()
  QColor headingAccentColor_;   // invalid → no h2 accent bar
  QColor blockquoteBackground_; // invalid → no quote fill
};

} // namespace muffin
