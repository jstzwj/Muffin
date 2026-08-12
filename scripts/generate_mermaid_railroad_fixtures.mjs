import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const VERSION = "11.16.0";
const MODULE_SHA = "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const CHROME_PRODUCT = "Chrome/151.0.7922.76";
const CHROME_SHA = "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const NOTO_SHA = "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";
const DIALECT_MODULES = {
  railroad: ["railroadDiagram-RFXS5EU6.mjs", "070c34a3c52bcf70f45f37d1ea3a8f9c8881a565ecac10d9c777133a7c93bda6"],
  railroadEbnf: ["ebnfDiagram-CCIWWBDH.mjs", "76620e31e376bdb16c9e7e2cbf5009ef9aa7bb620eff92d8764f40d3ca868d8c"],
  railroadAbnf: ["abnfDiagram-VRR7QNED.mjs", "7104d15f869ae8944eef735784b7b677aecb35edded748476e47ad69e03d5641"],
  railroadPeg: ["pegDiagram-2B236MQR.mjs", "46306a1b083db2c918f32e9b13009075f53d02acfba41c44a6e6c098646284ba"],
};
const SHARED_MODULE = ["chunk-MOJQB5TN.mjs", "3c6b785a3e3755b7ea2e22a1b84e03d5af6b3d121a4d015295161762afac9165"];

