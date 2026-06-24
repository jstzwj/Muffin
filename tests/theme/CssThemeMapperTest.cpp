#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"
#include "theme/CssThemeParser.h"
#include "theme/ThemeManager.h"
#include "render/KeyframeSampler.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QString>
#include <QtGlobal>
#include <QTemporaryDir>

#include "../TestUtils.h"

using namespace muffin;

namespace {

void testResolveVars() {
  QHash<QString, QString> vars;
  vars.insert(QStringLiteral("--accent"), QStringLiteral("#ff7096"));
  vars.insert(QStringLiteral("--deep"), QStringLiteral("var(--accent)"));
  require(CssThemeParser::resolveVars(QStringLiteral("var(--accent)"), vars) == QStringLiteral("#ff7096"),
          QStringLiteral("simple var() should resolve"));
  require(CssThemeParser::resolveVars(QStringLiteral("1px solid var(--accent)"), vars) == QStringLiteral("1px solid #ff7096"),
          QStringLiteral("var() inside a shorthand should resolve in place"));
  require(CssThemeParser::resolveVars(QStringLiteral("var(--missing, #fb)"), vars) == QStringLiteral("#fb"),
          QStringLiteral("unknown var should use the fallback"));
  require(CssThemeParser::resolveVars(QStringLiteral("var(--deep)"), vars) == QStringLiteral("#ff7096"),
          QStringLiteral("nested var() should resolve transitively"));
}

void testSplitCommas() {
  const QStringList parts = CssThemeParser::splitTopLevelCommas(
      QStringLiteral("\"iA Writer Quattro\", \"Inter\", sans-serif"));
  require(parts.size() == 3, QStringLiteral("font stack should split into 3 families"));
  require(parts.at(0) == QStringLiteral("\"iA Writer Quattro\""), QStringLiteral("first family preserved with quotes"));
}

// A self-contained CSS-theme theme that exercises every mapper path with
// known values, so the assertions are deterministic.
const char* kSampleCss = R"(
:root {
  --accent: #ff7096;
  --code-bg: #fce4ec;
  --mark: rgba(255, 220, 128, 0.46);
  --muffin-label: "Sample Test Theme";
  --muffin-serif-body: 1;
  --muffin-chrome-background: #fafafa;
  --muffin-canvas: #f0f0f0;
  --muffin-accent: #c2185b;
}
html, body {
  background: #fbfaf7;
  color: #1f2328;
  font-family: "iA Writer Quattro", "Inter", sans-serif;
  font-size: 16px;
  line-height: 1.7;
}
#write { background: #ffffff; }
a { color: #2f6f9f; }
a:hover { color: #234a70; }
h1 { color: #111111; font-size: 2.0rem; }
h2 { color: #222222; font-size: 1.5rem; border-left: 4px solid var(--accent); }
code, tt { background: var(--code-bg); font-family: "JetBrains Mono", monospace; }
pre, .md-fences { background: #f6f8fa; }
blockquote { border-left: 4px solid #c8bfae; background: #fff8ee; }
table { border: 1px solid #dfe2e5; }
thead { background: #edf4ff; }
th { background: #edf4ff; }
tbody tr:nth-child(even) { background: #f6f8fa; }
mark { background: var(--mark); }
::selection { background: #d7e8ff; }
)";

void testSampleTheme() {
  const ThemeDefinition d = CssThemeMapper::fromCss(QString::fromUtf8(kSampleCss), QStringLiteral("sample"), QString());
  require(d.valid(), QStringLiteral("sample theme should produce a valid definition"));

  require(d.colors.background.name(QColor::HexRgb) == QStringLiteral("#ffffff"),
          QStringLiteral("page background should come from #write (id beats body tag)"));
  require(d.colors.text.name(QColor::HexRgb) == QStringLiteral("#1f2328"), QStringLiteral("text colour from body"));
  require(d.colors.link.name(QColor::HexRgb) == QStringLiteral("#2f6f9f"), QStringLiteral("link colour from base a, not a:hover"));
  require(d.colors.codeBackground.name(QColor::HexRgb) == QStringLiteral("#fce4ec"), QStringLiteral("inline code background"));
  require(d.colors.codeBlockBackground.name(QColor::HexRgb) == QStringLiteral("#f6f8fa"), QStringLiteral("fenced code background distinct from inline"));
  require(d.colors.highlight.red() == 255 && d.colors.highlight.green() == 220 && d.colors.highlight.blue() == 128,
          QStringLiteral("highlight colour resolved through var() including rgba"));
  require(d.colors.quoteBorder.name(QColor::HexRgb) == QStringLiteral("#c8bfae"), QStringLiteral("quote border from blockquote border-left"));
  require(d.colors.blockquoteBackground.name(QColor::HexRgb) == QStringLiteral("#fff8ee"), QStringLiteral("blockquote fill"));
  require(d.colors.tableBorder.name(QColor::HexRgb) == QStringLiteral("#dfe2e5"), QStringLiteral("table border"));
  require(d.colors.tableHeaderBackground.name(QColor::HexRgb) == QStringLiteral("#edf4ff"), QStringLiteral("table header background"));
  require(d.colors.tableAlternateBackground.name(QColor::HexRgb) == QStringLiteral("#f6f8fa"), QStringLiteral("alternating row background"));
  require(d.colors.selection.name(QColor::HexRgb) == QStringLiteral("#d7e8ff"), QStringLiteral("selection from ::selection"));

  require(d.typography.headingColor[0].name(QColor::HexRgb) == QStringLiteral("#111111"), QStringLiteral("h1 colour"));
  require(d.typography.headingColor[1].name(QColor::HexRgb) == QStringLiteral("#222222"), QStringLiteral("h2 colour"));
  require(qAbs(d.typography.headingSizePt[0] - 24.0) < 0.01, QStringLiteral("h1 2rem → 24pt"));
  require(qAbs(d.typography.headingSizePt[1] - 18.0) < 0.01, QStringLiteral("h2 1.5rem → 18pt"));
  require(d.colors.headingAccentColor.name(QColor::HexRgb) == QStringLiteral("#ff7096"),
          QStringLiteral("h2 accent bar colour from border-left via var()"));

  require(d.typography.bodyFont.startsWith(QStringLiteral("iA Writer Quattro")), QStringLiteral("body font stack should preserve first family"));
  require(d.typography.codeFont.startsWith(QStringLiteral("JetBrains Mono")), QStringLiteral("code font stack should preserve first family"));
  require(qAbs(d.typography.bodySizePt - 12.0) < 0.01, QStringLiteral("16px body → 12pt"));
  require(qAbs(d.typography.lineHeight - 1.7) < 0.001, QStringLiteral("line-height multiplier"));

  require(d.colors.serifBody == true, QStringLiteral("--muffin-serif-body should flip serif body"));
  require(d.label == QStringLiteral("Sample Test Theme"), QStringLiteral("--muffin-label sets the display name"));
  require(d.colors.chromeBackground.name(QColor::HexRgb) == QStringLiteral("#fafafa"), QStringLiteral("chrome background via --muffin-*"));
  require(d.colors.canvas.name(QColor::HexRgb) == QStringLiteral("#f0f0f0"), QStringLiteral("canvas via --muffin-*"));
  require(d.colors.accent.name(QColor::HexRgb) == QStringLiteral("#c2185b"), QStringLiteral("accent via --muffin-* overrides link-derived"));
  require(d.colors.isDark == false, QStringLiteral("light background → not dark"));
}

void testWhiteyTypographySemantics() {
  const QString css = QStringLiteral(
      "html { font-size: 19px; }"
      "body { background:#fefefe; color:#333; font-family:'Vollkorn', Palatino, Times; line-height:1.4; text-align:justify; }"
      "#write { line-height:1.53; }"
      "h1 { font-size:3em; margin-top:1.6em; font-weight:normal; }"
      "h2 { margin-top:2em; font-weight:normal; }"
      "h3 { margin-top:3em; font-weight:normal; font-style:italic; }"
      "h1, h2, h3 { text-align:center; }"
      "h2:after { border-bottom:1px solid #2f2f2f; content:''; width:100px; display:block; margin:0 auto; height:1px; }");
  const ThemeDefinition d = CssThemeMapper::fromCss(css, QStringLiteral("whitey-like"), QString());
  require(d.valid(), QStringLiteral("whitey-like CSS should produce a valid definition"));
  require(d.colors.serifBody, QStringLiteral("serif font stacks should flip the serif body flag"));
  require(d.typography.bodyFont.contains(QStringLiteral("Palatino")) && d.typography.bodyFont.contains(QStringLiteral("Times")),
          QStringLiteral("body font stack should preserve Whitey's serif fallback families"));
  require(qAbs(d.typography.bodySizePt - 14.25) < 0.01, QStringLiteral("19px root/body font-size should map to 14.25pt"));
  require(qAbs(d.typography.lineHeight - 1.53) < 0.001, QStringLiteral("#write line-height should override body line-height"));
  require(d.typography.bodyAlignment == Qt::AlignJustify, QStringLiteral("body text-align: justify should be captured"));
  for (int i = 0; i < 3; ++i) {
    require(d.typography.headingAlignment[i] == Qt::AlignHCenter, QStringLiteral("h%1 text-align should be centered").arg(i + 1));
    require(d.typography.headingFontWeightSet[i], QStringLiteral("h%1 font-weight should be explicit").arg(i + 1));
    require(d.typography.headingFontWeight[i] == QFont::Normal, QStringLiteral("h%1 font-weight should be normal").arg(i + 1));
  }
  require(d.typography.headingItalicSet[2] && d.typography.headingItalic[2], QStringLiteral("h3 font-style: italic should be captured"));
  require(qAbs(d.typography.headingSizePt[0] - 42.75) < 0.01, QStringLiteral("h1 3em should be relative to 19px body font"));
  require(qAbs(d.spacing.headingMargin[0].top() - 91.2) < 0.5, QStringLiteral("h1 margin-top 1.6em should use h1 font-size as em"));

  const PseudoElementRule* h2After = nullptr;
  for (const PseudoElementRule& r : d.decorations.pseudos) {
    if (r.host == QStringLiteral("h2") && r.pseudo == QStringLiteral("after")) { h2After = &r; }
  }
  require(h2After != nullptr, QStringLiteral("Whitey h2:after should be captured as a pseudo decoration"));
  require(qAbs(h2After->size.width() - 100.0) < 0.01, QStringLiteral("h2:after width should be 100px"));
  require(qAbs(h2After->size.height() - 1.0) < 0.01, QStringLiteral("h2:after height should be 1px"));
  require(h2After->borderBottomColor == QColor(QStringLiteral("#2f2f2f")), QStringLiteral("h2:after border-bottom colour should be captured"));
  require(qAbs(h2After->borderBottomWidth - 1.0) < 0.01, QStringLiteral("h2:after border-bottom width should be captured"));
}

void testMistBlueFixture(const QString& path) {
  QFile f(path);
  require(f.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("could not open mist-blue fixture"));
  const QString text = QString::fromUtf8(f.readAll());
  const QString baseDir = QFileInfo(path).absolutePath();
  const ThemeDefinition d = CssThemeMapper::fromCss(text, QStringLiteral("mist-blue"), baseDir);
  require(d.valid(), QStringLiteral("real mist-blue theme should parse to a valid definition"));
  require(d.page.viewportBackground.red() == 217 && d.page.viewportBackground.green() == 222 && d.page.viewportBackground.blue() == 232,
          QStringLiteral("body background should become the viewport background"));
  // #write paper is now the page/card background, while body remains the outer viewport.
  require(d.page.pageBackground.red() == 248 && d.page.pageBackground.green() == 250 && d.page.pageBackground.blue() == 253,
          QStringLiteral("#write paper should become the page card background"));
  require(d.colors.background.red() == 248 && d.colors.background.green() == 250 && d.colors.background.blue() == 253,
          QStringLiteral("legacy background should still be the page card colour"));
  require(qAbs(d.page.pageMaxWidth - 860.0) < 0.01, QStringLiteral("#write max-width should map to pageMaxWidth"));
  require(qAbs(d.page.pageMargin.top() - 32.0) < 0.01 && qAbs(d.page.pageMargin.bottom() - 32.0) < 0.01,
          QStringLiteral("#write vertical margins should be 32px"));
  require(qAbs(d.page.pagePadding.top() - 52.0) < 0.01 && qAbs(d.page.pagePadding.left() - 64.0) < 0.01 &&
              qAbs(d.page.pagePadding.right() - 64.0) < 0.01 && qAbs(d.page.pagePadding.bottom() - 72.0) < 0.01,
          QStringLiteral("#write padding should map to page padding"));
  require(qAbs(d.page.pageBorderRadius - 8.0) < 0.01, QStringLiteral("#write radius should map to page radius"));
  require(d.colors.text.name(QColor::HexRgb) == QStringLiteral("#263241"), QStringLiteral("ink text colour"));
  require(d.colors.link.name(QColor::HexRgb) == QStringLiteral("#2f5e88"), QStringLiteral("accent-strong link colour"));
  require(d.colors.codeBackground.red() == 226, QStringLiteral("inline code bg via var(--code-bg)"));
  require(d.colors.codeBlockBackground.red() == 224, QStringLiteral("fenced code bg via var(--code-block-bg)"));
  // p,li { font-family: var(--font-writing) } comes after html,body { var(--font-sans) } → writing wins.
  require(d.typography.bodyFont.startsWith(QStringLiteral("LXGW WenKai")), QStringLiteral("body font should be the writing stack, not sans"));
  require(qAbs(d.typography.bodySizePt - 12.0) < 0.01, QStringLiteral("body 16px should map to 12pt"));
  require(qAbs(d.typography.lineHeight - 1.78) < 0.001, QStringLiteral("body line-height should map"));
  require(qAbs(d.typography.headingSizePt[0] - 25.2) < 0.01, QStringLiteral("h1 2.1rem should map to 25.2pt"));
  require(qAbs(d.typography.headingSizePt[1] - 18.6) < 0.01, QStringLiteral("h2 1.55rem should map to 18.6pt"));
  require(qAbs(d.spacing.paragraphMargin.top() - 11.52) < 0.01 && qAbs(d.spacing.paragraphMargin.bottom() - 11.52) < 0.01,
          QStringLiteral("p margin 0.72em should map"));
  require(qAbs(d.spacing.headingPadding[1].left() - 14.384) < 0.01, QStringLiteral("h2 padding-left 0.58em should use h2 font-size as em"));
  require(d.spacing.headingBorderLeftColor[1].name(QColor::HexRgb) == QStringLiteral("#4c6f91"),
          QStringLiteral("h2 left border colour should map"));
  require(d.colors.isDark == false, QStringLiteral("mist-blue is a light theme"));
}

// A "pure variable" CSS theme: ONLY :root custom properties, no element
// colour rules at all. This mirrors the real-world pattern of community CSS themes community
// themes (e.g. the phycat family) that define colours as variables and @import
// a shared base which applies them — defeating selector-based extraction but
// resolving cleanly through the standard-variable vocabulary tier.
const char* kPureVariableCss = R"(
:root {
  --bg-color: #0f111a;
  --text-color: #d6deeb;
  --text-color-secondary: #7e8c9f;
  --primary-color: #00f3ff;
  --code-block-bg: rgba(15, 17, 26, 0.6);
  --select-text-bg-color: rgba(0, 243, 255, 0.3);
  --side-bar-bg-color: #0f111a;
  --item-hover-bg-color: rgba(0, 243, 255, 0.08);
  --active-file-bg-color: #11203a;
  --control-text-color: #c8d3e5;
}
/* No html/body/#write colour rules — only variables. */
)";

void testPureVariableTheme() {
  const ThemeDefinition d = CssThemeMapper::fromCss(
      QString::fromUtf8(kPureVariableCss), QStringLiteral("phycat"), QString());
  require(d.valid(), QStringLiteral("a variables-only theme should be valid via the --bg-color/--text-color fallback"));
  require(d.colors.text.name(QColor::HexRgb) == QStringLiteral("#d6deeb"),
          QStringLiteral("text should come from --text-color"));
  require(d.colors.background.name(QColor::HexRgb) == QStringLiteral("#0f111a"),
          QStringLiteral("background should come from --bg-color"));
  require(d.page.viewportBackground.name(QColor::HexRgb) == QStringLiteral("#0f111a"),
          QStringLiteral("viewport should paint --bg-color"));
  require(d.colors.muted.name(QColor::HexRgb) == QStringLiteral("#7e8c9f"),
          QStringLiteral("muted should come from --text-color-secondary"));
  require(d.colors.accent.name(QColor::HexRgb) == QStringLiteral("#00f3ff"),
          QStringLiteral("accent should come from --primary-color"));
  require(d.colors.link.name(QColor::HexRgb) == QStringLiteral("#00f3ff"),
          QStringLiteral("link should fall back to --primary-color"));
  require(d.colors.codeBlockBackground.red() == 15 && d.colors.codeBlockBackground.green() == 17 &&
              d.colors.codeBlockBackground.blue() == 26,
          QStringLiteral("fenced-code bg should come from --code-block-bg (rgba resolved)"));
  require(d.colors.selection.red() == 0 && d.colors.selection.green() == 243 && d.colors.selection.blue() == 255,
          QStringLiteral("selection should come from --select-text-bg-color"));
  require(d.colors.chromeBackground.name(QColor::HexRgb) == QStringLiteral("#0f111a"),
          QStringLiteral("chrome background should come from --side-bar-bg-color"));
  require(d.colors.hover.red() == 0 && d.colors.hover.green() == 243 && d.colors.hover.blue() == 255,
          QStringLiteral("hover should come from --item-hover-bg-color"));
  require(d.colors.selected.name(QColor::HexRgb) == QStringLiteral("#11203a"),
          QStringLiteral("selected should come from --active-file-bg-color"));
  require(d.colors.chromeText.name(QColor::HexRgb) == QStringLiteral("#c8d3e5"),
          QStringLiteral("chrome text should come from --control-text-color (NOT --active-file-text-color, which is the active-item highlight text)"));
  require(d.colors.isDark == true, QStringLiteral("dark --bg-color should set isDark"));
}

// Precedence invariant: a concrete painted rule is authoritative for what is
// actually painted, so it must win over a same-purpose :root variable. Without
// this the variable tier would silently recolour themes that use --bg-color as
// an accent rather than the page fill.
void testCascadeBeatsVariable() {
  const char* css = R"(
:root { --bg-color: #000000; --text-color: #999999; }
body { background: #ffffff; color: #111111; }
)";
  const ThemeDefinition d = CssThemeMapper::fromCss(QString::fromUtf8(css), QStringLiteral("c"), QString());
  require(d.colors.background.name(QColor::HexRgb) == QStringLiteral("#ffffff"),
          QStringLiteral("a painted body background must win over --bg-color"));
  require(d.colors.text.name(QColor::HexRgb) == QStringLiteral("#111111"),
          QStringLiteral("a painted body colour must win over --text-color"));
}

// A theme with text but no background at all is still valid; the loader
// synthesises a contrasting canvas so isDark/chrome derivation stay sane.
void testTextOnlySynthesisesBackground() {
  const char* css = R"(
:root { --text-color: #d6deeb; }
)";
  const ThemeDefinition d = CssThemeMapper::fromCss(QString::fromUtf8(css), QStringLiteral("t"), QString());
  require(d.valid(), QStringLiteral("a text-only theme should be valid"));
  require(d.colors.text.name(QColor::HexRgb) == QStringLiteral("#d6deeb"), QStringLiteral("text from --text-color"));
  require(d.colors.background.isValid(), QStringLiteral("background should be synthesised"));
  require(d.colors.isDark == true, QStringLiteral("light text on no-bg should infer a dark canvas"));
}

// Regression for the pixyll "everything turns black" bug: pixyll declares
// `code { color:#7a7a7a }` with NO background and no mark / ::selection, so the
// code-background fill, highlight, selection, and the hover/selected tokens that
// fall back to codeBackground all resolved INVALID. Qt paints an unset QBrush as
// solid black, and hexRgb turns an invalid QSS token into #000000 — so the inline
// code chip, menu-item hover, tool-button hover and sidebar selection all went
// black. deriveChromeDefaults must now fill every one of them with a valid colour.
void testPixyllHasNoInvalidBlackProneTokens() {
  const ThemeDefinition d = ThemeDefinition::fromCss(QStringLiteral(":/themes/pixyll.css"), QStringLiteral("pixyll"));
  require(d.valid(), QStringLiteral("pixyll.css should load from the QRC resource"));
  const auto mustBeValid = [](const QColor& c, const char* name) {
    require(c.isValid(),
            QStringLiteral("pixyll %1 must resolve to a valid colour (was the invalid→black trap)")
                .arg(QString::fromLatin1(name)));
  };
  mustBeValid(d.colors.background, "background");
  mustBeValid(d.colors.chromeBackground, "chromeBackground");
  mustBeValid(d.colors.codeBackground, "codeBackground");
  mustBeValid(d.colors.codeBlockBackground, "codeBlockBackground");
  mustBeValid(d.colors.codeBorder, "codeBorder");
  mustBeValid(d.colors.highlight, "highlight");
  mustBeValid(d.colors.selection, "selection");
  mustBeValid(d.colors.hover, "hover");
  mustBeValid(d.colors.selected, "selected");
  mustBeValid(d.colors.chromeDisabled, "chromeDisabled");
  // On pixyll's white page the derived code chip must be a light grey, never black.
  require(d.colors.codeBackground.lightness() > 128,
          QStringLiteral("pixyll code background should be a light fill, not black (got %1)")
              .arg(d.colors.codeBackground.name(QColor::HexRgb)));
}

// Inline /* */ comments between declarations must not glue onto the next
// property and silently drop it. This is the a community theme (phycat) failure: a large
// :root with a comment before (nearly) every variable lost --bg-color and
// --text-color because the comment text became part of the property name.
void testCommentsDoNotDropDeclarations() {
  const char* css = R"(
:root {
  /* page note */
  --bg-color: #0f111a;
  /* text note */
  --text-color: #d6deeb;
  /* url value with an embedded ; and () must survive comment stripping */
  --icon: url("data:image/svg+xml;utf8,<svg viewBox='0 0 1 1'><path d='M0 0'/></svg>");
  --primary-color: #00f3ff;
}
)";
  const ThemeDefinition d = CssThemeMapper::fromCss(QString::fromUtf8(css), QStringLiteral("c"), QString());
  require(d.valid(), QStringLiteral("a theme with inline comments should still resolve text"));
  require(d.colors.background.name(QColor::HexRgb) == QStringLiteral("#0f111a"),
          QStringLiteral("a comment before --bg-color must not drop it"));
  require(d.colors.text.name(QColor::HexRgb) == QStringLiteral("#d6deeb"),
          QStringLiteral("a comment before --text-color must not drop it"));
  require(d.colors.accent.name(QColor::HexRgb) == QStringLiteral("#00f3ff"),
          QStringLiteral("a var after a url() with embedded punctuation must still resolve"));
}

// #write::before is a decorative texture/watermark overlay, not the page card.
// Many themes paint a vivid tint there (e.g. a community theme uses a purple
// --texture-mask-color). That tint must NOT leak into pageBackground — otherwise
// the page, status bar (backgroundColor) and derived chrome all turn that tint
// colour. isWrite must match only the bare #write, never its pseudo-elements.
void testWriteBeforeDoesNotLeakBackground() {
  const char* css = R"(
:root {
  --bg-color: #0f111a;
  --text-color: #d6deeb;
  --texture-mask-color: #bd93f9;
}
#write { max-width: 950px; margin: 0 auto; padding: 15px; color: var(--text-color); }
#write::before {
  content: "";
  position: absolute;
  background-color: var(--texture-mask-color);
  opacity: 0.05;
}
body::before { background-color: #bd93f9; }
)";
  const ThemeDefinition d = CssThemeMapper::fromCss(QString::fromUtf8(css), QStringLiteral("c"), QString());
  require(d.colors.background.name(QColor::HexRgb) == QStringLiteral("#0f111a"),
          QStringLiteral("#write::before's purple texture tint must not become the page background"));
  require(d.colors.chromeBackground.name(QColor::HexRgb) == QStringLiteral("#0f111a"),
          QStringLiteral("chrome derives from the real background, not the ::before tint"));
}

// color-mix() (CSS Color 4) must be evaluated, not fall through to the #hex
// scanner that grabs the operand verbatim. Community themes (phycat family)
// tint code/blockquote/table backgrounds with color-mix(in srgb, <color>,
// transparent N%); without evaluation the code background became full #00f3ff
// and the blockquote/code edge became full #bd93f9 — the "headings/code/
// blockquote turned cyan-purple" regression.
void testColorMixResolvesTintedBackground() {
  const char* css = R"(
:root { --primary-color: #00f3ff; }
#write { color: #d6deeb; }
#write code:not(.md-fencescode) {
  background-color: color-mix(in srgb, var(--primary-color), transparent 90%);
  border: 1px solid color-mix(in srgb, var(--primary-color), transparent 85%);
}
)";
  const ThemeDefinition d = CssThemeMapper::fromCss(QString::fromUtf8(css), QStringLiteral("c"), QString());
  // Background must be a faint cyan tint — the premultiplied blend keeps the
  // full chroma and only drops alpha, NOT the full-saturation operand.
  require(d.colors.codeBackground.alpha() != 255,
          QStringLiteral("color-mix(...,transparent 90%) must not collapse to full-opacity #00f3ff"));
  require(d.colors.codeBackground.red() == 0 && d.colors.codeBackground.green() == 243 &&
              d.colors.codeBackground.blue() == 255,
          QStringLiteral("premultiplied blend keeps full chroma (cyan), only alpha drops"));
  require(qAbs(d.colors.codeBackground.alphaF() - 0.1) < 0.02,
          QStringLiteral("transparent 90% yields ~10% alpha"));
  // The same evaluator resolves color-mix embedded in a border shorthand.
  require(d.colors.codeBorder.alpha() != 255 && d.colors.codeBorder.green() == 243,
          QStringLiteral("border shorthand color-mix(...) also resolves to a tinted edge"));

  // Explicit 50/50 mix of two opaque colours blends channels in equal parts.
  const ThemeDefinition half = CssThemeMapper::fromCss(
      QStringLiteral("#write { color: #000; } #write code { background-color: color-mix(in srgb, #ff0000, #0000ff); }"),
      QStringLiteral("h"), QString());
  require(half.colors.codeBackground.red() > 120 && half.colors.codeBackground.blue() > 120 &&
              half.colors.codeBackground.green() < 10,
          QStringLiteral("50/50 red+blue mix lands near magenta (#7f007f-ish)"));
}

// Interactive states (:hover/:focus/:active/:visited) and the community editor's
// .md-focus class describe appearances that never apply to a static render (no
// pointer-over or focused block on the painted page). They outrank the base rule
// — higher specificity AND later source order — so without filtering them out,
// `code:hover { background: var(--primary-color) }` (a FULL-SATURATION colour)
// overrides the base tint and becomes the DEFAULT code background. That is the
// real "headings/code/blockquote turned cyan-purple" regression on the community
// phycat family: the leak bypasses color-mix() entirely because the winning rule
// is a plain var(), not a tint. The base (non-interactive) rule must win.
void testInteractiveStatesDoNotLeak() {
  const char* css = R"(
:root { --primary-color: #00f3ff; }
#write { color: #d6deeb; }
#write code:not(.md-fencescode) {
  background-color: color-mix(in srgb, var(--primary-color), transparent 90%);
}
#write code:not(.md-fencescode):hover {
  background-color: var(--primary-color);
}
#write code:not(.md-fencescode).md-focus {
  background-color: var(--primary-color);
}
#write blockquote {
  background-color: rgba(0, 0, 0, .2);
  border: 1px solid color-mix(in srgb, #2979ff, transparent 70%);
}
#write blockquote:hover {
  border-color: var(--primary-color);
  background-color: color-mix(in srgb, var(--primary-color), transparent 95%);
}
#write h2 { color: #d6deeb; }
#write h2:hover { color: var(--primary-color); }
)";
  const ThemeDefinition d = CssThemeMapper::fromCss(QString::fromUtf8(css), QStringLiteral("c"), QString());
  // Inline-code background must be the BASE color-mix tint (~10% alpha), NOT the
  // full-saturation #00f3ff from either the :hover or .md-focus variant.
  require(qAbs(d.colors.codeBackground.alphaF() - 0.1) < 0.02,
          QStringLiteral("code bg must be the base tint; :hover/.md-focus full-saturation must not leak"));
  // Blockquote background must stay the BASE rgba(0,0,0,.2) (dark), not the
  // :hover cyan tint.
  require(d.colors.blockquoteBackground.red() == 0 && d.colors.blockquoteBackground.green() == 0 &&
              d.colors.blockquoteBackground.blue() == 0,
          QStringLiteral("blockquote bg must be the base dark rgba(0,0,0,.2), not the :hover cyan"));
  // Quote border must be the BASE faint blue (#2979ff chroma, low alpha), not
  // the :hover full primary-color (green 243 vs base 121 distinguishes them).
  require(d.colors.quoteBorder.green() == 121 && d.colors.quoteBorder.alpha() != 255,
          QStringLiteral("quote border must be the base faint #2979ff, not the :hover full primary-color"));
  // Heading text colour must be the BASE ink, not the :hover accent.
  require(d.typography.headingColor[1].name(QColor::HexRgb) == QStringLiteral("#d6deeb"),
          QStringLiteral("h2 colour must be the base, not the :hover primary-color"));
}

// Export-shell selectors describe a different DOM (`.typora-export #write`,
// export sidebar, etc.). Muffin's live editor has no such ancestor, so these
// rules must not enter the live semantic cascade as higher-specificity `#write`
// or heading rules. This is the narrow-column root cause: an export
// `width: 90%` rule beat the live `max-width: 950px` and collapsed the page.
void testExportSelectorsDoNotLeak() {
  const char* css = R"(
:root { --bg-color: #0f111a; }
#write {
  background: #111111;
  max-width: 950px;
  margin: 0 auto;
  padding: 15px;
  color: #d6deeb;
}
.typora-export #write {
  background: #ff00ff;
  max-width: 700px;
  width: 90%;
  padding: 80px;
}
body.typora-export #write { border-radius: 44px; }
#write h2 { color: #d6deeb; }
.typora-export h2 { color: #ff00ff; }
.typora-export h2::after { content: ''; background: #ff00ff; width: 999px; height: 999px; }
.typora-export-sidebar + #write { box-shadow: 0 4px 20px #ff00ff; }
)";
  const ThemeDefinition d = CssThemeMapper::fromCss(QString::fromUtf8(css), QStringLiteral("export-leak"), QString());
  require(d.page.pageBackground.name(QColor::HexRgb) == QStringLiteral("#111111"),
          QStringLiteral("export #write background must not override live #write"));
  require(qAbs(d.page.pageMaxWidth - 950.0) < 0.01,
          QStringLiteral("export #write width/max-width must not override live max-width"));
  require(qAbs(d.page.pagePadding.left() - 15.0) < 0.01 && qAbs(d.page.pagePadding.right() - 15.0) < 0.01,
          QStringLiteral("export #write padding must not override live padding"));
  require(d.page.pageMarginExplicit && d.page.pageMargin.isNull(),
          QStringLiteral("live margin: 0 auto should still produce an explicit zero margin"));
  require(d.page.pageBorderRadius == 0.0,
          QStringLiteral("body.typora-export #write radius must not leak"));
  require(d.typography.headingColor[1].name(QColor::HexRgb) == QStringLiteral("#d6deeb"),
          QStringLiteral("export h2 color must not override live h2 color"));
  for (const PseudoElementRule& rule : d.decorations.pseudos) {
    require(!(rule.host == QStringLiteral("h2") && rule.pseudo == QStringLiteral("after")),
            QStringLiteral("export-only h2::after decoration must not be captured for live editor"));
  }
}

void testPageWidthPercentDoesNotCollapse() {
  const ThemeDefinition capped = CssThemeMapper::fromCss(
      QStringLiteral("#write { width: 90%; max-width: 950px; color: #000; }"),
      QStringLiteral("capped"), QString());
  require(qAbs(capped.page.pageMaxWidth - 950.0) < 0.01,
          QStringLiteral("concrete max-width should cap width:90%, not lose to it"));

  const ThemeDefinition fill = CssThemeMapper::fromCss(
      QStringLiteral("#write { width: 90%; color: #000; }"),
      QStringLiteral("fill"), QString());
  require(fill.page.pageMaxWidth > 10000.0,
          QStringLiteral("width:90% should be viewport-fill sentinel, not em-relative tiny pixels"));
}

// The built-in themes are now CSS resources at :/themes/<id>.css. This guards
// that each resource loads to a valid definition AND that it reproduces the
// matching RenderTheme factory bit-for-bit — the same property
// testFromDefinitionReproducesBuiltIns checks via ThemeManager, but here at the
// CSS-source level and pinning the :/ resource registration.
void testBuiltInCssResourcesReproduceFactories() {
  struct Entry { const char* id; RenderTheme factory; };
  const Entry entries[] = {
      {"github", RenderTheme::github()},     {"newsprint", RenderTheme::newsprint()},
      {"night", RenderTheme::night()},       {"pixyll", RenderTheme::pixyll()},
      {"whitey", RenderTheme::whitey()},
  };
  for (const Entry& e : entries) {
    const QString id = QString::fromLatin1(e.id);
    const ThemeDefinition d = ThemeDefinition::fromCss(QStringLiteral(":/themes/%1.css").arg(id), id);
    require(d.valid(), QStringLiteral("%1 built-in CSS should load to a valid definition").arg(id));
    const RenderTheme viaDef = RenderTheme::fromDefinition(d);
    require(viaDef.backgroundColor().name() == e.factory.backgroundColor().name(),
            QStringLiteral("%1 background via CSS should match factory").arg(id));
    require(viaDef.textColor().name() == e.factory.textColor().name(),
            QStringLiteral("%1 text via CSS should match factory").arg(id));
    require(viaDef.mutedTextColor().name() == e.factory.mutedTextColor().name(),
            QStringLiteral("%1 muted via CSS should match factory").arg(id));
    require(viaDef.linkColor().name() == e.factory.linkColor().name(),
            QStringLiteral("%1 link via CSS should match factory").arg(id));
    require(viaDef.codeBackgroundColor().name() == e.factory.codeBackgroundColor().name(),
            QStringLiteral("%1 code bg via CSS should match factory").arg(id));
    require(viaDef.highlightBackgroundColor().name() == e.factory.highlightBackgroundColor().name(),
            QStringLiteral("%1 highlight via CSS should match factory").arg(id));
    require(viaDef.selectionColor().name() == e.factory.selectionColor().name(),
            QStringLiteral("%1 selection via CSS should match factory").arg(id));
    require(viaDef.spellCheckColor().name() == e.factory.spellCheckColor().name(),
            QStringLiteral("%1 spell-check via CSS should match factory").arg(id));
  }
}

// @font-face declared in an @import'd base sheet must be captured, and its src
// url() resolved relative to the BASE file's directory (not the top file's). This
// is the crux of multi-file themes like phycat: phycat-abyss.css @imports
// phycat/phycat.dark.css, whose @font-face url(Cascadia-Code-Regular.ttf) lives
// next to phycat.dark.css. Without per-file baseDir threading the font would be
// sought in the top file's dir and not found.
void testFontFaceCaptureAcrossImport() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("could not create temp dir"));
  const QString root = dir.path();
  const QString subDir = root + QStringLiteral("/sub");
  require(QDir().mkpath(subDir), QStringLiteral("mkpath sub"));
  {
    QFile base(subDir + QStringLiteral("/base.css"));
    require(base.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("write base.css"));
    base.write("@font-face { font-family: \"TestFont\"; src: url(font.ttf) format('truetype'); }");
    base.close();
    QFile font(subDir + QStringLiteral("/font.ttf"));  // must exist to be captured
    require(font.open(QIODevice::WriteOnly), QStringLiteral("write font.ttf"));
    font.write("dummy");
    font.close();
  }
  const QString topText = QStringLiteral("@import url(./sub/base.css);\n#write { color: #000; }");
  const CssThemeSheet sheet = CssThemeParser::parse(topText, root);
  require(sheet.fontFaces().size() == 1,
          QStringLiteral("the @font-face in the @import'd base should be captured"));
  require(sheet.fontFaces().at(0).family == QStringLiteral("TestFont"),
          QStringLiteral("font-family should be captured with quotes stripped"));
  require(sheet.fontFaces().at(0).srcPath == QDir::cleanPath(subDir + QStringLiteral("/font.ttf")),
          QStringLiteral("src must resolve relative to the BASE file's dir, not the top file's"));
}

// localResourcePaths discovers every local file a theme transitively references:
// @import'd base sheets (recursed) + @font-face/url() resources. data: and
// remote (http) urls are excluded; missing files are dropped. Used at import to
// mirror the theme's folder.
void testLocalResourcePaths() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("could not create temp dir"));
  const QString root = dir.path();
  require(QDir().mkpath(root + QStringLiteral("/sub")), QStringLiteral("mkpath sub"));
  {
    QFile top(root + QStringLiteral("/top.css"));
    require(top.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("write top.css"));
    top.write("@import url(./sub/base.css);\n#write { color: #000; }");
    top.close();
    QFile base(root + QStringLiteral("/sub/base.css"));
    require(base.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("write base.css"));
    // local font (kept), data: uri (dropped), remote (dropped)
    base.write("@font-face { font-family: F; src: url(font.ttf); }\n"
               "#write { background: url(data:image/png;base64,AAAA); }\n"
               "#write a { background: url(https://example.com/x.png); }");
    base.close();
    QFile font(root + QStringLiteral("/sub/font.ttf"));
    require(font.open(QIODevice::WriteOnly), QStringLiteral("write font.ttf"));
    font.write("x");
    font.close();
  }
  QFile topFile(root + QStringLiteral("/top.css"));
  require(topFile.open(QIODevice::ReadOnly), QStringLiteral("read top.css"));
  const QString topText = QString::fromUtf8(topFile.readAll());
  topFile.close();
  const QStringList res = CssThemeParser::localResourcePaths(topText, root);
  const QString baseCanon = QDir::cleanPath(root + QStringLiteral("/sub/base.css"));
  const QString fontCanon = QDir::cleanPath(root + QStringLiteral("/sub/font.ttf"));
  require(res.contains(baseCanon), QStringLiteral("@import'd base sheet should be discovered"));
  require(res.contains(fontCanon), QStringLiteral("@font-face font should be discovered"));
  for (const QString& p : res) {
    require(!p.contains(QStringLiteral("data:")) && !p.contains(QStringLiteral("http")),
            QStringLiteral("data:/remote urls must be excluded"));
  }
}

