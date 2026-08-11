import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Architecture source-entry oracle.
const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_ARCHITECTURE_MODULE_SHA256 =
  "d6f8424fba961c50f2cfcbd4e1c5f53f37311d83cc768bcf41afd8874c0454ba";
const EXPECTED_PARSER_MODULE_SHA256 =
  "08628d5e6194206bf5f1d5afb9c456db355492cd4245d13b55303fa3d2267387";
const EXPECTED_GRAMMAR_MODULE_SHA256 =
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
const architectureModuleFile = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.esm", "architectureDiagram-FW2JMN5B.mjs",
);
const parserRoot = path.join(path.dirname(mermaidRoot), "@mermaid-js", "parser", "dist", "chunks", "mermaid-parser.esm");
const parserModuleFile = path.join(parserRoot, "chunk-O467AACY.mjs");
const grammarModuleFile = path.join(parserRoot, "chunk-36B4POZ4.mjs");
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
assertEqual(sha256(fs.readFileSync(architectureModuleFile)), EXPECTED_ARCHITECTURE_MODULE_SHA256, "Architecture module");
assertEqual(sha256(fs.readFileSync(parserModuleFile)), EXPECTED_PARSER_MODULE_SHA256, "Architecture parser module");
assertEqual(sha256(fs.readFileSync(grammarModuleFile)), EXPECTED_GRAMMAR_MODULE_SHA256, "Parser grammar module");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome binary");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans font");

const init = (config, body) => `%%{init:${JSON.stringify(config)}}%%\n${body}`;
const frontmatter = (config, body, title = "") => `---\n${title ? `title: ${title}\n` : ""}config:\n${Object.entries(config).map(([key, value]) => `  ${key}: ${JSON.stringify(value)}`).join("\n")}\n---\n${body}`;

const canonical = `architecture-beta
group platform(cloud)[Platform]
service user(internet)[User]
service api(server)[API] in platform
service db(database)[Database] in platform
junction hub in platform
user:R --> L:api
api:B -- T:db
hub:R -- L:api
align row hub api`;

