import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Sankey source-entry oracle.
const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_SANKEY_MODULE_SHA256 =
  "c34fab7be73b824f060579c70c10ed3a4d0fc40489e4d57abfe00fc042662ca4";
const EXPECTED_ENGINE_SHA256 =
  "b721b95e028b9b45dff0b79edfddd9ce57934ac9e00a7ddd080d245ddfaf0cf0";
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
const sankeyModuleFile = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.core", "sankeyDiagram-HTMAVEWB.mjs",
);
const engineRoot = path.join(path.dirname(mermaidRoot), "d3-sankey", "src");
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assertEqual = (actual, expected, label) => {
  if (actual !== expected) throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const enginePayload = fs.readdirSync(engineRoot).sort().map((name) =>
  `${name}\n${fs.readFileSync(path.join(engineRoot, name), "utf8")}`,
).join("\n");
const writeJson = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assertEqual(pkg.version, EXPECTED_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MODULE_SHA256, "Mermaid module");
assertEqual(sha256(fs.readFileSync(sankeyModuleFile)), EXPECTED_SANKEY_MODULE_SHA256, "Sankey module");
assertEqual(sha256(enginePayload), EXPECTED_ENGINE_SHA256, "d3-sankey engine");
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
const canonical = `sankey-beta
Coal,Electricity,30
Gas,Electricity,20
Electricity,Industry,28
Electricity,Homes,17
Electricity,Losses,5`;

const grammarCases = [
  ["canonical", canonical],
  ["non-beta", canonical.replace("sankey-beta", "sankey")],
  ["uppercase", canonical.replace("sankey-beta", "SANKEY-BETA")],
  ["prefix", "sankey-betaX\na,b,1"],
  ["header-only", "sankey-beta"],
  ["header-newline", "sankey-beta\n"],
  ["same-line", "sankey-beta a,b,1"],
  ["one-record", "sankey-beta\na,b,1"],
  ["decimal", "sankey-beta\na,b,.5"],
  ["signed", "sankey-beta\na,b,-2.5"],
  ["exponent", "sankey-beta\na,b,1e2"],
  ["nan", "sankey-beta\na,b,NaN"],
  ["infinity", "sankey-beta\na,b,Infinity"],
  ["garbage-number", "sankey-beta\na,b,12widgets"],
  ["empty-number", "sankey-beta\na,b,"],
  ["empty-source", "sankey-beta\n,b,1"],
  ["empty-target", "sankey-beta\na,,1"],
  ["quoted-comma", "sankey-beta\n\"a, one\",\"b, two\",3"],
  ["quoted-newline", "sankey-beta\n\"a\nline\",b,3"],
  ["escaped-quote", "sankey-beta\n\"a\"\"one\",b,3"],
  ["unterminated-quote", "sankey-beta\n\"a,b,3"],
  ["too-few-fields", "sankey-beta\na,b"],
  ["too-many-fields", "sankey-beta\na,b,1,x"],
  ["two-records-one-line", "sankey-beta\na,b,1,c,d,2"],
  ["semicolon", "sankey-beta\na,b,1;"],
  ["blank-lines", "\n sankey-beta \n\n a,b,1 \n\n b,c,2 \n"],
  ["crlf", "sankey-beta\r\na,b,1\r\nb,c,2"],
  ["comment-before", "%% comment\nsankey-beta\na,b,1"],
  ["comment-body", "sankey-beta\n%% comment\na,b,1"],
  ["duplicate-links", "sankey-beta\na,b,1\na,b,2"],
  ["self-cycle", "sankey-beta\na,a,1"],
  ["two-cycle", "sankey-beta\na,b,1\nb,a,2"],
  ["sanitizer", "sankey-beta\n\"<script>x</script><b>A</b>\",\"<img src=x onerror=bad>B\",1"],
  ["unicode-bare", "sankey-beta\n中文,b,1"],
  ["unicode-quoted", "sankey-beta\n\"中文\",b,1"],
  ["frontmatter", frontmatter({ sankey: { showValues: false } }, "sankey-beta\na,b,1", "Front")],
  ["directive", init({ sankey: { nodeAlignment: "left" } }, "sankey-beta\na,b,1")],
].map(([id, source]) => ({ id, source }));

const geometryCases = [
  ["canonical", canonical, {}],
  ["branching", "sankey-beta\nA,B,8\nA,C,5\nB,D,5\nC,D,3\nC,E,2", {}],
  ["three-columns", "sankey-beta\nA,B,10\nB,C,6\nB,D,4", {}],
  ["disconnected", "sankey-beta\nA,B,3\nC,D,7", {}],
  ["duplicate-links", "sankey-beta\nA,B,2\nA,B,3", {}],
  ["left", canonical, { sankey: { nodeAlignment: "left" } }],
  ["right", canonical, { sankey: { nodeAlignment: "right" } }],
  ["center", canonical, { sankey: { nodeAlignment: "center" } }],
  ["no-values", canonical, { sankey: { showValues: false } }],
  ["outlined", canonical, { sankey: { labelStyle: "outlined" } }],
  ["source-links", canonical, { sankey: { linkColor: "source" } }],
  ["target-links", canonical, { sankey: { linkColor: "target" } }],
  ["solid-links", canonical, { sankey: { linkColor: "#ff0000" } }],
  ["custom-size", canonical, { sankey: { width: 420, height: 260, nodeWidth: 24, nodePadding: 4 } }],
  ["node-colors", canonical, { sankey: { nodeColors: { Coal: "#ff0000", Electricity: "rgb(0, 128, 0)" } } }],
  ["long-labels", "sankey-beta\nA very long source label,A very long middle label,4\nA very long middle label,A very long target label,4", {}],
].map(([id, body, config]) => ({ id, source: init({ fontFamily: "Noto Sans", ...config }, body) }));

const configCases = [
  ["defaults", {}],
  ["width", { sankey: { width: 420 } }],
  ["height", { sankey: { height: 260 } }],
  ["use-max-false", { sankey: { useMaxWidth: false } }],
  ["use-width", { sankey: { useWidth: 320 } }],
  ["link-gradient", { sankey: { linkColor: "gradient" } }],
  ["link-source", { sankey: { linkColor: "source" } }],
  ["link-target", { sankey: { linkColor: "target" } }],
  ["link-color", { sankey: { linkColor: "purple" } }],
  ["align-left", { sankey: { nodeAlignment: "left" } }],
  ["align-right", { sankey: { nodeAlignment: "right" } }],
  ["align-center", { sankey: { nodeAlignment: "center" } }],
  ["align-justify", { sankey: { nodeAlignment: "justify" } }],
  ["show-values-false", { sankey: { showValues: false } }],
  ["prefix", { sankey: { prefix: "$" } }],
  ["suffix", { sankey: { suffix: " MW" } }],
  ["node-width", { sankey: { nodeWidth: 30 } }],
  ["node-padding", { sankey: { nodePadding: 2 } }],
  ["label-outlined", { sankey: { labelStyle: "outlined" } }],
  ["node-colors", { sankey: { nodeColors: { Coal: "#ff0000", Gas: "lime" } } }],
  ["node-colors-invalid", { sankey: { nodeColors: { Coal: "url(javascript:bad)" } } }],
  ["width-string", { sankey: { width: "420" } }],
  ["height-string", { sankey: { height: "260" } }],
  ["node-width-string", { sankey: { nodeWidth: "30" } }],
  ["node-padding-string", { sankey: { nodePadding: "2" } }],
  ["show-values-string", { sankey: { showValues: "false" } }],
  ["use-max-string", { sankey: { useMaxWidth: "false" } }],
  ["width-zero", { sankey: { width: 0 } }],
  ["height-zero", { sankey: { height: 0 } }],
  ["width-null", { sankey: { width: null } }],
  ["theme-dark", { theme: "dark" }],
  ["theme-forest", { theme: "forest" }],
  ["theme-redux-color", { theme: "redux-color" }],
  ["font-family", { fontFamily: "DefinitelyMissing, Noto Sans" }],
  ["text-color", { themeVariables: { textColor: "#ff0000" } }],
  ["main-bkg", { sankey: { labelStyle: "outlined" }, themeVariables: { mainBkg: "#00ff00" } }],
  ["background", { sankey: { labelStyle: "outlined" }, themeVariables: { background: "#0000ff" } }],
  ["frontmatter", null],
].map(([id, config]) => ({
  id,
  source: id === "frontmatter"
    ? frontmatter({ fontFamily: "Noto Sans", sankey: { width: 480, height: 280, nodeAlignment: "right", showValues: false, useMaxWidth: false } }, canonical, "Front")
    : init({ fontFamily: "Noto Sans", ...config }, canonical),
}));

const pixelCases = [
  ["default", {}], ["dark", { theme: "dark" }],
  ["forest", { theme: "forest" }], ["outlined", { sankey: { labelStyle: "outlined" } }],
].map(([id, config]) => ({ id, source: init({ fontFamily: "Noto Sans", ...config }, canonical) }));

const { default: puppeteer } = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js"),
).href);
const browser = await puppeteer.launch({
  headless: "new", executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"],
});
const provenance = {
  package: "mermaid", version: EXPECTED_VERSION, moduleSha256: EXPECTED_MODULE_SHA256,
  sankeyModuleSha256: EXPECTED_SANKEY_MODULE_SHA256,
  engine: "d3-sankey@0.12.3", engineSha256: EXPECTED_ENGINE_SHA256,
  chromeProduct: EXPECTED_CHROME_PRODUCT, chromeSha256: EXPECTED_CHROME_SHA256,
  notoSansSha256: EXPECTED_NOTO_SHA256, sourceEntry: true,
};