// installCssTheme mirrors a multi-file theme into a dest dir: the top .css
// verbatim (preserving @import) plus every referenced local file with its
// relative path preserved, so the installed theme resolves identically to the
// source folder at runtime.
void testInstallCssThemeMirrorsFolder() {
  QTemporaryDir srcDir;
  QTemporaryDir destDir;
  require(srcDir.isValid() && destDir.isValid(), QStringLiteral("could not create temp dirs"));
  const QString src = srcDir.path();
  require(QDir().mkpath(src + QStringLiteral("/sub")), QStringLiteral("mkpath sub"));
  {
    QFile top(src + QStringLiteral("/MyTheme.css"));
    require(top.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("write top"));
    top.write("@import url(./sub/base.css);\n#write { color: #000; }");
    top.close();
    QFile base(src + QStringLiteral("/sub/base.css"));
    require(base.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("write base"));
    base.write("@font-face { font-family: F; src: url(font.ttf); }\n#write { background: #fff; }");
    base.close();
    QFile font(src + QStringLiteral("/sub/font.ttf"));
    require(font.open(QIODevice::WriteOnly), QStringLiteral("write font"));
    font.write("x");
    font.close();
  }
  require(ThemeManager::installCssTheme(src + QStringLiteral("/MyTheme.css"), destDir.path()),
          QStringLiteral("installCssTheme should succeed for a readable top file"));
  // Top file is lowercased; @import'd base + font mirrored with relative paths.
  require(QFileInfo::exists(destDir.path() + QStringLiteral("/mytheme.css")),
          QStringLiteral("top file should be copied (lowercased stem)"));
  require(QFileInfo::exists(destDir.path() + QStringLiteral("/sub/base.css")),
          QStringLiteral("@import'd base sheet should be mirrored"));
  require(QFileInfo::exists(destDir.path() + QStringLiteral("/sub/font.ttf")),
          QStringLiteral("@font-face font should be mirrored"));
  // Runtime: the installed top file resolves its @import and @font-face against
  // the DEST dir (proving the mirror is self-consistent).
  QFile installedTop(destDir.path() + QStringLiteral("/mytheme.css"));
  require(installedTop.open(QIODevice::ReadOnly), QStringLiteral("read installed top"));
  const QString installedText = QString::fromUtf8(installedTop.readAll());
  installedTop.close();
  const CssThemeSheet sheet = CssThemeParser::parse(installedText, destDir.path());
  require(sheet.fontFaces().size() == 1 && sheet.fontFaces().at(0).family == QStringLiteral("F"),
          QStringLiteral("installed theme's @font-face should resolve from the dest dir"));
  require(sheet.fontFaces().at(0).srcPath == QDir::cleanPath(destDir.path() + QStringLiteral("/sub/font.ttf")),
          QStringLiteral("font src should resolve to the mirrored dest file"));
}

