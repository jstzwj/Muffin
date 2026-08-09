import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0 radar-beta source-entry behavior:
//   tests/fixtures/mermaid/radar-grammar.json
//   tests/fixtures/mermaid/radar-geometry.json
//   tests/fixtures/mermaid/radar-config.json
//   tests/fixtures/mermaid/radar-pixel/*.png + manifest.json
//
// Usage:
//   node scripts/generate_mermaid_radar_fixtures.mjs \
//     [mermaid-root] [fixture-dir] [chrome-exe]

const EXPECTED_MERMAID_VERSION = "11.16.0";
const EXPECTED_MERMAID_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA256 =
  "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";
const VIEWPORT = { width: 1600, height: 1200, deviceScaleFactor: 1 };
const FONT_FAMILY = "Noto Sans";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const fixtureDir = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const pixelDir = path.join(fixtureDir, "radar-pixel");
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const chunkDir = path.join(mermaidRoot, "dist", "chunks", "mermaid.esm");
const radarDiagramFile = fs
  .readdirSync(chunkDir)
  .filter((file) => /^diagram-.*\.mjs$/.test(file))
  .map((file) => path.join(chunkDir, file))
  .find((file) => fs.readFileSync(file, "utf8").includes("src/diagrams/radar/db.ts"));
if (!radarDiagramFile) throw new Error("Unable to locate the pinned radar diagram chunk");
const fontFile = path.resolve(
  "third_party",
  "noto",
  "fonts",
  "NotoSans-Regular.ttf",
);

const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const readJson = (file) => JSON.parse(fs.readFileSync(file, "utf8"));
const assertEqual = (actual, expected, label) => {
  if (actual !== expected) {
    throw new Error(`${label}: expected ${expected}, found ${actual}`);
  }
};
const writeFixture = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = readJson(path.join(mermaidRoot, "package.json"));
assertEqual(pkg.version, EXPECTED_MERMAID_VERSION, "Mermaid version");
assertEqual(
  sha256(fs.readFileSync(moduleFile)),
  EXPECTED_MERMAID_MODULE_SHA256,
  "Mermaid module sha256",
);
assertEqual(
  sha256(fs.readFileSync(chrome)),
  EXPECTED_CHROME_SHA256,
  "Chrome sha256",
);
assertEqual(
  sha256(fs.readFileSync(fontFile)),
  EXPECTED_NOTO_SHA256,
  "NotoSans-Regular.ttf sha256",
);

fs.mkdirSync(pixelDir, { recursive: true });

const mermaidModule = pathToFileURL(moduleFile).href;
const radarDiagramModule = pathToFileURL(radarDiagramFile).href;
const fontUrl = pathToFileURL(fontFile).href;
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

