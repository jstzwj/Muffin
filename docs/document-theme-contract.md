# Document theme contract

Document CSS is parsed once and retained in `ThemeDefinition::sourceSheet`.
`CssComputedStyleEngine` evaluates media conditions, custom properties,
inheritance, declaration order, specificity and `!important`. Box and border
shorthands enter the same cascade as their longhands. Computed element and root
font sizes provide the bases for `em` and `rem`; user text size and viewport zoom
are applied separately.

Lengths distinguish missing, invalid and valid values (including zero). Invalid
literal declarations are ignored before cascading; a winning declaration whose
`var()` substitution fails becomes `unset` at computed-value time. Box, border,
font and background shorthands reset their omitted longhands. Custom-property
names are case-sensitive; aliases are computed in the defining element before
inheritance, and dependency cycles invalidate their participating variables.

Computed box lengths keep a pixel component and a percentage component.
Markdown flow and HTML layout supply the actual containing content width for
margins, padding and mixed `calc()` expressions; vertical margin/padding
percentages also use this width. Font-relative units are frozen before
inheritance. Dimensional arithmetic rejects length-times-length and division by
zero. Border style initially means `none`; a visible style with no width uses
`medium`. HTML passes borders and `box-sizing` to Yoga.

Computed-style caches are scoped to the engine generation and opt-in immutable
element snapshots. Temporary elements are uncached. Editing/rebuilding clears
both computed and projected caches; live projections distinguish node objects
and requested style keys, including nodes with identical IDs.
Equivalent matched declarations and inherited inputs reuse immutable cascade,
computed-value, native projection and font results within that generation.
Cache hash buckets also compare their full inputs before reuse.
Equivalent inline runs share immutable used-style snapshots rather than keeping
a full box/paint recipe in every text span; positioned fragments own their geometry.

`:is()` and `:where()` accept forgiving selector lists; `:not()` accepts selector
lists including complex selectors. Their specificity follows the maximum
argument rule, with zero specificity for `:where()`. Media conditions include
width/height/resolution ranges, logical conditions, pointer/hover and reduced
motion preferences. This extends the native CSS subset; complete flex/grid,
cascade layers and browser formatting contexts remain outside this contract.

`CssThemeMapper::projectComputedStyle` projects this result into native text,
paint and box values. Theme prototypes, live Markdown nodes and HTML boxes use
this projection. Mermaid uses the same declaration cascade and retains CSS-wide
values until its SVG presentation and inheritance projection.

The mapper does not match declarations or run a second cascade. Prototypes and
live nodes share the `html > body > #write` host and semantic element ancestry,
including table sections and Markdown formatting around inline HTML. Anonymous
HTML fragment containers are transparent to selectors. Simple HTML tags retain
the Markdown editing projection, while their fonts come from the actual style
tree. Legacy JSON/manual theme fields are lowered into CSS by
`LegacyThemeAdapter` before entering this pipeline; they are not runtime geometry
fallbacks. Document defaults are compiled into the UI library, so all consumers
have the same defaults without relying on an application's resource initializer.

`RenderTheme::updateForViewport` evaluates the retained sheet using CSS viewport
dimensions (physical layout dimensions divided by zoom). Resizing the editor
rebuilds layout when this environment changes. Page width distinguishes the CSS
outer box, border, padding and text area; an explicit user width overrides the
theme width. `resources/themes/document-base.css` supplies document defaults
before authored CSS and remains independent of editor chrome.

`LayoutBox` stores the computed style, used lengths, font and border, padding,
content and overflow rectangles produced during layout. Paragraphs, headings,
code fences, quotes, table/row/cell boxes and embedded HTML use this snapshot. Painting, clicking,
caret placement and selection read its geometry rather than reconstructing it
from theme tokens. All four heading borders and padding sides enter the flow
box; fixed, minimum/maximum and fit-content sizes change actual occupied space.
Inline code and keyboard boxes reserve horizontal
padding and border width in the text layout. Their generated layout spacers map
to zero source and visible-text length, so they affect wrapping and cursor
coordinates without entering copy or saved Markdown. Format ranges address the
layout buffer, which can include these spacers and collapsed math/image atoms.
Code blocks read `pre` typography and line height; inline code reads `code`.
Table header/body cells use actual `th`/`td` styles, including their individual
fonts, line heights, borders and padding. Inline HTML code/keyboard boxes preserve
local style attributes, and nested text spans reserve their shared box padding
once.
HTML block code/keyboard runs use the same box painter and reserve their edges
in their text buffer; metadata keeps content offsets separate from layout offsets.
Lazy promotion replaces both leading and trailing prototype margins with live
node margins.
Inline formula flow includes the actual inner math bounds when they exceed
KaTeX's nominal outer strut, and its paint/selection ranges use those same bounds.

