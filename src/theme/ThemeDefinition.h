#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMarginsF>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <Qt>

#include <memory>
#include <optional>
#include <vector>

namespace muffin {

class CssThemeSheet;  // forward-decl; full type lives in CssThemeParser.h

// Every colour the UI can theme, in one place. This is the single source of
// truth that the editor (via RenderTheme), the chrome (menu bar, sidebar,
// status bar) and the dialogs (Preferences, …) all read from. Split into two
// groups:
//   • document colours — mirror RenderTheme (the editor / HTML engine palette)
//   • chrome colours  — the window/dialog surface palette, previously
//                       hard-coded per-widget in QSS strings
struct ThemeColors {
  // --- Document / editor (mirror RenderTheme) ---
  QColor background;
  QColor text;
  QColor muted;
  QColor link;
  QColor codeBackground;
  QColor codeBorder;
  QColor quoteBorder;
  QColor tableBorder;
  QColor tableHeaderBackground;
  QColor tableAlternateBackground;
  QColor highlight;
  QColor selection;
  // Fenced code-block fill. Distinct from inline `code` so a CSS theme that
  // gives `pre` a different shade than `code` is honoured. Invalid → codeBackground.
  QColor codeBlockBackground;
  // P5 cheap decorations the paint engine can already draw. Invalid → unused.
  QColor headingAccentColor;    // h2 left accent bar
  QColor blockquoteBackground;  // blockquote fill

  // --- Chrome / dialog (previously hard-coded per-widget) ---
  QColor chromeBackground;  // main window + dialog base background
  QColor chromeText;        // menu bar / dialog primary text
  QColor chromeMuted;       // active secondary text (toolbar, sidebar, scrollbar)
  QColor chromeDisabled;    // disabled/ghosted text + controls (clearly faded)
  QColor surface;           // cards, panels, sidebar, list backgrounds
  QColor canvas;            // tone behind cards/dialog panels (slightly distinct
                            //   from surface for depth; equals chromeBackground
                            //   when a theme doesn't distinguish them)
  QColor border;            // generic chrome border (menus, cards, splitter)
  QColor hover;             // hover background (menu items, list rows, buttons)
  QColor selected;          // selected item background (list rows, checked tabs)
  QColor accent;            // focus / links / checked-indicator

  // Typography: when true, paragraph + heading text renders in a serif family
  // (code/math stay monospace). Models themes whose identity is typography
  // rather than colour — e.g. Pixyll, which is a serif-body theme.
  bool serifBody = false;

  bool isDark = false;
};

// Typography carried by a theme. Every field is optional: when a font family is
// empty RenderTheme falls back to its per-platform stack, and when a size is zero
// it falls back to the built-in size — so a theme that only sets colours (every
// JSON theme today, and stock CSS themes that omit font rules) is unchanged.
// When a family IS set, RenderTheme uses it as the primary family and keeps the
// platform stack as a substitution tail so missing glyphs (CJK, symbols) resolve.
struct ThemeTypography {
  QString bodyFont;       // paragraph + list text
  QString headingFont;    // h1-h6 (empty → bodyFont / platform stack)
  QString codeFont;       // inline + fenced code
  QString mathFont;       // math blocks (empty → platform math stack)

  // Point sizes at the 16px / 100% reference (same scale as RenderTheme's
  // hard-coded sizes). Zero → fall back to the built-in size.
  qreal bodySizePt = 0.0;
  qreal mathSizePt = 0.0;           // `.MathJax { font-size }`; 0 -> built-in
  qreal lineHeight = 0.0;           // unitless CSS line-height multiplier
  qreal headingSizePt[6] = {};      // h1..h6; 0 → fall back
  qreal headingLineHeight[6] = {};  // h1..h6; 0 → lineHeight/default

  // Per-heading text colour. Invalid → fall back to the body text colour.
  QColor headingColor[6];

  // CSS text-align. Empty alignment means unset; headings then inherit the body
  // alignment at RenderTheme time. Stored as Qt alignment because the native
  // renderer feeds it straight into QTextOption.
  Qt::Alignment bodyAlignment;
  Qt::Alignment headingAlignment[6];

