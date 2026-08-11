import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_LAYOUT_SHA =
  "da16cc93e8359e7c7479e6e5ee4572f90eda7f76b73905a3e7354804ad476a35";
const EXPECTED_LAYOUT_MAP_SHA =
  "91f3efc6e897559e7923a432cad3fea28106f70d3305ddd091cc256dd15328e4";
const EXPECTED_DIAGRAM_SHA =
  "85a5cfcf634e50a645de2a7a6057b07a3bc3dbf7746b352ee514e9dfcbf00060";
const EXPECTED_DIAGRAM_MAP_SHA =
  "2700c58b9d34fe3b2b027b4371483117c29535bfb305570524340b0c306041e6";
const EXPECTED_CHROME = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA =
  "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const fixtureDir = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const chunkDir = path.join(mermaidRoot, "dist", "chunks", "mermaid.esm");
const layoutFile = path.join(chunkDir, "swimlanes-YAP2ZHFU.mjs");
const diagramFile = path.join(chunkDir, "swimlanesDiagram-7S2UDSDB.mjs");
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assertEqual = (actual, expected, name) => {
  if (actual !== expected) throw new Error(`${name}: ${actual} != ${expected}`);
};
const writeJson = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json")));
assertEqual(pkg.version, EXPECTED_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MODULE_SHA, "Mermaid module");
assertEqual(sha256(fs.readFileSync(layoutFile)), EXPECTED_LAYOUT_SHA, "Swimlane layout");
assertEqual(sha256(fs.readFileSync(`${layoutFile}.map`)), EXPECTED_LAYOUT_MAP_SHA, "Swimlane layout map");
assertEqual(sha256(fs.readFileSync(diagramFile)), EXPECTED_DIAGRAM_SHA, "Swimlane diagram");
assertEqual(sha256(fs.readFileSync(`${diagramFile}.map`)), EXPECTED_DIAGRAM_MAP_SHA, "Swimlane diagram map");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA, "Chrome");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA, "Noto Sans");

const init = (config, body) => `%%{init:${JSON.stringify(config)}}%%\n${body}`;
const canonical = `swimlane-beta TB
subgraph sales[Sales]
 a[Lead] --> b[Quote]
end
subgraph legal[Legal]
 c[Review] --> d[Approve]
end
b --> c`;
const directional = (direction) => canonical.replace("swimlane-beta TB", `swimlane-beta ${direction}`);
const crossing = `swimlane-beta TB
subgraph one[One]
 a1[A1] --> a2[A2]
end
subgraph two[Two]
 b1[B1] --> b2[B2]
end
a1 --> b2
b1 --> a2`;
const asymmetricLanes = `swimlane-beta TB
subgraph left[Left]
 l1[L1]
end
subgraph middle[Middle]
 m1[M1]
end
subgraph right[Right]
 r1[R1]
end
l1 --> r1`;
const hopStress = `swimlane-beta TB
subgraph one[One]
 a1[A1] --> a2[A2] --> a3[A3]
end
subgraph two[Two]
 b1[B1] --> b2[B2] --> b3[B3]
end
subgraph three[Three]
 c1[C1] --> c2[C2] --> c3[C3]
end
a1 --> c3
c1 --> a3
a2 --> c2
c2 --> b3
b1 --> a3`;

const grammarCases = [
  ["canonical", canonical],
  ["header-only", "swimlane-beta"],
  ["tb", "swimlane-beta TB\nA --> B"],
  ["lr", "swimlane-beta LR\nA --> B"],
  ["rl", "swimlane-beta RL\nA --> B"],
  ["bt", "swimlane-beta BT\nA --> B"],
  ["uppercase", "SWIMLANE-BETA\nA --> B"],
  ["missing-beta", "swimlane\nA --> B"],
  ["prefix", "swimlane-betaX\nA --> B"],
  ["leading-comment", "%% before\nswimlane-beta\nA --> B"],
  ["same-line", "swimlane-beta TB; A --> B"],
  ["subgraphs", canonical],
  ["loose", "swimlane-beta\nA --> B --> C"],
  ["shape", "swimlane-beta\nA{Choice} --> B([Done])"],
  ["edge-label", "swimlane-beta\nA -->|review| B"],
  ["class-style", "swimlane-beta\nclassDef hot fill:#f00\nA:::hot --> B"],
  ["malformed-edge", "swimlane-beta\nA -->"],
].map(([id, source]) => ({ id, source }));

