import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Event Modeling source-entry oracle.

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_EVENT_MODULE_SHA256 =
  "80d536fceb203c1bb77516be6120c3918d70ec89d25ad4496ff54f19301c3c70";
const EXPECTED_PARSER_SHA256 =
  "d0badc6aa49baa2464210edcff0cb778f147639d5f3c5d902d44d442a4c16643";
const EXPECTED_PARSER_EVENT_SHA256 =
  "cdd34483258805545678ec3684db715c9f5d03e3e545dc62a196c7de5f1958d9";
const EXPECTED_PARSER_SUPPORT_SHA256 =
  "65e3ff386104b98fa427f9b335d6425ef03a2f669c2a19ff1f2ce6643b745308";
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
const eventModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.core",
  "diagram-FQU43EPY.mjs",
);
const parserRoot = path.join(path.dirname(mermaidRoot), "@mermaid-js", "parser", "dist");
const parserFile = path.join(parserRoot, "mermaid-parser.esm.mjs");
const parserEventFile = path.join(
  parserRoot,
  "chunks",
  "mermaid-parser.esm",
  "eventmodeling-3TKZ67CO.mjs",
);
const parserSupportFile = path.join(
  parserRoot,
  "chunks",
  "mermaid-parser.esm",
  "chunk-HTDLG6IT.mjs",
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
assertEqual(
  sha256(fs.readFileSync(eventModuleFile)),
  EXPECTED_EVENT_MODULE_SHA256,
  "Event Modeling module",
);
assertEqual(sha256(fs.readFileSync(parserFile)), EXPECTED_PARSER_SHA256, "Parser module");
assertEqual(
  sha256(fs.readFileSync(parserEventFile)),
  EXPECTED_PARSER_EVENT_SHA256,
  "Event Modeling parser entry",
);
assertEqual(
  sha256(fs.readFileSync(parserSupportFile)),
  EXPECTED_PARSER_SUPPORT_SHA256,
  "Event Modeling parser support",
);
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

const canonicalBody = `eventmodeling
entity UI
entity Command
entity Event
entity Read
tf 001 ui UI.Start
tf 002 cmd Command.Submit ->> 001
tf 003 evt Event.Submitted ->> 002
tf 004 rmo Read.Order ->> 003
tf 005 ui UI.History ->> 004`;

const grammarCases = [
  { id: "empty", source: "eventmodeling" },
  { id: "canonical", source: canonicalBody },
  { id: "same-line", source: "eventmodeling tf 1 evt Created" },
  {
    id: "long-keywords",
    source:
      "eventmodeling\nentity App\ntimeframe 1 processor App.Handler\nresetframe 2 readmodel App.View",
  },
  {
    id: "all-entity-types",
    source:
      "eventmodeling\ntf 1 ui A\ntf 2 pcr B\ntf 3 processor C\ntf 4 cmd D\ntf 5 command E\ntf 6 evt F\ntf 7 event G\ntf 8 rmo H\ntf 9 readmodel I",
  },
  { id: "qualified-many", source: "eventmodeling\ntf 001 evt one.two.three" },
  { id: "frame-leading-zero", source: "eventmodeling\ntf 000 evt Zero\ntf 999 evt Max" },
  {
    id: "explicit-multiple-sources",
    source:
      "eventmodeling\ntf 1 ui A\ntf 2 pcr B\ntf 3 cmd C ->> 1 ->> 2",
  },
  {
    id: "reset-frame",
    source:
      "eventmodeling\ntf 1 ui A\ntf 2 cmd B ->> 1\nrf 3 evt C ->> 2\ntf 4 rmo D ->> 3",
  },
  { id: "inline-data-json", source: "eventmodeling\ntf 1 evt Created `json` {\"id\": 1}" },
  { id: "inline-data-string", source: "eventmodeling\ntf 1 evt Created 'hello world'" },
  {
    id: "data-reference",
    source:
      "eventmodeling\ndata payload `json` {\n  \"id\": 1\n}\ntf 1 evt Created [[payload]]",
  },
  {
    id: "data-types",
    source:
      "eventmodeling\ndata a `jsobj` {\n x: 1\n}\ndata b `figma` {\n x\n}\ndata c `salt` {\n x\n}\ndata d `uri` {\n x\n}\ndata e `md` {\n x\n}\ndata f `html` {\n x\n}\ndata g `text` {\n x\n}",
  },
  {
    id: "note-and-gwt",
    source:
      "eventmodeling\nentity A\nentity B\ntf 1 ui A\ntf 2 cmd B ->> 1\nnote 2 {\n note text\n}\ngwt 2 given ui A when cmd B then ui A",
  },
  {
    id: "metadata",
    source:
      "eventmodeling\ntitle Inline title\naccTitle: Accessible\naccDescr: Description\ntf 1 evt Created",
  },
  {
    id: "metadata-block",
    source: "eventmodeling\naccDescr { First line\n  Second line }\ntf 1 evt Created",
  },
  { id: "comments", source: "%% pre\neventmodeling\n// slash\n/* block */\n%% percent\ntf 1 evt A" },
  { id: "frontmatter", source: frontmatter(undefined, "eventmodeling\ntf 1 evt A", "Front") },
  { id: "directive", source: init({ theme: "dark" }, "eventmodeling\ntf 1 evt A") },
  { id: "uppercase-keywords", source: "eventmodeling\nTF 1 EVT A" },
  { id: "duplicate-frame-id", source: "eventmodeling\ntf 1 ui A\ntf 1 cmd B" },
  { id: "duplicate-entity", source: "eventmodeling\nentity A\nentity A\ntf 1 evt A" },
  { id: "unresolved-data", source: "eventmodeling\ntf 1 evt A [[missing]]" },
  { id: "unresolved-source", source: "eventmodeling\ntf 1 cmd A ->> 9" },
  { id: "reject-uppercase-detector", source: "EVENTMODELING\ntf 1 evt A" },
  { id: "reject-prefix", source: "eventmodelingX\ntf 1 evt A" },
  { id: "reject-frame-four-digits", source: "eventmodeling\ntf 1000 evt A" },
  { id: "reject-frame-alpha", source: "eventmodeling\ntf one evt A" },
  { id: "reject-missing-entity", source: "eventmodeling\ntf 1 evt" },
  { id: "reject-inline-bare", source: "eventmodeling\ntf 1 evt A bare" },
  { id: "reject-data-single-line", source: "eventmodeling\ndata x { a: 1 }" },
  { id: "reject-unclosed-data", source: "eventmodeling\ndata x {\n a: 1" },
  { id: "reject-bad-type", source: "eventmodeling\ntf 1 unknown A" },
  {
    id: "reject-validator-command-source",
    source: "eventmodeling\ntf 1 evt A\ntf 2 cmd B ->> 1",
  },
  {
    id: "reject-validator-event-source",
    source: "eventmodeling\ntf 1 ui A\ntf 2 evt B ->> 1",
  },
  {
    id: "reject-validator-readmodel-source",
    source: "eventmodeling\ntf 1 cmd A\ntf 2 rmo B ->> 1",
  },
  { id: "reject-gwt-unknown", source: "eventmodeling\ntf 1 evt A\ngwt 1 given ui Missing then ui Missing" },
];

const geometryCases = [
  { id: "canonical", source: init({ fontFamily: "Noto Sans" }, canonicalBody) },
  { id: "empty", source: init({ fontFamily: "Noto Sans" }, "eventmodeling") },
  {
    id: "namespace-lanes",
    source: init(
      { fontFamily: "Noto Sans" },
      "eventmodeling\ntf 1 ui Web.Start\ntf 2 cmd Sales.Submit ->> 1\ntf 3 evt Orders.Created ->> 2\ntf 4 rmo Sales.View ->> 3\ntf 5 ui Web.Done ->> 4",
    ),
  },
  {
    id: "implicit-relations",
    source: init(
      { fontFamily: "Noto Sans" },
      "eventmodeling\ntf 1 ui A\ntf 2 cmd B\ntf 3 evt C\ntf 4 rmo D\ntf 5 pcr E\ntf 6 ui F",
    ),
  },
  {
    id: "multi-source-reset",
    source: init(
      { fontFamily: "Noto Sans" },
      "eventmodeling\ntf 1 ui A\ntf 2 pcr B\ntf 3 cmd C ->> 1 ->> 2\nrf 4 evt D ->> 3\ntf 5 rmo E ->> 4",
    ),
  },
  {
    id: "long-label",
    source: init(
      { fontFamily: "Noto Sans" },
      "eventmodeling\ntf 1 evt ThisIsAnExtremelyLongEventNameThatExceedsTheMinimumWidthAndWrapsAcrossLines",
    ),
  },
  {
    id: "inline-data",
    source: init(
      { fontFamily: "Noto Sans" },
      "eventmodeling\ntf 1 evt Created `json` {\"id\": 1, \"name\": \"Alpha Beta\"}",
    ),
  },
  {
    id: "data-reference",
    source: init(
      { fontFamily: "Noto Sans" },
      "eventmodeling\ndata payload `json` {\n  \"id\": 1,\n  \"name\": \"Alpha Beta\"\n}\ntf 1 evt Created [[payload]]",
    ),
  },
  {
    id: "alias-types",
    source: init(
      { fontFamily: "Noto Sans" },
      "eventmodeling\ntimeframe 1 processor Handler\ntimeframe 2 command Run ->> 1\ntimeframe 3 event Done ->> 2\ntimeframe 4 readmodel View ->> 3",
    ),
  },
  {
    id: "frontmatter",
    source: frontmatter({ fontFamily: "Noto Sans" }, "eventmodeling\ntf 1 evt Created", "Front"),
  },
];

const styleOverrides = {
  emUiFill: "#110000",
  emUiStroke: "#ff1111",
  emProcessorFill: "#001100",
  emProcessorStroke: "#11ff11",
  emReadModelFill: "#000011",
  emReadModelStroke: "#1111ff",
  emCommandFill: "#111100",
  emCommandStroke: "#ffff11",
  emEventFill: "#110011",
  emEventStroke: "#ff11ff",
  emSwimlaneBackgroundOdd: "#001111",
  emSwimlaneBackgroundStroke: "#11ffff",
  emArrowhead: "#123456",
  emRelationStroke: "#654321",
};
const configCases = [
  { id: "defaults", config: {} },
  { id: "padding", config: { eventmodeling: { padding: 80 } } },
  { id: "padding-zero", config: { eventmodeling: { padding: 0 } } },
  { id: "padding-negative", config: { eventmodeling: { padding: -10 } } },
  { id: "padding-string", config: { eventmodeling: { padding: "12" } } },
  { id: "padding-null", config: { eventmodeling: { padding: null } } },
  { id: "padding-array", config: { eventmodeling: { padding: [12] } } },
  { id: "use-max-width-false", config: { eventmodeling: { useMaxWidth: false } } },
  { id: "use-max-width-string", config: { eventmodeling: { useMaxWidth: "false" } } },
  { id: "row-height-inert", config: { eventmodeling: { rowHeight: 96 } } },
  { id: "use-width-inert", config: { eventmodeling: { useWidth: 900 } } },
  { id: "theme-dark", config: { theme: "dark" } },
  { id: "theme-forest", config: { theme: "forest" } },
  { id: "theme-neutral", config: { theme: "neutral" } },
  { id: "theme-redux-color", config: { theme: "redux-color" } },
  { id: "style-all", config: { themeVariables: styleOverrides } },
  ...Object.entries(styleOverrides).map(([key, value]) => ({
    id: `style-${key}`,
    config: { themeVariables: { [key]: value } },
  })),
  { id: "font-family", config: { fontFamily: "DefinitelyMissing, Noto Sans" } },
  {
    id: "frontmatter-config",
    source: frontmatter(
      { fontFamily: "Noto Sans", eventmodeling: { padding: 45, useMaxWidth: false } },
      canonicalBody,
    ),
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

const provenance = {
  package: "mermaid",
  version: EXPECTED_VERSION,
  moduleSha256: EXPECTED_MODULE_SHA256,
  eventModelingModuleSha256: EXPECTED_EVENT_MODULE_SHA256,
  parserSha256: EXPECTED_PARSER_SHA256,
  parserEventModelingSha256: EXPECTED_PARSER_EVENT_SHA256,
  parserSupportSha256: EXPECTED_PARSER_SUPPORT_SHA256,
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

  const grammar = [];
  for (const fixture of grammarCases) {
    await page.goto(hostPage);
    const result = await page.evaluate(
      async ({ source, caseId, moduleUrl, parserUrl }) => {
        const clean = source.replace(/^---[\s\S]*?---\s*/, "").replace(/^%%\{[\s\S]*?\}%%\s*/, "");
        const pickFrame = (frame) => ({
          kind: frame.$type,
          name: frame.name,
          modelEntityType: frame.modelEntityType,
          entityIdentifier: frame.entityIdentifier,
          sourceFrames: frame.sourceFrames.map((ref) => ref.$refText),
          dataReference: frame.dataReference?.$refText ?? null,
          dataType: frame.dataType ?? null,
          dataInlineValue: frame.dataInlineValue ?? null,
        });
        try {
          const { default: mermaid } = await import(moduleUrl);
          mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
          await mermaid.parse(source);
          const { parse } = await import(parserUrl);
          const ast = await parse("eventmodeling", clean);
          const rendered = await mermaid.render(`grammar-${caseId}`, source);
          document.body.innerHTML = rendered.svg;
          return {
            accept: true,
            ast: {
              title: ast.title ?? "",
              accTitle: ast.accTitle ?? "",
              accDescr: ast.accDescr ?? "",
              modelEntities: ast.modelEntities.map((item) => item.name),
              frames: ast.frames.map(pickFrame),
              dataEntities: ast.dataEntities.map((item) => ({
                name: item.name,
                dataType: item.dataType ?? null,
                dataBlockValue: item.dataBlockValue,
              })),
              notes: ast.noteEntities.map((item) => ({
                sourceFrame: item.sourceFrame.$refText,
                dataType: item.dataType ?? null,
                dataBlockValue: item.dataBlockValue,
              })),
              gwt: ast.gwtEntities.map((item) => ({
                sourceFrame: item.sourceFrame.$refText,
                given: item.givenStatements.map((s) => s.entityIdentifier.$refText),
                when: item.whenStatements.map((s) => s.entityIdentifier.$refText),
                then: item.thenStatements.map((s) => s.entityIdentifier.$refText),
              })),
            },
          };
        } catch (error) {
          const raw = String(error?.message ?? error);
          const message = raw.replace(/\s+/g, " ").trim();
          const location = raw.match(/(?:Lexer|Parse) error on line (\d+|\?), column (\d+|\?)/i);
          const line = Number(location?.[1] === "?" ? 0 : location?.[1] ?? 0);
          const column = Number(location?.[2] === "?" ? 0 : location?.[2] ?? 0);
          let kind = "runtime";
          if (message.startsWith("No diagram type detected")) kind = "no-diagram";
          else if (/Lexer error/i.test(message)) kind = "lexer";
          else if (/Parse error|Parsing failed/i.test(message)) kind = "parser";
          else if (/A (command|event|read model|processor|ui) can only receive input/.test(message))
            kind = "validation";
          else if (/Could not resolve reference/.test(message)) kind = "reference";
          return { accept: false, reject: { kind, message, line, column } };
        }
      },
      {
        source: fixture.source,
        caseId: fixture.id,
        moduleUrl: pathToFileURL(moduleFile).href,
        parserUrl: pathToFileURL(parserFile).href,
      },
    );
    grammar.push({ id: fixture.id, source: fixture.source, ...result });
  }

  const capture = async (source, id) => {
    await page.goto(hostPage);
    return page.evaluate(
      async ({ source, id, moduleUrl, fontUrl }) => {
        const font = new FontFace("Noto Sans", `url(${fontUrl})`);
        await font.load();
        document.fonts.add(font);
        await document.fonts.load("16px 'Noto Sans'");
        await document.fonts.load("bold 16px 'Noto Sans'");
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          deterministicIds: true,
          deterministicIDSeed: "eventmodeling-fixture",
        });
        const rendered = await mermaid.render(`eventmodeling-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
        const root = document.querySelector("svg");
        const attrs = (element) =>
          element
            ? Object.fromEntries([...element.attributes].map((attr) => [attr.name, attr.value]))
            : null;
        const bbox = (element) => {
          if (!element) return null;
          const value = element.getBBox();
          return { x: value.x, y: value.y, width: value.width, height: value.height };
        };
        const style = (element) => {
          if (!element) return null;
          const value = getComputedStyle(element);
          return {
            fill: value.fill,
            stroke: value.stroke,
            strokeWidth: value.strokeWidth,
            fontFamily: value.fontFamily,
            fontSize: value.fontSize,
            fontWeight: value.fontWeight,
            color: value.color,
          };
        };
        const client = root.getBoundingClientRect();
        return {
          root: {
            attrs: attrs(root),
            bbox: bbox(root),
            client: { width: client.width, height: client.height },
          },
          childTags: [...root.children].map((child) => child.tagName.toLowerCase()),
          swimlanes: [...root.querySelectorAll("g.em-swimlane")].map((group) => ({
            bbox: bbox(group),
            rect: { attrs: attrs(group.querySelector("rect")), bbox: bbox(group.querySelector("rect")), computed: style(group.querySelector("rect")) },
            label: { attrs: attrs(group.querySelector("text")), value: group.querySelector("text")?.textContent ?? "", bbox: bbox(group.querySelector("text")), computed: style(group.querySelector("text")) },
          })),
          boxes: [...root.querySelectorAll("g.em-box")].map((group) => {
            const foreign = group.querySelector("foreignObject");
            const span = group.querySelector("span");
            return {
              bbox: bbox(group),
              rect: { attrs: attrs(group.querySelector("rect")), bbox: bbox(group.querySelector("rect")), computed: style(group.querySelector("rect")) },
              foreignObject: { attrs: attrs(foreign), bbox: bbox(foreign) },
              span: { html: span?.innerHTML ?? "", text: span?.textContent ?? "", rect: span ? (() => { const r = span.getBoundingClientRect(); return { x: r.x, y: r.y, width: r.width, height: r.height }; })() : null, computed: style(span) },
            };
          }),
          relations: [...root.querySelectorAll("path.em-relation")].map((path) => ({ attrs: attrs(path), bbox: bbox(path), computed: style(path) })),
          marker: (() => {
            const marker = root.querySelector("defs marker");
            const polygon = marker?.querySelector("polygon");
            return marker ? { attrs: attrs(marker), polygon: { attrs: attrs(polygon), computed: style(polygon) } } : null;
          })(),
          metadata: {
            title: root.querySelector(":scope > title")?.textContent ?? "",
            desc: root.querySelector(":scope > desc")?.textContent ?? "",
            ariaLabelledby: root.getAttribute("aria-labelledby") ?? "",
            ariaDescribedby: root.getAttribute("aria-describedby") ?? "",
            role: root.getAttribute("role") ?? "",
          },
        };
      },
      { source, id, moduleUrl: pathToFileURL(moduleFile).href, fontUrl: pathToFileURL(fontFile).href },
    );
  };

  const geometry = [];
  for (const fixture of geometryCases) {
    geometry.push({ id: fixture.id, source: fixture.source, expected: await capture(fixture.source, `geometry-${fixture.id}`) });
  }

  const config = [];
  for (const fixture of configCases) {
    const source = fixture.source ?? init(
      { fontFamily: "Noto Sans", themeVariables: { fontFamily: "Noto Sans" }, ...fixture.config },
      canonicalBody,
    );
    let expected;
    try {
      expected = { status: "ready", dom: await capture(source, `config-${fixture.id}`) };
    } catch (error) {
      expected = { status: "error", message: String(error?.message ?? error).replace(/\s+/g, " ").trim() };
    }
    config.push({ id: fixture.id, source, expected });
  }

  writeJson(path.join(fixtureDir, "eventmodeling-grammar.json"), {
    upstream: provenance,
    oracle: "source-entry detector, Langium AST, references, validation, metadata, and diagnostics",
    cases: grammar,
  });
  writeJson(path.join(fixtureDir, "eventmodeling-geometry.json"), {
    upstream: provenance,
    oracle: "source-entry SVG swimlanes, boxes, relations, marker, viewBox, and metadata",
    cases: geometry,
  });
  writeJson(path.join(fixtureDir, "eventmodeling-config.json"), {
    upstream: provenance,
    oracle: "source-entry Event Modeling sizing, raw coercion, themes, and fourteen theme variables",
    cases: config,
  });

  const pixelDir = path.join(fixtureDir, "eventmodeling-pixel");
  fs.mkdirSync(pixelDir, { recursive: true });
  const pixelCases = [
    geometryCases.find((item) => item.id === "canonical"),
    { id: "dark", source: init({ theme: "dark", fontFamily: "Noto Sans" }, canonicalBody) },
    { id: "styled", source: init({ fontFamily: "Noto Sans", themeVariables: styleOverrides }, canonicalBody) },
  ];
  const pixels = [];
  for (const fixture of pixelCases) {
    await page.goto(hostPage);
    await page.evaluate(
      async ({ source, id, moduleUrl, fontUrl }) => {
        const font = new FontFace("Noto Sans", `url(${fontUrl})`);
        await font.load();
        document.fonts.add(font);
        await document.fonts.load("16px 'Noto Sans'");
        await document.fonts.load("bold 16px 'Noto Sans'");
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({ startOnLoad: false, securityLevel: "strict", deterministicIds: true, deterministicIDSeed: "eventmodeling-pixel" });
        const rendered = await mermaid.render(`eventmodeling-pixel-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
      },
      { source: fixture.source, id: fixture.id, moduleUrl: pathToFileURL(moduleFile).href, fontUrl: pathToFileURL(fontFile).href },
    );
    const element = await page.$("svg");
    const file = `${fixture.id}.png`;
    const bytes = await element.screenshot({ path: path.join(pixelDir, file), omitBackground: true });
    const box = await element.boundingBox();
    pixels.push({ id: fixture.id, source: fixture.source, file, width: Math.round(box.width), height: Math.round(box.height), sha256: sha256(bytes) });
  }
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream: provenance,
    oracle: "transparent Event Modeling element screenshots at DPR 1",
    cases: pixels,
  });

  const accepted = grammar.filter((item) => item.accept).length;
  console.log(`Wrote Event Modeling fixtures: ${grammar.length} grammar (${accepted} accept), ${geometry.length} geometry, ${config.length} config, ${pixels.length} pixel`);
} finally {
  await browser.close();
}
