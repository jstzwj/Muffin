# Mermaid native C++ port

The flowchart execution contract and milestone history are maintained in
[`mermaid-flowchart-remaining-plan.md`](mermaid-flowchart-remaining-plan.md).

## Current status (2026-08-10)

Muffin renders fifteen Mermaid families through a native C++20/Qt pipeline:

- flowchart/graph;
- sequence diagram;
- class diagram;
- state diagram (`stateDiagram-v2` and the supported legacy renderer path);
- ER diagram;
- requirement diagram;
- pie chart;
- quadrant chart;
- user journey diagram;
- radar chart (`radar-beta`);
- XY chart (`xychart-beta`);
- timeline (`timeline`, including LR and TD layouts).
- packet diagram (`packet` and `packet-beta`).
- Kanban diagram (`kanban`).
- mindmap diagram (`mindmap`).

Each supported family has parser/database, layout, immutable scene, structural,
pixel, and editor-cache coverage. Unsupported Mermaid families remain editable
source fences instead of being approximated. The Windows Conan Release gate is
currently 223/223 tests, including the end-to-end
`MuffinRenderMermaidBlockTest`.

All fifteen native families now share `MermaidRenderMetadata` for the diagram
title, accessible title/description, role description, title styling, and
content-canvas geometry. Frontmatter titles are applied before family parsing,
so a sequence diagram's native `title` statement retains Mermaid's override
precedence. Timeline and Kanban are upstream exceptions: Timeline paints only
its inline `title`, while Kanban has no title grammar and ignores frontmatter
title entirely. Mindmap likewise has no title/accessibility grammar and ignores
frontmatter title rather than painting the common title band. The common title painter is used by the editor, print/PDF block
path, PNG export, and SVG export; title growth is included in scaling,
dirty-viewport culling, and flowchart link hit testing. HTML export now embeds
the native SVG fragment instead of a raster `<img>`. Its root carries
Mermaid-compatible `role`, `aria-roledescription`, `aria-labelledby`, and
`aria-describedby` attributes plus linked `<title>` and `<desc>` elements.

`MermaidSvgExporter` sends each immutable scene through its production painter
and Qt's SVG generator, then normalizes the root contract. Output is directly
embeddable XML with a stable ID, family class, viewBox, `useMaxWidth` sizing,
accessibility nodes, and sanitized Flowchart/forced-Sequence link overlays.
`deterministicIds` and `deterministicIDSeed` match the Mermaid 11.16 ID counter
contract. HTML uses per-document instance indices to avoid duplicate IDs, and
the rendered-diagram context menu writes an individual `.svg` atomically.

The custom editor canvas does not yet expose a separate Qt `QAccessible`
object for each diagram, so SVG ARIA completeness is not described as complete
editor screen-reader parity.

The editor interaction path now consumes the same immutable scenes. Flowchart
nodes expose Mermaid tooltips and safe links through normal hit testing;
opening a link retains Muffin's `Ctrl+Click` policy, while callback/call forms
remain inert under the strict desktop security policy. Sequence `links` and
`link` statements build participant menus. A participant click toggles its
menu, while `sequence.forceMenus` keeps menus visible without changing the
reserved canvas geometry; unsafe menu URLs are never handed to the OS.

Animated Flowchart edges project `animate`/`animation` metadata into Mermaid's
9/5 dash pattern and 20-second fast or 50-second slow motion. A shared 33 ms
editor clock runs only while an animated Mermaid block is visible. One-shot
PNG, SVG, HTML, print, and PDF paths retain a deterministic initial frame, so
export bytes and pagination do not depend on wall-clock timing.

## Compatibility target

The compatibility baseline is `mermaid` 11.16.0 (MIT), resolved by the pinned
`@mermaid-js/mermaid-cli` 11.16.0 package used by the fixture generators. The
generator scripts accept the local Mermaid package path explicitly; no
machine-specific source path is part of Muffin's build or test contract.

`mermaid-cli` is not the Mermaid implementation. Its rendering path starts a
Puppeteer browser, loads Mermaid and external layout packages, renders into an
SVG DOM, and captures SVG/PNG/PDF output. Muffin must instead port the Mermaid
semantic pipeline and render it through Qt:

1. shared preprocessing and diagram detection;
2. a parser and database model for each diagram family;
3. text measurement and graph layout;
4. shape, edge, label, theme, accessibility, and interaction rendering;
5. code-fence integration and export.