const geometryCases = [
  ["canonical-tb", canonical, {}],
  ["canonical-lr", directional("LR"), {}],
  ["canonical-rl", directional("RL"), {}],
  ["canonical-bt", directional("BT"), {}],
  ["default-lane", "swimlane-beta\nA --> B --> C", {}],
  ["mixed-loose", `${canonical}\nx[Loose] --> d`, {}],
  ["nested", "swimlane-beta\nsubgraph outer[Outer]\n subgraph inner[Inner]\n  A --> B\n end\n C --> B\nend", {}],
  ["edge-label", canonical.replace("b --> c", "b -->|handoff| c"), {}],
  ["cycle", canonical.replace("b --> c", "b --> c\nd --> a"), {}],
  ["crossing", crossing, {}],
  ["html-false", canonical, { htmlLabels: false }],
  ["look-neo", canonical, { look: "neo" }],
  ["look-hand", canonical, { look: "handDrawn", handDrawnSeed: 7 }],
  ["layout-dagre", canonical, { layout: "dagre" }],
  ["spacing", canonical, { flowchart: { nodeSpacing: 80, rankSpacing: 90 } }],
].map(([id, body, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", ...config }, body),
}));

const configCases = [
  ["defaults", canonical, {}],
  ["flow-use-max-false", canonical, { flowchart: { useMaxWidth: false } }],
  ["swimlane-use-max-inert", canonical, { swimlane: { useMaxWidth: false } }],
  ["line-hops-arc", hopStress, {}],
  ["line-hops-false", hopStress, { swimlane: { lineHops: false } }],
  ["line-hops-gap", hopStress, { swimlane: { lineHops: "gap" } }],
  ["cross-lane-ranks", crossing, { swimlane: { ignoreCrossLaneEdges: false } }],
  ["rank-crossings-off", crossing, { swimlane: { optimizeRanksByCrossings: false } }],
  ["auto-lanes", crossing, { swimlane: { automaticLaneOrdering: true } }],
  ["auto-lanes-baseline", asymmetricLanes, {}],
  ["auto-lanes-asymmetric", asymmetricLanes, { swimlane: { automaticLaneOrdering: true } }],
  ["node-spacing", canonical, { flowchart: { nodeSpacing: 100 } }],
  ["rank-spacing", canonical, { flowchart: { rankSpacing: 100 } }],
  ["curve-linear", canonical, { flowchart: { curve: "linear" } }],
  ["diagram-padding", canonical, { flowchart: { diagramPadding: 30 } }],
  ["node-padding", canonical, { flowchart: { padding: 30 } }],
  ["layout-dagre", canonical, { layout: "dagre" }],
  ["theme-dark", canonical, { theme: "dark" }],
  ["theme-forest", canonical, { theme: "forest" }],
  ["theme-lane", canonical, { themeVariables: { clusterBkg: "#123456", clusterBorder: "#abcdef", titleColor: "#fedcba" } }],
].map(([id, body, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", ...config }, body),
}));

const pixelCases = [
  ["default", canonical, {}],
  ["dark", canonical, { theme: "dark" }],
  ["neo", canonical, { look: "neo" }],
  ["hand", canonical, { look: "handDrawn", handDrawnSeed: 7 }],
  ["crossing", crossing, {}],
].map(([id, body, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", ...config }, body),
}));