  // CSS font-weight / font-style for headings. The explicit flags distinguish
  // `font-weight: normal` from an omitted declaration, so legacy themes can keep
  // their bold heading fallback while CSS themes like Whitey can opt out.
  int headingFontWeight[6] = {};
  bool headingFontWeightSet[6] = {};
  bool headingItalic[6] = {};
  bool headingItalicSet[6] = {};

  // Phase 3 inline CSS. letter-spacing is baked into the theme fonts (Qt supports
  // it natively), so layout/estimate/paint/hit-test stay consistent for free;
  // 0 = unchanged. linkUnderlined drives `a` underline from CSS `text-decoration`
  // (phycat sets `text-decoration:none`); default true preserves built-ins.
  qreal letterSpacing = 0.0;        // body + heading (inherited from #write/body)
  qreal codeLetterSpacing = 0.0;    // inline + fenced code
  QColor inlineCodeTextColor;       // `code { color }`; invalid → inherit prose text
  QColor delColor;                  // `del { color }` (phycat mutes deleted text); invalid → inherit prose
  bool linkUnderlined = true;       // `a { text-decoration }` — false when `none`
  // CSS `a { text-decoration: <line> <style> <color> }`. Underline style maps to a
  // QTextCharFormat::UnderlineStyle value; -1 ⇒ unset (SingleUnderline). Qt has no
  // separate colour for strike/overline (always the text colour), so these apply
  // to the link underline only. colour invalid → inherits the link text colour.
  int linkUnderlineStyle = -1;      // QTextCharFormat::UnderlineStyle; -1 ⇒ Single
  QColor linkUnderlineColor;        // `text-decoration-color` on `a`; invalid → link colour
  bool linkOverline = false;        // `text-decoration-line: overline` on `a`
  // Phase 3b: inline-code chip geometry from CSS `code` (paint-only box; advance
  // stays = text advance so editing/cursor/hit-test are unaffected). Padding +
  // radius defaults reproduce the legacy hardcoded chip (-3/+6 padding, radius 3).
  // Border is DECLARED-ONLY: width defaults to 0 (no border) unless the CSS sets
  // `border`/`border-width` on `code`. The old 1px default invented an edge the
  // theme never declared — Typora renders a border-less `code` rule with no
  // border, so a non-zero default deviated from the source CSS (phycat, plus the
  // newsprint/night/pixyll/whitey built-ins, all declare no code border).
  qreal inlineCodePaddingH = 3.0;
  qreal inlineCodePaddingV = 1.0;
  qreal inlineCodeBorderRadius = 3.0;
  qreal inlineCodeBorderWidth = 0.0;
  QColor inlineCodeShadowColor;
  qreal inlineCodeShadowOffsetX = 0.0;
  qreal inlineCodeShadowOffsetY = 0.0;
  qreal inlineCodeShadowBlur = 0.0;
  qreal inlineCodeShadowSpread = 0.0;
  // Phase 3c: HTML <kbd> keycap box driven by CSS `kbd`. Invalid/zero → fall
  // back to the legacy light/dark keycap heuristic so built-ins are unchanged.
  QColor kbdBackground;           // `kbd { background-color }`
  QColor kbdTextColor;            // `kbd { color }`
  QString kbdFont;                // `kbd { font-family }`
  qreal kbdPaddingH = 0.0;
  qreal kbdPaddingV = 0.0;
  qreal kbdBorderRadius = 0.0;
  QColor kbdBorderColor;
  qreal kbdBorderWidth = 0.0;
  // Phase 4: `kbd { border-bottom-width / border-bottom-color }` — phycat's chunky
  // 3D keycap uses a thicker bottom edge than the other three sides. Zero/invalid
  // → fall back to the uniform border width/colour so the legacy keycap is intact.
  qreal kbdBorderBottomWidth = 0.0;
  QColor kbdBorderBottomColor;
  QColor kbdShadowColor;          // bottom-edge "raised key" line colour
};

// The CSS-theme page model: body/html paint the viewport, while #write is a centered
// document card with its own max-width, margins and padding. All fields are
// optional; invalid/zero values fall back to Muffin's legacy flat page metrics.
struct ThemePage {
  QColor viewportBackground;
  QColor pageBackground;
  QColor pageBorderColor;
  qreal pageBorderWidth = 0.0;
  qreal pageBorderRadius = 0.0;
  QMarginsF pagePadding;
  QMarginsF pageMargin;
  // True when the CSS declared a #write margin at all (shorthand or any
  // longhand), OR when #write has padding so the CSS default margin of 0 is the
  // intent. Distinguishes "margin: 0 auto" (parses to an all-zero QMarginsF)
  // from "no margin rule" — both are null QMarginsF, but only the latter should
  // fall back to Muffin's legacy flat-document inset. See RenderTheme::pageMargin().
  bool pageMarginExplicit = false;
  qreal pageMaxWidth = 0.0;
  QColor pageShadowColor;
  qreal pageShadowOffsetX = 0.0;
  qreal pageShadowBlur = 0.0;
  qreal pageShadowOffsetY = 0.0;
  qreal pageShadowSpread = 0.0;
};

// Element VISUAL-BOX geometry (margin/padding/border/radius/fit-content of p,
// h1–h6, blockquote, and the list indent) lives in `ThemeDecorations::elementStyles`
// (ThemeElementStyle::box) — the single source for those properties. This struct
// holds only what that system does NOT cover: layout-flow block margins, the box
// flags/padding for `pre`/`table` (which have no element style), the heading
// `::before` marker advance, and the render-layer list-marker gap.
struct ThemeBlockSpacing {
  // Px the heading text is inset from its left padding edge to reserve room for
  // an inline `::before` marker (phycat h4/h5/h6). 0 for absolute befores (h3,
  // which sits in the heading's own padding gap) and headings with no before.
  qreal headingBeforeAdvance[6] = {};
  QMarginsF codeBlockMargin;
  QMarginsF tableMargin;
  QMarginsF listMargin;
  // Gap between the list marker and the content text. 0 ⇒ "auto": the renderer
  // floors it at a readable minimum so small-indent themes (phycat's
  // padding-left:13px) don't collapse the bullet onto the text, while large-indent
  // themes keep their proportional look. A theme may set an explicit px override.
  qreal listMarkerGap = 0.0;
  // CSS `list-style-type` declared on ul / ol / li (the type keyword of the
  // `list-style` shorthand too). Empty ⇒ legacy depth-based marker. Resolved per
  // list kind at layout time (ordered → ol, bullet → ul, with li as a fallback).
  QString ulListStyleType;
  QString olListStyleType;
  QString liListStyleType;
  // Phase 4b: CSS `pre`/`.md-fences` box. codeBlockBoxThemed flips codePadding()
  // to the CSS value (legacy scaled(12/10) otherwise) and rounds the fence box.
  QMarginsF codeBlockPadding;
  qreal codeBlockBorderRadius = 0.0;
  bool codeBlockBoxThemed = false;
  // Phase 4c: CSS `td`/`th` padding + `table` radius. tableBoxThemed flips
  // tableCellPadding() to the CSS value (legacy scaled(12/6) otherwise).
  QMarginsF tableCellPadding;
  qreal tableBorderRadius = 0.0;
  bool tableBoxThemed = false;
};

// A CSS linear/radial gradient parsed at theme-map time. Carried as data (not a
// Qt brush) so the paint side can rebuild a QGradient against any target rect at
// paint time (gradients are rect-relative). kind=None ⇒ no gradient; the field is
// optional on every decoration that holds one.
struct GradientStop {
  qreal position = 0.0;  // 0..1 along the gradient axis
  QColor color;
};
struct GradientSpec {
  enum class Kind { None, Linear, Radial, Conic };
  Kind kind = Kind::None;
  std::vector<GradientStop> stops;
  qreal angleDeg = 180.0;                       // linear: CSS angle (0=to top, 90=to right)
  QPointF radialCenter = QPointF(0.5, 0.5);     // radial: center as fractions of target rect
  qreal radialRadius = 0.5;                     // radial: radius as fraction of target's larger side
  QPointF conicCenter = QPointF(0.5, 0.5);      // conic: center as fractions of target rect
  qreal conicStartDeg = 0.0;                    // conic: CSS `from <angle>` (0=12 o'clock, clockwise)
};

// One `::before`/`::after` rule, resolved to a paint recipe against its host
// element. Muffin has no CSS box model, so positioning is heuristic (anchored to
// the host's existing rect); see the decoration painters. `present` marks a rule
// that the theme actually declared (so callers can tell "no ::after" from
// "::after with empty content + a background").
// A parsed piece of a CSS `content` value: a literal run, or a counter() call.
// `counter(name[, style])` / `counters(name, sep[, style])` are resolved at layout
// time against the live counter state (the implicit `list-item` counter for lists;
// the heading outline counters h1..h6 for heading ::before auto-numbering).
struct ContentToken {
  enum class Kind { Literal, Counter, Counters };
  Kind kind = Kind::Literal;
  QString text;    // Literal: the text; Counter/Counters: the counter name
  QString style;   // Counter/Counters: list-style-type (empty ⇒ decimal)
  QString sep;     // Counters: the separator string
};

struct PseudoElementRule {
  QString host;           // "h2","blockquote","a","#write","li","pre",…
  QString pseudo;         // "before" | "after"
  QString content;        // literal text / "" / "attr(data-language)"
  // `content` split into tokens so heading ::before content like `counter(h1) ". "`
  // can be resolved against the live counter state at layout time (mirrors
  // ThemeDecorations::listMarkerContent). Empty for content with no counter() —
  // the painter then draws `content` verbatim (the legacy literal fast path).
  std::vector<ContentToken> contentTokens;
  QByteArray svgData;     // decoded url(data:image/svg+xml,…)
  bool svgFromMask = false;  // svgData came from `mask:` → render as alpha mask tinted with `color`/`backgroundColor`
  GradientSpec background;
  QColor backgroundColor;
  QColor color;           // currentColor for text/icon
  QColor maskTint;        // texture ::before background-color
  GradientSpec maskPattern;          // mask-image (radial-gradient dots, …)
  QSizeF maskTile = QSizeF(20, 20);  // mask-size
  QSizeF size;            // width/height (invalid/0 ⇒ content/auto)
  // The var-resolved raw width/height declarations, carried so the painter can
  // resolve a `%` against the HOST box (e.g. heading height) instead of the
  // em-relative value `size` bakes at map-time. phycat's `h3::before { height:
  // 61% }` is 61% of the rendered heading, not 0.61em — resolving at map-time
  // (before the box exists) made the bar too short. Empty when the CSS sets no
  // width/height; the painter then falls back to `size` / em.
  QString sizeRawWidth;
  QString sizeRawHeight;
  // Hover-state width for ::after/::before (phycat `h1:hover::after { width:100% }`):
  // the bar widens from its base `size.width()` toward this on hover, animated by
  // the HoverAnimator phase. Empty ⇒ no hover widening. Carried raw so the painter
  // can resolve a `%` against the host box at paint time (like `sizeRawWidth`).
  QString hoverWidthRaw;
  // Focus-state width for ::after/::before (parallel to hoverWidthRaw): the bar
  // widens toward this on focus, animated by the FocusAnimator phase. Applied after
  // the hover widening so the two compose. Empty ⇒ no focus widening.
  QString focusWidthRaw;
  QMarginsF insets;       // top/left/bottom/right offsets (absolute pseudo left/top)
  QColor borderBottomColor;
  qreal borderBottomWidth = 0.0;
  // Phase 2b: heading pseudo geometry. `absolute` (position:absolute) anchors the
  // box to the host padding box (e.g. phycat's h3 left bar); otherwise the pseudo
  // is inline and the host text is inset by `advance` to make room (h4/h5/h6
  // circles / h6 dash). borderRadius/borderColor/borderWidth paint a filled
  // rounded box and/or an outline (h4 filled disc vs h5 hollow ring).
  bool absolute = false;
  qreal borderRadius = 0.0;
  QColor borderColor;
  qreal borderWidth = 0.0;
  qreal marginLeft = 0.0;
  qreal marginRight = 0.0;
  // `top` offset (absolute pseudo) and `font-size` (e.g. blockquote ✨ at
  // font-size:20px). 0 ⇒ inherit the host's font/top behaviour. `left` already
  // lives in insets.left(); `top` has its own slot so the painter can tell an
  // explicit top from "unset" (QMarginsF defaults every side to 0).
  qreal insetsTop = -1.0;  // < 0 ⇒ unset (fall back to text-baseline anchoring)
  qreal fontSizePx = 0.0;
  qreal opacity = 1.0;
  bool present = false;
};

// A host element's OWN background (not a pseudo-element): the gradient/colour an
// element like `h2 { background-image: radial-gradient(...) }` paints behind its
// content. phycat's h2 "fusion glass" glow and hr gradient live here.
struct ElementBackground {
  QString host;  // "h2","blockquote","hr",…
  GradientSpec gradient;
  QColor color;       // solid background-color (paint under the gradient)
  qreal opacity = 1.0;
  // Phase 2c: a fit-content heading pill (phycat h2) rounds its corners and may
  // carry a top hairline. Both paint on the same box as the gradient fill.
  qreal borderRadius = 0.0;
  QColor borderTopColor;
  qreal borderTopWidth = 0.0;
  bool present = false;
};

// A `:hover` effect on a host element — the tractable subset Muffin animates:
// a box-shadow glow (colour + blur) and/or a background tint. phycat's
// blockquote:hover / h2:hover glows live here. Other hover changes (transform
// scale, text-shadow, background-image swap) are deferred.
struct HoverEffect {
  QString host;
  QColor glowColor;
  qreal glowBlur = 0.0;
  QColor bgTint;
  bool present = false;
};

// A CSS `transition` duration on a host element (e.g. `transition: all .3s ease`),
// used to time the hover fade. 0 → snap (no animation).
struct TransitionSpec {
  QString host;
  qreal durationMs = 0.0;
};

// One keyframe stop: a timeline position (0..1) and the raw declarations active
// there (var() unresolved — the sampler resolves + interpolates at paint time).
struct KeyframeStop {
  qreal position = 0.0;
  QHash<QString, QString> declarations;
};

// A named `@keyframes` definition. Stops sorted by position.
struct KeyframesDef {
  QString name;
  std::vector<KeyframeStop> stops;
};

// A host element's `animation:` shorthand, bound to a KeyframesDef by name.
struct AnimationDef {
  QString host;
  QString name;
  qreal durationMs = 0.0;
  qreal delayMs = 0.0;
  int iterations = 1;   // -1 = infinite
  enum class Direction { Normal, Reverse, Alternate, AlternateReverse };
  Direction direction = Direction::Normal;
  QString easing;  // "linear"/"ease"/"ease-in-out"/cubic-bezier(...) — sampler parses
};

// Pseudo-element decorations + hover effects + transitions (+ future @keyframes).
// A nested-list guide line drawn from a `li::before { border-left: …; left;
// top; height: calc(100% - Npx) }` rule (phycat's tree guide). Distinct from the
// generic pseudo-element painter because it is a per-item vertical decoration,
// not a marker/icon: each list item draws its own segment, and nesting depth
// (each item's indented left edge) produces the stacked tree automatically.
struct ListGuide {
  QColor color;
  qreal width = 0.0;       // border-left width; 0 ⇒ no line
  qreal leftOffset = 0.0;  // li-relative X offset of the line (may be negative)
  qreal topInset = 0.0;    // px below the item's top where the line begins
  qreal bottomInset = 0.0; // px above the item's bottom where the line ends
  bool present = false;    // a usable guide (valid colour + positive width)
};

struct ThemeDecorations {
  std::vector<PseudoElementRule> pseudos;     // ::before/::after, host-keyed
  std::vector<ElementBackground> backgrounds;  // element own background, host-keyed
  std::vector<HoverEffect> hoverEffects;       // :hover glow/tint, host-keyed
  std::vector<TransitionSpec> transitions;     // transition duration, host-keyed
  std::vector<KeyframesDef> keyframes;         // @keyframes defs, name-keyed
  std::vector<AnimationDef> animations;        // host → animation binding
  ListGuide listGuide;                         // nested-list guide line, host=li::before
  // `li::marker { content: … counter(list-item) … }` parsed into tokens. Non-empty
  // ⇒ the marker text is content-driven (counter resolved per item at layout time),
  // overriding list-style-type.
  std::vector<ContentToken> listMarkerContent;