const cleanSource = (source) => source
  .replace(/^---[\s\S]*?---\s*/, "")
  .replace(/^%%\{[\s\S]*?\}%%\s*/, "");
const errorValue = (error) => ({
  name: error?.name ?? "Error", message: String(error?.message ?? error),
  line: Number(error?.hash?.loc?.first_line ?? error?.hash?.line ?? 0) +
    (error?.hash?.loc?.first_line === 0 ? 1 : 0),
  column: Number(error?.hash?.loc?.first_column ?? 0) + 1,
});

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
      await font.load(); document.fonts.add(font);
      await document.fonts.load("14px 'Noto Sans'");
    }, pathToFileURL(fontFile).href);
  };
  const render = async (source, id) => page.evaluate(async ({ source, id, moduleUrl }) => {
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const result = await mermaid.render(`sankey-${id}`, source);
    document.body.innerHTML = result.svg;
    const svg = document.querySelector("svg");
    const attrs = (el, names) => Object.fromEntries(names.map((name) => [name, el?.getAttribute(name)]));
    const box = (el) => { const b = el.getBBox(); return { x:b.x, y:b.y, width:b.width, height:b.height }; };
    const computed = (el) => { const s = getComputedStyle(el); return {
      fill:s.fill, stroke:s.stroke, strokeWidth:s.strokeWidth, strokeOpacity:s.strokeOpacity,
      fontFamily:s.fontFamily, fontSize:s.fontSize, textAnchor:s.textAnchor,
      mixBlendMode:s.mixBlendMode, paintOrder:s.paintOrder,
    }; };
    let graph;
    try {
      const diagram = await mermaid.mermaidAPI.getDiagramFromText(source
        .replace(/^---[\s\S]*?---\s*/, "").replace(/^%%\{[\s\S]*?\}%%\s*/, ""));
      graph = diagram.db.getGraph();
    } catch { graph = undefined; }
    return {
      graph,
      root: {
        attrs: attrs(svg, ["id","class","viewBox","width","height","style","role","aria-roledescription","aria-labelledby"]),
        bbox: box(svg), client: { width:svg.getBoundingClientRect().width, height:svg.getBoundingClientRect().height },
        order: [...svg.children].map((x) => x.getAttribute("class") || x.tagName),
      },
      nodes: [...svg.querySelectorAll(":scope > g.nodes > g.node")].map((g) => ({
        attrs: attrs(g,["id","class","transform","x","y"]), bbox:box(g),
        rect:{ attrs:attrs(g.querySelector("rect"),["height","width","fill"]), bbox:box(g.querySelector("rect")), computed:computed(g.querySelector("rect")) },
      })),
      labels: [...svg.querySelectorAll(":scope > g.node-labels > text")].map((el) => ({
        attrs:attrs(el,["class","x","y","dy","text-anchor"]), text:el.textContent, bbox:box(el), computed:computed(el),
      })),
      links: [...svg.querySelectorAll(":scope > g.links > g.link")].map((g) => ({
        attrs:attrs(g,["class","style"]), computed:computed(g),
        gradient:g.querySelector("linearGradient") ? {
          attrs:attrs(g.querySelector("linearGradient"),["id","gradientUnits","x1","x2"]),
          stops:[...g.querySelectorAll("stop")].map((s)=>attrs(s,["offset","stop-color"])),
        } : null,
        path:{ attrs:attrs(g.querySelector("path"),["d","stroke","stroke-width"]), bbox:box(g.querySelector("path")), computed:computed(g.querySelector("path")) },
      })),
      titles:[...svg.querySelectorAll(":scope > title, :scope > desc")].map((x)=>({tag:x.tagName,text:x.textContent})),
      svg:svg.outerHTML,
    };
  }, { source, id, moduleUrl:pathToFileURL(moduleFile).href });

  const grammar = [];
  for (const test of grammarCases) {
    await prepare();
    const result = await page.evaluate(async ({ source, moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({ startOnLoad:false, securityLevel:"strict" });
      try {
        await mermaid.parse(source);
        let graph;
        try {
          const diagram = await mermaid.mermaidAPI.getDiagramFromText(source
            .replace(/^---[\s\S]*?---\s*/, "").replace(/^%%\{[\s\S]*?\}%%\s*/, ""));
          graph = diagram.db.getGraph();
        } catch {}
        try { await mermaid.render("grammar-sankey", source); return { parse:true, render:true, graph }; }
        catch (error) { return { parse:true, render:false, graph, error:{ name:error?.name??"Error", message:String(error?.message??error) } }; }
      } catch (error) {
        return { parse:false, render:false, error:{ name:error?.name??"Error", message:String(error?.message??error), line:Number(error?.hash?.loc?.first_line??0)+1, column:Number(error?.hash?.loc?.first_column??0)+1 } };
      }
    }, { source:test.source, moduleUrl:pathToFileURL(moduleFile).href });
    grammar.push({ ...test, expected:result });
  }

  const geometry = [];
  for (const test of geometryCases) { await prepare(); geometry.push({ ...test, expected:await render(test.source,test.id) }); }
  const config = [];
  for (const test of configCases) { await prepare(); config.push({ ...test, expected:await render(test.source,`config-${test.id}`) }); }

  const pixelDir = path.join(fixtureDir, "sankey-pixel");
  fs.mkdirSync(pixelDir, { recursive:true });
  const pixelManifest = [];
  for (const test of pixelCases) {
    await prepare(); await render(test.source,`pixel-${test.id}`);
    const svg = await page.$("svg");
    const file = path.join(pixelDir, `${test.id}.png`);
    await svg.screenshot({ path:file, omitBackground:true });
    const bounds = await svg.boundingBox();
    pixelManifest.push({ id:test.id, source:test.source, file:`${test.id}.png`, width:Math.round(bounds.width), height:Math.round(bounds.height), sha256:sha256(fs.readFileSync(file)) });
  }
  fs.mkdirSync(fixtureDir, { recursive:true });
  writeJson(path.join(fixtureDir,"sankey-grammar.json"), { upstream:provenance, cases:grammar });
  writeJson(path.join(fixtureDir,"sankey-geometry.json"), { upstream:provenance, cases:geometry });
  writeJson(path.join(fixtureDir,"sankey-config.json"), { upstream:provenance, cases:config });
  writeJson(path.join(pixelDir,"manifest.json"), { upstream:provenance, cases:pixelManifest });
  console.log(`Generated Sankey fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixelManifest.length} pixel.`);
} finally {
  await browser.close();
}