// Intrinsic-sizing keywords (max-content/fit-content/min-content/none) mean
// "don't constrain the column". They can't be a pixel length, so without
// handling they parse to 0 and the theme collapses to the 860px default.
// They should instead be treated as unbounded (large sentinel → fills viewport).
void testIntrinsicWidthIsUnbounded() {
  for (const char* kw : {"max-content", "fit-content", "min-content", "none"}) {
    const QString css = QStringLiteral("#write { max-width: %1; }").arg(QString::fromLatin1(kw));
    const ThemeDefinition d = CssThemeMapper::fromCss(css, QStringLiteral("k"), QString());
    require(d.page.pageMaxWidth > 10000.0,
            QStringLiteral("max-width: %1 should be unbounded, not the 860px default").arg(QString::fromLatin1(kw)));
  }
  // A concrete length still parses normally (regression guard).
  const ThemeDefinition d = CssThemeMapper::fromCss(QStringLiteral("#write { max-width: 950px; }"),
                                                    QStringLiteral("k"), QString());
  require(qAbs(d.page.pageMaxWidth - 950.0) < 0.01, QStringLiteral("max-width: 950px should map to 950"));
}

// Regression guard for the "narrow window squeezes content into a thin column
// with huge side margins" symptom reported on multi-file CSS themes (phycat).
// Such themes declare `#write { max-width: <px>; margin: 0 auto; padding: <px> }`
// in an @imported base, with the viewport background coming from a `--bg-color`
// root variable rather than a body/html rule. The page model MUST extract as a
// real card (pagePadding present + pageMarginExplicit + the declared max-width):
//   • pagePadding present  → RenderTheme treats it as a CSS page box
//     (cssPageBox=true), whose horizontalInset = max(margin.left, margin.right).
//   • margin: 0 auto → margin (0,0,0,0), so horizontalInset = 0 → the column
//     fills the viewport at every width (matching the theme's intent), NOT the legacy
//     flat-document ~8%-of-viewport gutter that produces the "thin column"
//     look on a 400–600px window.
// If any of these regress (e.g. the @imported #write rule stops reaching the
// mapper, or `margin: 0 auto` stops parsing to a zero box), the theme silently
// falls into the legacy path and the squeeze returns.
void testWriteMarginAutoProducesCardModel() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("temp dir for multi-file theme"));
  QFile base(dir.path() + QStringLiteral("/base.css"));
  require(base.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("open base.css"));
  base.write("#write {\n"
             "  max-width: 950px;\n"
             "  margin: 0 auto;\n"
             "  padding: 15px;\n"
             "  color: #d6deeb;\n"
             "}\n");
  base.close();
  QFile top(dir.path() + QStringLiteral("/top.css"));
  require(top.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("open top.css"));
  top.write("@import url(./base.css);\n"
            ":root { --bg-color: #0f111a; }\n");
  top.close();

  const ThemeDefinition d = ThemeDefinition::fromCss(top.fileName(), QStringLiteral("phycat-like"));
  require(d.valid(), QStringLiteral("multi-file #write theme should be valid"));
  // The viewport background comes from --bg-color, not a body rule — confirm the
  // variable fallback still lights up so the page card model has a canvas.
  require(d.page.viewportBackground == QColor(QStringLiteral("#0f111a")),
          QStringLiteral("viewport background should resolve from --bg-color"));
  // The three page-model fields that make this a CSS card box (cssPageBox=true).
  require(qAbs(d.page.pageMaxWidth - 950.0) < 0.01,
          QStringLiteral("#write max-width should extract as 950, not collapse to the 860 default"));
  require(!d.page.pagePadding.isNull(),
          QStringLiteral("#write padding should extract as a non-null box so cssPageBox is true"));
  require(qAbs(d.page.pagePadding.left() - 15.0) < 0.01 && qAbs(d.page.pagePadding.right() - 15.0) < 0.01,
          QStringLiteral("#write padding: 15px should extract as 15px sides"));
  require(d.page.pageMarginExplicit,
          QStringLiteral("#write margin: 0 auto should mark the margin explicit (zero box, not legacy inset)"));
  require(d.page.pageMargin.isNull(),
          QStringLiteral("margin: 0 auto should parse to a zero box (horizontalInset 0 → fills viewport)"));
}