const grammarCases = [
  ["canonical", canonical],
  ["header-only", "architecture-beta"],
  ["header-newline", "architecture-beta\n"],
  ["wrong-header", "architecture\nservice a(server)[A]"],
  ["uppercase-header", "ARCHITECTURE-BETA\nservice a(server)[A]"],
  ["mixed-header", "Architecture-Beta\nservice a(server)[A]"],
  ["prefix", "architecture-betaX\nservice a(server)[A]"],
  ["same-line", "architecture-beta service a(server)[A]"],
  ["leading-blank", "\n\narchitecture-beta\nservice a(server)[A]"],
  ["comment", "%% before\narchitecture-beta\n%% body\nservice a(server)[A]"],
  ["directive", init({ architecture: { padding: 12 } }, "architecture-beta\nservice a(server)[A]")],
  ["frontmatter", frontmatter({ architecture: { padding: 12 } }, "architecture-beta\nservice a(server)[A]", "Front")],
  ["metadata", "architecture-beta\ntitle Inline\naccTitle: Accessible\naccDescr: Description\nservice a(server)[A]"],
  ["acc-block", "architecture-beta\naccDescr {first\n  second}\nservice a(server)[A]"],
  ["group-minimal", "architecture-beta\ngroup g"],
  ["group-icon-title", "architecture-beta\ngroup g(cloud)[Cloud Group]"],
  ["group-quoted-title", "architecture-beta\ngroup g(cloud)[\"Quoted title\"]"],
  ["group-nested", "architecture-beta\ngroup outer(cloud)[Outer]\ngroup inner(server)[Inner] in outer"],
  ["group-before-parent", "architecture-beta\ngroup inner(server)[Inner] in outer\ngroup outer(cloud)[Outer]"],
  ["group-self", "architecture-beta\ngroup g in g"],
  ["duplicate-group", "architecture-beta\ngroup g\ngroup g"],
  ["service-minimal", "architecture-beta\nservice a"],
  ["service-icon", "architecture-beta\nservice a(server)"],
  ["service-text-icon", "architecture-beta\nservice a \"TXT\" [Service]"],
  ["service-title", "architecture-beta\nservice a[Service Label]"],
  ["service-escaped-title", "architecture-beta\nservice a[\"It\\\"s API\"]"],
  ["service-in-group", "architecture-beta\ngroup g\nservice a(server)[A] in g"],
  ["service-parent-missing", "architecture-beta\nservice a(server)[A] in g"],
  ["service-parent-node", "architecture-beta\nservice a\nservice b in a"],
  ["service-self", "architecture-beta\nservice a in a"],
  ["duplicate-service", "architecture-beta\nservice a\nservice a"],
  ["junction", "architecture-beta\njunction j"],
  ["junction-in-group", "architecture-beta\ngroup g\njunction j in g"],
  ["duplicate-cross-type", "architecture-beta\ngroup same\nservice same"],
  ["edge-horizontal", "architecture-beta\nservice a\nservice b\na:R -- L:b"],
  ["edge-vertical", "architecture-beta\nservice a\nservice b\na:B -- T:b"],
  ["edge-bend", "architecture-beta\nservice a\nservice b\na:R -- T:b"],
  ["edge-title", "architecture-beta\nservice a\nservice b\na:R -[flow]- L:b"],
  ["edge-arrows", "architecture-beta\nservice a\nservice b\na:R <--> L:b"],
  ["edge-left-arrow", "architecture-beta\nservice a\nservice b\na:R <-- L:b"],
  ["edge-right-arrow", "architecture-beta\nservice a\nservice b\na:R --> L:b"],
  ["edge-group-modifiers", "architecture-beta\ngroup g1\ngroup g2\nservice a in g1\nservice b in g2\na{group}:R --> L:b{group}"],
  ["edge-missing-left", "architecture-beta\nservice b\na:R -- L:b"],
  ["edge-missing-right", "architecture-beta\nservice a\na:R -- L:b"],
  ["edge-same-port", "architecture-beta\nservice a\nservice b\na:R -- R:b"],
  ["edge-lower-direction", "architecture-beta\nservice a\nservice b\na:r -- l:b"],
  ["align-row", "architecture-beta\nservice a\nservice b\nalign row a b"],
  ["align-column", "architecture-beta\nservice a\nservice b\nservice c\nalign column a b c"],
  ["align-one", "architecture-beta\nservice a\nalign row a"],
  ["align-missing", "architecture-beta\nservice a\nalign row a missing"],
  ["align-duplicate", "architecture-beta\nservice a\nservice b\nalign row a a b"],
  ["id-hyphen", "architecture-beta\nservice api-v2(server)[API V2]"],
  ["id-leading-hyphen", "architecture-beta\nservice -api(server)[API]"],
  ["icon-pack", "architecture-beta\nservice a(logos:aws)[AWS]"],
  ["unknown-icon", "architecture-beta\nservice a(does-not-exist)[Unknown]"],
  ["bad-title-punctuation", "architecture-beta\nservice a[API / DB]"],
  ["semicolon", "architecture-beta\nservice a;"],
  ["crlf", "architecture-beta\r\nservice a(server)[A]\r\n"],
].map(([id, source]) => ({ id, source }));

const geometryCases = [
  ["canonical", canonical, {}],
  ["empty", "architecture-beta", {}],
  ["single-default", "architecture-beta\nservice a[Service]", {}],
  ["icons", "architecture-beta\nservice a(database)[Database]\nservice b(server)[Server]\nservice c(internet)[Internet]\na:R -- L:b\nb:R -- L:c", {}],
  ["text-icon", "architecture-beta\nservice a \"API\" [Text icon]", {}],
  ["junction", "architecture-beta\nservice a[Left]\njunction j\nservice b[Right]\na:R -- L:j\nj:R -- L:b", {}],
  ["nested-groups", "architecture-beta\ngroup outer(cloud)[Outer]\ngroup inner(server)[Inner] in outer\nservice a(database)[A] in inner\nservice b(internet)[B] in outer\na:R --> L:b", {}],
  ["cross-groups", grammarCases.find((c) => c.id === "edge-group-modifiers").source, {}],
  ["all-directions", "architecture-beta\nservice c[Center]\nservice l[Left]\nservice r[Right]\nservice t[Top]\nservice b[Bottom]\nc:L -- R:l\nc:R -- L:r\nc:T -- B:t\nc:B -- T:b", {}],
  ["arrows-labels", "architecture-beta\nservice a[A]\nservice b[B]\nservice c[C]\na:R <-[both]-> L:b\nb:B -[down]- T:c", {}],
  ["bend-label", "architecture-beta\nservice a[A]\nservice b[B]\na:R -[bend]- T:b", {}],
  ["align-row", "architecture-beta\nservice a[A]\nservice b[B]\nservice c[C]\na:R -- L:b\nb:R -- L:c\nalign row a b c", {}],
  ["align-column", "architecture-beta\nservice a[A]\nservice b[B]\nservice c[C]\na:B -- T:b\nb:B -- T:c\nalign column a b c", {}],
  ["disconnected", "architecture-beta\nservice a[A]\nservice b[B]\nservice c[C]", {}],
  ["title", "architecture-beta\ntitle Architecture Title\nservice a[A]", {}],
  ["padding-icon-size", canonical, { architecture: { padding: 20, iconSize: 56 } }],
  ["layout-tuning", canonical, { architecture: { nodeSeparation: 30, idealEdgeLengthMultiplier: 2, edgeElasticity: 0.2, numIter: 500, seed: 7 } }],
  ["fixed-width", canonical, { architecture: { useMaxWidth: false } }],
].map(([id, body, config]) => ({
  id,
  source: init({ fontFamily: "Noto Sans", ...config }, body),
}));

