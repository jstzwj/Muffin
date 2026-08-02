import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Captures real mermaid 11.16.0 requirementDiagram geometry (requirementBox
// node bounds + body-row count + divider presence + relationship edge markers
// + relationship type) via headless Chrome, normalized to the first source-order
// node's centre (mirroring Muffin's origin subtraction). Output schema matches
// the fields of requirement::RequirementScene::toJsonObject that are comparable
// against mermaid.
//
// Edge marker kinds are extracted from the marker-start/marker-end url; the
// Start/End suffix is DROPPED (field position encodes the side), so the
// fixture's markerStart/markerEnd read as bare types ("requirement_contains",
// "requirement_arrow") exactly like Muffin's RequirementScene JSON.
//
// The relationship TYPE is extracted from the edge label text ("<<contains>>"
// → "contains") so the oracle can assert the full 7-type multiset (the 6 dashed
// types share the same marker; only the label distinguishes them).
//
// htmlLabels:false is set globally so both node rows and edge labels render as
// deterministic SVG <text> (matching Muffin's htmlLabels:false measurement
// path: row height = ink height + 6).
//
//   node scripts/generate_mermaid_requirement_geometry_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Env override: MERMAID_REFERENCE_ROOT (takes precedence over the default).
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/requirement-geometry.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const mermaidRoot = path.resolve(
  process.env.MERMAID_REFERENCE_ROOT ??
    process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ??
    path.join("tests", "fixtures", "mermaid", "requirement-geometry.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

// Corpus exercising every requirement type, element, all 7 relationship types,
// multi-field requirements, and a direction variant.
const cases = [
  {
    id: "basic-requirement",
    source:
      "requirementDiagram\n" +
      "requirement TestReq {\n" +
      "  id: 1\n" +
      "  text: the test text\n" +
      "  risk: high\n" +
      "  verifyMethod: inspection\n" +
      "}",
  },
  {
    id: "all-types",
    source:
      "requirementDiagram\n" +
      "requirement R1 {\n  id: 1\n}\n" +
      "functionalRequirement R2 {\n  id: 2\n}\n" +
      "interfaceRequirement R3 {\n  id: 3\n}\n" +
      "performanceRequirement R4 {\n  id: 4\n}\n" +
      "physicalRequirement R5 {\n  id: 5\n}\n" +
      "designConstraint R6 {\n  id: 6\n}",
  },
  {
    id: "element",
    source:
      "requirementDiagram\n" +
      "element TestElement {\n" +
      "  type: Model\n" +
      "  docref: spec001\n" +
      "}",
  },
  {
    id: "contains",
    source:
      "requirementDiagram\n" +
      "requirement Parent {\n  id: 1\n}\n" +
      "requirement Child {\n  id: 2\n}\n" +
      "Parent -contains-> Child",
  },
  {
    id: "dashed-relations",
    source:
      "requirementDiagram\n" +
      "requirement A {\n  id: 1\n}\n" +
      "requirement B {\n  id: 2\n}\n" +
      "requirement C {\n  id: 3\n}\n" +
      "requirement D {\n  id: 4\n}\n" +
      "requirement E {\n  id: 5\n}\n" +
      "requirement F {\n  id: 6\n}\n" +
      "requirement G {\n  id: 7\n}\n" +
      "A -copies-> B\n" +
      "B -derives-> C\n" +
      "C -satisfies-> D\n" +
      "D -verifies-> E\n" +
      "E -refines-> F\n" +
      "F -traces-> G",
  },
  {
    id: "mixed",
    source:
      "requirementDiagram\n" +
      "requirement Req1 {\n" +
      "  id: REQ001\n" +
      "  text: The system shall do X\n" +
      "  risk: medium\n" +
      "  verifyMethod: test\n" +
      "}\n" +
      "functionalRequirement Req2 {\n" +
      "  id: REQ002\n" +
      "  text: The system shall do Y\n" +
      "  risk: low\n" +
      "  verifyMethod: analysis\n" +
      "}\n" +
      "element Mod {\n" +
      "  type: Module\n" +
      "  docref: DOC1\n" +
      "}\n" +
      "Req1 -contains-> Req2\n" +
      "Mod -satisfies-> Req1\n" +
      "Req2 <-verifies- Req1",
  },
  {
    id: "direction-lr",
    source:
      "requirementDiagram\n" +
      "direction LR\n" +
      "requirement Left {\n  id: 1\n}\n" +
      "requirement Right {\n  id: 2\n}\n" +
      "Left -contains-> Right",
  },
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
      // Extract the bare marker type from url(#id_type-requirement_XStart/End);
      // drop the Start/End suffix (field position encodes the side).
      const markerType = (value) => {
        if (!value) return null;
        const m = value.match(/(requirement_contains|requirement_arrow)(Start|End)/);
        return m ? m[1] : null;
      };
      // Extract the relationship type from the edge label "<<type>>".
      const relationshipType = (text) => {
        if (!text) return null;
        const m = text.match(/^<<(.+)>>$/);
        return m ? m[1] : text.trim();
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

      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        // htmlLabels:false forces deterministic SVG <text> for both node rows
        // and edge labels (matching Muffin's measurement path).
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily: '"trebuchet ms", verdana, arial, sans-serif',
          htmlLabels: false,
        });
        const svgId = `req-geometry-${index}`;
        const { svg } = await mermaid.render(svgId, fixture.source);
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");
        const rootInverse = root.getScreenCTM()?.inverse();
        // Strip the SVG id prefix to recover the bare node name (mermaid emits
        // `<svgId>-<name>` as the g.node id).
        const svgIdPrefix = svgId + "-";

        const nodeElements = [...root.querySelectorAll("g.node")];
        const nodes = nodeElements
          .map((node) => {
            const rawId = node.getAttribute("id") ?? node.getAttribute("data-id") ?? "";
            const id = rawId.startsWith(svgIdPrefix) ? rawId.slice(svgIdPrefix.length) : rawId;
            // Outline = the outer-path rect (same selector pattern as class/ER).
            const outline =
              node.querySelector(".outer-path") ?? node.querySelector(".basic") ??
              node.querySelector(".label-container");
            const box = absoluteBox(outline, rootInverse);
            // Body row count = total .label groups inside the node minus 2
            // (type line + name). Only non-empty fields create a .label group
            // (mermaid addText3 returns 0 without creating one for empty text).
            const labels = node.querySelectorAll(".label");
            const bodyRows = Math.max(0, labels.length - 2);
            // Divider presence (g.divider child).
            const dividers = node.querySelectorAll(".divider").length;
            // Node type from the first label's text content ("<<Type>>").
            let type = null;
            if (labels.length > 0) {
              const firstText = labels[0].textContent?.trim() ?? "";
              const m = firstText.match(/^<<(.+)>>$/);
              type = m ? m[1] : firstText;
            }
            // Divider Y relative to the node center (mermaid draws it at the body
            // top: boxTop + typeHeight + nameHeight + gap). Captured so the oracle
            // can assert the exact divider position, not just its presence.
            const nodeCenterY = box ? box.y + box.height / 2 : null;
            let dividerY = null;
            const dividerEl = node.querySelector(".divider");
            if (dividerEl && nodeCenterY != null) {
              const dbox = absoluteBox(dividerEl, rootInverse);
              if (dbox) dividerY = number(dbox.y + dbox.height / 2 - nodeCenterY);
            }
            return {
              id,
              type,
              bodyRows,
              dividers,
              width: box ? number(box.width) : null,
              height: box ? number(box.height) : null,
              dividerY,
            };
          });

        // Edges: pattern + marker kinds + relationship type (from the label).
        const edgePaths = [...root.querySelectorAll(".edgePaths path")];
        const edgeLabels = [...root.querySelectorAll(".edgeLabels .edgeLabel")];
        const edges = edgePaths.map((edge, i) => {
          const klass = edge.getAttribute("class") ?? "";
          const pattern = klass.includes("edge-pattern-dashed") ? "dashed" : "solid";
          // The corresponding edge label (same source order).
          let label = null;
          if (i < edgeLabels.length) {
            const text = edgeLabels[i].textContent?.trim() ?? "";
            label = text;
          }
          return {
            pattern,
            markerStart: markerType(edge.getAttribute("marker-start")),
            markerEnd: markerType(edge.getAttribute("marker-end")),
            type: relationshipType(label),
            label,
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
        "Real mermaid 11.16.0 requirementDiagram geometry captured via headless Chrome " +
        "with htmlLabels:false (deterministic SVG <text>). Per requirementBox node: body-row " +
        "count (.label groups minus 2) + divider presence + width/height (origin-independent, " +
        "font-coupled). Per relationship edge: line pattern + start/end marker kinds + " +
        "relationship type (extracted from the <<type>> label). Marker kinds are bare types " +
        "(Start/End suffix dropped). Positions are not captured (doubly font-coupled via " +
        "dagre). Compare against requirement::RequirementScene::toJsonObject.",
    },
    mermaidVersion: "11.16.0",
    fontMode: "trebuchet-ms",
    oracle:
      "requirementDiagram.render requirementBox nodes (bounds+bodyRows+dividers) + " +
      "relationship edges (pattern+markers+type)",
    cases: results,
  };
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  console.log(`Wrote ${output} (${results.length} cases)`);
} finally {
  await browser.close();
}