const grammarCases = [
  { id: "empty", source: "radar-beta" },
  { id: "header-colon", source: "radar-beta:" },
  { id: "header-space-colon", source: "radar-beta :" },
  { id: "same-line", source: "radar-beta axis A curve C {1}" },
  { id: "bom-crlf", source: "\ufeffradar-beta\r\n axis A\r\n curve C {1}" },
  {
    id: "comments",
    source: "%% before\nradar-beta %% header\n axis A %% axis\n curve C {1}",
  },
  { id: "axis-single", source: "radar-beta\n axis A" },
  { id: "axis-list", source: "radar-beta\n axis A,B,C" },
  {
    id: "axis-ids",
    source: "radar-beta\n axis _A1,A-B,z9\n curve C {1,2,3}",
  },
  {
    id: "axis-duplicate",
    source: "radar-beta\n axis A,A\n curve C {1,2}",
  },
  {
    id: "labels-double-single-empty",
    source:
      "radar-beta\n axis A[\"Alpha One\"],B['Beta Two'],C[\"\"]\n curve D[\"Curve Label\"] {1,2,3}",
  },
  {
    id: "label-escapes",
    source: "radar-beta\n axis A[\"line\\nnext\\tend\\q\"]\n curve C {1}",
  },
  {
    id: "numeric-values",
    source: "radar-beta\n axis A,B,C\n curve C {0,1.25,999}",
  },
  {
    id: "numeric-count-more",
    source: "radar-beta\n axis A\n curve C {1,2}",
  },
  {
    id: "numeric-count-less",
    source: "radar-beta\n axis A,B\n curve C {1}",
  },
  {
    id: "detailed-reordered",
    source:
      "radar-beta\n axis A[\"Alpha\"],B[\"Beta\"],C[\"Gamma\"]\n curve R {C:30,A:10,B:20}",
  },
  {
    id: "detailed-optional-colon",
    source: "radar-beta\n axis A,B\n curve R {B 2,A 1}",
  },
  {
    id: "detailed-before-axis",
    source: "radar-beta\n curve R {B:2,A:1}\n axis A,B",
  },
  {
    id: "detailed-extra-ref",
    source: "radar-beta\n axis A\n curve R {A:1,B:2}",
  },
  {
    id: "detailed-duplicate-first",
    source: "radar-beta\n axis A\n curve R {A:1,A:2}",
  },
  {
    id: "duplicate-curves",
    source: "radar-beta\n axis A\n curve C {1},C {2}",
  },
  {
    id: "entries-newlines",
    source: "radar-beta\n axis A,B\n curve C {\n 1,\n 2\n }",
  },
  {
    id: "options-all",
    source:
      "radar-beta\n axis A\n curve C {1}\n showLegend false, ticks 3, max 10, min 1, graticule polygon",
  },
  {
    id: "options-last-wins",
    source:
      "radar-beta\n axis A\n curve C {1}\n ticks 3\n ticks 7\n max 9\n max 11",
  },
  {
    id: "metadata",
    source:
      "radar-beta\n title Hello  world\n accTitle:  Radar title\n accDescr:  Radar description\n axis A\n curve C {1}",
  },
  {
    id: "metadata-block-last-wins",
    source:
      "radar-beta\n title First\n title Second\n accDescr {\n first line\n second line\n}\n axis A\n curve C {1}",
  },
  {
    id: "frontmatter-title",
    source:
      "---\ntitle: Frontmatter Radar\n---\nradar-beta\n axis A\n curve C {1}",
  },
  {
    id: "huge-finite",
    source: `radar-beta\n axis A\n curve C {${"9".repeat(308)}}`,
  },
  {
    id: "huge-infinity",
    source: `radar-beta\n axis A\n curve C {${"9".repeat(309)}}`,
  },
  { id: "reject-header-case", source: "Radar-beta\n axis A" },
  { id: "reject-header-prefix", source: "radar-betaX\n axis A" },
  { id: "reject-semicolon", source: "radar-beta;\n axis A" },
  { id: "reject-axis-unicode", source: "radar-beta\n axis \u4e2d" },
  {
    id: "reject-axis-reserved",
    source: "radar-beta\n axis axis\n curve C {1}",
  },
  {
    id: "reject-axis-bare-label",
    source: "radar-beta\n axis A[Alpha]\n curve C {1}",
  },
  { id: "reject-axis-comma-newline", source: "radar-beta\n axis A,\n B" },
  {
    id: "reject-empty-curve",
    source: "radar-beta\n axis A\n curve C {}",
  },
  {
    id: "reject-mixed-number-ref",
    source: "radar-beta\n axis A,B\n curve C {1,B:2}",
  },
  {
    id: "reject-mixed-ref-number",
    source: "radar-beta\n axis A,B\n curve C {A:1,2}",
  },
  {
    id: "reject-number-leading-zero",
    source: "radar-beta\n axis A\n curve C {01}",
  },
  {
    id: "reject-number-leading-dot",
    source: "radar-beta\n axis A\n curve C {.5}",
  },
  {
    id: "reject-number-trailing-dot",
    source: "radar-beta\n axis A\n curve C {1.}",
  },
  {
    id: "reject-number-negative",
    source: "radar-beta\n axis A\n curve C {-1}",
  },
  {
    id: "reject-number-plus",
    source: "radar-beta\n axis A\n curve C {+1}",
  },
  {
    id: "reject-number-exponent",
    source: "radar-beta\n axis A\n curve C {1e3}",
  },
  {
    id: "reject-number-infinity-token",
    source: "radar-beta\n axis A\n curve C {Infinity}",
  },
  {
    id: "reject-detailed-no-axes",
    source: "radar-beta\n curve C {A:1}",
  },
  {
    id: "reject-detailed-missing-axis",
    source: "radar-beta\n axis A,B\n curve C {A:1}",
  },
  {
    id: "reject-detailed-unknown-ref",
    source: "radar-beta\n axis A\n curve C {B:1}",
  },
  {
    id: "reject-option-colon",
    source: "radar-beta\n ticks: 3",
  },
  {
    id: "reject-option-case",
    source: "radar-beta\n showlegend false",
  },
  {
    id: "reject-option-bool-case",
    source: "radar-beta\n showLegend TRUE",
  },
  {
    id: "reject-option-graticule",
    source: "radar-beta\n graticule square",
  },
  { id: "reject-title-colon", source: "radar-beta\n title: Radar" },
  { id: "reject-unknown", source: "radar-beta\n unknown value" },
];

