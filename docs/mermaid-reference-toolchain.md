# Mermaid reference toolchain (real mermaid 11.16.0)

The committed mermaid fixtures under `tests/fixtures/mermaid/` (`*-geometry.json`,
`*-layout.json`, `*-db.json`, `golden-pixel/`, `flowchart-dagre-snapshots.json`,
`config-effect-matrix.json`) are **real mermaid 11.16.0 output**, captured by
driving headless Chrome through `mermaid.render()` / dagre. They are the parity
ground truth Muffin's native renderer is checked against.

The capture scripts in `scripts/generate_mermaid_*.mjs` are **developer-run** (not
CI). They depend on a sibling checkout at `<repo>/../mermaid-cli` whose
`node_modules` provides `mermaid`, `dagre-d3-es`, and `puppeteer`, and they launch
the system Chrome.

The sibling default is overridable two ways: pass the mermaid package root as the
script's first argument, or set `MERMAID_REFERENCE_ROOT` (the geometry generators —
flowchart, er, class — check the env var first). This lets a single canonical
checkout live anywhere, e.g. an upstream `mermaid-cli` source clone (which already
ships `node_modules/mermaid@11.16.0`, `dagre-d3-es`, the puppeteer shim, and the
`#container` `index.html` the generators need — no setup script required).

## One-command setup

```bash
node scripts/setup_mermaid_reference_toolchain.mjs
```

Creates `../mermaid-cli`, installs the pinned dependencies, and writes the two
files a bare `npm install` does **not** provide (the generators need both):

- `index.html` with a `<div id="container">` mount (generators render into it).
- a shim at `node_modules/puppeteer/lib/puppeteer/puppeteer.js` that re-exports
  the package main, so the generators' legacy deep-import path resolves on any
  puppeteer version (modern puppeteer moved the file under `lib/cjs/`).

The script is **idempotent** — safe to re-run (it reinstalls and recreates the
two files). Accepts an optional sibling-path argument.

Requirements: Node ≥ 18, npm, git, and Chrome at
`C:/Program Files/Google/Chrome/Application/chrome.exe` (the generators' default;
overridable per-script).

## Version rationale

| dep | version | why |
|---|---|---|
| `mermaid` | `11.16.0` | Pinned — every fixture's `upstream.version` asserts this exactly. |
| `dagre-d3-es` | `7.0.14` | mermaid 11.16.0's own internal dagre dependency; the dagre-snapshots harness must match it to reproduce layout. |
| `puppeteer` | `^21.11.0` | Drives the system Chrome (`executablePath`), so its bundled Chromium is **skipped** (`PUPPETEER_SKIP_DOWNLOAD=true`) — no ~150 MB download. The shim makes the version irrelevant to the generators. |

## Verify reproducibility

After setup, regenerate a committed fixture and byte-compare — it must match
exactly (this is how the toolchain itself is proven):

```bash
node scripts/generate_mermaid_flowchart_geometry_fixture.mjs \
  "../mermaid-cli/node_modules/mermaid" /tmp/fg.json \
  "C:/Program Files/Google/Chrome/Application/chrome.exe"
diff tests/fixtures/mermaid/flowchart-geometry.json /tmp/fg.json   # => no output = identical
```

If a regenerated fixture differs, the toolchain drifted (wrong mermaid/dagre
version, or Chrome changed rendering) — investigate before trusting new
references.

## What it unlocks

- **Real-mermaid geometry oracles** — fail-on-divergence tests that assert
  Muffin's native geometry against captured mermaid 11.16.0 output, splitting
  font-independent parity (asserted) from font-coupled deltas (reported). All
  all nineteen native families are covered: flowchart (dagre-snapshots + geometry), ER
  (`er-geometry.json` + `MermaidErGeometryOracleTest`), class
  (`class-geometry.json` + `MermaidClassGeometryOracleTest`), state
  (`state-geometry.json` + `MermaidStateGeometryOracleTest`), and sequence
  (`sequence-geometry.json` + `MermaidSequenceGeometryOracleTest`, a pure
  structural oracle since the legacy renderer is positionally font-coupled),
  Requirement, pie, quadrant, journey, radar, XYChart, Timeline, Packet, Kanban,
  Mindmap, TreeView, Event Modeling, Gantt, and Info (family-specific geometry and pixel
  fixtures).
- `config-effect-matrix.json` covers all nineteen native families.
- Regenerating any drifted fixture (e.g. after a mermaid version bump, or when
  extending the case corpus).

## Generator inventory

Flowchart: `geometry`, `scene`, `db`, `label`, `pixel` (`golden-pixel`),
`dagre-snapshots`, `differential-fuzz`, `style-cascade`, `theme`, `diagnostic-coverage`.
Class: `db`, `layout`, `label`, `pixel`, `differential-fuzz`, `geometry`.
Sequence / state: `db`, `layout`, `label`, `pixel`, `differential-fuzz`,
`geometry` (sequence also `mathml-box`). ER: `geometry` (+ Muffin self-snapshot
`er-scene`). Requirement, pie, quadrant, journey, radar, XYChart, Timeline, Packet, Kanban, and Mindmap add their own parser,
geometry, structure, and pixel fixtures; the journey and radar generators pin Chrome
and bundled-font hashes.
Plus `config-effect-matrix`, `compatibility`, `error`, `rough-ops`, `katex-golden`.
