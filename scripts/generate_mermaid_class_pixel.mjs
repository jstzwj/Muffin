import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

const mermaidRoot = path.resolve(process.argv[2] ??
  path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const outDir = path.resolve(process.argv[3] ??
  path.join("tests", "fixtures", "mermaid", "class-pixel"));
const chrome = process.argv[4] ??
  "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (pkg.version !== "11.16.0")
  throw new Error(`Expected Mermaid 11.16.0, found ${pkg.version}`);

const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [
  ["Noto Sans", "NotoSans-Regular.ttf"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf"],
];
const faces = fonts.map(([family, file]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}")}`
).join("\n");
const stack = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';
const init = (theme = "default", htmlLabels = true) =>
  `%%{init: {"theme":"${theme}","htmlLabels":${htmlLabels},` +
  `"themeVariables":{"fontFamily":${JSON.stringify(stack)}}}}%%\n`;
const cases = [
  { id: "label-node-html-math", cropOnly: true, cropTarget: "node",
    cropKind: "node-html-math", cropSelector: "g.node .label", source: init() + [
      "classDiagram", 'class A["<b>Service</b><br/>$$\\frac{x}{y}$$"]',
    ].join("\n") },
  { id: "label-node-markdown", cropOnly: true, cropTarget: "node",
    cropKind: "node-markdown", cropSelector: "g.node .label", dpr: 1.25,
    source: init() + ["classDiagram", 'class A["**Service** _value_"]'].join("\n") },
  { id: "label-node-cjk", cropOnly: true, cropTarget: "node",
    cropKind: "node-cjk", cropSelector: "g.node .label", dpr: 1.5,
    source: init() + ["classDiagram", 'class A["\u7528\u6237\u670d\u52a1\u65e0\u7a7a\u683c\u6362\u884c"]'].join("\n") },
  { id: "label-node-rtl-dark", cropOnly: true, cropTarget: "node",
    cropKind: "node-rtl", cropSelector: "g.node .label", dpr: 2, theme: "dark",
    source: init("dark") + ["classDiagram", 'class A["\u0645\u0631\u062d\u0628\u0627 123 \u05e9\u05dc\u05d5\u05dd"]'].join("\n") },
  { id: "label-edge-math-bidi", cropOnly: true, cropTarget: "edge",
    cropKind: "edge-math-bidi", cropSelector: "g.edgeLabel .label", dpr: 1.5,
    source: init() + ["classDiagram", "A --> B : $$\\sqrt{x^2}$$ \u05e9\u05dc\u05d5\u05dd 42"].join("\n") },
  { id: "label-cluster-cjk-rtl-dark", cropOnly: true, cropTarget: "cluster",
    cropKind: "cluster-cjk-rtl", cropSelector: "g.cluster .cluster-label", dpr: 1.25,
    theme: "dark", source: init("dark") + [
      "classDiagram", 'namespace Core["\u6838\u5fc3 \u0645\u0631\u062d\u0628\u0627"] {', "class A", "}",
    ].join("\n") },
  { id: "label-node-svg-multiline-cjk", cropOnly: true, cropTarget: "node",
    cropKind: "node-svg-multiline-cjk", cropSelector: "g.node .label-group .label",
    dpr: 1.5, htmlLabels: false, source: init("default", false) + [
      "classDiagram", 'class A["Service<br/>中文 مرحبا"]',
    ].join("\n") },
  { id: "label-node-svg-rtl-dark", cropOnly: true, cropTarget: "node",
    cropKind: "node-svg-rtl", cropSelector: "g.node .label-group .label",
    dpr: 2, theme: "dark", htmlLabels: false, source: init("dark", false) + [
      "classDiagram", 'class A["مرحبا 123 שלום"]',
    ].join("\n") },
  { id: "compartments", dpr: 1, source: init() + [
    "classDiagram", "accTitle: Service classes", "accDescr: Service inheritance model",
    "class Service {", "  <<interface>>", "  +String name",
    "  +run(input) Result*", "}", "class Empty", "Service <|-- Empty : extends",
  ].join("\n") },
  { id: "marker-matrix", dpr: 1, source: init() + [
    "classDiagram", "direction LR", "A <|-- B : extension", "C *-- D : composition",
    "E o-- F : aggregation", "G <.. H : dependency", "I ()-- J",
  ].join("\n") },
  { id: "note", dpr: 1, source: init() + [
    "classDiagram", "class Service", "note for Service \"A folded note\"",
    "class Client", "Client --> Service : uses",
  ].join("\n") },
  { id: "nested-namespaces", dpr: 1, source: init() + [
    "classDiagram", "namespace Company {", "  namespace Core {",
    "    class Api", "    class Worker", "  }", "}", "Api --> Worker : delegates",
  ].join("\n") },
  { id: "styled-members", dpr: 1.5, source: init() + [
    "classDiagram", "class Generic~T~ {", "  +T value", "  +get() T$",
    "  +abstract() void*", "}", "style Generic fill:#e8f5e9,stroke:#2e7d32,color:#102810",
  ].join("\n") },
  { id: "cjk-rtl", dpr: 1.5, source: init() + [
    "classDiagram", "class 用户服务 {", "  +字符串 名称", "  +执行() 结果",
    "}", "class مرحبا", "用户服务 --> مرحبا : שלום 123",
  ].join("\n") },
  { id: "dark-compound", dpr: 2, theme: "dark", source: init("dark") + [
    "classDiagram", "namespace Runtime {", "  class Store {", "    +save() void",
    "  }", "  class Cache", "}", "Store *-- Cache : owns",
  ].join("\n") },
  { id: "dark-note-markers", dpr: 1.25, theme: "dark", source: init("dark") + [
    "classDiagram", "direction LR", "class Client", "class Service",
    "note for Client \"客户端 مرحبا\"", "Client ..> Service : calls",
  ].join("\n") },
  { id: "svg-compartments", dpr: 1.25, htmlLabels: false,
    source: init("default", false) + [
      "classDiagram", "class Service {", "  <<interface>>", "  +名称",
      "  +run(value) Result*", "}",
    ].join("\n") },
];

const puppeteer = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
fs.mkdirSync(outDir, { recursive: true });

function writeStablePng(screenshot, outputPath) {
  const decoded = PNG.sync.read(screenshot);
  for (let offset = 0; offset < decoded.data.length; offset += 4) {
    if (decoded.data[offset + 3] === 0)
      decoded.data.fill(0, offset, offset + 3);
  }
  let png = PNG.sync.write(decoded, {
    colorType: 6, inputColorType: 6, bitDepth: 8,
  });
  if (fs.existsSync(outputPath)) {
    const existingBytes = fs.readFileSync(outputPath);
    const existing = PNG.sync.read(existingBytes);
    if (existing.width === decoded.width && existing.height === decoded.height) {
      let changedPixels = 0;
      let maximumDelta = 0;
      for (let offset = 0; offset < decoded.data.length; offset += 4) {
        let changed = false;
        for (let channel = 0; channel < 4; ++channel) {
          const delta = Math.abs(existing.data[offset + channel] -
                                 decoded.data[offset + channel]);
          changed ||= delta !== 0;
          maximumDelta = Math.max(maximumDelta, delta);
        }
        changedPixels += changed ? 1 : 0;
      }
      const changedRatio = changedPixels / (decoded.width * decoded.height);
      if (maximumDelta <= 72 && changedRatio <= 0.03) png = existingBytes;
    }
  }
  fs.writeFileSync(outputPath, png);
  return createHash("sha256").update(png).digest("hex");
}

const browser = await puppeteer.default.launch({
  executablePath: chrome, headless: true,
  args: ["--allow-file-access-from-files", "--disable-gpu",
    "--disable-lcd-text", "--font-render-hinting=none"],
});
try {
  const manifestCases = [];
  for (let index = 0; index < cases.length; ++index) {
    const fixture = cases[index];
    const page = await browser.newPage();
    await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: fixture.dpr });
    await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
    const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
    await page.evaluate(async ({ mermaidModule, faces, stack, source, id, theme,
                                 htmlLabels }) => {
      const { default: mermaid } = await import(mermaidModule);
      const style = document.createElement("style");
      style.textContent = faces;
      document.head.appendChild(style);
      await Promise.all([
        document.fonts.load('16px "Noto Sans"', "Class"),
        document.fonts.load('16px "Noto Sans CJK SC"', "中文"),
        document.fonts.load('16px "Noto Sans Arabic"', "مرحبا"),
        document.fonts.load('16px "Noto Sans Hebrew"', "שלום"),
      ]);
      await document.fonts.ready;
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict", theme,
        fontFamily: stack, themeVariables: { fontFamily: stack },
        htmlLabels, class: { padding: 12, hierarchicalNamespaces: true } });
      // The directive remains in the fixture source so the native cache sees the
      // same theme. Mermaid generates browser theme CSS during initialize(), so
      // applying it again during parsing would reset the fixed font to the
      // theme default after that CSS has already been emitted.
      const browserSource = source.split("\n").slice(1).join("\n");
      const { svg } = await mermaid.render(`class-pixel-${id}`, browserSource);
      document.body.style.margin = "0";
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    }, { mermaidModule, faces, stack, source: fixture.source, id: fixture.id,
         theme: fixture.theme ?? "default", htmlLabels: fixture.htmlLabels ?? true });
    const dimensions = await page.$eval("svg", (svg) => {
      const rect = svg.getBoundingClientRect();
      const viewBox = svg.viewBox.baseVal;
      const round = (value) => Math.round(value * 1000) / 1000;
      const box = (element) => {
        const value = element.getBBox();
        return { x: round(value.x), y: round(value.y),
          width: round(value.width), height: round(value.height) };
      };
      const computed = (element) => {
        const style = getComputedStyle(element);
        return { fill: style.fill, stroke: style.stroke, color: style.color,
          strokeWidth: style.strokeWidth, opacity: style.opacity };
      };
      const attributes = (element) => Object.fromEntries(
        [...element.attributes].map((attribute) => [attribute.name, attribute.value]));
      const nodes = [...svg.querySelectorAll("g.node")].map((element) => {
        const outer = element.querySelector(":scope > .outer-path") ??
          element.querySelector(":scope > rect, :scope > polygon, :scope > path");
        const paintedOuter = outer?.querySelector("path, rect, polygon, circle") ?? outer;
        const label = element.querySelector(".nodeLabel, .label span, .label text");
        return { id: element.id, text: element.textContent ?? "", bbox: box(element),
          outer: outer ? box(outer) : null, style: computed(paintedOuter ?? element),
          labelStyle: label ? computed(label) : null,
          outerTag: outer?.tagName ?? "", outerClass: outer?.getAttribute("class") ?? "" };
      });
      const clusters = [...svg.querySelectorAll("g.cluster")].map((element) => ({
        id: element.id, text: element.textContent ?? "", bbox: box(element),
        style: computed(element.querySelector("rect") ?? element),
      }));
      const edgePaths = [...svg.querySelectorAll("g.edgePaths path")].map((element) => ({
        id: element.id, style: computed(element), class: element.getAttribute("class") ?? "",
      }));
      const edgeLabels = [...svg.querySelectorAll("g.edgeLabels g.edgeLabel")].map((element) => {
        const label = element.querySelector(".label, .edgeLabel");
        return { id: element.id, text: element.textContent ?? "", bbox: box(element),
          style: computed(label ?? element) };
      });
      const markers = [...svg.querySelectorAll("defs marker")].map((element) => ({
        attributes: attributes(element),
        childTag: element.firstElementChild?.tagName ?? "",
        childAttributes: element.firstElementChild
          ? attributes(element.firstElementChild) : {},
      }));
      const labelContainers = [...svg.querySelectorAll(
        "g.node .label, g.edgeLabel .label, g.cluster .cluster-label")].map((element) => ({
          tag: element.tagName, class: element.getAttribute("class") ?? "",
          text: element.textContent ?? "",
          attributes: attributes(element),
          foreignObjectCount: element.querySelectorAll("foreignObject").length,
          textCount: element.querySelectorAll("text").length,
          tspanCount: element.querySelectorAll("tspan").length,
          mathCount: element.querySelectorAll("math").length,
        }));
      const domOrder = [...svg.querySelectorAll(
        "defs, marker, g.clusters, g.edgePaths, g.edgeLabels, g.nodes, g.cluster, g.edgeLabel, g.node, foreignObject, text, tspan, math")]
        .map((element) => `${element.tagName}:${element.getAttribute("class") ?? ""}`);
      const rootAttributes = attributes(svg);
      const ariaTitle = rootAttributes["aria-labelledby"]
        ? document.getElementById(rootAttributes["aria-labelledby"])?.textContent ?? "" : "";
      const ariaDescription = rootAttributes["aria-describedby"]
        ? document.getElementById(rootAttributes["aria-describedby"])?.textContent ?? "" : "";
      return { cssWidth: rect.width, cssHeight: rect.height,
        viewBox: { x: viewBox.x, y: viewBox.y, width: viewBox.width, height: viewBox.height },
        structure: { nodes, clusters, edgePaths, edgeLabels },
        svgStructure: { root: rootAttributes, markers, labelContainers, domOrder,
          ariaTitle, ariaDescription,
          counts: { defs: svg.querySelectorAll("defs").length,
            markers: markers.length, nodes: nodes.length, clusters: clusters.length,
            edgePaths: edgePaths.length, edgeLabels: edgeLabels.length,
            foreignObject: svg.querySelectorAll("foreignObject").length,
            math: svg.querySelectorAll("math").length } } };
    });
    if (fixture.cropOnly) {
      const labelBox = await page.$eval(fixture.cropSelector, (element) => {
        const rect = element.getBoundingClientRect();
        const content = element.querySelector(".nodeLabel,.edgeLabel,span,p") ?? element;
        const contentRect = content.getBoundingClientRect();
        const style = getComputedStyle(content);
        return { width: rect.width, height: rect.height,
          tag: element.tagName, class: element.getAttribute("class") ?? "",
          text: element.textContent ?? "",
          contentBox: { width: contentRect.width, height: contentRect.height },
          style: { fontSize: style.fontSize, lineHeight: style.lineHeight,
            fontFamily: style.fontFamily, fontWeight: style.fontWeight,
            fontStyle: style.fontStyle, direction: style.direction },
          foreignObjectCount: element.querySelectorAll("foreignObject").length +
            (element.tagName === "foreignObject" ? 1 : 0),
          textCount: element.querySelectorAll("text").length +
            (element.tagName === "text" ? 1 : 0),
          tspanCount: element.querySelectorAll("tspan").length,
          mathCount: element.querySelectorAll("math").length };
      });
      const crop = await page.$eval(fixture.cropSelector, (element) => {
        const rect = element.getBoundingClientRect();
        const root = document.querySelector("svg");
        for (const node of root.querySelectorAll("*")) node.style.visibility = "hidden";
        for (let node = element; node && node !== root; node = node.parentElement)
          node.style.visibility = "visible";
        for (const node of element.querySelectorAll("*")) node.style.visibility = "visible";
        for (const node of element.querySelectorAll("div,span,p"))
          node.style.background = "transparent";
        const padding = 4;
        return { x: Math.max(0, rect.left - padding), y: Math.max(0, rect.top - padding),
          width: Math.max(1, rect.width + padding * 2),
          height: Math.max(1, rect.height + padding * 2) };
      });
      await page.evaluate(() => new Promise((resolve) =>
        requestAnimationFrame(() => requestAnimationFrame(resolve))));
      const cropFile = `${fixture.id}-label.png`;
      const cropBytes = await page.screenshot({ omitBackground: true, clip: crop });
      const cropSha256 = writeStablePng(cropBytes, path.join(outDir, cropFile));
      manifestCases.push({ ...fixture, cropFile, cropSha256, labelBox, ...dimensions });
      await page.close();
      continue;
    }
    const file = `${fixture.id}.png`;
    await page.$eval("svg", (svg) => {
      svg.style.display = "block";
      document.body.style.width = `${svg.getBoundingClientRect().width}px`;
      document.body.style.height = `${svg.getBoundingClientRect().height}px`;
    });
    const clip = await page.$eval("svg", (svg) => {
      const rect = svg.getBoundingClientRect();
      return { x: rect.left, y: rect.top, width: rect.width, height: rect.height };
    });
    const screenshot = await page.screenshot({ omitBackground: true, clip });
    const outputPath = path.join(outDir, file);
    const sha256 = writeStablePng(screenshot, outputPath);
    let maskFile;
    let maskSha256;
    let textMaskFile;
    let textMaskSha256;
    if (fixture.id === "marker-matrix") {
      await page.$eval("svg", (svg) => {
        const transparent = "rgba(0, 0, 0, 0)";
        const markerElements = [...svg.querySelectorAll(
          "defs marker path, defs marker polygon, defs marker circle")];
        const markerFilled = new Map(markerElements.map((element) => {
          const fill = getComputedStyle(element).fill;
          return [element, fill !== "none" && fill !== "transparent" &&
            fill !== "rgba(0, 0, 0, 0)"];
        }));
        for (const element of svg.querySelectorAll("path,rect,line,circle,polygon,text,tspan")) {
          element.style.setProperty("fill", transparent, "important");
          element.style.setProperty("stroke", transparent, "important");
        }
        for (const element of svg.querySelectorAll("foreignObject,foreignObject *")) {
          element.style.setProperty("color", transparent, "important");
          element.style.setProperty("background", transparent, "important");
        }
        for (const element of svg.querySelectorAll("g.node .outer-path *, g.node > rect, g.node .divider *")) {
          element.style.setProperty("fill", "#ff0000", "important");
          element.style.setProperty("stroke", "#ff0000", "important");
        }
        for (const element of svg.querySelectorAll("g.node .label, g.node .label *")) {
          element.style.setProperty("fill", "#ff00ff", "important");
          element.style.setProperty("color", "#ff00ff", "important");
          element.style.setProperty("background", transparent, "important");
        }
        for (const element of svg.querySelectorAll("g.edgePaths path")) {
          element.style.setProperty("fill", transparent, "important");
          element.style.setProperty("stroke", "#00ff00", "important");
        }
        for (const element of svg.querySelectorAll("g.edgeLabels g.edgeLabel .label, g.edgeLabels g.edgeLabel .label *")) {
          element.style.setProperty("fill", "#ff00ff", "important");
          element.style.setProperty("color", "#ff00ff", "important");
          element.style.setProperty("background", "#0000ff", "important");
        }
        for (const element of markerElements) {
          element.style.setProperty("stroke", "#00ffff", "important");
          element.style.setProperty("fill", markerFilled.get(element)
            ? "#00ffff" : transparent, "important");
        }
      });
      maskFile = "marker-matrix-mask.png";
      const mask = await page.screenshot({ omitBackground: true, clip });
      maskSha256 = writeStablePng(mask, path.join(outDir, maskFile));
      await page.$eval("svg", (svg) => {
        const transparent = "rgba(0, 0, 0, 0)";
        for (const element of svg.querySelectorAll("path,rect,line,circle,polygon,text,tspan")) {
          element.style.setProperty("fill", transparent, "important");
          element.style.setProperty("stroke", transparent, "important");
        }
        for (const element of svg.querySelectorAll("foreignObject,foreignObject *")) {
          element.style.setProperty("color", transparent, "important");
          element.style.setProperty("background", transparent, "important");
        }
        for (const element of svg.querySelectorAll(
          "g.node .label, g.node .label *, g.edgeLabels g.edgeLabel .label, g.edgeLabels g.edgeLabel .label *")) {
          element.style.setProperty("fill", "#ff00ff", "important");
          element.style.setProperty("color", "#ff00ff", "important");
        }
      });
      textMaskFile = "marker-matrix-text-mask.png";
      const textMask = await page.screenshot({ omitBackground: true, clip });
      textMaskSha256 = writeStablePng(
        textMask, path.join(outDir, textMaskFile));
    }
    manifestCases.push({ ...fixture, file, sha256,
      ...(maskFile ? { maskFile, maskSha256, textMaskFile, textMaskSha256 } : {}),
      ...dimensions });
    await page.close();
  }
  const payload = { mermaidVersion: pkg.version,
    fontMode: "bundled-noto-2.13b171", cases: manifestCases };
  payload.fixtureSha256 = createHash("sha256")
    .update(JSON.stringify(payload)).digest("hex");
  fs.writeFileSync(path.join(outDir, "manifest.json"),
    `${JSON.stringify(payload, null, 2)}\n`);
  const references = new Set(manifestCases.flatMap((fixture) =>
    [fixture.file, fixture.maskFile, fixture.textMaskFile, fixture.cropFile].filter(Boolean)));
  for (const name of fs.readdirSync(outDir).filter((name) => name.endsWith(".png")))
    if (!references.has(name)) fs.rmSync(path.join(outDir, name));
  console.log(`Wrote ${manifestCases.length} class pixel cases to ${outDir}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