const canonicalBody = [
  "radar-beta",
  "title Team capability",
  'axis Quality["Quality"],Speed["Speed"],Reach["Reach"],Cost["Cost"],Risk["Risk"]',
  'curve Alpha["Alpha"] {8,6,7,4,5}',
  'curve Beta["Beta"] {5,8,4,7,6}',
].join("\n");

const withConfig = (config, body = canonicalBody) =>
  `%%{init: ${JSON.stringify(config)}}%%\n${body}`;

const geometryCases = [
  { id: "default-circle", source: canonicalBody },
  {
    id: "polygon-options",
    source: [
      "radar-beta",
      "title Polygon",
      "axis A,B,C,D",
      "curve C {1,4,7,10}",
      "showLegend false",
      "ticks 3",
      "min 1",
      "max 10",
      "graticule polygon",
    ].join("\n"),
  },
  {
    id: "asymmetric-config",
    source: withConfig(
      {
        radar: {
          width: 480,
          height: 320,
          marginTop: 20,
          marginRight: 70,
          marginBottom: 30,
          marginLeft: 40,
          axisScaleFactor: 0.8,
          axisLabelFactor: 1.2,
          curveTension: 0.31,
          useMaxWidth: false,
        },
      },
      [
        "radar-beta",
        "axis A,B,C,D,E",
        "curve C {1,2,3,4,5}",
        "ticks 4",
      ].join("\n"),
    ),
  },
  {
    id: "detailed-reorder",
    source: [
      "radar-beta",
      'axis A["Alpha"],B["Beta"],C["Gamma"]',
      'curve R["Reordered"] {C:30,A:10,B:20}',
      "min 0",
      "max 30",
    ].join("\n"),
  },
  {
    id: "mismatch-more",
    source: "radar-beta\n axis A,B\n curve TooMany {1,2,3}",
  },
  {
    id: "mismatch-less",
    source: "radar-beta\n axis A,B,C\n curve TooFew {1,2}",
  },
  { id: "axes-only", source: "radar-beta\n axis A,B,C" },
  { id: "empty", source: "radar-beta" },
  {
    id: "zero-ticks-no-legend",
    source:
      "radar-beta\n axis A,B,C\n curve C {1,2,3}\n ticks 0\n showLegend false",
  },
  {
    id: "equal-min-max",
    source:
      "radar-beta\n axis A,B,C\n curve C {5,5,5}\n min 5\n max 5\n graticule polygon",
  },
  {
    id: "clipping",
    source:
      "radar-beta\n axis A,B,C\n curve C {0,5,10}\n min 2\n max 8\n graticule polygon",
  },
];

