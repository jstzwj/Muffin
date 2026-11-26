# erDiagram — how the native implementation follows Mermaid 11.16.0

The port is a translation of upstream's own pipeline, stage for stage. Each
stage lives in its own file, names the upstream code it translates, and is
checked against real Mermaid by an oracle. When a stage is in doubt, read the
upstream file next to the native one; when a number is in doubt, run the
oracle.

Upstream paths are relative to the reference package
(`../mermaid-cli/node_modules/mermaid`, `src/`).

## Conventions

- Namespace `muffin::mermaid::er`.
- A `.cpp` of this directory defines its functions with fully-qualified names
  (`muffin::mermaid::er::...`); it contains no `namespace muffin { }` block
  (the lupdate rule in CLAUDE.md, kept although the module has no `tr()`).
  File-local helpers go in an anonymous namespace at file scope.
- A parse failure is `ErParseError`, a `DiagramError`, carrying an
  `ErDiagnostic`.

| Stage | Native | Upstream | Oracle |
|---|---|---|---|
| Text preparation | `MermaidPreprocessor.cpp` (`encodeMermaidEntities`) | `Diagram.fromText` | grammar |
| Lexer | `ErTokenizer.cpp` | `diagrams/er/erDiagram.jison`, lexer rules | grammar |
| Parser and database | `ErDiagram.cpp` | `erDiagram.jison`, productions and actions; `diagrams/er/erDb.ts` | grammar |
| Layout input | `ErLayout.cpp` (`buildErLayoutInput`) | `erDb.ts`, `getData` | grammar (ids), geometry |
| Measurement | `ErLayout.cpp` (`measureErTable`) | `rendering-util/rendering-elements/shapes/erBox.ts` | geometry |
| Placement | `ErLayout.cpp` (`layoutErDiagramDagre`) | `diagrams/er/erRenderer-unified.ts`, dagre | geometry |
| Scene | `ErScene.cpp` | `erBox.ts`, `diagrams/er/styles.ts` | geometry, pixel |
| Painter | `ErScenePainter.cpp` | `styles.ts`, the marker definitions | pixel |
| Theme | `editor/ErDiagramAdapter.cpp` | `styles.ts`, the theme constructors | geometry, theme CSS |

## 1. Text preparation

Mermaid hands a diagram `encodeEntities(text) + "\n"`: `&#`, `&` and `;` are
hidden behind private-use characters, so a semicolon cannot end a `style` or
`classDef` statement, and the text always ends with a newline.
`encodeMermaidEntities` does the same and returns the map back to the source,
which the parser uses to report positions. `buildErLayoutInput` is where the
entities of a relationship label are put back, as `createText` does.

## 2. Lexer

`ErTokenizer` is upstream's rule table: every rule of every condition, in
order, with its action. The first rule that matches at the offset wins, as in
jison. Three consequences are load-bearing:

- A rule that returns nothing (whitespace, most newlines) skips the text.
- In `block` the catch-all returns the character itself as the token, so `,`
  and `?` are terminals there.
- The text ends with `EOF` from the INITIAL rule and with the lexer's own end
  token in any other condition, which the grammar never accepts: an
  unterminated `accDescr {` is a parse error.

## 3. Parser and database

`ErDiagram.cpp` walks upstream's productions by recursive descent, one
function per nonterminal, and keeps Mermaid's database semantics:

- An entity is `entity-<name>-<n>`, numbered as entities are declared. Its
  `name` is what the source refers to; an alias fills the `label` the table
  shows.
- A relationship records both ends by entity id and has no id of its own.
- `classDef` keeps each class's declarations, `class` appends to an entity's
  class list, `style` appends to its inline declarations and stops at the
  first id that names no entity.
- A failure is an `ErParseError` with the stage (lexer or parser), the
  production, the token and the span. Its line is the line of the last token
  shifted, which is what `hash.loc` reports.

The limits (entities, attributes per entity, relationships, source length)
are the port's own and throw `LimitExceeded`.

## 4. Layout input

`buildErLayoutInput` is `getData`: the nodes in declaration order, and one
edge per relationship, numbered `id_<a>_<b>_<n>`, with the label, `markdown`
as its label type and the cardinality of each end.

## 5. Measurement

`measureErTable` is `erBox`, in the bundled fonts:

- Every text is Markdown in a `<p>`: a line height of 1.5. A table's texts
  are never wrapped (erBox gives each a `max-width` of its own width plus
  100); an entity without attributes wraps its name where the name and the
  padding do not fit `minEntityWidth`, at `flowchart.wrappingWidth`.
- An entity without attributes is a `.node rect`: the name centred, a
  `diagramPadding` on each side, one and a half above and below, and at least
  `minEntityWidth` wide.
