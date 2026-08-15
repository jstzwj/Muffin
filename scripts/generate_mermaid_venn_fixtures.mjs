import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Venn source-entry oracle.

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_VENN_MODULE_SHA256 =
  "382a5b11e80ff80f1481f88c113cc036879d160f7fcd1a029d0f4a08b7548eb7";
const EXPECTED_ENGINE_SHA256 =
  "47e73982c0cf8a4e692c57a06656fc928aeced63267d9a1e6f48122c4127da30";
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
const vennModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.core",
  "vennDiagram-L72KCM5P.mjs",
);
const engineFile = path.join(
  path.dirname(mermaidRoot),
  "@upsetjs",
  "venn.js",
  "build",
  "venn.esm.js",
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
assertEqual(sha256(fs.readFileSync(vennModuleFile)), EXPECTED_VENN_MODULE_SHA256, "Venn module");
assertEqual(sha256(fs.readFileSync(engineFile)), EXPECTED_ENGINE_SHA256, "Venn engine");
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

const canonicalBody = `venn-beta
title Product fit
set Users["Users"]: 12
set Needs["Needs"]: 10
set Product["Product"]: 9
union Users,Needs["Demand"]: 4
union Needs,Product["Solution"]: 3
union Users,Product: 2
union Users,Needs,Product["Fit"]: 1
text Users u1["Research"]
text Users u2["Feedback"]
text Users,Needs shared["Pain points"]
style Users fill:#4e79a7,stroke:#25476b,stroke-width:4,color:#ffffff,fill-opacity:0.18
style Users,Needs fill:rgba(255,0,0,0.22),color:#222222`;

const grammarCases = [
  { id: "header-only", source: "venn-beta" },
  { id: "header-newline", source: "venn-beta\n" },
  { id: "canonical", source: canonicalBody },
  { id: "uppercase-header", source: "VENN-BETA\nset A" },
  { id: "mixed-header", source: "Venn-Beta\nset A" },
  { id: "prefix", source: "venn-betaX\nset A" },
  { id: "non-beta", source: "venn\nset A" },
  { id: "same-line", source: "venn-beta set A" },
  { id: "set-minimal", source: "venn-beta\nset A" },
  { id: "set-quoted-id", source: "venn-beta\nset \"A one\"" },
  { id: "set-label", source: "venn-beta\nset A[Alpha]" },
  { id: "set-quoted-label", source: "venn-beta\nset A[\"Alpha one\"]" },
  { id: "set-empty-label", source: "venn-beta\nset A[\"\"]" },
  { id: "set-size", source: "venn-beta\nset A: 12" },
  { id: "set-label-size", source: "venn-beta\nset A[Alpha]: 12.5" },
  { id: "signed-sizes", source: "venn-beta\nset A: -2\nset B: +.5" },
  { id: "size-exponent", source: "venn-beta\nset A: 1e3" },
  { id: "size-trailing-dot", source: "venn-beta\nset A: 1." },
  { id: "union-minimal", source: "venn-beta\nset A\nset B\nunion A,B" },
  { id: "union-label-size", source: "venn-beta\nset A\nset B\nunion B,A[Both]: 2" },
  { id: "union-three", source: "venn-beta\nset A\nset B\nset C\nunion A,B,C[All]: 1" },
  { id: "union-one", source: "venn-beta\nset A\nunion A" },
  { id: "union-unknown", source: "venn-beta\nset A\nunion A,B" },
  { id: "union-before-set", source: "venn-beta\nunion A,B\nset A\nset B" },
  { id: "union-spaces", source: "venn-beta\nset A\nset B\nunion A , B" },
  { id: "text-simple", source: "venn-beta\nset A\ntext A note" },
  { id: "text-label", source: "venn-beta\nset A\ntext A note[Label]" },
  { id: "text-union", source: "venn-beta\nset A\nset B\ntext A,B note[Shared]" },
  { id: "text-quoted", source: "venn-beta\nset \"A one\"\ntext \"A one\" \"node one\"[\"Node label\"]" },
  { id: "indented-text", source: "venn-beta\nset A\n  text note[Label]" },
  { id: "indented-bare", source: "venn-beta\nset A\n  note[Label]" },
  { id: "indented-two", source: "venn-beta\nset A\n  first[One]\n  second[Two]" },
  { id: "text-before-set", source: "venn-beta\n  note[Label]" },
  { id: "style-fill", source: "venn-beta\nset A\nstyle A fill:#ff0000" },
  { id: "style-many", source: "venn-beta\nset A\nstyle A fill:rgb(1, 2, 3),stroke-width: 4 px,color:\"red\"" },
  { id: "style-union", source: "venn-beta\nset A\nset B\nstyle B,A fill:rgba(1,2,3,.4)" },
  { id: "style-repeat", source: "venn-beta\nset A\nstyle A fill:red\nstyle A stroke:blue" },
  { id: "style-no-set", source: "venn-beta\nstyle A fill:red" },
  { id: "inline-title", source: "venn-beta\ntitle  Heading with spaces  \nset A" },
  { id: "repeated-title", source: "venn-beta\ntitle One\ntitle Two\nset A" },
  { id: "acc-looking", source: "venn-beta\naccTitle: A\naccDescr: D\nset A" },
  { id: "comments", source: "%% pre\nvenn-beta\n%% comment\nset A %% tail\nset B" },
  { id: "semicolons", source: "venn-beta; set A; set B" },
  { id: "blank-lines", source: "\nvenn-beta\n\nset A\n\nset B\n" },
  { id: "duplicate-set", source: "venn-beta\nset A:10\nset A:20" },
  { id: "duplicate-union", source: "venn-beta\nset A\nset B\nunion A,B:2\nunion A,B:3" },
  { id: "sanitizer", source: "venn-beta\nset A[\"<script>x</script><b>A</b>\"]\ntext A n[\"<img src=x onerror=bad>N\"]" },
  { id: "frontmatter", source: frontmatter(undefined, "venn-beta\nset A", "Front") },
  { id: "directive", source: init({ theme: "dark" }, "venn-beta\nset A") },
  { id: "crlf", source: "venn-beta\r\nset A\r\nset B\r\nunion A,B" },
];

const geometryCases = [
  { id: "one-set", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A[Alpha]: 10") },
  { id: "two-overlap", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A[Alpha]: 10\nset B[Beta]: 10\nunion A,B[Both]: 3") },
  { id: "two-disjoint", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A: 10\nset B: 4\nunion A,B: 0") },
  { id: "contained", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A: 12\nset B: 3\nunion A,B: 3") },
  { id: "three-way", source: init({ fontFamily: "Noto Sans" }, canonicalBody) },
  { id: "synthetic-pairs", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A:12\nset B:11\nset C:10\nunion A,B,C[All]:2") },
  { id: "four-sets", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A:20\nset B:18\nset C:16\nset D:14\nunion A,B:6\nunion A,C:5\nunion A,D:4\nunion B,C:3\nunion B,D:2\nunion C,D:1") },
  { id: "text-grid", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A[Alpha]:10\ntext A n1[First]\ntext A n2[Second]\ntext A n3[Third]\ntext A n4[Fourth]\ntext A n5[A long text node that stays inside its fixed cell]") },
  { id: "intersection-text", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A:10\nset B:10\nunion A,B[Both]:4\ntext A,B one[Shared one]\ntext A,B two[Shared two]") },
  { id: "title", source: init({ fontFamily: "Noto Sans" }, "venn-beta\ntitle Venn title\nset A:10\nset B:8\nunion A,B:2") },
  { id: "styled", source: init({ fontFamily: "Noto Sans" }, canonicalBody) },
  { id: "debug", source: init({ fontFamily: "Noto Sans", venn: { useDebugLayout: true } }, canonicalBody) },
  { id: "width-400", source: init({ fontFamily: "Noto Sans", venn: { width: 400, height: 260, padding: 4 } }, canonicalBody) },
  { id: "zero-size", source: init({ fontFamily: "Noto Sans" }, "venn-beta\nset A:0\nset B:10\nunion A,B:0") },
  { id: "hand-drawn", source: init({ fontFamily: "Noto Sans", look: "handDrawn", handDrawnSeed: 17 }, canonicalBody) },
];

const configCases = [
  { id: "defaults", config: {} },
  { id: "width-400", config: { venn: { width: 400 } } },
  { id: "height-300", config: { venn: { height: 300 } } },
  { id: "padding-zero", config: { venn: { padding: 0 } } },
  { id: "padding-40", config: { venn: { padding: 40 } } },
  { id: "debug", config: { venn: { useDebugLayout: true } } },
  { id: "use-max-false", config: { venn: { useMaxWidth: false } } },
  { id: "width-string", config: { venn: { width: "400" } } },
  { id: "height-string", config: { venn: { height: "300" } } },
  { id: "padding-string", config: { venn: { padding: "20" } } },
  { id: "width-zero", config: { venn: { width: 0 } } },
  { id: "height-zero", config: { venn: { height: 0 } } },
  { id: "padding-negative", config: { venn: { padding: -5 } } },
  { id: "width-null", config: { venn: { width: null } } },
  { id: "width-array", config: { venn: { width: [400] } } },
  { id: "use-max-string", config: { venn: { useMaxWidth: "false" } } },
  { id: "debug-string", config: { venn: { useDebugLayout: "false" } } },
  { id: "theme-dark", config: { theme: "dark" } },
  { id: "theme-forest", config: { theme: "forest" } },
  { id: "theme-neutral", config: { theme: "neutral" } },
  { id: "theme-redux-color", config: { theme: "redux-color" } },
  { id: "font-family", config: { fontFamily: "DefinitelyMissing, Noto Sans" } },
  { id: "look-hand-drawn", config: { look: "handDrawn", handDrawnSeed: 17 } },
  { id: "look-case", config: { look: "HandDrawn", handDrawnSeed: 17 } },
  { id: "venn1", config: { themeVariables: { venn1: "#ff0000" } } },
  { id: "venn2", config: { themeVariables: { venn2: "#00ff00" } } },
  { id: "venn8", config: { themeVariables: { venn8: "#0000ff" } } },
  { id: "title-color", config: { themeVariables: { vennTitleTextColor: "#ff0000" } } },
  { id: "set-text-color", config: { themeVariables: { vennSetTextColor: "#00ff00" } } },
  { id: "background-dark", config: { themeVariables: { background: "#000000" } } },
  { id: "primary-color", config: { themeVariables: { primaryColor: "#ff00ff" } } },
  { id: "style-empty-fill", body: `${canonicalBody}\nstyle Product fill:\"\"` },
  { id: "style-stroke-zero", body: `${canonicalBody}\nstyle Product stroke-width:0` },
  { id: "frontmatter", source: frontmatter({ fontFamily: "Noto Sans", venn: { width: 500, height: 320, padding: 25, useMaxWidth: false } }, canonicalBody, "Front title") },
];

const pixelCases = [
  { id: "default", source: init({ fontFamily: "Noto Sans" }, canonicalBody) },
  { id: "dark", source: init({ fontFamily: "Noto Sans", theme: "dark" }, canonicalBody) },
  { id: "forest", source: init({ fontFamily: "Noto Sans", theme: "forest" }, canonicalBody) },
  { id: "hand-drawn", source: init({ fontFamily: "Noto Sans", look: "handDrawn", handDrawnSeed: 17 }, canonicalBody) },
];

const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js"),
  ).href,
);
const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"],
});

const provenance = {
  package: "mermaid",
  version: EXPECTED_VERSION,
  moduleSha256: EXPECTED_MODULE_SHA256,
  vennModuleSha256: EXPECTED_VENN_MODULE_SHA256,
  vennEngine: "@upsetjs/venn.js@2.0.0",
  vennEngineSha256: EXPECTED_ENGINE_SHA256,
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
      const font = new FontFace("Noto Sans", `url(${fontUrl})`);
      await font.load();
      document.fonts.add(font);
      await document.fonts.load("16px 'Noto Sans'");
      await document.fonts.load("24px 'Noto Sans'");
    }, pathToFileURL(fontFile).href);
  };

  const grammar = [];
  for (const fixture of grammarCases) {
    await prepare();
    const result = await page.evaluate(
      async ({ source, caseId, moduleUrl }) => {
        const clean = source.replace(/^---[\s\S]*?---\s*/, "").replace(/^%%\{[\s\S]*?\}%%\s*/, "");
        try {
          const { default: mermaid } = await import(moduleUrl);
          mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
          await mermaid.parse(source);
          const diagram = await mermaid.mermaidAPI.getDiagramFromText(clean);
          let render;
          try {
            const rendered = await mermaid.render(`grammar-venn-${caseId}`, source);
            document.body.innerHTML = rendered.svg;
            render = {
              accept: true,
              areas: document.querySelectorAll("g.venn-area").length,
              textNodes: document.querySelectorAll(".venn-text-node").length,
            };
          } catch (error) {
            render = {
              accept: false,
              message: String(error?.message ?? error).replace(/\s+/g, " ").trim(),
            };
          }
          return {
            accept: true,
            db: {
              subsets: diagram.db.getSubsetData(),
              textNodes: diagram.db.getTextData(),
              styles: diagram.db.getStyleData(),
              title: diagram.db.getDiagramTitle(),
              accTitle: diagram.db.getAccTitle(),
              accDescr: diagram.db.getAccDescription(),
            },
            rendered: render,
          };
        } catch (error) {
          const raw = String(error?.message ?? error);
          const message = raw.replace(/\s+/g, " ").trim();
          const lineMatch = raw.match(/(?:Parse|Lexical) error on line (\d+)/i);
          const column = error?.hash?.loc?.first_column;
          let kind = "runtime";
          if (message.startsWith("No diagram type detected")) kind = "no-diagram";
          else if (/Lexical error/i.test(message)) kind = "lexer";
          else if (/Parse error/i.test(message)) kind = "parser";
          return {
            accept: false,
            reject: {
              kind,
              message,
              line: Number(lineMatch?.[1] ?? (error?.hash?.line ?? -1) + 1),
              column: Number.isFinite(column) ? column + 1 : 0,
              token: error?.hash?.token ?? "",
            },
          };
        }
      },
      { source: fixture.source, caseId: fixture.id, moduleUrl: pathToFileURL(moduleFile).href },
    );
    grammar.push({ id: fixture.id, source: fixture.source, ...result });
  }

  const capture = async (source, id) => {
    await prepare();
    return page.evaluate(
      async ({ source, id, moduleUrl }) => {
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          deterministicIds: true,
          deterministicIDSeed: "venn-fixture",
        });
        const rendered = await mermaid.render(`venn-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
        const root = document.querySelector("svg");
        const attrs = (element) => element ? Object.fromEntries([...element.attributes].map((attr) => [attr.name, attr.value])) : null;
        const bbox = (element) => {
          if (!element) return null;
          const value = typeof element.getBBox === "function"
            ? element.getBBox()
            : element.getBoundingClientRect();
          return { x: value.x, y: value.y, width: value.width, height: value.height };
        };
        const computed = (element) => {
          if (!element) return null;
          const value = getComputedStyle(element);
          return {
            fill: value.fill,
            fillOpacity: value.fillOpacity,
            stroke: value.stroke,
            strokeOpacity: value.strokeOpacity,
            strokeWidth: value.strokeWidth,
            color: value.color,
            fontFamily: value.fontFamily,
            fontSize: value.fontSize,
            textAnchor: value.textAnchor,
            dominantBaseline: value.dominantBaseline,
          };
        };
        const client = root.getBoundingClientRect();
        const project = (element) => ({
          tag: element.tagName.toLowerCase(),
          attrs: attrs(element),
          bbox: bbox(element),
          computed: computed(element),
          text: element.textContent ?? "",
        });
        return {
          root: {
            attrs: attrs(root),
            bbox: bbox(root),
            client: { width: client.width, height: client.height },
            order: [...root.children].map((child) => `${child.tagName.toLowerCase()}.${child.getAttribute("class") ?? ""}`),
          },
          title: root.querySelector(".venn-title") ? project(root.querySelector(".venn-title")) : null,
          areas: [...root.querySelectorAll("g.venn-area")].map((group) => ({
            ...project(group),
            order: [...group.children].map((child) => child.tagName.toLowerCase()),
            path: group.querySelector(":scope > path") ? project(group.querySelector(":scope > path")) : null,
            textElement: group.querySelector(":scope > text") ? {
              ...project(group.querySelector(":scope > text")),
              tspans: [...group.querySelectorAll(":scope > text > tspan")].map(project),
            } : null,
            roughPaths: [...group.querySelectorAll(":scope > g path")].map(project),
          })),
          textAreas: [...root.querySelectorAll("g.venn-text-area")].map((group) => ({
            ...project(group),
            order: [...group.children].map((child) => `${child.tagName.toLowerCase()}.${child.getAttribute("class") ?? ""}`),
          })),
          foreignObjects: [...root.querySelectorAll("foreignObject.venn-text-node-fo")].map((item) => ({
            ...project(item),
            span: item.querySelector("span") ? project(item.querySelector("span")) : null,
          })),
          debugCircles: [...root.querySelectorAll(".venn-text-debug-circle")].map(project),
          debugCells: [...root.querySelectorAll(".venn-text-debug-cell")].map(project),
        };
      },
      { source, id, moduleUrl: pathToFileURL(moduleFile).href },
    );
  };

  const geometry = [];
  for (const fixture of geometryCases) geometry.push({ ...fixture, expected: await capture(fixture.source, fixture.id) });

  const config = [];
  for (const fixture of configCases) {
    const source = fixture.source ?? init({ fontFamily: "Noto Sans", ...(fixture.config ?? {}) }, fixture.body ?? canonicalBody);
    try {
      config.push({ id: fixture.id, source, accept: true, expected: await capture(source, `config-${fixture.id}`) });
    } catch (error) {
      config.push({ id: fixture.id, source, accept: false, message: String(error?.message ?? error).replace(/\s+/g, " ").trim() });
    }
  }

  const pixelDir = path.join(fixtureDir, "venn-pixel");
  fs.mkdirSync(pixelDir, { recursive: true });
  const pixel = [];
  for (const fixture of pixelCases) {
    await prepare();
    const selector = `#venn-pixel-${fixture.id}`;
    await page.evaluate(
      async ({ source, id, moduleUrl }) => {
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({ startOnLoad: false, securityLevel: "strict", deterministicIds: true, deterministicIDSeed: "venn-pixel" });
        const rendered = await mermaid.render(`venn-pixel-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
      },
      { source: fixture.source, id: fixture.id, moduleUrl: pathToFileURL(moduleFile).href },
    );
    const element = await page.$(selector);
    if (!element) throw new Error(`Missing pixel SVG ${selector}`);
    const image = await element.screenshot({ omitBackground: true });
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), image);
    pixel.push({ id: fixture.id, source: fixture.source, file, sha256: sha256(image) });
  }

  writeJson(path.join(fixtureDir, "venn-grammar.json"), { upstream: provenance, cases: grammar });
  writeJson(path.join(fixtureDir, "venn-geometry.json"), { upstream: provenance, cases: geometry });
  writeJson(path.join(fixtureDir, "venn-config.json"), { upstream: provenance, cases: config });
  writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixel });

  const accepts = grammar.filter((entry) => entry.accept).length;
  console.log(`Venn fixtures: grammar ${grammar.length} (${accepts} accept), geometry ${geometry.length}, config ${config.length}, pixel ${pixel.length}`);
} finally {
  await browser.close();
}
