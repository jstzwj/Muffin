import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const VERSION = "11.16.0";
const MODULE_SHA = "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const CHROME_PRODUCT = "Chrome/151.0.7922.76";
const CHROME_SHA = "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const NOTO_SHA = "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";

const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const outputFile = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "remaining-parity.json"));
const chrome = path.resolve(process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe");
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const sha = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assert = (condition, message) => { if (!condition) throw new Error(message); };

const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assert(pkg.version === VERSION, `Mermaid ${pkg.version}`);
assert(sha(fs.readFileSync(moduleFile)) === MODULE_SHA, "Mermaid module drifted");
assert(sha(fs.readFileSync(chrome)) === CHROME_SHA, "Chrome drifted");
assert(sha(fs.readFileSync(fontFile)) === NOTO_SHA, "Noto drifted");

const init = (config, source) => `%%{init: ${JSON.stringify(config)}}%%\n${source}`;
const frontmatter = (config, source) => `---\nconfig: ${JSON.stringify(config)}\n---\n${source}`;
const common = {
  fontFamily: "Noto Sans",
  themeVariables: { fontFamily: "Noto Sans", fontSize: "16px" },
};
const flow = "flowchart TB\nsubgraph S[Long cluster title]\nA[**Alpha Beta Gamma Delta**] --> B[Second node]\nend\nC[Outside] --> A";
const requirement = "requirementDiagram\nrequirement R {\n id: 1\n text: Long requirement text\n risk: high\n verifymethod: test\n}\nelement E {\n type: system\n docref: DOC1\n}\nR -contains-> E";
const classLook = "classDiagram\nnamespace Domain {\nclass A {\n+String value\n+run() bool\n}\n}\nclass B\nA --> B : uses";
const stateLook = "stateDiagram-v2\nstate Running {\n[*] --> Idle\nIdle --> Active : start\n}\nRunning --> Done : finish";
const markerFlow = "flowchart LR\nA --> B";
const markerSequence = "sequenceDiagram\nA<<->>B: both";
const markerClass = "classDiagram\nA <|-- B";
const markerState = "stateDiagram-v2\n[*] --> A\nA --> [*]";
const markerEr = "erDiagram\nA ||--o{ B : has";
const markerRequirement = "requirementDiagram\nrequirement R {\n id: 1\n text: R\n}\nelement E {\n type: system\n}\nR -contains-> E";
const markerBlock = "block-beta\nA --> B";
const markerSwimlane = "swimlane-beta LR\nA --> B";
const cases = [
  { id: "flow-html-global-true", source: init({ ...common, htmlLabels: true }, flow) },
  { id: "flow-html-global-false", source: init({ ...common, htmlLabels: false }, flow) },
  { id: "flow-html-alias-false", source: init({ ...common, flowchart: { htmlLabels: false } }, flow) },
  { id: "flow-wrap-80", source: init({ ...common, flowchart: { wrappingWidth: 80 } }, flow) },
  { id: "flow-subgraph-margin", source: init({ ...common, flowchart: { subGraphTitleMargin: { top: 31, bottom: 47 } } }, flow) },
  { id: "flow-inherit-dir-false", source: init({ ...common, flowchart: { inheritDir: false } }, "flowchart LR\nsubgraph S[Group]\nA --> B\nend\nC --> A") },
  { id: "flow-inherit-dir-true", source: init({ ...common, flowchart: { inheritDir: true } }, "flowchart LR\nsubgraph S[Group]\nA --> B\nend\nC --> A") },
  { id: "flow-inherit-isolated-false", source: init({ ...common, flowchart: { inheritDir: false } }, "flowchart LR\nsubgraph S[Group]\nA --> B\nend\nC[Outside]") },
  { id: "flow-inherit-isolated-true", source: init({ ...common, flowchart: { inheritDir: true } }, "flowchart LR\nsubgraph S[Group]\nA --> B\nend\nC[Outside]") },
  { id: "flow-renderer-dagre-d3", source: init({ ...common, flowchart: { defaultRenderer: "dagre-d3" } }, "flowchart LR\nA --> B") },
  { id: "flow-renderer-unknown", source: init({ ...common, flowchart: { defaultRenderer: "unknown" } }, "flowchart LR\nA --> B") },
  { id: "requirement-html-true", source: init({ ...common, htmlLabels: true }, requirement) },
  { id: "requirement-html-false", source: init({ ...common, htmlLabels: false }, requirement) },
  { id: "class-layout-elk", source: init({ ...common, layout: "elk" }, "classDiagram\nclass A\nclass B\nA --> B") },
  { id: "class-layout-unknown", source: init({ ...common, layout: "unknown" }, "classDiagram\nclass A\nclass B\nA --> B") },
  { id: "class-renderer-elk", source: init({ ...common, class: { defaultRenderer: "elk" } }, "classDiagram\nclass A\nclass B\nA --> B") },
  { id: "class-renderer-dagre-d3", source: init({ ...common, class: { defaultRenderer: "dagre-d3" } }, "classDiagram\nclass A\nclass B\nA --> B") },
  { id: "state-layout-elk", source: init({ ...common, layout: "elk" }, "stateDiagram-v2\nA --> B") },
  { id: "state-layout-unknown", source: init({ ...common, layout: "unknown" }, "stateDiagram-v2\nA --> B") },
  { id: "state-renderer-elk", source: init({ ...common, state: { defaultRenderer: "elk" } }, "stateDiagram-v2\nA --> B") },
  { id: "state-renderer-unknown", source: init({ ...common, state: { defaultRenderer: "unknown" } }, "stateDiagram-v2\nA --> B") },
  { id: "class-look-classic", source: init({ ...common, look: "classic", handDrawnSeed: 7 }, classLook) },
  { id: "class-look-neo", source: init({ ...common, look: "neo", handDrawnSeed: 7 }, classLook) },
  { id: "class-look-handdrawn-7", source: init({ ...common, look: "handDrawn", handDrawnSeed: 7 }, classLook) },
  { id: "class-look-handdrawn-9", source: init({ ...common, look: "handDrawn", handDrawnSeed: 9 }, classLook) },
  { id: "class-look-wrong-case", source: init({ ...common, look: "handdrawn", handDrawnSeed: 7 }, classLook) },
  { id: "state-look-classic", source: init({ ...common, look: "classic", handDrawnSeed: 7 }, stateLook) },
  { id: "state-look-neo", source: init({ ...common, look: "neo", handDrawnSeed: 7 }, stateLook) },
  { id: "state-look-handdrawn-7", source: init({ ...common, look: "handDrawn", handDrawnSeed: 7 }, stateLook) },
  { id: "state-look-handdrawn-9", source: init({ ...common, look: "handDrawn", handDrawnSeed: 9 }, stateLook) },
  { id: "state-look-wrong-case", source: init({ ...common, look: "handdrawn", handDrawnSeed: 7 }, stateLook) },
  { id: "swimlane-layout-unknown", source: init({ ...common, layout: "unknown" }, "swimlane-beta TB\nsubgraph one[One]\n  A[Start] --> B[Done]\nend\nsubgraph two[Two]\n  C[Review] --> D[Ship]\nend\nB --> C") },
  { id: "max-edges-init", source: init({ ...common, maxEdges: 1 }, "flowchart LR\nA --> B\nB --> C") },
  { id: "max-edges-frontmatter", source: frontmatter({ maxEdges: 1 }, "flowchart LR\nA --> B\nB --> C") },
  { id: "max-text-init", source: init({ ...common, maxTextSize: 10 }, "flowchart LR\nA --> B") },
  { id: "max-text-frontmatter", source: frontmatter({ maxTextSize: 10 }, "flowchart LR\nA --> B") },
  { id: "marker-flow-relative", source: init({ ...common, arrowMarkerAbsolute: false }, markerFlow) },
  { id: "marker-flow-root-absolute", source: init({ ...common, arrowMarkerAbsolute: true }, markerFlow) },
  { id: "marker-flow-family-absolute", source: init({ ...common, flowchart: { arrowMarkerAbsolute: true } }, markerFlow) },
  { id: "marker-sequence-relative", source: init({ ...common, sequence: { arrowMarkerAbsolute: false } }, markerSequence) },
  { id: "marker-sequence-root-absolute", source: init({ ...common, arrowMarkerAbsolute: true }, markerSequence) },
  { id: "marker-sequence-family-absolute", source: init({ ...common, sequence: { arrowMarkerAbsolute: true } }, markerSequence) },
  { id: "marker-class-relative", source: init({ ...common, class: { arrowMarkerAbsolute: false } }, markerClass) },
  { id: "marker-class-root-absolute", source: init({ ...common, arrowMarkerAbsolute: true }, markerClass) },
  { id: "marker-class-family-absolute", source: init({ ...common, class: { arrowMarkerAbsolute: true } }, markerClass) },
  { id: "marker-state-relative", source: init({ ...common, state: { arrowMarkerAbsolute: false } }, markerState) },
  { id: "marker-state-root-absolute", source: init({ ...common, arrowMarkerAbsolute: true }, markerState) },
  { id: "marker-state-family-absolute", source: init({ ...common, state: { arrowMarkerAbsolute: true } }, markerState) },
  { id: "marker-er-relative", source: init({ ...common, arrowMarkerAbsolute: false }, markerEr) },
  { id: "marker-er-root-absolute", source: init({ ...common, arrowMarkerAbsolute: true }, markerEr) },
  { id: "marker-requirement-relative", source: init({ ...common, arrowMarkerAbsolute: false }, markerRequirement) },
  { id: "marker-requirement-root-absolute", source: init({ ...common, arrowMarkerAbsolute: true }, markerRequirement) },
  { id: "marker-block-relative", source: init({ ...common, arrowMarkerAbsolute: false }, markerBlock) },
  { id: "marker-block-root-absolute", source: init({ ...common, arrowMarkerAbsolute: true }, markerBlock) },
  { id: "marker-swimlane-relative", source: init({ ...common, arrowMarkerAbsolute: false }, markerSwimlane) },
  { id: "marker-swimlane-root-absolute", source: init({ ...common, arrowMarkerAbsolute: true }, markerSwimlane) },
  { id: "theme-css", source: init({ ...common, themeCSS: ".node rect { fill: rgb(255, 0, 0) !important; stroke-width: 9px !important; }" }, "flowchart LR\nA --> B") },
];

const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true, args: ["--no-sandbox", "--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"] });
assert(await browser.version() === CHROME_PRODUCT, "Chrome product drifted");
const moduleUrl = pathToFileURL(moduleFile).href;
const fontUrl = pathToFileURL(fontFile).href;
const hostUrl = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;

