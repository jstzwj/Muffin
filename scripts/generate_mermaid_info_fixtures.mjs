import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Info grammar/DOM/config/pixel oracle.

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_INFO_MODULE_SHA256 =
  "b824d959dd8443f8dea0d2582d84ed9ff7e50394d00dcdee8fa3b4d495160900";
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
const infoModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.core",
  "infoDiagram-FWYZ7A6U.mjs",
);
const parserModuleFile = path.join(
  path.dirname(mermaidRoot),
  "@mermaid-js",
  "parser",
  "dist",
  "mermaid-parser.esm.mjs",
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
assertEqual(sha256(fs.readFileSync(infoModuleFile)), EXPECTED_INFO_MODULE_SHA256, "Info module");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const grammarCases = [
  { id: "plain", source: "info" },
  { id: "show-info-inline", source: "info showInfo" },
  { id: "show-info-next-line", source: "info\nshowInfo" },
  { id: "leading-newlines-comments", source: " \n%% comment\ninfo\n" },
  { id: "frontmatter", source: "---\ntitle: Front\n---\ninfo" },
  { id: "metadata", source: "info\ntitle Inline\naccTitle: Accessible\naccDescr: Description" },
  { id: "metadata-inline", source: "info title Inline" },
  { id: "metadata-after-show", source: "info showInfo title Visible" },
  { id: "metadata-last-wins", source: "info\ntitle One\ntitle Two\naccTitle: A\naccTitle: B" },
  { id: "metadata-empty-title", source: "info\ntitle" },
  { id: "metadata-accdescr-block", source: "info\naccDescr { First line\n  Second line }" },
  { id: "inline-comment", source: "info %% trailing comment" },
  { id: "directive", source: init({ theme: "dark" }, "info") },
  { id: "reject-uppercase-detector", source: "INFO" },
  { id: "reject-prefix", source: "information" },
  { id: "reject-unknown", source: "info\nunknown" },
  { id: "reject-repeat-show", source: "info\nshowInfo\nshowInfo" },
  { id: "reject-semicolon", source: "info;" },
  { id: "reject-title-colon", source: "info\ntitle: Bad" },
  { id: "reject-garbage-after-show", source: "info showInfo garbage" },
];

const renderCases = [
  { id: "default", source: init({ fontFamily: "Noto Sans" }, "info") },
  { id: "show-info", source: init({ fontFamily: "Noto Sans" }, "info showInfo") },
  {
    id: "metadata-inert",
    source: init(
      { fontFamily: "Noto Sans" },
      "info\ntitle Inline title\naccTitle: Accessible\naccDescr: Description",
    ),
  },
  {
    id: "frontmatter-inert",
    source: "---\ntitle: Front title\n---\n" + init({ fontFamily: "Noto Sans" }, "info"),
  },
  { id: "dark", source: init({ theme: "dark", fontFamily: "Noto Sans" }, "info") },
  {
    id: "text-color",
    source: init(
      { fontFamily: "Noto Sans", themeVariables: { textColor: "#ff0000" } },
      "info",
    ),
  },
  {
    id: "font-family",
    source: init(
      { fontFamily: "monospace", themeVariables: { fontFamily: "monospace" } },
      "info",
    ),
  },
  {
    id: "theme-css-structure",
    source: init(
      {
        fontFamily: "Noto Sans",
        themeVariables: { fontFamily: "Noto Sans", fontSize: "16px" },
        themeCSS:
          ".version { fill:#ff0000 !important; opacity:0.5; font-size:24px !important; font-weight:700; } g:nth-of-type(2) text.version { fill:#00ff00 !important; }",
      },
      "info",
    ),
  },
  {
    id: "unrelated-config-inert",
    source: init({ useMaxWidth: false, width: 999, height: 777 }, "info"),
  },
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

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const hostPage = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const page = await browser.newPage();
  await page.setViewport({ width: 800, height: 600, deviceScaleFactor: 1 });

  const grammar = [];
  for (let index = 0; index < grammarCases.length; ++index) {
    const fixture = grammarCases[index];
    await page.goto(hostPage);
    const result = await page.evaluate(
      async ({ source, moduleUrl, parserModuleUrl }) => {
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
        try {
          await mermaid.parse(source);
          const { parse } = await import(parserModuleUrl);
          const ast = await parse("info", source);
          return {
            accept: true,
            ast: {
              title: ast.title ?? "",
              accTitle: ast.accTitle ?? "",
              accDescr: ast.accDescr ?? "",
            },
          };
        } catch (error) {
          const raw = String(error?.message ?? error);
          const message = raw.replace(/\s+/g, " ").trim();
          const match = raw.match(/(?:Lexer|Parse) error on line (\d+|\?), column (\d+|\?)/);
          let kind = "unknown";
          if (message.startsWith("No diagram type detected")) kind = "no-diagram";
          else if (message.includes("Lexer error")) kind = "lexer";
          else if (message.includes("Parse error") || message.includes("Parsing failed")) kind = "parser";
          return {
            accept: false,
            reject: {
              kind,
              message,
              line: match && match[1] !== "?" ? Number(match[1]) : 0,
              column: match && match[2] !== "?" ? Number(match[2]) : 0,
            },
          };
        }
      },
      {
        source: fixture.source,
        moduleUrl: pathToFileURL(moduleFile).href,
        parserModuleUrl: pathToFileURL(parserModuleFile).href,
      },
    );
    grammar.push({ id: fixture.id, source: fixture.source, ...result });
  }

  const captureGeometry = async (fixture, run) => {
    const renderPage = await browser.newPage();
    await renderPage.setViewport({ width: 800, height: 600, deviceScaleFactor: 1 });
    try {
      await renderPage.goto(hostPage);
      return await renderPage.evaluate(
      async ({ source, id, moduleUrl, fontUrl }) => {
        const font = new FontFace("Noto Sans", `url(${fontUrl})`);
        await font.load();
        document.fonts.add(font);
        await document.fonts.load("32px 'Noto Sans'");
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({ startOnLoad: false, logLevel: "fatal" });
        const rendered = await mermaid.render(`info-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
        const root = document.querySelector("svg");
        const text = root.querySelector("text.version");
        const attrs = (element) =>
          Object.fromEntries([...element.attributes].map((attr) => [attr.name, attr.value]));
        const bbox = (element) => {
          const value = element.getBBox();
          return { x: value.x, y: value.y, width: value.width, height: value.height };
        };
        const rootClient = root.getBoundingClientRect();
        const computed = getComputedStyle(text);
        const parent = text.parentElement;
        const parentSiblings = [...parent.parentElement.children].filter(
          (element) => element.tagName === parent.tagName,
        );
        const effectiveConfig = mermaid.mermaidAPI.getConfig();
        return {
          config: {
            theme: effectiveConfig.theme ?? "",
            themeCSS: effectiveConfig.themeCSS ?? "",
          },
          root: {
            attrs: attrs(root),
            bbox: bbox(root),
            client: {
              x: rootClient.x,
              y: rootClient.y,
              width: rootClient.width,
              height: rootClient.height,
            },
          },
          childTags: [...root.children].map((child) => child.tagName.toLowerCase()),
          text: {
            attrs: attrs(text),
            value: text.textContent,
            bbox: bbox(text),
            computedTextLength: text.getComputedTextLength(),
            parent: {
              tag: parent.tagName.toLowerCase(),
              typeIndex: parentSiblings.indexOf(parent) + 1,
              childTags: [...parent.children].map((child) => child.tagName.toLowerCase()),
            },
            computed: {
              fill: computed.fill,
              fontFamily: computed.fontFamily,
              fontSize: computed.fontSize,
              fontWeight: computed.fontWeight,
              textAnchor: computed.textAnchor,
            },
          },
          metadata: {
            title: root.querySelector(":scope > title")?.textContent ?? "",
            desc: root.querySelector(":scope > desc")?.textContent ?? "",
            ariaLabelledby: root.getAttribute("aria-labelledby") ?? "",
            role: root.getAttribute("role") ?? "",
          },
        };
      },
      {
        source: fixture.source,
        id: fixture.id,
        moduleUrl: `${pathToFileURL(moduleFile).href}?info-geometry=${fixture.id}-${run}`,
        fontUrl: pathToFileURL(fontFile).href,
      },
      );
    } finally {
      await renderPage.close();
    }
  };

  const geometry = [];
  for (let index = 0; index < renderCases.length; ++index) {
    const fixture = renderCases[index];
    const snapshot = await captureGeometry(fixture, 0);
    const repeated = await captureGeometry(fixture, 1);
    assertEqual(JSON.stringify(repeated), JSON.stringify(snapshot), `${fixture.id} geometry`);
    geometry.push({ id: fixture.id, source: fixture.source, expected: snapshot });
  }

  const provenance = {
    package: "mermaid",
    version: EXPECTED_VERSION,
    moduleSha256: EXPECTED_MODULE_SHA256,
    infoModuleSha256: EXPECTED_INFO_MODULE_SHA256,
    chromeProduct: EXPECTED_CHROME_PRODUCT,
    chromeSha256: EXPECTED_CHROME_SHA256,
    notoSansSha256: EXPECTED_NOTO_SHA256,
    sourceEntry: true,
  };
  writeJson(path.join(fixtureDir, "info-grammar.json"), {
    upstream: provenance,
    oracle: "source-entry detector and Langium InfoGrammar acceptance/diagnostics",
    cases: grammar,
  });
  writeJson(path.join(fixtureDir, "info-geometry.json"), {
    upstream: provenance,
    oracle: "source-entry SVG DOM, used style, fixed viewport, and inert metadata/config",
    cases: geometry,
  });

  const pixelDir = path.join(fixtureDir, "info-pixel");
  fs.mkdirSync(pixelDir, { recursive: true });
  const capturePixel = async (fixture, run) => {
    const renderPage = await browser.newPage();
    await renderPage.setViewport({ width: 800, height: 600, deviceScaleFactor: 1 });
    try {
      await renderPage.goto(hostPage);
      await renderPage.evaluate(
      async ({ source, id, moduleUrl, fontUrl }) => {
        const font = new FontFace("Noto Sans", `url(${fontUrl})`);
        await font.load();
        document.fonts.add(font);
        await document.fonts.load("32px 'Noto Sans'");
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({ startOnLoad: false, logLevel: "fatal" });
        const rendered = await mermaid.render(`info-pixel-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
      },
      {
        source: fixture.source,
        id: fixture.id,
        moduleUrl: `${pathToFileURL(moduleFile).href}?info-pixel=${fixture.id}-${run}`,
        fontUrl: pathToFileURL(fontFile).href,
      },
      );
      const element = await renderPage.$("svg");
      return {
        bytes: await element.screenshot({ omitBackground: true }),
        box: await element.boundingBox(),
      };
    } finally {
      await renderPage.close();
    }
  };

  const pixels = [];
  for (const id of ["default", "dark"]) {
    const fixture = renderCases.find((item) => item.id === id);
    const capture = await capturePixel(fixture, 0);
    const repeated = await capturePixel(fixture, 1);
    assertEqual(sha256(repeated.bytes), sha256(capture.bytes), `${id} pixel bytes`);
    assertEqual(JSON.stringify(repeated.box), JSON.stringify(capture.box), `${id} pixel box`);
    const file = `${id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), capture.bytes);
    pixels.push({
      id,
      source: fixture.source,
      file,
      width: Math.round(capture.box.width),
      height: Math.round(capture.box.height),
      sha256: sha256(capture.bytes),
    });
  }
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream: provenance,
    oracle: "transparent element screenshots at DPR 1",
    cases: pixels,
  });

  const accepted = grammar.filter((item) => item.accept).length;
  console.log(
    `Wrote Info fixtures: ${grammar.length} grammar (${accepted} accept), ${geometry.length} geometry, ${pixels.length} pixel`,
  );
} finally {
  await browser.close();
}
