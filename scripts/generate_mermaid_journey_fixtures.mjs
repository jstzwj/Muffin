import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Freezes the source-entry contract for Mermaid 11.16.0 user-journey diagrams:
//   tests/fixtures/mermaid/journey-grammar.json
//   tests/fixtures/mermaid/journey-geometry.json
//   tests/fixtures/mermaid/journey-pixel/{default,dark}.png + manifest.json
//
// The generator intentionally records upstream quirks such as fixed y=450
// bounds, the diagramMarginX/taskMargin bounds mismatch, source-inert palette
// arrays, and the three distinct textPlacement paths. Do not normalize them.
//
// Usage:
//   node scripts/generate_mermaid_journey_fixtures.mjs \
//     [mermaid-root] [fixture-dir] [chrome-exe]
//
// Defaults:
//   ../mermaid-cli/node_modules/mermaid
//   tests/fixtures/mermaid
//   C:/Program Files/Google/Chrome/Application/chrome.exe

const EXPECTED_MERMAID_VERSION = "11.16.0";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const VIEWPORT = { width: 1600, height: 1200, deviceScaleFactor: 1 };
const FONT_STACK =
  '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const fixtureDir = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const pixelDir = path.join(fixtureDir, "journey-pixel");

const sha256 = (bytes) =>
  createHash("sha256").update(bytes).digest("hex");
const readJson = (file) => JSON.parse(fs.readFileSync(file, "utf8"));
const writeJson = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = readJson(path.join(mermaidRoot, "package.json"));
if (pkg.version !== EXPECTED_MERMAID_VERSION) {
  throw new Error(
    `Expected Mermaid ${EXPECTED_MERMAID_VERSION}, found ${pkg.version}`,
  );
}
if (!fs.existsSync(chrome)) throw new Error(`Chrome executable not found: ${chrome}`);
const chromeSha256 = sha256(fs.readFileSync(chrome));
if (chromeSha256 !== EXPECTED_CHROME_SHA256) {
  throw new Error(
    `Expected Chrome sha256 ${EXPECTED_CHROME_SHA256}, found ${chromeSha256}`,
  );
}

const notoDir = path.resolve("third_party", "noto", "fonts");
const fontFiles = [
  {
    family: "Noto Sans",
    file: "NotoSans-Regular.ttf",
    sha256: "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5",
  },
  {
    family: "Noto Sans CJK SC",
    file: "NotoSansCJKsc-Regular.otf",
    sha256: "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b",
  },
  {
    family: "Noto Sans Arabic",
    file: "NotoSansArabic-Regular.ttf",
    sha256: "ceea25b464a656dc3b26849bab9356740401af62aedf1bfa8b7f0d9b75925b1b",
  },
  {
    family: "Noto Sans Hebrew",
    file: "NotoSansHebrew-Regular.ttf",
    sha256: "a7fa16fffb27bedb060a0866267c29e9859aeb9c21cc33f5b3aaf6eb062eca85",
  },
];
for (const font of fontFiles) {
  const actual = sha256(fs.readFileSync(path.join(notoDir, font.file)));
  if (actual !== font.sha256) {
    throw new Error(`${font.file}: expected sha256 ${font.sha256}, found ${actual}`);
  }
}
const fontFaces = fontFiles
  .map(
    ({ family, file }) =>
      `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}");font-style:normal;font-weight:400}`,
  )
  .join("\n");

const mermaidModule = pathToFileURL(
  path.join(mermaidRoot, "dist", "mermaid.esm.mjs"),
).href;
const moduleSha256 = sha256(
  fs.readFileSync(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")),
);
const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(
      path.dirname(mermaidRoot),
      "puppeteer",
      "lib",
      "puppeteer",
      "puppeteer.js",
    ),
  )
);

