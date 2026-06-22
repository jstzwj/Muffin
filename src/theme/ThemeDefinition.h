#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMarginsF>
#include <QPointF>
#include <QSizeF>
#include <QString>

#include <optional>
#include <vector>

namespace muffin {

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
  QColor chromeMuted;       // secondary text, disabled controls
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
  qreal lineHeight = 0.0;           // unitless CSS line-height multiplier
  qreal headingSizePt[6] = {};      // h1..h6; 0 → fall back
  qreal headingLineHeight[6] = {};  // h1..h6; 0 → lineHeight/default

  // Per-heading text colour. Invalid → fall back to the body text colour.
  QColor headingColor[6];
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
  qreal pageShadowBlur = 0.0;
  qreal pageShadowOffsetY = 0.0;
};

struct ThemeBlockSpacing {
  QMarginsF paragraphMargin;
  QMarginsF headingMargin[6];
  QMarginsF headingPadding[6];
  QColor headingBorderBottomColor[6];
  qreal headingBorderBottomWidth[6] = {};
  QColor headingBorderLeftColor[6];
  qreal headingBorderLeftWidth[6] = {};
  QMarginsF blockquoteMargin;
  QMarginsF codeBlockMargin;
  QMarginsF tableMargin;
  QMarginsF listMargin;
  qreal listPaddingLeft = 0.0;
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
  enum class Kind { None, Linear, Radial };
  Kind kind = Kind::None;
  std::vector<GradientStop> stops;
  qreal angleDeg = 180.0;                       // linear: CSS angle (0=to top, 90=to right)
  QPointF radialCenter = QPointF(0.5, 0.5);     // radial: center as fractions of target rect
  qreal radialRadius = 0.5;                     // radial: radius as fraction of target's larger side
};

// One `::before`/`::after` rule, resolved to a paint recipe against its host
// element. Muffin has no CSS box model, so positioning is heuristic (anchored to
// the host's existing rect); see the decoration painters. `present` marks a rule
// that the theme actually declared (so callers can tell "no ::after" from
// "::after with empty content + a background").
struct PseudoElementRule {
  QString host;           // "h2","blockquote","a","#write","li","pre",…
  QString pseudo;         // "before" | "after"
  QString content;        // literal text / "" / "attr(data-language)"
  QByteArray svgData;     // decoded url(data:image/svg+xml,…)
  bool svgFromMask = false;  // svgData came from `mask:` → render as alpha mask tinted with `color`/`backgroundColor`
  GradientSpec background;
  QColor backgroundColor;
  QColor color;           // currentColor for text/icon
  QColor maskTint;        // texture ::before background-color
  GradientSpec maskPattern;          // mask-image (radial-gradient dots, …)
  QSizeF maskTile = QSizeF(20, 20);  // mask-size
  QSizeF size;            // width/height (invalid/0 ⇒ content/auto)
  QMarginsF insets;       // top/left/bottom/right offsets
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
struct ThemeDecorations {
  std::vector<PseudoElementRule> pseudos;     // ::before/::after, host-keyed
  std::vector<ElementBackground> backgrounds;  // element own background, host-keyed
  std::vector<HoverEffect> hoverEffects;       // :hover glow/tint, host-keyed
  std::vector<TransitionSpec> transitions;     // transition duration, host-keyed
  std::vector<KeyframesDef> keyframes;         // @keyframes defs, name-keyed
  std::vector<AnimationDef> animations;        // host → animation binding
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
  // invalid definition if the file can't be read or lacks background + text.
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
