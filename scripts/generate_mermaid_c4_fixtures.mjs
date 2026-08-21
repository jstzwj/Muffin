import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_C4_SHA256 =
  "ea38b507259b57ac7d0fa526018cd1589abc99dc5f90af5c03f69540964a9862";
const EXPECTED_C4_MAP_SHA256 =
  "ff3aeaa5dd97f61feef685f5b09eb787f1d06dad665cb6353fe3d1d62ccf0df8";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA256 =
  "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";
const EXPECTED_NOTO_CJK_SHA256 =
  "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b";

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
const c4File = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.core", "c4Diagram-LMCZKHZV.mjs",
);
const c4MapFile = `${c4File}.map`;
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const cjkFontFile = path.resolve(
  "third_party", "noto", "fonts", "NotoSansCJKsc-Regular.otf",
);
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
assertEqual(sha256(fs.readFileSync(c4File)), EXPECTED_C4_SHA256, "C4 module");
assertEqual(sha256(fs.readFileSync(c4MapFile)), EXPECTED_C4_MAP_SHA256, "C4 source map");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans");
assertEqual(sha256(fs.readFileSync(cjkFontFile)), EXPECTED_NOTO_CJK_SHA256, "Noto Sans CJK SC");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const c4ShapeTypes = [
  "person", "external_person", "system", "external_system", "system_db",
  "external_system_db", "system_queue", "external_system_queue", "boundary",
  "message", "container", "external_container", "container_db",
  "external_container_db", "container_queue", "external_container_queue",
  "component", "external_component", "component_db", "external_component_db",
  "component_queue", "external_component_queue",
];
const fixedFontConfig = {
  fontFamily: "Noto Sans",
  c4: Object.fromEntries(c4ShapeTypes.map((type) => [`${type}FontFamily`, "Noto Sans"])),
};
const mergeFixtureConfig = (config = {}) => ({
  ...fixedFontConfig,
  ...config,
  c4: { ...fixedFontConfig.c4, ...(config.c4 ?? {}) },
});
const canonical = `C4Context
title Banking Context
Person(customer, "Customer", "Uses the bank")
System_Boundary(bank, "Bank") {
  SystemDb(core, "Core", "Accounts and payments")
  System(api, "API", "Public interface")
}
System_Ext(mail, "Mail", "Sends notifications")
Rel(customer, api, "Uses", "HTTPS")
BiRel(api, core, "Reads and writes", "SQL")
Rel_R(api, mail, "Sends", "SMTP")`;

const allShapes = `C4Deployment
Person(p, "Person", "Description")
Person_Ext(pe, "External person")
System(s, "System", "Description")
SystemDb(sd, "System DB", "Description")
SystemQueue(sq, "System Queue", "Description")
System_Ext(se, "External System")
SystemDb_Ext(sde, "External DB")
SystemQueue_Ext(sqe, "External Queue")
Container(c, "Container", "Node", "Description")
ContainerDb(cd, "Container DB", "SQL", "Description")
ContainerQueue(cq, "Container Queue", "MQ", "Description")
Container_Ext(ce, "External Container", "Node")
ContainerDb_Ext(cde, "External Container DB", "SQL")
ContainerQueue_Ext(cqe, "External Container Queue", "MQ")
Component(k, "Component", "Library", "Description")
ComponentDb(kd, "Component DB", "SQL", "Description")
ComponentQueue(kq, "Component Queue", "MQ", "Description")
Component_Ext(ke, "External Component", "Library")
ComponentDb_Ext(kde, "External Component DB", "SQL")
ComponentQueue_Ext(kqe, "External Component Queue", "MQ")`;

