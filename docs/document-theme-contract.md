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

Generated `::before`/`::after` styles use the same stable originating-element
views for Markdown hosts and inline Markdown/HTML links. Attribute and structural
selectors navigate the origin's DOM position; inheritance includes its active
state. `GeneratedContentStyle` projects the live computed result once for flow
and positioned fragments. Flow text/icons participate in wrapping and source
mapping; positioned fragments retain relative used geometry and normal/hover/
focus endpoints. Painting only consumes these snapshots. Cached block translation
moves the containing origin, and structural style fingerprints invalidate stale
generated content. Link icons and list guides have no separate runtime recipe.
The compiled selector index conservatively filters pseudo subjects by their
rightmost tag/id before allocating snapshots; unrelated prose pays no per-state
pseudo cascade cost. Functional selectors remain candidates, and active media
changes replace the index with the engine's new environment snapshot.

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
their geometry is interdependent. Editing recomputes container allocation and
reuses unchanged measurements and allocated children, while normal
vertical documents keep sparse promotion and suffix shifts. Visible-block queries
and clicking use actual rectangles because visual order may differ from source order.
`overflow` controls clipping and automatic minimums; embedded containers do not
yet provide their own interactive scrollbars. Orthogonal writing modes and full
WPT compatibility remain follow-up work.

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
The shared layout resolves its count against the definite content axis and gap.
For an indefinite preferred size, a definite maximum supplies the largest fitting
count; a minimum-only constraint supplies the smallest count satisfying it. The
border-box constraints first exclude padding and borders, and conflicting minimums
win over maximums. One repetition is used only when all these sizes are indefinite.
`auto-fit` collapses only unoccupied
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

### Size dependencies and replaced content

Intrinsic measurement and final allocation use explicit definite/indefinite size
references. Percentage tracks in an indefinite axis behave as intrinsic tracks
while measuring; cyclic gap percentages contribute zero and `calc()` retains its
absolute part. Grid then freezes its intrinsic content height, applies min/max
constraints, and resolves percentage tracks and gaps once against that height.
Overflow from this pass does not increase the frozen height. In auto-height Flex
containers a percentage row gap stays zero, including when only a min/max height
is specified; its absolute `calc()` component still contributes.

`CssSizing` supplies common content constraints and replaced-element sizing.
Adapters carry natural image dimensions separately from computed CSS, project
lengths to layout pixels once, and pass definite allocations into the same ratio
calculation. HTML image dimension attributes also follow document zoom and remain
overridable by authored CSS. `aspect-ratio: auto <ratio>` retains an available natural image ratio;
an authored ratio follows `box-sizing`, while natural ratios use the content box.
Min/max constraints may transfer to the automatic opposite axis or break the ratio
when required. Flex automatic minimums and Grid track contributions use these
sizes. A directed dependency phase revisits columns after a definite row allocation
changes a stretched item's ratio contribution; column Flex likewise transfers its
allocated main size back to an automatic cross size.

Markdown and HTML image painting consumes the allocated content rectangle. Padding
and borders reserve space outside that rectangle, and image upscaling or an authored
ratio is not undone by a second painting-time natural-size cap. Images use the CSS
default `object-fit: fill`; additional `object-fit` modes are not yet implemented.

Browser fixtures run in standards mode with an explicit doctype. They cover cyclic
percentage/calc dependencies, min/max-only automatic repeats, natural and authored
image ratios, transferred constraints and both box-sizing modes at 100%/200% zoom.
Image cases additionally compare painted content rectangles. Native tests exercise
Markdown image hit testing and caret coordinates, resizing and full/lazy convergence.

Masonry, orthogonal writing modes and complete CSS Grid intrinsic sizing/WPT
coverage remain future work. The directed size-dependency phases do not establish
full browser compatibility for arbitrary nested cyclic constraints.
Escaped CSS custom identifiers and escaped area strings, and the full `grid` /
`grid-template` shorthands are not yet supported.

### Measurement phases and invalidation

`CssMeasureRequest` distinguishes intrinsic measurement, inline allocation,
block allocation, dependent allocation and final materialization. A negative
containing height is indefinite; a frozen cyclic intrinsic height does not become
a definite percentage reference. Fixed rows and inherited subgrid rows can
provide definite references during column contribution measurement. The Grid
scheduler processes row-to-column ratio transfers in a dependent phase rather
than recursively calling its own layout entry with a retry counter. Each nested
formatting context owns its phases and inherited references.

