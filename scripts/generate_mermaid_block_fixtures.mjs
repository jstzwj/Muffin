import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_BLOCK_MODULE_SHA256 =
  "2e993bfcf368ff51b44be0b4eea45d293fde010f1d5e79e3ed556fa648a77ba3";
const EXPECTED_BLOCK_MAP_SHA256 =
  "11743524dc75ca380e817b58695c317e6ca17d1c875d1c3ad580dd4befaa3c9b";
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
const blockModuleFile = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.esm", "blockDiagram-T2JWLICG.mjs",
);
const blockMapFile = `${blockModuleFile}.map`;
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
assertEqual(sha256(fs.readFileSync(blockModuleFile)), EXPECTED_BLOCK_MODULE_SHA256, "Block module");
assertEqual(sha256(fs.readFileSync(blockMapFile)), EXPECTED_BLOCK_MAP_SHA256, "Block source map");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome binary");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans font");

const init = (config, body) => `%%{init:${JSON.stringify(config)}}%%\n${body}`;
const frontmatter = (body) => `---
title: Front title
config:
  fontFamily: Noto Sans
  block:
    padding: 18
---
${body}`;

const canonical = `block-beta
columns 3
frontend["Frontend"] backend("Backend") db[("Database")]
frontend --> backend
backend --> db`;

const grammarCases = [
  ["canonical", canonical],
  ["non-beta", "block\na"],
  ["header-only", "block-beta"],
  ["header-newline", "block-beta\n"],
  ["uppercase-header", "BLOCK-BETA\na"],
  ["prefix", "block-betaX\na"],
  ["same-line", "block-beta a"],
  ["leading-blank", "\n\nblock-beta\na"],
  ["comment", "%% before\nblock-beta\n%% body\na"],
  ["directive", init({ block: { padding: 12 } }, "block-beta\na")],
  ["frontmatter", frontmatter("block-beta\na")],
  ["acc-title-rejected", "block-beta\naccTitle: Accessible\na"],
  ["acc-descr-rejected", "block-beta\naccDescr: Description\na"],
  ["title-is-node", "block-beta\ntitle Inline"],
  ["plain-node", "block-beta\na"],
  ["quoted-square", "block-beta\na[\"Square\"]"],
  ["markdown-square", "block-beta\na[\"`**Bold** label`\"]"],
  ["round", "block-beta\na(\"Round\")"],
  ["circle", "block-beta\na((\"Circle\"))"],
  ["double-circle", "block-beta\na(((\"Double\")))"],
  ["diamond", "block-beta\na{\"Diamond\"}"],
  ["hexagon", "block-beta\na{{\"Hexagon\"}}"],
  ["stadium", "block-beta\na([\"Stadium\"])"],
  ["subroutine", "block-beta\na[[\"Subroutine\"]]"],
  ["cylinder", "block-beta\na[(\"Cylinder\")]"],
  ["lean-right", "block-beta\na[/\"Right\"/]"],
  ["lean-left", "block-beta\na[\\\"Left\"\\]"],
  ["trapezoid", "block-beta\na[/\"Trap\"\\]"],
  ["inv-trapezoid", "block-beta\na[\\\"Inv\"/]"],
  ["odd", "block-beta\na>\"Odd\"]"],
  ["block-arrow", "block-beta\na<[\"Arrow\"]>(right, down)"],
  ["columns", "block-beta\ncolumns 2\na b c"],
  ["columns-auto", "block-beta\ncolumns auto\na b c"],
  ["columns-zero", "block-beta\ncolumns 0\na"],
  ["node-width", "block-beta\ncolumns 3\na[\"Wide\"]:2 b"],
  ["space-one", "block-beta\ncolumns 3\na space b"],
  ["space-many", "block-beta\ncolumns 4\na space:2 b"],
  ["composite", "block-beta\nblock:g\n columns 1\n a[\"A\"]\n b[\"B\"]\nend"],
  ["anonymous-composite", "block-beta\nblock\n a\n b\nend"],
  ["nested-composite", "block-beta\nblock:outer\n block:inner\n  a\n end\n b\nend"],
  ["missing-end", "block-beta\nblock:g\na"],
  ["extra-end", "block-beta\na\nend"],
  ["edge-solid", "block-beta\na --> b"],
  ["edge-open", "block-beta\na --- b"],
  ["edge-thick", "block-beta\na ==> b"],
  ["edge-dotted", "block-beta\na -.-> b"],
  ["edge-cross", "block-beta\na --x b"],
  ["edge-circle", "block-beta\na --o b"],
  ["edge-bidirectional", "block-beta\na <--> b"],
  ["edge-label", "block-beta\na -- \"flow\" --> b"],
  ["edge-markdown-label", "block-beta\na -- \"`**flow**`\" --> b"],
  ["style", "block-beta\na\nstyle a fill:#f00,stroke:#0f0,color:#00f"],
  ["class-def", "block-beta\nclassDef hot fill:#f00,color:#fff\na\nclass a hot"],
  ["class-before-node", "block-beta\nclass a hot\nclassDef hot fill:#f00\na"],
  ["style-missing-node", "block-beta\nstyle missing fill:#f00"],
  ["duplicate-node", "block-beta\na[\"First\"] a[\"Second\"]"],
  ["invalid-bare-label", "block-beta\na[Bare label]"],
  ["unterminated-label", "block-beta\na[\"Open]"],
  ["semicolon", "block-beta\na;"],
  ["crlf", "block-beta\r\ncolumns 2\r\na b\r\n"],
].map(([id, source]) => ({ id, source }));