const boundariesSource = `C4Deployment
Enterprise_Boundary(e, "Enterprise") {
 System_Boundary(s, "System") {
  Container_Boundary(c, "Container") {
   Component(x, "X", "Tech")
  }
 }
 Boundary(b, "Generic", "Custom") {
  Person(p, "P")
 }
 Deployment_Node(n, "Node", "Type", "Description") {
  System(a, "A")
 }
 Node_L(l, "Left") {
  System(ll, "LL")
 }
 Node_R(r, "Right") {
  System(rr, "RR")
 }
}
`;

const grammarCases = [
  ["canonical", canonical],
  ["context", "C4Context\nPerson(a, \"A\")"],
  ["container", "C4Container\nContainer(a, \"A\", \"Tech\")"],
  ["component", "C4Component\nComponent(a, \"A\", \"Tech\")"],
  ["dynamic", "C4Dynamic\nPerson(a, \"A\")\nSystem(b, \"B\")\nRel(a,b,\"Uses\")"],
  ["deployment", "C4Deployment\nDeployment_Node(n, \"Node\") {\nSystem(a, \"A\")\n}\n"],
  ["uppercase-detector", "c4context\nPerson(a, A)"],
  ["detector-prefix", "prefix C4Container suffix\nContainer(a,A,T)"],
  ["direction-before", "direction LR\nC4Context\nPerson(a,A)"],
  ["direction-after", "C4Context\ndirection TB\nPerson(a,A)"],
  ["metadata", "C4Context\ntitle Heading\naccTitle: Accessible\naccDescr: Description\nPerson(a,\"A\")"],
  ["acc-block", "C4Context\naccDescr {first\n  second}\nPerson(a,\"A\")"],
  ["comments", "%% before\nC4Context\n%% body\nPerson(a,\"A\")"],
  ["all-shapes", allShapes],
  ["boundaries", boundariesSource],
  ["boundary-same-line-close", "C4Context\nBoundary(b,B) { Person(a,A) }"],
  ["relations", `C4Context
Person(a,"A")
System(b,"B")
System(c,"C")
Rel(a,b,"R")
BiRel(b,c,"BR")
Rel_U(a,c,"U")
Rel_Down(c,a,"D")
Rel_Left(a,b,"L")
Rel_Right(b,c,"RR")
Rel_Back(c,b,"B")`],
  ["rel-index", "C4Dynamic\nPerson(a,\"A\")\nSystem(b,\"B\")\nRelIndex(7,a,b,\"R\",\"T\")"],
  ["empty-attributes", "C4Context\nPerson(,A)\nSystem(a,)"],
  ["bare-attributes", "C4Context\nPerson(a, Alice Smith, Description here)"],
  ["quoted-comma", "C4Context\nPerson(a, \"Alice, Smith\", \"A \\\"quoted\\\" user\")"],
  ["key-value", "C4Context\nPerson(a,A,$descr=\"Desc\",$sprite=\"icon\",$tags=\"tag\",$link=\"https://example.org\")"],
  ["duplicate-shape", "C4Context\nPerson(a,\"A\",\"One\")\nSystem(a,\"B\",\"Two\")"],
  ["duplicate-rel", "C4Context\nPerson(a,\"A\")\nSystem(b,\"B\")\nRel(a,b,\"One\")\nBiRel(a,b,\"Two\",\"T\")"],
  ["styles", `C4Context
Person(a,"A")
System(b,"B")
Rel(a,b,"R")
UpdateElementStyle(a,$bgColor="#ff0000",$fontColor="#00ff00",$borderColor="#0000ff")
UpdateRelStyle(a,b,$textColor="#123456",$lineColor="#654321",$offsetX="10",$offsetY="-5")
UpdateLayoutConfig($c4ShapeInRow="2",$c4BoundaryInRow="1")`],
  ["style-positional", "C4Context\nPerson(a,\"A\")\nUpdateElementStyle(a,#f00,#0f0,#00f,\"true\")"],
  ["layout-invalid", "C4Context\nUpdateLayoutConfig(0,\"abc\")\nPerson(a,\"A\")"],
  ["missing-newline", "C4Context Person(a,A)"],
  ["missing-close", "C4Context\nPerson(a,A"],
  ["extra-close", "C4Context\nPerson(a,A))"],
  ["unknown", "C4Context\nUnknown(a,A)"],
  ["semicolon", "C4Context\nPerson(a,A);"],
  ["empty", "C4Context"],
  ["title-only", "C4Context\ntitle Heading"],
  ["crlf", "C4Context\r\nPerson(a,\"A\")\r\n"],
].map(([id, source]) => ({ id, source }));