`measureCssItem` is the common memoized measurement entry. Its bounded request
key includes width, containing width/height, allocated height, phase and inherited
track geometry, names and definitions. Thus equal rectangles with different
percentage references or subgrid definitions cannot alias. Root edits retain
measurements only after validating a content/style/projection signature. The
signature covers descendants, resolved inline links, computed structural styles,
the stylesheet including pseudo-element rules, font/zoom, render settings and
active composition. Changed item identities and
changed signatures invalidate the item and its enclosing contribution query.

Formatting contexts retain the constraints and measurements consumed by their
previous track solution. A subsequent pass validates intrinsic inputs, styles,
inherited tracks and the exact measured contributions before reusing that
solution. A changed contribution triggers the ordinary shared Flex/Grid solver;
equal contributions stop propagation at the container boundary. This still
visits the affected context's input items; it does not promise sublinear track
solving for arbitrary cross-track dependencies.

Native block reuse applies recursively inside containers, including allocated
children. Measurement passes cannot consume retained layout objects. Final
materialization moves unchanged objects from the previous tree, translates their
geometry and updates absolute source positions together with reuse metadata.
Removed measurements/solutions are pruned after a full formatting pass. Explicit
full refreshes clear persistent measurements. Generated TOC content still uses a
conservative rebuild because of its document-wide outline dependency.
List labels and their shared gutter are measured once per list per pass and are
part of each item's reuse signature. A wider sibling marker invalidates the
affected indents; ordinary body edits retain siblings whose marker geometry is
unchanged.

### Viewport updates and lazy estimates

Viewport updates compare the active media-rule set before projecting a theme.
When that set is unchanged, immutable style-engine snapshots reuse compiled
selectors and candidate indices. Computed and cascade caches are discarded:
computed values carry their viewport context, and cascade matches contain
addresses into the snapshot's declaration storage. Theme copies retain their
own environment and caches.

Theme projections are reused only when the sheet has no viewport-unit values
and the page box has no percentage width or insets. Media-rule changes,
viewport-dependent projections, zoom and user text-size changes rebuild the
projection through the same computed-style engine. The current viewport still
reaches live nodes and inline declarations on the reuse path.

If projections and content width stay unchanged, native prose layouts can move
horizontally without reshaping text. Materialized HTML conservatively disables
this path because its inline styles may contain viewport units absent from the
theme sheet. Unbuilt HTML resolves the current environment when promoted.

Lazy paragraph and heading estimates cache used boxes, line heights and wrapping
capacity by prototype element and containing width within one layout pass.
Explicit height, min/max height and box sizing participate in the placeholder
height. Starting a new pass clears these entries, including after theme, font,
zoom or width changes. Visible promotion still computes the full live style.

### Resource versions

`LayoutResources` gives each image, font database and Mermaid cache entry separate
paint and geometry versions. A resource scope captures reads, including nested
reads, into the block or measurement dependency snapshot. Geometry version
changes invalidate measurements; paint-only changes preserve their validity.
Images with unchanged natural dimensions update retained pixels without text
shaping or track solving. Changed natural dimensions revalidate size
contributions, including CSS constraints that can make the allocated size stay
unchanged.

The shared image loader handles remote downloads, data URLs and cached local
files. Local file/directory watches detect replacement and modification. HTML
painting also consults this cache, so it cannot retain a private failed or stale
remote image. Font database notifications invalidate metric caches even when
the `QFont` key is unchanged. Mermaid keys include font geometry generations;
cache clears invalidate dependent layouts and superseded workers cannot publish
results into a later cache epoch.

Editor resource notifications are coalesced per event-loop turn. The layout
refreshes dependency owners among promoted blocks and pins the viewport across
geometry changes. Unpromoted content reads the current resource versions when
it is built. An in-progress asynchronous document open does not rebuild stale
document contents in response to a resource notification.
Leaving the loading state schedules a dependency refresh as well, so cancelling
an open cannot lose resource changes deferred while the old document was hidden.

### Inline formatting contexts and baselines

`inline-flex` and `inline-grid` reserve an atomic inline box in the surrounding
native text layout. Its shrink-to-fit width, margins and baseline come from the
shared Flex/Grid formatter. The same rectangle determines wrapping, painting and
pointer coordinates. Line boxes include the text strut as well as the component's
ascent/descent. Grid first/last baseline groups participate in track contribution
and final item alignment; container baselines propagate to nested inline boxes.
Container baseline export uses occupied rows, shared baseline groups and row-major
grid order; empty leading/trailing rows do not force a synthesized bottom baseline.

