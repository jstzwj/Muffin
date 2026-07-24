# erDiagram — Native Implementation Spec (Muffin)

This document is the contract for the `.cpp` files implementing
`src/mermaid/erdiagram/`. The headers in that directory are **frozen**; this
spec resolves every upstream ambiguity the implementer needs. Conventions match
`src/mermaid/classdiagram/` and `src/mermaid/state/` exactly:

- Namespace `muffin::mermaid::er`.
- `.cpp` files define methods with fully-qualified names
  (`er::ClassName::method()`); they MUST NOT contain a `namespace muffin {}` or
  `namespace muffin::mermaid::er {}` block (lupdate/correctness rule from
  CLAUDE.md — even though this module has no `tr()`, we keep the convention).
  File-local helpers go in an anonymous `namespace { }` at file scope.
- C++20, Qt6, `QString`/`QStringLiteral`/`QLatin1String`/`QChar`. Include what
  you use.
- Parse errors are thrown as `er::ErParseError(ErDiagnostic{...})`
  (`std::runtime_error` subclass).
- No `tr()` calls anywhere.

Reference sources used to derive this spec:
`node_modules/mermaid/dist/diagrams/er/{erTypes.d.ts,erDb.d.ts,erMarkers.d.ts}`
and the bundled jison grammar + marker builder functions in
`node_modules/mermaid/dist/mermaid.js` (mermaid **11.16.0**).

---

## 1. Data model recap (ErDiagram.h — frozen)

| Struct | Key fields | Notes |
|---|---|---|
| `ErEntity` | `id`, `name`, `attributes`, `link`, `linkTarget`, `tooltip`, `haveCallback` | `name` is the display label; equals `id` unless an alias is given. |
| `ErAttribute` | `attributeType`, `attributeName`, `comment`, `keyType` | `keyType` ∈ {None,PK,FK,UK}. Single key per row (upstream `keys[]` reduced to first key). |
| `ErRelationship` | `entityA/B`, `cardA/B`, `identifying`, `roleA/B`, `label`, `comment`, `id` | `identifying=true` ⟺ `--` solid; `false` ⟺ `..` dotted. `id` = `"relN"`. |
| `ErCardinality` | ExactlyOne, ZeroOrOne, OneOrMore, ZeroOrMore | Glyph map in §5. |
| `ErDiagramData` | `title`, `accTitle`, `accDescription`, `entities`, `relationships` | No `direction` field — er direction is fixed (§3). |

---

## 2. ErTokenizer.cpp

### Lexer rules (maximal munch, in priority order)

| Rule | Token | Text carried |
|---|---|---|
| `\r\n` / `\n` / `\r` | `Newline` | the matched newline |
| `[ \t]+` | `Space` | the run (kept so `raw()` can rebuild source; the parser skips these) |
| `%%[^\n]*` | `Comment` | body after `%%`, trimmed |
| `erDiagram` (only when it is the first non-space token of a line and the header has not yet been emitted) | `ErHeader` | `"erDiagram"` |
| `[A-Za-z][A-Za-z0-9_-]*` | `Identifier` | the word. Internal hyphens allowed → `LINE-ITEM` is one token. |
| `"` … `"` (no escapes per upstream jison; a missing close is `Invalid`) | `QuotedText` | payload without the quotes |
| `\|\|` `\}o` `o\{` `\}\|` `\|\{` `\|o` `o\|` | `Cardinality` | the raw glyph (8 glyphs only) |
| `-+` | `Hyphen` | the run. Must be length 2 when used as a connector. |
| `\.+` | `Dot` | the run. Must be length 2 when used as a connector. |
| `\{` | `OpenBrace` | `{` |
| `\}` | `CloseBrace` | `}` |
| `:` | `Colon` | `:` |
| `,` | `Comma` | `,` |
| `PK` / `FK` / `UK` (only recognized as standalone words inside an attribute block — see precedence note) | `KeyPK` / `KeyFK` / `KeyUK` | the keyword |

**Precedence note for PK/FK/UK:** these are produced as `KeyXxx` only when the
bare word `PK`/`FK`/`UK` is matched AND it is not the leading type token of an
attribute row. The simplest correct approach: emit `Identifier` for any
`[A-Za-z][A-Za-z0-9_-]*` match, and in the **parser** reinterpret an `Identifier`
whose text is exactly `PK`/`FK`/`UK` as a key marker. Keep the `KeyPK/KeyFK/KeyUK`
kinds available for an alternative tokenizer strategy, but the parser MUST also
accept `Identifier("PK")` etc. so both paths work. (This avoids context-sensitive
lexer state, which the classdiagram tokenizer deliberately avoids.)

