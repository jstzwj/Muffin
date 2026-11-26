import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Captures real mermaid 11.16.0 erDiagram geometry via headless Chrome: every
// entity table with its name, rows, cells and dividers, every relationship
// path with its markers, and every label, all relative to the first
// source-order entity's centre (mirroring Muffin's FlowchartLayout origin
// subtraction). The fields are those of er::ErScene::toJsonObject, so
// MermaidErGeometryOracleTest compares the native scene to them one by one.
//
// The fonts are the four Noto faces the app bundles, so the numbers do not
// depend on the machine's fonts and Muffin measures text in the same faces
// (MermaidFontRegistry::cssFamilyStack()). The configuration is upstream's
// default, and a theme reaches a case through the `%%{init}%%` directive its
// source starts with, the way the port reads one.
//
//   node scripts/generate_mermaid_er_geometry_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/er-geometry.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const mermaidRoot = path.resolve(
  process.env.MERMAID_REFERENCE_ROOT ??
    process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "er-geometry.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js")).href
);

const er = (lines) => `erDiagram\n${lines.join("\n")}`;
const block = (name, lines) => `${name} {\n${lines.join("\n")}\n}`;
const rel = (a, card, b, label) => `${a} ${card} ${b} : ${label}`;
const styled = (lines) =>
  er([
    block("ORDER", ['string number PK,FK "order number"', "date placed", "string status", "decimal total"]),
    block("CUSTOMER", ["string name PK"]),
    rel("CUSTOMER", "||--o{", "ORDER", "places"),
    block("LINE-ITEM", ["int quantity", "string sku PK"]),
    rel("ORDER", "|o--|{", "LINE-ITEM", "contains"),
    ...lines,
  ]);
// A directive carries a configuration, the way a document reaches the port.
const init = (config, source) => `%%{init: ${JSON.stringify(config)}}%%\n${source}`;
const longName = "A-VERY-LONG-ENTITY-NAME-WITHOUT-ATTRIBUTES-THAT-WRAPS";
// A label with spaces is a quoted string in the grammar.
const longLabel = '"places orders for customers across regions, seasons and sales channels"';
const wrapping = er([rel(longName, "||--o{", "ORDER", "has"), "ANOTHER-QUITE-LONG-ENTITY-NAME-THAT-WRAPS-TOO", "SHORT"]);

