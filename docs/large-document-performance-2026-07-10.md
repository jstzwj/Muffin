# Large-Document Typing Performance Review

Date: 2026-07-10

Updated: 2026-07-23

## Scope

This report covers the remaining input latency observed after the first round of
Piece Table, incremental parsing, line-index, outline, word-count, and pending-marker
work. Conclusions below are based on Release profiling rather than source inspection alone.

## Measured Remaining Costs

1. A structural edit queued a whole-document layout index rebuild. On a 2 MB fixture,
   `layout.range.nestedIndex+tops` took 8.803 ms and the complete queued range refresh
   took 9.618 ms. The cost scaled with every top-level block.
2. CSS `childIndex()` and `typeIndex()` walked every preceding sibling. An edit near the
   middle of a 250,000-block document could therefore inspect roughly 125,000 nodes for
   each `:nth-child` or `:nth-of-type` style query.
3. MainWindow synchronously recomputed status and command state from multiple signals for
   one keypress. The context refresh scanned the command declaration table eight times and
   repeatedly recalculated the same cursor, selection, table, heading, and image predicates.
4. `EditorView::paintEvent()` ignored the paint event region and repainted the complete
   viewport even when only a caret or one block was invalidated.

## Root Fixes

### Stable layout order index

Layout slots now hold stable tokens owned by an implicit treap. The tree provides current
rank and lazy vertical suffix adjustments. A structural splice replaces only the changed
token range; unchanged suffix tokens keep their address and accumulated shift.

The nested-node lookup is also updated per replaced subtree. Flat top-level leaf blocks do
not allocate descendant-removal vectors. Full prefix/suffix validation, Fenwick
materialization, full nested-index reconstruction, and `tops` reconstruction were removed
from the structural edit path.

### Persistent CSS ordinals

The document source-position tree now stores a compact fixed-category subtree summary.
Direct document children resolve sibling rank and same-type rank in O(log n). Nested lists
and tables retain their local linked-list fallback because those sibling groups are bounded
by one top-level block.

This keeps the sparse CSS adapter while removing its arbitrary-position linear scan. It
does not restore the previous full-document CSS element cache.

### Coalesced application state

Render cursor, source cursor, parsed, modified, and editor-state signals now converge on one
queued refresh. That refresh captures one immutable command-context snapshot and traverses
the command table once. Menu and toolbar state no longer blocks the key event that changed
the document.

### Dirty-region painting

The editor now clips all page, decoration, block, selection, and caret painting to the Qt
dirty region and asks the layout only for blocks intersecting that region. Animation host
tracking still uses the complete visible viewport so partial paints do not stop unrelated
visible animations.

## Complexity Change

| Operation | Before | After |
| --- | --- | --- |
| Layout suffix shift | O(log n), but materialized on splice | O(log n), preserved through splice |
| Structural layout index refresh | O(document nodes + slots) | O(log n + changed subtree) |
| Top-level `:nth-child` | O(preceding siblings) | O(log n) |
| Top-level `:nth-of-type` | O(preceding siblings) | O(log n) |
| Context action refresh per key | Multiple synchronous passes | One queued snapshot and one pass |
| Partial repaint | Full viewport | Dirty region plus bounded paint margin |

## Verification

- Release build completed with the Conan CMake preset.
- All 114 tests passed with `ctest --preset conan-release -j 4 --output-on-failure`.
- Added regressions cover source/layout token rank, lazy suffix preservation across a
  structural splice, incremental layout equivalence after an accumulated shift, and live
  top-level `:nth-child`/`:nth-of-type` matching.
- On a 20 MB synthetic GUI fixture with 195,999 top-level blocks:
  - `layout.range.localIndex`: 0.0019 ms
  - complete `view.refreshTopLevelRange`: 0.3397 ms
  - ordinary dash input: 0.39-0.66 ms after warm-up
- On `build/prose_100mb.md` (about 100 MB on disk, 38.8 million UTF-16 code units,
  389,460 top-level blocks):
  - local parse median at top/middle/end: 0.546 / 0.320 / 0.792 ms
  - lazy suffix shift median: 0.002-0.005 ms
  - working set after parse: 701 MB
  - working set after Lazy layout: 751 MB
  - working set after promoting the first visible window: 753 MB

