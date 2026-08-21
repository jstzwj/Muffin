import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Captures real mermaid 11.16.0 erDiagram geometry (entity table bounds +
// relationship paths + cardinality) via headless Chrome, normalized to the
// first source-order entity's centre (mirroring Muffin's
// FlowchartLayout origin subtraction). Output schema matches the fields of
// er::ErScene::toJsonObject that are comparable against mermaid's DOM.
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
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

// Corpus adapted from er-db.json. NOTE: mermaid 11.16's render-time ER grammar
// REQUIRES a relationship label (`: name`) and REJECTS the per-end role syntax
// (`A "role" }o--o{ "role" B`), so every relationship here carries a label and
// the roles case is dropped. (Muffin's parser accepts unlabeled relationships —
// a leniency divergence; unlabeled sources have no real-mermaid reference.)
const cases = [
  { id: "cardinality-types", source: "erDiagram\nA ||--|| B : r1\nC |o--o| D : r2\nE }|--|{ F : r3\nG }o--o{ H : r4" },
  {
    id: "entity-attributes-keys",
    source:
      'erDiagram\nCUSTOMER {\n  string name PK "primary name key"\n  int age\n  bigint account_id FK\n  string email UK "unique email"\n}',
  },
  { id: "relationship-with-label", source: "erDiagram\nCUSTOMER ||--o{ ORDER : places" },
  { id: "entity-alias", source: 'erDiagram\nENTITY "Display Name"\nUSER "Application User" {\n  string id PK\n}' },
  { id: "acc-title", source: "erDiagram\naccTitle: ER Model\nA ||--|| B : rel" },
  { id: "non-identifying", source: "erDiagram\nCUSTOMER }|..|{ DELIVERY-ADDRESS : uses" },
  { id: "single-entity", source: "erDiagram\nACCOUNT" },
];

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const results = await page.evaluate(
    async ({ cases, mermaidModule }) => {
      const number = (v) => Math.round(v * 1000) / 1000;
      const { default: mermaid } = await import(mermaidModule);
      const cardName = {
        onlyOne: "ExactlyOne",
        zeroOrOne: "ZeroOrOne",
        oneOrMore: "OneOrMore",
        zeroOrMore: "ZeroOrMore",
      };
      // Extract the crow's-foot marker kind from a url(#...) value.
      const erMarker = (value) => {
        if (!value) return null;
        const m = value.match(/(onlyOne|zeroOrOne|oneOrMore|zeroOrMore)/);
        return m ? m[1] : null;
      };
      const elementMatrix = (element, rootInverse) =>
        rootInverse.multiply(element.getScreenCTM());
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
      const relativePath = (pathData, originX, originY, matrix) => {
        let coordinate = 0;
        let pendingX = 0;
        return pathData.replace(/-?\d+(?:\.\d+)?(?:e[-+]?\d+)?/gi, (token) => {
          if (coordinate++ % 2 === 0) {
            pendingX = Number(token);
            return `__x${pendingX}__`;
          }
          const point = new DOMPoint(pendingX, Number(token)).matrixTransform(matrix);
          return `${String(number(point.x - originX))},${String(number(point.y - originY))}`;
        }).replace(/__x-?\d+(?:\.\d+)?(?:e[-+]?\d+)?__,/gi, "");
      };
      // entity domId is `<svgId>-entity-<name>-<index>` where <name> may contain
      // dashes; <index> is the global source-order counter, so index 0 is the
      // first source entity (matches Muffin's first-vertex origin).
      const parseEntityId = (domId) => {
        const tail = domId.includes("-entity-") ? domId.split("-entity-")[1] : domId;
        const m = tail.match(/^(.*)-(\d+)$/);
        return m ? { id: m[1], index: Number(m[2]) } : { id: tail, index: -1 };
      };

      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        // fontFamily matches Muffin's default theme stack (first family
        // "trebuchet ms"); htmlLabels:false forces deterministic SVG <text>.
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily: '"trebuchet ms", verdana, arial, sans-serif',
          flowchart: { htmlLabels: false },
        });
        const svgId = `er-geometry-${index}`;
        const { svg } = await mermaid.render(svgId, fixture.source);
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");
        const rootInverse = root.getScreenCTM()?.inverse();

        const nodeElements = [...root.querySelectorAll("g.node")];
        const raw = nodeElements.map((node) => {
          const { id, index: entityIndex } = parseEntityId(node.id);
          const centre = absoluteOrigin(node, rootInverse);
          // Attribute-bearing entities use <path class="outer-path">; the
          // attribute-less drawRect branch emits a standard .label-container.
          const outline = node.querySelector(".outer-path") ?? node.querySelector(".label-container");
          const box = absoluteBox(outline, rootInverse);
          return { id, entityIndex, centre, box };
        });
        // Origin = first source-order entity (index 0), matching Muffin.
        const originEntity = raw.find((r) => r.entityIndex === 0) ?? raw[0];
        const originX = originEntity ? originEntity.centre.x : 0;
        const originY = originEntity ? originEntity.centre.y : 0;

        const entities = raw
          .sort((a, b) => a.entityIndex - b.entityIndex)
          .map((r) => ({
            id: r.id,
            bounds: r.box
              ? {
                  x: number(r.box.x - originX),
                  y: number(r.box.y - originY),
                  width: number(r.box.width),
                  height: number(r.box.height),
                }
              : null,
          }));

        const relationships = [...root.querySelectorAll(".edgePaths path")].map((edge) => {
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
            path: relativePath(edge.getAttribute("d"), originX, originY, elementMatrix(edge, rootInverse)),
            // The marker drawn at an entity end shows THAT entity's cardinality:
            // marker-start sits at entityA (path start) => cardA; marker-end at
            // entityB => cardB. (Empirically verified against CUSTOMER ||--o{
            // ORDER, where CUSTOMER=ExactlyOne, ORDER=ZeroOrMore.)
            cardA: markerStart ? cardName[markerStart] : null,
            cardB: markerEnd ? cardName[markerEnd] : null,
            identifying,
            strokeDasharray: computed.strokeDasharray,
            strokeWidth: computed.strokeWidth,
          };
        });

        out.push({ id: fixture.id, source: fixture.source, expected: { entities, relationships } });
      }
      return out;
    },
    { cases, mermaidModule },
  );

  const root = {
    upstream: {
      package: "mermaid",
      version: "11.16.0",
      notes:
        "Real mermaid 11.16.0 erDiagram geometry captured via headless Chrome. " +
        "Entity bounds + relationship paths are normalized to the first source-order " +
        "entity's centre (index 0), matching Muffin's FlowchartLayout origin subtraction. " +
        "cardA/cardB recovered from crow's-foot markers (marker-end=cardA, marker-start=cardB). " +
        "Compare against er::ErScene::toJsonObject with entity matching by id.",
    },
    mermaidVersion: "11.16.0",
    fontMode: "trebuchet-ms",
    oracle: "erDiagram.render entity bounds + relationship paths (first-entity normalized)",
    cases: results,
  };
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  console.log(`Wrote ${output} (${results.length} cases)`);
} finally {
  await browser.close();
}
