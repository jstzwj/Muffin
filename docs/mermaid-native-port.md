# Mermaid native C++ port

The detailed execution plan for the remaining flowchart work is maintained in
[`mermaid-flowchart-remaining-plan.md`](mermaid-flowchart-remaining-plan.md).

## Compatibility target

The compatibility baseline is `mermaid` 11.16.0 (MIT), resolved by the
`@mermaid-js/mermaid-cli` 11.16.0 lockfile at
`C:\Users\jstzw\Documents\github\mermaid-cli\package-lock.json`.

`mermaid-cli` is not the Mermaid implementation. Its rendering path starts a
Puppeteer browser, loads Mermaid and external layout packages, renders into an
SVG DOM, and captures SVG/PNG/PDF output. Muffin must instead port the Mermaid
semantic pipeline and render it through Qt:

1. shared preprocessing and diagram detection;
2. a parser and database model for each diagram family;
3. text measurement and graph layout;
4. shape, edge, label, theme, accessibility, and interaction rendering;
5. code-fence integration and export.

The first implemented layer is (1), in `src/mermaid`. It is linked into
`MuffinCore` and has no JavaScript or browser dependency. YAML frontmatter uses
the native `yaml-cpp` library.

The first flowchart parser milestone is in `src/mermaid/flowchart`. It exposes a
typed `FlowchartData` model for later layout work and currently has upstream DB
goldens for legacy node shapes, edge forms and labels, grouped/chained links,
parallel edge IDs, classes/styles, nested subgraphs, and string/Markdown labels.
The same comparison covers accessibility statements, click/callback/link data,
node and edge metadata (`@{...}`), explicit edge IDs, animation properties,
long link spellings, default `linkStyle`, tooltips, and Mermaid's default edge
and text-size limits. Compatibility remains scoped to those fixtures: the
remaining `flow.jison` productions and invalid-input branches must receive
explicit upstream cases before parser compatibility can be declared complete.

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
sub-pixel comparison. `FlowchartShapes` implements the 13 legacy flowchart
shapes: rectangle, rounded rectangle, circle, diamond, stadium, subroutine,
cylinder, asymmetric/odd, hexagon, trapezoid, inverse trapezoid, and the two
leaning parallelograms. Each has a browser-generated silhouette compared with
the Qt alpha mask.

The full Dagre compound pipeline is now the active flowchart layout engine
(`layoutFlowchartNodes` delegates to `layoutFlowchartNodesDagre`). All 22
geometry golden cases pass — including `compound-crossing`, which is no longer
marked `pendingNative`. The native Dagre port (nesting-graph, parent-dummy-
chains, add-border-segments, compound ordering, BK type-2 coordinate
assignment, normalize/acyclic/coordinate-system/self-edge handling, and the
27-phase `runLayout` orchestration) lives in `src/mermaid/dagre/`.

Mermaid's modern expanded shape catalogue still remains beyond the 13 legacy
shapes above, and silhouette masks validate geometry only; complete theme
pixels still require native fill/stroke, markers, labels, fonts, CSS/theme
mapping, and a whole-diagram painter. No editor integration is enabled while
those contracts remain incomplete. The legacy flat `WorkGraph` pipeline
(`FlowchartLayout.cpp` anonymous namespace) is now dead code pending removal.

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
determinism. The flowchart layout pipeline does not yet use this graph; it keeps
its flat `WorkGraph` until milestone C switches the Dagre compound pipeline
over, so existing non-compound geometry goldens are unaffected.

## Dagre compound pipeline (milestone C, in progress)

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

Still ahead for milestone C: the compound-aware ordering port (`order/*.js`,
C4), the full BK coordinate assignment with `findType2Conflicts` and block-graph
compaction (`position/bk.js`, C5), the remaining orchestration (`normalize`,
`acyclic`, `coordinate-system`, self-edge handling, `layout.js` glue), switching
`FlowchartLayout` from `WorkGraph` to the `DagreGraph` pipeline, and only then
dropping the `compound-crossing` `pendingNative` marker.

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