`next()` returns one token per call; `tokenize()` runs `next()` to `Eof` and
collects. On an unrecognized character emit `Invalid` with
`diagnosticCode = ErErrorCode::UnexpectedToken` and advance one char (the parser
turns any `Invalid` token into a Lexer-stage `ErParseError`, mirroring
ClassDiagram.cpp lines 100–103). Set `line`/`column`/`offset` on every token;
`offset` is the byte offset into `source_`.

`ErTokenCursor` mirrors `ClassTokenCursor` (same method semantics). `consumeLine()`
returns a sub-cursor spanning the tokens up to the next `Newline`/`Eof`. `raw()`
rebuilds source text from `tokens_[first].offset` to `tokens_[last].offset + length`
so inter-token spaces survive.

---

## 3. ErDiagram.cpp — parser

### Grammar (EBNF-ish; terminals are token kinds)

```
diagram       := ER_HEADER newline
                 ( statement )* EOF

statement     := comment
               | blankLine
               | accTitle
               | accDescription
               | titleDirective
               | entityBlock
               | relationship
               | entityAlias            # bare  ENTITY "Display Name"

comment       := COMMENT
blankLine     := (Space)? Newline
accTitle      := "accTitle" Colon <rest of line>
accDescription:= "accDescr" Colon <rest of line>
                 | "accDescr" OpenBrace <multi-line until matching CloseBrace>
titleDirective:= "title" <rest of line>     # accepted, stored in data_.title (frontmatter title wins)

entityAlias   := Identifier QuotedText      # id=Identifier, name=QuotedText; no body

entityBlock   := Identifier OpenBrace newline
                 ( attributeLine )*
                 CloseBrace
               # If a QuotedText immediately follows the Identifier it is the
               # alias (name), consumed before OpenBrace:
               #   Identifier QuotedText OpenBrace ... CloseBrace

attributeLine := (Space)? attribute (Space)? (Newline | inside-brace-EOL)
attribute     := Identifier Identifier              # type name
               | Identifier Identifier keyMarker   # type name PK|FK|UK
               | Identifier Identifier keyMarker QuotedText   # + comment
               | Identifier Identifier QuotedText             # + comment, no key
keyMarker     := Identifier-in-{PK,FK,UK}  (also KeyPK/KeyFK/KeyUK if emitted)

relationship  := entityRef connector entityRef ( Colon label )? newline?
                 # A "role" is a QuotedText adjacent to an entity reference.
entityRef     := Identifier (QuotedText)?          # id, optional role AFTER id
               | (QuotedText)? Identifier          # role BEFORE id
               # For the LEFT side, the optional role may precede or follow the id;
               #   store the first QuotedText as roleA. Same for right side / roleB.
connector     := Cardinality (Hyphen | Dot) Cardinality
                 # leftCard  "--"/".."  rightCard
                 # identifying = (middle token kind == Hyphen)
                 # Hyphen/Dot text MUST be length 2, else InvalidCardinality.
label         := <raw text of the rest of the line>   # may itself end in a
                                                         # trailing "comment" quote
```

**Concrete examples (all must parse):**
```
CUSTOMER ||--o{ ORDER : places
ORDER ||--|{ LINE-ITEM : contains
CUSTOMER }|..|{ DELIVERY-ADDRESS : uses            # non-identifying (..)
A "roleA" }o--o{ B : has                            # roles both sides
CUSTOMER {                                          # entity block
  string name PK "the key"
  int age
  bigint id FK
}
ENTITY "Display Name"                               # alias only
%% a line comment
```

### Cardinality glyph → `ErCardinality`

| Glyph | Token text | ErCardinality | side |
|---|---|---|---|
| `\|\|` | `"||"` | `ExactlyOne` | both |
| `\|o` | `"|o"` | `ZeroOrOne` | left (entityA) |
| `o\|` | `"o\|"` | `ZeroOrOne` | right (entityB) |
| `\}\|` | `"}\|"` | `OneOrMore` | left |
| `\|\{` | `"|\{"` | `OneOrMore` | right |
| `\}o` | `"\}o"` | `ZeroOrMore` | left |
| `o\{` | `"o\{"` | `ZeroOrMore` | right |

