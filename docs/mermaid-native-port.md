# Mermaid native C++ port

The flowchart execution contract and milestone history are maintained in
[`mermaid-flowchart-remaining-plan.md`](mermaid-flowchart-remaining-plan.md).
The complete 38-ID expansion and acceptance contract is maintained in
[`mermaid-11.16-complete-parity-plan.md`](mermaid-11.16-complete-parity-plan.md).

## Current status (2026-08-12)

Muffin renders thirty-one Mermaid families through a native C++20/Qt pipeline:

- flowchart/graph, including the `flowchart-elk` detector ID and Mermaid
  11.16's bundled Dagre fallback when no external ELK loader is registered;
- Swimlane (`swimlane-beta`, native Sugiyama or explicit Dagre layout);
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
- Block diagram (`block` and `block-beta`).
- GitGraph (`gitGraph`, including LR/TB/BT and parallel commit layouts).
- C4 diagrams (`C4Context`, `C4Container`, `C4Component`, `C4Dynamic`, and
  `C4Deployment`).
- TreeView diagram (`treeView-beta`).
- Event Modeling diagram (`eventmodeling`).
- Ishikawa/fishbone diagram (`ishikawa`).
- Venn diagram (`venn-beta`).
- Sankey diagram (`sankey` and `sankey-beta`).
- Treemap diagram (`treemap` and `treemap-beta`).
- Cynefin diagram (`cynefin-beta`).
- Wardley map (`wardley-beta`).
- Architecture diagram (`architecture-beta`).
- Gantt chart (`gantt`).
- Info diagram (`info`).
- Railroad grammars (`railroad-beta`, `railroad-ebnf-beta`,
  `railroad-abnf-beta`, and `railroad-peg-beta`).
- the error diagram: upstream's first-registered type. A literal `error`
  source renders the fixed lightbulb SVG (viewBox `0 0 2412 512`, six
  `.error-icon` paths, two `.error-text` lines at 150px/100px), and every
  parse/detector-stage failure attaches the same scene as the fallback
  visual for the export paths (PNG/SVG) — mirroring mermaid.core's
  `Diagram.fromText("error")` path — while the diagnostic contract (exact
  upstream messages) stays primary. The editor canvas deliberately keeps its
  source + diagnostic panel for Error entries (a Muffin editing surface
  locked by RenderMermaidBlockTest) instead of inlining the lightbulb.
  `suppressErrorRendering` is stripped by the shared secure-source
  sanitizer in both renderers, so the source API cannot disable the
  fallback. The `"---"` frontmatter-guard diagram (registered second
  upstream) returns the exact upstream parse-error message; frontmatter
  YAML failures happen before mermaid's try/catch upstream and therefore
  carry no fallback scene.

Each supported family has parser/database, layout, immutable scene, structural,
pixel, and editor-cache coverage. All 38 Mermaid 11.16 detector IDs now resolve
through a native adapter. The Windows Conan Release gate is currently 292/292
tests, including the end-to-end
`MuffinRenderMermaidBlockTest` and the error-diagram parity test.

All thirty-one native families now share `MermaidRenderMetadata` for the diagram
title, accessible title/description, role description, title styling, and
content-canvas geometry. Frontmatter titles are applied before family parsing,
so a sequence diagram's native `title` statement retains Mermaid's override
precedence. Timeline and Kanban are upstream exceptions: Timeline paints only
its inline `title`, while Kanban has no title grammar and ignores frontmatter
title entirely. Mindmap likewise has no title/accessibility grammar and ignores
frontmatter title rather than painting the common title band. TreeView ignores
visual titles but keeps `accTitle`/`accDescr` for SVG accessibility. Info accepts
the shared metadata grammar but its upstream parser discards the AST, so both
inline and frontmatter metadata remain invisible. Event Modeling has no common
metadata grammar and ignores frontmatter titles and accessibility metadata.
Ishikawa draws its root label inside the fish head and likewise suppresses
frontmatter/common accessibility metadata rather than adding a second title
band. Venn owns its inline title inside the family scene and, like upstream,
does not project common accessibility metadata. C4 preserves an upstream DB
quirk where `accTitle` overwrites the visible diagram title while the accessible
title remains empty; `accDescr` still reaches the SVG description. The common title painter is
used by the editor, print/PDF block path, PNG
export, and SVG export; title growth is included in scaling,
dirty-viewport culling, and flowchart link hit testing. HTML export now embeds
the native SVG fragment instead of a raster `<img>`. Its root carries
Mermaid-compatible `role`, `aria-roledescription`, `aria-labelledby`, and
`aria-describedby` attributes plus linked `<title>` and `<desc>` elements.

