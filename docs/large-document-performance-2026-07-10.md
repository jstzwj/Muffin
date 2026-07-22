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

Timing and working-set samples are emitted as schema-version-2 structured JSON but remain
informational, so machine load cannot make CI flaky. `parserRuns.open` and `parserRuns.reopen`
contain the parser total plus every cmark/conversion/annotation phase and its post-phase working
set. Collection is explicitly enabled by the benchmark; normal full parses and local edit slices
retain the uninstrumented path. Deterministic gates require local parsing, zero full-layout
fallbacks, bounded promoted slots, bounded piece-table fragmentation, exact undo/redo source and
tree restoration, byte-identical save/reopen, and AST equivalence after the full roundtrip.

Larger profiling runs accept 1-100 MiB fixtures:

```powershell
$env:MUFFIN_BENCH_SIZE_MB = "20"
$env:MUFFIN_BENCH_JSON = "large-document-roundtrip.json"
ctest --preset conan-release -R "^MuffinLargeDocumentRoundTripTest$" --output-on-failure -V
```

The edited tree is reduced to its digest, node counts, and SHA-256 fingerprint before reopening.
This preserves the complete deterministic comparison without keeping two full ASTs alive. After
removing the definition-insertion quadratic path, the Release baselines on the dense mixed fixture
measured:

| Metric | 50 MiB | 100 MiB |
| --- | ---: | ---: |
| UTF-8 bytes | 52,429,529 | 104,858,485 |
| Top-level / block / inline nodes | 747,211 / 2,081,512 / 2,294,998 | 1,486,579 / 4,141,180 / 4,565,914 |
| Open and parse | 14.467 s | 31.264 s |
| Structured parser open / reopen | 10.464 / 10.238 s | 23.151 / 22.627 s |
| Lazy layout index | 4.216 s | 8.095 s |
| First viewport | 73.4 ms | 61.6 ms |
| Top/middle/near-end edits | 2.47 / 4.66 / 4.48 ms | 3.71 / 6.06 / 6.44 ms |
| Undo all / redo all | 18.4 / 196.9 ms | 16.6 / 354.0 ms |
| Save | 266.5 ms | 353.4 ms |
| Working set before release | 2,089.9 MiB | 4,135.3 MiB |
| Release time / working set after release | 1.311 s / 43.0 MiB | 2.655 s / 51.0 MiB |
| Reopen time / working set | 18.238 s / 1,973.2 MiB | 48.139 s / 3,886.6 MiB |

Both sizes retained zero full-layout refreshes, 32 promoted slots, and 7 piece-table pieces.
Memory scales approximately with node count and returns close to the process baseline before the
second open, eliminating the previous double-AST peak. The structured parser scaling matrix was:

| Fixture | Open parser | Reopen parser | `insertMissingDefinitions` (open) |
| ---: | ---: | ---: | ---: |
| 25 MiB | 5.484 s | 5.283 s | 86.5 ms |
| 50 MiB | 10.464 s | 10.238 s | 144.2 ms |
| 75 MiB | 19.752 s | 18.328 s | 436.9 ms |
| 100 MiB | 23.151 s | 22.627 s | 305.9 ms |

The 75 MiB outer roundtrip coincided with heavy Windows memory compression, but both structured
parser samples remained bounded between the 50 and 100 MiB results. Across the 25-to-100 MiB
endpoints, bytes grew 4x and parser time grew 4.22x (open) / 4.28x (reopen), replacing the prior
superlinear result with approximately linear scaling.

The structured data identified `insertMissingDefinitions` as the old dominant cost. Although its
search cursor was monotonic, every synthetic definition still performed a middle insert into the
root `std::vector`, shifting the remaining nodes and making dense definitions quadratic. A single
detach/filter/append pass now feeds the existing stable source-order sort. At 25 MiB this phase fell
from 8.325 s to 86.5 ms (96x); at 50 MiB it fell from 38.924 s to 144.2 ms (270x). Total parser time
fell from 47.652 s to 10.464 s at 50 MiB and from 193.470 s to 23.151 s at 100 MiB.

The profiling work also exposed and now guards three definition/range defects: cmark-gfm collects
footnote definitions at the document tail; its footnote source start is after the `[^label]:`
marker; and local replacement could leave the synthetic document-root range stale. The old exact
start lookup missed each real cmark footnote and synthesized a duplicate. Footnotes now match by
source line, retain the parsed subtree, and expand back to the complete definition range. A
512-link/512-footnote dense regression verifies exact counts and source order, including indented
definitions. The corrected 100 MiB tree contains 106,184 fewer duplicate top-level/block nodes.

## Residual Risk

At working sets close to physical-memory pressure, Windows paging, memory compression,
font loading, or graphics-driver scheduling can still create occasional outliers that are
outside the deterministic edit path. Ordinary prose remains much smaller than the deliberately
node-dense mixed fixture. The remaining dominant parser phases are cmark construction and
`convertBlock`, while the reopened 100 MiB AST still occupies about 3.8 GiB. Future work should
reduce cmark/AST construction memory and post-parse materialization cost, using the structured
open/reopen phase data to distinguish stable regressions from paging or memory-compression outliers.