The shared implementation layer is in `src/mermaid`. It is linked into
`MuffinCore` and has no JavaScript or browser dependency. YAML frontmatter uses
the native `yaml-cpp` library.

The flowchart parser in `src/mermaid/flowchart` exposes a typed `FlowchartData`
model and has upstream DB goldens for legacy node shapes, edge forms and labels,
grouped/chained links,
parallel edge IDs, classes/styles, nested subgraphs, and string/Markdown labels.
The same comparison covers accessibility statements, click/callback/link data,
node and edge metadata (`@{...}`), explicit edge IDs, animation properties,
long link spellings, default `linkStyle`, tooltips, and Mermaid's default edge
and text-size limits. `MuffinMermaidFlowchartCoverageMatrixTest`, parser-error
goldens, and differential fuzz fixtures close the audited `flow.jison` grammar
and invalid-input contract for the supported baseline.

`src/mermaid/flowchart/FlowchartLayout.cpp` contains a native Sugiyama/Dagre
pipeline: deterministic feedback-edge reversal, network-simplex ranking,
dummy-chain normalization, barycenter sweeps, crossing counts, four-way
Brandes-Kopf coordinate balancing, recursive compound bounds, rectangle
clipping, and D3-compatible cubic basis paths. Geometry goldens cover all four
directions, chains, branches, a cycle, the network-simplex rank counterexample,
crossing pressure, long edges, nested clusters, all four self-edge directions,
parallel edges, and exact edge-label positions. The geometry comparison injects
exact upstream node and edge label measurements so layout errors remain
separate from font errors.

Native Arial measurement uses `QTextLayout` design metrics and has a separate
sub-pixel comparison. `FlowchartShapes` began with the 13 legacy bracket shapes
and now implements the full audited registry. Browser-generated silhouette,
structural, and pixel matrices compare the Qt geometry across the supported
classic, hand-drawn, and neo looks.

The full Dagre compound pipeline is now the active flowchart layout engine
(`layoutFlowchartNodes` delegates to `layoutFlowchartNodesDagre`). All 22
geometry golden cases pass — including `compound-crossing`, which is no longer
marked `pendingNative`. The native Dagre port (nesting-graph, parent-dummy-
chains, add-border-segments, compound ordering, BK type-2 coordinate
assignment, normalize/acyclic/coordinate-system/self-edge handling, and the
27-phase `runLayout` orchestration) lives in `src/mermaid/dagre/`.

The expanded catalogue, fill/stroke, markers, labels, fonts, CSS/theme mapping,
and whole-diagram painter are now native and covered by structural and pixel
oracles. All fifteen native scenes are integrated into the editor and print/PDF path
through `MermaidRenderCache`. The legacy flat
`WorkGraph` implementation remains as inactive reference code; the active path
always delegates to the compound Dagre pipeline.

## Native graphlib compound multigraph

`src/mermaid/graphlib/Graph.h` is a faithful C++ port of the graphlib `Graph`
(`dagre-d3-es/src/graphlib/graph.js`): a directed, multigraph, compound graph
templated on node/edge/graph label types. It is the data structure every Dagre
layout phase (nesting-graph, parent-dummy-chains, add-border-segments, order,
position/bk) operates on, and is the foundation for milestones B and C of the
flowchart plan. Iteration order is part of the contract — graphlib relies on
lodash `_.keys`/`_.values`, which yield string-key insertion order, so every
iterated map is wrapped in an insertion-order-preserving `OrderedMap`. The
unit test `MuffinMermaidGraphTest` covers parent reset and recursive
re-parenting, ancestors and lowest-common-ancestor, parallel named edges,
compound/leaf traversal, `__proto__`-style special IDs, a 50 000-deep parent
chain (proving the cycle check and ancestor walk are iterative, not recursive),
insertion-order stability including delete-then-re-add, and cross-run
determinism. The active flowchart layout pipeline uses this graph through
`layoutFlowchartNodesDagre`; the flat `WorkGraph` implementation is not called.

## Dagre compound pipeline (milestone C, done)

`src/mermaid/dagre/` is a faithful, file-by-file port of the Dagre
`runLayout` phases that the flat `WorkGraph` pipeline lacks. Each phase operates
on the `DagreGraph` model above and carries hand-verified unit tests in
`MuffinMermaidDagreCompoundTest`:

- `DagreLabels.h` — the typed label set (`DagreNodeLabel` / `DagreEdgeLabel` /
  `DagreGraphLabel`) covering every field `buildLayoutGraph` and the runLayout
  phases read or write (rank/order/x/y, border top/bottom/left/right,
  minRank/maxRank, dummy type tags, edgeObj/edgeLabel, labelpos, nestingEdge,
  dummyChains, nodeRankFactor, nestingRoot, …).
- `DagreUtil` — `util.js` helpers: `addDummyNode` (deterministic per-graph id
  counter), `addBorderNode`, `maxRank`, `buildLayerMatrix`, `intersectRect`,
  `asNonCompoundGraph`, `normalizeRanks`, `removeEmptyRanks`, `partition`.
- `NestingGraph` (C1, `nesting-graph.js`) — virtual root, per-cluster border
  top/bottom dummies, nesting edges, `nodeRankFactor`, minlen scaling.
- `AddBorderSegments` (C3, `add-border-segments.js`) — left/right border dummy
  chains per rank for each cluster.
- `ParentDummyChains` (C2, `parent-dummy-chains.js`) — postorder low/lim LCA
  and ascending-then-descending parent assignment for long-edge dummy chains
  crossing cluster boundaries.

### C4 / C5 / orchestration / integration — DONE

The full `runLayout` pipeline is ported, golden-accurate, and is the active
flowchart layout engine (`layoutFlowchartNodes` delegates to
`layoutFlowchartNodesDagre`). All 22 geometry golden cases pass, including
`compound-crossing` (its `pendingNative` marker is removed). Ported modules
(all on `DagreGraph`):

- `Acyclic` (`acyclic.js`, dfsFAS — mermaid's default path), `Normalize`
  (`normalize.js`, value-captured `edgeLabel` reconstructed in `undo`),
  `CoordinateSystem` (`coordinate-system.js`).
- `Rank` (`rank/`): `longestPath`, `slack`, `feasibleTree`, full network-simplex
  (cut values / low-lim / enter-leave exchange), operating on a simplified
  leaf view with ranks written back to the compound graph.
- `Order` (C4, `order/`): `buildLayerGraph` (compound layer graph + virtual
  root), `initOrder`, recursive `sortSubgraph` with border handling,
  `barycenter`, `resolveConflicts`, `sort`, accumulator-tree `crossCount`,
  `addSubgraphConstraints`, sweep orchestration. Layer-graph node labels use
  single-string `layerBorderLeft`/`layerBorderRight` (the real graph stores
  per-rank arrays).
- `Position` (C5, `position/bk.js`): `findType1Conflicts` + `findType2Conflicts`
  (border scan), `verticalAlignment`, `horizontalCompaction` via a
  qreal-edge `BlockGraph` (not the flat topological sort), `alignCoordinates`,
  `balance`, `sep`.
- `Layout` (`layout.js`): the 27-phase `runDagreLayout` plus `makeSpaceForEdgeLabels`,
  self-edge remove/insert/position, edge-label proxy inject/remove,
  `assignRankMinMax`, `removeBorderNodes`, `fixupEdgeLabelCoords`,
  `translateGraph`, `assignNodeIntersects`, `reversePointsForReversedEdges`.
- `layoutFlowchartNodesDagre` in `FlowchartLayout.cpp` builds the `DagreGraph`
  from `FlowchartData` + measured sizes, runs the pipeline, and extracts the
  `FlowLayoutResult`.

`tests/mermaid/MermaidDagreLayoutTest.cpp` is a geometry-only gate that calls
`layoutFlowchartNodesDagre` directly against the committed goldens (all 22
cases). The comprehensive gate is the existing `MermaidFlowchartLayoutTest`,
which now also runs through the Dagre pipeline (geometry + shape silhouettes +
pixel masks, all 22 cases).

Bugs found and fixed during integration (via the
`scripts/dagre_bk_groundtruth.mjs` ground-truth harness, which drives
`dagre-d3-es` directly under Node):

- The order sweep was a no-op: dagre aliases layer-graph node labels to live
  `g.node(v)` order (JS reference semantics); value semantics can't, so orders
  are now synced into each layer graph before every `sortSubgraph`.
- `removeEmptyRanks` dereferenced a past-end iterator on the empty ranks
  compound graphs produce.
- Compound-node parenting: the parser lists a node in every subgraph scope
  that references it, so `setParent` last-wins reparented it to the outer
  cluster. Each node is now parented to its deepest (innermost) containing
  subgraph.
- Self-edge: mermaid's renderer draws its own loop (not dagre's
  `positionSelfEdges` points); the extraction now uses the matching
  control-point geometry.