`MermaidSvgExporter` sends each immutable scene through its production painter
and Qt's SVG generator, then normalizes the root contract. Output is directly
embeddable XML with a stable ID, family class, upstream-compatible viewBox
presence, `useMaxWidth` sizing,
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
oracles. All thirty-one native scenes are integrated into the editor and print/PDF path
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
`MindmapDiagramConfig`, `BlockDiagramConfig`, `TreeViewDiagramConfig`, `EventModelingDiagramConfig`,
`IshikawaDiagramConfig`, `VennDiagramConfig`, `SankeyDiagramConfig`,
`TreemapDiagramConfig`, `CynefinDiagramConfig`, `WardleyDiagramConfig`,
`ArchitectureDiagramConfig`, `GitGraphDiagramConfig`, `C4DiagramConfig`,
`RailroadDiagramConfig`, and `GanttDiagramConfig`
declarations and writes the
committed `tests/fixtures/mermaid/config-effect-matrix.json` oracle. The
generator fails if an upstream family field is missing from the reviewed
policy or the policy contains a stale field. The current matrix contains 533
rows: 511 family-interface fields, five external-ELK option fields, and 17
shared root/theme/security fields (the `suppressErrorRendering` row records
the shared sanitizer strip — through the Markdown source API the error
diagram fallback stays enabled in both renderers).

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
| `parity` | 363 | Audited upstream and native stages agree |
| `upstream-inert` | 125 | Mermaid retains the option but 11.16.0 does not consume it |
| `legacy-only` | 19 | Applies to an old browser renderer, not the unified native scene |
| `api-only` | 25 | Function-valued hooks cannot be expressed by Markdown JSON/YAML config |
| `security-fixed` | 1 | Muffin intentionally keeps its strict desktop security policy |

The `themeVariables.*` row moved from partial to parity: every one of the
285 resolved inventory keys is byte-locked across all 11 themes through
`FlowThemeVariables::get()` (26 live keys wired to their family consumers, 30
upstream-dead keys modeled with zero-consumer evidence).

`MuffinMermaidConfigEffectMatrixTest` validates the generated fixture digest,
schema completeness, status invariants, all seven dimensions, and production
probes. The first audit also closed concrete gaps: Flowchart `diagramPadding`
now reaches editor/PNG canvas geometry, State `nodeSpacing`/`rankSpacing` reach
Dagre, Sequence honors root `%%{wrap}%%` and `showSequenceNumbers`, and
unsupported root `layout` or family `defaultRenderer` engines return
`configuration/unsupported-layout-engine`. Flowchart `elk` is the explicit
upstream exception: this pinned Mermaid runtime has no external ELK loader and
it intentionally warns, then renders through Dagre.

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
consumers. Native SVG export is now complete at the product boundary: all thirty-one
families produce deterministic, renderable fragments; HTML embeds them; and a
rendered diagram can be saved from its context menu. The matrix moved
`deterministicIds`, `deterministicIDSeed`, the effective family `useMaxWidth`,
and root `arrowMarkerAbsolute` rows to parity. Native SVG now serializes reusable
marker definitions for every marker-bearing family. With an explicit document
URL export context it emits Mermaid's absolute references for Flowchart,
Swimlane, and Sequence; the same-named family keys and all other families remain
fragment-only exactly as in Mermaid 11.16.0.

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