const allShapes = `block-beta
columns 4
r["Rect"] ro("Round") c(("Circle")) d{"Diamond"}
h{{"Hex"}} st(["Stadium"]) sub[["Sub"]] cy[("Cylinder")]
lr[/"Lean R"/] ll[\\"Lean L"\\] tb[/"Trap"\\] tt[\\"Inv"/]
odd>"Odd"] dc((("Double"))) ba<["Arrow"]>(right,down)`;
const nested = `block-beta
columns 2
block:group["Group"]
 columns 2
 a["A"] b("B")
 block:inner["Inner"]:2
  columns 1
  c[("C")]
  d{{"D"}}
 end
end
e["Outside"]`;
const edges = `block-beta
columns 3
a["A"] b["B"] c["C"]
a --> b
b -. "dotted" .-> c
a <== "thick" ==> c`;
const geometryCases = [
  ["single", "block-beta\na[\"A\"]", {}],
  ["columns", "block-beta\ncolumns 3\na[\"A\"] b[\"Longer B\"] c[\"C\"] d[\"D\"]", {}],
  ["columns-auto", "block-beta\ncolumns auto\na[\"A\"] b[\"B\"] c[\"C\"]", {}],
  ["width-space", "block-beta\ncolumns 4\na[\"Wide\"]:2 space b[\"B\"]\nc[\"C\"] space:2 d[\"D\"]", {}],
  ["nested", nested, {}],
  ["all-shapes", allShapes, {}],
  ["edges", edges, {}],
  ["styles", "block-beta\ncolumns 2\na[\"A\"] b[\"B\"]\nclassDef hot fill:#f00,stroke:#0f0,color:#fff\nclass a hot\nstyle b fill:#00f,stroke-width:4px", {}],
  ["long-label", "block-beta\na[\"A long block label with spaces\"]", {}],
  ["padding-20", canonical, { block: { padding: 20 } }],
  ["fixed-width", canonical, { block: { useMaxWidth: false } }],
  ["html-false", canonical, { htmlLabels: false }],
].map(([id, body, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", ...config }, body),
}));