// CSS gradient parsing → GradientSpec. Stop colours resolve via the same path as
// every theme colour (var/color-mix/rgb/hex/named).
void testParseGradientLinear() {
  QHash<QString, QString> vars;
  const GradientSpec g = CssThemeMapper::parseGradient(
      QStringLiteral("linear-gradient(45deg, #ffffff, #000000)"), vars);
  require(g.kind == GradientSpec::Kind::Linear, QStringLiteral("linear-gradient should parse as Linear"));
  require(qAbs(g.angleDeg - 45.0) < 0.01, QStringLiteral("45deg should set angleDeg=45"));
  require(g.stops.size() == 2, QStringLiteral("two stops expected"));
  require(g.stops.at(0).color == QColor(QStringLiteral("#ffffff")), QStringLiteral("first stop #ffffff"));
  require(qAbs(g.stops.at(0).position - 0.0) < 0.01, QStringLiteral("first stop implicit position 0"));
  require(qAbs(g.stops.at(1).position - 1.0) < 0.01, QStringLiteral("last stop implicit position 1"));
}

void testParseGradientRadialTransparent() {
  QHash<QString, QString> vars;
  const GradientSpec g = CssThemeMapper::parseGradient(
      QStringLiteral("radial-gradient(circle at center, rgba(0,243,255,0.15), transparent 70%)"), vars);
  require(g.kind == GradientSpec::Kind::Radial, QStringLiteral("radial-gradient should parse as Radial"));
  require(qAbs(g.radialCenter.x() - 0.5) < 0.01 && qAbs(g.radialCenter.y() - 0.5) < 0.01,
          QStringLiteral("'at center' should set center (0.5,0.5)"));
  require(g.stops.size() == 2, QStringLiteral("two stops expected"));
  require(g.stops.at(1).color == QColor(Qt::transparent), QStringLiteral("'transparent' stop → transparent color"));
  require(qAbs(g.stops.at(1).position - 0.70) < 0.01, QStringLiteral("explicit 70% position honoured"));
}

