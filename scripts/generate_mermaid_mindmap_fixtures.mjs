import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Freezes Mermaid 11.16.0 Mindmap renderer behavior. Mindmap is measured in
// SVG first and then laid out by Cytoscape 3.34 + cose-bilkent 4.1.0.
const EXPECTED_MERMAID_VERSION = "11.16.0";
const EXPECTED_MERMAID_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA256 =
  "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";
const VIEWPORT = { width: 1600, height: 1200, deviceScaleFactor: 1 };
const FONT_FAMILY = "Noto Sans";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const fixtureDir = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const pixelDir = path.join(fixtureDir, "mindmap-pixel");
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");

const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const readJson = (file) => JSON.parse(fs.readFileSync(file, "utf8"));
const assertEqual = (actual, expected, label) => {
  if (actual !== expected)
    throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const writeJson = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = readJson(path.join(mermaidRoot, "package.json"));
assertEqual(pkg.version, EXPECTED_MERMAID_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MERMAID_MODULE_SHA256,
            "Mermaid module sha256");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256,
            "Chrome sha256");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256,
            "NotoSans-Regular.ttf sha256");

const mermaidModule = pathToFileURL(moduleFile).href;
const fontUrl = pathToFileURL(fontFile).href;
const { default: puppeteer } = await import(pathToFileURL(path.join(
  path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js",
)).href);

