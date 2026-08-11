import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Treemap source-entry oracle.
const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_TREEMAP_MODULE_SHA256 =
  "ba9be795c170a8ee77d1183f6554aa48c3090f9c116e901c2ee675770642ddc3";
const EXPECTED_PARSER_MODULE_SHA256 =
  "c3d546a632223958abd37439b23ca6c586e2c6f43306619ea9a6843cdc993e10";
const EXPECTED_ENGINE_SHA256 =
  "4409a95e3886ae711cbc66c6c45dc1015372abc89297c3a3f084bef3750c48c9";
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
const treemapModuleFile = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.core", "diagram-G47NLZAW.mjs",
);
const parserModuleFile = path.join(
  path.dirname(mermaidRoot), "@mermaid-js", "parser", "dist", "chunks",
  "mermaid-parser.core", "chunk-R7FJI6CG.mjs",
);
const engineRoot = path.join(path.dirname(mermaidRoot), "d3-hierarchy", "src");
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assertEqual = (actual, expected, label) => {
  if (actual !== expected) throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const walk = (directory) => fs.readdirSync(directory, { withFileTypes: true })
  .flatMap((entry) => entry.isDirectory()
    ? walk(path.join(directory, entry.name))
    : [path.join(directory, entry.name)]);
const enginePayload = walk(engineRoot).sort().map((file) =>
  `${path.relative(engineRoot, file).replaceAll("\\", "/")}\n${fs.readFileSync(file, "utf8")}`,
).join("\n");
const writeJson = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assertEqual(pkg.version, EXPECTED_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MODULE_SHA256, "Mermaid module");
assertEqual(sha256(fs.readFileSync(treemapModuleFile)), EXPECTED_TREEMAP_MODULE_SHA256, "Treemap module");
assertEqual(sha256(fs.readFileSync(parserModuleFile)), EXPECTED_PARSER_MODULE_SHA256, "Treemap parser module");
assertEqual(sha256(enginePayload), EXPECTED_ENGINE_SHA256, "d3-hierarchy engine");
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
const canonical = `treemap-beta
"Portfolio"
  "Products"
    "Alpha": 35
    "Beta": 20
  "Services"
    "Consulting": 25
    "Support": 20`;

const grammarCases = [
  ["canonical", canonical],
  ["non-beta", canonical.replace("treemap-beta", "treemap")],
  ["uppercase", canonical.replace("treemap-beta", "TREEMAP-BETA")],
  ["prefix", "treemap-betaX\n\"Root\""],
  ["header-only", "treemap-beta"],
  ["header-newline", "treemap-beta\n"],
  ["single-section", "treemap-beta\n\"Root\""],
  ["single-leaf", "treemap-beta\n\"A\": 10"],
  ["same-line", "treemap-beta \"A\": 10"],
  ["unquoted", "treemap-beta\nRoot\n  A: 10"],
  ["single-quotes", "treemap-beta\n'Root'\n  'A': 10"],
  ["empty-name", "treemap-beta\n\"\"\n  \"A\": 10"],
  ["quoted-newline", "treemap-beta\n\"A\nB\": 10"],
  ["escaped-quote", "treemap-beta\n\"A\\\"B\": 10"],
  ["decimal", "treemap-beta\n\"A\": .5"],
  ["comma-number", "treemap-beta\n\"A\": 1,234.5"],
  ["underscore-number", "treemap-beta\n\"A\": 1_000"],
  ["signed-number", "treemap-beta\n\"A\": -2"],
  ["exponent-number", "treemap-beta\n\"A\": 1e2"],
  ["malformed-number", "treemap-beta\n\"A\": ..."],
  ["missing-value", "treemap-beta\n\"A\":"],
  ["multiple-roots", "treemap-beta\n\"A\": 1\n\"B\": 2"],
  ["irregular-indent", "treemap-beta\n\"Root\"\n   \"A\"\n \"B\": 2"],
  ["tabs", "treemap-beta\n\"Root\"\n\t\"A\": 2"],
  ["class-section", "treemap-beta\n\"Root\":::group\n  \"A\": 2\nclassDef group fill:#f00,color:#fff;"],
  ["class-leaf-before", "treemap-beta\nclassDef hot fill:#f00,color:#fff;\n\"A\": 2:::hot"],
  ["class-leaf-after", "treemap-beta\n\"A\": 2:::hot\nclassDef hot fill:#f00,color:#fff;"],
  ["bad-class-name", "treemap-beta\nclassDef 3x fill:#f00;\n\"A\":2"],
  ["title", "treemap-beta\ntitle Inline Heading\n\"A\": 2"],
  ["metadata", "treemap-beta\naccTitle: AT\naccDescr: AD\n\"A\": 2"],
  ["acc-block", "treemap-beta\naccDescr {first\n  second}\n\"A\": 2"],
  ["comment-before", "%% comment\ntreemap-beta\n\"A\": 2"],
  ["comment-body", "treemap-beta\n%% comment\n\"A\": 2"],
  ["blank-lines", "\n treemap-beta\n\n \"Root\"\n\n   \"A\": 2\n"],
  ["crlf", "treemap-beta\r\n\"Root\"\r\n  \"A\": 2"],
  ["semicolon", "treemap-beta\n\"A\": 2;"],
  ["literal-markup", "treemap-beta\n\"<script>x</script><b>A</b>\": 2"],
  ["frontmatter", frontmatter({ treemap: { showValues: false } }, "treemap-beta\n\"A\":2", "Front")],
  ["directive", init({ treemap: { padding: 3 } }, "treemap-beta\n\"A\":2")],
].map(([id, source]) => ({ id, source }));

const manyLeaves = Array.from({ length: 22 }, (_, index) =>
  `  "Leaf ${index + 1}": ${index + 1}`).join("\n");
const geometryCases = [
  ["canonical", canonical, {}],
  ["flat", "treemap-beta\n\"Root\"\n  \"A\": 50\n  \"B\": 30\n  \"C\": 20", {}],
  ["nested", "treemap-beta\n\"Root\"\n  \"A\"\n    \"A1\": 7\n    \"A2\": 3\n  \"B\"\n    \"B1\": 6\n    \"B2\": 4", {}],
  ["multiple-roots", "treemap-beta\n\"A\": 10\n\"B\": 20", {}],
  ["zero-values", "treemap-beta\n\"Root\"\n  \"Zero\": 0\n  \"One\": 1\n  \"Also zero\": 0", {}],
  ["decimal-values", "treemap-beta\n\"Root\"\n  \"A\": 1234.5\n  \"B\": 67.89", {}],
  ["no-values", canonical, { treemap: { showValues: false } }],
  ["padding-zero", canonical, { treemap: { padding: 0 } }],
  ["padding-twenty", canonical, { treemap: { padding: 20 } }],
  ["custom-size", canonical, { treemap: { nodeWidth: 48, nodeHeight: 28 } }],
  ["title", `treemap-beta\ntitle Revenue Map\n${canonical.split("\n").slice(1).join("\n")}`, {}],
  ["long-section", "treemap-beta\n\"Root\"\n  \"A section heading that must be clipped\"\n    \"A\": 1\n  \"B\"\n    \"B\": 9", {}],
  ["tiny-cells", "treemap-beta\n\"Root\"\n  \"Huge\": 999\n  \"Tiny A\": 1\n  \"Tiny B\": 1", {}],
  ["complex", `treemap-beta\n\"Root\"\n${manyLeaves}`, {}],
  ["class-styles", "treemap-beta\nclassDef hot fill:#ff0000,stroke:#00ff00,color:#0000ff,stroke-width:7px;\n\"Root\"\n  \"A\": 6:::hot\n  \"B\": 4", {}],
  ["format-currency", canonical, { treemap: { valueFormat: "$,.2f" } }],
].map(([id, body, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", ...config }, body),
}));

const configCases = [
  ["defaults", {}],
  ["use-max-false", { treemap: { useMaxWidth: false } }],
  ["use-width", { treemap: { useWidth: 500 } }],
  ["padding", { treemap: { padding: 3 } }],
  ["padding-string", { treemap: { padding: "3" } }],
  ["padding-zero", { treemap: { padding: 0 } }],
  ["diagram-padding", { treemap: { diagramPadding: 30 } }],
  ["diagram-padding-zero", { treemap: { diagramPadding: 0 } }],
  ["show-values-false", { treemap: { showValues: false } }],
  ["show-values-string", { treemap: { showValues: "false" } }],
  ["node-width", { treemap: { nodeWidth: 48 } }],
  ["node-height", { treemap: { nodeHeight: 28 } }],
  ["node-width-string", { treemap: { nodeWidth: "48" } }],
  ["node-height-string", { treemap: { nodeHeight: "28" } }],
  ["border-width", { treemap: { borderWidth: 9 } }],
  ["label-font-size", { treemap: { labelFontSize: 25 } }],
  ["value-font-size", { treemap: { valueFontSize: 22 } }],
  ["format-comma", { treemap: { valueFormat: "," } }],
  ["format-currency", { treemap: { valueFormat: "$0,0" } }],
  ["format-currency-decimal", { treemap: { valueFormat: "$,.2f" } }],
  ["format-percent", { treemap: { valueFormat: ".1%" } }],
  ["format-invalid", { treemap: { valueFormat: "[" } }],
  ["theme-dark", { theme: "dark" }],
  ["theme-forest", { theme: "forest" }],
  ["theme-redux-color", { theme: "redux-color" }],
  ["font-family", { fontFamily: "DefinitelyMissing, Noto Sans" }],
  ["cscale", { themeVariables: { cScale1: "#ff0000", cScalePeer1: "#00ff00", cScaleLabel1: "#0000ff" } }],
  ["style-label-color", { themeVariables: { treemap: { labelColor: "#ff0000" } } }],
  ["style-title-color", { themeVariables: { treemap: { titleColor: "#00ff00" } } }],
  ["style-value-color", { themeVariables: { treemap: { valueColor: "#0000ff" } } }],
  ["style-label-size", { themeVariables: { treemap: { labelFontSize: "20px" } } }],
  ["style-value-size", { themeVariables: { treemap: { valueFontSize: "20px" } } }],
  ["style-title-size", { themeVariables: { treemap: { titleFontSize: "24px" } } }],
  ["style-section-fill", { themeVariables: { treemap: { sectionFillColor: "#ff0000" } } }],
  ["style-leaf-fill", { themeVariables: { treemap: { leafFillColor: "#00ff00" } } }],
  ["frontmatter", null],
].map(([id, config]) => ({
  id,
  source: id === "frontmatter"
    ? frontmatter({ fontFamily: "Noto Sans", treemap: { padding: 4, nodeWidth: 50, nodeHeight: 30, showValues: false, useMaxWidth: false } }, canonical, "Front")
    : init(
      { fontFamily: "Noto Sans", ...config },
      id === "style-title-color" || id === "style-title-size"
        ? `treemap-beta\ntitle Revenue Map\n${canonical.split("\n").slice(1).join("\n")}`
        : canonical,
    ),
}));

const pixelCases = [
  ["default", {}],
  ["dark", { theme: "dark" }],
  ["forest", { theme: "forest" }],
  ["class-styles", {}],
].map(([id, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", ...config },
    id === "class-styles" ? geometryCases.find((item) => item.id === id).source.replace(/^%%\{init:[^\n]+\}%%\n/, "") : canonical),
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
  treemapModuleSha256: EXPECTED_TREEMAP_MODULE_SHA256,
  parserModuleSha256: EXPECTED_PARSER_MODULE_SHA256,
  engine: "d3-hierarchy@3.1.2",
  engineSha256: EXPECTED_ENGINE_SHA256,
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
      await document.fonts.load("38px 'Noto Sans'");
    }, pathToFileURL(fontFile).href);
  };
  const render = async (source, id) => page.evaluate(async ({ source, id, moduleUrl }) => {
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const result = await mermaid.render(`treemap-${id}`, source);
    document.body.innerHTML = result.svg;
    const svg = document.querySelector("svg");
    const attrs = (element, names) => Object.fromEntries(
      names.map((name) => [name, element?.getAttribute(name) ?? null]),
    );
    const box = (element) => {
      const value = element.getBBox();
      return { x: value.x, y: value.y, width: value.width, height: value.height };
    };
    const computed = (element) => {
      const style = getComputedStyle(element);
      return {
        display: style.display,
        fill: style.fill,
        fillOpacity: style.fillOpacity,
        stroke: style.stroke,
        strokeWidth: style.strokeWidth,
        strokeOpacity: style.strokeOpacity,
        fontFamily: style.fontFamily,
        fontSize: style.fontSize,
        fontStyle: style.fontStyle,
        fontWeight: style.fontWeight,
        textAnchor: style.textAnchor,
      };
    };
    const text = (element) => ({
      text: element?.textContent ?? "",
      attrs: attrs(element, ["class", "x", "y", "clip-path", "style"]),
      bbox: box(element),
      computed: computed(element),
    });
    const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
    const dbNodes = diagram.db.getNodes().map((node) => ({
      name: node.name,
      value: node.value,
      classSelector: node.classSelector,
      cssCompiledStyles: node.cssCompiledStyles,
      children: node.children?.map((child) => child.name),
    }));
    return {
      db: {
        title: diagram.db.getDiagramTitle(),
        accTitle: diagram.db.getAccTitle(),
        accDescr: diagram.db.getAccDescription(),
        nodes: dbNodes,
        root: diagram.db.getRoot(),
        classes: [...diagram.db.getClasses().entries()],
      },
      root: {
        attrs: attrs(svg, ["id", "class", "viewBox", "width", "height", "style", "role", "aria-roledescription", "aria-labelledby"]),
        bbox: box(svg),
        client: { width: svg.getBoundingClientRect().width, height: svg.getBoundingClientRect().height },
        order: [...svg.children].map((child) => child.getAttribute("class") || child.tagName),
      },
      title: svg.querySelector(":scope > text.treemapTitle")
        ? text(svg.querySelector(":scope > text.treemapTitle")) : null,
      container: attrs(svg.querySelector(":scope > g.treemapContainer"), ["class", "transform"]),
      sections: [...svg.querySelectorAll("g.treemapSection")].map((group) => ({
        attrs: attrs(group, ["class", "transform"]),
        bbox: box(group),
        header: { attrs: attrs(group.children[0], ["width", "height", "class", "fill", "fill-opacity", "stroke-width", "style"]), bbox: box(group.children[0]), computed: computed(group.children[0]) },
        clip: { attrs: attrs(group.querySelector("clipPath"), ["id"]), rect: attrs(group.querySelector("clipPath rect"), ["width", "height"]) },
        rect: { attrs: attrs(group.querySelector("rect.treemapSection"), ["width", "height", "class", "fill", "fill-opacity", "stroke", "stroke-width", "stroke-opacity", "style"]), bbox: box(group.querySelector("rect.treemapSection")), computed: computed(group.querySelector("rect.treemapSection")) },
        label: text(group.querySelector("text.treemapSectionLabel")),
        value: group.querySelector("text.treemapSectionValue") ? text(group.querySelector("text.treemapSectionValue")) : null,
      })),
      leaves: [...svg.querySelectorAll("g.treemapLeafGroup")].map((group) => ({
        attrs: attrs(group, ["class", "transform"]),
        bbox: box(group),
        rect: { attrs: attrs(group.querySelector("rect.treemapLeaf"), ["width", "height", "class", "fill", "fill-opacity", "stroke", "stroke-width", "style"]), bbox: box(group.querySelector("rect.treemapLeaf")), computed: computed(group.querySelector("rect.treemapLeaf")) },
        clip: { attrs: attrs(group.querySelector("clipPath"), ["id"]), rect: attrs(group.querySelector("clipPath rect"), ["width", "height"]) },
        label: text(group.querySelector("text.treemapLabel")),
        value: group.querySelector("text.treemapValue") ? text(group.querySelector("text.treemapValue")) : null,
      })),
      titles: [...svg.querySelectorAll(":scope > title, :scope > desc")].map((element) => ({ tag: element.tagName, text: element.textContent })),
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
            title: diagram.db.getDiagramTitle(),
            accTitle: diagram.db.getAccTitle(),
            accDescr: diagram.db.getAccDescription(),
            nodes: diagram.db.getNodes(),
            root: diagram.db.getRoot(),
          };
        } catch {}
        try {
          await mermaid.render("grammar-treemap", source);
          return { parse: true, render: true, db };
        } catch (error) {
          return { parse: true, render: false, db, error: { name: error?.name ?? "Error", message: String(error?.message ?? error) } };
        }
      } catch (error) {
        return {
          parse: false,
          render: false,
          error: {
            name: error?.name ?? "Error",
            message: String(error?.message ?? error),
            line: Number(error?.result?.parserErrors?.[0]?.token?.startLine ?? error?.result?.lexerErrors?.[0]?.line ?? 0),
            column: Number(error?.result?.parserErrors?.[0]?.token?.startColumn ?? error?.result?.lexerErrors?.[0]?.column ?? 0),
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

  const pixelDir = path.join(fixtureDir, "treemap-pixel");
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
  writeJson(path.join(fixtureDir, "treemap-grammar.json"), { upstream: provenance, cases: grammar });
  writeJson(path.join(fixtureDir, "treemap-geometry.json"), { upstream: provenance, cases: geometry });
  writeJson(path.join(fixtureDir, "treemap-config.json"), { upstream: provenance, cases: config });
  writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixelManifest });
  console.log(`Generated Treemap fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixelManifest.length} pixel.`);
} finally {
  await browser.close();
}