Editing invalidates the style generation and sparse tree together. When a theme
uses structural selectors, already materialized blocks compare their saved style
fingerprints with the new tree and rebuild when affected, including inline and
table-cell styles. Unmaterialized blocks read fresh styles on promotion. Embedded
HTML fragments are conservatively rebuilt after structural edits because their
descendant tree is currently temporary. This preserves correctness while keeping
the Markdown tree sparse; it is not a full browser dependency graph.

Bundled webfonts are converted to native SFNT by CMake at build time using
`scripts/prepare_theme_fonts.py`. Only original WOFF/WOFF2 and license notices
are maintained in `resources/themes`; generated TTF live in the build tree's
`theme-fonts` directory. CMake generates `themes.qrc` in that build tree, retaining
authored CSS URL aliases, and orders resource compilation after font conversion.
Install the pinned conversion tools with
`python -m pip install -r requirements-build.txt` before configuring. Unchanged
fonts are not regenerated by incremental builds. Imported local WOFF/WOFF2 files are decoded offline using FreeType,
then registered through Qt; failed registrations do not enter the success cache.
Font aliases belong to the theme, and successful registrations are deduplicated
by content digest.

## Flex formatting stage

`CssLayoutStyle` is the common computed input for formatting. `CssFormattingContext`
owns Yoga configuration and Flex allocation; the HTML and Markdown adapters only
measure native content and materialize its final `LayoutBox`. It supports directions
and reversals, wrapping, order, alignment, baseline alignment, gaps, auto margins,
grow/shrink/basis, intrinsic width keywords and min/max constraints. Percentages
use the flex content box, while percentage padding uses its containing width.
Text reflows at the allocated width. Minimum violations freeze and redistribute
space after allocation, avoiding Yoga's early minimum-size floor of flex bases.

Markdown block containers and `#write` can establish a Flex context; paragraph
and heading items retain their native text/source mapping. Markdown inline runs
remain one inline formatting item rather than an arbitrary DOM of flex items. All items
inside a root Flex context are materialized together even under Lazy policy:
their geometry is interdependent. Editing rebuilds this context, while normal
vertical documents keep sparse promotion and suffix shifts. Visible-block queries
and clicking use actual rectangles because visual order may differ from source order.
`overflow` controls clipping and automatic minimums; embedded containers do not
yet provide their own interactive scrollbars. General inline formatting around an
`inline-flex` atom, orthogonal writing modes, replaced-element aspect-ratio minimums,
and full WPT compatibility remain follow-up work.

`CssFlexLayoutTest` consumes `tests/fixtures/theme/flex-layout-browser.json`,
generated with `node scripts/probe_flex_css.mjs`. It compares Chrome box geometry
at 100% and 200% zoom and checks real edits, hit testing and eager/lazy convergence.
Geometry tolerance is 0.8px, with 1.25px for baseline-dependent coordinates because
the native Qt and Chrome font backends round ascent metrics differently.

## Grid formatting stage

`layoutFormattingItems` dispatches Flex and Grid from the same computed input.
`CssGridLayout` owns line/area placement, intrinsic track contributions, free-space
distribution and final item rectangles. Both document adapters use its returned
Grid area widths for percentage padding, content reflow and final boxes. Grid
tracks freeze font-relative lengths during computed-style resolution, including
explicit inheritance; percentages resolve when the Grid area is known.

The first stage supports length/percentage/`calc()` tracks, `auto`, `min-content`,
`max-content`, `fr`, `minmax()` and integer `repeat()`. Numeric positive/negative
lines, row/column spans, implicit track patterns, row/column auto-flow and dense
packing share the same placement pass. Gaps, item/content alignment, auto margins,
min/max item sizes and nested Grid/Flex use the common box snapshots. Root Grid
uses the same atomic materialization/invalidation and XY hit testing as Flex.
Track counts and authored line magnitudes are bounded at 1000 to keep malformed
or adversarial CSS from allocating an unbounded grid.