Implement a single lookup table mapping glyph-text → `ErCardinality`
(side is implicit in which of the two `Cardinality` tokens surrounds the
connector: the first is `cardA`, the second is `cardB`).

### Parser structure (mirror ClassDiagram.cpp)

- Anonymous-namespace `raise(...)` helper building `ErDiagnostic` from a source
  offset via a `spanForOffset()` that walks the source counting `\n`
  (copy the ClassDiagram.cpp implementation verbatim, swapping types).
- A `Parser` class constructed with `(source, limits)`, holding the tokenized
  `QVector<ErToken>` and an `ErTokenCursor document`. `parse()`:
  1. `maxTextSize` guard → `Resource`/`LimitExceeded`.
  2. Reject any `Invalid` token → `Lexer`/`UnexpectedToken`.
  3. Require `ErHeader` as the first meaningful line → else
     `Detector`/`MissingHeader`, expected `{"erDiagram"}`.
  4. Loop over lines via `consumeLine()`; dispatch on the leading token
     (`accTitle`/`accDescr`/`title`/`Identifier`-with-`OpenBrace`/
     relationship probe). `%%` comments and blank lines are skipped.
  5. Track open entity-block braces; EOF with an open brace →
     `Parser`/`MissingClosingTarget`... use `MissingClosingBrace`.
- `parseRelationship`: peek `Identifier` (entityA) + optional `QuotedText`
  (roleA) + `Cardinality` + (`Hyphen`|`Dot`) + `Cardinality` + optional
  `QuotedText` (roleB) + `Identifier` (entityB) + optional `Colon label`.
  Use the same sliding-window relation-probe technique as
  `ClassDiagram::parseRelation` (try the connector at the first plausible
  offset; on failure, if a partial connector was seen, raise
  `MissingRelationTarget`). Enforce `maxRelationships`. Auto-create both
  entities (`addEntity`) so a relationship implies its entity nodes even
  without an explicit block (matches upstream `addRelationship` semantics).
- `parseEntityBlock`: open brace, then each inner line is an `attribute`.
  Enforce `maxAttributesPerEntity` and `maxEntities`.
- `ErDiagram::parse` constructs a `Parser`, runs `parse()`, returns the
  populated `ErDiagram`.
- `erErrorStageName` / `erErrorCodeName` / `formatErDiagnostic`: identical
  shape to the `class*` helpers. `ErParseError`'s constructor calls
  `formatErDiagnostic` for the `runtime_error::what()` message, exactly like
  `ClassParseError`.

### `ErDiagram::toJson()` shape (upstream-comparable)

```jsonc
{
  "title": "...", "accTitle": "...", "accDescription": "...",
  "entities": [
    { "id":"CUSTOMER", "name":"CUSTOMER",
      "attributes":[
        {"type":"string","name":"name","comment":"the key","keyType":"PK"},
        {"type":"int","name":"age","comment":"","keyType":"None"} ] }
  ],
  "relationships": [
    { "id":"rel0", "entityA":"CUSTOMER", "entityB":"ORDER",
      "cardA":"ExactlyOne", "cardB":"ZeroOrMore",
      "identifying":true, "roleA":"", "roleB":"", "label":"places", "comment":"" } ]
}
```
`ErAttributeKeyType` serializes as `"None"|"PrimaryKey"|"ForeignKey"|"UniqueKey"`
via a small switch (use `erAttributeKeyTypeName` helper local to the .cpp).
`ErCardinality` serializes as the enum name string (`"ExactlyOne"` etc.).

---

## 4. ErLayout.cpp

Reuses the flowchart dagre pipeline exactly as `StateLayout.cpp` does. The er
diagram is a flat graph: vertices = entities, edges = relationships, **no
subgraphs**, **no clusters**.

### `buildErLayoutInput(const ErDiagramData&)`

- `direction = "TB"` (erDb default; erDiagram has no direction keyword).
- For each `ErEntity` build an `ErLayoutEntityInput`:
  - `id`, `name` copied.
  - `attributeLines`: one formatted string per attribute:
    `type + ' ' + name`, then ` (' PK|FK|UK ')` if `keyType != None`, then
    ` (' "' + comment + '"')` if `comment` non-empty. These lines size the
    entity table (max line width → table width).
  - `attributeStyles`: empty for now (placeholder; upstream populates
    compiled CSS — deferred).
