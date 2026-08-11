# Mermaid 11.16 complete native parity plan

## Baseline and completion contract

The only behavioral baseline is the installed `mermaid` 11.16.0 package under
`G:\github\mermaid-cli\node_modules\mermaid`. `mermaid-cli` is used only as the
browser host; grammar, database transforms, layout algorithms, theme cascade,
SVG DOM, diagnostics, and raster output come from Mermaid itself.

A diagram ID is native only after all of these gates pass:

1. source-entry detector and preprocessing parity, including directives,
   frontmatter, comments, case sensitivity, prefixes, and source mapping;
2. lexer/parser/database parity with exact acceptance, transformed data,
   JavaScript coercion, runtime errors, and line/column diagnostics;
3. the same upstream layout algorithm and constants, with deterministic
   geometry fixtures captured from Mermaid 11.16.0;
4. immutable scene and painter parity for DOM order, shapes, paths, text,
   themes, CSS used values, accessibility, and interaction regions;
5. production PNG and SVG parity, including browser replaced-element sizing,
   DPR, root attributes, metadata, safe links, and deterministic IDs;
6. every live/inert configuration key classified in the generated effect
   matrix and exercised through the real source-entry path;
7. family tests plus the complete Conan Release build and CTest suite.

Unsupported input remains editable source. Muffin must not silently substitute
Dagre, a hand-written approximation, or a visually similar diagram when the
registered Mermaid algorithm has not been ported.

## Registered diagram inventory

Mermaid 11.16.0 registers 38 IDs. Muffin currently implements 27 IDs as 24
logical families:

`flowchart-v2`, `flowchart`, `sequence`, `classDiagram`, `class`,
`stateDiagram`, `state`, `er`, `gantt`, `info`, `pie`, `requirement`,
`timeline`, `journey`, `quadrantChart`, `radar`, `xychart`, `packet`, `kanban`,
`mindmap`, `treeView`, `eventmodeling`, `ishikawa`, `venn`, `sankey`, and
`treemap`, and `cynefin`.

The 11 remaining IDs are:

`flowchart-elk`, `architecture`, `c4`, `swimlane`, `git`, `block`,
`railroad`, `railroad-ebnf`,
`railroad-abnf`, `railroad-peg`, and `wardley`.

## Delivery order

### Phase 0: global compatibility ledger

- Freeze all 38 detector IDs, loader precedence, aliases, and error boundaries.
- Extend the generated configuration matrix and shared cache/SVG/PNG tests as
  each family becomes native.
- Keep generator provenance pinned to Mermaid, diagram chunks, Chrome, fonts,
  and external layout package hashes.

### Phase 1: bounded and formula-driven families

Info, TreeView, Event Modeling, Ishikawa, and Venn are complete.
These have bounded or explicit layout rules and establish reusable set and
annotation primitives without introducing another general graph engine.

### Phase 2: data and domain-layout families

Sankey, Treemap, and Cynefin are complete. Implement `wardley`. Port its
exact domain algorithms and ordering rules; use structured parsers and upstream
scales rather than string or pixel heuristics.

### Phase 3: structural and board families

Implement `architecture`, `block`, `swimlane`, `git`, and `c4`. Preserve each
family's own recursive placement, routing, icon, grouping, and interaction
semantics. Shared primitives may be reused only after their contracts match.

### Phase 4: railroad grammar family

Implement the shared railroad scene/layout engine, then four independent
frontends: `railroad`, `railroad-ebnf`, `railroad-abnf`, and `railroad-peg`.
Their grammars and diagnostics remain separate even when drawing primitives are
shared.

### Phase 5: Flowchart ELK

Port the exact ELK-backed flowchart path, including option projection,
compound-node geometry, ports, edge sections, labels, and error behavior.
`flowchart-elk` must never fall back to Dagre. The external ELK version and
layout output become explicit reproducible oracles.

### Phase 6: final global gate

- Regenerate every fixture twice and require byte-stable output.
- Run the complete Release build, complete CTest suite, dist packaging, and a
  packaged-app smoke test.
- Recount all registered IDs and require every one to resolve to a native
  adapter with parser, scene, painter, config, cache, PNG, and SVG evidence.
- Update the compatibility documentation only from generated inventories.

## Implementation rules

- Read the upstream implementation and source maps before designing native
  types. Preserve observable upstream bugs unless a security or resource limit
  is explicitly documented and tested as a native policy.
- Keep parser/database, layout, scene, painter, and adapter boundaries separate.
  The adapter owns preprocessing/config projection; the parser never guesses
  renderer behavior; the painter never recomputes semantics.
- Reuse existing text, color, CSS, shape, graph, rough, and export primitives
  only when their used-value behavior is identical. Add a family-specific
  primitive when sharing would hide a contract difference.
- Every manual safety ceiling must fail deterministically with a typed
  diagnostic. It must not mutate valid diagrams below the ceiling.
- A family is committed only when its focused gates and the full Release gate
  are green. Each family is one reviewable commit before the next begins.