  // CSS counter-reset / counter-increment declarations, captured per host so heading
  // ::before content like `counter(h1) ". "` can be resolved against a real outline
  // state machine at layout time (DocumentLayout walks the AST in document order,
  // applies each heading host's resets then increments). `none` is the absence of ops.
  // Keyed by host: "h1".."h6", plus "#write"/"body" for the document-root reset.
  struct CounterOps {
    QVector<QPair<QString, int>> resets;      // name → reset value (default 0)
    QVector<QPair<QString, int>> increments;  // name → increment step (default 1)
  };
  QHash<QString, CounterOps> hostCounterOps;
  // True iff some h1..h6 ::before rule has a counter()/counters() token. Gates the
  // whole heading-counter subsystem: when false, DocumentLayout never walks the AST
  // for counters and builders never look up — zero cost for ordinary themes.
  bool hasHeadingCounters = false;
};

struct ThemeElementBoxStyle {
  bool present = false;
  QMarginsF margin;
  QMarginsF padding;
  qreal borderTopWidth = 0.0;
  qreal borderRightWidth = 0.0;
  qreal borderBottomWidth = 0.0;
  qreal borderLeftWidth = 0.0;
  QColor borderTopColor;
  QColor borderRightColor;
  QColor borderBottomColor;
  QColor borderLeftColor;
  qreal borderRadius = 0.0;
  // `width: fit-content` (or max-content/min-content) on the element: its own
  // background/decoration box shrinks to the text instead of spanning the block.
  // Other width values (auto/%/px) leave this false — only fit-content is a
  // paint-time concern; layout/hit-test stay full-width regardless.
  bool widthFitContent = false;
};

struct ThemeElementPaintStyle {
  QColor color;
  QColor backgroundColor;
  GradientSpec backgroundImage;
  QColor boxShadowColor;
  qreal boxShadowBlur = 0.0;
  qreal opacity = 1.0;
  qreal transformScale = 1.0;
  // CSS `filter:` applied to the element's own background box (subtree content is
  // not filtered). Defaults leave the image unchanged; present ⇒ any filter set.
  qreal filterBlur = 0.0;
  qreal filterBrightness = 1.0;   // 1 = unchanged
  qreal filterContrast = 1.0;
  qreal filterGrayscale = 0.0;    // 0..1
  qreal filterSepia = 0.0;        // 0..1
  qreal filterHueRotateDeg = 0.0;
  qreal filterOpacity = 1.0;
  bool filterPresent = false;
  // CSS `backdrop-filter:` — same func set as `filter:`, applied to the backdrop
  // (the content painted BEHIND the element's box) rather than the element's own
  // background. Rendered by sampling the paint device behind the box (works when
  // painting to a QImage — tests/export; the live screen editor would need a
  // QImage-backed viewport). Defaults leave the backdrop unchanged.
  qreal backdropBlur = 0.0;
  qreal backdropBrightness = 1.0;
  qreal backdropContrast = 1.0;
  qreal backdropGrayscale = 0.0;
  qreal backdropSepia = 0.0;
  qreal backdropHueRotateDeg = 0.0;
  qreal backdropOpacity = 1.0;
  bool backdropPresent = false;
};

// CSS `text-shadow: <ox> <oy> <blur>? <color>`. Only the first shadow of a
// comma list is honoured (multiple shadows are rare in themes). present ⇒ the
// theme declared one with a valid colour.
struct TextShadow {
  QPointF offset;
  qreal blur = 0.0;
  QColor color;
  bool present = false;
};

struct ThemeElementTextStyle {
  QString fontFamily;
  qreal fontSizePx = 0.0;
  qreal lineHeight = 0.0;
  qreal wordSpacing = 0.0;
  int fontWeight = 0;
  bool fontWeightSet = false;
  bool italic = false;
  bool italicSet = false;
  Qt::Alignment alignment;
  // CSS text-transform: 0=none, 1=uppercase, 2=lowercase, 3=capitalize (maps 1:1 to
  // the TextTransform enum in InlineProjection.h). int (not the enum) keeps this
  // header free of a projection include.
  int textTransform = 0;
  TextShadow textShadow;  // CSS `text-shadow` on the element; present=false ⇒ none
};

struct ThemeElementStyle {
  QString key;  // e.g. "h2", "blockquote p", "li::marker"
  ThemeElementBoxStyle box;
  ThemeElementPaintStyle paint;
  ThemeElementTextStyle text;
};

// A complete, serializable theme. Built-in themes are produced by builtIns();
// custom themes are loaded from JSON files (see fromJson) and merged into the
// same registry, so the rest of the app never has to special-case them.
struct ThemeDefinition {
  QString id;         // machine name, e.g. "newsprint" (also the JSON file stem)
  QString label;      // display name, e.g. "Newsprint"
  ThemeColors colors;
  ThemeTypography typography;
  ThemePage page;
  ThemeBlockSpacing spacing;
  ThemeDecorations decorations;
  std::vector<ThemeElementStyle> elementStyles;
  // True when the theme uses selectors that need the live document tree to match
  // (`+`/`~` combinators or structural pseudo-classes such as `:first-child`,
  // `:nth-child(n)`, `:has(...)`). When set, `structuralSheet` carries the parsed
  // CSS so the layout path can run the engine against each node's real position.
  // Absent (false/null) for every theme without such selectors — the load-time
  // prototype precompute is the whole answer, with no per-layout cost.
  bool hasStructuralRules = false;
  // True only when some selector reads typeIndex (:nth-of-type / :first-of-type / :last-of-type /
  // :only-of-type). When false (every bundled theme), the structural builder skips the per-sibling
  // typeCounts QString-hash maintenance — the dominant cost of the per-splice sibling re-link.
  bool hasNthOfType = false;
  qreal bodyFontPx = 16.0;  // body font size in CSS px (em basis for the structural path)
  std::shared_ptr<CssThemeSheet> structuralSheet;  // only populated when hasStructuralRules
  bool isBuiltIn = true;

