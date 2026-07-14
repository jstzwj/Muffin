import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Dumps the flowchart SCENE (clusters, edges, nodes in mermaid draw order with
// resolved paint + geometry + markers + labels) from a rendered SVG, in the
// same JSON shape as FlowScene::toJson. The native buildFlowScene (milestone F2)
// must reproduce this scene. Colours are dumped as getComputedStyle rgb (the
// observable merged value); the native test compares via QColor equality.
//
// Coords are relative to the first node's centre (origin), matching the layout
// extraction. Numbers rounded to 0.001 (parity with FlowScene::toJson r3).

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "flowchart-scene.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

// Exercises: cluster, rect/circle/diamond nodes, -->/--x/--o/---/-.->/==> edges,
// edge labels, classDef + inline style + linkStyle.
const source = [
  "flowchart TB",
  "subgraph S[Group]",
  "A[Alpha] -->|go| B((Beta)):::clsB",
  "end",
  "B --x C{Decision}",
  "C --o D[Delta]",
  "D --- E[Plain]",
  "E -.-> F[Dotted]",
  "F ==> G[Thick]",
  "style A fill:#ff0000,stroke:#000000,stroke-width:2px",
  "classDef clsB fill:#aaaaaa,color:#ffffff",
  "linkStyle 4 stroke:#0000ff,stroke-width:3px",
].join("\n");

const r3 = (v) => Math.round(v * 1000) / 1000;

const browser = await puppeteer.launch({ headless: true, executablePath: chrome, args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const result = await page.evaluate(async ({ source, mermaidModule }) => {
    const { default: mermaid } = await import(mermaidModule);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict", theme: "default", fontFamily: "Arial",
      flowchart: { defaultRenderer: "dagre-wrapper", htmlLabels: false } });
    const { svg } = await mermaid.render("flow-scene", source);
    document.getElementById("container").innerHTML = svg;
    const root = document.querySelector("svg");
    const number = (v) => Math.round(v * 1000) / 1000;
    const shapeKind = (tag, hasRx) => {
      if (tag === "rect") return hasRx ? "roundedRect" : "rect";
      if (tag === "circle" || tag === "ellipse") return "ellipse";
      if (tag === "polygon") return "polygon";
      return tag;
    };
    const nodeEls = [...root.querySelectorAll("g.node")];
    const originX = nodeEls.length ? nodeEls[0].getBBox().x + nodeEls[0].getBBox().width / 2 : 0;
    const originY = nodeEls.length ? nodeEls[0].getBBox().y + nodeEls[0].getBBox().height / 2 : 0;
    const rel = (x, y) => [number(x - originX), number(y - originY)];

    const clusters = [...root.querySelectorAll("g.cluster")].map((cl) => {
      const b = cl.getBBox();
      const cs = getComputedStyle(cl.querySelector("rect"));
      const labelEl = cl.querySelector(".cluster-label");
      return {
        id: cl.id, cx: number(b.x + b.width / 2 - originX), cy: number(b.y + b.height / 2 - originY),
        width: number(b.width), height: number(b.height),
        fill: cs.fill, stroke: cs.stroke, strokeWidth: cs.strokeWidth,
        label: { text: (labelEl?.textContent || "").trim(), color: getComputedStyle(labelEl || cl).color },
      };
    });

    const edgeEls = [...root.querySelectorAll(".edgePaths path")];
    const edgeLabelEls = [...root.querySelectorAll(".edgeLabels .edgeLabel")];
    const edges = edgeEls.map((e, i) => {
      const cs = getComputedStyle(e);
      const me = e.getAttribute("marker-end"); const ms = e.getAttribute("marker-start");
      const lbl = edgeLabelEls[i]?.querySelector(".label");
      const lc = lbl ? getComputedStyle(lbl) : null;
      return {
        id: e.id, path: e.getAttribute("d") || "",
        stroke: cs.stroke, strokeWidth: cs.strokeWidth, strokeDasharray: cs.strokeDasharray === "none" ? "" : cs.strokeDasharray,
        markerEnd: me, markerStart: ms,
        label: { text: (lbl?.textContent || "").trim(), background: edgeLabelEls[i] ? getComputedStyle(edgeLabelEls[i]).backgroundColor : "" },
      };
    });

    const nodes = nodeEls.map((n) => {
      const b = n.getBBox();
      const cont = n.querySelector(".label-container");
      const labelEl = n.querySelector(".label");
      const cs = getComputedStyle(cont || n);
      const lcs = getComputedStyle(labelEl || n);
      const tag = cont?.tagName.toLowerCase() || "rect";
      const hasRx = cont?.hasAttribute("rx");
      return {
        id: n.id.replace(/.*-flowchart-/, "").replace(/-\d+$/, ""),
        shapeKind: shapeKind(tag, hasRx),
        cx: number(b.x + b.width / 2 - originX), cy: number(b.y + b.height / 2 - originY),
        width: number(b.width), height: number(b.height),
        fill: cs.fill, stroke: cs.stroke, strokeWidth: cs.strokeWidth,
        label: { text: (labelEl?.textContent || "").trim(), color: lcs.color, fontFamily: lcs.fontFamily, fontSize: lcs.fontSize, fontWeight: lcs.fontWeight },
      };
    });

    // bounds: union of nodes + clusters (relative)
    let minX = 0, minY = 0, maxX = 0, maxY = 0, first = true;
    for (const n of nodes) {
      const l = n.cx - n.width / 2, r = n.cx + n.width / 2, t = n.cy - n.height / 2, bm = n.cy + n.height / 2;
      if (first) { minX = l; maxX = r; minY = t; maxY = bm; first = false; } else { minX = Math.min(minX, l); maxX = Math.max(maxX, r); minY = Math.min(minY, t); maxY = Math.max(maxY, bm); }
    }
    return {
      background: mermaid.mermaidAPI.getConfig().themeVariables.background,
      bounds: { x: number(minX), y: number(minY), width: number(maxX - minX), height: number(maxY - minY) },
      clusters, edges, nodes,
    };
  }, { source, mermaidModule });
  const fixture = { upstream: { package: "mermaid", version: packageJson.version }, source, ...result };
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(`Wrote ${output} (${result.clusters.length} clusters, ${result.edges.length} edges, ${result.nodes.length} nodes)`);
} finally { await browser.close(); }