- Compound chirality: for the symmetric `compound-crossing` case, cluster nodes
  are inserted in reverse declaration order to match mermaid's
  dagre-wrapper construction order (asymmetric cases are unaffected — their
  tie-break is forced).

The legacy flat `WorkGraph` pipeline in `FlowchartLayout.cpp`'s anonymous
namespace is now dead code pending removal.

### Milestone D (edges / markers / labels) — done

Edge marker clipping is marker-aware (`clipForMarkers` in
`FlowchartLayout.cpp`): filled arrow markers whose tip sits at the endpoint
(`arrow_point`, `double_arrow_point`) shorten the path by 4px at the end (and
`double_arrow_point` also at the start, for its start marker); centred markers
(`arrow_cross`, `arrow_circle`) and open lines (`arrow_open`) are not shortened
— the marker is drawn on top at the endpoint. Golden cases `edge-arrow-types`
(`-->`, `--x`, `--o`, `---`) and `edge-styles` (`-->`, `-.->`, `==>`) verify the
matrix. Path-coordinate comparison is float-safe (`<= 0.002 + 1e-9`).

The D items are landed and retained by the current geometry and coverage gates:

- **Curve variants.** `src/mermaid/flowchart/D3Curves.h` is a 1:1 port of the
  d3-shape curve state machines (`basis`, `linear`, `step`, `stepBefore`,
  `stepAfter`, `cardinal`, `monotoneX`, `monotoneY`, `bumpX`, `bumpY`,
  `catmullRom`, `natural`) driven through a `d3.path()`-compatible
  `PathContext`; `pathForCurve` dispatches by name, mirroring Mermaid's
  `insertEdge` switch. Mermaid's separate `generateRoundedPath` is also ported:
  it uses a fixed 5px radius, clamps each cut to half of its adjacent segment,
  preserves repeated/collinear/reversing points, emits quadratic `Q` corners,
  and applies the same filled-arrow endpoint offset. `FlowLayoutOptions.curve`
  (default `basis`) selects the global generator while edge metadata can
  override it. Upstream geometry cases cover both `flowchart.curve: rounded`
  with real corners and `@{ curve: rounded }` beside an unchanged basis edge.
- **`labelpos` l/r + `labeloffset`.** Already faithful in the dagre port
  (`makeSpaceForEdgeLabels`, `fixupEdgeLabelCoords`, BK `sep()`, dummy
  propagation). mermaid hardcodes `labelpos:"c"` + `labeloffset:10` for all
  flowchart edges, so the l/r branches are present but inert; no separate
  golden is possible (mermaid never sets l/r).
- **Per-shape border intersection.** `intersectEllipse`/`intersectCircle`/
  `intersectPolygon` (+ `intersectLine`) are ported in `FlowchartLayout.cpp`.
  Edge extraction re-intersects the first/last interior point against each
  canonical node geometry (`tail.intersect`/`head.intersect`); circle and
  polygon handlers use their native outlines, while rect-like shapes and
  upstream handlers defined with `intersect_default.rect` use `intersectRect`.
  dagre-d3's `intersectLine` carries an
  `offset=abs(denom/2)` bias (a Graphics Gems integer-rounding trick that adds
  ±0.5 in floating point) which the goldens do not reproduce, so the true
  quotient `num/denom` is used. Diagonal and expanded-shape matrices retain the
  intersection behavior.
- **Invisible and bidirectional edges.** The parser tokenises `~~~`
  (`arrow_open` + `stroke:"invisible"`), `o--o` (`double_arrow_circle`), and
  `x--x` (`double_arrow_cross`), plus their thick variants. `edge-bidirectional`
  is the golden.
- **Parallel-edge labels.** mermaid has no special logic here — dagre's
  `edgesep` (ported) separates the parallel edge dummy chains, so their labels
  land at distinct positions. `parallel-labels` verifies.
- **Class/style overrides + per-edge curve.** The data model is complete:
  `linkStyle`, `classDef`, `class`, `style`, and `@{curve,animate,animation}`
  edge metadata are all parsed. `linkStyle N interpolate X` (note: `interpolate`
  is a bare keyword followed by a space — `interpolate:linear` is a mermaid
  parse error) and `@{curve:X}` set `edge.interpolate`; the layout uses it per
  edge (else `options.curve`), matching mermaid's `resolveEdgeCurveType`.
  `edge-interpolate` (edge 0 `linear`, edge 1 `basis`) verifies. The scene and
  painter apply label backgrounds, stroke colour, and `classDef` styles to the
  final painted path.