The remaining D items are landed (37 geometry golden cases, 122/122 ctest):

- **Curve variants.** `src/mermaid/flowchart/D3Curves.h` is a 1:1 port of the
  d3-shape curve state machines (`basis`, `linear`, `step`, `stepBefore`,
  `stepAfter`, `cardinal`, `monotoneX`, `monotoneY`, `bumpX`, `bumpY`,
  `catmullRom`, `natural`) driven through a `d3.path()`-compatible
  `PathContext`; `pathForCurve` dispatches by name, mirroring mermaid's
  `insertEdge` switch. `FlowLayoutOptions.curve` (default `basis`) selects the
  generator; the fixture generator re-initialises mermaid per case with
  `flowchart.curve`, and the tests read `curve` from the fixture. `monotoneX`
  is exercised on a horizontal edge and `monotoneY`/`bumpY` on a vertical one
  (the slope math hits `0*inf=NaN` on a degenerate collinear edge). The custom
  `rounded` curve (`generateRoundedPath`) is not a d3 curve and is deferred.
- **`labelpos` l/r + `labeloffset`.** Already faithful in the dagre port
  (`makeSpaceForEdgeLabels`, `fixupEdgeLabelCoords`, BK `sep()`, dummy
  propagation). mermaid hardcodes `labelpos:"c"` + `labeloffset:10` for all
  flowchart edges, so the l/r branches are present but inert; no separate
  golden is possible (mermaid never sets l/r).
- **Per-shape border intersection.** `intersectEllipse`/`intersectCircle`/
  `intersectPolygon` (+ `intersectLine`) are ported in `FlowchartLayout.cpp`.
  Edge extraction re-intersects the first/last interior point against each
  node's shape (`tail.intersect`/`head.intersect`): circle →
  `intersectCircle(r=w/2)`, diamond → `intersectPolygon` (vertices at ±w/2),
  rect/rounded → `intersectRect`. dagre-d3's `intersectLine` carries an
  `offset=abs(denom/2)` bias (a Graphics Gems integer-rounding trick that adds
  ±0.5 in floating point) which the goldens do not reproduce, so the true
  quotient `num/denom` is used. `diag-shapes` (diagonal edges into a circle
  and a diamond) verifies it; horizontal approaches hit the shape at the same
  point as the rect edge, which is why earlier rect-only goldens passed.
  Other polygon shapes (hexagon/trapezoid/stadium/…) fall back to
  `intersectRect` until diagonal goldens are added for them.
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
  `edge-interpolate` (edge 0 `linear`, edge 1 `basis`) verifies. Rendering the
  label background rect, stroke colour, and `classDef` styles on the painted
  path is milestone-E work (no painter yet).

## Milestone E (expanded node shapes) — started

Mermaid 11.16.0's flowchart shape registry (`shapesDefs` in
`chunk-65BZPYT2.mjs`) exposes 49 documented shapes selectable via
`@{ shape: NAME }`, each with a shortName, aliases, and a semanticName. The 13
legacy bracket shapes are ported; the remaining 36 expanded shapes are in
progress.