void testParseGradientVarAndColorMixStops() {
  QHash<QString, QString> vars;
  vars.insert(QStringLiteral("--accent"), QStringLiteral("#ff7096"));
  const GradientSpec g = CssThemeMapper::parseGradient(
      QStringLiteral("linear-gradient(to right, var(--accent), color-mix(in srgb, #000 50%, #fff))"), vars);
  require(g.kind == GradientSpec::Kind::Linear, QStringLiteral("linear-gradient should parse"));
  require(qAbs(g.angleDeg - 90.0) < 0.01, QStringLiteral("'to right' should set angleDeg=90"));
  require(g.stops.at(0).color == QColor(QStringLiteral("#ff7096")), QStringLiteral("var() stop resolves"));
  require(g.stops.at(1).color.isValid(), QStringLiteral("color-mix stop resolves to a valid colour"));
}

// ::before/::after capture → PseudoElementRule, grouped by host.
void testPseudoExtractionGroupsByHost() {
  const QString css = QStringLiteral(
      ":root { --tint: #bd93f9; --glow: #00f3ff; }"
      "body { background: #0f111a; color: #d6deeb; }"
      "#write h2::after { background: linear-gradient(to right, var(--glow), transparent); height: 2px; }"
      "blockquote::before { content: \"X\"; color: #f00; }"
      "#write { max-width: 950px; padding: 15px; margin: 0 auto; }");
  const ThemeDefinition d = CssThemeMapper::fromCss(css, QStringLiteral("t"), QString());
  int h2After = 0, bqBefore = 0;
  for (const PseudoElementRule& r : d.decorations.pseudos) {
    if (r.host == QStringLiteral("h2") && r.pseudo == QStringLiteral("after")) {
      ++h2After;
      require(r.background.kind == GradientSpec::Kind::Linear, QStringLiteral("h2::after background is the gradient"));
      require(qAbs(r.size.height() - 2.0) < 0.01, QStringLiteral("h2::after height: 2px captured"));
    }
    if (r.host == QStringLiteral("blockquote") && r.pseudo == QStringLiteral("before")) {
      ++bqBefore;
      require(r.content == QStringLiteral("X"), QStringLiteral("blockquote::before content literal captured"));
      require(r.color == QColor(QStringLiteral("#ff0000")), QStringLiteral("blockquote::before color captured"));
    }
  }
  require(h2After == 1, QStringLiteral("exactly one h2::after rule"));
  require(bqBefore == 1, QStringLiteral("exactly one blockquote::before rule"));
}