The benchmark still invokes the old full pending-marker scan explicitly as an isolated
complexity oracle; it measured about 1,010 ms on this file. That scan is not called by the
editing or debounce paths.

## End-to-End Roundtrip Baseline (2026-07-23)

`MuffinLargeDocumentRoundTripTest` now provides one deterministic contract across the real
file, parser, document, lazy-layout, editor, undo/redo, and save paths. Its shared fixture
generator mixes front matter, headings, editable inline-rich paragraphs, alerts, nested task
and ordered lists, tables, code, math, HTML, Mermaid, references, footnotes, and Unicode.

The default CTest fixture is 1 MiB so the Release suite stays practical. The test performs:

1. UTF-8 fixture generation, asynchronous `FileController` open, and parse completion;
2. lazy `DocumentLayout` construction with no eager promotion, then first-viewport promotion;
3. local edits near the top, middle, and 90% position, followed by undo-all and redo-all;
4. a real save, destruction of the editor/layout/first session, and asynchronous reopen;
5. exact source SHA-256 and complete semantic/source-range AST fingerprint comparison.

Timing and working-set samples are emitted as structured JSON but remain informational, so
machine load cannot make CI flaky. Deterministic gates require local parsing, zero full-layout
fallbacks, bounded promoted slots, bounded piece-table fragmentation, exact undo/redo source and
tree restoration, byte-identical save/reopen, and AST equivalence after the full roundtrip.

Larger profiling runs accept 1-100 MiB fixtures:

```powershell
$env:MUFFIN_BENCH_SIZE_MB = "20"
$env:MUFFIN_BENCH_JSON = "large-document-roundtrip.json"
ctest --preset conan-release -R "^MuffinLargeDocumentRoundTripTest$" --output-on-failure -V
```

The edited tree is reduced to its digest, node counts, and SHA-256 fingerprint before reopening.
This preserves the complete deterministic comparison without keeping two full ASTs alive. The
Release baselines on the dense mixed fixture measured:

| Metric | 50 MiB | 100 MiB |
| --- | ---: | ---: |
| UTF-8 bytes | 52,429,529 | 104,858,485 |
| Top-level / block / inline nodes | 800,583 / 2,134,884 / 2,294,998 | 1,592,763 / 4,247,364 / 4,565,914 |
| Open and parse | 51.417 s | 200.590 s |
| Parser-reported time | 47.652 s | 193.470 s |
| Lazy layout index | 5.218 s | 9.426 s |
| First viewport | 68.0 ms | 68.0 ms |
| Top/middle/near-end edits | 3.91 / 5.35 / 6.32 ms | 4.33 / 3.71 / 6.17 ms |
| Undo all / redo all | 18.7 / 195.6 ms | 11.8 / 382.2 ms |
| Save | 234.1 ms | 373.9 ms |
| Working set before release | 2,130.9 MiB | 4,216.3 MiB |
| Release time / working set after release | 1.023 s / 43.2 MiB | 2.482 s / 51.0 MiB |
| Reopen time / working set | 54.510 s / 2,002.3 MiB | 198.352 s / 3,924.4 MiB |

Both sizes retained zero full-layout refreshes, 32 promoted slots, and 7 piece-table pieces.
Memory scales approximately with node count and returns close to the process baseline before the
second open, eliminating the previous double-AST peak. The 50-to-100 MiB parse time increased by
about 4x while input and node counts increased by about 2x. The next profiling pass should split
the full parser/annotation pipeline by phase; the edit and first-viewport paths are not the current
scaling bottleneck.

The first complete run exposed and now guards two source-range defects: cmark-gfm collected
footnote definitions at the document tail instead of source order, which made local suffix shifts
move the wrong definitions; and local replacement left the synthetic document-root range stale.
Top-level nodes are now restored to source order, and the root range is refreshed with the same
front-matter and trailing-line semantics as a fresh cmark parse.

## Residual Risk

At working sets close to physical-memory pressure, Windows paging, memory compression,
font loading, or graphics-driver scheduling can still create occasional outliers that are
outside the deterministic edit path. Ordinary prose remains much smaller than the deliberately
node-dense mixed fixture, but the 100 MiB result now demonstrates a parser-side superlinear trend.
Future work should use the existing per-phase parse diagnostics to identify that cost before
adding another document-wide cache.