const { default: puppeteer } = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js"),
).href);
const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"],
});
const provenance = {
  package: "mermaid", version: EXPECTED_VERSION,
  moduleSha256: EXPECTED_MODULE_SHA,
  swimlaneLayoutSha256: EXPECTED_LAYOUT_SHA,
  swimlaneLayoutMapSha256: EXPECTED_LAYOUT_MAP_SHA,
  swimlaneDiagramSha256: EXPECTED_DIAGRAM_SHA,
  swimlaneDiagramMapSha256: EXPECTED_DIAGRAM_MAP_SHA,
  chromeProduct: EXPECTED_CHROME, chromeSha256: EXPECTED_CHROME_SHA,
  notoSansSha256: EXPECTED_NOTO_SHA, sourceEntry: true,
};

try {
  assertEqual(await browser.version(), EXPECTED_CHROME, "Chrome product");
  const page = await browser.newPage();
  await page.setViewport({ width: 1400, height: 1000, deviceScaleFactor: 1 });
  const host = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const moduleUrl = pathToFileURL(moduleFile).href;
  const prepare = async () => {
    await page.goto(host);
    await page.evaluate(async (fontUrl) => {
      document.body.style.margin = "0";
      const font = new FontFace("Noto Sans", `url(${fontUrl})`);
      await font.load(); document.fonts.add(font);
      await document.fonts.load("16px 'Noto Sans'");
    }, pathToFileURL(fontFile).href);
  };
  const snapshot = async (source, id) => page.evaluate(async ({ source, id, moduleUrl }) => {
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const rendered = await mermaid.render(`swimlane-${id}`, source);
    document.body.innerHTML = rendered.svg;
    const svg = document.querySelector("svg");
    const attrs = (element, names) => Object.fromEntries(
      names.map((name) => [name, element?.getAttribute(name) ?? null]),
    );
    const box = (element) => {
      if (!element) return null;
      const b = element.getBBox();
      return { x: b.x, y: b.y, width: b.width, height: b.height };
    };
    const computed = (element) => {
      const s = getComputedStyle(element);
      return { fill: s.fill, stroke: s.stroke, strokeWidth: s.strokeWidth,
        fontFamily: s.fontFamily, fontSize: s.fontSize, color: s.color, filter: s.filter };
    };
    const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
    return {
      db: {
        direction: diagram.db.getDirection?.() ?? null,
        vertices: JSON.parse(JSON.stringify([...diagram.db.getVertices().values()])),
        edges: JSON.parse(JSON.stringify(diagram.db.getEdges())),
        subgraphs: JSON.parse(JSON.stringify(diagram.db.getSubGraphs())),
      },
      root: {
        attrs: attrs(svg, ["id", "class", "viewBox", "width", "height", "style", "role", "aria-roledescription"]),
        bbox: box(svg), client: { width: svg.getBoundingClientRect().width, height: svg.getBoundingClientRect().height },
      },
      clusters: [...svg.querySelectorAll("g.cluster")].map((group) => ({
        id: group.id, class: group.getAttribute("class"), bbox: box(group),
        label: { text: group.querySelector(".cluster-label")?.textContent ?? "", transform: group.querySelector(".cluster-label")?.getAttribute("transform"), bbox: box(group.querySelector(".cluster-label")) },
        rects: [...group.querySelectorAll(":scope > rect")].map((rect) => ({ attrs: attrs(rect, ["class", "x", "y", "width", "height", "fill", "stroke"]), bbox: box(rect), computed: computed(rect) })),
      })),
      nodes: [...svg.querySelectorAll("g.nodes > g.node, g.nodes > g.rough-node")].map((group) => ({ id: group.id, class: group.getAttribute("class"), transform: group.getAttribute("transform"), bbox: box(group), text: group.textContent ?? "" })),
      edges: [...svg.querySelectorAll("path.flowchart-link")].map((edge) => ({ attrs: attrs(edge, ["id", "class", "d", "stroke", "stroke-width", "marker-start", "marker-end"]), bbox: box(edge), computed: computed(edge) })),
      edgeLabels: [...svg.querySelectorAll("g.edgeLabel")].map((label) => ({ transform: label.getAttribute("transform"), bbox: box(label), text: label.textContent ?? "" })),
      order: [...svg.querySelectorAll("g.root > g")].map((element) => element.getAttribute("class") ?? element.tagName),
    };
  }, { source, id, moduleUrl });

  const grammar = [];
  for (const item of grammarCases) {
    await prepare();
    const expected = await page.evaluate(async ({ source, moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
      try {
        const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
        await mermaid.render("swimlane-grammar", source);
        return { accepted: true, type: diagram.type, direction: diagram.db.getDirection?.() ?? null,
          vertices: diagram.db.getVertices?.().size ?? 0, edges: diagram.db.getEdges?.().length ?? 0,
          subgraphs: diagram.db.getSubGraphs?.().length ?? 0 };
      } catch (cause) {
        const hash = cause?.hash ?? {};
        return { accepted: false, error: { name: cause?.name ?? "Error", message: String(cause?.message ?? cause),
          kind: String(cause?.message ?? "").startsWith("Lexical error") ? "Lexer" : "Parser",
          line: Number(hash?.loc?.first_line ?? hash?.line ?? 0) + (hash?.loc ? 0 : 1),
          column: Number(hash?.loc?.first_column ?? 0) + 1 } };
      }
    }, { source: item.source, moduleUrl });
    grammar.push({ ...item, expected });
  }

  const collect = async (items) => {
    const values = [];
    for (const item of items) { await prepare(); values.push({ ...item, expected: await snapshot(item.source, item.id) }); }
    return values;
  };
  const geometry = await collect(geometryCases);
  const config = await collect(configCases);
  const pixelDir = path.join(fixtureDir, "swimlane-pixel");
  fs.mkdirSync(pixelDir, { recursive: true });
  const pixels = [];
  for (const item of pixelCases) {
    await prepare(); await snapshot(item.source, `pixel-${item.id}`);
    const svg = await page.$("svg");
    const file = path.join(pixelDir, `${item.id}.png`);
    await svg.screenshot({ path: file, omitBackground: true });
    const bounds = await svg.boundingBox();
    pixels.push({ id: item.id, source: item.source, file: `${item.id}.png`,
      width: Math.round(bounds.width), height: Math.round(bounds.height), sha256: sha256(fs.readFileSync(file)) });
  }

  const byId = (items, id) => items.find((item) => item.id === id).expected;
  if (byId(geometry, "canonical-tb").root.attrs.viewBox === byId(geometry, "canonical-lr").root.attrs.viewBox)
    throw new Error("TB/LR geometry unexpectedly identical");
  if (byId(config, "flow-use-max-false").root.attrs.width === "100%")
    throw new Error("flowchart.useMaxWidth did not affect Swimlane root sizing");
  if (byId(config, "swimlane-use-max-inert").root.attrs.width !== "100%")
    throw new Error("swimlane.useMaxWidth unexpectedly became live");
  const paths = (id) => byId(config, id).edges.map((edge) => edge.attrs.d).join("|");
  if (!paths("line-hops-arc").includes("A6,6") ||
      paths("line-hops-arc") === paths("line-hops-false") ||
      paths("line-hops-gap") === paths("line-hops-false") ||
      paths("line-hops-gap") === paths("line-hops-arc"))
    throw new Error("Swimlane line-hop oracle did not produce arc/disabled/gap variants");
  const nodeTransforms = (id) => byId(config, id).nodes.map((node) => node.transform).join("|");
  if (nodeTransforms("auto-lanes-baseline") === nodeTransforms("auto-lanes-asymmetric"))
    throw new Error("automaticLaneOrdering did not change an asymmetric lane arrangement");
  fs.mkdirSync(fixtureDir, { recursive: true });
  writeJson(path.join(fixtureDir, "swimlane-grammar.json"), { upstream: provenance, cases: grammar });
  writeJson(path.join(fixtureDir, "swimlane-geometry.json"), { upstream: provenance, cases: geometry });
  writeJson(path.join(fixtureDir, "swimlane-config.json"), { upstream: provenance, cases: config });
  writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixels });
  console.log(`Generated Swimlane fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixels.length} pixel.`);
} finally {
  await browser.close();
}