const configBody = [
  "radar-beta",
  "title Config probe",
  "axis A,B,C",
  "curve C {1,2,3}",
  "ticks 2",
  "max 3",
  "graticule polygon",
].join("\n");
const rawConfigValues = [
  { id: "string", value: "320" },
  { id: "bool", value: true },
  { id: "null", value: null },
  { id: "array", value: [320] },
  { id: "object", value: { x: 320 } },
  { id: "zero", value: 0 },
  { id: "huge", value: 1e9 },
];
const rawConfigKeys = [
  "width",
  "height",
  "marginTop",
  "marginRight",
  "marginBottom",
  "marginLeft",
  "axisScaleFactor",
  "axisLabelFactor",
  "curveTension",
  "useMaxWidth",
];
const configCases = [
  { id: "baseline", source: configBody },
  ...rawConfigKeys.flatMap((key) =>
    rawConfigValues.map(({ id, value }) => ({
      id: `layout-${key}-${id}`,
      source: withConfig({ radar: { [key]: value } }, configBody),
    })),
  ),
  {
    id: "theme-live-all",
    source: withConfig(
      {
        themeVariables: {
          radar: {
            axisColor: "#ff0000",
            axisStrokeWidth: 7,
            axisLabelFontSize: 19,
            curveOpacity: 0.25,
            curveStrokeWidth: 8,
            graticuleColor: "#00ff00",
            graticuleStrokeWidth: 6,
            graticuleOpacity: 0.4,
            legendFontSize: 21,
          },
        },
      },
      configBody,
    ),
  },
  {
    id: "theme-live-zero",
    source: withConfig(
      {
        themeVariables: {
          radar: {
            axisStrokeWidth: 0,
            axisLabelFontSize: 0,
            curveOpacity: 0,
            curveStrokeWidth: 0,
            graticuleStrokeWidth: 0,
            graticuleOpacity: 0,
            legendFontSize: 0,
          },
        },
      },
      configBody,
    ),
  },
  {
    id: "theme-live-invalid",
    source: withConfig(
      {
        themeVariables: {
          radar: {
            axisColor: "not-a-color",
            axisStrokeWidth: "bad",
            axisLabelFontSize: "bad",
            curveOpacity: "bad",
            curveStrokeWidth: "bad",
            graticuleColor: "not-a-color",
            graticuleStrokeWidth: "bad",
            graticuleOpacity: "bad",
            legendFontSize: "bad",
          },
        },
      },
      configBody,
    ),
  },
  {
    id: "radar-style-keys-preserved-but-inert",
    source: withConfig(
      {
        radar: {
          axisColor: "#ff0000",
          axisStrokeWidth: 7,
          axisLabelFontSize: 19,
          curveOpacity: 0.25,
          curveStrokeWidth: 8,
          graticuleColor: "#00ff00",
          graticuleStrokeWidth: 6,
          graticuleOpacity: 0.4,
          legendFontSize: 21,
          legendBoxSize: 99,
        },
      },
      configBody,
    ),
  },
  {
    id: "theme-legend-box-size-dead",
    source: withConfig(
      { themeVariables: { radar: { legendBoxSize: 99 } } },
      configBody,
    ),
  },
];