// #write::before texture overlay: background-color + mask-image + opacity + mask-size.
void testWriteBeforeTextureCapture() {
  const QString css = QStringLiteral(
      ":root { --tint: #bd93f9; }"
      "body { background: #0f111a; color: #d6deeb; }"
      "#write { max-width: 950px; padding: 15px; margin: 0 auto; }"
      "#write::before { content: \"\"; background-color: var(--tint); opacity: 0.05;"
      "  mask-image: radial-gradient(#fff 1px, transparent 1px); mask-size: 20px 20px; }");
  const ThemeDefinition d = CssThemeMapper::fromCss(css, QStringLiteral("t"), QString());
  const PseudoElementRule* rule = nullptr;
  for (const PseudoElementRule& r : d.decorations.pseudos) {
    if (r.host == QStringLiteral("#write") && r.pseudo == QStringLiteral("before")) { rule = &r; }
  }
  require(rule != nullptr, QStringLiteral("#write::before rule should be captured"));
  require(rule->maskTint == QColor(QStringLiteral("#bd93f9")), QStringLiteral("texture tint from background-color"));
  require(rule->maskPattern.kind == GradientSpec::Kind::Radial, QStringLiteral("mask-image parsed as radial gradient"));
  require(qAbs(rule->opacity - 0.05) < 0.001, QStringLiteral("opacity 0.05 captured"));
  require(qAbs(rule->maskTile.width() - 20.0) < 0.01 && qAbs(rule->maskTile.height() - 20.0) < 0.01,
          QStringLiteral("mask-size 20px 20px captured"));
}