const cases = [
  { id: "cardinality-types", source: er([rel("A", "||--||", "B", "r1"), rel("C", "|o--o|", "D", "r2"), rel("E", "}|--|{", "F", "r3"), rel("G", "}o--o{", "H", "r4")]) },
  {
    id: "entity-attributes-keys",
    source: er([
      block("CUSTOMER", ['string name PK "primary name key"', "int age", "bigint account_id FK", 'string email UK "unique email"']),
    ]),
  },
  { id: "relationship-with-label", source: er([rel("CUSTOMER", "||--o{", "ORDER", "places")]) },
  { id: "acc-title", source: er(["accTitle: ER Model", rel("A", "||--||", "B", "rel")]) },
  { id: "non-identifying", source: er([rel("CUSTOMER", "}|..|{", "DELIVERY-ADDRESS", "uses")]) },
  { id: "single-entity", source: er(["ACCOUNT"]) },
  { id: "chain", source: er([rel("A", "||--o{", "B", "r1"), rel("B", "||--o{", "C", "r2"), rel("C", "||--o{", "D", "r3")]) },
  {
    id: "fan-out",
    source: er([
      block("CUSTOMER", ["string id PK", "string name"]),
      rel("CUSTOMER", "||--o{", "ORDER", "places"),
      rel("CUSTOMER", "||--o{", "INVOICE", "receives"),
      rel("CUSTOMER", "||--||", "ADDRESS", "has"),
    ]),
  },
  {
    id: "wide-attributes",
    source: er([
      block("SHIPMENT", ['string tracking_number PK "carrier tracking id"', "date shipped_on", 'text notes "free form remarks"']),
      block("ITEM", ["string sku PK", "int quantity"]),
      rel("SHIPMENT", "||--|{", "ITEM", "contains"),
    ]),
  },
  {
    id: "self-relationship",
    source: er([block("EMPLOYEE", ["string id PK", "string manager_id FK"]), rel("EMPLOYEE", "|o--o{", "EMPLOYEE", "manages")]),
  },
  { id: "self-relationship-alone", source: er([rel("PERSON", "||--o{", "PERSON", "knows")]) },
  {
    id: "two-self-relationships",
    source: er([block("NODE", ["string id PK", "string parent FK"]), rel("NODE", "||--o{", "NODE", "children"), rel("NODE", "|o--|{", "NODE", "links"), rel("NODE", "||--||", "LEAF", "ends")]),
  },
  {
    id: "two-relationships-same-pair",
    source: er([rel("A", "||--o{", "B", "r1"), rel("B", "}o--||", "A", "r2")]),
  },
  {
    id: "mixed-identifying",
    source: er([rel("A", "||--o{", "B", "r1"), rel("B", "}|..|{", "C", "r2"), rel("C", "|o..o|", "D", "r3")]),
  },
  {
    id: "md-parent",
    source: er([block("NOTE", ["string body PK", "string author FK"]), rel("A", "}o--o|", "B", "parents"), rel("A", "|o..o{", "NOTE", "annotates")]),
  },
  {
    id: "tree",
    source: er([
      "ROOT",
      rel("ROOT", "||--o{", "LEFT", "left"),
      rel("ROOT", "||--o{", "RIGHT", "right"),
      rel("LEFT", "||--||", "LEAF-A", "a"),
      rel("RIGHT", "||--||", "LEAF-B", "b"),
      rel("RIGHT", "||--o{", "LEAF-C", "c"),
    ]),
  },
  { id: "blocks-without-relationships", source: er([block("A", ["string id PK"]), block("B", ["int age"]), "C"]) },
  {
    id: "ring",
    source: er([rel("A", "||--o{", "B", "ab"), rel("B", "||--o{", "C", "bc"), rel("C", "||--o{", "D", "cd"), rel("D", "||--o{", "E", "de"), rel("E", "||--o{", "A", "ea")]),
  },
  {
    id: "cjk-labels",
    source: er([rel("客户", "||--o{", "订单", "下单"), rel("订单", "||--|{", "明细", "包含"), block("订单", ["string 编号 PK"])]),
  },
  {
    id: "two-components",
    source: er([rel("CUSTOMER", "}|..|{", "DELIVERY-ADDRESS", "uses"), rel("ORDER", "}o..o|", "COUPON", "redeemed")]),
  },
  {
    // `a[Alias]` is a statement of its own: the table shows the alias, the
    // entity stays `a`.
    id: "aliases",
    source: er([
      'customer-account["Customer Account"]',
      'sales-order["Sales Order"] {',
      "  date placed_on",
      "  string status",
      "}",
      block("customer-account", ['string number PK "the account number"', "string name"]),
      rel("customer-account", "||--o{", "sales-order", "places"),
    ]),
  },
  {
    id: "direction-lr",
    source: er(["direction LR", block("A", ["string id PK", "string name"]), rel("A", "||--o{", "B", "r1"), rel("B", "||--o{", "C", "r2"), rel("A", "||--o{", "D", "r3")]),
  },
  {
    id: "direction-bt",
    source: er(["direction BT", block("ROOT", ["string id PK"]), rel("ROOT", "||--o{", "LEFT", "left"), rel("ROOT", "||--o{", "RIGHT", "right"), rel("LEFT", "||--||", "LEAF", "leaf")]),
  },
  {
    id: "direction-rl",
    source: er(["direction RL", rel("A", "||--o{", "B", "r1"), rel("B", "||--o{", "C", "r2"), rel("A", "|o--o{", "D", "r3")]),
  },
  {
    id: "word-cardinality",
    source: er([
      rel("A", "one or zero to zero or more", "B", "r1"),
      rel("B", "one or many optionally to one or more", "C", "r2"),
      rel("C", "one to many", "D", "r3"),
      rel("D", "many(1) to one or many", "E", "r4"),
    ]),
  },
  {
    // A name wider than its columns widens them; short names stay 100 wide.
    id: "name-wider-than-columns",
    source: er([
      block("A-VERY-LONG-ENTITY-NAME-INDEED", ["int q", "int r"]),
      block("X", ["int quantity_ordered_and_shipped", "int z"]),
    ]),
  },
  {
    id: "styles",
    source: styled([
      "classDef highlighted fill:#ff0000,stroke:#00ff00,stroke-width:3px,color:#0000ff",
      "class ORDER highlighted",
      "style CUSTOMER fill:#00ff00,stroke:#ff00ff",
      "style LINE-ITEM stroke-dasharray:5 5",
    ]),
  },
  // The same styled diagram in every theme that colors it its own way.
  ...[
    "base",
    "forest",
    "neutral",
    "dark",
    "neo",
    "redux",
    "redux-dark",
    "redux-color",
    "redux-dark-color",
  ].map((theme) => ({
    id: `theme-${theme}`,
    source: `%%{init: {"theme":"${theme}"}}%%
${styled([
      "classDef highlighted fill:#ff0000,stroke:#00ff00,stroke-width:3px,color:#0000ff",
      "class ORDER highlighted",
    ])}`,
  })),
  ...[
    ["html-labels-false", { htmlLabels: false }, styled([])],
    ["html-labels-true", { htmlLabels: true }, styled([])],
    // The deprecated diagram key stands in while the root key is unset.
    ["html-labels-false-flowchart", { flowchart: { htmlLabels: false } }, styled([])],
    [
      "html-labels-false-long-labels",
      { htmlLabels: false },
      er([
        rel("CUSTOMER", "||--o{", "ORDER", longLabel),
        rel("CUSTOMER", "}o..o|", longName, longLabel),
        block("客户", ["string 编号 PK", 'text 备注 "a comment long enough to be noticed"']),
        rel("客户", "|o--|{", "CUSTOMER", "下单"),
      ]),
    ],
    [
      "classdef-text-style",
      {},
      styled([
        'classDef big font-size:24px,font-weight:bold,font-style:italic,color:#0000ff',
        "class ORDER big",
        // A quoted font family does not lex in a style, so sizes only.
        "style CUSTOMER font-size:20px,font-style:italic",
      ]),
    ],
    [
      "theme-css-text",
      {
        themeCSS:
          ".label{font-size:24px} .node .label.name{font-weight:bold} .edgeLabel .label{font-size:20px} " +
          ".labelBkg{background-color:#ffff00} .label{color:#0000ff}",
      },
      styled([]),
    ],
    [
      "theme-css-paint",
      {
        themeCSS:
          ".node path{stroke:#00ff00;stroke-width:4px;stroke-dasharray:2,3;fill:#ffdddd} " +
          ".node rect{fill:#ff0000;stroke:#0000ff} .relationshipLine{stroke:#ff00ff;stroke-width:3px} " +
          ".marker{stroke-width:3px} .marker circle{fill:#ff0000} .row-rect-even path{fill:#ddddff}",
      },
      styled([]),
    ],
    ["long-attributeless-name", {}, wrapping],
    ["wrapping-width-120", { flowchart: { wrappingWidth: 120 } }, wrapping],
    // The limit of a relationship label is createText's own 200, whatever this says.
    ["wrapping-width-relationship", { flowchart: { wrappingWidth: 60 } }, er([rel("CUSTOMER", "||--o{", "ORDER", longLabel)])],
    ["look-neo", { look: "neo" }, styled([])],
    ["look-neo-theme-neo", { theme: "neo", look: "neo" }, styled([])],
    ["look-hand-drawn", { look: "handDrawn", handDrawnSeed: 17 }, styled([])],
    [
      "look-hand-drawn-attributeless",
      { look: "handDrawn", handDrawnSeed: 17 },
      er([rel("PERSON", "||--o{", "ORDER", "places"), rel("ORDER", "}o..o|", "COUPON", "redeemed"), "DELIVERY-ADDRESS"]),
    ],
    [
      "look-hand-drawn-self",
      { look: "handDrawn", handDrawnSeed: 17 },
      er([block("EMPLOYEE", ["string id PK", "string manager_id FK"]), "EMPLOYEE |o--o{ EMPLOYEE : manages"]),
    ],
    [
      "look-hand-drawn-styled",
      { look: "handDrawn", handDrawnSeed: 17 },
      er([
        block("ORDER", ["string number PK", "date placed"]),
        rel("CUSTOMER", "||--o{", "ORDER", "places"),
        "classDef highlighted fill:#ff0000,stroke:#00ff00,stroke-dasharray: 5 5,color:#0000ff",
        "class ORDER highlighted",
        "style CUSTOMER stroke:#333,stroke-width:4",
      ]),
    ],
    ["look-hand-drawn-svg-text", { look: "handDrawn", handDrawnSeed: 17, htmlLabels: false }, styled([])],
    ["look-hand-drawn-neo", { theme: "neo", look: "handDrawn", handDrawnSeed: 17 }, styled([])],
  ].map(([id, config, source]) => ({ id, source: config ? init(config, source) : source })),
];