const pixelCases = [
  {
    id: "default",
    theme: "default",
    source: withConfig({
      theme: "default",
      fontFamily: FONT_FAMILY,
      themeVariables: { fontFamily: FONT_FAMILY },
    }),
  },
  {
    id: "dark",
    theme: "dark",
    source: withConfig({
      theme: "dark",
      fontFamily: FONT_FAMILY,
      themeVariables: { fontFamily: FONT_FAMILY },
    }),
  },
  {
    id: "redux-color",
    theme: "redux-color",
    source: withConfig({
      theme: "redux-color",
      fontFamily: FONT_FAMILY,
      themeVariables: { fontFamily: FONT_FAMILY },
    }),
  },
  {
    id: "path-stress",
    theme: "default",
    source: withConfig(
      {
        theme: "default",
        fontFamily: FONT_FAMILY,
        themeVariables: { fontFamily: FONT_FAMILY },
        radar: { curveTension: -2 },
      },
      [
        "radar-beta",
        "title Closed path stress",
        "axis A,B,C,D,E",
        "curve Stress {10,1,10,1,10}",
        "curve Cross {1,10,1,10,1}",
        "max 10",
        "showLegend false",
      ].join("\n"),
    ),
  },
];

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const page = await browser.newPage();
  await page.setViewport(VIEWPORT);
  await page.goto(
    pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href,
  );
  await page.addStyleTag({
    content: `@font-face{font-family:"${FONT_FAMILY}";src:url("${fontUrl}");font-style:normal;font-weight:400}html,body{margin:0;padding:0}`,
  });
  await page.evaluate(async (family) => {
    await document.fonts.load(`16px "${family}"`);
    await document.fonts.ready;
  }, FONT_FAMILY);

  const grammar = await page.evaluate(
    async ({ cases, mod, radarMod }) => {
      const { default: mermaid } = await import(mod);
      const { diagram: radarDiagram } = await import(radarMod);
      const normalizedNumber = (v) => {
        if (Number.isNaN(v)) return "NaN";
        if (v === Number.POSITIVE_INFINITY) return "Infinity";
        if (v === Number.NEGATIVE_INFINITY) return "-Infinity";
        return v;
      };
      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          look: "classic",
        });
        try {
          await mermaid.parse(fixture.source);
          // render() is the only public source-entry operation that forwards a
          // frontmatter title into Diagram.fromText. It repopulates the same DB;
          // parser-only cases remain valid because radar drawing tolerates empty,
          // mismatched, degenerate, and non-finite geometry.
          await mermaid.render(`radar-grammar-${index}`, fixture.source);
          // mermaid.parse() uses the full source-entry preprocessor and leaves the
          // singleton DB populated. Reading that same module captures frontmatter
          // title ownership as well as the grammar-native fields.
          const options = radarDiagram.db.getOptions();
          out.push({
            id: fixture.id,
            source: fixture.source,
            accept: true,
            expectedDb: {
              axes: radarDiagram.db.getAxes(),
              curves: radarDiagram.db.getCurves().map((curve) => ({
                ...curve,
                entries: curve.entries.map(normalizedNumber),
              })),
              options: {
                ...options,
                ticks: normalizedNumber(options.ticks),
                max: options.max === null ? null : normalizedNumber(options.max),
                min: normalizedNumber(options.min),
              },
              title: radarDiagram.db.getDiagramTitle(),
              accTitle: radarDiagram.db.getAccTitle(),
              accDescription: radarDiagram.db.getAccDescription(),
            },
          });
        } catch (error) {
          out.push({
            id: fixture.id,
            source: fixture.source,
            accept: false,
            reject: {
              message: String(error?.message ?? error).replace(/\s+/g, " ").trim(),
            },
          });
        }
      }
      return out;
    },
    { cases: grammarCases, mod: mermaidModule, radarMod: radarDiagramModule },
  );

  const byId = new Map(grammar.map((item) => [item.id, item]));
  const mustAccept = [
    "header-space-colon",
    "bom-crlf",
    "detailed-reordered",
    "huge-infinity",
  ];
  const mustReject = [
    "reject-header-case",
    "reject-axis-bare-label",
    "reject-mixed-number-ref",
    "reject-number-exponent",
    "reject-detailed-missing-axis",
    "reject-detailed-unknown-ref",
  ];
  for (const id of mustAccept) assertEqual(byId.get(id)?.accept, true, `${id} accept`);
  for (const id of mustReject) assertEqual(byId.get(id)?.accept, false, `${id} reject`);
  assertEqual(
    byId.get("detailed-reordered").expectedDb.curves[0].entries.join(","),
    "10,20,30",
    "detailed reference order",
  );
  assertEqual(
    byId.get("huge-infinity").expectedDb.curves[0].entries[0],
    "Infinity",
    "NUMBER overflow",
  );
  assertEqual(
    byId.get("reject-detailed-unknown-ref").reject.message,
    "Missing entry for axis A",
    "unknown reference resolves as a missing declared axis",
  );

  writeFixture(path.join(fixtureDir, "radar-grammar.json"), {
    upstream: {
      package: "mermaid",
      version: EXPECTED_MERMAID_VERSION,
      moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
      parser: "Langium RadarGrammar",
      sourceEntry: true,
    },
    oracle: "source-entry accept/reject plus normalized radar DB state",
    cases: grammar,
  });

  const geometry = await page.evaluate(
    async ({ cases, mod }) => {
      const { default: mermaid } = await import(mod);
      const attrs = (element, names) =>
        Object.fromEntries(names.map((name) => [name, element?.getAttribute(name) ?? null]));
      const style = (element, names) => {
        if (!element) return null;
        const computed = getComputedStyle(element);
        return Object.fromEntries(names.map((name) => [name, computed.getPropertyValue(name)]));
      };
      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "loose",
          theme: "default",
          look: "classic",
        });
        const { svg } = await mermaid.render(`radar-geometry-${index}`, fixture.source);
        const host = document.createElement("div");
        host.innerHTML = svg;
        document.body.replaceChildren(host);
        const root = host.querySelector("svg");
        const title = root.querySelector("text.radarTitle");
        const frame = title?.parentElement ?? null;
        const graticules = [
          ...root.querySelectorAll("circle.radarGraticule, polygon.radarGraticule"),
        ];
        const axes = [...root.querySelectorAll("line.radarAxisLine")];
        const labels = [...root.querySelectorAll("text.radarAxisLabel")];
        const curves = [
          ...root.querySelectorAll('[class^="radarCurve-"]'),
        ];
        const legendBoxes = [
          ...root.querySelectorAll('[class^="radarLegendBox-"]'),
        ];
        const legendTexts = [...root.querySelectorAll("text.radarLegendText")];
        out.push({
          id: fixture.id,
          source: fixture.source,
          expected: {
            root: attrs(root, ["viewBox", "width", "height", "style", "overflow"]),
            frameTransform: frame?.getAttribute("transform") ?? null,
            graticules: graticules.map((element) => ({
              tag: element.tagName.toLowerCase(),
              attributes: attrs(element, ["r", "points"]),
              style: style(element, [
                "fill",
                "fill-opacity",
                "stroke",
                "stroke-width",
              ]),
            })),
            axes: axes.map((element) => ({
              attributes: attrs(element, ["x1", "y1", "x2", "y2"]),
              style: style(element, ["stroke", "stroke-width"]),
            })),
            axisLabels: labels.map((element) => ({
              text: element.textContent,
              attributes: attrs(element, [
                "x",
                "y",
                "text-anchor",
                "dominant-baseline",
              ]),
              style: style(element, ["color", "font-size"]),
            })),
            curves: curves.map((element) => ({
              tag: element.tagName.toLowerCase(),
              attributes: attrs(element, ["d", "points"]),
              style: style(element, [
                "fill",
                "fill-opacity",
                "stroke",
                "stroke-width",
              ]),
            })),
            legendBoxes: legendBoxes.map((element) => ({
              attributes: attrs(element, ["width", "height"]),
              style: style(element, ["fill", "fill-opacity", "stroke"]),
            })),
            legendTexts: legendTexts.map((element) => ({
              text: element.textContent,
              attributes: attrs(element, ["x", "y"]),
              parentTransform: element.parentElement?.getAttribute("transform") ?? null,
              style: style(element, ["font-size"]),
            })),
            title: title
              ? {
                  text: title.textContent,
                  attributes: attrs(title, ["x", "y"]),
                  style: style(title, ["font-size", "color"]),
                }
              : null,
          },
        });
      }
      return out;
    },
    { cases: geometryCases, mod: mermaidModule },
  );

  const geometryById = new Map(geometry.map((item) => [item.id, item.expected]));
  assertEqual(
    geometryById.get("default-circle").root.viewBox,
    "0 0 700 700",
    "default viewBox",
  );
  assertEqual(
    geometryById.get("default-circle").graticules.length,
    5,
    "default tick count",
  );
  assertEqual(
    geometryById.get("polygon-options").graticules[0].tag,
    "polygon",
    "polygon graticule",
  );
  assertEqual(
    geometryById.get("polygon-options").legendBoxes.length,
    0,
    "showLegend false",
  );
  assertEqual(
    geometryById.get("asymmetric-config").root.viewBox,
    "0 0 590 370",
    "asymmetric viewBox",
  );
  assertEqual(
    geometryById.get("mismatch-more").curves.length,
    0,
    "mismatched curve skipped",
  );
  assertEqual(
    geometryById.get("zero-ticks-no-legend").graticules.length,
    0,
    "zero ticks",
  );

  writeFixture(path.join(fixtureDir, "radar-geometry.json"), {
    upstream: {
      package: "mermaid",
      version: EXPECTED_MERMAID_VERSION,
      moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
      chrome: EXPECTED_CHROME_PRODUCT,
      chromeSha256: EXPECTED_CHROME_SHA256,
    },
    oracle:
      "rendered radar SVG attributes and attached-DOM computed styles; exact formula geometry",
    cases: geometry,
  });

  const config = await page.evaluate(
    async ({ cases, mod }) => {
      const { default: mermaid } = await import(mod);
      const attrs = (element, names) =>
        Object.fromEntries(names.map((name) => [name, element?.getAttribute(name) ?? null]));
      const style = (element, names) => {
        if (!element) return null;
        const computed = getComputedStyle(element);
        return Object.fromEntries(names.map((name) => [name, computed.getPropertyValue(name)]));
      };
      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "loose",
          theme: "default",
          look: "classic",
        });
        let sanitizedConfig = null;
        try {
          sanitizedConfig = (await mermaid.parse(fixture.source)).config;
        } catch (error) {
          out.push({
            id: fixture.id,
            source: fixture.source,
            parse: false,
            parseError: String(error?.message ?? error).replace(/\s+/g, " ").trim(),
          });
          continue;
        }
        try {
          const { svg } = await mermaid.render(`radar-config-${index}`, fixture.source);
          const host = document.createElement("div");
          host.innerHTML = svg;
          document.body.replaceChildren(host);
          const root = host.querySelector("svg");
          const title = root.querySelector("text.radarTitle");
          const frame = title?.parentElement ?? null;
          const graticule = root.querySelector(".radarGraticule");
          const axis = root.querySelector(".radarAxisLine");
          const axisLabel = root.querySelector(".radarAxisLabel");
          const curve = root.querySelector('[class^="radarCurve-"]');
          const legendBox = root.querySelector('[class^="radarLegendBox-"]');
          const legendText = root.querySelector(".radarLegendText");
          out.push({
            id: fixture.id,
            source: fixture.source,
            parse: true,
            sanitizedConfig,
            render: true,
            expected: {
              root: attrs(root, ["viewBox", "width", "height", "style"]),
              frameTransform: frame?.getAttribute("transform") ?? null,
              graticule: {
                tag: graticule?.tagName.toLowerCase() ?? null,
                attributes: attrs(graticule, ["r", "points"]),
                style: style(graticule, [
                  "fill",
                  "fill-opacity",
                  "stroke",
                  "stroke-width",
                ]),
              },
              axis: {
                attributes: attrs(axis, ["x1", "y1", "x2", "y2"]),
                style: style(axis, ["stroke", "stroke-width"]),
              },
              axisLabel: {
                text: axisLabel?.textContent ?? null,
                attributes: attrs(axisLabel, ["x", "y"]),
                style: style(axisLabel, ["color", "font-size"]),
              },
              curve: {
                tag: curve?.tagName.toLowerCase() ?? null,
                attributes: attrs(curve, ["d", "points"]),
                style: style(curve, [
                  "fill",
                  "fill-opacity",
                  "stroke",
                  "stroke-width",
                ]),
              },
              legendBox: {
                attributes: attrs(legendBox, ["width", "height"]),
                style: style(legendBox, ["fill", "fill-opacity", "stroke"]),
              },
              legendText: {
                text: legendText?.textContent ?? null,
                style: style(legendText, ["font-size"]),
              },
            },
          });
        } catch (error) {
          out.push({
            id: fixture.id,
            source: fixture.source,
            parse: true,
            sanitizedConfig,
            render: false,
            renderError: String(error?.message ?? error).replace(/\s+/g, " ").trim(),
          });
        }
      }
      return out;
    },
    { cases: configCases, mod: mermaidModule },
  );

  const configById = new Map(config.map((item) => [item.id, item]));
  assertEqual(config.length, configCases.length, "config case count");
  assertEqual(configById.get("baseline").expected.root.viewBox, "0 0 700 700", "config baseline");
  assertEqual(
    configById.get("theme-live-all").expected.axis.style.stroke,
    "rgb(255, 0, 0)",
    "nested radar axisColor",
  );
  assertEqual(
    configById.get("theme-live-all").expected.curve.style["stroke-width"],
    "8px",
    "nested radar curveStrokeWidth",
  );
  assertEqual(
    configById.get("theme-legend-box-size-dead").expected.legendBox.attributes.width,
    "12",
    "legendBoxSize is renderer-inert",
  );
  const radarStyleInert = configById.get("radar-style-keys-preserved-but-inert");
  assertEqual(
    Object.hasOwn(radarStyleInert.sanitizedConfig.radar ?? {}, "axisColor"),
    true,
    "config.radar style keys survive source sanitization",
  );
  assertEqual(
    radarStyleInert.expected.axis.style.stroke,
    configById.get("baseline").expected.axis.style.stroke,
    "config.radar style keys are renderer-inert",
  );
  writeFixture(path.join(fixtureDir, "radar-config.json"), {
    upstream: {
      package: "mermaid",
      version: EXPECTED_MERMAID_VERSION,
      moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
      chrome: EXPECTED_CHROME_PRODUCT,
      chromeSha256: EXPECTED_CHROME_SHA256,
      sourceEntry: true,
    },
    oracle:
      "raw JSON config types after source sanitizer plus rendered SVG geometry/computed style",
    cases: config,
  });

  const pixelManifest = {
    upstream: {
      package: "mermaid",
      version: EXPECTED_MERMAID_VERSION,
      moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
      chrome: EXPECTED_CHROME_PRODUCT,
      chromeSha256: EXPECTED_CHROME_SHA256,
    },
    font: {
      family: FONT_FAMILY,
      file: "NotoSans-Regular.ttf",
      sha256: EXPECTED_NOTO_SHA256,
    },
    capture: "attached SVG element screenshot at DPR 1",
    cases: [],
  };

  for (let index = 0; index < pixelCases.length; ++index) {
    const fixture = pixelCases[index];
    const metrics = await page.evaluate(
      async ({ fixture, index, mod, family }) => {
        const { default: mermaid } = await import(mod);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "loose",
          theme: "default",
          look: "classic",
          fontFamily: family,
          themeVariables: { fontFamily: family },
        });
        const { svg } = await mermaid.render(`radar-pixel-${index}`, fixture.source);
        const host = document.createElement("div");
        host.id = "radar-capture";
        host.style.cssText = "display:inline-block;margin:0;padding:0;line-height:0";
        host.innerHTML = svg;
        document.body.replaceChildren(host);
        await document.fonts.load(`16px "${family}"`);
        await document.fonts.ready;
        const root = host.querySelector("svg");
        const parts = root.getAttribute("viewBox").trim().split(/\s+/).map(Number);
        root.style.width = `${parts[2]}px`;
        root.style.height = `${parts[3]}px`;
        root.style.maxWidth = "none";
        return { width: parts[2], height: parts[3] };
      },
      { fixture, index, mod: mermaidModule, family: FONT_FAMILY },
    );
    const element = await page.$("#radar-capture svg");
    if (!element) throw new Error(`Missing SVG for pixel case ${fixture.id}`);
    const bytes = await element.screenshot({ omitBackground: true });
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), bytes);
    pixelManifest.cases.push({
      id: fixture.id,
      theme: fixture.theme,
      dpr: VIEWPORT.deviceScaleFactor,
      source: fixture.source,
      file,
      width: metrics.width,
      height: metrics.height,
      sha256: sha256(bytes),
    });
  }

  writeFixture(path.join(pixelDir, "manifest.json"), pixelManifest);

  console.log(
    JSON.stringify(
      {
        grammar: {
          total: grammar.length,
          accepted: grammar.filter((item) => item.accept).length,
          rejected: grammar.filter((item) => !item.accept).length,
        },
        geometry: geometry.length,
        config: {
          total: config.length,
          rendered: config.filter((item) => item.render).length,
          renderErrors: config.filter((item) => item.parse && !item.render).length,
        },
        pixel: pixelManifest.cases.map(({ id, width, height, sha256: hash }) => ({
          id,
          width,
          height,
          sha256: hash,
        })),
      },
      null,
      2,
    ),
  );
} finally {
  await browser.close();
}
