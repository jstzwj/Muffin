import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const VERSION = "11.16.0";
const MODULE_SHA = "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const FLOW_MODULE = ["flowDiagram-QQNUYIFB.mjs", "6a211d64c6dff33eda21df86fd91cce5cc50691319270878397d1d1f58885848"];
const CHROME_PRODUCT = "Chrome/151.0.7922.76";
const CHROME_SHA = "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const NOTO_SHA = "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";

const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const fixtureDir = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid"));
const chrome = path.resolve(process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe");
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const flowModuleFile = path.join(mermaidRoot, "dist", "chunks", "mermaid.esm", FLOW_MODULE[0]);
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const pixelDir = path.join(fixtureDir, "flowchart-elk-pixel");
const sha = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assert = (condition, message) => { if (!condition) throw new Error(message); };
const writeJson = (name, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha(JSON.stringify(payload));
  fs.writeFileSync(path.join(fixtureDir, name), `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assert(pkg.version === VERSION, `Mermaid ${pkg.version}`);
assert(sha(fs.readFileSync(moduleFile)) === MODULE_SHA, "Mermaid module drifted");
assert(sha(fs.readFileSync(flowModuleFile)) === FLOW_MODULE[1], "flowchart module drifted");
assert(sha(fs.readFileSync(chrome)) === CHROME_SHA, "Chrome drifted");
assert(sha(fs.readFileSync(fontFile)) === NOTO_SHA, "Noto drifted");

const directive = (config, source) => `%%{init: ${JSON.stringify(config)}}%%\n${source}`;
const base = {
  fontFamily: "Noto Sans",
  themeVariables: { fontFamily: "Noto Sans", fontSize: "16px" },
  flowchart: { htmlLabels: false },
};
const withConfig = (config, source) => directive({
  ...base,
  ...config,
  themeVariables: { ...base.themeVariables, ...(config.themeVariables ?? {}) },
  flowchart: { ...base.flowchart, ...(config.flowchart ?? {}) },
}, source);

const cases = [
  {
    id: "explicit-lr",
    expectedDetected: "flowchart-elk", expectedRole: "flowchart-elk",
    source: withConfig({}, "flowchart-elk LR\nA[Alpha] --> B{Choice}\nB -->|yes| C((Done))\nB -.->|no| D[Retry]"),
    dagreSource: withConfig({}, "flowchart LR\nA[Alpha] --> B{Choice}\nB -->|yes| C((Done))\nB -.->|no| D[Retry]"),
    pixel: true,
  },
  {
    id: "explicit-subgraph",
    expectedDetected: "flowchart-elk", expectedRole: "flowchart-elk",
    source: withConfig({}, "flowchart-elk TB\nsubgraph S[Services]\nA[API] --> B[(Store)]\nend\nC[Client] --> A"),
    dagreSource: withConfig({}, "flowchart TB\nsubgraph S[Services]\nA[API] --> B[(Store)]\nend\nC[Client] --> A"),
    pixel: true,
  },
  {
    id: "default-renderer-flowchart",
    expectedDetected: "flowchart-v2", expectedRole: "flowchart-elk",
    source: withConfig({ flowchart: { defaultRenderer: "elk" } }, "flowchart LR\nA --> B --> C"),
    dagreSource: withConfig({ flowchart: { defaultRenderer: "dagre-wrapper" } }, "flowchart LR\nA --> B --> C"),
  },
  {
    id: "default-renderer-graph",
    expectedDetected: "flowchart", expectedDagreDetected: "flowchart",
    expectedRole: "flowchart-elk",
    source: withConfig({ flowchart: { defaultRenderer: "elk" } }, "graph TB\nA --> B --> C"),
    dagreSource: withConfig({ flowchart: { defaultRenderer: "dagre-wrapper" } }, "graph TB\nA --> B --> C"),
  },
  {
    id: "top-layout-elk",
    expectedDetected: "flowchart-v2", expectedRole: "flowchart-v2",
    source: withConfig({ layout: "elk" }, "flowchart LR\nA --> B --> C"),
    dagreSource: withConfig({ layout: "dagre" }, "flowchart LR\nA --> B --> C"),
  },
  {
    id: "elk-options-inert",
    expectedDetected: "flowchart-elk", expectedRole: "flowchart-elk",
    source: withConfig({ elk: { mergeEdges: true, nodePlacementStrategy: "SIMPLE", cycleBreakingStrategy: "DEPTH_FIRST", forceNodeModelOrder: true, considerModelOrder: "NODES_AND_EDGES" } }, "flowchart-elk LR\nA --> B\nA --> C\nB --> D\nC --> D"),
    dagreSource: withConfig({}, "flowchart LR\nA --> B\nA --> C\nB --> D\nC --> D"),
  },
  {
    id: "dark-theme",
    expectedDetected: "flowchart-elk", expectedRole: "flowchart-elk",
    source: withConfig({ theme: "dark" }, "flowchart-elk TB\nA[Start] --> B[Finish]"),
    dagreSource: withConfig({ theme: "dark" }, "flowchart TB\nA[Start] --> B[Finish]"),
    pixel: true,
  },
];

const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true, args: ["--no-sandbox", "--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"] });
assert(await browser.version() === CHROME_PRODUCT, "Chrome product drifted");
const moduleUrl = pathToFileURL(moduleFile).href;
const fontUrl = pathToFileURL(fontFile).href;
const hostUrl = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;

const snapshot = async (source, id, pngFile = "") => {
  const page = await browser.newPage();
  await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
  await page.goto(hostUrl);
  const result = await page.evaluate(async ({ source, id, moduleUrl, fontUrl }) => {
    document.body.style.margin = "0";
    document.body.innerHTML = '<div id="container"></div>';
    const style = document.createElement("style");
    style.textContent = `@font-face{font-family:"Noto Sans";src:url("${fontUrl}");font-weight:400;font-style:normal}html,body{margin:0;padding:0}`;
    document.head.appendChild(style);
    await document.fonts.load('16px "Noto Sans"', "Alpha Choice Services 0123456789");
    await document.fonts.ready;
    const warnings = [];
    const originalWarn = console.warn;
    console.warn = (...values) => warnings.push(values.map(String).join(" "));
    const mermaid = (await import(moduleUrl)).default;
    mermaid.initialize({ startOnLoad: false, logLevel: "warn" });
    let svg = "";
    try { svg = (await mermaid.render(id, source)).svg; }
    finally { console.warn = originalWarn; }
    const detected = await mermaid.detectType(source);
    document.getElementById("container").innerHTML = svg;
    const root = document.querySelector("svg");
    const box = root.getBoundingClientRect();
    const round = (value) => Number(value.toFixed(6));
    const bbox = (node) => { const b = node.getBBox(); return [round(b.x), round(b.y), round(b.width), round(b.height)]; };
    const matrix = (node) => {
      const m = root.getScreenCTM().inverse().multiply(node.getScreenCTM());
      return [round(m.a), round(m.b), round(m.c), round(m.d), round(m.e), round(m.f)];
    };
    const nodes = [...root.querySelectorAll("g.node")].map((node) => ({
      id: node.id.replace(new RegExp(`^flowchart-`), "flowchart-"),
      classes: node.getAttribute("class"), matrix: matrix(node), bbox: bbox(node),
      text: node.textContent.trim(),
    }));
    const clusters = [...root.querySelectorAll("g.cluster")].map((node) => ({ classes: node.getAttribute("class"), matrix: matrix(node), bbox: bbox(node), text: node.textContent.trim() }));
    const edges = [...root.querySelectorAll(".edgePaths > path, .edgePath > path")].map((node) => ({
      classes: node.getAttribute("class"), d: node.getAttribute("d"),
      stroke: getComputedStyle(node).stroke, strokeWidth: getComputedStyle(node).strokeWidth,
      markerStart: Boolean(node.getAttribute("marker-start")), markerEnd: Boolean(node.getAttribute("marker-end")),
    }));
    const labels = [...root.querySelectorAll(".edgeLabel")].map((node) => ({ matrix: matrix(node), bbox: bbox(node), text: node.textContent.trim() }));
    return {
      detected,
      warnings,
      root: {
        viewBox: root.getAttribute("viewBox"), width: root.getAttribute("width"), height: root.getAttribute("height"),
        style: root.getAttribute("style"), roleDescription: root.getAttribute("aria-roledescription"),
        client: [round(box.width), round(box.height)],
      },
      nodes, clusters, edges, labels,
    };
  }, { source, id, moduleUrl, fontUrl });
  if (pngFile) {
    const svg = await page.$("svg");
    await svg.screenshot({ path: pngFile, omitBackground: true });
  }
  await page.close();
  return result;
};

const normalize = (snapshot) => ({
  root: { ...snapshot.root, roleDescription: undefined },
  nodes: snapshot.nodes.map(({ id: _id, ...node }) => node),
  clusters: snapshot.clusters,
  edges: snapshot.edges,
  labels: snapshot.labels,
});
const fallbackWarning = (warnings) => {
  const marker = "flowchart-elk was moved to an external package";
  const warning = warnings.find((message) => message.includes(marker)) ?? "";
  const start = warning.indexOf(marker);
  return start >= 0 ? warning.slice(start) : "";
};

fs.mkdirSync(pixelDir, { recursive: true });
const results = [];
for (const item of cases) {
  const pngFile = item.pixel ? path.join(pixelDir, `${item.id}.png`) : "";
  const elk = await snapshot(item.source, `elk-${item.id}`, pngFile);
  const dagre = await snapshot(item.dagreSource, `dagre-${item.id}`);
  assert(elk.detected === item.expectedDetected, `${item.id}: detector ${elk.detected}`);
  assert(elk.root.roleDescription === item.expectedRole, `${item.id}: rendered type ${elk.root.roleDescription}`);
  assert(dagre.detected === (item.expectedDagreDetected ?? "flowchart-v2"), `${item.id}: Dagre detector ${dagre.detected}`);
  const warning = fallbackWarning(elk.warnings);
  assert(warning.startsWith("flowchart-elk was moved to an external package"), `${item.id}: missing fallback warning`);
  assert(JSON.stringify(normalize(elk)) === JSON.stringify(normalize(dagre)), `${item.id}: ELK fallback differs from Dagre`);
  results.push({
    id: item.id, source: item.source, dagreSource: item.dagreSource,
    detected: elk.detected, dagreDetected: dagre.detected,
    fallbackWarning: warning,
    root: elk.root, nodes: elk.nodes, clusters: elk.clusters, edges: elk.edges, labels: elk.labels,
    pixel: item.pixel ? { file: `${item.id}.png`, sha256: sha(fs.readFileSync(pngFile)) } : undefined,
  });
}
await browser.close();

const provenance = {
  version: VERSION,
  moduleSha256: MODULE_SHA,
  flowModule: { name: FLOW_MODULE[0], sha256: FLOW_MODULE[1] },
  chrome: CHROME_PRODUCT,
  chromeSha256: CHROME_SHA,
  fontSha256: NOTO_SHA,
  registeredLayouts: ["dagre", "swimlane", "cose-bilkent"],
  elkPackageRegistered: false,
};
writeJson("flowchart-elk.json", { provenance, cases: results });
console.log(`Flowchart ELK fallback fixtures: ${results.length} cases`);
