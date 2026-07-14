import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Dumps the resolved per-node and per-edge STYLES for a style-matrix flowchart
// (classDef + class + inline style + linkStyle) under the default theme. The
// native FlowStyleResolve port (milestone F1) must reproduce the merged values.
//
// mermaid merges classDef (cssCompiledStyles) + inline `style` (cssStyles) +
// labelStyle in `compileStyles` (chunk-BNCO5QFQ.mjs:22) with last-wins, then
// `styles2String` splits into label/node/border/background with `!important`.
// The merged result is observable two ways:
//   - the element's inline `style` attribute (if the renderer sets nodeStyles
//     there) — exact string, but may omit classDef-only fields;
//   - getComputedStyle — the fully-merged value, but normalises format
//     (#0f0 → rgb(0,255,0), hsl → rgb). We dump BOTH: the style attr string for
//     format fidelity, and computed values for the merged-field check.

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "flowchart-style-cascade.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"),
  )
);

// Style matrix exercising every cascade layer:
//   A  — no styles (theme defaults only)
//   B  — classDef only (:::clsB)
//   C  — inline `style` only (fill/stroke/width/color)
//   D  — classDef + inline (class wins fill via :::clsA, inline overrides fill)
//   E  — classDef with text styles (font-weight/color via textStyles)
// Edges:
//   L0 — no linkStyle (theme lineColor)
//   L1 — linkStyle stroke/width
//   L2 — linkStyle stroke/dasharray
const source = [
  "flowchart LR",
  "A[Plain] --> B[ClassOnly]:::clsB",
  "B -->|lbl1| C[Inline]",
  "C --> D[ClassAndInline]:::clsA",
  "C --> E[TextCls]:::clsE",
  "style C fill:#00ff00,stroke:#333333,stroke-width:2px,color:#ff0000",
  "style D fill:#0000ff",
  "linkStyle 1 stroke:#0000ff,stroke-width:3px",
  'linkStyle 2 stroke:#ff0000,stroke-dasharray:4 2,color:#008800',
  "classDef clsA fill:#ffff00,stroke:#ff00ff,color:#000000",
  "classDef clsB fill:#aaaaaa,color:#ffffff,font-weight:bold",
  "classDef clsE fill:#eeeeee,color:#110000,font-weight:bold,font-size:20px",
].join("\n");

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const result = await page.evaluate(
    async ({ source, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: "default",
        fontFamily: "Arial",
        flowchart: { defaultRenderer: "dagre-wrapper", htmlLabels: false },
      });
      const { svg } = await mermaid.render("flow-style-cascade", source);
      document.getElementById("container").innerHTML = svg;
      const root = document.querySelector("svg");
      const stripId = (el, prefix) => el?.id.replace(`${prefix}-flowchart-`, "").replace(/-\d+$/, "");
      const nodeEls = [...root.querySelectorAll("g.node")];
      const nodes = nodeEls.map((nodeEl) => {
        const id = stripId(nodeEl, "flow-style-cascade");
        const container = nodeEl.querySelector(".label-container");
        const label = nodeEl.querySelector(".label");
        const cs = getComputedStyle(container ?? nodeEl);
        const labelCs = getComputedStyle(label ?? nodeEl);
        return {
          id,
          containerStyleAttr: container?.getAttribute("style") ?? null,
          labelStyleAttr: label?.getAttribute("style") ?? null,
          fill: cs.fill,
          stroke: cs.stroke,
          strokeWidth: cs.strokeWidth,
          strokeDasharray: cs.strokeDasharray,
          color: labelCs.color,
          fontFamily: labelCs.fontFamily,
          fontSize: labelCs.fontSize,
          fontWeight: labelCs.fontWeight,
        };
      });
      const edgeEls = [...root.querySelectorAll(".edgePaths path")];
      const edgeLabelEls = [...root.querySelectorAll(".edgeLabels .edgeLabel")];
      const edges = edgeEls.map((edgeEl, i) => {
        const cs = getComputedStyle(edgeEl);
        const labelEl = edgeLabelEls[i];
        const labelCs = labelEl ? getComputedStyle(labelEl.querySelector(".label") ?? labelEl) : {};
        return {
          index: i,
          pathStyleAttr: edgeEl.getAttribute("style") ?? null,
          stroke: cs.stroke,
          strokeWidth: cs.strokeWidth,
          strokeDasharray: cs.strokeDasharray,
          markerEnd: edgeEl.getAttribute("marker-end") ?? null,
          markerStart: edgeEl.getAttribute("marker-start") ?? null,
          labelColor: labelCs.color ?? null,
          labelFontSize: labelCs.fontSize ?? null,
          labelBg: labelEl ? getComputedStyle(labelEl).backgroundColor : null,
        };
      });
      return { source, nodes, edges };
    },
    { source, mermaidModule },
  );
  const fixture = {
    upstream: { package: "mermaid", version: packageJson.version },
    ...result,
  };
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(`Wrote ${output} (${result.nodes.length} nodes, ${result.edges.length} edges)`);
} finally {
  await browser.close();
}