const themes = [
  "base", "dark", "default", "forest", "neutral", "neo", "neo-dark",
  "redux", "redux-dark", "redux-color", "redux-dark-color",
];
const configCases = [
  ["defaults", {}],
  ["use-max-width", { block: { useMaxWidth: false } }],
  ["padding", { block: { padding: 18 } }],
  ["padding-zero", { block: { padding: 0 } }],
  ["padding-string", { block: { padding: "18" } }],
  ["padding-null", { block: { padding: null } }],
  ["padding-array", { block: { padding: [18] } }],
  ["use-max-string", { block: { useMaxWidth: "false" } }],
  ["top-level-padding", { padding: 18 }],
  ["html-false", { htmlLabels: false }],
  ["look-neo", { look: "neo" }],
  ["look-hand", { look: "handDrawn", handDrawnSeed: 7 }],
  ["font-family", { fontFamily: "Courier New" }],
  ["frontmatter", null],
  ...themes.map((theme) => [`theme-${theme}`, { theme }]),
  ["theme-node", { themeVariables: { mainBkg: "#123456", nodeBorder: "#654321", nodeTextColor: "#abcdef" } }],
  ["theme-edge", { themeVariables: { lineColor: "#112233", arrowheadColor: "#334455", edgeLabelBackground: "#ffeedd" } }],
  ["theme-cluster", { themeVariables: { clusterBkg: "#224466", clusterBorder: "#6688aa", titleColor: "#aabbcc" } }],
].map(([id, config]) => ({
  id,
  source: id === "frontmatter"
    ? frontmatter(canonical)
    : init({ fontFamily: "Noto Sans", ...config }, canonical),
}));
configCases.push(
  {
    id: "padding-string-rect",
    source: init({ fontFamily: "Noto Sans", block: { padding: "18" } },
      'block-beta\nfrontend["Frontend"]'),
  },
  {
    id: "padding-string-round",
    source: init({ fontFamily: "Noto Sans", block: { padding: "18" } },
      'block-beta\nbackend("Backend")'),
  },
  {
    id: "padding-string-cylinder",
    source: init({ fontFamily: "Noto Sans", block: { padding: "18" } },
      'block-beta\ndb[("Database")]'),
  },
);

