import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_GITGRAPH_SHA256 =
  "9d46246ac6ec251dd26b28033496ea7e56c4914c1f1a6a9f9cb6b90a105a206b";
const EXPECTED_PARSER_SHA256 =
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
const diagramFile = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.core", "gitGraphDiagram-IHSO6WYX.mjs",
);
const parserFile = path.join(
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
assertEqual(sha256(fs.readFileSync(diagramFile)), EXPECTED_GITGRAPH_SHA256, "GitGraph module");
assertEqual(sha256(fs.readFileSync(parserFile)), EXPECTED_PARSER_SHA256, "Parser module");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const canonical = `gitGraph LR:
  commit id: "root" msg: "Initial" tag: "v1"
  branch develop order: 2
  commit id: "dev-1" type: HIGHLIGHT
  branch feature order: 1
  commit id: "feat-1" msg: "Feature"
  checkout develop
  merge feature id: "merge-1" tag: "merged"
  checkout main
  merge develop id: "release" type: REVERSE
  branch hotfix order: 3
  commit id: "fix-1"
  checkout main
  cherry-pick id: "fix-1" tag: "picked"`;

const grammarCases = [
  ["canonical", canonical],
  ["header", "gitGraph"],
  ["header-colon", "gitGraph:"],
  ["directions", "gitGraph TB:\ncommit id: \"a\""],
  ["direction-bt", "gitGraph BT:\ncommit id: \"a\""],
  ["direction-lowercase", "gitGraph lr:\ncommit id: \"a\""],
  ["uppercase-header", "GITGRAPH\ncommit id: \"a\""],
  ["prefix", "gitGraphX\ncommit id: \"a\""],
  ["commit-options", "gitGraph\ncommit id: 'a' msg: \"hello\" tag: \"x\" tag: 'y' type: REVERSE"],
  ["commit-message-short", "gitGraph\ncommit \"hello\""],
  ["commit-empty", "gitGraph\ncommit"],
  ["branch-id", "gitGraph\ncommit id: \"a\"\nbranch feature-1 order: 2.5\ncommit id: \"b\""],
  ["branch-string", "gitGraph\ncommit id: \"a\"\nbranch \"feature one\"\ncommit id: \"b\""],
  ["switch", "gitGraph\ncommit id: \"a\"\nbranch b\ncommit id: \"b\"\nswitch main\ncommit id: \"c\""],
  ["merge", "gitGraph\ncommit id: \"a\"\nbranch b\ncommit id: \"b\"\ncheckout main\nmerge b id: \"m\" tag: \"T\" type: HIGHLIGHT"],
  ["cherry", "gitGraph\ncommit id: \"a\"\nbranch b\ncommit id: \"b\"\ncheckout main\ncherry-pick id: \"b\" tag: \"pick\""],
  ["duplicate-commit", "gitGraph\ncommit id: \"a\"\ncommit id: \"a\""],
  ["duplicate-branch", "gitGraph\nbranch b\nbranch b"],
  ["checkout-missing", "gitGraph\ncheckout b"],
  ["merge-self", "gitGraph\ncommit id: \"a\"\nmerge main"],
  ["merge-missing", "gitGraph\ncommit id: \"a\"\nmerge b"],
  ["merge-same-head", "gitGraph\ncommit id: \"a\"\nbranch b\ncheckout main\nmerge b"],
  ["cherry-missing", "gitGraph\ncommit id: \"a\"\ncherry-pick id: \"missing\""],
  ["cherry-same-branch", "gitGraph\ncommit id: \"a\"\ncherry-pick id: \"a\""],
  ["metadata", "gitGraph\ntitle History\naccTitle: Git history\naccDescr: Commit graph\ncommit id: \"a\""],
  ["acc-block", "gitGraph\naccDescr {first\n  second}\ncommit id: \"a\""],
  ["comments", "%% before\ngitGraph\n%% body\ncommit id: \"a\""],
  ["semicolon", "gitGraph\ncommit id: \"a\";"],
  ["bare-id", "gitGraph\ncommit id: a"],
  ["bad-type", "gitGraph\ncommit type: MERGE"],
  ["same-line", "gitGraph commit id: \"a\""],
  ["crlf", "gitGraph\r\ncommit id: \"a\"\r\n"],
].map(([id, source]) => ({ id, source }));

const geometryCases = [
  ["canonical", canonical, {}],
  ["empty", "gitGraph", {}],
  ["linear", "gitGraph\ncommit id: \"a\"\ncommit id: \"b\"\ncommit id: \"c\"", {}],
  ["tb", canonical.replace("gitGraph LR:", "gitGraph TB:"), {}],
  ["bt", canonical.replace("gitGraph LR:", "gitGraph BT:"), {}],
  ["types", "gitGraph\ncommit id: \"a\"\ncommit id: \"b\" type: REVERSE\ncommit id: \"c\" type: HIGHLIGHT", {}],
  ["tags", "gitGraph\ncommit id: \"a\" tag: \"one\" tag: \"two\"\ncommit id: \"b\" tag: \"long tag\"", {}],
  ["multiline-branch", "gitGraph\ncommit id: \"a\"\nbranch \"feature\\nline\"\ncommit id: \"b\"", {}],
  ["parallel", canonical, { gitGraph: { parallelCommits: true } }],
  ["parallel-lr", `gitGraph LR:
  commit id: "root"
  branch alpha
  commit id: "alpha-1"
  checkout main
  branch beta
  commit id: "beta-1"
  checkout alpha
  commit id: "alpha-2"`, { gitGraph: { parallelCommits: true } }],
  ["parallel-tb", `gitGraph TB:
  commit id: "root"
  branch alpha
  commit id: "alpha-1"
  checkout main
  branch beta
  commit id: "beta-1"
  checkout alpha
  commit id: "alpha-2"`, { gitGraph: { parallelCommits: true } }],
  ["parallel-bt", `gitGraph BT:
  commit id: "root"
  branch alpha
  commit id: "alpha-1"
  checkout main
  branch beta
  commit id: "beta-1"
  checkout alpha
  commit id: "alpha-2"`, { gitGraph: { parallelCommits: true } }],
  ["no-labels", canonical, { gitGraph: { showCommitLabel: false } }],
  ["no-branches", canonical, { gitGraph: { showBranches: false } }],
  ["no-rotate", canonical, { gitGraph: { rotateCommitLabel: false } }],
  ["title", `gitGraph\ntitle Release History\ncommit id: \"a\"`, {}],
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
  ["use-max-false", { gitGraph: { useMaxWidth: false } }],
  ["padding", { gitGraph: { diagramPadding: 30 } }],
  ["title-margin", { gitGraph: { titleTopMargin: 50 } }],
  ["main-name", { gitGraph: { mainBranchName: "trunk" } }],
  ["main-order", { gitGraph: { mainBranchOrder: 5 } }],
  ["show-label", { gitGraph: { showCommitLabel: false } }],
  ["show-branches", { gitGraph: { showBranches: false } }],
  ["rotate-label", { gitGraph: { rotateCommitLabel: false } }],
  ["parallel", { gitGraph: { parallelCommits: true } }],
  ["use-width-inert", { gitGraph: { useWidth: 300 } }],
  ...themes.map((theme) => [`theme-${theme}`, { theme }]),
  ["font-family", { fontFamily: "monospace" }],
  ["commit-font", { themeVariables: { commitLabelFontSize: "20px" } }],
  ["tag-font", { themeVariables: { tagLabelFontSize: "20px" } }],
  ["commit-line", { themeVariables: { commitLineColor: "#ff0000" } }],
  ["git-color", { themeVariables: { git2: "#ff0000" } }],
].map(([id, config]) => ({
  id,
  source: init(
    { fontFamily: "Noto Sans", ...config },
    id === "main-name" ? canonical.replaceAll("checkout main", "checkout trunk") : canonical,
  ),
}));

const pixelCases = [
  ["default", {}], ["dark", { theme: "dark" }],
  ["redux-color", { theme: "redux-color" }], ["neo", { look: "neo", theme: "neo" }],
].map(([id, config]) => ({ id, source: init({ fontFamily: "Noto Sans", ...config }, canonical) }));
pixelCases.push({
  id: "dash-width-4",
  dashMask: true,
  source: init({
    fontFamily: "Noto Sans",
    gitGraph: { showCommitLabel: false },
    themeCSS:
      ".branch{stroke:#00ff00!important;stroke-width:4px!important;}" +
      ".branchLabelBkg,.branch-label0,.branch-label1{display:none!important;}",
  }, `gitGraph LR:
  commit id: "root"
  branch feature
  commit id: "feature"`),
});

const { default: puppeteer } = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js"),
).href);
const browser = await puppeteer.launch({
  headless: "new", executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"],
});
const provenance = {
  package: "mermaid", version: EXPECTED_VERSION,
  moduleSha256: EXPECTED_MODULE_SHA256, gitGraphModuleSha256: EXPECTED_GITGRAPH_SHA256,
  parserModuleSha256: EXPECTED_PARSER_SHA256, chromeProduct: EXPECTED_CHROME_PRODUCT,
  chromeSha256: EXPECTED_CHROME_SHA256, notoSansSha256: EXPECTED_NOTO_SHA256,
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
      await font.load(); document.fonts.add(font);
      await document.fonts.load("24px 'Noto Sans'");
    }, pathToFileURL(fontFile).href);
  };
  const render = async (source, id) => page.evaluate(async ({ source, id, moduleUrl }) => {
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const result = await mermaid.render(`git-${id}`, source);
    document.body.innerHTML = result.svg;
    const svg = document.querySelector("svg");
    const attrs = (el, names) => Object.fromEntries(names.map((name) => [name, el?.getAttribute(name) ?? null]));
    const box = (el) => { const b = el?.getBBox(); return b ? { x: b.x, y: b.y, width: b.width, height: b.height } : null; };
    const globalBox = (el) => {
      const b = el?.getBBox();
      const rootMatrix = svg?.getCTM();
      const elementMatrix = el?.getCTM();
      if (!b || !rootMatrix || !elementMatrix) return null;
      const matrix = rootMatrix.inverse().multiply(elementMatrix);
      const corners = [
        new DOMPoint(b.x, b.y),
        new DOMPoint(b.x + b.width, b.y),
        new DOMPoint(b.x, b.y + b.height),
        new DOMPoint(b.x + b.width, b.y + b.height),
      ].map((point) => point.matrixTransform(matrix));
      const xs = corners.map((point) => point.x);
      const ys = corners.map((point) => point.y);
      return {
        x: Math.min(...xs), y: Math.min(...ys),
        width: Math.max(...xs) - Math.min(...xs),
        height: Math.max(...ys) - Math.min(...ys),
      };
    };
    const primitive = (el) => {
      const style = getComputedStyle(el);
      return {
        tag: el.tagName, text: el.tagName === "text" ? el.textContent : "",
        parentClass: el.parentElement?.getAttribute("class") ?? "",
        attrs: attrs(el, ["class", "x", "y", "x1", "y1", "x2", "y2", "cx", "cy", "r", "width", "height", "rx", "ry", "d", "points", "transform", "fill", "stroke", "stroke-width", "stroke-dasharray", "opacity", "font-size", "font-weight", "text-anchor"]),
        bbox: box(el), globalBBox: globalBox(el),
        textLength: el.tagName === "text" ? el.getComputedTextLength() : null,
        computed: { fill: style.fill, stroke: style.stroke, strokeWidth: style.strokeWidth, strokeDasharray: style.strokeDasharray, opacity: style.opacity, fontFamily: style.fontFamily, fontSize: style.fontSize, fontWeight: style.fontWeight, textAnchor: style.textAnchor },
      };
    };
    const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
    const commits = [...diagram.db.getCommits().values()].map((c) => ({ ...c }));
    const branches = [...diagram.db.getBranches().entries()].map(([name, head]) => ({ name, head }));
    const currentConfig = mermaid.mermaidAPI.getConfig();
    const themeVariables = currentConfig.themeVariables;
    const snapshot = {
      effectiveStyle: {
        theme: currentConfig.theme,
        textColor: themeVariables.textColor,
        lineColor: themeVariables.lineColor,
        commitLineColor: themeVariables.commitLineColor ?? null,
        nodeBorder: themeVariables.nodeBorder,
        mainBkg: themeVariables.mainBkg,
        primaryColor: themeVariables.primaryColor,
        commitLabelColor: themeVariables.commitLabelColor,
        commitLabelBackground: themeVariables.commitLabelBackground,
        commitLabelFontSize: themeVariables.commitLabelFontSize,
        tagLabelColor: themeVariables.tagLabelColor,
        tagLabelBackground: themeVariables.tagLabelBackground,
        tagLabelBorder: themeVariables.tagLabelBorder,
        tagLabelFontSize: themeVariables.tagLabelFontSize,
        git: Array.from({ length: 8 }, (_, index) => themeVariables[`git${index}`]),
        gitInv: Array.from({ length: 8 }, (_, index) => themeVariables[`gitInv${index}`]),
        gitBranchLabel: Array.from({ length: 8 }, (_, index) => themeVariables[`gitBranchLabel${index}`]),
      },
      db: { direction: diagram.db.getDirection(), currentBranch: diagram.db.getCurrentBranch(), commits, branches, orderedBranches: diagram.db.getBranchesAsObjArray(), title: diagram.db.getDiagramTitle(), accTitle: diagram.db.getAccTitle(), accDescr: diagram.db.getAccDescription() },
      root: { attrs: attrs(svg, ["class", "viewBox", "width", "height", "style", "role", "aria-roledescription", "aria-labelledby"]), bbox: box(svg), client: { width: svg.getBoundingClientRect().width, height: svg.getBoundingClientRect().height }, order: [...svg.children].map((c) => c.getAttribute("class") || c.tagName) },
      primitives: [...svg.querySelectorAll("line,path,circle,rect,polygon,text")].map(primitive),
      metadata: [...svg.querySelectorAll(":scope > title,:scope > desc")].map((el) => ({ tag: el.tagName, text: el.textContent })),
    };
    const json = JSON.stringify(snapshot).replace(
      /\b(\d+)-[a-z0-9]{7}\b/g,
      (_match, seq) => `@generated:${seq}`,
    );
    return JSON.parse(json);
  }, { source, id, moduleUrl: pathToFileURL(moduleFile).href });

  const grammar = [];
  for (const test of grammarCases) {
    await prepare();
    const expected = await page.evaluate(async ({ source, moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict", deterministicIds: true, deterministicIDSeed: "git-fixture" });
      try {
        const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
        const commits = [...diagram.db.getCommits().values()].map((c) => ({ ...c }));
        const rawDb = { direction: diagram.db.getDirection(), currentBranch: diagram.db.getCurrentBranch(), commits, branches: [...diagram.db.getBranches().entries()].map(([name, head]) => ({ name, head })), orderedBranches: diagram.db.getBranchesAsObjArray(), title: diagram.db.getDiagramTitle(), accTitle: diagram.db.getAccTitle(), accDescr: diagram.db.getAccDescription() };
        const dbJson = JSON.stringify(rawDb).replace(
          /\b(\d+)-[a-z0-9]{7}\b/g,
          (_match, seq) => `@generated:${seq}`,
        );
        const db = JSON.parse(dbJson);
        try { await mermaid.render("grammar-git", source); return { parse: true, render: true, db }; }
        catch (error) { return { parse: true, render: false, db, error: { name: error?.name ?? "Error", message: String(error?.message ?? error) } }; }
      } catch (error) {
        const parser = error?.result?.parserErrors?.[0]; const lexer = error?.result?.lexerErrors?.[0];
        return { parse: false, render: false, error: { name: error?.name ?? "Error", message: String(error?.message ?? error), kind: lexer ? "Lexer" : parser ? "Parser" : "Runtime", line: Number(parser?.token?.startLine ?? lexer?.line ?? 0), column: Number(parser?.token?.startColumn ?? lexer?.column ?? 0), token: String(parser?.token?.text ?? lexer?.character ?? "") } };
      }
    }, { source: test.source, moduleUrl: pathToFileURL(moduleFile).href });
    grammar.push({ ...test, expected });
  }
  const geometry = [];
  for (const test of geometryCases) { await prepare(); geometry.push({ ...test, expected: await render(test.source, test.id) }); }
  const config = [];
  for (const test of configCases) { await prepare(); config.push({ ...test, expected: await render(test.source, `config-${test.id}`) }); }
  const pixelDir = path.join(fixtureDir, "gitgraph-pixel"); fs.mkdirSync(pixelDir, { recursive: true });
  const pixels = [];
  const capturePixel = async (test) => {
    await prepare(); await render(test.source, `pixel-${test.id}`);
    const dashStyles = test.dashMask
      ? await page.$$eval("line.branch", (elements) => elements.map((element) => {
          const style = getComputedStyle(element);
          return { stroke: style.stroke, strokeWidth: style.strokeWidth,
            strokeDasharray: style.strokeDasharray,
            strokeLinecap: style.strokeLinecap, strokeLinejoin: style.strokeLinejoin };
        }))
      : [];
    const svg = await page.$("svg");
    const rootGeometry = await page.$eval("svg", (element) => {
      const rect = element.getBoundingClientRect();
      return { clientX: rect.x, clientY: rect.y, clientWidth: rect.width,
        clientHeight: rect.height, viewBox: element.getAttribute("viewBox") };
    });
    const bytes = await svg.screenshot({ omitBackground: true });
    const bounds = await svg.boundingBox();
    let dashMask = null;
    if (test.dashMask) {
      await page.$eval("svg", (root) => {
        const transparent = "rgba(0, 0, 0, 0)";
        for (const element of root.querySelectorAll("*")) {
          element.style.setProperty("fill", transparent, "important");
          element.style.setProperty("stroke", transparent, "important");
          element.style.setProperty("color", transparent, "important");
          element.style.setProperty("background", transparent, "important");
        }
        for (const element of root.querySelectorAll("line.branch"))
          element.style.setProperty("stroke", "#00ff00", "important");
      });
      dashMask = await svg.screenshot({ omitBackground: true });
    }
    return { bytes, bounds, dashMask, dashStyles, rootGeometry };
  };
  for (const test of pixelCases) {
    const first = await capturePixel(test);
    const second = await capturePixel(test);
    if (!first.bytes.equals(second.bytes) ||
        JSON.stringify(first.dashStyles) !== JSON.stringify(second.dashStyles) ||
        JSON.stringify(first.rootGeometry) !== JSON.stringify(second.rootGeometry) ||
        Boolean(first.dashMask) !== Boolean(second.dashMask) ||
        (first.dashMask && !first.dashMask.equals(second.dashMask)))
      throw new Error(`${test.id}: pixel rendering is not byte deterministic`);
    const file = `${test.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), first.bytes);
    const dashMaskFile = first.dashMask ? `${test.id}-dash-mask.png` : null;
    if (dashMaskFile) fs.writeFileSync(path.join(pixelDir, dashMaskFile), first.dashMask);
    pixels.push({ id: test.id, source: test.source, file,
      width: Math.round(first.bounds.width), height: Math.round(first.bounds.height),
      sha256: sha256(first.bytes),
      ...(dashMaskFile ? { dashStyles: first.dashStyles, dashMaskFile,
        dashMaskSha256: sha256(first.dashMask),
        rootGeometry: first.rootGeometry } : {}) });
  }
  const referencedPngs = new Set(pixels.flatMap((fixture) =>
    [fixture.file, fixture.dashMaskFile].filter(Boolean)));
  for (const name of fs.readdirSync(pixelDir).filter((name) => name.endsWith(".png")))
    if (!referencedPngs.has(name)) fs.rmSync(path.join(pixelDir, name));
  fs.mkdirSync(fixtureDir, { recursive: true });
  writeJson(path.join(fixtureDir, "gitgraph-grammar.json"), { upstream: provenance, cases: grammar });
  writeJson(path.join(fixtureDir, "gitgraph-geometry.json"), { upstream: provenance, cases: geometry });
  writeJson(path.join(fixtureDir, "gitgraph-config.json"), { upstream: provenance, cases: config });
  writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixels });
  console.log(`Generated GitGraph fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixels.length} pixel.`);
} finally {
  await browser.close();
}