- For each `ErRelationship` build an `ErLayoutRelationshipInput` copying all
  fields; assign `id = "relN"` (already set by parser).

### `measureErLayoutInput(input, fontFamily, fontSize)`

Mirror `measureStateLayoutInput`:
- `FlowTextOptions`: `fontFamily`, `fontPixelSize = fontSize`,
  `lineHeight = fontSize * 1.5`, `horizontalPadding = 16`, `verticalPadding = 16`.
- For each entity: measure the table.
  - Build a multi-line label: line 0 = entity `name`; lines 1..N =
    `attributeLines`. Join with `\n`.
  - `width  = measureLabel(joined, "markdown", opts).width() + 2*entityHPad`
    (entityHPad ≈ 8). Min width = name width + padding.
  - `height = (N+1) * lineHeight + 2*entityVPad` (entityVPad ≈ 6) plus a 1px
    header divider band. Use `measureLabel` per line to be exact, then sum.
  - Insert into `result.entities[id] = QSizeF(w,h)`.
- For each relationship with a non-empty `label`:
  `result.relationships[id] = measureLabel(label, "markdown", opts)`.

### `layoutErDiagramDagre(input, measurements, entitySpacing, rankSpacing)`

1. Build a `flowchart::FlowchartData projected`:
   - `projected.direction = input.direction` (`"TB"`).
   - For each entity: `FlowVertex{ id, text = name, type = "rect" }`.
   - For each relationship: `FlowEdge{ id, start = entityA, end = entityB,
     text = label, labelType = "markdown" }`. (Roles are NOT edge labels for
     dagre — they are rendered as crow's-foot-adjacent text by the painter.)
   - No subgraphs.
2. `FlowLayoutOptions options; options.nodeSpacing = entitySpacing;
   options.rankSpacing = rankSpacing; options.nodePadding = 8.0;
   options.measuredEdgeLabels = measurements.relationships;`
3. `auto placed = flowchart::layoutFlowchartNodesDagre(projected, measurements.entities, options);`
4. Map back:
   - `ErPlacementEntity{ id, center=QPointF(x,y), size=QSizeF(w,h) }` for each
     `FlowLayoutNode` (centers are the dagre node centers; the renderer treats
     them as box centers — dagre gives top-left? **Confirm against
     FlowchartLayout.h: `FlowLayoutNode.x/y` are the node CENTER** per the
     flowchart painter; use center semantics, matching StateLayout.cpp which
     passes `QPointF(node.x, node.y)` straight into `StatePlacementNode.center`.)
   - `ErPlacementRelationship` from each `FlowLayoutEdge`: copy `path`, `points`,
     `segments`; set `labelPosition` iff `edge.hasLabelPosition`.
5. Return the `ErPlacementResult`.

### `erLayoutInputToJson`

Same shape as `stateLayoutInputToJson` but with er field names
(`entities`/`relationships`, `cardA`/`cardB`/`identifying`/`roleA`/`roleB`).
Used only by layout goldens/tests.

---

## 5. Crow's-foot marker geometry (ErScenePainter.cpp)

**Authoritative source:** the marker builder functions `only_one`,
`zero_or_one`, `one_or_more`, `zero_or_more` in
`node_modules/mermaid/dist/mermaid.js` (mermaid 11.16.0). Each marker is an SVG
`<marker>` with `orient="auto"`, a `refX/refY` anchor, and a `markerWidth ×
markerHeight` box. The path `d` strings below are the **exact** upstream data.

A marker has two variants: **Start** (drawn at the entityA endpoint; its local
+X axis points from A toward B along the path) and **End** (entityB endpoint;
local +X points from B toward A). The variant chosen for a relationship is:
`cardA` → Start variant; `cardB` → End variant.

### 5.1 ONLY_ONE / ExactlyOne  (box 18×18)
```
Start  refX=0,  refY=9   path d="M9,0 L9,18 M15,0 L15,18"        # two ticks at x=9,15
End    refX=18, refY=9   path d="M3,0 L3,18 M9,0 L9,18"          # two ticks at x=3,9
```

### 5.2 ZERO_OR_ONE / ZeroOrOne  (box 30×18)  = one tick + a circle
```
Start  refX=0,  refY=9   circle cx=21 cy=9 r=6 (white fill)
                          + path d="M9,0 L9,18"                   # tick at x=9; circle to its right
End    refX=30, refY=9   circle cx=9  cy=9 r=6 (white fill)
                          + path d="M21,0 L21,18"                 # tick at x=21; circle to its left
```

### 5.3 ONE_OR_MORE / OneOrMore  (box 45×36)  = fork (leaf) + one tick
```
Start  refX=18, refY=18  path d="M0,18 Q18,0 36,18 Q18,36 0,18 M42,9 L42,27"
                          # quadratic leaf between (0,18)&(36,18) = crow's foot opening toward +X;
                          # tick at x=42
End    refX=27, refY=18  path d="M3,9 L3,27 M9,18 Q27,0 45,18 Q27,36 9,18"
                          # tick at x=3; leaf opening toward +X (apex at x=45)
```

### 5.4 ZERO_OR_MORE / ZeroOrMore  (box 57×36)  = fork + circle
```
Start  refX=18, refY=18  circle cx=48 cy=18 r=6 (white)
                          + path d="M0,18 Q18,0 36,18 Q18,36 0,18"
End    refX=39, refY=18  circle cx=9  cy=18 r=6 (white)
                          + path d="M21,18 Q39,0 57,18 Q39,36 21,18"
```

### 5.5 Drawing a marker at an endpoint (rotation by edge angle)

For an endpoint at world point `P` with the adjacent path segment direction
`v = (dx,dy)` (unit vector pointing **from this entity toward the other
entity** along the segment that touches this entity), and the marker's local
`(refX, refY)` anchor:

1. Parse the marker `d` string into a `QPainterPath` **in local box coords**
   using the exact SVG-path tokenizer already in
   `src/mermaid/classdiagram/ClassScenePainter.cpp` (`painterPath(source)` —
   copy that helper into an anonymous namespace here; it handles M/L/H/V/C/Q/Z,
   absolute & relative). The quadratic `Q` command IS handled by `QPainterPath`
   via `quadTo`; verify the helper maps `Q` → `quadTo` (add it if missing).
2. Build the transform:
   ```
   angle = atan2(dy, dx)            # radians
   QTransform t;
   t.translate(P.x(), P.y());        // move anchor to the endpoint
   t.rotateRadians(angle);           // align local +X with the segment
   t.translate(-refX, -refY);        // put the refX/refY point at local origin
   ```
   Apply `t` to the path; draw the path with the relationship pen. For the
   circle markers, draw a small filled white circle at `t.map(QPointF(cx,cy))`
   with radius `r` first (so the stroke draws over it), then the tick/fork path.
3. **Direction at each endpoint:**
   - entityA endpoint `P_A = points.first()`: segment direction =
     `unit(points[1] - points[0])`. Use the **Start** variant path for `cardA`.
   - entityB endpoint `P_B = points.last()`: segment direction =
     `unit(points[size-2] - points[size-1])` (points **from B back toward A**).
     Use the **End** variant path for `cardB`.
   Both endpoints therefore have their local +X pointing toward the opposite
   entity, which is exactly what the Start/End path data assumes.

The marker unit is 1 SVG unit = 1 device px at the default scale (mermaid does
not scale markers by strokeWidth — `markerUnits` is the default `strokeWidth` in
SVG, but the er markers are sized in absolute box coords and read correctly at
1:1). Keep 1:1; if markers look too small relative to a large font, scale the
whole transform uniformly by a `markerScale` (default 1.0) — do not change box
coords.

### 5.6 Relationship line style
- `identifying == true`  → solid pen (`Qt::SolidLine`).
- `identifying == false` → dashed pen, pattern `[6,4]` × strokeWidth
  (`Qt::CustomDashLine`), matching mermaid's `.er.relationshipLine` non-identifying
  rendering.

---

## 6. ErScene.cpp

`buildErScene(input, placement, style)`:

1. Start with an empty `ErScene`, copy `style`.
2. For each `ErPlacementEntity`:
   - Find the matching `ErLayoutEntityInput` by id (linear find; N is tiny).
   - `ErSceneEntity e; e.id; e.name; e.bounds = QRectF(center - size/2, size);`
   - Header band: `headerRect = QRectF(bounds.left(), bounds.top(), bounds.width(), style.lineHeight)`.
   - Attribute row rects: stacked below the header, each `QRectF(left, top + lineHeight*(i+1), width, lineHeight)`. If there are no attributes, the box is header-only (height = lineHeight + padding).
   - Build `nameDocument` via `flowchart::parseFlowLabel(name, "text")` (er labels are plain text, not markdown — use `"text"` label type; falls back safely). Build one `attributeDocuments[k]` per attribute line the same way.
   - Populate `ErSceneAttribute{text, keyType, comment}` from the input lines (re-split the formatted line, or — cleaner — pass the raw `ErAttribute` data through `ErLayoutEntityInput` by extending `attributeLines` parsing; **preferred**: in `buildErLayoutInput`, also stash the structured fields. Since the frozen header only carries `attributeLines`/`attributeStyles`, `buildErScene` re-parses each line: `type`, `name`, optional `(PK|FK|UK)` token, optional quoted comment. Keep a small line-parser helper in an anonymous namespace.)
3. For each `ErPlacementRelationship`:
   - Find the matching `ErLayoutRelationshipInput`.
   - Copy `path`, `points`, `segments`, `roleA/B`, `label`, `identifying`,
     `cardA`, `cardB`.
   - `labelDocument = parseFlowLabel(label, "text")` if label non-empty.
   - `labelSize`: measured size passed via `ErPlacementRelationship`? The
     placement does not carry sizes — measure here with
     `flowchart::measureLabel(label, "text", opts)` where opts mirror
     `measureErLayoutInput`.
   - `labelBounds` / `pathBounds`: compute from `labelPosition`/`labelSize` and
     `points` bounding box respectively (for culling).
4. `scene.bounds` = union of all entity + relationship bounds, then grow by the
   style padding.
5. Return the scene.

---

## 7. ErScenePainter.cpp

Anonymous-namespace helpers (copy/reuse from ClassScenePainter.cpp):
- `QColor color(const QString&)` → `mermaid::color::toQColor`.
- `QPainterPath painterPath(const QString& d)` → SVG path parser (add `Q`/`q`
  support if the copied helper lacks it).
- `void drawMarker(QPainter&, ErCardinality, bool start, QPointF P, QPointF segDir, const QPen&, qreal strokeWidth)` implementing §5.5.

`paintErScene(scene, painter, options)`:
1. `painter.setRenderHint(QPainter::Antialiasing, true);`
2. For each relationship (draw edges **before** entities so entities paint over
   the line ends — though markers should sit outside the box; cull each edge by
   `mermaidPrimitiveIsVisible(rel.labelBounds.united(rel.pathBounds), options)`):
   - Set pen: color = `relationshipColor`; width = `strokeWidth`; solid or
     custom-dash per `identifying`.
   - Build the edge `QPainterPath` from `points`/`segments`/`path` exactly as
     the flowchart/state painters reconstruct edge geometry (the dagre `path`
     string is already a full SVG path — feed it to `painterPath()`;
     alternatively replay `points` with `moveTo`/`lineTo`/`cubicTo`). Prefer the
     `path` string when non-empty; fall back to `points`.
   - Draw the path.
   - Draw the Start marker for `cardA` at `points.first()` and the End marker for
     `cardB` at `points.last()` per §5.5.
   - If `labelPosition`: paint `labelDocument` centered at `labelPosition` with
     a `labelBackground` rounded rect behind it (use
     `flowchart::paintFlowLabel` after drawing the background, mirroring how the
     flowchart edge label is painted).
   - Roles: paint `roleA`/`roleB` as small text near the corresponding endpoint,
     offset perpendicular to the segment (upstream renders roles inline with the
     edge; a simple offset placement is acceptable for the core subset).
3. For each entity (cull by `bounds`):
   - Fill `bounds` with `entityFill`, stroke with `entityStroke` / `strokeWidth`.
   - Draw the header band background (slightly different fill or a divider line
     under the header). Paint `nameDocument` centered in `headerRect` with
     `entityTitle1` via `flowchart::paintFlowLabel`.
   - For each attribute row: paint `attributeDocuments[k]` in `attributeRects[k]`
     with `attributeColor`. If `keyType` is set, draw a small badge/tag
     (PK=bold or a colored mark) — a minimal treatment: prefix the key letter to
     the rendered text in `attributeLines` already does this; the painter just
     draws the prepared document. A divider line between rows is optional.

`renderErSceneToImage(scene, dpr, padding)`:
- Compute `QSizeF logical = scene.bounds.size() + 2*padding`.
- `QImage img(ceil(logical.width()*dpr), ceil(logical.height()*dpr),
  QImage::Format_ARGB32_Premultiplied);` fill transparent.
- `QPainter p(&img); p.setRenderHint(Antialiasing,true); p.scale(dpr,dpr);`
- Translate by `(-scene.bounds.left() + padding, -scene.bounds.top() + padding)`.
- `paintErScene(scene, p, {});` return img.
(Mirror `renderClassSceneToImage` in ClassScenePainter.cpp for exact DPR math.)

---

## 8. Integration (owned by the orchestrator — noted for completeness)

1. **Detector:** `src/mermaid/MermaidDiagramDetector.cpp` already returns
   `"er"` for `^\s*erDiagram`. **No change needed.**
2. **Render cache dispatch:** `src/mermaid/editor/MermaidRenderCache.cpp` needs
   an `else if (type == QLatin1String("er"))` branch (alongside the
   `class`/`state` branches around lines 255–260 and the render branches around
   580/680) that:
   - calls `er::ErDiagram::parse(code)`,
   - builds `er::ErLayoutInput` via `er::buildErLayoutInput`,
   - measures via `er::measureErLayoutInput`,
   - places via `er::layoutErDiagramDagre`,
   - builds `er::ErScene` via `er::buildErScene`,
   - renders via `er::renderErSceneToImage` (or `paintErScene` for direct paint).
   Wrap parse in try/catch on `er::ErParseError` and map
   `er::formatErDiagnostic` into the cache's `MermaidDiagnostic` exactly as the
   class branch maps `ClassParseError`.
3. **CMake:** `CMakeLists.txt` `MUFFIN_CORE_SOURCES` is an explicit list (no
   glob). Add:
   ```
   src/mermaid/erdiagram/ErDiagram.cpp
   src/mermaid/erdiagram/ErTokenizer.cpp
   src/mermaid/erdiagram/ErLayout.cpp
   src/mermaid/erdiagram/ErScene.cpp
   src/mermaid/erdiagram/ErScenePainter.cpp
   ```
   The five headers are listed too if the CMake lists headers explicitly (the
   classdiagram headers are not separately listed because they are picked up as
   siblings of their .cpp by the IDE; follow whatever the classdiagram headers
   do — currently they are NOT in the source list, so do not add the er
   headers either).
4. **Config section:** er has no diagram-specific config consumed by the native
   path beyond theme colors; `MermaidRenderCache` can read
   `pre.config.value("er")` for future `entityPadding`/`fontSize` overrides but
   it is optional for the core subset.

---

## 9. Scope / deferrals (documented, not blocking)

- **MD_PARENT cardinality** (jison token 70): markdown-parent edge case; not in
  the crow's-foot set. Parser should reject `MD_PARENT` glyphs if encountered
  (they are not producible from the 8 crow's-foot glyphs).
- **Multi-key attributes** (`string id PK FK`): upstream `Attribute.keys[]` is an
  array. The frozen model carries a single `keyType`; parser takes the first key
  and ignores subsequent ones (or raises `InvalidAttribute`). Widen later if
  needed.
- **`click`/`link`/`callback`** interaction directives on entities: fields exist
  on `ErEntity`; the parser may accept and store them but the core subset does
  not require it. Mark as a follow-up.
- **classDef / inline `style`** on entities (`addClass`/`addCssStyles` in erDb):
  deferred — `ErSceneStyle` is a single global style for now.
- **neo / handDrawn looks:** out of scope; classic look only.
- **Roles rendering:** minimal offset placement; upstream inline rendering is a
  later refinement.

---

## 10. Test hooks (informational)

- `ErDiagram::toJson()` is the parser golden surface (mirror
  `tests/mermaid/MermaidFlowchartLayoutTest.cpp` / classDiagram test style).
- `erLayoutInputToJson()` + `layoutErDiagramDagre()` output is the layout golden
  surface.
- Fixture corpus: `node_modules/mermaid-cli/test-positive/erDiagram.markdown`
  plus the per-feature snippets in §3.