const pixelCases = [
  ["default", canonical, {}],
  ["dark", canonical, { theme: "dark" }],
  ["forest", canonical, { theme: "forest" }],
  ["nested", nested, {}],
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
  package: "mermaid",
  version: EXPECTED_VERSION,
  moduleSha256: EXPECTED_MODULE_SHA256,
  blockModuleSha256: EXPECTED_BLOCK_MODULE_SHA256,
  blockMapSha256: EXPECTED_BLOCK_MAP_SHA256,
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
    const result = await mermaid.render(`block-${id}`, source);
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
        text: element.textContent ?? "",
        attrs: attrs(element, [
          "id", "class", "x", "y", "cx", "cy", "r", "width", "height",
          "rx", "ry", "d", "points", "transform", "fill", "stroke",
          "stroke-width", "stroke-dasharray", "marker-start", "marker-end",
          "font-size", "font-weight", "text-anchor", "dominant-baseline",
        ]),
        bbox: box(element),
        computed: {
          fill: style.fill,
          stroke: style.stroke,
          strokeWidth: style.strokeWidth,
          strokeDasharray: style.strokeDasharray,
          fontFamily: style.fontFamily,
          fontSize: style.fontSize,
          fontWeight: style.fontWeight,
          color: style.color,
        },
      };
    };
    const normalizeIds = (() => {
      const ids = new Map();
      return (value) => {
        if (typeof value !== "string") return value;
        return value.replace(/id-[a-z0-9]+-\d+/g, (raw) => {
          if (!ids.has(raw)) ids.set(raw, `generated-${ids.size + 1}`);
          return ids.get(raw);
        });
      };
    })();
    const cleanBlock = (block) => ({
      id: normalizeIds(block.id),
      type: block.type,
      label: normalizeIds(block.label ?? null),
      width: block.width ?? null,
      widthInColumns: block.widthInColumns ?? null,
      columns: block.columns ?? null,
      directions: block.directions ?? null,
      classes: block.classes ?? [],
      styles: block.styles ?? [],
      children: (block.children ?? []).map(cleanBlock),
    });
    const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
    const db = diagram.db;
    return {
      db: {
        blocks: db.getBlocks().map(cleanBlock),
        flat: db.getBlocksFlat().map(cleanBlock),
        edges: db.getEdges().map((edge) => ({
          id: normalizeIds(edge.id), start: edge.start, end: edge.end,
          label: edge.label ?? "", thickness: edge.thickness,
          pattern: edge.pattern, arrowTypeStart: edge.arrowTypeStart,
          arrowTypeEnd: edge.arrowTypeEnd,
        })),
        columns: db.getColumns("root"),
        classes: [...db.getClasses().values()].map((value) => ({ ...value })),
      },
      root: {
        attrs: attrs(svg, ["id", "class", "viewBox", "width", "height", "style", "role", "aria-roledescription", "aria-labelledby"]),
        bbox: box(svg),
        client: { width: svg.getBoundingClientRect().width, height: svg.getBoundingClientRect().height },
        order: [...svg.children].map((child) => child.getAttribute("class") || child.tagName),
      },
      groups: [...svg.querySelectorAll("g.node,g.edgePath,g.edgeLabel")].map((group) => ({
        id: group.id,
        class: group.getAttribute("class") ?? "",
        transform: group.getAttribute("transform"),
        bbox: box(group),
        children: [...group.children].map((child) => child.tagName),
      })),
      primitives: [...svg.querySelectorAll("rect,line,circle,ellipse,path,polygon,text,foreignObject")].map(primitive),
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
        const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
        const dbValue = diagram.db;
        const generated = new Map();
        const normalizeId = (value) => {
          if (typeof value !== "string") return value;
          return value.replace(/id-[a-z0-9]+-\d+/g, (raw) => {
            if (!generated.has(raw)) generated.set(raw, `generated-${generated.size + 1}`);
            return generated.get(raw);
          });
        };
        const clean = (block) => ({
          id: normalizeId(block.id), type: block.type,
          label: normalizeId(block.label ?? null),
          width: block.width ?? null,
          widthInColumns: block.widthInColumns ?? null,
          columns: block.columns ?? null,
          directions: block.directions ?? null,
          classes: block.classes ?? [], styles: block.styles ?? [],
          children: (block.children ?? []).map(clean),
        });
        const db = {
          blocks: dbValue.getBlocks().map(clean),
          flat: dbValue.getBlocksFlat().map(clean),
          edges: dbValue.getEdges().map((edge) => ({
            id: normalizeId(edge.id), start: edge.start, end: edge.end,
            label: edge.label ?? "", thickness: edge.thickness,
            pattern: edge.pattern, arrowTypeStart: edge.arrowTypeStart,
            arrowTypeEnd: edge.arrowTypeEnd,
          })),
          columns: dbValue.getColumns("root"),
          classes: [...dbValue.getClasses().values()].map((value) => ({ ...value })),
        };
        let render = true;
        let error;
        try { await mermaid.render("grammar-block", source); }
        catch (cause) {
          render = false;
          error = { name: cause?.name ?? "Error", message: String(cause?.message ?? cause) };
        }
        return { parse: true, render, db, error };
      } catch (cause) {
        const hash = cause?.hash ?? {};
        return {
          parse: false,
          render: false,
          error: {
            name: cause?.name ?? "Error",
            message: String(cause?.message ?? cause),
            kind: String(cause?.message ?? "").startsWith("Lexical error") ? "Lexer" : "Parser",
            line: Number(hash?.loc?.first_line ?? hash?.line ?? 0) + (hash?.loc ? 0 : 1),
            column: Number(hash?.loc?.first_column ?? 0) + 1,
            token: String(hash?.text ?? ""),
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

  const pixelDir = path.join(fixtureDir, "block-pixel");
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
  writeJson(path.join(fixtureDir, "block-grammar.json"), { upstream: provenance, cases: grammar });
  writeJson(path.join(fixtureDir, "block-geometry.json"), { upstream: provenance, cases: geometry });
  writeJson(path.join(fixtureDir, "block-config.json"), { upstream: provenance, cases: config });
  writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixelManifest });
  console.log(`Generated Block fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixelManifest.length} pixel.`);
} finally {
  await browser.close();
}