const themes = [
  "base", "dark", "default", "forest", "neutral", "neo", "neo-dark",
  "redux", "redux-dark", "redux-color", "redux-dark-color",
];
const architectureKeys = [
  "useMaxWidth", "padding", "iconSize", "fontSize", "randomize",
  "nodeSeparation", "idealEdgeLengthMultiplier", "edgeElasticity", "numIter", "seed",
];
const architectureValues = {
  useMaxWidth: false,
  padding: 18,
  iconSize: 52,
  fontSize: 21,
  randomize: true,
  nodeSeparation: 32,
  idealEdgeLengthMultiplier: 2.25,
  edgeElasticity: 0.2,
  numIter: 600,
  seed: 9,
};
const styleKeys = [
  "archEdgeColor", "archEdgeArrowColor", "archEdgeWidth",
  "archGroupBorderColor", "archGroupBorderWidth",
];
const styleValues = {
  archEdgeColor: "#112233",
  archEdgeArrowColor: "#223344",
  archEdgeWidth: "7px",
  archGroupBorderColor: "#334455",
  archGroupBorderWidth: "5px",
};
const configCases = [
  ["defaults", {}],
  ...architectureKeys.map((key) => [`architecture-${key}`, { architecture: { [key]: architectureValues[key] } }]),
  ["all-architecture", { architecture: architectureValues }],
  ["padding-string", { architecture: { padding: "18" } }],
  ["padding-zero", { architecture: { padding: 0 } }],
  ["padding-null", { architecture: { padding: null } }],
  ["padding-array", { architecture: { padding: [18] } }],
  ["icon-size-string", { architecture: { iconSize: "52" } }],
  ["randomize-string", { architecture: { randomize: "false" } }],
  ["use-max-string", { architecture: { useMaxWidth: "false" } }],
  ["seed-zero", { architecture: { seed: 0 } }],
  ["seed-string", { architecture: { seed: "9" } }],
  ["top-level-padding", { padding: 18 }],
  ["frontmatter", null],
  ...themes.map((theme) => [`theme-${theme}`, { theme }]),
  ...styleKeys.map((key) => [`style-${key}`, { themeVariables: { [key]: styleValues[key] } }]),
  ["style-all", { themeVariables: styleValues }],
  ["font-family", { fontFamily: "Courier New" }],
].map(([id, config]) => ({
  id,
  source: id === "frontmatter"
    ? frontmatter({ fontFamily: "Noto Sans", architecture: architectureValues }, canonical, "Front title")
    : init({ fontFamily: "Noto Sans", ...config }, canonical),
}));