- ~~global `htmlLabels:false` is partial~~ closed: Requirement's
  `htmlLabels:false` path follows upstream `createFormattedText` exactly —
  SVG `<text>` rows at the fixed 1.1em dy from the font-cell top, the ±2px
  background rect, and the advance × cell-height getBBox feeding Dagre
  (pixel oracle: exact 253×718 canvas, IoU 0.999);
- external `mermaid.initialize()` object/array configuration is not part of
  Muffin's Markdown source API. In particular, source-level custom
  `borderColorArray`/`bkgColorArray` values are ignored, matching Mermaid
  11.16.0's source-entry behavior.

Arbitrary `themeCSS` is no longer a boundary: all 34 native family interfaces
resolve user CSS through the shared `MermaidCssCascade` against real 11.16.0
DOM oracles (`mermaid-theme-css.json`, 117 cases). Wardley is the one
upstream-inert exception — its `draw()` clears the svg before painting, so
themeCSS never reaches the DOM there and native parity holds by construction.
The error family resolves its six lightbulb paths individually
(`ErrorElementCss.icons`, index-aligned): structural selectors such as
`.error-icon:nth-of-type(2)` or `.error-icon + .error-icon` style single
paths, and the `error-diagram.json` `themeCssPerPath` oracle locks every
path's computed fill/stroke/stroke-width/opacity/display against the
browser. State resolves themeCSS against its full 11.16 DOM (clusters/
edgePaths/edgeLabels/nodes groups, per-node shape + label-span stacks,
`path.transition`, the edge-label `fo/div/span/p` chain): per-element
`StateElementCss` slots carry computed values into measurement (label
fonts, `.node rect{display:none}` — note nodes keep their size) and
painting, locked by the `state-layout.json` themeCss differential cases.

### State diagram contract (11.16.0)