const notoDir = path.resolve(import.meta.dirname, "..", "third_party", "noto", "fonts");
const fontFaces = [
  ["Noto Sans", "NotoSans-Regular.ttf"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf"],
]
  .map(
    ([family, file]) =>
      `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}")}`,
  )
  .join("\n");
const fontFamily =
  '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  // An SVG is as wide as `max-width` lets it, which a narrow page cuts down
  // and scales: a text laid out at another scale has other boxes. A page
  // wider than any diagram keeps the scale at 1.
  await page.setViewport({ width: 3000, height: 2200 });
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const results = await page.evaluate(
    async ({ cases, mermaidModule, fontFaces, fontFamily }) => {
      const number = (v) => Math.round(v * 1000) / 1000;
      const style = document.createElement("style");
      style.textContent = fontFaces;
      document.head.appendChild(style);
      // Chrome loads a face when a text first needs it, and measures that
      // text in the fallback font until then: load each face first.
      for (const [family, sample] of [
        ["Noto Sans", "CUSTOMER"],
        ["Noto Sans CJK SC", "编号"],
        ["Noto Sans Arabic", "مرحبا"],
        ["Noto Sans Hebrew", "שלום"],
      ])
        await document.fonts.load(`16px "${family}"`, sample);
      await document.fonts.ready;
      const { default: mermaid } = await import(mermaidModule);
      const cardName = {
        onlyOne: "ExactlyOne",
        zeroOrOne: "ZeroOrOne",
        oneOrMore: "OneOrMore",
        zeroOrMore: "ZeroOrMore",
      };
      // The crow's-foot marker kind behind a url(#...) value.
      const erMarker = (value) => {
        if (!value) return null;
        const m = value.match(/(onlyOne|zeroOrOne|oneOrMore|zeroOrMore)(Start|End)/);
        if (!m) return null;
        return cardName[m[1]];
      };
      const elementMatrix = (element, rootInverse) =>
        rootInverse.multiply(element.getScreenCTM());
      // A <rect>, in root user space.
      const absoluteBox = (element, rootInverse) => {
        if (!element || !rootInverse) return null;
        const box = element.getBBox();
        const matrix = elementMatrix(element, rootInverse);
        const corners = [
          new DOMPoint(box.x, box.y),
          new DOMPoint(box.x + box.width, box.y),
          new DOMPoint(box.x, box.y + box.height),
          new DOMPoint(box.x + box.width, box.y + box.height),
        ].map((p) => p.matrixTransform(matrix));
        const left = Math.min(...corners.map((p) => p.x));
        const right = Math.max(...corners.map((p) => p.x));
        const top = Math.min(...corners.map((p) => p.y));
        const bottom = Math.max(...corners.map((p) => p.y));
        return { x: left, y: top, width: right - left, height: bottom - top };
      };
      const absoluteOrigin = (element, rootInverse) =>
        new DOMPoint(0, 0).matrixTransform(elementMatrix(element, rootInverse));
      const rect = (x, y, width, height) => ({
        x: number(x),
        y: number(y),
        width: number(width),
        height: number(height),
      });
      // A path's data about `origin`: every second number is an x and the one
      // after it its y, whatever the commands between them.
      const relativePath = (pathData, originX, originY, matrix) => {
        const numberPattern = /-?\d+(?:\.\d+)?(?:e[-+]?\d+)?/gi;
        const tokens = pathData.match(numberPattern) ?? [];
        const points = [];
        for (let index = 0; index + 1 < tokens.length; index += 2) {
          const point = new DOMPoint(Number(tokens[index]), Number(tokens[index + 1])).matrixTransform(matrix);
          points.push(`${String(number(point.x - originX))},${String(number(point.y - originY))}`);
        }
        let pair = 0;
        let seen = 0;
        return pathData.replace(numberPattern, () => {
          const rest = seen++ % 2;
          return rest === 0 ? points[pair].split(",")[0] : points[pair++].split(",")[1];
        });
      };
      const translate = (element) => {
        const m = /translate\(([^,]+),\s*([^)]+)\)/.exec(element?.getAttribute("transform") ?? "");
        return m ? [Number(m[1]), Number(m[2])] : [0, 0];
      };
      // A label: the box of its <foreignObject>, in the coordinates of the
      // group that carries it.
      const labelBox = (label) => {
        if (!label) return null;
        const [x, y] = translate(label);
        const fo = label.querySelector("foreignObject");
        if (fo && Number(fo.getAttribute("width")) !== 0)
          return rect(x, y, Number(fo.getAttribute("width")), Number(fo.getAttribute("height")));
        // htmlLabels:false draws the text itself. A relationship label also has
        // the background rectangle Mermaid lays the label out by.
        // A table's text also has one, sized by nothing: it is not laid out by.
        const background = label.querySelector("rect.background[width]");
        if (background)
          return rect(x + Number(background.getAttribute("x")), y + Number(background.getAttribute("y")),
            Number(background.getAttribute("width")), Number(background.getAttribute("height")));
        // A text may carry a translate of its own, as a centered name does.
        const textElement = label.querySelector("text");
        if (!textElement) return null;
        const box = textElement.getBBox();
        const [tx, ty] = translate(textElement);
        return box.width === 0 && box.height === 0
          ? null
          : rect(x + tx + box.x, y + ty + box.y, box.width, box.height);
      };
      const text = (label) => (label?.textContent ?? "").trim();
      // The colour a label's text is painted in: `color` for an HTML label,
      // `fill` where the text is an SVG <text> of its own.
      const textColorOf = (label) => {
        const painted = label?.querySelector("text") ?? label?.querySelector("p") ?? label;
        const style = painted ? getComputedStyle(painted) : null;
        if (!style) return null;
        return rgb(painted.tagName === "text" ? style.fill : style.color);
      };
      const computed = (element, properties) =>
        element ? Object.fromEntries(properties.map((p) => [p, getComputedStyle(element)[p]])) : null;
      // rgb(r, g, b) as mermaid's stylesheet variables are written.
      const rgb = (value) => {
        const m = /^rgba?\(([^)]+)\)$/.exec(value ?? "");
        return m ? `rgb(${m[1].trim()})` : value;
      };
      // entity domId is `<svgId>-entity-<name>-<index>` where <name> may contain
      // dashes; <index> is the global source-order counter, so index 0 is the
      // first source entity (matches Muffin's first-vertex origin).
      const parseEntityId = (domId) => {
        const tail = (domId.includes("-entity-") ? domId.split("-entity-")[1] : domId).replace(/-background$/, "");
        const m = tail.match(/^(.*)-(\d+)$/);
        return m ? { id: m[1], index: Number(m[2]) } : { id: tail, index: -1 };
      };
      // What Chrome paints of an element: its fill and its stroke where it
      // has either, `null` where the property is none.
      const paint = (...elements) => {
        elements = elements.filter(Boolean);
        const firstWith = (property) => elements.find((element) => getComputedStyle(element)[property] !== "none");
        // Where several elements stroke the same shape, the widest one is
        // the stroke the shape shows.
        const stroking = elements
          .filter((element) => getComputedStyle(element).stroke !== "none")
          .sort((a, b) => Number.parseFloat(getComputedStyle(b).strokeWidth) - Number.parseFloat(getComputedStyle(a).strokeWidth))[0];
        const fill = firstWith("fill");
        const computed = stroking ? getComputedStyle(stroking) : null;
        return {
          fill: fill ? rgb(getComputedStyle(fill).fill) : null,
          stroke: computed ? rgb(computed.stroke) : null,
          strokeWidth: computed ? Number.parseFloat(computed.strokeWidth) : null,
          strokeDasharray: computed ? computed.strokeDasharray : null,
        };
      };
      // The outer rect of a table and each row rect are rough.js groups of
      // the paths that paint them: a fill path and a stroke path, or either
      // one alone where the theme leaves the fill or the stroke out.
      const roughPaint = (group) => paint(...group.querySelectorAll(":scope > path"));
      // A shape rough.js draws is two paths: the sketch of the fill, lines in
      // the fill's color, and the outline. Its fill is the sketch's color and
      // its stroke the outline's.
      const roughShapePaint = (group) => {
        const paths = group.querySelectorAll(":scope > path");
        const outline = paths[paths.length - 1];
        // Where the theme has no fill for the shape there is no sketch.
        const sketch = paths.length > 1 ? paths[0] : null;
        const computed = getComputedStyle(outline);
        return {
          fill: sketch ? rgb(getComputedStyle(sketch).stroke) : null,
          stroke: rgb(computed.stroke),
          strokeWidth: Number.parseFloat(computed.strokeWidth),
          strokeDasharray: computed.strokeDasharray,
        };
      };
      // What Chrome computes for an element, where a rule can change it.
      const paintOf = (...elements) => {
        const computed = paint(...elements);
        const stroking = elements.filter((element) => getComputedStyle(element).stroke !== "none")[0];
        return stroking
          ? { ...computed, strokeDasharray: getComputedStyle(stroking).strokeDasharray }
          : computed;
      };

      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily,
          themeVariables: { fontFamily },
        });
        const svgId = `er-geometry-${index}`;
        const { svg } = await mermaid.render(svgId, fixture.source);
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");
        // Mermaid sizes the SVG with a `max-width` it rounds to thousandths of
        // a pixel, short of the viewBox's own width, and Chrome snaps a CSS
        // length to 1/64 px: the diagram ends up scaled by a hair, and a text
        // laid out at any scale but exactly 1 has other boxes. Grow the viewBox
        // to whole pixels from its own origin, so the SVG can be laid out at
        // exactly scale 1; keep what the stylesheet said.
        const stylesheetWidth = root.getAttribute("width");
        const stylesheetHeight = root.getAttribute("height");
        const stylesheetMaxWidth = getComputedStyle(root).maxWidth;
        const [viewX, viewY, viewWidth, viewHeight] = (root.getAttribute("viewBox") ?? "").trim().split(/\s+/).map(Number);
        root.style.maxWidth = "none";
        const wholeWidth = Math.ceil(viewWidth), wholeHeight = Math.ceil(viewHeight);
        root.setAttribute("viewBox", `${viewX} ${viewY} ${wholeWidth} ${wholeHeight}`);
        root.setAttribute("width", String(wholeWidth));
        root.setAttribute("height", String(wholeHeight));
        const rootInverse = root.getScreenCTM()?.inverse();

        // The handDrawn look draws every entity by rough.js, in a group of
        // the class rough-node, over a copy of itself in the classic look.
        const nodeElements = [...root.querySelectorAll("g.node, g.rough-node")];
        const raw = nodeElements.map((node) => {
          const { id, index: entityIndex } = parseEntityId(node.id);
          // The id as Mermaid's document writes it, a copy's suffix included.
          const domId = node.id.includes("-entity-") ? node.id.split("-entity-")[1] : node.id;
          const centre = absoluteOrigin(node, rootInverse);
          // Attribute-bearing entities use <path class="outer-path">; the
          // attribute-less drawRect branch emits a standard .label-container.
          const outline =
            node.querySelector(".outer-path") ??
            node.querySelector(".label-container") ??
            // handDrawn: the shape is rough.js paths in a group of their own.
            [...node.children].find((child) => child.tagName === "g" && child.querySelector("path"));
          const box = absoluteBox(outline, rootInverse);
          return { node, id, domId, entityIndex, centre, box };
        });
        // Origin = first source-order entity (index 0), matching Muffin.
        const originEntity = raw.find((r) => r.entityIndex === 0) ?? raw[0];
        const originX = originEntity ? originEntity.centre.x : 0;
        const originY = originEntity ? originEntity.centre.y : 0;

        // What the document holds for one node, an entity or the copy drawn
        // behind it: its table with its name, rows, cells, dividers and paint.
        const describe = ({ node, id, domId, box, centre }) => {
            const nameLabel = node.querySelector("g.label.name") ?? node.querySelector("g.label");
            const outer = node.querySelector(".outer-path");
            const drawn = node.classList.contains("rough-node");
            const shapePaint = drawn ? roughShapePaint : roughPaint;
            const table = outer ? shapePaint(outer) : null;
            const container = node.querySelector(".label-container");
            const boxPaint = container
              ? drawn && container.tagName === "g"
                ? roughShapePaint(container)
                : paint(container)
              : null;
            const rows = [...node.querySelectorAll('[class^="row-rect-"]')].map((group) => {
              // The group has no transform of its own, so its bounding box is
              // the row's rectangle, in the table's coordinates.
              const rowBox = group.getBBox();
              const rowRect = rect(rowBox.x, rowBox.y, rowBox.width, rowBox.height);
              // The cells of this row: the attribute labels whose translate
              // lies in the row.
              const cells = {};
              for (const kind of ["type", "name", "keys", "comment"]) {
                const label = [...node.querySelectorAll(`g.label.attribute-${kind}`)].find((candidate) => {
                  const box = labelBox(candidate);
                  return box && box.y >= rowRect.y - 0.001 && box.y <= rowRect.y + rowRect.height + 0.001;
                });
                cells[kind] = label;
              }
              return { rect: rowRect, paint: drawn ? roughShapePaint(group) : paintOf(...group.querySelectorAll(":scope > path")), cells };
            });
            return {
              id,
              domId,
              // Where the node's transform puts the rectangle's own centre.
              centre: { x: number(centre.x - originX), y: number(centre.y - originY) },
              bounds: box
                ? rect(box.x - originX, box.y - originY, box.width, box.height)
                : null,
              // The table's own coordinates: its centre is the origin.
              name: { text: text(nameLabel), rect: labelBox(nameLabel) },
              rows: rows.map((row) => ({
                rect: row.rect,
                fill: row.paint.fill,
                stroke: row.paint.stroke,
                strokeWidth: row.paint.strokeWidth,
                strokeDasharray: row.paint.strokeDasharray,
                type: { text: text(row.cells.type), rect: labelBox(row.cells.type) },
                name: { text: text(row.cells.name), rect: labelBox(row.cells.name) },
                keys: { text: text(row.cells.keys), rect: labelBox(row.cells.keys) },
                comment: { text: text(row.cells.comment), rect: labelBox(row.cells.comment) },
              })),
              // A divider is a polygon a hundredth of a pixel thick: the line
              // through the middle of it.
              dividers: [...node.querySelectorAll(".divider")].map((divider) => {
                const b = divider.getBBox();
                return b.width >= b.height
                  ? { x1: number(b.x), y1: number(b.y + b.height / 2), x2: number(b.x + b.width), y2: number(b.y + b.height / 2) }
                  : { x1: number(b.x + b.width / 2), y1: number(b.y), x2: number(b.x + b.width / 2), y2: number(b.y + b.height) };
              }),
              fill: table ? table.fill : (boxPaint ? boxPaint.fill : null),
              stroke: table ? table.stroke : (boxPaint ? boxPaint.stroke : null),
              strokeWidth: table ? table.strokeWidth : (boxPaint ? boxPaint.strokeWidth : null),
              strokeDasharray: table ? table.strokeDasharray : (boxPaint ? boxPaint.strokeDasharray : null),
              textColor: nameLabel ? textColorOf(nameLabel) : null,
            };
                  };
        const byIndex = (a, b) => a.entityIndex - b.entityIndex;
        const entities = raw.filter((r) => !r.node.id.endsWith("-background")).sort(byIndex).map(describe);
        // The copies behind the entities, in the entities' order.
        const backgrounds = raw.filter((r) => r.node.id.endsWith("-background")).sort(byIndex).map(describe);
        // The relationship paths, in the document's order.
        const paths = [...root.querySelectorAll(".edgePaths path")].map((edge) => {
          const markerStart = erMarker(edge.getAttribute("marker-start"));
          const markerEnd = erMarker(edge.getAttribute("marker-end"));
          const klass = edge.getAttribute("class") ?? "";
          // Identifying relationships render with edge-pattern-solid; non-identifying
          // (..) render dashed/dotted.
          const identifying = klass.includes("edge-pattern-solid");
          // Computed stroke channel: the er stylesheet's own
          // `.edge-pattern-dashed { stroke-dasharray: 8,8; }` overrides the common
          // sheet's `3` (equal specificity, later rule wins), so this records the
          // value Chrome actually paints — the painter's dash constant must match.
          const computed = getComputedStyle(edge);
          return {
            dataId: edge.getAttribute("data-id"),
            path: relativePath(edge.getAttribute("d"), originX, originY, elementMatrix(edge, rootInverse)),
            // The marker drawn at an entity end shows THAT entity's cardinality:
            // marker-start sits at entityA (path start) => cardA; marker-end at
            // entityB => cardB.
            cardA: markerStart,
            cardB: markerEnd,
            identifying,
            stroke: rgb(computed.stroke),
            strokeDasharray: computed.strokeDasharray,
            strokeWidth: computed.strokeWidth,
            // The label on this path, if it carries one: a loop's label sits on
            // its middle path, the other two have none.
            label: (() => {
              const group = [...root.querySelectorAll(".edgeLabels > g")].find(
                (g) => g.querySelector("g.label")?.getAttribute("data-id") === edge.getAttribute("data-id"),
              );
              const box = labelBox(group?.querySelector("g.label"));
              if (!box) return null;
              const [gx, gy] = translate(group);
              const rectOut = rect(gx + box.x - originX, gy + box.y - originY, box.width, box.height);
              // The label's background: the HTML box's, or the fill of the
              // rectangle that stands in for it under htmlLabels:false. As
              // written, with its alpha, which the pixel keeps.
              const div = group.querySelector("div.labelBkg");
              const backgroundRect = group.querySelector("g.label rect.background");
              const background = div
                ? getComputedStyle(div).backgroundColor
                : backgroundRect
                  ? getComputedStyle(backgroundRect).fill
                  : null;
              return {
                text: text(group.querySelector("g.label")),
                rect: rectOut,
                textColor: textColorOf(group.querySelector("g.label")),
                background,
              };
            })(),
          };
        });

        // Every marker definition, in the document's order: what strokes it,
        // and what paints its ring.
        const markers = [...root.querySelectorAll("defs marker")].map((marker) => {
          const path = marker.querySelector("path");
          const ring = marker.querySelector("circle");
          return {
            id: marker.id,
            strokeWidth: path ? Number.parseFloat(getComputedStyle(path).strokeWidth) : null,
            stroke: path ? rgb(getComputedStyle(path).stroke) : null,
            ringFill: ring ? rgb(getComputedStyle(ring).fill) : null,
            ringStroke: ring ? rgb(getComputedStyle(ring).stroke) : null,
          };
        });

        // The canvas upstream sizes the SVG to: its viewBox, the width and
        // height attributes it writes, and the CSS max-width it sets.
        const canvas = {
          viewBox: [viewX, viewY, viewWidth, viewHeight],
          width: stylesheetWidth,
          height: stylesheetHeight,
          maxWidth: stylesheetMaxWidth,
          diagramWidth: number(root.getBBox().width),
          diagramHeight: number(root.getBBox().height),
        };
        const expected = { entities, relationships: paths, markers, canvas };
        if (backgrounds.length > 0)
          // A copy is known by its entity's full id.
          expected.backgrounds = backgrounds.map((copy) => ({ ...copy, id: copy.domId }));
        out.push({ id: fixture.id, source: fixture.source, expected });
      }
      return out;
    },
    { cases, mermaidModule, fontFaces, fontFamily },
  );

  const root = {
    upstream: {
      package: "mermaid",
      version: "11.16.0",
      notes:
        "Real mermaid 11.16.0 erDiagram geometry captured via headless Chrome with " +
        "the bundled Noto fonts. Every table with its name, rows, cells and dividers, " +
        "every relationship path with its markers, and every label, normalized " +
        "to the first source-order entity's centre (index 0), matching Muffin's " +
        "FlowchartLayout origin subtraction. cardA/cardB recovered from crow's-foot " +
        "markers (marker-start=cardA at entityA, marker-end=cardB at entityB). " +
        "Relationship paths are in document order. Compare against " +
        "er::ErScene::toJsonObject.",
    },
    mermaidVersion: "11.16.0",
    fontMode: "bundled-noto-2.13b171",
    oracle: "erDiagram.render tables, relationship paths, labels and canvas (first-entity normalized)",
    cases: results,
  };
  root.fixtureSha256 = createHash("sha256").update(JSON.stringify(root)).digest("hex");
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  console.log(`Wrote ${output} (${results.length} cases)`);
} finally {
  await browser.close();
}