const sourceInit = (config, body) =>
  `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const stableSource = (body, journey = {}, topLevel = {}) =>
  sourceInit(
    {
      ...topLevel,
      fontFamily: FONT_STACK,
      themeVariables: {
        ...(topLevel.themeVariables ?? {}),
        fontFamily: FONT_STACK,
      },
      journey: {
        taskFontFamily: FONT_STACK,
        titleFontFamily: FONT_STACK,
        ...journey,
      },
    },
    body,
  );

const grammarCases = [
  { id: "empty", source: "journey" },
  { id: "empty-newlines", source: "journey\n\n" },
  { id: "bom-crlf", source: "\ufeffjourney\r\nsection A\r\n  task: 5: Me\r\n" },
  {
    id: "canonical",
    source:
      "journey\ntitle My working day\nsection Go to work\n  Make tea: 5: Me\n  Go upstairs: 3: Me\n  Do work: 1: Me\nsection Go home\n  Go downstairs: 5: Me\n  Sit down: 5: Me",
  },
  {
    id: "accessibility",
    source:
      "journey\naccTitle:  Journey title  \naccDescr:  A concise description  \nsection A\n  task: 5",
  },
  {
    id: "accessibility-multiline",
    source:
      "journey\naccDescr {\n  first line\n  second line\n}\nsection A\n  task: 5",
  },
  {
    id: "title-last-wins-whitespace",
    source:
      "journey\ntitle First\ntitle    Second  \nsection A\n  task: 5",
  },
  {
    id: "section-trailing-whitespace",
    source: "journey\nsection A   \n  task: 5",
  },
  {
    id: "empty-section-and-adjacent-duplicate",
    source:
      "journey\nsection Empty\nsection A\n  one: 5\nsection A\n  two: 4",
  },
  {
    id: "nonadjacent-duplicate-section",
    source:
      "journey\nsection A\n  one: 5\nsection B\n  two: 4\nsection A\n  three: 3",
  },
  { id: "task-before-section", source: "journey\n  task: 5: Me" },
  { id: "score-only", source: "journey\nsection A\n  task: 4" },
  {
    id: "people-trim-duplicates",
    source: "journey\nsection A\n  task: 5:  Bob , Alice, Bob  ",
  },
  {
    id: "extra-colons-dropped",
    source: "journey\nsection A\n  task: 5: Alice: ignored: again",
  },
  { id: "empty-actor", source: "journey\nsection A\n  task: 5:" },
  {
    id: "actors-utf16-sort",
    source:
      "journey\nsection A\n  task: 5: Zed, Alice, \u00e9, \ud83d\ude00, \ud83d\udca9",
  },
  {
    id: "actor-prototype-name",
    source: "journey\nsection A\n  task: 5: __proto__, constructor",
  },
  { id: "score-zero", source: "journey\nsection A\n  task: 0" },
  { id: "score-negative-zero", source: "journey\nsection A\n  task: -0" },
  { id: "score-negative", source: "journey\nsection A\n  task: -2" },
  { id: "score-fraction", source: "journey\nsection A\n  task: 2.5" },
  { id: "score-over-five", source: "journey\nsection A\n  task: 6" },
  { id: "score-plus", source: "journey\nsection A\n  task: +3" },
  { id: "score-hex", source: "journey\nsection A\n  task: 0x10" },
  { id: "score-binary", source: "journey\nsection A\n  task: 0b11" },
  { id: "score-octal", source: "journey\nsection A\n  task: 0o10" },
  { id: "score-exponent", source: "journey\nsection A\n  task: 1e2" },
  { id: "score-infinity", source: "journey\nsection A\n  task: Infinity" },
  { id: "score-nan", source: "journey\nsection A\n  task: NaN" },
  { id: "score-css-garbage", source: "journey\nsection A\n  task: 1px" },
  {
    id: "score-whitespace-is-zero",
    source: "journey\nsection A\n  blank:   \n  after: 3",
  },
  {
    id: "comments-hash-percent",
    source:
      "journey\n# hash comment\n% percent comment\nsection A # tail\n  task: 5: Me # tail",
  },
  {
    id: "comments-double-percent",
    source:
      "journey\n%% full line\nsection A\n  task: 5: Me %% trailing\n  after: 4",
  },
  {
    id: "body-keywords-case-insensitive",
    source: "journey\nTITLE Upper title\nSECTION Upper section\n  task: 5",
  },
  {
    id: "quoted-task-is-literal",
    source: 'journey\nsection A\n  "quoted task": 5',
  },
  {
    id: "text-placement-fo",
    source: sourceInit(
      { journey: { textPlacement: "fo" } },
      "journey\nsection A\n  alpha<br>beta: 5",
    ),
    renderProbe: true,
    expectedTextMode: "fo",
  },
  {
    id: "text-placement-old",
    source: sourceInit(
      { journey: { textPlacement: "old" } },
      "journey\nsection A\n  alpha<br>beta: 5",
    ),
    renderProbe: true,
    expectedTextMode: "old",
  },
  {
    id: "text-placement-tspan",
    source: sourceInit(
      { journey: { textPlacement: "tspan" } },
      "journey\nsection A\n  alpha<br>beta: 5",
    ),
    renderProbe: true,
    expectedTextMode: "tspan",
  },
  {
    id: "text-placement-unknown-falls-to-tspan",
    source: sourceInit(
      { journey: { textPlacement: "unexpected" } },
      "journey\nsection A\n  alpha<br>beta: 5",
    ),
    renderProbe: true,
    expectedTextMode: "tspan",
  },
  {
    id: "text-placement-false-falls-to-tspan",
    source: sourceInit(
      { journey: { textPlacement: false } },
      "journey\nsection A\n  alpha<br>beta: 5",
    ),
    renderProbe: true,
    expectedTextMode: "tspan",
  },
  { id: "reject-no-header", source: "section A\n  task: 5", reject: true },
  { id: "reject-uppercase-header", source: "JOURNEY\nsection A\n  task: 5", reject: true },
  { id: "reject-header-suffix", source: "journeySuffix\nsection A\n  task: 5", reject: true },
  { id: "reject-section-colon", source: "journey\nsection A: B\n  task: 5", reject: true },
  { id: "reject-semicolon-task", source: "journey\nsection A\n  task;semi: 5", reject: true },
  { id: "reject-semicolon-separator", source: "journey; section A", reject: true },
  { id: "reject-task-missing-colon", source: "journey\nsection A\n  task 5 Me", reject: true },
  { id: "reject-invalid-token", source: "journey\n@@@", reject: true },
  { id: "reject-unclosed-accdescr", source: "journey\naccDescr {never closes", reject: true },
];

const CANONICAL_BODY =
  "journey\n" +
  "title My working day\n" +
  "section Go to work\n" +
  "  Make tea: 5: Me\n" +
  "  Go upstairs: 3: Me\n" +
  "  Do work: 1: Me\n" +
  "section Go home\n" +
  "  Go downstairs: 5: Me\n" +
  "  Sit down: 5: Me";
const ONE_TASK_BODY = "journey\nsection A\n  task: 5: Me";
const TWO_TASK_BODY = "journey\nsection A\n  first: 5: Me\n  second: 2: Me";

const geometryCases = [
  { id: "canonical", source: stableSource(CANONICAL_BODY) },
  { id: "empty", source: stableSource("journey") },
  { id: "title-only", source: stableSource("journey\ntitle Title only") },
  { id: "task-before-section", source: stableSource("journey\n  orphan: 3: Me") },
  {
    id: "section-grouping",
    source: stableSource(
      "journey\nsection A\n  one: 5\nsection A\n  two: 4\nsection B\n  three: 3\nsection A\n  four: 2",
    ),
  },
  {
    id: "section-cycle-seven",
    source: stableSource(
      "journey\n" +
        Array.from({ length: 8 }, (_, i) =>
          `section S${i}\n  task${i}: ${i % 6}`,
        ).join("\n"),
    ),
  },
  {
    id: "actor-sort-cycle-duplicates",
    source: stableSource(
      "journey\nsection A\n  task: 5: Golf, Alpha, Foxtrot, Bravo, Echo, Charlie, Delta, Alpha",
    ),
  },
  {
    id: "actor-wrap",
    source: stableSource(
      "journey\nsection A\n  task: 5: Alexander Supercalifragilisticexpialidocious",
      { maxLabelWidth: 55 },
    ),
  },
  {
    id: "scores",
    source: stableSource(
      "journey\nsection Scores\n  zero: 0\n  sad: 2.5\n  neutral: 3\n  smile: 4\n  high: 6\n  negative: -2\n  nan: NaN",
    ),
  },
  { id: "config-base", source: stableSource(TWO_TASK_BODY) },
  {
    id: "config-diagram-margin-x",
    source: stableSource(TWO_TASK_BODY, { diagramMarginX: 17 }),
  },
  {
    id: "config-diagram-margin-y",
    source: stableSource(TWO_TASK_BODY, { diagramMarginY: 23 }),
  },
  {
    id: "config-left-margin",
    source: stableSource(TWO_TASK_BODY, { leftMargin: 211 }),
  },
  {
    id: "config-max-label-width",
    source: stableSource(
      "journey\nsection A\n  task: 5: Alexander Beckett",
      { maxLabelWidth: 30 },
    ),
  },
  {
    id: "config-width",
    source: stableSource(TWO_TASK_BODY, { width: 93 }),
  },
  {
    id: "config-height",
    source: stableSource(TWO_TASK_BODY, { height: 71 }),
  },
  {
    id: "config-task-margin",
    source: stableSource(TWO_TASK_BODY, { taskMargin: 19 }),
  },
  {
    id: "config-box-text-margin",
    source: stableSource(TWO_TASK_BODY, { boxTextMargin: 20 }),
  },
  {
    id: "config-task-vs-diagram-margin",
    source: stableSource(TWO_TASK_BODY, {
      width: 93,
      taskMargin: 19,
      diagramMarginX: 17,
    }),
  },
  {
    id: "config-use-max-width-false",
    source: stableSource(TWO_TASK_BODY, { useMaxWidth: false }),
  },
  {
    id: "text-placement-fo",
    source: stableSource(
      "journey\nsection Long section label\n  alpha beta gamma delta epsilon: 5",
      { width: 105, textPlacement: "fo" },
    ),
  },
  {
    id: "text-placement-tspan",
    source: stableSource(
      "journey\nsection alpha<br>beta\n  gamma<br>delta: 5",
      { textPlacement: "tspan", taskFontSize: 20 },
    ),
  },
  {
    id: "text-placement-old",
    source: stableSource(
      "journey\nsection alpha<br>beta\n  gamma<br>delta: 5",
      { textPlacement: "old" },
    ),
  },
];

const pixelCases = [
  {
    id: "default",
    theme: "default",
    renderId: "journey-pixel-default",
    source: stableSource(CANONICAL_BODY),
  },
  {
    id: "dark",
    theme: "dark",
    renderId: "journey-pixel-dark",
    source: stableSource(CANONICAL_BODY, {}, { theme: "dark" }),
  },
];

function canonicalPng(bytes) {
  const image = PNG.sync.read(bytes);
  for (let offset = 0; offset < image.data.length; offset += 4) {
    if (image.data[offset + 3] === 0) image.data.fill(0, offset, offset + 3);
  }
  return PNG.sync.write(image, {
    colorType: 6,
    inputColorType: 6,
    bitDepth: 8,
  });
}

fs.mkdirSync(fixtureDir, { recursive: true });
fs.mkdirSync(pixelDir, { recursive: true });

const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: [
    "--allow-file-access-from-files",
    "--disable-gpu",
    "--disable-lcd-text",
    "--font-render-hinting=none",
    "--force-color-profile=srgb",
    "--hide-scrollbars",
    "--lang=en-US",
  ],
});

const browserProduct = await browser.version();
if (browserProduct !== EXPECTED_CHROME_PRODUCT) {
  await browser.close();
  throw new Error(
    `Expected ${EXPECTED_CHROME_PRODUCT}, found ${browserProduct}`,
  );
}

const upstream = {
  package: "mermaid",
  version: EXPECTED_MERMAID_VERSION,
  module: "dist/mermaid.esm.mjs",
  moduleSha256,
  browser: {
    product: browserProduct,
    executableSha256: chromeSha256,
    headlessMode: "new",
  },
  viewport: VIEWPORT,
  fonts: fontFiles,
  fontFamily: FONT_STACK,
  generator: "scripts/generate_mermaid_journey_fixtures.mjs",
};

async function preparePage() {
  const page = await browser.newPage();
  await page.setViewport(VIEWPORT);
  await page.goto(
    pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href,
  );
  await page.evaluate(
    async ({ fontFaces: css }) => {
      document.documentElement.setAttribute("lang", "en");
      document.body.style.margin = "0";
      const style = document.createElement("style");
      style.id = "journey-oracle-fonts";
      style.textContent = css;
      document.head.appendChild(style);
      await Promise.all([
        document.fonts.load('16px "Noto Sans"', "Journey 0123456789"),
        document.fonts.load('16px "Noto Sans CJK SC"', "\u65c5\u7a0b"),
        document.fonts.load('16px "Noto Sans Arabic"', "\u0631\u062d\u0644\u0629"),
        document.fonts.load('16px "Noto Sans Hebrew"', "\u05de\u05e1\u05e2"),
      ]);
      await document.fonts.ready;
    },
    { fontFaces },
  );
  return page;
}

try {
  // Grammar and DB fixture. The accepted state comes directly from journeyDb;
  // no source regex attempts to reconstruct parser behavior.
  const grammarPage = await preparePage();
  const grammar = await grammarPage.evaluate(
    async ({ cases, fontFamily, mermaidModule: moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      const initialize = () =>
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          look: "classic",
          fontFamily,
          themeVariables: { fontFamily },
        });
      initialize();
      const bootstrap = await mermaid.mermaidAPI.getDiagramFromText(
        "journey\nsection Bootstrap\n  task: 3",
      );
      const generated = bootstrap.parser.parser;
      const originalPerformAction = generated.performAction;
      let reductions;
      generated.performAction = function (...args) {
        reductions?.add(args[4]);
        return originalPerformAction.apply(this, args);
      };

      const numberValue = (value) => {
        if (Number.isNaN(value)) return { kind: "nan" };
        if (value === Infinity) return { kind: "infinity", sign: 1 };
        if (value === -Infinity) return { kind: "infinity", sign: -1 };
        return {
          kind: "finite",
          value,
          ...(Object.is(value, -0) ? { negativeZero: true } : {}),
        };
      };
      const normalizeDb = (diagram) => {
        const db = diagram.db;
        const tasks = db.getTasks().map((task) => ({
          section: task.section,
          type: task.type,
          task: task.task,
          score: numberValue(task.score),
          people: [...task.people],
        }));
        return {
          title: db.getDiagramTitle?.() ?? "",
          accTitle: db.getAccTitle?.() ?? "",
          accDescription: db.getAccDescription?.() ?? "",
          sections: [...db.getSections()],
          tasks,
          actors: [...db.getActors()],
        };
      };
      const classify = (error) => {
        const message = String(error?.message ?? error);
        if (message.startsWith("No diagram type detected")) return "no-diagram";
        if (message.includes("Lexical error") || message.includes("Lexer error"))
          return "lexer";
        if (message.includes("Parse error") || message.includes("Parsing failed"))
          return "parser";
        return "runtime";
      };
      const renderContract = async (fixture) => {
        initialize();
        const renderId = `journey-grammar-${fixture.id}`;
        const { svg } = await mermaid.render(renderId, fixture.source);
        document.getElementById("container").innerHTML = svg;
        await document.fonts.ready;
        await new Promise((resolve) =>
          requestAnimationFrame(() => requestAnimationFrame(resolve)),
        );
        const root = document.querySelector("svg");
        const taskGroup = root.querySelector("rect.task")?.parentElement ?? null;
        const sectionGroup =
          root.querySelector("rect.journey-section")?.parentElement ?? null;
        const mode = (group, selector) => {
          if (!group) return null;
          if (group.querySelector("foreignObject")) return "fo";
          const texts = [...group.querySelectorAll(selector)];
          return texts.some((text) => text.querySelector("tspan"))
            ? "tspan"
            : "old";
        };
        return {
          renderId,
          taskMode: mode(taskGroup, "text.task"),
          sectionMode: mode(sectionGroup, "text.journey-section"),
          foreignObjectCount: root.querySelectorAll("foreignObject").length,
          taskTextNodes: root.querySelectorAll("text.task").length,
          taskTspans: root.querySelectorAll("text.task tspan").length,
          taskTextContent:
            taskGroup?.querySelector("foreignObject .label")?.textContent ??
            [...(taskGroup?.querySelectorAll("text.task") ?? [])]
              .map((node) => node.textContent)
              .join("|"),
        };
      };

      const output = [];
      for (const fixture of cases) {
        initialize();
        reductions = new Set();
        try {
          bootstrap.db.clear();
          const diagram = await mermaid.mermaidAPI.getDiagramFromText(
            fixture.source,
          );
          const result = {
            id: fixture.id,
            source: fixture.source,
            accept: true,
            reductions: [...reductions].sort((a, b) => a - b),
            expectedDb: normalizeDb(diagram),
          };
          if (fixture.renderProbe) {
            result.expectedRender = await renderContract(fixture);
            if (result.expectedRender.taskMode !== fixture.expectedTextMode) {
              throw new Error(
                `${fixture.id}: expected ${fixture.expectedTextMode}, got ${result.expectedRender.taskMode}`,
              );
            }
          }
          output.push(result);
        } catch (error) {
          output.push({
            id: fixture.id,
            source: fixture.source,
            accept: false,
            reductions: [...reductions].sort((a, b) => a - b),
            reject: {
              class: classify(error),
              firstLine: String(error?.message ?? error).split(/\r?\n/)[0],
              token: error?.hash?.token ?? null,
              line: error?.hash?.loc?.first_line ?? null,
              column: error?.hash?.loc?.first_column ?? null,
            },
          });
        }
      }
      reductions = undefined;
      generated.performAction = originalPerformAction;
      return output;
    },
    { cases: grammarCases, fontFamily: FONT_STACK, mermaidModule },
  );
  await grammarPage.close();

  for (const fixture of grammarCases) {
    const actual = grammar.find((value) => value.id === fixture.id);
    if (!actual || actual.accept === Boolean(fixture.reject)) {
      throw new Error(
        `${fixture.id}: expected ${fixture.reject ? "reject" : "accept"}, got ${actual?.accept ? "accept" : "reject"}`,
      );
    }
  }
  const grammarFixture = {
    upstream,
    oracle:
      "journey jison accept/reject, parser reductions, and direct journeyDb state",
    notes: [
      "Scores preserve JavaScript Number semantics with explicit nan/infinity/negativeZero tags.",
      "Actors are the direct getActors() result and therefore use JavaScript UTF-16 default sort order.",
      "textPlacement is strict: fo and old select those paths; every other value selects tspan.",
      "Source-entry array overrides are intentionally outside this grammar corpus because Mermaid sanitization removes them.",
    ],
    cases: grammar,
  };
  writeJson(path.join(fixtureDir, "journey-grammar.json"), grammarFixture);

  // Geometry fixture. It records attributes and computed styles separately so
  // tests can preserve SVG/CSS cascade behavior (not just final colors).
  const geometryPage = await preparePage();
  const geometry = await geometryPage.evaluate(
    async ({ cases, fontFamily, mermaidModule: moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      const round = (value) =>
        Number.isFinite(value) ? Math.round(value * 1000) / 1000 : null;
      const attrs = (element, names) =>
        Object.fromEntries(
          names
            .map((name) => [name, element?.getAttribute(name) ?? null])
            .filter(([, value]) => value !== null),
        );
      const box = (value) => ({
        x: round(value.x),
        y: round(value.y),
        width: round(value.width),
        height: round(value.height),
      });
      const bbox = (element) => (element ? box(element.getBBox()) : null);
      const relativeClientBox = (element, rootRect) => {
        if (!element) return null;
        const value = element.getBoundingClientRect();
        return {
          x: round(value.x - rootRect.x),
          y: round(value.y - rootRect.y),
          width: round(value.width),
          height: round(value.height),
        };
      };
      const computed = (element, names) => {
        if (!element) return null;
        const style = getComputedStyle(element);
        return Object.fromEntries(names.map((name) => [name, style.getPropertyValue(name)]));
      };
      const paint = (element) =>
        computed(element, ["fill", "stroke", "stroke-width", "opacity"]);
      const textPaint = (element) =>
        computed(element, [
          "fill",
          "color",
          "font-family",
          "font-size",
          "font-weight",
          "text-anchor",
          "dominant-baseline",
        ]);
      const textNode = (element, rootRect, detailed = false) => ({
        text: element.textContent ?? "",
        attrs: attrs(element, [
          "x",
          "y",
          "fill",
          "class",
          "dominant-baseline",
          "alignment-baseline",
          "font-size",
          "font-family",
          "font-weight",
        ]),
        computed: textPaint(element),
        ...(detailed
          ? {
              bbox: bbox(element),
              clientBox: relativeClientBox(element, rootRect),
            }
          : {}),
        tspans: [...element.querySelectorAll(":scope > tspan")].map((tspan) => ({
          text: tspan.textContent ?? "",
          attrs: attrs(tspan, ["x", "y", "dx", "dy"]),
          ...(detailed ? { bbox: bbox(tspan) } : {}),
        })),
      });
      const label = (group, selector, rootRect, detailed = false) => {
        const foreign = group?.querySelector("foreignObject") ?? null;
        const texts = [...(group?.querySelectorAll(selector) ?? [])];
        if (foreign) {
          const visible = foreign.querySelector(".label");
          const range = document.createRange();
          if (visible) range.selectNodeContents(visible);
          const lineRects = visible
            ? [...range.getClientRects()].map((value) => ({
                x: round(value.x - rootRect.x),
                y: round(value.y - rootRect.y),
                width: round(value.width),
                height: round(value.height),
              }))
            : [];
          return {
            mode: "fo",
            text: visible?.textContent ?? "",
            visibleComputed: textPaint(visible),
            ...(detailed
              ? {
                  foreignAttrs: attrs(foreign, [
                    "x",
                    "y",
                    "width",
                    "height",
                    "position",
                  ]),
                  foreignClientBox: relativeClientBox(foreign, rootRect),
                  visibleClientBox: relativeClientBox(visible, rootRect),
                  lineRects,
                  fallbackTexts: texts.map((text) =>
                    textNode(text, rootRect, true),
                  ),
                }
              : {}),
          };
        }
        return {
          mode: texts.some((text) => text.querySelector("tspan"))
            ? "tspan"
            : "old",
          text: texts.map((text) => text.textContent ?? "").join("|"),
          texts: texts.map((text) => textNode(text, rootRect, detailed)),
        };
      };

      const output = [];
      for (const fixture of cases) {
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          look: "classic",
          fontFamily,
          themeVariables: { fontFamily },
        });
        const renderId = `journey-geometry-${fixture.id}`;
        const { svg } = await mermaid.render(renderId, fixture.source);
        const container = document.getElementById("container");
        container.innerHTML = svg;
        await document.fonts.ready;
        await new Promise((resolve) =>
          requestAnimationFrame(() => requestAnimationFrame(resolve)),
        );
        const root = container.querySelector("svg");
        const rootRect = root.getBoundingClientRect();
        const detailedText = fixture.id.startsWith("text-placement-");
        const detailedLegend = [
          "actor-wrap",
          "config-max-label-width",
          "config-box-text-margin",
        ].includes(fixture.id);
        const sections = [...root.querySelectorAll("rect.journey-section")].map(
          (rect) => {
            const group = rect.parentElement;
            return {
              rect: {
                attrs: attrs(rect, [
                  "x",
                  "y",
                  "width",
                  "height",
                  "rx",
                  "ry",
                  "fill",
                  "stroke",
                  "class",
                ]),
                computed: paint(rect),
              },
              label: label(
                group,
                "text.journey-section",
                rootRect,
                detailedText,
              ),
            };
          },
        );
        const tasks = [...root.querySelectorAll("rect.task")].map((rect) => {
          const group = rect.parentElement;
          const face = group.querySelector("circle.face");
          const taskLine = group.querySelector("line.task-line");
          const mouth = group.querySelector("path.mouth, line.mouth");
          const eyes = [...group.querySelectorAll('circle[r="1.5"]')];
          const people = [...group.querySelectorAll('circle[class*="actor-"]')]
            .filter((circle) => circle.querySelector("title"))
            .map((circle) => ({
              attrs: attrs(circle, ["cx", "cy", "r", "fill", "stroke", "class"]),
              title: circle.querySelector("title")?.textContent ?? "",
            }));
          return {
            rect: {
              attrs: attrs(rect, [
                "x",
                "y",
                "width",
                "height",
                "rx",
                "ry",
                "fill",
                "stroke",
                "class",
              ]),
              computed: paint(rect),
            },
            taskLine: {
              attrs: attrs(taskLine, [
                "id",
                "x1",
                "y1",
                "x2",
                "y2",
                "stroke",
                "stroke-width",
                "stroke-dasharray",
                "class",
              ]),
              computed: paint(taskLine),
            },
            face: {
              attrs: attrs(face, ["cx", "cy", "r", "stroke-width", "overflow"]),
              computed: paint(face),
            },
            eyes: eyes.map((eye) => ({
              attrs: attrs(eye, ["cx", "cy", "r", "fill", "stroke", "stroke-width"]),
            })),
            mouth: mouth
              ? {
                  tag: mouth.tagName.toLowerCase(),
                  attrs: attrs(mouth, [
                    "d",
                    "transform",
                    "x1",
                    "y1",
                    "x2",
                    "y2",
                    "fill",
                    "stroke",
                    "stroke-width",
                    "class",
                  ]),
                  computed: paint(mouth),
                }
              : null,
            people,
            label: label(group, "text.task", rootRect, detailedText),
          };
        });
        const legendCircles = [...root.children]
          .filter(
            (element) =>
              element.tagName?.toLowerCase() === "circle" &&
              /(?:^|\s)actor-/.test(element.getAttribute("class") ?? "") &&
              !element.querySelector("title"),
          )
          .map((circle) => ({
            attrs: attrs(circle, ["cx", "cy", "r", "fill", "stroke", "class"]),
          }));
        const legendTexts = [...root.children]
          .filter(
            (element) =>
              element.tagName?.toLowerCase() === "text" &&
              /(?:^|\s)legend(?:\s|$)/.test(element.getAttribute("class") ?? ""),
          )
          .map((text) => textNode(text, rootRect, detailedLegend));
        const title = [...root.children].find(
          (element) =>
            element.tagName?.toLowerCase() === "text" &&
            element.getAttribute("font-weight") === "bold" &&
            element.getAttribute("y") === "25",
        );
        const bottomLine = [...root.children].find(
          (element) =>
            element.tagName?.toLowerCase() === "line" &&
            !element.classList.contains("task-line"),
        );
        const marker = root.querySelector("defs marker");
        const markerPath = marker?.querySelector("path") ?? null;
        output.push({
          id: fixture.id,
          source: fixture.source,
          renderId,
          expected: {
            root: {
              attrs: attrs(root, [
                "width",
                "height",
                "style",
                "viewBox",
                "preserveAspectRatio",
                "role",
                "aria-roledescription",
                "aria-labelledby",
                "aria-describedby",
              ]),
              computed: computed(root, [
                "font-family",
                "font-size",
                "color",
                "width",
                "height",
              ]),
              clientBox: box(rootRect),
            },
            sections,
            tasks,
            actorLegend: { circles: legendCircles, texts: legendTexts },
            title: title ? textNode(title, rootRect, true) : null,
            bottomLine: bottomLine
              ? {
                  attrs: attrs(bottomLine, [
                    "x1",
                    "y1",
                    "x2",
                    "y2",
                    "stroke",
                    "stroke-width",
                    "marker-end",
                  ]),
                  computed: paint(bottomLine),
                }
              : null,
            marker: marker
              ? {
                  attrs: attrs(marker, [
                    "id",
                    "refX",
                    "refY",
                    "markerWidth",
                    "markerHeight",
                    "markerUnits",
                    "orient",
                  ]),
                  path: {
                    attrs: attrs(markerPath, ["d", "fill", "stroke"]),
                    computed: paint(markerPath),
                  },
                }
              : null,
          },
        });
      }
      return output;
    },
    { cases: geometryCases, fontFamily: FONT_STACK, mermaidModule },
  );

  const deterministic = await geometryPage.evaluate(
    async ({ fontFamily, mermaidModule: moduleUrl, source }) => {
      const { default: mermaid } = await import(moduleUrl);
      const render = async () => {
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          look: "classic",
          fontFamily,
          themeVariables: { fontFamily },
        });
        return (await mermaid.render("journey-geometry-determinism", source)).svg;
      };
      const first = await render();
      const second = await render();
      return { equal: first === second, length: first.length };
    },
    { fontFamily: FONT_STACK, mermaidModule, source: geometryCases[0].source },
  );
  await geometryPage.close();
  if (!deterministic.equal) throw new Error("Journey SVG is not byte deterministic");

  const byId = (id) => geometry.find((value) => value.id === id)?.expected;
  const canonical = byId("canonical");
  if (
    canonical.root.attrs.viewBox !== "0 -25 1300 540" ||
    canonical.root.attrs.height !== "565" ||
    canonical.sections.length !== 2 ||
    canonical.tasks.length !== 5 ||
    canonical.actorLegend.circles.length !== 1
  ) {
    throw new Error("Canonical Journey geometry contract changed");
  }
  const empty = byId("empty");
  if (
    empty.root.attrs.viewBox !== "0 -25 400 20" ||
    empty.root.attrs.height !== "45" ||
    empty.sections.length !== 0 ||
    empty.tasks.length !== 0
  ) {
    throw new Error("Empty Journey geometry contract changed");
  }
  if (byId("task-before-section").sections.length !== 0) {
    throw new Error("A pre-section Journey task unexpectedly drew a section");
  }
  if (byId("text-placement-fo").tasks[0].label.mode !== "fo") {
    throw new Error("textPlacement fo contract changed");
  }
  if (byId("text-placement-tspan").tasks[0].label.mode !== "tspan") {
    throw new Error("textPlacement tspan contract changed");
  }
  if (byId("text-placement-old").tasks[0].label.mode !== "old") {
    throw new Error("textPlacement old contract changed");
  }
  const cycleSections = byId("section-cycle-seven").sections;
  if (
    cycleSections.length !== 8 ||
    cycleSections[0].rect.attrs.class !== "journey-section section-type-0" ||
    cycleSections[7].rect.attrs.class !== "journey-section section-type-0"
  ) {
    throw new Error("Journey seven-section color cycle contract changed");
  }
  const cycleActors = byId("actor-sort-cycle-duplicates").actorLegend.circles;
  if (
    cycleActors.length !== 7 ||
    cycleActors[0].attrs.fill !== cycleActors[6].attrs.fill ||
    cycleActors[6].attrs.class !== "actor-6"
  ) {
    throw new Error("Journey six-actor color cycle contract changed");
  }
  if (
    byId("config-box-text-margin").actorLegend.texts[0].tspans[0].attrs.x !==
    "80"
  ) {
    throw new Error("boxTextMargin no longer controls legend tspan x");
  }
  const geometryFixture = {
    upstream,
    oracle:
      "Journey renderer SVG attributes, computed styles, visible text layout, and fixed canvas geometry",
    notes: [
      "All coordinates are direct DOM/SVG observations rounded to 0.001; raw SVG attributes remain strings.",
      "foreignObject visible text and its hidden tspan fallback are recorded separately.",
      "Root viewBox height and root height intentionally differ by 25 pixels.",
      "Section width uses diagramMarginX while task x spacing uses taskMargin; task bounds also use diagramMarginX instead of rect width.",
      "Presentation attributes and computed paint can differ because Journey CSS overrides line strokes and fillType classes.",
    ],
    deterministicSvg: deterministic,
    cases: geometry,
  };
  writeJson(path.join(fixtureDir, "journey-geometry.json"), geometryFixture);

  async function capturePixel(fixture) {
    const page = await preparePage();
    await page.evaluate(
      async ({ fixture: value, fontFamily, mermaidModule: moduleUrl }) => {
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: value.theme,
          look: "classic",
          fontFamily,
          themeVariables: { fontFamily },
        });
        const { svg } = await mermaid.render(value.renderId, value.source);
        document.getElementById("container").innerHTML = svg;
        await document.fonts.ready;
        await new Promise((resolve) =>
          requestAnimationFrame(() => requestAnimationFrame(resolve)),
        );
      },
      { fixture, fontFamily: FONT_STACK, mermaidModule },
    );
    const element = await page.$("svg");
    const bytes = canonicalPng(
      await element.screenshot({ omitBackground: true }),
    );
    await page.close();
    return bytes;
  }

  const manifestCases = [];
  for (const fixture of pixelCases) {
    const first = await capturePixel(fixture);
    const second = await capturePixel(fixture);
    const firstHash = sha256(first);
    const secondHash = sha256(second);
    if (firstHash !== secondHash || !first.equals(second)) {
      throw new Error(`${fixture.id}: pixel rendering is not byte deterministic`);
    }
    const image = PNG.sync.read(first);
    if (image.width !== 1300 || image.height !== 565) {
      throw new Error(
        `${fixture.id}: expected 1300x565, got ${image.width}x${image.height}`,
      );
    }
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), first);
    manifestCases.push({
      id: fixture.id,
      theme: fixture.theme,
      dpr: VIEWPORT.deviceScaleFactor,
      viewport: { width: VIEWPORT.width, height: VIEWPORT.height },
      renderId: fixture.renderId,
      source: fixture.source,
      file,
      width: image.width,
      height: image.height,
      sha256: firstHash,
    });
  }
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream,
    oracle: "Headless Chrome element screenshots with transparent background",
    notes: [
      "Every case is rendered twice and must produce byte-identical canonical RGBA PNG data.",
      "Transparent RGB channels are zeroed before encoding.",
      "The source itself declares Noto fonts and the dark theme so the native source-entry pipeline can reproduce each golden.",
    ],
    cases: manifestCases,
  });

  const accepts = grammar.filter((value) => value.accept).length;
  console.log(
    `grammar: ${accepts}/${grammar.length} accept -> tests/fixtures/mermaid/journey-grammar.json`,
  );
  console.log(
    `geometry: ${geometry.length} cases -> tests/fixtures/mermaid/journey-geometry.json`,
  );
  console.log(
    `pixels: ${manifestCases.length} deterministic cases -> tests/fixtures/mermaid/journey-pixel`,
  );
} finally {
  await browser.close();
}