const results = [];
for (let index = 0; index < cases.length; ++index) {
  const test = cases[index];
  const page = await browser.newPage();
  await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
  await page.goto(hostUrl);
  const result = await page.evaluate(async ({ source, id, moduleUrl, fontUrl }) => {
    document.body.style.margin = "0";
    document.body.innerHTML = '<div id="container"></div>';
    const style = document.createElement("style");
    style.textContent = `@font-face{font-family:"Noto Sans";src:url("${fontUrl}");font-weight:400;font-style:normal}`;
    document.head.appendChild(style);
    await document.fonts.load('16px "Noto Sans"', "Alpha Beta Gamma Delta Long cluster title Second node Outside ABC");
    await document.fonts.ready;
    const mermaid = (await import(`${moduleUrl}?case=${id}`)).default;
    mermaid.initialize({ startOnLoad: false, logLevel: "fatal" });
    try {
      const rendered = await mermaid.render(`remaining-${id}`, source);
      document.querySelector("#container").innerHTML = rendered.svg;
      const svg = document.querySelector("svg");
      const box = svg.getBBox();
      const client = svg.getBoundingClientRect();
      const all = [...svg.querySelectorAll("*")];
      return {
        status: "ready",
        diagramType: svg.getAttribute("aria-roledescription"),
        viewBox: svg.getAttribute("viewBox"),
        client: { width: client.width, height: client.height },
        bbox: { x: box.x, y: box.y, width: box.width, height: box.height },
        foreignObjectCount: svg.querySelectorAll("foreignObject").length,
        textCount: svg.querySelectorAll("text").length,
        nodeRects: [...svg.querySelectorAll(".node rect")].map((node) => ({
          bbox: (() => { const b = node.getBBox(); return { x: b.x, y: b.y, width: b.width, height: b.height }; })(),
          fill: getComputedStyle(node).fill,
          strokeWidth: getComputedStyle(node).strokeWidth,
        })),
        nodeGroups: [...svg.querySelectorAll(".node")].map((node) => {
          const b = node.getBBox();
          return { transform: node.getAttribute("transform"), x: b.x, y: b.y, width: b.width, height: b.height };
        }),
        clusters: [...svg.querySelectorAll(".cluster")].map((node) => {
          const b = node.getBBox();
          return { class: node.getAttribute("class"), transform: node.getAttribute("transform"),
            x: b.x, y: b.y, width: b.width, height: b.height,
            paths: [...node.querySelectorAll(":scope > path, :scope > g > path")].map((path) => ({
              d: path.getAttribute("d"), fill: getComputedStyle(path).fill,
              stroke: getComputedStyle(path).stroke,
              strokeWidth: getComputedStyle(path).strokeWidth,
            })),
          };
        }),
        stateClusters: [...svg.querySelectorAll(".statediagram-cluster")].map((node) => {
          const b = node.getBBox();
          return { class: node.getAttribute("class"), transform: node.getAttribute("transform"),
            x: b.x, y: b.y, width: b.width, height: b.height,
            paths: [...node.querySelectorAll(":scope > path, :scope > g > path")].map((path) => ({
              d: path.getAttribute("d"), fill: getComputedStyle(path).fill,
              stroke: getComputedStyle(path).stroke,
              strokeWidth: getComputedStyle(path).strokeWidth,
            })),
          };
        }),
        rootGroups: [...svg.querySelectorAll("g.root")].map((node) => {
          const b = node.getBBox();
          return { transform: node.getAttribute("transform"),
            x: b.x, y: b.y, width: b.width, height: b.height };
        }),
        labels: all.filter((node) => node.matches?.(".nodeLabel, .cluster-label, .edgeLabel")).map((node) => {
          const b = node.getBoundingClientRect();
          return { tag: node.tagName, text: node.textContent, width: b.width, height: b.height };
        }),
        svgTexts: [...svg.querySelectorAll("text")].map((node) => {
          const b = node.getBBox();
          return { text: node.textContent, transform: node.parentElement?.getAttribute("transform") ?? null,
            x: b.x, y: b.y, width: b.width, height: b.height };
        }),
        nodeLabelGroups: [...svg.querySelectorAll(".node .label")].map((node) => {
          const b = node.getBBox();
          return { text: node.textContent, transform: node.getAttribute("transform"),
            x: b.x, y: b.y, width: b.width, height: b.height };
        }),
        markers: all.filter((node) => node.hasAttribute?.("marker-start") || node.hasAttribute?.("marker-end")).map((node) => ({
          tag: node.tagName,
          id: node.getAttribute("id"),
          class: node.getAttribute("class"),
          d: node.getAttribute("d"),
          x1: node.getAttribute("x1"), y1: node.getAttribute("y1"),
          x2: node.getAttribute("x2"), y2: node.getAttribute("y2"),
          start: node.getAttribute("marker-start"), end: node.getAttribute("marker-end"),
        })),
        markerDefinitions: [...svg.querySelectorAll("marker")].map((node) => ({
          id: node.getAttribute("id"),
          viewBox: node.getAttribute("viewBox"),
          refX: node.getAttribute("refX"), refY: node.getAttribute("refY"),
          markerWidth: node.getAttribute("markerWidth"),
          markerHeight: node.getAttribute("markerHeight"),
          markerUnits: node.getAttribute("markerUnits"),
          orient: node.getAttribute("orient"),
          children: [...node.children].map((child) => ({
            tag: child.tagName,
            attributes: Object.fromEntries(
              [...child.attributes].map((attribute) => [attribute.name, attribute.value]),
            ),
            children: [...child.children].map((nested) => ({
              tag: nested.tagName,
              attributes: Object.fromEntries(
                [...nested.attributes].map((attribute) => [attribute.name, attribute.value]),
              ),
            })),
          })),
        })),
        roughNodes: [...svg.querySelectorAll(".rough-node")].map((node) => {
          const b = node.getBBox();
          return { tag: node.tagName, class: node.getAttribute("class"),
            transform: node.getAttribute("transform"),
            x: b.x, y: b.y, width: b.width, height: b.height,
            paths: [...node.querySelectorAll("path")].map((path) => ({
              d: path.getAttribute("d"), fill: getComputedStyle(path).fill,
              stroke: getComputedStyle(path).stroke,
              strokeWidth: getComputedStyle(path).strokeWidth,
            })),
          };
        }),
        stateTerminals: [...svg.querySelectorAll(".state-start, .state-end")].map((node) => {
          const b = node.getBBox();
          const parent = node.parentElement;
          const pb = parent?.getBBox();
          return { class: node.getAttribute("class"),
            x: b.x, y: b.y, width: b.width, height: b.height,
            parent: pb ? { x: pb.x, y: pb.y, width: pb.width, height: pb.height } : null,
          };
        }),
        roughEdges: [...svg.querySelectorAll('[data-edge="true"][data-look="handDrawn"]')].map((node) => {
          const b = node.getBBox();
          return { d: node.getAttribute("d"), x: b.x, y: b.y,
            width: b.width, height: b.height, stroke: getComputedStyle(node).stroke,
            strokeWidth: getComputedStyle(node).strokeWidth };
        }),
      };
    } catch (error) {
      return { status: "error", name: error?.name ?? "", message: String(error?.message ?? error), str: error?.str ?? "", hash: error?.hash ?? null };
    }
  }, { source: test.source, id: test.id, moduleUrl, fontUrl });
  results.push({ id: test.id, source: test.source, ...result });
  await page.close();
}

await browser.close();
const payload = {
  upstream: { version: VERSION, moduleSha256: MODULE_SHA, chromeProduct: CHROME_PRODUCT, chromeSha256: CHROME_SHA, fontSha256: NOTO_SHA },
  cases: results,
};
payload.fixtureSha256 = sha(JSON.stringify(payload));
fs.writeFileSync(outputFile, `${JSON.stringify(payload, null, 2)}\n`);
console.log(JSON.stringify({ outputFile, cases: results.length, fixtureSha256: payload.fixtureSha256 }, null, 2));
