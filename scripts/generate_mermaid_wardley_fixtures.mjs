import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Wardley source-entry oracle.
const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_WARDLEY_MODULE_SHA256 =
  "7688a218dfc9e1eccdb8fca61d89414723ab05f3aced6f970a4ca6464e9ca3ee";
const EXPECTED_PARSER_MODULE_SHA256 =
  "f541603e5c4d057f0c557f0873bd5b3be3c9878caed7ef2b6ee4e6699206dd3d";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA256 =
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
const wardleyModuleFile = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.core", "wardleyDiagram-EHGQE667.mjs",
);
const parserModuleFile = path.join(
  path.dirname(mermaidRoot), "@mermaid-js", "parser", "dist", "chunks",
  "mermaid-parser.esm", "chunk-36B4POZ4.mjs",
);
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assertEqual = (actual, expected, label) => {
  if (actual !== expected) throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const writeJson = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assertEqual(pkg.version, EXPECTED_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MODULE_SHA256, "Mermaid module");
assertEqual(sha256(fs.readFileSync(wardleyModuleFile)), EXPECTED_WARDLEY_MODULE_SHA256, "Wardley module");
assertEqual(sha256(fs.readFileSync(parserModuleFile)), EXPECTED_PARSER_MODULE_SHA256, "Parser module");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const frontmatter = (config, body, title = undefined) => {
  const lines = ["---"];
  if (title !== undefined) lines.push(`title: ${title}`);
  if (config !== undefined) {
    lines.push("config:");
    for (const line of JSON.stringify(config, null, 2).split("\n")) lines.push(`  ${line}`);
  }
  lines.push("---", body);
  return lines.join("\n");
};

const canonical = `wardley-beta
title Platform Landscape
accTitle: Platform map
accDescr: Dependencies and evolution
size [900,600]
evolution Genesis @0.18 -> Custom Built @0.42 -> Product @0.72 -> Commodity @1.0
anchor User [0.95,0.08]
component Platform [0.82,0.34] label [12,-14] (build) inertia
component Service [0.62,0.58] (buy)
component Supplier [0.44,0.78] (outsource)
component Market [0.30,0.90] (market)
User -> Platform
Platform +'depends'> Service ; ignored because flow label wins
Service -.-> Supplier ; contract
Supplier +<> Market
evolve Service 0.88
note 'Important dependency' [0.72,0.43]
annotations [0.18,0.78]
annotation 1, [0.55,0.53] 'Watch the transition'
accelerator Adoption [0.28,0.18]
deaccelerator Regulation [0.35,0.75]
pipeline Platform {
  component Prototype [0.12] label [-4,12]
  component Productised [0.52]
  component Utility [0.86]
}`;
const configCanonical = canonical.replace("size [900,600]\n", "");

const grammarCases = [
  ["canonical", canonical],
  ["header-only", "wardley-beta"],
  ["header-newline", "wardley-beta\n"],
  ["uppercase-header", "WARDLEY-BETA\ncomponent A [0.5,0.5]"],
  ["mixed-header", "Wardley-Beta\ncomponent A [0.5,0.5]"],
  ["prefix", "wardley-betaX\ncomponent A [0.5,0.5]"],
  ["wrong-header", "wardley\ncomponent A [0.5,0.5]"],
  ["same-line", "wardley-beta component A [0.5,0.5]"],
  ["blank-lines", "\nwardley-beta\n\ncomponent A [0.5,0.5]\n"],
  ["comment", "%% before\nwardley-beta\n%% body\ncomponent A [0.5,0.5]"],
  ["directive", init({ "wardley-beta": { width: 480 } }, "wardley-beta\ncomponent A [0.5,0.5]")],
  ["frontmatter", frontmatter({ "wardley-beta": { width: 480 } }, "wardley-beta\ncomponent A [0.5,0.5]", "Front")],
  ["metadata", "wardley-beta\ntitle Inline\naccTitle: Accessible\naccDescr: Description\ncomponent A [0.5,0.5]"],
  ["acc-block", "wardley-beta\naccDescr {first\n  second}\ncomponent A [0.5,0.5]"],
  ["size", "wardley-beta\nsize [480,320]\ncomponent A [0.5,0.5]"],
  ["size-float", "wardley-beta\nsize [480.5,320]\ncomponent A [0.5,0.5]"],
  ["evolution-default-names", "wardley-beta\nevolution Genesis -> Custom Built -> Product -> Commodity\ncomponent A [0.5,0.5]"],
  ["evolution-boundaries-dual", "wardley-beta\nevolution Genesis @0.2 / Concept -> Custom Built @0.5 / Emerging -> Product @0.8 / Rental -> Commodity @1.0 / Utility\ncomponent A [0.5,0.5]"],
  ["evolution-single", "wardley-beta\nevolution Genesis\ncomponent A [0.5,0.5]"],
  ["anchor", "wardley-beta\nanchor User [1.0,0.0]\ncomponent A [0.5,0.5]"],
  ["anchor-integer", "wardley-beta\nanchor User [1,0.0]\ncomponent A [0.5,0.5]"],
  ["quoted-names", "wardley-beta\nanchor 'User role' [0.9,0.1]\ncomponent 'Core API' [0.5,0.5]"],
  ["markup-name", "wardley-beta\ncomponent '<script>x</script><b>A</b>' [0.5,0.5]"],
  ["coordinates-percent", "wardley-beta\ncomponent A [80.0,30.0]"],
  ["coordinates-over-one", "wardley-beta\ncomponent A [1.1,2.0]"],
  ["coordinates-out-of-range", "wardley-beta\ncomponent A [101.0,50.0]"],
  ["coordinates-negative", "wardley-beta\ncomponent A [-1,0.5]"],
  ["label-offsets", "wardley-beta\ncomponent A [0.5,0.5] label [-12,-7]"],
  ["strategies", "wardley-beta\ncomponent A [0.9,0.1] (build)\ncomponent B [0.7,0.3] (buy)\ncomponent C [0.5,0.5] (outsource)\ncomponent D [0.3,0.7] (market)"],
  ["inertia", "wardley-beta\ncomponent A [0.5,0.5] inertia\ncomponent B [0.4,0.6] (inertia)"],
  ["bad-strategy", "wardley-beta\ncomponent A [0.5,0.5] (rent)"],
  ["duplicate-node", "wardley-beta\ncomponent A [0.8,0.2] label [1,2]\ncomponent A [0.4,0.6] inertia"],
  ["links", "wardley-beta\ncomponent A [0.8,0.2]\ncomponent B [0.5,0.5]\nA -> B\nA --> B\nA -.-> B ; dashed\nA +> B\nA +< B\nA +<> B\nA +'flow'> B ; annotation"],
  ["missing-link-node", "wardley-beta\ncomponent A [0.5,0.5]\nA -> Missing"],
  ["evolve", "wardley-beta\ncomponent A [0.5,0.5]\nevolve A 0.9\nevolve Missing 0.7"],
  ["evolve-out-of-range", "wardley-beta\ncomponent A [0.5,0.5]\nevolve A 101.0"],
  ["pipeline", "wardley-beta\ncomponent Parent [0.7,0.5]\npipeline Parent {\ncomponent One [0.1]\ncomponent Two [0.8] label [-2,3]\n}"],
  ["pipeline-missing-parent", "wardley-beta\npipeline Missing {\ncomponent One [0.1]\n}"],
  ["pipeline-no-components", "wardley-beta\ncomponent Parent [0.7,0.5]\npipeline Parent {\n}"],
  ["note", "wardley-beta\nnote 'A note' [0.7,0.4]"],
  ["note-unquoted", "wardley-beta\nnote A note [0.7,0.4]"],
  ["annotations", "wardley-beta\nannotations [20,80]\nannotation 2, [0.4,0.6] 'Second'\nannotation 1, [50,50] 'First'"],
  ["accelerators", "wardley-beta\naccelerator Fast [0.3,0.2]\ndeaccelerator Slow [0.4,0.7]"],
  ["accelerator-integer", "wardley-beta\naccelerator Fast [1,0.2]"],
  ["semicolon", "wardley-beta\ncomponent A [0.5,0.5];"],
  ["crlf", "wardley-beta\r\ncomponent A [0.5,0.5]\r\n"],
].map(([id, source]) => ({ id, source }));

const geometryCases = [
  ["canonical", canonical, {}],
  ["empty", "wardley-beta", {}],
  ["single", "wardley-beta\ncomponent A [0.5,0.5]", {}],
  ["grid", "wardley-beta\ncomponent A [0.5,0.5]", { "wardley-beta": { showGrid: true } }],
  ["custom-size", "wardley-beta\nsize [520,360]\ncomponent A [0.5,0.5]", {}],
  ["diagram-size", "wardley-beta\nsize [480,320]\ncomponent A [0.5,0.5]", { "wardley-beta": { width: 700, height: 500 } }],
  ["custom-evolution", grammarCases.find((c) => c.id === "evolution-boundaries-dual").source, {}],
  ["strategies", grammarCases.find((c) => c.id === "strategies").source, {}],
  ["links", grammarCases.find((c) => c.id === "links").source, {}],
  ["pipeline", grammarCases.find((c) => c.id === "pipeline").source, {}],
  ["annotations", grammarCases.find((c) => c.id === "annotations").source, {}],
  ["notes-arrows", "wardley-beta\nnote 'Note' [0.7,0.4]\naccelerator Fast [0.3,0.2]\ndeaccelerator Slow [0.4,0.7]", {}],
  ["labels", "wardley-beta\nanchor 'Anchor label' [0.9,0.1]\ncomponent 'Offset label' [0.5,0.5] label [-20,18] (outsource) inertia", {}],
  ["long-label", "wardley-beta\ncomponent 'A deliberately long component label that never wraps' [0.5,0.5]", {}],
  ["title", "wardley-beta\ntitle Wardley Title\ncomponent A [0.5,0.5]", {}],
  ["fixed-width", canonical, { "wardley-beta": { useMaxWidth: false } }],
].map(([id, body, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", ...config }, body),
}));

const themes = [
  "base", "dark", "default", "forest", "neutral", "neo", "neo-dark",
  "redux", "redux-dark", "redux-color", "redux-dark-color",
];
const styleKeys = [
  "backgroundColor", "axisColor", "axisTextColor", "gridColor",
  "componentFill", "componentStroke", "componentLabelColor", "linkStroke",
  "evolutionStroke", "annotationStroke", "annotationTextColor", "annotationFill",
];
const styleValues = Object.fromEntries(styleKeys.map((key, index) => [
  key, `#${String(index + 1).padStart(2, "0")}1122`,
]));
const configCases = [
  ["defaults", {}],
  ["use-max-false", { "wardley-beta": { useMaxWidth: false } }],
  ["use-max-string", { "wardley-beta": { useMaxWidth: "false" } }],
  ["use-width", { "wardley-beta": { useWidth: 500 } }],
  ["width", { "wardley-beta": { width: 480 } }],
  ["height", { "wardley-beta": { height: 360 } }],
  ["padding", { "wardley-beta": { padding: 20 } }],
  ["node-radius", { "wardley-beta": { nodeRadius: 12 } }],
  ["node-label-offset", { "wardley-beta": { nodeLabelOffset: 20 } }],
  ["axis-font-size", { "wardley-beta": { axisFontSize: 22 } }],
  ["label-font-size", { "wardley-beta": { labelFontSize: 18 } }],
  ["show-grid", { "wardley-beta": { showGrid: true } }],
  ["show-grid-string", { "wardley-beta": { showGrid: "false" } }],
  ["width-string", { "wardley-beta": { width: "480" } }],
  ["width-zero", { "wardley-beta": { width: 0 } }],
  ["width-null", { "wardley-beta": { width: null } }],
  ["width-array", { "wardley-beta": { width: [480] } }],
  ["height-string", { "wardley-beta": { height: "360" } }],
  ["padding-string", { "wardley-beta": { padding: "20" } }],
  ["radius-string", { "wardley-beta": { nodeRadius: "12" } }],
  ["legacy-wardley-key", { wardley: { width: 480, showGrid: true } }],
  ["frontmatter", null],
  ...themes.map((theme) => [`theme-${theme}`, { theme }]),
  ...styleKeys.map((key) => [
    `style-${key}`,
    { themeVariables: { wardley: { [key]: styleValues[key] } } },
  ]),
].map(([id, config]) => ({
  id,
  source: id === "frontmatter"
    ? frontmatter({ fontFamily: "Noto Sans", "wardley-beta": {
      width: 520, height: 360, padding: 20, nodeRadius: 9,
      nodeLabelOffset: 14, axisFontSize: 17, labelFontSize: 15,
      showGrid: true, useMaxWidth: false,
    } }, configCanonical, "Front title")
    : init({ fontFamily: "Noto Sans", ...config }, configCanonical),
}));

const pixelCases = [
  ["default", {}],
  ["dark", { theme: "dark" }],
  ["forest", { theme: "forest" }],
  ["strategies", {}, grammarCases.find((c) => c.id === "strategies").source],
].map(([id, config, body = canonical]) => ({
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
  package: "mermaid",
  version: EXPECTED_VERSION,
  moduleSha256: EXPECTED_MODULE_SHA256,
  wardleyModuleSha256: EXPECTED_WARDLEY_MODULE_SHA256,
  parserModuleSha256: EXPECTED_PARSER_MODULE_SHA256,
  chromeProduct: EXPECTED_CHROME_PRODUCT,
  chromeSha256: EXPECTED_CHROME_SHA256,
  notoSansSha256: EXPECTED_NOTO_SHA256,
  sourceEntry: true,
};

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const hostPage = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const page = await browser.newPage();
  await page.setViewport({ width: 1400, height: 1000, deviceScaleFactor: 1 });
  const prepare = async () => {
    await page.goto(hostPage);
    await page.evaluate(async (fontUrl) => {
      document.body.style.margin = "0";
      const font = new FontFace("Noto Sans", `url(${fontUrl})`);
      await font.load();
      document.fonts.add(font);
      await document.fonts.load("24px 'Noto Sans'");
    }, pathToFileURL(fontFile).href);
  };
  const render = async (source, id) => page.evaluate(async ({ source, id, moduleUrl }) => {
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const result = await mermaid.render(`wardley-${id}`, source);
    document.body.innerHTML = result.svg;
    const svg = document.querySelector("svg");
    const attrs = (element, names) => Object.fromEntries(
      names.map((name) => [name, element?.getAttribute(name) ?? null]),
    );
    const box = (element) => {
      if (!element) return null;
      const value = element.getBBox();
      return { x: value.x, y: value.y, width: value.width, height: value.height };
    };
    const primitive = (element) => {
      const style = getComputedStyle(element);
      return {
        tag: element.tagName,
        parentClass: element.parentElement?.getAttribute("class") ?? "",
        text: element.tagName === "text" ? element.textContent : "",
        attrs: attrs(element, [
          "class", "x", "y", "x1", "y1", "x2", "y2", "cx", "cy", "r",
          "width", "height", "rx", "ry", "d", "transform", "fill", "stroke",
          "stroke-width", "stroke-dasharray", "opacity", "font-size", "font-weight",
          "text-anchor", "dominant-baseline", "marker-start", "marker-end",
        ]),
        bbox: box(element),
        computed: {
          fill: style.fill,
          stroke: style.stroke,
          strokeWidth: style.strokeWidth,
          strokeDasharray: style.strokeDasharray,
          opacity: style.opacity,
          fontFamily: style.fontFamily,
          fontSize: style.fontSize,
          fontWeight: style.fontWeight,
          textAnchor: style.textAnchor,
        },
      };
    };
    const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
    const map = svg.querySelector("g.wardley-map");
    return {
      db: JSON.parse(JSON.stringify(diagram.db.getWardleyData())),
      metadataDb: {
        title: diagram.db.getDiagramTitle(),
        accTitle: diagram.db.getAccTitle(),
        accDescr: diagram.db.getAccDescription(),
      },
      root: {
        attrs: attrs(svg, ["id", "class", "viewBox", "width", "height", "style", "role", "aria-roledescription", "aria-labelledby"]),
        bbox: box(svg),
        client: { width: svg.getBoundingClientRect().width, height: svg.getBoundingClientRect().height },
        order: [...svg.children].map((child) => child.getAttribute("class") || child.tagName),
      },
      container: {
        bbox: box(map),
        order: [...map.children].map((child) => child.getAttribute("class") || child.tagName),
      },
      primitives: [...map.querySelectorAll("rect,line,circle,path,text")].map(primitive),
      markers: [...svg.querySelectorAll("defs marker")].map((marker) => ({
        attrs: attrs(marker, ["id", "viewBox", "refX", "refY", "markerWidth", "markerHeight", "orient"]),
        path: primitive(marker.querySelector("path")),
      })),
      metadata: [...svg.querySelectorAll(":scope > title, :scope > desc")].map((element) => ({ tag: element.tagName, text: element.textContent })),
    };
  }, { source, id, moduleUrl: pathToFileURL(moduleFile).href });

  const grammar = [];
  for (const test of grammarCases) {
    await prepare();
    const result = await page.evaluate(async ({ source, moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
      try {
        await mermaid.parse(source);
        let db;
        try {
          const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
          db = {
            data: JSON.parse(JSON.stringify(diagram.db.getWardleyData())),
            title: diagram.db.getDiagramTitle(),
            accTitle: diagram.db.getAccTitle(),
            accDescr: diagram.db.getAccDescription(),
          };
        } catch {}
        try {
          await mermaid.render("grammar-wardley", source);
          return { parse: true, render: true, db };
        } catch (error) {
          return { parse: true, render: false, db, error: { name: error?.name ?? "Error", message: String(error?.message ?? error) } };
        }
      } catch (error) {
        const parser = error?.result?.parserErrors?.[0];
        const lexer = error?.result?.lexerErrors?.[0];
        return {
          parse: false,
          render: false,
          error: {
            name: error?.name ?? "Error",
            message: String(error?.message ?? error),
            kind: lexer ? "Lexer" : parser ? "Parser" : "Runtime",
            line: Number(parser?.token?.startLine ?? lexer?.line ?? 0),
            column: Number(parser?.token?.startColumn ?? lexer?.column ?? 0),
            token: String(parser?.token?.text ?? lexer?.character ?? ""),
          },
        };
      }
    }, { source: test.source, moduleUrl: pathToFileURL(moduleFile).href });
    grammar.push({ ...test, expected: result });
  }

  const geometry = [];
  for (const test of geometryCases) {
    await prepare();
    geometry.push({ ...test, expected: await render(test.source, test.id) });
  }
  const config = [];
  for (const test of configCases) {
    await prepare();
    config.push({ ...test, expected: await render(test.source, `config-${test.id}`) });
  }

  const pixelDir = path.join(fixtureDir, "wardley-pixel");
  fs.mkdirSync(pixelDir, { recursive: true });
  const pixelManifest = [];
  for (const test of pixelCases) {
    await prepare();
    await render(test.source, `pixel-${test.id}`);
    const svg = await page.$("svg");
    const file = path.join(pixelDir, `${test.id}.png`);
    await svg.screenshot({ path: file, omitBackground: true });
    const bounds = await svg.boundingBox();
    pixelManifest.push({
      id: test.id,
      source: test.source,
      file: `${test.id}.png`,
      width: Math.round(bounds.width),
      height: Math.round(bounds.height),
      sha256: sha256(fs.readFileSync(file)),
    });
  }
  fs.mkdirSync(fixtureDir, { recursive: true });
  writeJson(path.join(fixtureDir, "wardley-grammar.json"), { upstream: provenance, cases: grammar });
  writeJson(path.join(fixtureDir, "wardley-geometry.json"), { upstream: provenance, cases: geometry });
  writeJson(path.join(fixtureDir, "wardley-config.json"), { upstream: provenance, cases: config });
  writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixelManifest });
  console.log(`Generated Wardley fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixelManifest.length} pixel.`);
} finally {
  await browser.close();
}
