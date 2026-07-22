import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ??
  path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const stateDbPath = path.resolve(process.argv[3] ??
  path.join("tests", "fixtures", "mermaid", "state-db.json"));
const output = path.resolve(process.argv[4] ??
  path.join("tests", "fixtures", "mermaid", "state-layout.json"));
const chrome = process.argv[5] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
const stateDb = JSON.parse(fs.readFileSync(stateDbPath, "utf8"));
if (pkg.version !== "11.16.0" || stateDb.upstream.version !== pkg.version)
  throw new Error("State layout oracle requires Mermaid 11.16.0 fixtures");

const selected = new Set([
  "transitions-start-end", "aliases-descriptions",
  "composite-direction-concurrency", "pseudostates",
]);
const cases = stateDb.cases.filter((item) => selected.has(item.id)).map((item) => ({
  id: item.id, source: item.source,
  expectedNodeIds: item.layoutInput.nodes.map((node) => node.id),
  expectedGroupFlags: item.layoutInput.nodes.map((node) => node.isGroup),
  expectedEdgeIds: item.layoutInput.edges.map((edge) => edge.id),
}));
cases.push({
  id: "renderable-note",
  source: "stateDiagram-v2\nActive --> Done\nnote right of Active : Inline note",
  expectedNodeIds: ["Active", "Done", "Active----parent", "Active----note-1"],
  expectedGroupFlags: [false, false, true, false],
  expectedEdgeIds: ["edge0", "Active-Active----note-1"],
});