const geometryCases = [
  ["canonical", canonical, {}],
  ["context", "C4Context\nPerson(a, \"Person\")\nSystem(b, \"System\", \"Description\")\nRel(a,b,\"Uses\",\"HTTPS\")", {}],
  ["all-shapes", allShapes, { c4: { c4ShapeInRow: 5 } }],
  ["nested", grammarCases.find((c) => c.id === "boundaries").source, {}],
  ["relations", grammarCases.find((c) => c.id === "relations").source, {}],
  ["dynamic", "C4Dynamic\nPerson(a,\"A\")\nSystem(b,\"B\")\nSystem(c,\"C\")\nRel(a,b,\"First\")\nRel(b,c,\"Second\",\"T\")", {}],
  ["styles", grammarCases.find((c) => c.id === "styles").source, {}],
  ["layout-two", `${allShapes}\nUpdateLayoutConfig(2,"1")`, {}],
  ["long-labels", "C4Context\nPerson(a, \"A very long person label that exceeds the configured box\", \"A long description with several words and a line<br/>break\")", {}],
  ["wrap-off", "C4Context\nPerson(a, \"A very long person label that exceeds the configured box\", \"A long description with several words\")", { c4: { wrap: false } }],
  ["title", "C4Context\ntitle Architecture\nPerson(a,\"A\")", {}],
  ["metadata", grammarCases.find((c) => c.id === "metadata").source, {}],
  ["title-only", "C4Context\ntitle Empty", {}],
].map(([id, body, config]) => ({ id, source: init(mergeFixtureConfig(config), body) }));

const configCases = [
  ["defaults", {}],
  ["use-max-false", { c4: { useMaxWidth: false } }],
  ["margin-x", { c4: { diagramMarginX: 80 } }],
  ["margin-y", { c4: { diagramMarginY: 40 } }],
  ["shape-margin", { c4: { c4ShapeMargin: 10 } }],
  ["shape-padding", { c4: { c4ShapePadding: 40 } }],
  ["width", { c4: { width: 300 } }],
  ["height", { c4: { height: 120 } }],
  ["box-margin", { c4: { boxMargin: 40 } }],
  ["shape-in-row", { c4: { c4ShapeInRow: 2 } }],
  ["next-line-padding", { c4: { nextLinePaddingX: 35 } }],
  ["boundary-in-row", { c4: { c4BoundaryInRow: 1 } }],
  ["wrap", { c4: { wrap: false } }],
  ["wrap-padding", { c4: { wrapPadding: 30 } }],
  ["person-font-size", { c4: { personFontSize: 24 } }],
  ["person-font-family", { c4: { personFontFamily: "Noto Sans CJK SC" } }],
  ["person-font-weight", { c4: { personFontWeight: "bold" } }],
  ["system-font-size", { c4: { systemFontSize: 24 } }],
  ["container-font-size", { c4: { containerFontSize: 24 } }],
  ["component-font-size", { c4: { componentFontSize: 24 } }],
  ["boundary-font-size", { c4: { boundaryFontSize: 24 } }],
  ["message-font-size", { c4: { messageFontSize: 24 } }],
  ["person-color", { c4: { person_bg_color: "#ff0000", person_border_color: "#00ff00" } }],
  ["system-color", { c4: { system_bg_color: "#ff0000", system_border_color: "#00ff00" } }],
  ["container-color", { c4: { container_bg_color: "#ff0000", container_border_color: "#00ff00" } }],
  ["component-color", { c4: { component_bg_color: "#ff0000", component_border_color: "#00ff00" } }],
  ["top-font", { fontFamily: "monospace", fontSize: 22, fontWeight: "bold" }],
  ["theme-dark", { theme: "dark" }],
  ["theme-forest", { theme: "forest" }],
  ["source-style", {}, grammarCases.find((c) => c.id === "styles").source],
  ["source-layout", {}, `${canonical}\nUpdateLayoutConfig(1,1)`],
].map(([id, config, body = canonical]) => ({ id, source: init(mergeFixtureConfig(config), body) }));

