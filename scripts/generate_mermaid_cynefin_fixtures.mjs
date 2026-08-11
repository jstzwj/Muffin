import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Cynefin source-entry oracle.
const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_CYNEFIN_MODULE_SHA256 =
  "3dbe403effb1abbc413cce6a4433bc2dd59feef86c5da296057459b40a5ffc9c";
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
const cynefinModuleFile = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.core", "cynefinDiagram-TSTJHNR4.mjs",
);
const parserModuleFile = path.join(
  path.dirname(mermaidRoot), "@mermaid-js", "parser", "dist", "chunks",
  "mermaid-parser.core", "chunk-KEIR6QF5.mjs",
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
assertEqual(sha256(fs.readFileSync(cynefinModuleFile)), EXPECTED_CYNEFIN_MODULE_SHA256, "Cynefin module");
assertEqual(sha256(fs.readFileSync(parserModuleFile)), EXPECTED_PARSER_MODULE_SHA256, "Cynefin parser module");
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

const canonical = `cynefin-beta
title Decision Landscape
complex
  "Explore"
  "Experiment"
complicated
  "Analyse"
clear
  "Standardise"
chaotic
  "Stabilise"
confusion
  "Unknown one"
  "Unknown two"
  "Unknown three"
  "Unknown four"
complex --> complicated : "learn"
chaotic --> clear`;

const grammarCases = [
  ["canonical", canonical],
  ["colon-header", canonical.replace("cynefin-beta", "cynefin-beta:")],
  ["uppercase", canonical.replace("cynefin-beta", "CYNEFIN-BETA")],
  ["prefix", "cynefin-betaX\nclear"],
  ["header-only", "cynefin-beta"],
  ["header-newline", "cynefin-beta\n"],
  ["colon-only", "cynefin-beta:"],
  ["all-empty-domains", "cynefin-beta\ncomplex\ncomplicated\nchaotic\nclear\nconfusion"],
  ["single-domain", "cynefin-beta\ncomplex\n\"Probe\""],
  ["same-line-items", "cynefin-beta complex \"A\" \"B\""],
  ["single-quotes", "cynefin-beta\ncomplex\n'A'\n'B'"],
  ["empty-item", "cynefin-beta\ncomplex\n\"\""],
  ["escaped-item", "cynefin-beta\ncomplex\n\"A\\\"B\""],
  ["markup-item", "cynefin-beta\ncomplex\n\"<script>x</script><b>A</b>\""],
  ["bare-item", "cynefin-beta\ncomplex\nA"],
  ["unterminated-item", "cynefin-beta\ncomplex\n\"A"],
  ["multiline-item", "cynefin-beta\ncomplex\n\"A\nB\""],
  ["unknown-domain", "cynefin-beta\nobvious\n\"A\""],
  ["case-domain", "cynefin-beta\nComplex\n\"A\""],
  ["repeated-domain", "cynefin-beta\ncomplex\n\"old\"\ncomplex\n\"new\""],
  ["transition", "cynefin-beta\ncomplex --> clear : \"move\""],
  ["transition-no-label", "cynefin-beta\ncomplex --> clear"],
  ["transition-empty-label", "cynefin-beta\ncomplex --> clear : \"\""],
  ["self-loop", "cynefin-beta\ncomplex --> complex : \"ignored\""],
  ["transition-no-space", "cynefin-beta\ncomplex-->clear:\"move\""],
  ["transition-bare-label", "cynefin-beta\ncomplex --> clear : move"],
  ["transition-same-line", "cynefin-beta\ncomplex --> clear complex --> chaotic"],
  ["semicolon", "cynefin-beta\ncomplex --> clear;"],
  ["title", "cynefin-beta\ntitle Inline Heading\nclear\n\"Known\""],
  ["metadata", "cynefin-beta\naccTitle: AT\naccDescr: AD\nclear\n\"Known\""],
  ["acc-block", "cynefin-beta\naccDescr {first\n  second}\nclear\n\"Known\""],
  ["repeated-title", "cynefin-beta\ntitle First\ntitle Second\nclear"],
  ["comment-before", "%% comment\ncynefin-beta\nclear"],
  ["comment-body", "cynefin-beta\n%% comment\nclear"],
  ["blank-lines", "\n cynefin-beta:\n\n complex\n \"A\"\n"],
  ["crlf", "cynefin-beta\r\ncomplex\r\n\"A\""],
  ["frontmatter", frontmatter({ cynefin: { seed: 7 } }, "cynefin-beta\nclear", "Front")],
  ["directive", init({ cynefin: { seed: 7 } }, "cynefin-beta\nclear")],
].map(([id, source]) => ({ id, source }));

const geometryCases = [
  ["canonical", canonical, {}],
  ["empty", "cynefin-beta", {}],
  ["domains-empty", "cynefin-beta\ncomplex\ncomplicated\nchaotic\nclear\nconfusion", {}],
  ["items", "cynefin-beta\ncomplex\n\"Explore\"\n\"Experiment\"\nclear\n\"Standardise\"", {}],
  ["confusion-overflow", "cynefin-beta\nconfusion\n\"One\"\n\"Two\"\n\"Three\"\n\"Four\"\n\"Five\"", {}],
  ["transitions", "cynefin-beta\ncomplex --> complicated : \"learn\"\nchaotic --> clear\nconfusion --> complex : \"sense\"", {}],
  ["no-descriptions", canonical, { cynefin: { showDomainDescriptions: false } }],
  ["custom-size", canonical, { cynefin: { width: 480, height: 360, padding: 12 } }],
  ["amplitude-zero", canonical, { cynefin: { boundaryAmplitude: 0 } }],
  ["amplitude-large", canonical, { cynefin: { boundaryAmplitude: 30 } }],
  ["title-only", "cynefin-beta\ntitle Decision Landscape", {}],
  ["metadata", "cynefin-beta\naccTitle: Accessible\naccDescr: Description\nclear\n\"Known\"", {}],
  ["long-item", "cynefin-beta\ncomplex\n\"A deliberately very long item that is never wrapped\"", {}],
  ["empty-item", "cynefin-beta\ncomplex\n\"\"", {}],
  ["all-transitions", "cynefin-beta\ncomplex --> complicated : \"A\"\ncomplicated --> clear : \"B\"\nclear --> chaotic : \"C\"\nchaotic --> confusion : \"D\"", {}],
  ["fixed-width", canonical, { cynefin: { useMaxWidth: false } }],
].map(([id, body, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", cynefin: { seed: 17 }, ...config }, body),
}));

const themes = [
  "base", "dark", "default", "forest", "neutral", "neo", "neo-dark",
  "redux", "redux-dark", "redux-color", "redux-dark-color",
];
const styleKeys = [
  "domainFontSize", "itemFontSize", "boundaryColor", "boundaryWidth",
  "cliffColor", "cliffWidth", "arrowColor", "arrowWidth", "complexBg",
  "complicatedBg", "chaoticBg", "clearBg", "confusionBg", "textColor",
  "labelColor",
];
const styleValues = {
  domainFontSize: 23,
  itemFontSize: 19,
  boundaryColor: "#110000",
  boundaryWidth: 7,
  cliffColor: "#220000",
  cliffWidth: 8,
  arrowColor: "#330000",
  arrowWidth: 9,
  complexBg: "#440000",
  complicatedBg: "#550000",
  chaoticBg: "#660000",
  clearBg: "#770000",
  confusionBg: "#880000",
  textColor: "#990000",
  labelColor: "#aa0000",
};
const configCases = [
  ["defaults", {}],
  ["use-max-false", { cynefin: { useMaxWidth: false } }],
  ["use-max-string", { cynefin: { useMaxWidth: "false" } }],
  ["use-width", { cynefin: { useWidth: 500 } }],
  ["width", { cynefin: { width: 480 } }],
  ["width-false", { cynefin: { width: false } }],
  ["width-null", { cynefin: { width: null } }],
  ["width-array", { cynefin: { width: [480] } }],
  ["height", { cynefin: { height: 360 } }],
  ["padding", { cynefin: { padding: 12 } }],
  ["padding-false", { cynefin: { padding: false } }],
  ["padding-null", { cynefin: { padding: null } }],
  ["padding-array", { cynefin: { padding: [12] } }],
  ["descriptions-false", { cynefin: { showDomainDescriptions: false } }],
  ["descriptions-string", { cynefin: { showDomainDescriptions: "false" } }],
  ["descriptions-zero", { cynefin: { showDomainDescriptions: 0 } }],
  ["descriptions-null", { cynefin: { showDomainDescriptions: null } }],
  ["amplitude", { cynefin: { boundaryAmplitude: 30 } }],
  ["amplitude-zero", { cynefin: { boundaryAmplitude: 0 } }],
  ["amplitude-string", { cynefin: { boundaryAmplitude: "30" } }],
  ["amplitude-null", { cynefin: { boundaryAmplitude: null } }],
  ["seed", { cynefin: { seed: 37 } }],
  ["seed-zero", { cynefin: { seed: 0 } }],
  ["seed-string", { cynefin: { seed: "37" } }],
  ["seed-bool", { cynefin: { seed: true } }],
  ["seed-null", { cynefin: { seed: null } }],
  ["width-string", { cynefin: { width: "480" } }],
  ["height-string", { cynefin: { height: "360" } }],
  ["padding-string", { cynefin: { padding: "12" } }],
  ["frontmatter", null],
  ...themes.map((theme) => [`theme-${theme}`, { theme }]),
  ...styleKeys.map((key) => [`style-${key}`, { themeVariables: { cynefin: { [key]: styleValues[key] } } }]),
].map(([id, config]) => ({
  id,
  source: id === "frontmatter"
    ? frontmatter({ fontFamily: "Noto Sans", cynefin: { width: 480, height: 360, padding: 12, showDomainDescriptions: false, boundaryAmplitude: 20, seed: 37, useMaxWidth: false } }, canonical, "Front")
    : init({ fontFamily: "Noto Sans", cynefin: { seed: 17 }, ...config }, canonical),
}));

const pixelCases = [
  ["default", {}],
  ["dark", { theme: "dark" }],
  ["forest", { theme: "forest" }],
  ["transitions", {}],
].map(([id, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", cynefin: { seed: 17 }, ...config },
    id === "transitions" ? geometryCases.find((entry) => entry.id === "transitions").source.replace(/^%%\{init:[^\n]+\}%%\n/, "") : canonical),
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
  cynefinModuleSha256: EXPECTED_CYNEFIN_MODULE_SHA256,
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
    const result = await mermaid.render(`cynefin-${id}`, source);
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
    const computed = (element) => {
      if (!element) return null;
      const style = getComputedStyle(element);
      return {
        fill: style.fill,
        fillOpacity: style.fillOpacity,
        stroke: style.stroke,
        strokeWidth: style.strokeWidth,
        strokeDasharray: style.strokeDasharray,
        fontFamily: style.fontFamily,
        fontSize: style.fontSize,
        fontStyle: style.fontStyle,
        fontWeight: style.fontWeight,
        textAnchor: style.textAnchor,
      };
    };
    const text = (element) => element ? {
      text: element.textContent,
      attrs: attrs(element, ["class", "x", "y", "text-anchor", "dominant-baseline"]),
      bbox: box(element),
      computed: computed(element),
    } : null;
    const shape = (element) => element ? {
      attrs: attrs(element, ["class", "x", "y", "width", "height", "rx", "ry", "d", "fill", "fill-opacity", "stroke", "stroke-width", "stroke-dasharray", "marker-end"]),
      bbox: box(element),
      computed: computed(element),
    } : null;
    const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
    const rootGroup = [...svg.children].find((element) =>
      element.tagName === "g" && element.hasAttribute("transform"));
    return {
      db: {
        title: diagram.db.getDiagramTitle(),
        accTitle: diagram.db.getAccTitle(),
        accDescr: diagram.db.getAccDescription(),
        domains: [...diagram.db.getDomains().entries()],
        transitions: diagram.db.getTransitions(),
      },
      root: {
        attrs: attrs(svg, ["id", "class", "viewBox", "width", "height", "style", "role", "aria-roledescription", "aria-labelledby"]),
        bbox: box(svg),
        client: { width: svg.getBoundingClientRect().width, height: svg.getBoundingClientRect().height },
        order: [...svg.children].map((child) => child.getAttribute("class") || child.tagName),
      },
      container: {
        attrs: attrs(rootGroup, ["transform"]),
        order: [...rootGroup.children].map((child) => child.getAttribute("class") || child.tagName),
      },
      backgrounds: [...svg.querySelectorAll(".cynefin-backgrounds rect")].map(shape),
      boundaries: [...svg.querySelectorAll(".cynefin-boundaries path")].map(shape),
      confusion: shape(svg.querySelector("path.cynefinConfusion")),
      labels: [...svg.querySelectorAll("text.cynefinDomainLabel")].map(text),
      subtitles: [...svg.querySelectorAll("text.cynefinSubtitle")].map(text),
      items: [...svg.querySelectorAll(".cynefin-items > g")].map((group) => ({
        attrs: attrs(group, ["transform"]),
        order: [...group.children].map((child) => child.tagName),
        bbox: box(group),
        rect: shape(group.querySelector("rect")),
        text: text(group.querySelector("text")),
      })),
      arrows: [...svg.querySelectorAll(".cynefin-arrows > *")].map((element) =>
        element.tagName === "path" ? { tag: "path", ...shape(element) } : { tag: "text", ...text(element) }),
      marker: shape(svg.querySelector("marker path")),
      markerAttrs: attrs(svg.querySelector("marker"), ["id", "viewBox", "refX", "refY", "markerWidth", "markerHeight", "orient"]),
      title: text(svg.querySelector("text.cynefinTitle")),
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
            title: diagram.db.getDiagramTitle(),
            accTitle: diagram.db.getAccTitle(),
            accDescr: diagram.db.getAccDescription(),
            domains: [...diagram.db.getDomains().entries()],
            transitions: diagram.db.getTransitions(),
          };
        } catch {}
        try {
          await mermaid.render("grammar-cynefin", source);
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
            kind: lexer ? "Lexer" : "Parser",
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

  const pixelDir = path.join(fixtureDir, "cynefin-pixel");
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
  writeJson(path.join(fixtureDir, "cynefin-grammar.json"), { upstream: provenance, cases: grammar });
  writeJson(path.join(fixtureDir, "cynefin-geometry.json"), { upstream: provenance, cases: geometry });
  writeJson(path.join(fixtureDir, "cynefin-config.json"), { upstream: provenance, cases: config });
  writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixelManifest });
  console.log(`Generated Cynefin fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixelManifest.length} pixel.`);
} finally {
  await browser.close();
}