State parity is evidence-based, not blanket: the marker is the concave
`M 19,7 L9,13 L14,7 L9,1 Z` barb whose fill/stroke is the GLOBAL
transitionColor via `defs [id$="-barbEnd"]` (themeCSS can restyle the marker
without touching per-edge strokes; the raster arrowhead follows the same
slot, and `stroke: none` disables either channel independently). Under
`look: neo` edges reference the `-margin` clone (refX 17, tip 2px past a
5.5px-shortened endpoint — `markerOffsets.arrow_barb_neo`), the common neo
sheet adds `drop-shadow(...)` filters to node rects/circles, the note/end
`g.outer-path`, and cluster outers (blur σ = radius/2; the redux-dark
family's `url(#drop-shadow)` resolves to a flat feDropShadow — dx4 dy4,
flood color/opacity from the theme — and the tint's own alpha multiplies
the coverage), and restyles every node shape stroke to `nodeBorder` at 1px.
Note/choice/fork/stateEnd shapes are rough.js output even under classic
look (roughness 0, solid fill — geometrically straight edges as a fill path
+ stroke path pair at the 1.3px `userNodeOverrides` default); themeCSS
resolves them as separate path elements, each with independent
display/visibility. Concurrent `--` partitions are single dashed grey
rects (10/10, altBackground), note connectors are the only dashed edges
(`.note-edge { stroke-dasharray: 5 }`), `click` statements become node
interaction regions (upstream wraps the node's `<g>` in an `<a xlink:href>`
— also modeled in the themeCSS DOM so anchor-descendant selectors
resolve), and the grammar has **no linkStyle** — `linkStyle 0 stroke:red`
parses as three plain state tokens. Edge-label backgrounds are the `<p>`'s
`edgeLabelBackground` (html labels; the SVG-label `rect` rule is dead),
`background-color: transparent` clears it, and edge-label boxes join the
scene bounds exactly like `svg.getBBox()` does. `visibility: hidden` hides
only paint (frames stay; display:none also collapses the layout box —
including pseudostate circles/paths), `fill:none`/`stroke:none`/declared
`stroke-width: 0` disable the pen/brush rather than degrading to black,
and cluster titles measure with their `.cluster-label` computed font.
The CSS opacity model composes each factor EXACTLY ONCE: the element
`opacity` slot carries the effective (ancestor-folded) value while
`fill-opacity`/`stroke-opacity` stay PURE declared factors, so the used
channel alpha is color alpha × opacity × channel (`opacity: 0.2`
renders 0.2, not 0.04 — the cascade engine's effective channel values
already fold the element opacity and must not be multiplied again).
A DECLARED `stroke-width: 0` disables the pen through `strokeWidthSet`
(no theme-width fallback), rect.inner is ONE element whose fill and
stroke channels both compose on the `innerCss` slot (a hidden inner
blanks the body, not just the outline), and the label `<p>` is the
TEXT carrier: it sits INSIDE the span, so its computed font/color
(folding the span chain) drive measurement AND paint while its own
channels ride `labelBackgroundCss` on edges (`.edgeLabel p {
font-size:31px; color:red }` grows the chip and the viewBox) and
`labelTextCss`/`descriptionTextCss` on nodes/clusters — the
rectWithTitle rows are the second foreignObject's own `<p>` and
measure separately (width = max(title, rows), height = title + rows).
`display:none` on a `<p>` collapses the LABEL BOX (a plain node shrinks
to its padding-only 16×16 rect, the cluster title band to zero, the
edge label reserves no space), not just the paint; on the DESCRIPTION
`<p>` it collapses the description block — the second foreignObject
measures 0×0 and a 0×0 foreignObject is EXCLUDED from label.getBBox(),
so the titled node's dagre box is the title alone (65 → 32 tall, the
9px title-rows gap vanishes with the rows) while the divider line
still paints inside the title-only box.
The drop-shadow filter input is the element's actual rendering (per
channel: color alpha × fill-/stroke-opacity × element opacity; a hidden
or none-painted shape casts no shadow; a filtered GROUP like stateEnd's
g.outer-path contributes each region separately — a transparent ring
with a visible inner dot casts a dot-sized shadow), and the marker's own CSS carries
display/visibility/opacity/stroke-width plus per-channel opacities — the
rendered arrowhead composites at the MARKER's opacity times the path's
(`defs [id$="-barbEnd"]` matches the id-carrying marker element; opacity
does not inherit, but fill-opacity DOES — a marker-element fill-opacity
reaches the path by inheritance). Cluster frames hide PER ELEMENT:
`rect.outer`, the label span, and `rect.inner` are siblings, and the
handDrawn renderer consumes the same pen gates as the smooth path
(including the note connector's 5,5 dash).
Titled diagrams reserve a 25 + round(font ascent) + 8 band above the
content (the title baseline sits at the ABSOLUTE -titleTopMargin,
centered on the content bbox at insert time), the title box can widen
the client box, and the exported SVG root carries the exact fractional
client viewBox and max-width — ORIGIN included (upstream writes
svgBBox(content ∪ title) ± padding with no translate; the scene keeps
the wrapper's dagre marginx/marginy of 8 absolute coordinates, fork/join
dagre boxes are 74×14 around the painted 70×10 bar, and the note-side
reflection re-anchors to the margin) — while the production raster snaps
to the nearest device pixel. Under `look: handDrawn` the node class
token becomes `rough-node` (`.node` selectors stop matching), plain
rects render as the rough pair whose fill path carries the hachure via
its stroke, and the rough renderer consumes the same CSS gates as the
smooth path. Pre-existing layout divergences remain open and
are tracked in `mermaid-architecture.md` (external-edge-into-cluster
cluster height, note-group ranking beside a composite, the
fork+note zig-zag, and handDrawn rough ink extents); until they
close, state is described as oracle-locked rather than strictly 1:1.

### Family expansion status

Pie, quadrant, journey, radar, XYChart, Timeline, Packet, Kanban, Mindmap, Swimlane,
TreeView, Event Modeling, Ishikawa, Venn, Sankey, Treemap, Cynefin, Gantt, and Info are now native. Each was implemented probe-first
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
Swimlane freezes its flowchart-derived grammar/database, lane-aware and gravity
Sugiyama layering, automatic lane ordering, orthogonal cross-lane routing,
line-hop modes, explicit Dagre override, classic/Neo/hand-drawn painting, and
the `flowchart`/`swimlane` configuration split against Mermaid 11.16.0.
TreeView freezes its Langium grammar, synthetic-root database, recursive fixed
layout, annotation/style cascade, icon reservation and stripped-`use` quirk,
source diagnostics, SVG accessibility, and fixed/max-width export behavior.
Event Modeling freezes its Langium grammar and frame/data database, swimlane and
relation formulas, literal Trebuchet SVG measurement, namespace and reset quirks,
14-key theme model, ignored metadata, and fixed/max-width export behavior.
Ishikawa freezes its Jison indentation tree, alternating fishbone placement,
RoughJS-compatible hand-drawn paths, Chromium text geometry, invisible
frontmatter metadata, and padding/fixed-width export behavior.
Venn freezes its Jison database, JavaScript scalar coercion, the exact
`@upsetjs/venn.js` 2.0.0 greedy/Nelder-Mead layout, intersection arcs, text-node
grid, classic/rough paint, theme cascade, zero-dimension viewport behavior, and
fixed/max-width export behavior.
Sankey freezes its CSV/Jison database, JavaScript `parseFloat` and scalar
coercion, the exact `d3-sankey` 0.12.3 six-pass breadth relaxation/collision
layout, D3 Tableau colors and gradient links, legacy/outlined labels, cycle
errors, theme cascade, and fixed/max-width export behavior.
Treemap freezes its indentation grammar/database, class cascade, JavaScript
number coercion, the exact `d3-hierarchy` 3.1.2 squarify/padding/rounding layout,
ordinal theme consumption, tile-derived label/value sizing, D3 value formats,
in-scene title/accessibility metadata, and fixed/max-width export behavior.
Cynefin freezes its Langium grammar/database, repeated-domain Map ordering,
transition filtering, JavaScript scalar coercion, deterministic seeded boundary
waves, fixed quadrant/confusion layout, all 11 built-in themes, nested 15-field
style object, in-scene title/accessibility metadata, and fixed/max-width export
behavior.
Gantt freezes its Jison database and date arithmetic, task dependency and
exclude/include semantics, D3-style time ticks, section/task/milestone/vertical
marker geometry, 11-theme paint model, safe task links, accessibility metadata,
and fixed/max-width export behavior.
Info freezes its Langium grammar and diagnostic locations, fixed version label,
400x150 replaced-element viewport, theme/font behavior, renderer-inert
`showInfo`, discarded metadata AST, and intentionally absent SVG viewBox.

Flowchart ELK is complete through the exact fallback bundled by the locked
Mermaid runtime: no external ELK loader is registered, so Mermaid emits its
migration warning and uses Dagre. Seven detector/config cases, immutable-scene
equivalence, and three browser PNGs freeze that behavior.

Architecture freezes its Langium grammar/database, Cytoscape 3.34 + fCoSE 2.2
non-random and spectral-randomized layouts, compound groups, directional ports,
orthogonal routing, icons, labels, all 11 themes, diagnostics, metadata, and
fixed/max-width export behavior. Its `title` statement is stored in the DB but
never rendered (draw() does not call insertTitle — the browser Title oracle
carries no title element and the untitled viewBox), so the shared title band
stays off and accTitle/accDescr keep their accessibility roles. The flowchart
family (with Swimlane) carries the exact FRACTIONAL viewBox extents upstream
writes (`0 0 426.75 70`) through `FlowScene::svgClientViewBox()` — the shared
client-box channel with `diagramPadding` as the viewBox padding: the layout
keeps Dagre's ABSOLUTE margin-anchored coordinates (`preserveDagreCoordinates`
+ the wrapper's hardcoded margin 8 via `dagreWrapperMargin`; classes keep
their own fixture-locked anchoring), and `useMaxWidth:false` writes the same
fractional box as the width/height attributes. Styled (bold/italic) inline
boxes measure through their real face's SHAPED advances — Qt's text
layout keeps the base face's advance table for weighted fonts, so
`styledRangeWidth` itemizes each segment with DirectWrite
(AnalyzeScript + AnalyzeBidi) at the INTERSECTION of script and bidi
boundaries — each atomic run takes its own resolved level and rounds
independently (a digit inside Hebrew keeps one script run but bidi level
2: Chrome's "א1ב" = 597+570+590, three roundings) — and then SHAPES
EVERY RUN WITH HARFBUZZ over the same
face's tables (`hb_face_create_for_tables` fed by
`IDWriteFontFace::TryGetFontTable`; kerning, ligatures, GSUB/GPOS — with
the `kern` feature explicitly on, as Chromium's font-kerning:auto does).
The font is scaled so one font unit = 1/128 px — Chromium's hb font funcs
return FLOAT advances (canvas measureText shows Arial Bold alef at
1193/128 = 9.3203125px, a half sixty-fourth), and each itemized run's
float sum snaps to LayoutUnit ONCE (round to the nearest 1/64, halves
away from zero), never per glyph: the kerned Arial Bold "AV" run rounds
21.0390625 → 21.046875, a Hebrew pair lands on the exact 18.53125, and
mixed "אA" rounds its Hebrew and Latin runs independently (597 + 740 =
20.890625). CSS letter/word spacing adds per grapheme cluster / word
separator and the two ADD — the letter receivers are real UAX #29
grapheme clusters (QTextBoundaryFinder) EXCEPT clusters whose FIRST
UTF-16 unit fails Blink's TreatAsZeroWidthSpace test (FormFeed/CR/
object-replacement or a BMP Default_Ignorable: bidi embedding controls,
standalone ZWJ/ZWNJ AND the Mn variation selectors FE00..FE0F/180B..180F
receive no spacing). Blink's `ShapeResultSpacing::ComputeSpacing` reads
the single UTF-16 unit at the glyph's cluster start, so a NON-BMP
ignorable cluster (language tag U+E0001, VS17 U+E0100) tests its high
surrogate — never ignorable — and still receives a unit when shaped
(Chrome: standalone VS16/VS1/FVS1 gain none, "A"+U+E0001 gains two).
All-or-nothing: missing-glyph segments fall
back to the legacy full-line width (other platforms too — no shaping
backend is linked there; tracked with the
Linux-font workstream). All 70 geometry cases are oracle-locked (63 exact
at 0.2 tolerance; 7 known measurement divergences — bidi shaper residue,
CJK fallback stacks, shape ink extents — print as PENDING-FAIL entries
with their root cause, stay drift-locked in both directions so a fix
self-trips the registry, and hard-fail under `MUFFIN_STRICT_PARITY=1`;
the oracle is Windows-only since the deltas are DirectWrite-recorded). GitGraph freezes its Jison grammar/database,
branch ordering, sequential and parent-driven parallel layouts in all three
directions, merge/cherry-pick routing, labels/tags, 11 themes, diagnostics,
metadata, and fixed/max-width export behavior. C4 freezes its five diagram
headers, recursive boundary placement, 20 element shapes, relationship routing,
style-update commands, text wrapping, all 11 themes, diagnostics, metadata
quirks, and fixed/max-width export behavior. Block, Swimlane, GitGraph, C4, and
the four Railroad grammar frontends are complete; all 38 registered detector
IDs plus the pre-registered `error` and `"---"` diagram types are native. No
family is treated as supported until its upstream
oracle, native scene, pixel evidence, error paths, and export integration pass.