Automatic repetition (`auto-fill`/`auto-fit`) stays symbolic in computed styles.
The shared layout resolves its count against the definite content axis and gap,
with one repetition for an indefinite axis. `auto-fit` collapses only unoccupied
repeated tracks and their gutters after placement, retaining the original explicit
line numbering. Fixed siblings, repeated track patterns and percentage/calc fixed
minimums participate in counting. Resizing re-expands the computed template rather
than caching an old count, including when Markdown root layout uses Lazy policy.

Named line lists retain case-sensitive names and merge adjacent repeat boundaries.
Positive/negative occurrences, named spans and implicit named lines resolve before
the common placement pass. `grid-template-areas` requires equal row lengths and
rectangular named regions; its implicit `name-start`/`name-end` lines use the same
resolver. Named shorthand omissions propagate custom identifiers according to
the CSS rules. Empty area cells do not reserve placement slots. Syntax-invalid
templates and areas are discarded by the common cascade.

`CssGridLayoutTest` reads `tests/fixtures/theme/grid-layout-browser.json`, generated
with `node scripts/probe_grid_css.mjs`. It checks Chrome geometry at 100% and 200%
zoom, native Markdown editing, caret/selection coordinates and full/lazy agreement.
Subgrid shares the parent's used tracks on one or both axes. `CssGridInheritance`
travels with each allocated item; it is not computed style and is never cached by
selector or NodeId. Shared tracks retain parent line names, with local names and
integer/auto-fill name repetition added at layout time. Padding, borders and
margins inset edge tracks; different subgrid gaps adjust both sides of each gutter.
Subgridded axes stretch and clamp placement to the inherited span, without implicit
tracks. Outside a grid item, `subgrid` behaves as `none` on that axis.

Descendant intrinsic contributions are translated onto parent tracks recursively,
then sized in global span order. A separate width query uses this same algorithm
without laying out blocks or measuring text height. Nested grids therefore measure
their actual tracks rather than summing widths as if they were Flex rows.
Indefinite `fr` tracks include max-content and spanning contributions;
`fit-content(length/percentage)` caps track growth without overriding its content
minimum. Explicit `min-content`/`max-content` minima may exceed a smaller maximum
in `minmax()`. Shared geometry is used by both adapters and native caret/selection;
a descendant edit invalidates its owning formatting context and parent tracks.

Masonry, orthogonal writing modes, Grid baseline groups and complete CSS Grid
intrinsic sizing/WPT coverage remain future work, including cyclic percentage
tracks/gaps in indefinite axes, automatic repetition under min/max-only containing
sizes, and transferred aspect-ratio contributions from complex replaced content.
General inline formatting of `inline-grid` has the same limitation as `inline-flex`; parsing these values does not promise browser-complete behavior.
Escaped CSS custom identifiers and escaped area strings, and the full `grid` /
`grid-template` shorthands are not yet supported.

## Verification

`DocumentStyleConformanceTest` covers inheritance and font-relative geometry,
shorthand/longhand ordering, conditional variables, responsive widths, native
font resources, imported webfont decoding, text/zoom scaling, heading flow,
HTML inheritance and separate code typography. Inline rendering tests check box
advances, unchanged visible text and caret/click round trips. The full suite
also checks editing, incremental/lazy layout and Mermaid.
Snapshot regressions compare eager and promoted lazy rectangles, test a heading
CSS edit across wrapping/borders/caret/selection, and compare structural edits
with a fresh layout. Chrome comparisons cover computed values and the used box
geometry of paragraphs, headings and preformatted blocks with both box-sizing
modes; native table behavior has separate regression coverage.
The committed reference is `tests/fixtures/theme/document-box-browser.json`.
Regenerate it with `node scripts/probe_document_css.mjs` after installing
Playwright. `CHROME_EXECUTABLE` selects a local browser; otherwise Playwright's
Chromium is used. The regular native test suite consumes this fixture without
requiring a browser installation.

At 100% zoom and a 16px user text size, Chrome with the same host CSS and
Newsprint sheet gives the following reference values:

| Value | Expected |
| --- | --- |
| Text width below the 1400px breakpoint | 580px (640px outer box minus 60px padding) |
| Text width at or above the breakpoint | 854px (914px outer box minus 60px padding) |
| h1 font size / top margin | 30px / 60px |
| h1 bottom padding / border | 24.375px / 1px, `#c5c5c5` |
| h2 top margin | 48px |
| Code block line height | approximately 24px |

These values have been checked against Chrome computed styles and the Windows
native Qt font backend. This contract supports the native renderer's CSS subset;
it does not imply complete browser CSS support or pixel identity with Typora's
application-specific DOM and stylesheet. Unknown media grammar is skipped.