const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const fixtureDir = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid"));
const chrome = path.resolve(process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe");
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const coreDir = path.join(mermaidRoot, "dist", "chunks", "mermaid.core");
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const pixelDir = path.join(fixtureDir, "railroad-pixel");
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
assert(sha(fs.readFileSync(chrome)) === CHROME_SHA, "Chrome drifted");
assert(sha(fs.readFileSync(fontFile)) === NOTO_SHA, "Noto drifted");
for (const [id, [name, expected]] of Object.entries(DIALECT_MODULES))
  assert(sha(fs.readFileSync(path.join(coreDir, name))) === expected, `${id} module drifted`);
assert(sha(fs.readFileSync(path.join(coreDir, SHARED_MODULE[0]))) === SHARED_MODULE[1], "railroad shared module drifted");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const stable = (body, config = {}) => init({
  ...config,
  fontFamily: "Noto Sans",
  themeVariables: { ...(config.themeVariables ?? {}), fontFamily: "Noto Sans", fontSize: "14px" },
  railroad: { fontFamily: "Noto Sans", fontSize: 14, ...(config.railroad ?? {}) },
}, body);

const DIRECT = `railroad-beta
title Direct grammar
accTitle: Direct accessible
accDescr {First line
  Second line}
root = sequence(terminal("if"), nonterminal("condition"), optional(terminal("else")), choice(terminal("yes"), terminal("no")), zeroOrMore(special("token")), oneOrMore(nonterminal("item")));
escaped = terminal("line\\nquote\\\"");`;
const EBNF = `railroad-ebnf-beta
root = "if", condition, ["else"], ("yes" | "no"), { ? token ? }, item+;
except ::= letter - "x";`;
const ABNF = `railroad-abnf-beta
root = "if" condition ["else"] ("yes" / "no") *item %x41-5A ;
bounded = 0*1foo 2bar 3*5baz ;`;
const PEG = `railroad-peg-beta
root <- "if" condition "else"? ("yes" / "no") item* .+;
look <- &"a" !forbidden value;`;

const grammarCases = [
  ["direct-canonical", DIRECT], ["direct-empty", "railroad-beta"],
  ["direct-comments", "%% head\nrailroad-beta\n/* body */ A=terminal('x');"],
  ["direct-duplicate", "railroad-beta\nA=terminal('x');\nA=terminal('y');"],
  ["direct-metadata", "railroad-beta\ntitle \"A\\nB\"\naccTitle:  A   B\naccDescr { one\n   two }\nA=terminal('x');"],
  ["direct-invalid-header-case", "RAILROAD-BETA\nA=terminal('x');"],
  ["direct-prefix", "railroad-betaX\nA=terminal('x');"],
  ["direct-missing-semicolon", "railroad-beta\nA=terminal('x')"],
  ["direct-unknown-call", "railroad-beta\nA=bogus('x');"],
  ["direct-empty-sequence", "railroad-beta\nA=sequence();"],
  ["direct-unquoted", "railroad-beta\nA=terminal(x);"],
  ["ebnf-canonical", EBNF], ["ebnf-empty", "railroad-ebnf-beta"],
  ["ebnf-adjacent", "railroad-ebnf-beta\nA='a' B [C] {D};"],
  ["ebnf-postfix-chain", "railroad-ebnf-beta\nA=('a'|'b')?*+;"],
  ["ebnf-comments", "railroad-ebnf-beta\n(* iso *) A=/*c*/'a';"],
  ["ebnf-special-empty", "railroad-ebnf-beta\nA = ?   ?;"],
  ["ebnf-trailing-comma", "railroad-ebnf-beta\nA='a',;"],
  ["ebnf-missing-semi", "railroad-ebnf-beta\nA='a'"],
  ["abnf-canonical", ABNF], ["abnf-empty", "railroad-abnf-beta"],
  ["abnf-comments", "railroad-abnf-beta\n; comment\nA = \"a\" ;"],
  ["abnf-repeats", "railroad-abnf-beta\nA = *foo 1*bar *2baz 3qux ;"],
  ["abnf-single-quote", "railroad-abnf-beta\nA='a';"],
  ["abnf-bad-num", "railroad-abnf-beta\nA=%x;"],
  ["abnf-missing-semi", "railroad-abnf-beta\nA=\"a\""],
  ["peg-canonical", PEG], ["peg-empty", "railroad-peg-beta"],
  ["peg-comments", "railroad-peg-beta\n# comment\nA <- 'a';"],
  ["peg-prefix-compound", "railroad-peg-beta\nA <- &(B C) !(D/E);"],
  ["peg-dot", "railroad-peg-beta\nA <- .;"],
  ["peg-double-suffix", "railroad-peg-beta\nA <- 'a'?*;"],
  ["peg-missing-semi", "railroad-peg-beta\nA <- 'a'"],
].map(([id, source]) => ({ id, source }));

const geometryCases = [
  ["direct", DIRECT], ["ebnf", EBNF], ["abnf", ABNF], ["peg", PEG],
  ["empty-direct", "railroad-beta"],
  ["single-terminal", "railroad-beta\nA=terminal('x');"],
  ["nested-choice", "railroad-ebnf-beta\nA=('a'|'bbbb'|('c', ['d']))+;"],
  ["optional", "railroad-ebnf-beta\nA=['optional'];"],
  ["zero-repeat", "railroad-ebnf-beta\nA={'zero'};"],
  ["one-repeat", "railroad-ebnf-beta\nA='one'+;"],
  ["special", "railroad-ebnf-beta\nA=? special value ?;"],
  ["multiple-rules", "railroad-ebnf-beta\nA='a';\nLongRule='long terminal',Ref;"],
  ["zero-spacing", "railroad-ebnf-beta\nA=('a'|'b'),['c'],{'d'};", { railroad: { padding: 0, verticalSeparation: 0, horizontalSeparation: 0, arcRadius: 0 } }],
  ["fixed-width", "railroad-peg-beta\nA <- 'a' / B;", { railroad: { useMaxWidth: false } }],
  ["font-size-24", "railroad-abnf-beta\nA = \"alpha\" beta ;", { railroad: { fontSize: 24 } }],
].map(([id, body, config = {}]) => ({ id, source: stable(body, config) }));

const configCases = [
  ["defaults", {}], ["use-max-false", { useMaxWidth: false }],
  ["compact-inert", { compactMode: true }], ["show-markers-inert", { showMarkers: false }],
  ["padding", { padding: 30 }], ["vertical-separation", { verticalSeparation: 30 }],
  ["horizontal-separation", { horizontalSeparation: 30 }], ["arc-radius", { arcRadius: 20 }],
  ["font-size", { fontSize: 24 }], ["font-family", { fontFamily: "Noto Sans" }],
  ["terminal-fill", { terminalFill: "#ff0000" }], ["terminal-stroke", { terminalStroke: "#00ff00" }],
  ["terminal-text", { terminalTextColor: "#0000ff" }], ["nonterminal-fill", { nonTerminalFill: "#ff0000" }],
  ["nonterminal-stroke", { nonTerminalStroke: "#00ff00" }], ["nonterminal-text", { nonTerminalTextColor: "#0000ff" }],
  ["line-color", { lineColor: "#ff0000" }], ["stroke-width", { strokeWidth: 7 }],
  ["marker-fill", { markerFill: "#ff0000" }], ["comment-fill-inert", { commentFill: "#ff0000" }],
  ["comment-stroke-inert", { commentStroke: "#ff0000" }], ["comment-text-inert", { commentTextColor: "#ff0000" }],
  ["special-fill", { specialFill: "#ff0000" }], ["special-stroke", { specialStroke: "#00ff00" }],
  ["rule-name", { ruleNameColor: "#ff0000" }], ["marker-radius", { markerRadius: 12 }],
  ["negative-padding-fallback", { padding: -1 }], ["parse-float", { padding: "20px" }],
  ["invalid-color-fallback", { terminalFill: "url(evil)" }], ["invalid-font-fallback", { fontFamily: "Noto;evil" }],
].map(([id, railroad]) => ({ id, source: stable("railroad-ebnf-beta\nA=('a'|B),['c'],{'d'},?s?;", { railroad }) }));
for (const theme of ["default", "dark", "forest", "neutral", "neo", "redux-color"])
  configCases.push({ id: `theme-${theme}`, source: stable("railroad-ebnf-beta\nA=('a'|B),?s?;", { theme }) });

const pixelCases = [
  { id: "default", source: stable(EBNF) },
  { id: "dark", source: stable(EBNF, { theme: "dark" }) },
  { id: "forest", source: stable(PEG, { theme: "forest" }) },
];

const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true, args: ["--no-sandbox", "--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"] });
assert(await browser.version() === CHROME_PRODUCT, "Chrome product drifted");
const fontUrl = pathToFileURL(fontFile).href;
const moduleUrl = pathToFileURL(moduleFile).href;