Markdown keeps each inactive HTML component's source and visible ranges while
collapsing its display buffer to one placeholder. Child text has source mappings
through nested tags and entities, so child links, caret hits and selections use
the retained HTML text layouts. Entering the group reveals editable source using
the existing projection policy. This does not add orthogonal writing modes,
arbitrary `vertical-align` values or browser-complete inline formatting.

Selected WPT sources are pinned with their revision, checksums and license under
`tests/fixtures/theme/wpt`. Adaptations document their scope; the ordinary native
tests consume committed Chromium geometry without a network or browser dependency.

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

`ResourceIncrementalLayoutTest` covers local file watching, asynchronous HTTP
images, same-size pixel updates, font registration, Mermaid completion and cache
clearing. Nested Flex/Grid/subgrid edits and image updates compare boxes, pixels,
caret and selection geometry with fresh eager and lazy layouts. Editor-level
checks cover coalesced notifications, viewport pinning and asynchronous open.
Newsprint, GitHub and Night are exercised at 100%/200% and narrow/wide viewports.
Work counters assert that paint-only changes skip layout and that an image
completion in a long document rebuilds its owning block rather than every block.

The same test includes a small timing probe without machine-dependent CI limits.
For a longer local run, build the Release target, then use PowerShell:

```powershell
$env:MUFFIN_LAYOUT_BENCH_BLOCKS = '1200'
$env:MUFFIN_LAYOUT_BENCH_ITERS = '12'
$env:MUFFIN_LAYOUT_BENCH_OUTPUT = "$PWD/build/theme-audit/resource-layout-performance.json"
ctest --preset conan-release -R '^MuffinResourceIncrementalLayoutTest$' --output-on-failure
```

The JSON reports median, p95 and maximum milliseconds for actual text edits,
image completion and viewport resizing. Typing includes the document update;
fresh references rebuild the same document at the same viewport width. Both
paths use lazy layout with the first eleven slots promoted. Image timing excludes
network transfer and measures decoded-resource publication through layout refresh.
Resize includes responsive theme recalculation and rebuilding the lazy estimates;
`resize_styles` and `resize_layout` split these costs. The resize probe always
rebuilds the lazy layout, even when an editor could use horizontal translation;
it deliberately has no unchanged-width reference. Remove these environment
variables to restore the smaller default test workload.

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

Generated heading content is inserted into the retained inline display stream,
with zero source extent. Its text is shaped by the same `QTextLayout`; atomic
shapes and masks own used `LayoutBox` fragments and participate in intrinsic
widths, line breaking, vertical alignment and visual overflow. Painting consumes
these fragments, and clicks on generated content snap to the adjoining source
caret. Source selections exclude the generated text. The heading-specific
advance reservation and draw-time inline position estimation have been deleted.
Absolutely positioned decorations remain outside normal inline flow.

`scripts/probe_inline_pseudos.mjs` records Chromium references with the bundled
font bytes. `RenderHeadingPseudoTest` compares generated-content geometry and
tests 100%/125%/200% zoom, narrow/multiline headings, local edits, source/visible
caret mapping, selection and fresh eager/lazy pixels. Pseudo styles use live
host topology for selectors, while inheriting from the originating element.
Heading counters retain the existing flat document-order scope. Complete CSS
counter scopes and margin collapsing for block pseudo-elements remain outside
this inline-layout contract.

`font_rendering::configureCssFont` is shared by Markdown and HTML. On Windows it
detects a missing bold face through OpenType metadata, verifies the uniform
DirectWrite simulation advance against the same unsimulated face, and removes
that extra advance while preserving bold ink. Spaces are compensated separately;
real bold faces and variable fonts retain their design metrics. Author spacing
is an explicit input, so applying the configuration twice does not accumulate a
correction. The original-family fallback stack remains intact.

The real-theme audit exports requested/resolved pixel sizes, requested/actual
face weights, native advances and layout spacing. The browser audit records the
same text's advance with the same imported font files. Qt currently rounds the
requested font size to integer logical pixels before font-engine resolution;
fractional CSS sizes can therefore retain a small advance difference. This
limitation is recorded explicitly instead of compensating with a family-specific
stretch or an undefined `QTextLayout::setRawFont` drawing path.

These values have been checked against Chrome computed styles and the Windows
native Qt font backend. This contract supports the native renderer's CSS subset;
it does not imply complete browser CSS support or pixel identity with Typora's
application-specific DOM and stylesheet. Unknown media grammar is skipped.