`src/mermaid/flowchart/FlowchartShapeRegistry.h` is the canonicalisation layer:
`canonicalShape(type)` collapses the three naming systems — legacy bracket name
(`"round"`), `@{ shape: }` shortName (`"rounded"`), and alias (`"event"`) — onto
one canonical key. The 13 legacy shapes keep their existing names as canonical
(so the existing measure/intersect/geometry if-chains work unchanged); expanded
shapes get descriptive canonicals (`double_circle`, `triangle`, …). It is wired
into `flowShapeGeometry`, `measureFlowchartNodes`, and `intersectNodeForShape`,
so every `@{ shape: }` alias now routes to its shape's handling. Unrecognised
names fall back to `rect` (mermaid's default shape).

`upstreamShortNames()` (the 49 names) and `nativeShapeCanonicalNames()` (the
ported set) feed `MuffinMermaidShapeRegistryTest`, a registry-diff gate that
fails if any upstream shortName is unmapped (a typo or missing entry) and
reports the ported/pending count. The diff must reach zero by the end of E.

Still ahead for E: port each expanded shape's geometry (`flowShapeGeometry`),
theme-dependent sizing (`measureFlowchartNodes`), border intersection
(`intersectNodeForShape`), and alpha-silhouette golden (extending the
`legacy-shapes` fixture). Each shape's handler in
`node_modules/mermaid/dist/chunks/mermaid.esm/chunk-65BZPYT2.mjs` is the source
of truth for its polygon points, padding, and `node.intersect`.

### E sizing batch — 16 shapes ported

`measureFlowchartNodes` sizing is ported and golden-verified (0.2 px tolerance)
for 16 expanded shapes via a new `expanded-shapes` fixture case (isolated nodes,
like `legacy-shapes`): `triangle`, `flipped_triangle`, `hourglass`,
`notched_pentagon`, `card`, `sloped_rect`, `divided_rect`, `lightning_bolt`,
`double_circle`, `filled_circle`, `crossed_circle`, `text`, `datastore`,
`tagged_rect`, `stacked_rect`, `lined_process`. The registry's native set is
now 30/49.

Calibration: mermaid's flowchart `node.padding` is 15 (the default), and
`labelHelper`'s `bbox` equals the raw label measurement (no extra padding), so
`measureLabel` == mermaid `bbox`. Each shape's `labelPaddingX/Y` = 15 (or 30
for `datastore`'s `drawRect` delegate), with shape-specific extras (NOTCH=12,
FRAME=8, rectOffset=5, tagWidth=0.2·h, etc.).

Two latent parser bugs surfaced and were fixed: `parseNode` now parses the
bracket part of `A[Label]@{ shape: X }` before applying metadata (it previously
took the whole `A[Label]` as the id), and the edge-metadata block no longer
throws "Unknown edge metadata id" when the id isn't an edge — it falls through
to node parsing (the throw was an uncaught exception that crashed the test on
node-metadata lines).

`fork` is deferred: its handler runs `node.height += padding/2` after
`updateNodeBounds`, so the dagre layout height (74) differs from the SVG bbox
(70) the golden captures — it can't satisfy both the sizing and layout checks
until the generator captures dagre's `node.height` for shapes with
post-`updateNodeBounds` size adjustments.

Remaining for these 16: per-shape border intersection (currently rect-fallback
— only `circle`/`diamond` have real polygon/ellipse intersect), the
`flowShapeGeometry` polygon/ellipse points, and the alpha-silhouette golden
(add `expanded-shapes` to the test's silhouette condition and extend the
QPainter rendering for `double_circle` etc.). Then the 19 wave/arc/cylinder
shapes (document, multi-document, tagged/lined document, horizontal/lined
cylinder, bow-tie rect, half-rounded rect, curved trapezoid, braces, flag,
bang, cloud, small/framed circle). None use SVG arc commands — all curves are
50-point polyline approximations, so `QPainter::drawPolygon` can render them
once the point generation is ported.

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
compares node/cluster geometry and every cubic path command and coordinate.
For all 13 legacy shapes the same fixture embeds isolated upstream PNG alpha
masks, allowing a pixel-level geometry comparison without mixing in theme or
font antialiasing. These masks are not described as theme pixel goldens.

Strict compatibility is claimed only for modules that have an upstream golden
and a passing native comparison. Parser, layout, and renderer coverage will be
added per diagram family. Until those layers exist, a `mermaid` code fence must
remain a normal code fence rather than silently producing an approximate graph.

## Port order

Flowchart is the first full diagram family because it exercises the shared
graph database, Dagre layout, most node shapes, edge markers, labels, classes,
styles, links, subgraphs, and Mermaid configuration. Sequence diagrams should
follow because their layout is diagram-specific and does not validate the
general graph path. Each family needs AST/database goldens, layout geometry
goldens, SVG/DOM geometry goldens, Qt pixel tests, invalid-input tests, and
recursion/size guards before editor integration is enabled for it.