const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [
  ["Noto Sans", "NotoSans-Regular.ttf", "U+0000-024F,U+1E00-1EFF"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf", "U+2E80-9FFF,U+3040-30FF,U+AC00-D7AF"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf", "U+0600-06FF,U+0750-077F,U+08A0-08FF"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf", "U+0590-05FF"],
];
const fontFaces = fonts.map(([family, file, range]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}");unicode-range:${range};}`
).join("\n");
const fontFamily = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';

const { default: puppeteer } = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true,
  args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const snapshots = await page.evaluate(async ({ cases, mermaidModule, fontFaces, fontFamily }) => {
    const { default: mermaid } = await import(mermaidModule);
    const style = document.createElement("style");
    style.textContent = fontFaces;
    document.head.appendChild(style);
    await document.fonts.load('16px "Noto Sans"', "State");
    await document.fonts.ready;
    const round = (value) => Math.round(value * 1000) / 1000;
    const point = (element, x = 0, y = 0) => {
      const value = new DOMPoint(x, y).matrixTransform(element.getCTM());
      return { x: value.x, y: value.y };
    };
    const relative = (value, origin) => ({
      x: round(value.x - origin.x), y: round(value.y - origin.y),
    });
    const bbox = (element) => {
      const value = element.getBBox();
      return { x: round(value.x), y: round(value.y),
        width: round(value.width), height: round(value.height) };
    };
    const result = [];
    for (const fixture of cases) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict",
        fontFamily, look: "classic", state: { padding: 8 } });
      const diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
      const graph = diagram.db.getData();
      const { svg } = await mermaid.render(`state-layout-${result.length}`, fixture.source);
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(resolve));

      const regularData = graph.nodes.filter((node) => !node.isGroup);
      const groupData = graph.nodes.filter((node) => node.isGroup);
      const regularIds = fixture.expectedNodeIds.filter((_, index) =>
        !fixture.expectedGroupFlags[index]);
      const groupIds = fixture.expectedNodeIds.filter((_, index) =>
        fixture.expectedGroupFlags[index]);
      const nodeElements = [...document.querySelectorAll("g.nodes > g.node")];
      const clusterElements = [...document.querySelectorAll("g.clusters > g[id]")];
      if (regularData.length !== regularIds.length || nodeElements.length !== regularIds.length ||
          groupData.length !== groupIds.length || clusterElements.length !== groupIds.length)
        throw new Error(`${fixture.id}: cannot associate state layout elements ` +
          `regular=${regularData.length}/${regularIds.length}/${nodeElements.length}, ` +
          `groups=${groupData.length}/${groupIds.length}/${clusterElements.length}`);
      const nodeCenters = nodeElements.map((element) => point(element));
      const origin = nodeCenters[0] ?? { x: 0, y: 0 };
      const nodes = nodeElements.map((element, index) => ({
        id: regularIds[index], center: relative(nodeCenters[index], origin),
        bbox: bbox(element), shape: regularData[index].shape ?? "",
      }));
      const clusters = clusterElements.map((element, index) => {
        const box = element.getBBox();
        const center = point(element, box.x + box.width / 2, box.y + box.height / 2);
        return { id: groupIds[index], center: relative(center, origin),
          bbox: bbox(element), shape: groupData[index].shape ?? "" };
      });
      const edges = graph.edges.map((edge, index) => {
        const id = fixture.expectedEdgeIds[index];
        const pathElement = document.querySelector(`g.edgePaths path[data-id="${edge.id}"]`);
        const labelElement = [...document.querySelectorAll("g.edgeLabel > g.label")]
          .find((element) => element.getAttribute("data-id") === edge.id)?.parentElement;
        if (!pathElement) throw new Error(`${fixture.id}: missing edge path ${edge.id}`);
        const length = pathElement.getTotalLength();
        const start = point(pathElement, pathElement.getPointAtLength(0).x,
          pathElement.getPointAtLength(0).y);
        const endPoint = pathElement.getPointAtLength(length);
        const end = point(pathElement, endPoint.x, endPoint.y);
        const label = labelElement?.hasAttribute("transform") ? point(labelElement) : null;
        return { id, path: pathElement.getAttribute("d") ?? "",
          start: relative(start, origin), end: relative(end, origin),
          labelCenter: label ? relative(label, origin) : null };
      });
      const svgElement = document.querySelector("svg");
      const structure = {
        root: {
          role: svgElement.getAttribute("role") ?? "",
          ariaRoledescription: svgElement.getAttribute("aria-roledescription") ?? "",
          viewBox: svgElement.getAttribute("viewBox") ?? "",
        },
        markers: [...svgElement.querySelectorAll("defs marker")].map((marker) => ({
          markerWidth: marker.getAttribute("markerWidth") ?? "",
          markerHeight: marker.getAttribute("markerHeight") ?? "",
          orient: marker.getAttribute("orient") ?? "",
          refX: marker.getAttribute("refX") ?? "",
          refY: marker.getAttribute("refY") ?? "",
          viewBox: marker.getAttribute("viewBox") ?? "",
          childTag: marker.firstElementChild?.tagName.toLowerCase() ?? "",
        })),
        nodes: nodeElements.map((element, index) => ({
          id: regularIds[index], classes: element.getAttribute("class") ?? "",
          childTags: [...element.children].map((child) => child.tagName.toLowerCase()),
          foreignObjectCount: element.querySelectorAll("foreignObject").length,
          textCount: element.querySelectorAll("text").length,
        })),
        clusters: clusterElements.map((element, index) => ({
          id: groupIds[index], classes: element.getAttribute("class") ?? "",
          childTags: [...element.children].map((child) => child.tagName.toLowerCase()),
        })),
        edges: graph.edges.map((edge, index) => {
          const element = document.querySelector(`g.edgePaths path[data-id="${edge.id}"]`);
          const markerEnd = element?.getAttribute("marker-end") ?? "";
          return { id: fixture.expectedEdgeIds[index],
            classes: element?.getAttribute("class") ?? "",
            markerEnd: markerEnd.includes("barbEnd") ? "barbEnd" : "",
            pathCommands: (element?.getAttribute("d")?.match(/[A-Za-z]/g) ?? []).join(""),
          };
        }),
      };
      result.push({ id: fixture.id, source: fixture.source,
        geometry: { nodes, clusters, edges, viewBox: svgElement.getAttribute("viewBox") },
        structure });
    }
    return result;
  }, { cases, mermaidModule, fontFaces, fontFamily });
  const payload = { upstream: { version: pkg.version },
    fontMode: "bundled-noto-2.13b171", cases: snapshots };
  payload.fixtureSha256 = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${snapshots.length} state layout cases to ${output}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