// Phase 3: inline link ::before mask icon (svgFromMask flag) + mark element
// background-image gradient.
void testLinkBeforeMaskIconAndMarkGradient() {
  const QString css = QStringLiteral(
      "body { background:#0f111a; color:#d6deeb; }"
      "#write { max-width:950px; padding:15px; margin:0 auto; }"
      "#write a::before { content:''; background-color:#00f3ff;"
      "  -webkit-mask:url(\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg'><path d='M0 0L10 10'/></svg>\") center/contain; }"
      "#write mark { background-image:linear-gradient(to top, rgba(0,243,255,0.5), transparent); }");
  const ThemeDefinition d = CssThemeMapper::fromCss(css, QStringLiteral("t"), QString());
  const PseudoElementRule* link = nullptr;
  for (const PseudoElementRule& r : d.decorations.pseudos) {
    if (r.host == QStringLiteral("a") && r.pseudo == QStringLiteral("before")) { link = &r; }
  }
  require(link != nullptr, QStringLiteral("a::before rule should be captured"));
  require(!link->svgData.isEmpty(), QStringLiteral("a::before SVG should decode from the mask data URI"));
  require(link->svgFromMask, QStringLiteral("a::before svgFromMask should be true (mask source)"));
  require(link->backgroundColor == QColor(QStringLiteral("#00f3ff")), QStringLiteral("a::before background-color tint captured"));
  bool markLinear = false;
  for (const ElementBackground& eb : d.decorations.backgrounds) {
    if (eb.host == QStringLiteral("mark")) { markLinear = eb.gradient.kind == GradientSpec::Kind::Linear; }
  }
  require(markLinear, QStringLiteral("mark background-image linear gradient should be captured"));
}