- With attributes both paddings grow by 1.25 unless `htmlLabels` is true
  (erBox reads the root key raw: unset, it scales). A column is as wide as
  its widest text plus the padding, a column with no text is left out, and a
  name wider than the columns widens them evenly. A row is as tall as its
  tallest text plus the text padding.

How a text is set is `ErTextStyle` (`ErLayout.h`), and each of an entity's
five kinds of text — the name, the type, the attribute name, the keys, the
comment — has its own, as the stylesheet tells them apart by the class of
their label: `resolveErDocumentStyle` (section 7) gives them, and a classDef,
an inline `style` and themeCSS reach each.

`htmlLabels` is read by two readers, as upstream reads it. erBox takes the
root key: unset, it scales the paddings though it then draws an HTML label;
`false` draws SVG `<text>`, laid out by `layoutSvgText` (`FlowLabel.cpp`) as
Blink does — every word a `<tspan>` of its own whose advance is ceiled to a
layout cell of a sixty-fourth, the box the union of the cells and the ink
edges, lines 1.1em apart. A relationship's label reads the root key too and,
where it is unset, `flowchart.htmlLabels`. It wraps at createText's 200 px
whatever `wrappingWidth` says.

`ErTableMetrics` keeps what the table is drawn from: the name's box, each
row's box and cells, the widths of the columns.

Under the `handDrawn` look an entity is sized by what rough.js draws for it:
`measureErLayoutInput` draws the rectangle (`ErRough.cpp`: roughness 0.7, a
hachure of 4px lines 5.2 apart, the seed `handDrawnSeed`) and takes the tight
box of the drawing, sketch included. It is larger than the rectangle by a
pixel and a half on every side, and dagre centres that box, which puts the
rectangle off the centre it draws about; `ErLayoutMeasurements::rough` keeps
the drawing for the scene.

## 6. Placement

The entities and the relationships are projected into the shared dagre
pipeline, as state and class diagrams are: entities are vertices,
relationships are edges that carry their measured label, `nodeSpacing` is 140
and `rankSpacing` 80 unless configured, and the direction is the diagram's.

A relationship from an entity to itself is laid out through two invisible
0.1 × 0.1 points and drawn as three paths, as upstream draws a loop in every
diagram that does not merge it: the first carries the marker of the first
entity, the middle one the label, the last the marker of the second. Mermaid
names a loop's edges after the entity alone, so of several relationships from
an entity to itself only the last one survives in its graph, and the shared
pipeline keeps to that. The invisible points stay in the drawing as
0.1 × 0.1 rectangles, and count in the canvas.

## 7. Style and scene

`resolveErDocumentStyle` (`ErStyles.cpp`) builds the elements of the document
— the markers, each entity's shapes and five labels, each relationship's line
and label — and runs them through the shared cascade with erDiagram's
stylesheet (`erBaseCss`), the inline styles erBox writes and themeCSS. Two
passes: the second seeds each element's fallback from its parent's resolved
style, because the engine inherits only what the stylesheet's rules give an
ancestor, not an inline style. It gives `ErDocumentStyle`:

- `text`: the stylesheet's fonts, and per entity what each kind of text
  declares (size, weight, italics, line height, color).
- `paint`: for every entity its table, rows and dividers (`ErPaint`: fill,
  stroke, width, dash, and the filter over the table), for every relationship
  its line and the background of its label, and the markers. erBox writes the
  classDefs and the inline styles important on its paths, and in the redux
  themes the whole node style on every path, where the others write only what
  strokes; the even rows carry it too. The color themes pick a color per
  entity by its order.

`buildErScene` turns the placement into what is drawn, each element in the
paint resolved for it:

- A table: the name centred over rows of four cells (type, name, keys,
  comment), each cell at the table's edge plus half the padding and the
  widths of the columns before it, with the dividers between the name and the
  rows and between the columns. erBox appends the line under the name twice,
  as two elements; the scene holds it once and says so.
- The canvas: everything drawn, by minimum and maximum — the tables, the
  geometry of every path, the label boxes dagre reserved, the loop marks. It
  is the bounding box Mermaid's root reports, which a union of `QRectF` does
  not give, because a path's point is not a rectangle.
- The markers: `erMarker` is upstream's `erMarkerDef`, the start and the end
  variant of each cardinality; `MdParent` has none.
- The image is sized as the browser sizes the SVG element (`erRasterExtent`):
  Mermaid caps the element's width at its viewBox's, Chrome floors the cap to
  a layout cell, and the element keeps the viewBox's proportions.

The looks:

- `neo` (`ErSceneStyle::neo`): the stylesheet strokes every path of a node,
  the filling ones too, at the theme's stroke width, and fills no row; it
  strokes with the theme's gradient where the theme has one
  (`[data-look="neo"].node path { stroke: url(#…-gradient) }`, over each
  shape's own box from left to right; a shape with no width of its own, a
  vertical divider, takes the last color), and filters the table through the
  theme's `dropShadow` (`scene::NeoShadow`, shared with the state diagram).
  The `.labelBkg` is the tertiary color at half for every theme. The markers
  are `userSpaceOnUse` with the theme's stroke width, the ring filled with
  the background and, for "zero or more", moved nearer the crow's foot. And
  each line carries an inline dash of its own: the length of a solid line
  (`0 0 <len> 0`), and over a dashed one as many `2 2` as fit; because it is
  an inline style, `resolveErLineDashes` resolves the lines again with it
  once the paths exist, so a rule of themeCSS with `!important` still wins.
- `handDrawn` (`ErSceneStyle::handDrawn`): rough.js draws the shapes
  (`ErSceneEntity::rough`: the table, the rows, the polygons of the dividers)
  and the paths (`ErSceneRelationship::rough`), each shape a sketch of its
  fill in lines of the fill's color and an outline. The options are
  `userNodeOverrides`'s (`roughEntity`): the classDefs and the inline styles
  reach them alone, since no style is written on the paths. Behind every
  entity lies a copy of it (`ErScene::backgrounds`), drawn as the classic look
  draws it with two styles of its own that stroke nothing and fill with the
  theme's background. The dividers are a line through the box of their
  drawing, and the table's box the box of its drawing. The copies' labels
  take what the classDefs declare for them, not the inline styles.

## 8. Painter

In the order of Mermaid's document: the lines with their markers, the labels,
then the tables — and under the `handDrawn` look, first the copies behind
them.

- A table is drawn as erBox appends it: the table, each row with its fill and
  its stroke, the texts, the dividers. rough.js strokes a rectangle as four
  edges without a join (`strokeRectEdges`); under the `neo` look the filling
  path is stroked too, corners included, and a divider runs half a stroke past
  its ends.
- A marker is 1px wide in the line's color whatever the line's own width,
  its ring stroked like the rest, clipped to its viewport, and turned the way
  the path leaves or arrives (`scene::pathStartTangent`,
  `scene::pathEndTangent`; a drawn path's own ends under the `handDrawn` look,
  `roughPathStart` and `roughPathEnd`). Under the `neo` look the viewport is
  in user units.
- A relationship that is not identifying is dashed, 8 on and 8 off, or
  `2 2` under the `neo` look, with the gap of 4 after the last whole pair. A
  label is 14px text over `.labelBkg`, the tertiary color at half its opacity.
- Under the `handDrawn` look a shape is its sketch and its outline
  (`paintRoughShape`), and a line the path rough.js drew at a roughness of
  0.3.

SVG export asks the scene for `svgMarkerProjection` instead.

## 9. Oracles

| Test | Fixture | Generator | Asserts |
|---|---|---|---|
| `MermaidErGrammarTest` | `er-grammar.json` | `generate_mermaid_er_grammar_fixture.mjs` | the detector, accept or reject, the error class and line, every entity, relationship and classDef |
| `MermaidErGeometryOracleTest` | `er-geometry.json` | `generate_mermaid_er_geometry_fixture.mjs` | every table, row, cell, divider and paint, every path, marker and label, and the canvas, in eleven themes, both label kinds, and the three looks (54 cases) |
| `MermaidErTextOracleTest` | `er-text.json` | `generate_mermaid_er_text_fixture.mjs` | the width of 118 strings as an HTML box and as SVG text |
| `MermaidErRoughOracleTest` | `er-rough.json` | `generate_mermaid_er_rough_fixture.mjs` | every call erDiagram makes to rough.js under the `handDrawn` look (seed 17), op for op and number for number |
| `MermaidErPixelTest` | `er-pixel/` | `generate_mermaid_er_pixel.mjs` | the rendered image: 25 in the classic look, 9 under `neo`, 8 under `handDrawn` |
| `MermaidErStyleTest` | — | — | that a classDef and a style reach the scene |

To change a fixture: edit the generator, run it, then re-pin the digests in
its test. A generator writes LF, and its test hashes the bytes after undoing a
CRLF checkout.

## 10. Open

- `look-hand-drawn-svg-text` is listed as an open divergence of the geometry
  oracle. Under the `handDrawn` look a hachure line depends on the size of its
  shape, and three strings measure a hundredth of a pixel narrow as SVG text
  ("order number": 106.200 here, 106.213 in Chrome), which moves the sketch
  of a row by a quarter of a pixel. The texts of a diagram in HTML labels are
  exact.
- `handDrawnSeed` 0, Mermaid's default, makes rough.js draw from
  `Math.random()`: no two renders of a diagram agree, and the port draws from
  a fixed stream. A diagram pins its seed to be reproducible at all.
- themeCSS that styles the markers or the relationship labels by an id, and a
  `filter` that is not a `drop-shadow()` or the redux themes' filter element.