const snapshot = async (item, screenshotFile) => {
  const page = await browser.newPage();
  await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const result = await page.evaluate(async ({ source, moduleUrl, fontUrl }) => {
    document.body.style.margin = "0";
    document.body.innerHTML = '<div id="container"></div>';
    const style = document.createElement("style");
    style.textContent = `@font-face{font-family:"Noto Sans";src:url("${fontUrl}");font-weight:400;font-style:normal}html,body{margin:0;padding:0}`;
    document.head.appendChild(style);
    await document.fonts.load('14px "Noto Sans"', "Railroad grammar 0123456789");
    await document.fonts.ready;
    const mermaid = (await import(moduleUrl)).default;
    mermaid.initialize({ startOnLoad: false });
    let svg = "";
    let error = null;
    try { svg = (await mermaid.render("railroadFixture", source)).svg; }
    catch (value) {
      const sourceError = value?.hash ?? value?.parserErrors?.[0] ?? value?.lexerErrors?.[0] ?? value;
      error = { message: String(value?.message ?? value), line: sourceError?.loc?.first_line ?? sourceError?.token?.startLine ?? sourceError?.line ?? 0, column: (sourceError?.loc?.first_column ?? sourceError?.token?.startColumn ?? sourceError?.column ?? 0) + (sourceError?.loc ? 1 : 0) };
    }
    if (error) return { error };
    const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
    const database = {
      title: diagram.db.getTitle(),
      accTitle: diagram.db.getAccTitle(),
      accDescr: diagram.db.getAccDescription(),
      rules: diagram.db.getRules(),
    };
    document.getElementById("container").innerHTML = svg;
    const root = document.querySelector("svg");
    const rootMatrix = root.getScreenCTM();
    const relativeMatrix = (node) => {
      const matrix = rootMatrix.inverse().multiply(node.getScreenCTM());
      return [matrix.a, matrix.b, matrix.c, matrix.d, matrix.e, matrix.f];
    };
    const texts = [...root.querySelectorAll("text")].map((node) => ({
      className: node.getAttribute("class") ?? node.parentElement?.getAttribute("class") ?? "",
      text: node.textContent,
      x: node.getAttribute("x"), y: node.getAttribute("y"),
      transform: node.parentElement?.getAttribute("transform") ?? "",
      matrix: relativeMatrix(node),
      bbox: (() => { const b = node.getBBox(); return [b.x,b.y,b.width,b.height]; })(),
      computed: { fill: getComputedStyle(node).fill, fontFamily: getComputedStyle(node).fontFamily, fontSize: getComputedStyle(node).fontSize, fontWeight: getComputedStyle(node).fontWeight },
    }));
    const primitives = [...root.querySelectorAll("rect,circle,path")].map((node) => ({
      tag: node.tagName.toLowerCase(), className: node.getAttribute("class") ?? node.parentElement?.getAttribute("class") ?? "",
      transform: node.parentElement?.getAttribute("transform") ?? "",
      matrix: relativeMatrix(node),
      attrs: Object.fromEntries([...node.attributes].map((a) => [a.name,a.value])),
      bbox: (() => { const b=node.getBBox(); return [b.x,b.y,b.width,b.height]; })(),
      computed: { fill:getComputedStyle(node).fill, stroke:getComputedStyle(node).stroke, strokeWidth:getComputedStyle(node).strokeWidth, strokeDasharray:getComputedStyle(node).strokeDasharray },
    }));
    const rules = [...root.querySelectorAll(".railroad-rule")].map((node) => ({ transform: node.getAttribute("transform"), matrix: relativeMatrix(node), bbox: (() => { const b=node.getBBox(); return [b.x,b.y,b.width,b.height]; })() }));
    const box=root.getBoundingClientRect();
    return { database, root:{ viewBox:root.getAttribute("viewBox"), width:root.getAttribute("width"), height:root.getAttribute("height"), style:root.getAttribute("style"), client:[box.width,box.height] }, texts, primitives, rules };
  }, { source: item.source, moduleUrl, fontUrl });
  if (screenshotFile && !result.error) {
    const svg = await page.$("svg");
    await svg.screenshot({ path: screenshotFile, omitBackground: true });
  }
  await page.close();
  return result;
};