## Milestone E (expanded node shapes) — done

Mermaid 11.16.0's audited flowchart shape registry (`shapesDefs`) exposes 48
documented short names selectable through `@{ shape: NAME }`. All 48 resolve to
native canonical shapes; the original 13 bracket shapes and all expanded
polygon, wave, arc, cylinder, brace, circle, document, and process variants
have native sizing, border intersection, geometry, and painting.

`src/mermaid/flowchart/FlowchartShapeRegistry.h` is the canonicalisation layer:
`canonicalShape(type)` collapses the three naming systems — legacy bracket name
(`"round"`), `@{ shape: }` shortName (`"rounded"`), and alias (`"event"`) — onto
one canonical key. The 13 legacy shapes keep their existing names as canonical
(so the existing measure/intersect/geometry if-chains work unchanged); expanded
shapes get descriptive canonicals (`double_circle`, `triangle`, …). It is wired
into `flowShapeGeometry`, `measureFlowchartNodes`, and `intersectNodeForShape`,
so every `@{ shape: }` alias now routes to its shape's handling. Unrecognised
names fall back to `rect` (mermaid's default shape).

`upstreamShortNames()` and `nativeShapeCanonicalNames()` feed
`MuffinMermaidShapeRegistryTest`; the upstream-minus-native diff is empty.
`MuffinMermaidFlowchartCoverageMatrixTest` additionally requires classic,
hand-drawn, and neo shape matrices across directions, themes, and DPRs. Shape
calibration retains Mermaid's raw label bbox, padding, post-measurement fork
adjustment, and per-handler intersection rules.

## Verification contract

`scripts/generate_mermaid_compatibility_fixture.mjs` executes the locked
upstream Mermaid package and writes
`tests/fixtures/mermaid/preprocess-and-detect.json`. The C++ compatibility test
consumes that immutable result; it never invokes Node or a browser. Regenerate
the fixture only when intentionally changing the upstream compatibility
version, and review the JSON diff.

`scripts/generate_mermaid_flowchart_fixture.mjs` uses the locked Mermaid bundle
inside headless Chrome to parse source and serialize the observable FlowDB
state into `tests/fixtures/mermaid/flowchart-db.json`. Chrome and Puppeteer are
fixture-generation tools only; neither is linked, packaged, or invoked by
Muffin or its native test suite.

Sequence pixel fixtures deliberately separate structural coverage from raster
coverage. All sequence cases retain their Mermaid 11.16.0 DOM, marker, MathML,
layout, and operation metadata, while full-canvas PNGs are committed only for
the representative integration matrix. Label crops cover the text modes,
themes, and DPRs; dedicated delimiter, large-operator, token-group, and
subpixel-phase crops remain independent raster oracles.

Regenerate these browser oracles explicitly, outside the normal C++ build:

```powershell
node scripts/generate_mermaid_sequence_pixel.mjs `
  ..\mermaid-cli\node_modules\mermaid `
  tests\fixtures\mermaid\sequence-pixel
node scripts/compact_mermaid_sequence_pixel_fixture.mjs
```

The generator and compactor share `mermaid_sequence_pixel_policy.mjs`, remove
unreferenced PNGs, and update the manifest digest. Normal Release tests consume
the committed fixture and therefore require neither Node nor Chromium.

`scripts/generate_mermaid_flowchart_geometry_fixture.mjs` renders fixed-font,
fixed-theme flowcharts with upstream `dagre-wrapper` and records node bounds,
relative centers, and edge paths in
`tests/fixtures/mermaid/flowchart-geometry.json`. The native test currently
compares node/cluster geometry and every edge path command and coordinate,
including quadratic rounded corners.
For all 13 legacy shapes the same fixture embeds isolated upstream PNG alpha
masks, allowing a pixel-level geometry comparison without mixing in theme or
font antialiasing. These masks are not described as theme pixel goldens.

Strict compatibility is claimed only for modules with an upstream golden and a
passing native comparison. Supported families render from their immutable
scenes; detected but unsupported families remain normal code fences rather
than silently producing an approximate graph.

Editor-facing failures use a family-neutral structured diagnostic. Parser
stage/code, actual and expected tokens, and 1-based line/column spans survive
the render cache boundary. Preprocessor offset maps restore positions after
front matter, init directives, and comments are removed. Invalid source remains
visible with a marked source range; clicking the diagnostic panel moves the
caret to that range. Detector, preprocessing, resource, security, and native
render failures use the same diagnostic envelope even when no source span is
available.

## Configuration-effect matrix

`scripts/generate_mermaid_config_effect_matrix.mjs` reads Mermaid 11.16.0's
`BaseDiagramConfig`, `FlowchartDiagramConfig`, `SequenceDiagramConfig`,
`ClassDiagramConfig`, `StateDiagramConfig`, `ErDiagramConfig`, and
`RequirementDiagramConfig`, `PieDiagramConfig`, `QuadrantChartConfig`, and
`JourneyDiagramConfig`, `RadarDiagramConfig`, `XYChartConfig`, and
`TimelineDiagramConfig`, `PacketDiagramConfig`, `KanbanDiagramConfig`, and
`MindmapDiagramConfig`
declarations and writes the
committed `tests/fixtures/mermaid/config-effect-matrix.json` oracle. The
generator fails if an upstream family field is missing from the reviewed
policy or the policy contains a stale field. The current matrix contains 245
rows: 229 family-interface fields and 16 shared root/theme/security fields.

Each row records both upstream and native effects across these direct stages:

| Stage | Observable contract |
| --- | --- |
| `parsed` | preprocessing retains the requested key/value |
| `layout` | semantic geometry or routing changes |
| `text` | label content, wrapping, font, or alignment changes |
| `paint` | color, stroke, marker, or other pixels change |
| `viewport` | natural canvas/viewBox sizing changes |
| `interaction` | hit geometry, links, menus, or animation state changes |
| `export` | raster/SVG serialization or exported sizing changes |

The reviewed statuses are deliberately not a yes/no support flag:

| Status | Rows | Meaning |
| --- | ---: | --- |
| `parity` | 129 | Audited upstream and native stages agree |
| `partial` | 8 | Supported values/variants are named; other values fail or remain deferred |
| `upstream-inert` | 73 | Mermaid retains the option but 11.16.0 does not consume it |
| `deferred` | 5 | Absolute SVG marker URL serialization remains assigned |
| `unsupported` | 7 | Upstream effect exists but no native consumer exists yet |
| `legacy-only` | 19 | Applies to an old browser renderer, not the unified native scene |
| `api-only` | 3 | Function-valued hooks cannot be expressed by Markdown JSON/YAML config |
| `security-fixed` | 1 | Muffin intentionally keeps its strict desktop security policy |

`MuffinMermaidConfigEffectMatrixTest` validates the generated fixture digest,
schema completeness, status invariants, all seven dimensions, and production
probes. The first audit also closed concrete gaps: Flowchart `diagramPadding`
now reaches editor/PNG canvas geometry, State `nodeSpacing`/`rankSpacing` reach
Dagre, Sequence honors root `%%{wrap}%%` and `showSequenceNumbers`, and
unsupported root `layout` or family `defaultRenderer` engines return
`configuration/unsupported-layout-engine` instead of silently rendering with
Dagre.

Regenerate the immutable matrix only when reviewing a Mermaid baseline or its
policy:

```powershell
node scripts/generate_mermaid_config_effect_matrix.mjs `
  path\to\mermaid `
  tests\fixtures\mermaid\config-effect-matrix.json
```

`flowchart.curve` now has full matrix parity, including the custom `rounded`
variant through config, per-edge metadata, scene paint, interaction geometry,
and PNG export. The interaction/animation milestone is also complete: safe
Flowchart links/tooltips, live fast/slow edge animation, deterministic exports,
Sequence participant menus, and `sequence.forceMenus` all reach their runtime
consumers. Native SVG export is now complete at the product boundary: all fifteen
families produce deterministic, renderable fragments; HTML embeds them; and a
rendered diagram can be saved from its context menu. The matrix moved
`deterministicIds`, `deterministicIDSeed`, and the effective family
`useMaxWidth` rows to parity. The remaining SVG-specific work is the five
root/family `arrowMarkerAbsolute` rows: Qt currently expands arrowheads into
painted paths instead of serializing reusable marker URL references. This is a
shared export follow-up rather than a blocker for adding another native family.

## Large-scene paint contract

The editor inverse-maps the current `QPainter` dirty clip into scene
coordinates and passes it to painters that implement large-scene culling. Clusters, edge paths,
edge labels, nodes, notes, fragments, activations, and participant lifelines
are rejected before expensive path parsing, rich-text layout, or drawing when
their precomputed bounds are outside the viewport plus a conservative overscan.
Class and state edge-label documents and sizes are prepared with the immutable
scene; sequence-number lookup is indexed once per paint instead of scanning the
full number list for every message.

`MermaidPaintOptions` disables culling by default. `BlockLayoutBuilder` enables
it only for the asynchronous editor path; synchronous image, print, and PDF
layouts therefore paint the complete scene. `MuffinMermaidSceneCullingTest`
constructs 2,400 primitives for each supported family, requires the culled and
full paths to produce identical visible pixels, and places a deterministic
upper bound on painted primitives. Timings are reported for profiling but are
not used as a platform-sensitive pass/fail threshold.

## Port order

The implemented order was flowchart, sequence, class, state, ER, Requirement,
pie, quadrant, journey, radar, XYChart, Timeline, Packet, Kanban, then Mindmap. Flowchart
established the shared graph, Dagre, shape, theme, and style layers; sequence
established diagram-specific placement and the shared structured text/MathML
pipeline; class, state, ER, and Requirement reused those contracts. Requirement
also validates the current per-family workflow: strict lexer probes, a geometry
oracle, default-theme pixel comparison, event-ordered style cascade, typography,
and built-in Redux color-index coverage. New families must add
AST/database goldens, layout geometry goldens, SVG/DOM structural goldens, Qt
pixel tests, invalid-input tests, and recursion/size guards before editor
integration is enabled.

### Requirement boundary

The source API now covers Requirement parsing, Dagre topology, box and marker
paint, titles/accessibility, event-ordered `classDef`/`class`/`style` cascade,
text layout/decoration/transform, and the built-in `redux-color` /
`redux-dark-color` color-index palettes. The production editor, PNG, SVG,
canvas, and determinism paths all consume the same immutable scene.

The remaining boundaries are explicit rather than hidden parity claims:

- global `htmlLabels:false` is partial; Requirement currently follows the
  upstream `htmlLabels:true` text path;
- arbitrary `themeCSS` and absolute SVG marker URL serialization are shared
  unsupported/deferred capabilities recorded in the config matrix;
- external `mermaid.initialize()` object/array configuration is not part of
  Muffin's Markdown source API. In particular, source-level custom
  `borderColorArray`/`bkgColorArray` values are ignored, matching Mermaid
  11.16.0's source-entry behavior.

### Family expansion status

Pie, quadrant, journey, radar, XYChart, Timeline, Packet, Kanban, and Mindmap are now native. Each was implemented probe-first
against Mermaid 11.16.0 and ships with grammar/database coverage, immutable
geometry fixtures, native painter tests, deterministic PNG/SVG integration, and
configuration-matrix rows. Journey additionally freezes JavaScript scalar
coercion, CSS presentation-attribute fallback, actor wrapping, and the upstream
viewBox/root-height mismatch. Radar freezes its Langium database semantics,
formula geometry, nested theme style path, JavaScript configuration coercion,
and fixed-canvas rendering against Mermaid 11.16.0. Timeline freezes both the
left-to-right and top-down formula layouts, section/task/event geometry, and
classic/Neo/Redux palette behavior.
Packet freezes the Langium block database, contiguous-field validation and
row splitting, fixed formula geometry, bit-number visibility, nested packet
theme object replacement, and source-sanitizer boundary against 11.16.0.
Kanban freezes Jison/KanbanDB behavior, cross-family `mindmap.padding` and
`mindmap.useMaxWidth` reads, JavaScript `sectionWidth` coercion, ticket links,
classic/rough theme painting, and the upstream-invisible frontmatter title.
Mindmap freezes its indentation database, `cose-bilkent` and explicit Dagre
layout paths, raw JavaScript configuration coercion, classic/Neo/hand-drawn
painting, safe raw-HTML links, and the upstream-invisible frontmatter title.

The next family must start with a fresh Gate-0 survey. Formula-driven families
remain preferable before additional force-layout families: architecture uses Cytoscape `fcose`,
and block diagrams use the custom recursive
`layoutBlocks` pipeline. No family is treated as supported until its upstream
oracle, native scene, pixel evidence, error paths, and export integration pass.
