import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Captures real mermaid 11.16.0 classDiagram geometry (class node bounds +
// compartment divider count + relationship edge markers) via headless Chrome,
// normalized to the first source-order class's centre (mirroring Muffin's
// origin subtraction). Output schema matches the fields of
// classdiagram::ClassScene::toJsonObject that are comparable against mermaid.
//
// Edge marker kinds are extracted from the marker-start/marker-end url and the
// Start/End suffix is DROPPED (the field position encodes the side), so the
// fixture's markerStart/markerEnd read as bare types ("extension", "composition",
// ...) exactly like Muffin's ClassScene JSON (arrowTypeStart/End via markerName).
//
//   node scripts/generate_mermaid_class_geometry_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Env override: MERMAID_REFERENCE_ROOT (takes precedence over the default).
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/class-geometry.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const mermaidRoot = path.resolve(
  process.env.MERMAID_REFERENCE_ROOT ??
    process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "class-geometry.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

// Corpus exercising each relationship marker + line pattern + compartments.
// mermaid class relation syntax: left-marker? + line(--|..) + right-marker?.
const cases = [
  { id: "inheritance", source: "classDiagram\nAnimal <|-- Duck" },
  { id: "realization", source: "classDiagram\nShape <|.. Shape2" },
  { id: "composition", source: "classDiagram\nCar *-- Wheel" },
  { id: "aggregation", source: "classDiagram\nCompany o-- Employee" },
  { id: "association", source: "classDiagram\nA --> B" },
  { id: "dependency-dashed", source: "classDiagram\nC ..> D" },
  { id: "solid-link", source: "classDiagram\nE -- F" },
  { id: "composition-both-ends", source: "classDiagram\nP *--* Q" },
  { id: "aggregation-both-ends", source: "classDiagram\nR o--o S" },
  { id: "labeled-edge", source: "classDiagram\nOwner --> Pet : owns" },
  {
    id: "compartments",
    source:
      "classDiagram\nclass Duck {\n  +String beakColor\n  +swim()\n  +quack()\n}",
  },
  { id: "bare-classes", source: "classDiagram\nclass X\nclass Y" },
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
      // Extract bare marker type from url(#.._class-extensionStart); drop the
      // Start/End suffix (field position encodes the side).
      const markerType = (value) => {
        if (!value) return null;
        const m = value.match(/(aggregation|composition|extension|dependency|lollipop)(Start|End)/);
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
      // class node domId is `<svgId>-classId-<name>-<index>`.
      const parseClassId = (domId) => {
        const tail = domId.includes("-classId-") ? domId.split("-classId-")[1] : domId;
        const m = tail.match(/^(.*)-\d+$/);
        return m ? m[1] : tail;
      };

      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        // fontFamily matches Muffin's default theme stack; htmlLabels:false
        // forces deterministic SVG <text>.
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily: '"trebuchet ms", verdana, arial, sans-serif',
          flowchart: { htmlLabels: false },
        });
        const svgId = `class-geometry-${index}`;
        const { svg } = await mermaid.render(svgId, fixture.source);
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");
        const rootInverse = root.getScreenCTM()?.inverse();

        const nodeElements = [...root.querySelectorAll("g.node")];
        const nodes = nodeElements
          .map((node) => {
            const id = parseClassId(node.id);
            // Class box outline = the outer-path group (same selector as ER).
            const outline = node.querySelector(".outer-path") ?? node.querySelector(".label-container");
            const box = absoluteBox(outline, rootInverse);
            // Compartment dividers = g.divider children (header|members|methods).
            const dividers = node.querySelectorAll("g.divider").length;
            // width/height are origin-independent (font-coupled diagnostic);
            // positions (cx/cy) are dropped — doubly font-coupled via dagre.
            return {
              id,
              dividers,
              width: box ? number(box.width) : null,
              height: box ? number(box.height) : null,
            };
          });

        const edges = [...root.querySelectorAll(".edgePaths path")].map((edge) => {
          const klass = edge.getAttribute("class") ?? "";
          const pattern = klass.includes("edge-pattern-dashed") ? "dashed" : "solid";
          return {
            pattern,
            // markerStart/markerEnd are bare types (null when absent) so they
            // line up with Muffin's ClassScene markerStart/markerEnd fields.
            markerStart: markerType(edge.getAttribute("marker-start")),
            markerEnd: markerType(edge.getAttribute("marker-end")),
          };
        });

        out.push({ id: fixture.id, source: fixture.source, expected: { nodes, edges } });
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
        "Real mermaid 11.16.0 classDiagram geometry captured via headless Chrome. " +
        "Per class node: compartment divider count + width/height (origin-independent, " +
        "font-coupled diagnostic). Per relationship edge: line pattern + start/end " +
        "marker kinds. Marker kinds are bare types (Start/End suffix dropped; field " +
        "position encodes the side) to line up with ClassScene's markerStart/markerEnd. " +
        "Positions are not captured (doubly font-coupled via dagre). Compare against " +
        "classdiagram::ClassScene::toJsonObject.",
    },
    mermaidVersion: "11.16.0",
    fontMode: "trebuchet-ms",
    oracle: "classDiagram.render class nodes (bounds+dividers) + relationship edges (pattern+markers)",
    cases: results,
  };
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  console.log(`Wrote ${output} (${results.length} cases)`);
} finally {
  await browser.close();
}