const grammarResults = [];
for (const item of grammarCases) {
  const rendered = await snapshot(item);
  grammarResults.push({ ...item, accepted: !rendered.error, database: rendered.database, error: rendered.error ?? undefined });
}
const geometryResults = [];
for (const item of geometryCases) geometryResults.push({ ...item, ...(await snapshot(item)) });
const configResults = [];
for (const item of configCases) configResults.push({ ...item, ...(await snapshot(item)) });
fs.mkdirSync(pixelDir, { recursive: true });
const pixelResults = [];
for (const item of pixelCases) {
  const file = path.join(pixelDir, `${item.id}.png`);
  const rendered = await snapshot(item, file);
  pixelResults.push({ ...item, file: `${item.id}.png`, width: rendered.root.client[0], height: rendered.root.client[1], sha256: sha(fs.readFileSync(file)) });
}
await browser.close();

const provenance = { version: VERSION, moduleSha256: MODULE_SHA, chrome: CHROME_PRODUCT, chromeSha256: CHROME_SHA, fontSha256: NOTO_SHA, dialectModules: Object.fromEntries(Object.entries(DIALECT_MODULES).map(([id,[name,hash]]) => [id,{name,sha256:hash}])), sharedModule:{name:SHARED_MODULE[0],sha256:SHARED_MODULE[1]} };
writeJson("railroad-grammar.json", { provenance, cases: grammarResults });
writeJson("railroad-geometry.json", { provenance, cases: geometryResults });
writeJson("railroad-config.json", { provenance, cases: configResults });
writeJson(path.join("railroad-pixel", "manifest.json"), { provenance, cases: pixelResults });
console.log(`Railroad fixtures: ${grammarResults.length} grammar, ${geometryResults.length} geometry, ${configResults.length} config, ${pixelResults.length} pixel`);