const sourceInit = (config, body) =>
  `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const stableSource = (body, config = {}) => sourceInit({
  ...config,
  fontFamily: FONT_FAMILY,
  handDrawnSeed: config.handDrawnSeed ?? 7,
  themeVariables: {
    ...(config.themeVariables ?? {}),
    fontFamily: FONT_FAMILY,
  },
}, body);

const BASE = [
  "mindmap",
  "  root((Mindmap))",
  "    Origins",
  "      Long history",
  "      Popularisation",
  "    Research",
  "      On effectiveness",
  "      On features",
  "    Tools",
  "      Pen and paper",
  "      Mermaid",
].join("\n");
const SMALL = "mindmap\n  root((Root))\n    Alpha\n    Beta";
const SHAPES = [
  "mindmap",
  "  root((Root))",
  "    a[Rectangle]",
  "    b(Rounded)",
  "    c((Circle))",
  "    d)Cloud(",
  "    e))Bang((",
  "    f{{Hexagon}}",
  "    g[**Markdown** label]",
].join("\n");
const STAR = ["mindmap", "  root((Root))",
  ...Array.from({ length: 13 }, (_, i) => `    child${i}`)].join("\n");

const geometryCases = [
  { id: "canonical", source: stableSource(BASE) },
  { id: "single-root", source: stableSource("mindmap\n  root((Only root))") },
  { id: "chain", source: stableSource("mindmap\n  Root\n    One\n      Two\n        Three\n          Four") },
  { id: "balanced", source: stableSource(SMALL) },
  { id: "asymmetric", source: stableSource("mindmap\n  Root\n    A\n      A1\n      A2\n        A21\n    B") },
  { id: "all-shapes", source: stableSource(SHAPES) },
  { id: "section-wrap", source: stableSource(STAR) },
  { id: "markdown-wrap", source: stableSource(
      "mindmap\n  root((Root))\n    a[alpha beta gamma delta epsilon zeta eta theta iota]",
      { mindmap: { maxNodeWidth: 90 } }) },
  { id: "unbroken-overflow", source: stableSource(
      "mindmap\n  root((Root))\n    SUPERCALIFRAGILISTICEXPIALIDOCIOUS0123456789",
      { mindmap: { maxNodeWidth: 70 } }) },
  { id: "svg-labels", source: stableSource(SHAPES, { htmlLabels: false }) },
  { id: "svg-terminal-overhang", source: stableSource(
      "mindmap\n  Root\n    Test\n    Cat\n    Hat\n    Text\n    Fast\n    Rectangle",
      { htmlLabels: false, layout: "dagre" }) },
  { id: "look-neo", source: stableSource(SHAPES, { look: "neo", theme: "neo" }) },
  { id: "look-redux-color", source: stableSource(
      BASE, { look: "neo", theme: "redux-color" }) },
  { id: "look-hand-drawn", source: stableSource(SMALL, { look: "handDrawn" }) },
  { id: "layout-dagre", source: stableSource(BASE, { layout: "dagre" }) },
  { id: "padding-zero", source: stableSource(SMALL, { mindmap: { padding: 0 } }) },
  { id: "padding-string", source: stableSource(SMALL, { mindmap: { padding: "20" } }) },
  { id: "max-width-zero", source: stableSource(
      "mindmap\n  root((Root))\n    alpha beta gamma delta",
      { mindmap: { maxNodeWidth: 0 } }) },
  { id: "font-size-zero", source: stableSource(SMALL,
      { themeVariables: { fontSize: "0px" } }) },
  { id: "tcl-two", source: stableSource(STAR,
      { themeVariables: { THEME_COLOR_LIMIT: 2 } }) },
];

const configCases = [
  ...geometryCases,
  { id: "baseline", source: stableSource(SMALL) },
  ...[false, true, "false", 0, 1, null].map((value, i) => ({
    id: `use-max-width-${i}`, source: stableSource(SMALL,
      { mindmap: { useMaxWidth: value } }),
  })),
  ...[-20, 0, 20, "20", "0x14", null].map((value, i) => ({
    id: `padding-${i}`, source: stableSource(SMALL,
      { mindmap: { padding: value } }),
  })),
  ...[-20, 0, 80, "80", "0x50", null].map((value, i) => ({
    id: `max-width-${i}`, source: stableSource(
      "mindmap\n  root((Root))\n    alpha beta gamma delta epsilon",
      { mindmap: { maxNodeWidth: value } }),
  })),
  ...[false, true, "false", 0, 1].map((value, i) => ({
    id: `html-labels-${i}`, source: stableSource(SMALL, { htmlLabels: value }),
  })),
  ...[false, true, "false", 0, 1].map((value, i) => ({
    id: `auto-wrap-${i}`, source: stableSource(
      "mindmap\n  root((Root))\n    alpha beta gamma delta epsilon",
      { markdownAutoWrap: value, mindmap: { maxNodeWidth: 80 } }),
  })),
  ...["base", "default", "dark", "forest", "neutral", "neo", "neo-dark",
      "redux", "redux-dark", "redux-color", "redux-dark-color"].map((theme) => ({
    id: `theme-${theme}`, source: stableSource(SMALL, { theme }),
  })),
  ...[0, 1, 2, 2.5, 12, 13].map((value) => ({
    id: `tcl-${String(value).replace(".", "-")}`, source: stableSource(STAR,
      { themeVariables: { THEME_COLOR_LIMIT: value } }),
  })),
  { id: "gradient-false", source: stableSource(SMALL,
      { look: "neo", themeVariables: { useGradient: false } }) },
  { id: "gradient-string-false", source: stableSource(SMALL,
      { look: "neo", themeVariables: { useGradient: "false" } }) },
  { id: "gradient-colors", source: stableSource(SMALL, { look: "neo",
      themeVariables: { useGradient: true, gradientStart: "#ff0000",
                        gradientStop: "#00ff00" } }) },
  { id: "drop-shadow-none", source: stableSource(SMALL,
      { look: "neo", themeVariables: { dropShadow: "none" } }) },
  { id: "unknown-layout", source: stableSource(SMALL, { layout: "missing" }) },
  { id: "mindmap-layout-algorithm-inert", source: stableSource(SMALL,
      { mindmap: { layoutAlgorithm: "dagre" } }) },
  { id: "empty", source: stableSource("mindmap") },
  { id: "frontmatter-title", source:
      `---\ntitle: Ignored title\n---\n${stableSource(SMALL)}` },
];

const pixelCases = [
  { id: "default", source: stableSource(BASE) },
  { id: "dark", source: stableSource(BASE, { theme: "dark" }) },
  { id: "neo", source: stableSource(BASE, { theme: "neo", look: "neo" }) },
  { id: "redux-color", source: stableSource(BASE,
      { theme: "redux-color", look: "neo" }) },
  { id: "hand-drawn", source: stableSource(SMALL, { look: "handDrawn" }) },
];

const canonicalPng = (bytes) => {
  const image = PNG.sync.read(bytes);
  for (let i = 0; i < image.data.length; i += 4)
    if (image.data[i + 3] === 0)
      image.data[i] = image.data[i + 1] = image.data[i + 2] = 0;
  return PNG.sync.write(image, { colorType: 6, inputColorType: 6, bitDepth: 8 });
};

fs.mkdirSync(fixtureDir, { recursive: true });
fs.mkdirSync(pixelDir, { recursive: true });
const browser = await puppeteer.launch({
  headless: "new", executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--disable-lcd-text",
         "--font-render-hinting=none", "--force-color-profile=srgb",
         "--hide-scrollbars", "--lang=en-US"],
});

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const upstream = {
    package: "mermaid", version: EXPECTED_MERMAID_VERSION,
    module: "dist/mermaid.esm.mjs", moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
    browser: { product: EXPECTED_CHROME_PRODUCT,
               executableSha256: EXPECTED_CHROME_SHA256, headlessMode: "new" },
    viewport: VIEWPORT,
    fonts: [{ family: FONT_FAMILY,
              file: "third_party/noto/fonts/NotoSans-Regular.ttf",
              sha256: EXPECTED_NOTO_SHA256 }],
    layout: { cytoscape: "3.34.0", coseBilkent: "4.1.0",
              quality: "proof", styleEnabled: false, animate: false },
    generator: "scripts/generate_mermaid_mindmap_fixtures.mjs",
  };

  const preparePage = async () => {
    const page = await browser.newPage();
    await page.setViewport(VIEWPORT);
    await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
    await page.evaluate(async ({ family, url }) => {
      document.documentElement.setAttribute("lang", "en");
      document.body.style.margin = "0";
      document.body.innerHTML = '<div id="container"></div>';
      const style = document.createElement("style");
      style.textContent = `@font-face{font-family:"${family}";src:url("${url}");font-style:normal;font-weight:400}html,body{margin:0;padding:0}`;
      document.head.appendChild(style);
      await document.fonts.load(`16px "${family}"`, "Mindmap 0123456789");
      await document.fonts.ready;
    }, { family: FONT_FAMILY, url: fontUrl });
    return page;
  };

  const captureCases = async (cases) => {
    const page = await preparePage();
    const output = await page.evaluate(async ({ browserCases, family, moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      const round = (value) => Number.isFinite(value)
        ? Math.round(value * 1e6) / 1e6 : null;
      const box = (element) => {
        const value = element.getBBox();
        return { x: round(value.x), y: round(value.y),
                 width: round(value.width), height: round(value.height) };
      };
      const client = (element) => {
        const value = element.getBoundingClientRect();
        return { x: round(value.x), y: round(value.y),
                 width: round(value.width), height: round(value.height) };
      };
      const attrs = (element, names) => Object.fromEntries(names
        .map((name) => [name, element.getAttribute(name)])
        .filter(([, value]) => value !== null));
      const computed = (element) => {
        const value = getComputedStyle(element);
        return { fill: value.fill, stroke: value.stroke,
                 strokeWidth: value.strokeWidth, color: value.color,
                 fontFamily: value.fontFamily, fontSize: value.fontSize,
                 fontWeight: value.fontWeight, filter: value.filter };
      };
      const snapshot = (root) => ({
        root: { attrs: attrs(root, ["width", "height", "viewBox", "style",
                                      "role", "aria-roledescription"]),
                bbox: box(root), clientBox: client(root), computed: computed(root) },
        directOrder: [...root.children].map((element) => ({
          tag: element.tagName.toLowerCase(),
          class: element.getAttribute("class") ?? "",
        })),
        defs: [...root.querySelectorAll("defs")].map((defs) => ({
          filters: [...defs.querySelectorAll("filter")].map((filter) => ({
            attrs: attrs(filter, ["id", "height", "width"]),
            dropShadow: filter.querySelector("feDropShadow")
              ? attrs(filter.querySelector("feDropShadow"),
                      ["dx", "dy", "stdDeviation", "flood-opacity", "flood-color"])
              : null,
          })),
          gradients: [...defs.querySelectorAll("linearGradient")].map((gradient) => ({
            attrs: attrs(gradient, ["id", "gradientUnits", "x1", "y1", "x2", "y2"]),
            stops: [...gradient.querySelectorAll("stop")].map((stop) =>
              attrs(stop, ["offset", "stop-color", "stop-opacity"])),
          })),
        })),
        nodes: [...root.querySelectorAll("g.nodes > g.mindmap-node")].map((node) => {
          const shape = node.querySelector(":scope > rect, :scope > circle, :scope > polygon, :scope > path, :scope > g.basic");
          const label = node.querySelector(":scope > g.label");
          const foreign = label?.querySelector("foreignObject");
          const svgText = label?.querySelector("text");
          return {
            attrs: attrs(node, ["id", "class", "transform", "data-look"]),
            bbox: box(node), computed: computed(node),
            shape: shape ? { tag: shape.tagName.toLowerCase(),
              attrs: attrs(shape, ["class", "x", "y", "width", "height", "rx", "ry",
                                   "r", "cx", "cy", "points", "d", "fill", "stroke",
                                   "stroke-width", "style"]),
              bbox: box(shape), computed: computed(shape),
              roughPaths: shape.tagName.toLowerCase() === "g"
                ? [...shape.querySelectorAll(":scope > path")].map((path) => ({
                    attrs: attrs(path, ["d", "fill", "stroke", "stroke-width",
                                       "fill-opacity", "stroke-opacity"]),
                    bbox: box(path), computed: computed(path),
                  }))
                : [] } : null,
            label: label ? { text: label.textContent ?? "",
              attrs: attrs(label, ["class", "transform", "style"]),
              bbox: box(label), computed: computed(label),
              svgText: svgText ? {
                length: round(svgText.getComputedTextLength()),
                chars: Array.from({ length: svgText.getNumberOfChars() }, (_, i) => ({
                  length: round(svgText.getSubStringLength(i, 1)),
                  extent: box({ getBBox: () => svgText.getExtentOfChar(i) }),
                })),
              } : null,
              foreignObject: foreign ? {
                attrs: attrs(foreign, ["width", "height"]),
                bbox: box(foreign), html: foreign.innerHTML,
              } : null } : null,
          };
        }),
        edges: [...root.querySelectorAll("g.edgePaths > path")].map((edge) => ({
          attrs: attrs(edge, ["id", "class", "d", "fill", "stroke", "stroke-width",
                              "marker-start", "marker-end"]),
          bbox: box(edge), computed: computed(edge),
        })),
      });
      const result = [];
      for (let index = 0; index < browserCases.length; ++index) {
        const fixture = browserCases[index];
        try {
          mermaid.initialize({ startOnLoad: false, securityLevel: "strict",
                               theme: "default", fontFamily: family,
                               themeVariables: { fontFamily: family } });
          const { svg } = await mermaid.render(`mindmap-oracle-${index}`, fixture.source);
          document.getElementById("container").innerHTML = svg;
          await document.fonts.ready;
          await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
          const root = document.querySelector("#container > svg");
          result.push({ id: fixture.id, source: fixture.source, status: "ready",
                        expected: snapshot(root) });
        } catch (error) {
          result.push({ id: fixture.id, source: fixture.source, status: "error",
                        error: String(error?.message ?? error) });
        }
      }
      return result;
    }, { browserCases: cases, family: FONT_FAMILY, moduleUrl: mermaidModule });
    await page.close();
    return output;
  };

  const geometry = await captureCases(geometryCases);
  assertEqual(geometry.length, 20, "Mindmap geometry case count");
  assertEqual(geometry.filter((entry) => entry.status === "ready").length, 20,
              "Mindmap geometry ready count");
  const geometryById = new Map(geometry.map((entry) => [entry.id, entry.expected]));
  assertEqual(geometryById.get("canonical").nodes.length, 10,
              "canonical node count");
  assertEqual(geometryById.get("canonical").edges.length, 9,
              "canonical edge count");
  assertEqual(geometryById.get("all-shapes").nodes.length, 8,
              "shape node count");
  assertEqual(geometryById.get("look-hand-drawn").nodes.length, 3,
              "handDrawn node count");
  writeJson(path.join(fixtureDir, "mindmap-geometry.json"), {
    upstream,
    oracle: "Mindmap measured nodes, CoSE-Bilkent centers/edge paths, shapes, labels and paint",
    notes: [
      "Nodes are inserted and measured with SVG getBBox before CoSE-Bilkent runs.",
      "The default layout is cose-bilkent unless top-level layout was explicitly supplied.",
      "Cose output uses edge rscratch start/mid/end points, rendered through Mermaid's basis curve.",
    ],
    cases: geometry,
  });

  const config = await captureCases(configCases);
  writeJson(path.join(fixtureDir, "mindmap-config.json"), {
    upstream,
    oracle: "Mindmap source-entry config coercion, layout selection, theme/TCL and look cascade",
    cases: config,
  });

  const pixelManifest = [];
  for (const fixture of pixelCases) {
    const page = await preparePage();
    await page.evaluate(async ({ family, moduleUrl, renderId, source }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict",
                           theme: "default", fontFamily: family,
                           themeVariables: { fontFamily: family } });
      const { svg } = await mermaid.render(renderId, source);
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    }, { family: FONT_FAMILY, moduleUrl: mermaidModule,
         renderId: `mindmap-pixel-${fixture.id}`, source: fixture.source });
    const root = await page.$("#container > svg");
    const bytes = canonicalPng(await root.screenshot({ omitBackground: true }));
    const image = PNG.sync.read(bytes);
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), bytes);
    pixelManifest.push({ id: fixture.id, source: fixture.source, file,
                         width: image.width, height: image.height,
                         sha256: sha256(bytes) });
    await page.close();
  }
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream,
    oracle: "Mindmap default/dark/neo/redux-color/handDrawn browser raster goldens",
    capture: { background: "transparent", deviceScaleFactor: 1,
               pngCanonicalization: "transparent RGB zeroed; PNG RGBA8 re-encoded" },
    cases: pixelManifest,
  });

  console.log(`geometry cases: ${geometry.length} (${geometry.filter((x) => x.status === "ready").length} ready)`);
  console.log(`config cases: ${config.length} (${config.filter((x) => x.status === "error").length} errors)`);
  for (const entry of pixelManifest)
    console.log(`pixel ${entry.id}: ${entry.width}x${entry.height} ${entry.sha256}`);
} finally {
  await browser.close();
}