const pixelCases = [
  ["default", canonical, {}],
  ["dark", canonical, { theme: "dark" }],
  ["deployment", grammarCases.find((c) => c.id === "boundaries").source, {}],
].map(([id, body, config]) => ({ id, source: init(mergeFixtureConfig(config), body) }));
pixelCases.push({
  id: "dash-width-4",
  dashMask: true,
  source: init(mergeFixtureConfig({
    themeCSS: "rect{stroke:#00ff00!important;stroke-width:4px!important;}",
  }), `C4Context
Boundary(scope, "Scope") {
  Person(user, "User")
}`),
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
  moduleSha256: EXPECTED_MODULE_SHA256, c4ModuleSha256: EXPECTED_C4_SHA256,
  c4SourceMapSha256: EXPECTED_C4_MAP_SHA256,
  chromeProduct: EXPECTED_CHROME_PRODUCT, chromeSha256: EXPECTED_CHROME_SHA256,
  notoSansSha256: EXPECTED_NOTO_SHA256,
  notoSansCjkSha256: EXPECTED_NOTO_CJK_SHA256,
  sourceEntry: true,
};

const hostPage = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
const moduleUrl = pathToFileURL(moduleFile).href;
const fontUrl = pathToFileURL(fontFile).href;
const cjkFontUrl = pathToFileURL(cjkFontFile).href;
const newPage = async () => {
  const page = await browser.newPage();
  await page.setViewport({ width: 1400, height: 1000, deviceScaleFactor: 1 });
  await page.goto(hostPage);
  await page.evaluate(async ({ fontUrl, cjkFontUrl }) => {
    document.body.style.margin = "0";
    const font = new FontFace("Noto Sans", `url(${fontUrl})`);
    const cjkFont = new FontFace("Noto Sans CJK SC", `url(${cjkFontUrl})`);
    await Promise.all([font.load(), cjkFont.load()]);
    document.fonts.add(font); document.fonts.add(cjkFont);
    await document.fonts.load("24px 'Noto Sans'");
    await document.fonts.load("24px 'Noto Sans CJK SC'");
  }, { fontUrl, cjkFontUrl });
  return page;
};

const snapshot = async (page, source, id, render) => page.evaluate(async ({ source, id, moduleUrl, render }) => {
  const { default: mermaid } = await import(moduleUrl);
  mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
  let diagram;
  let svg = null;
  if (render) {
    const result = await mermaid.render(`c4-${id}`, source);
    document.body.innerHTML = result.svg;
    svg = document.querySelector("svg");
    diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
  } else {
    diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
  }
  const db = diagram.db;
  const clean = (value) => JSON.parse(JSON.stringify(value, (key, item) => {
    if (["x", "y", "width", "height", "margin", "image", "startPoint", "endPoint", "Y", "textLines"].includes(key)) return undefined;
    return item;
  }));
  const database = {
    c4Type: db.getC4Type(), title: db.getTitle(), accTitle: db.getAccTitle(),
    accDescr: db.getAccDescription(), shapeInRow: db.getC4ShapeInRow(),
    boundaryInRow: db.getC4BoundaryInRow(), shapes: clean(db.getC4ShapeArray()),
    boundaries: clean(db.getBoundaries()), relations: clean(db.getRels()),
  };
  if (!svg) return { database };
  const attrs = (el, names) => Object.fromEntries(names.map((name) => [name, el?.getAttribute(name) ?? null]));
  const box = (el) => { const b = el?.getBBox(); return b ? { x: b.x, y: b.y, width: b.width, height: b.height } : null; };
  const primitive = (el) => {
    const style = getComputedStyle(el);
    return {
      tag: el.tagName, text: el.tagName === "text" ? el.textContent : "",
      attrs: attrs(el, ["class", "x", "y", "x1", "y1", "x2", "y2", "width", "height", "rx", "ry", "d", "transform", "fill", "stroke", "stroke-width", "stroke-dasharray", "marker-start", "marker-end", "font-size", "font-family", "font-weight", "font-style", "text-anchor", "dominant-baseline"]),
      bbox: box(el),
      computed: { fill: style.fill, stroke: style.stroke, strokeWidth: style.strokeWidth, strokeDasharray: style.strokeDasharray, fontFamily: style.fontFamily, fontSize: style.fontSize, fontWeight: style.fontWeight, fontStyle: style.fontStyle, textAnchor: style.textAnchor },
    };
  };
  const config = mermaid.mermaidAPI.getConfig();
  return {
    database,
    root: {
      attrs: attrs(svg, ["width", "height", "viewBox", "style", "role", "aria-roledescription", "aria-labelledby", "aria-describedby"]),
      bbox: box(svg), client: { width: svg.clientWidth, height: svg.clientHeight },
      screenAvailWidth: screen.availWidth,
      accessibility: {
        title: svg.querySelector(":scope > title")?.textContent ?? null,
        description: svg.querySelector(":scope > desc")?.textContent ?? null,
      },
    },
    primitives: [...svg.querySelectorAll("rect,path,line,text,image")]
      .filter((element) => !element.closest("defs"))
      .map(primitive),
    config: { c4: clean(config.c4), fontFamily: config.fontFamily, fontSize: config.fontSize, fontWeight: config.fontWeight, theme: config.theme },
  };
}, { source, id, moduleUrl, render });

const grammarSnapshot = async (page, source) => page.evaluate(async ({ source, moduleUrl }) => {
  try {
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
    const db = diagram.db;
    const clean = (value) => JSON.parse(JSON.stringify(value));
    return {
      accepted: true,
      database: {
        c4Type: db.getC4Type(), title: db.getTitle(), accTitle: db.getAccTitle(),
        accDescr: db.getAccDescription(), shapeInRow: db.getC4ShapeInRow(),
        boundaryInRow: db.getC4BoundaryInRow(), shapes: clean(db.getC4ShapeArray()),
        boundaries: clean(db.getBoundaries()), relations: clean(db.getRels()),
      },
    };
  } catch (error) {
    const hash = error?.hash ?? error?.cause?.hash;
    return {
      accepted: false,
      error: {
        name: error?.name ?? "Error", message: String(error?.message ?? error),
        hash: hash ? {
          text: hash.text ?? null, token: hash.token ?? null, line: hash.line ?? null,
          loc: hash.loc ? {
            first_line: hash.loc.first_line, last_line: hash.loc.last_line,
            first_column: hash.loc.first_column, last_column: hash.loc.last_column,
          } : null,
          expected: hash.expected ?? null,
        } : null,
      },
    };
  }
}, { source, moduleUrl });

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  fs.mkdirSync(fixtureDir, { recursive: true });
  const grammar = [];
  for (const test of grammarCases) {
    const page = await newPage();
    try {
      grammar.push({ ...test, ...(await grammarSnapshot(page, test.source)) });
    } finally { await page.close(); }
  }
  writeJson(path.join(fixtureDir, "c4-grammar.json"), { provenance, cases: grammar });

  const geometry = [];
  for (const test of geometryCases) {
    const page = await newPage();
    try { geometry.push({ ...test, ...(await snapshot(page, test.source, test.id, true)) }); }
    catch (error) { geometry.push({ ...test, error: { name: error.name, message: error.message } }); }
    finally { await page.close(); }
  }
  writeJson(path.join(fixtureDir, "c4-geometry.json"), { provenance, cases: geometry });

  const config = [];
  for (const test of configCases) {
    const page = await newPage();
    try { config.push({ ...test, ...(await snapshot(page, test.source, test.id, true)) }); }
    catch (error) { config.push({ ...test, error: { name: error.name, message: error.message } }); }
    finally { await page.close(); }
  }
  writeJson(path.join(fixtureDir, "c4-config.json"), { provenance, cases: config });

  const pixelDir = path.join(fixtureDir, "c4-pixel");
  fs.mkdirSync(pixelDir, { recursive: true });
  const pixels = [];
  const capturePixel = async (test) => {
    const page = await newPage();
    try {
      await snapshot(page, test.source, test.id, true);
      const svg = await page.$("svg");
      const size = await page.$eval("svg", (el) => ({ width: el.clientWidth, height: el.clientHeight }));
      const dashStyles = test.dashMask
        ? await page.$$eval("rect[stroke-dasharray]", (elements) =>
            elements.map((element) => {
              const style = getComputedStyle(element);
              return { stroke: style.stroke, strokeWidth: style.strokeWidth,
                strokeDasharray: style.strokeDasharray,
                strokeLinecap: style.strokeLinecap, strokeLinejoin: style.strokeLinejoin };
            }))
        : [];
      const bytes = await svg.screenshot({ omitBackground: true });
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
          // SVG image pixels do not consume fill/stroke/color, so hide them
          // explicitly before revealing only dashed boundary rects.
          for (const element of root.querySelectorAll("image"))
            element.style.setProperty("display", "none", "important");
          for (const element of root.querySelectorAll("rect[stroke-dasharray]"))
            element.style.setProperty("stroke", "#00ff00", "important");
        });
        dashMask = await svg.screenshot({ omitBackground: true });
      }
      return { bytes, size, dashMask, dashStyles };
    } finally { await page.close(); }
  };
  for (const test of pixelCases) {
    const first = await capturePixel(test);
    const second = await capturePixel(test);
    if (!first.bytes.equals(second.bytes) ||
        JSON.stringify(first.dashStyles) !== JSON.stringify(second.dashStyles) ||
        Boolean(first.dashMask) !== Boolean(second.dashMask) ||
        (first.dashMask && !first.dashMask.equals(second.dashMask)))
      throw new Error(`${test.id}: pixel rendering is not byte deterministic`);
    const file = `${test.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), first.bytes);
    const dashMaskFile = first.dashMask ? `${test.id}-dash-mask.png` : null;
    if (dashMaskFile) fs.writeFileSync(path.join(pixelDir, dashMaskFile), first.dashMask);
    pixels.push({ ...test, file, ...first.size, sha256: sha256(first.bytes),
      ...(dashMaskFile ? { dashStyles: first.dashStyles, dashMaskFile,
        dashMaskSha256: sha256(first.dashMask) } : {}) });
  }
  const referencedPngs = new Set(pixels.flatMap((fixture) =>
    [fixture.file, fixture.dashMaskFile].filter(Boolean)));
  for (const name of fs.readdirSync(pixelDir).filter((name) => name.endsWith(".png")))
    if (!referencedPngs.has(name)) fs.rmSync(path.join(pixelDir, name));
  writeJson(path.join(pixelDir, "manifest.json"), { provenance, cases: pixels });

  const accepted = grammar.filter((entry) => entry.accepted).length;
  if (accepted < 20) throw new Error(`C4 grammar coverage unexpectedly low: ${accepted}`);
  console.log(`Generated C4 fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixels.length} pixel`);
} finally {
  await browser.close();
}