  bool valid() const;

  // Derive chrome tokens (chromeBackground/Text/Muted, surface, canvas, border,
  // hover, selected, accent) from whatever document colours are already set, so
  // a theme that only specifies document colours still renders sane chrome.
  // Shared by the JSON and CSS loaders so both derive identically. Does NOT touch
  // isDark — each loader sets that from its own source (JSON bool / CSS inference).
  static void deriveChromeDefaults(ThemeColors& colors);

  // JSON serialization — used both to persist a built-in for inspection and to
  // load a user-supplied custom theme file. `id` overrides the JSON's name when
  // loading (e.g. derived from the file name).
  static ThemeDefinition fromJson(const QJsonObject& json, const QString& id = {});
  QJsonObject toJson() const;

  // Load a CSS theme file (filesystem path or :/resource) and
  // translate it into a ThemeDefinition via the CssThemeMapper. `id` overrides
  // any name the CSS declares (usually derived from the file stem). Returns an
  // invalid definition if the file can't be read or no usable text colour can
  // be resolved/derived from its CSS palette.
  // Also registers any @font-face fonts with QFontDatabase as a side effect.
  static ThemeDefinition fromCss(const QString& cssPath, const QString& id);
  // Resolve a CSS @font-face declared family name to the name QFontDatabase
  // registered (the font file's internal name), or empty if not an alias. Used
  // by the font-stack builder so a declared `CascadiaCode` (internal "Cascadia
  // Code") or `"LXGW WenKai"` (internal 霞鹜文楷) resolves to the bundled font.
  static QString fontFamilyAlias(const QString& declaredName);

  // The built-in themes, in display order. Each carries a chrome palette that
  // matches its document palette (so e.g. the warm "newsprint" theme warms the
  // whole chrome, not just the document).
  static const std::vector<ThemeDefinition>& builtIns();
  static std::optional<ThemeDefinition> builtIn(const QString& id);
};

}  // namespace muffin