// Phase 4: :hover box-shadow glow capture + `transition` duration.
void testHoverEffectAndTransitionCapture() {
  const QString css = QStringLiteral(
      "body { background:#0f111a; color:#d6deeb; }"
      "#write { max-width:950px; padding:15px; margin:0 auto; }"
      "#write blockquote { transition: all .3s ease; }"
      "#write blockquote:hover { box-shadow: 0 0 15px rgba(0,243,255,0.3); }");
  const ThemeDefinition d = CssThemeMapper::fromCss(css, QStringLiteral("t"), QString());
  const HoverEffect* he = nullptr;
  for (const HoverEffect& h : d.decorations.hoverEffects) {
    if (h.host == QStringLiteral("blockquote")) { he = &h; }
  }
  require(he != nullptr, QStringLiteral("blockquote :hover glow should be captured"));
  require(he->glowColor.isValid(), QStringLiteral("hover box-shadow glow colour resolves"));
  require(qAbs(he->glowBlur - 15.0) < 0.01, QStringLiteral("hover box-shadow blur 15px"));
  bool trans = false;
  for (const TransitionSpec& t : d.decorations.transitions) {
    if (t.host == QStringLiteral("blockquote") && qAbs(t.durationMs - 300.0) < 0.01) { trans = true; }
  }
  require(trans, QStringLiteral("blockquote transition .3s should parse to 300ms"));
}

// Phase 5: @keyframes capture + `animation:` shorthand parse.
void testKeyframesAndAnimationParse() {
  const QString css = QStringLiteral(
      "body { background:#0f111a; color:#d6deeb; }"
      "#write { max-width:950px; padding:15px; margin:0 auto; }"
      "@keyframes pulse { 0% { opacity: 1 } 50% { opacity: .3 } 100% { opacity: 1 } }"
      "#write h2 { animation: pulse 2s linear infinite; }");
  const ThemeDefinition d = CssThemeMapper::fromCss(css, QStringLiteral("t"), QString());
  const KeyframesDef* kf = nullptr;
  for (const KeyframesDef& k : d.decorations.keyframes) {
    if (k.name == QStringLiteral("pulse")) { kf = &k; }
  }
  require(kf != nullptr, QStringLiteral("@keyframes pulse should be captured"));
  require(kf->stops.size() == 3, QStringLiteral("three stops (0/50/100%)"));
  require(kf->stops.at(1).position == 0.5, QStringLiteral("stops sorted; middle is 50%"));
  const AnimationDef* a = nullptr;
  for (const AnimationDef& an : d.decorations.animations) {
    if (an.host == QStringLiteral("h2")) { a = &an; }
  }
  require(a != nullptr, QStringLiteral("h2 animation should be captured"));
  require(a->name == QStringLiteral("pulse"), QStringLiteral("animation name pulse"));
  require(qAbs(a->durationMs - 2000.0) < 0.01, QStringLiteral("duration 2s = 2000ms"));
  require(a->iterations == -1, QStringLiteral("infinite iterations"));
  require(a->easing == QStringLiteral("linear"), QStringLiteral("easing linear"));
}

// Phase 5: keyframe sampling/interpolation at 0 / 0.25 / 0.5 phase.
void testKeyframeSampling() {
  KeyframesDef kf;
  kf.name = QStringLiteral("pulse");
  KeyframeStop s0; s0.position = 0.0; s0.declarations.insert(QStringLiteral("opacity"), QStringLiteral("1.0"));
  KeyframeStop s1; s1.position = 0.5; s1.declarations.insert(QStringLiteral("opacity"), QStringLiteral("0.3"));
  KeyframeStop s2; s2.position = 1.0; s2.declarations.insert(QStringLiteral("opacity"), QStringLiteral("1.0"));
  kf.stops = {s0, s1, s2};
  const AnimatedSample at0 = KeyframeSampler::sampleAtPhase(kf, 0.0);
  require(at0.hasOpacity && qAbs(at0.opacity - 1.0) < 0.01, QStringLiteral("phase 0 → opacity 1"));
  const AnimatedSample atHalf = KeyframeSampler::sampleAtPhase(kf, 0.5);
  require(atHalf.hasOpacity && qAbs(atHalf.opacity - 0.3) < 0.01, QStringLiteral("phase 0.5 → opacity 0.3"));
  const AnimatedSample atQ = KeyframeSampler::sampleAtPhase(kf, 0.25);
  require(atQ.hasOpacity && qAbs(atQ.opacity - 0.65) < 0.01, QStringLiteral("phase 0.25 → opacity ~0.65 (lerp)"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc >= 2, QStringLiteral("Fixture path argument is required"));
  const QString fixture = QString::fromLocal8Bit(argv[1]);

#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testResolveVars);
  RUN_TEST(testSplitCommas);
  RUN_TEST(testSampleTheme);
  RUN_TEST(testWhiteyTypographySemantics);
  RUN_TEST(testPureVariableTheme);
  RUN_TEST(testCascadeBeatsVariable);
  RUN_TEST(testTextOnlySynthesisesBackground);
  RUN_TEST(testPixyllHasNoInvalidBlackProneTokens);
  RUN_TEST(testCommentsDoNotDropDeclarations);
  RUN_TEST(testWriteBeforeDoesNotLeakBackground);
  RUN_TEST(testColorMixResolvesTintedBackground);
  RUN_TEST(testInteractiveStatesDoNotLeak);
  RUN_TEST(testExportSelectorsDoNotLeak);
  RUN_TEST(testPageWidthPercentDoesNotCollapse);
  RUN_TEST(testBuiltInCssResourcesReproduceFactories);
  RUN_TEST(testFontFaceCaptureAcrossImport);
  RUN_TEST(testLocalResourcePaths);
  RUN_TEST(testInstallCssThemeMirrorsFolder);
  RUN_TEST(testIntrinsicWidthIsUnbounded);
  RUN_TEST(testWriteMarginAutoProducesCardModel);
  RUN_TEST(testParseGradientLinear);
  RUN_TEST(testParseGradientRadialTransparent);
  RUN_TEST(testParseGradientVarAndColorMixStops);
  RUN_TEST(testPseudoExtractionGroupsByHost);
  RUN_TEST(testWriteBeforeTextureCapture);
  RUN_TEST(testLinkBeforeMaskIconAndMarkGradient);
  RUN_TEST(testHoverEffectAndTransitionCapture);
  RUN_TEST(testKeyframesAndAnimationParse);
  RUN_TEST(testKeyframeSampling);
#undef RUN_TEST
  runTest("testMistBlueFixture", [&] { testMistBlueFixture(fixture); });
  return 0;
}