const pixelCases = [
  ["default", {}],
  ["dark", { theme: "dark" }],
  ["forest", { theme: "forest" }],
  ["nested", {}, geometryCases.find((c) => c.id === "nested-groups").source],
].map(([id, config, source]) => ({
  id,
  source: source ?? init({ fontFamily: "Noto Sans", ...config }, canonical),
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
  architectureModuleSha256: EXPECTED_ARCHITECTURE_MODULE_SHA256,
  parserModuleSha256: EXPECTED_PARSER_MODULE_SHA256,
  grammarModuleSha256: EXPECTED_GRAMMAR_MODULE_SHA256,
  cytoscapeVersion: "3.34.0",
  fcoseVersion: "2.2.0",
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
  const render = async (source, id) => page.evaluate(async ({ source, id, moduleUrl, configKeys }) => {
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const result = await mermaid.render(`architecture-${id}`, source);
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
      const attributes = attrs(element, [
        "id", "class", "x", "y", "x1", "y1", "x2", "y2", "cx", "cy", "r",
        "width", "height", "rx", "ry", "d", "points", "transform", "fill",
        "fill-opacity", "stroke", "stroke-width", "stroke-dasharray", "opacity",
        "font-size", "font-weight", "text-anchor", "dominant-baseline",
      ]);
      // Iconify assigns process/time-derived IDs to internal icon paths. They
      // are neither referenced by the Architecture renderer nor observable in
      // geometry/paint, so keep the structural presence while removing the
      // nondeterministic prefix from the reproducible oracle.
      if (attributes.id?.startsWith("IconifyId")) attributes.id = "IconifyId";
      return {
        tag: element.tagName,
        parentClass: element.parentElement?.getAttribute("class") ?? "",
        text: element.textContent ?? "",
        attrs: attributes,
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
    const db = diagram.db;
    const simplifyNode = (node) => ({
      id: node.id,
      type: node.type,
      icon: node.icon ?? null,
      iconText: node.iconText ?? null,
      title: node.title ?? null,
      in: node.in ?? null,
    });
    const simplifyEdge = (edge) => ({
      lhsId: edge.lhsId,
      rhsId: edge.rhsId,
      lhsDir: edge.lhsDir,
      rhsDir: edge.rhsDir,
      lhsInto: Boolean(edge.lhsInto),
      rhsInto: Boolean(edge.rhsInto),
      lhsGroup: Boolean(edge.lhsGroup),
      rhsGroup: Boolean(edge.rhsGroup),
      title: edge.title ?? null,
    });
    return {
      db: {
        services: db.getServices().map(simplifyNode),
        junctions: db.getJunctions().map(simplifyNode),
        groups: db.getGroups().map(simplifyNode),
        edges: db.getEdges().map(simplifyEdge),
        alignments: db.getLayoutHints().map((hint) => ({ direction: hint.direction, members: [...hint.members] })),
        dataStructures: JSON.parse(JSON.stringify(db.getDataStructures())),
        config: Object.fromEntries(configKeys.map((key) => [key, db.getConfigField(key)])),
        title: db.getDiagramTitle(),
        accTitle: db.getAccTitle(),
        accDescr: db.getAccDescription(),
      },
      root: {
        attrs: attrs(svg, ["id", "class", "viewBox", "width", "height", "style", "role", "aria-roledescription", "aria-labelledby"]),
        bbox: box(svg),
        client: { width: svg.getBoundingClientRect().width, height: svg.getBoundingClientRect().height },
        order: [...svg.children].map((child) => child.getAttribute("class") || child.tagName),
      },
      layers: [...svg.querySelectorAll(":scope > g")].map((layer) => ({
        class: layer.getAttribute("class") ?? "",
        bbox: box(layer),
        children: [...layer.children].map((child) => ({ tag: child.tagName, id: child.id, class: child.getAttribute("class") ?? "", transform: child.getAttribute("transform") })),
      })),
      primitives: [...svg.querySelectorAll("rect,line,circle,ellipse,path,polygon,text,foreignObject")].map(primitive),
      metadata: [...svg.querySelectorAll(":scope > title, :scope > desc")].map((element) => ({ tag: element.tagName, text: element.textContent })),
    };
  }, { source, id, moduleUrl: pathToFileURL(moduleFile).href, configKeys: architectureKeys });

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
          const value = diagram.db;
          db = {
            services: value.getServices().map(({ id, type, icon, iconText, title, in: parent }) => ({ id, type, icon: icon ?? null, iconText: iconText ?? null, title: title ?? null, in: parent ?? null })),
            junctions: value.getJunctions().map(({ id, type, in: parent }) => ({ id, type, in: parent ?? null })),
            groups: value.getGroups().map(({ id, icon, title, in: parent }) => ({ id, icon: icon ?? null, title: title ?? null, in: parent ?? null })),
            edges: value.getEdges().map((edge) => ({ ...edge })),
            alignments: value.getLayoutHints().map((hint) => ({ direction: hint.direction, members: [...hint.members] })),
            title: value.getDiagramTitle(),
            accTitle: value.getAccTitle(),
            accDescr: value.getAccDescription(),
          };
        } catch {}
        try {
          await mermaid.render("grammar-architecture", source);
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

  const pixelDir = path.join(fixtureDir, "architecture-pixel");
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
  writeJson(path.join(fixtureDir, "architecture-grammar.json"), { upstream: provenance, cases: grammar });
  writeJson(path.join(fixtureDir, "architecture-geometry.json"), { upstream: provenance, cases: geometry });
  writeJson(path.join(fixtureDir, "architecture-config.json"), { upstream: provenance, cases: config });
  writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixelManifest });
  console.log(`Generated Architecture fixtures: ${grammar.length} grammar, ${geometry.length} geometry, ${config.length} config, ${pixelManifest.length} pixel.`);
} finally {
  await browser.close();
}
