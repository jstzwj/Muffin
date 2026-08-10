import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Freezes Mermaid 11.16.0 timeline renderer behavior:
//   tests/fixtures/mermaid/timeline-geometry.json
//   tests/fixtures/mermaid/timeline-config.json
//   tests/fixtures/mermaid/timeline-pixel/{default,dark,redux-color}.png
//   tests/fixtures/mermaid/timeline-pixel/manifest.json
//
// This is a renderer/layout oracle, not a parser oracle. It intentionally
// retains upstream quirks including section--1, section-NaN at TCL=0, LR
// connectors for event-less tasks, and TD's unresolved #arrowhead marker.
//
// Usage:
//   node scripts/generate_mermaid_timeline_fixtures.mjs \
//     [mermaid-root] [fixture-dir] [chrome-exe]

const EXPECTED_MERMAID_VERSION = "11.16.0";
const EXPECTED_MERMAID_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA256 =
  "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";
const VIEWPORT = { width: 1800, height: 1400, deviceScaleFactor: 1 };
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
const pixelDir = path.join(fixtureDir, "timeline-pixel");
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
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
const writeJson = (file, value) => {
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

const mermaidModule = pathToFileURL(moduleFile).href;
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

const sourceInit = (config, body) =>
  `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const stableSource = (body, config = {}) =>
  sourceInit(
    {
      ...config,
      fontFamily: config.fontFamily ?? FONT_FAMILY,
      themeVariables: {
        fontFamily: FONT_FAMILY,
        ...(config.themeVariables ?? {}),
      },
    },
    body,
  );

const LR_CANONICAL = [
  "timeline",
  "title Product history",
  "section Alpha section",
  "  2020 start : event one : event two",
  "  A very long period label that wraps across several words : long event text that also wraps across many words",
  "section Beta",
  "  2022 end : final event",
].join("\n");
const TD_CANONICAL = LR_CANONICAL.replace(/^timeline/, "timeline TD");
const NO_SECTIONS = [
  "timeline",
  "One : first event",
  "Two",
  "Three : third event a : third event b",
].join("\n");

const geometryCases = [
  { id: "lr-canonical", source: stableSource(LR_CANONICAL) },
  { id: "td-canonical", source: stableSource(TD_CANONICAL) },
  { id: "lr-no-sections", source: stableSource(NO_SECTIONS) },
  {
    id: "lr-no-sections-disabled",
    source: stableSource(NO_SECTIONS, {
      timeline: { disableMulticolor: true },
    }),
  },
  {
    id: "td-no-sections",
    source: stableSource(NO_SECTIONS.replace(/^timeline/, "timeline TD")),
  },
  { id: "empty", source: stableSource("timeline") },
  {
    id: "empty-section",
    source: stableSource("timeline\nsection Lonely"),
  },
  { id: "eventless-task", source: stableSource("timeline\nOnly") },
  {
    id: "multi-event",
    source: stableSource("timeline\nOnly : first : second : third"),
  },
  {
    id: "explicit-break-and-long-word",
    source: stableSource(
      "timeline\nsection Alpha<br>Beta\nSupercalifragilisticexpialidocious : Event<br>Break",
    ),
  },
  {
    id: "zero-padding-left-fixed-size",
    source: stableSource(LR_CANONICAL, {
      timeline: { padding: 0, leftMargin: 0, useMaxWidth: false },
    }),
  },
  {
    id: "font-size-32",
    source: stableSource(LR_CANONICAL, {
      themeVariables: { fontSize: "32px" },
    }),
  },
  {
    id: "theme-color-limit-1",
    source: stableSource(NO_SECTIONS, {
      themeVariables: { THEME_COLOR_LIMIT: 1 },
    }),
  },
  {
    id: "theme-color-limit-0",
    source: stableSource(NO_SECTIONS, {
      themeVariables: { THEME_COLOR_LIMIT: 0 },
    }),
  },
  {
    id: "redux-square-nodes",
    source: stableSource(LR_CANONICAL, { theme: "redux-color" }),
  },
  {
    id: "neo-gradient",
    source: stableSource(LR_CANONICAL, { theme: "neo", look: "neo" }),
  },
];

const deadTimelineConfig = {
  diagramMarginX: 1,
  diagramMarginY: 2,
  width: 999,
  height: 888,
  boxMargin: 77,
  boxTextMargin: 66,
  noteMargin: 55,
  messageMargin: 44,
  messageAlign: "right",
  bottomMarginAdj: 33,
  rightAngles: true,
  taskFontSize: 42,
  taskFontFamily: "Courier New",
  taskMargin: 22,
  activationWidth: 11,
  textPlacement: "old",
  actorColours: ["#ff0000"],
  sectionFills: ["#00ff00"],
  sectionColours: ["#0000ff"],
};
const configCases = [
  { id: "default", body: LR_CANONICAL },
  { id: "leftMargin", body: LR_CANONICAL, config: { timeline: { leftMargin: 0 } } },
  { id: "padding", body: LR_CANONICAL, config: { timeline: { padding: 7 } } },
  { id: "useMaxWidth", body: LR_CANONICAL, config: { timeline: { useMaxWidth: false } } },
  { id: "disableMulticolor-base", body: NO_SECTIONS },
  {
    id: "disableMulticolor",
    body: NO_SECTIONS,
    config: { timeline: { disableMulticolor: true } },
  },
  { id: "legacy-fields", body: LR_CANONICAL, config: { timeline: deadTimelineConfig } },
  {
    id: "fontFamily",
    body: LR_CANONICAL,
    config: { themeVariables: { fontFamily: "Courier New" } },
  },
  {
    id: "fontSize",
    body: LR_CANONICAL,
    config: { themeVariables: { fontSize: "24px" } },
  },
  {
    id: "themeColorLimit-1",
    body: NO_SECTIONS,
    config: { themeVariables: { THEME_COLOR_LIMIT: 1 } },
  },
  {
    id: "themeColorLimit-0",
    body: NO_SECTIONS,
    config: { themeVariables: { THEME_COLOR_LIMIT: 0 } },
  },
  {
    id: "cScale0",
    body: NO_SECTIONS,
    config: { themeVariables: { cScale0: "#ff0000" } },
  },
  {
    id: "cScaleLabel0",
    body: NO_SECTIONS,
    config: { themeVariables: { cScaleLabel0: "#00ff00" } },
  },
  {
    id: "cScaleInv0",
    body: NO_SECTIONS,
    config: { themeVariables: { cScaleInv0: "#0000ff" } },
  },
  {
    id: "fontWeight",
    body: NO_SECTIONS,
    config: { themeVariables: { fontWeight: "bold" } },
  },
  {
    id: "strokeWidth-redux",
    body: NO_SECTIONS,
    config: { theme: "redux-color", themeVariables: { strokeWidth: "7px" } },
  },
  { id: "theme-dark", body: LR_CANONICAL, config: { theme: "dark" } },
  { id: "theme-redux-color", body: LR_CANONICAL, config: { theme: "redux-color" } },
  {
    id: "look-neo-gradient",
    body: LR_CANONICAL,
    config: { theme: "neo", look: "neo" },
  },
  {
    id: "look-neo-no-gradient",
    body: LR_CANONICAL,
    config: {
      theme: "neo",
      look: "neo",
      themeVariables: { useGradient: false },
    },
  },
  {
    id: "gradient-colors",
    body: LR_CANONICAL,
    config: {
      theme: "neo",
      look: "neo",
      themeVariables: { gradientStart: "#112233", gradientStop: "#ddeeff" },
    },
  },
];
for (const [key, value] of Object.entries(deadTimelineConfig)) {
  configCases.push({
    id: `legacy-${key}`,
    body: LR_CANONICAL,
    config: { timeline: { [key]: value } },
  });
}

const PIXEL_BODY = [
  "timeline",
  "title Release history",
  "section Foundation",
  "  Design begins : API agreed : first prototype",
  "  Implementation and integration across several components : alpha milestone",
  "section Delivery",
  "  Public release : documentation : support",
].join("\n");
const pixelCases = [
  { id: "default", theme: "default" },
  { id: "dark", theme: "dark" },
  { id: "redux-color", theme: "redux-color" },
].map(({ id, theme }) => ({
  id,
  theme,
  renderId: `timeline-pixel-${id}`,
  source: stableSource(PIXEL_BODY, { theme }),
}));

const THEME_COLOR_LIMIT_THEMES = [
  "base",
  "dark",
  "default",
  "forest",
  "neutral",
  "neo",
  "neo-dark",
  "redux",
  "redux-dark",
  "redux-color",
  "redux-dark-color",
];
const THEME_COLOR_LIMIT_GATE_EXPECTED = {
  base: { 13: "l", 14: "l" },
  dark: { 13: "accept", 14: "r" },
  default: { 13: "l", 14: "l" },
  forest: { 13: "l", 14: "l" },
  neutral: { 13: "r", 14: "r" },
  neo: { 13: "l", 14: "l" },
  "neo-dark": { 13: "l", 14: "l" },
  redux: { 13: "accept", 14: "accept" },
  "redux-dark": { 13: "l", 14: "l" },
  "redux-color": { 13: "r", 14: "r" },
  "redux-dark-color": { 13: "r", 14: "r" },
};

const canonicalPng = (bytes) => {
  const image = PNG.sync.read(bytes);
  for (let offset = 0; offset < image.data.length; offset += 4) {
    if (image.data[offset + 3] === 0) {
      image.data[offset] = 0;
      image.data[offset + 1] = 0;
      image.data[offset + 2] = 0;
    }
  }
  return PNG.sync.write(image, {
    colorType: 6,
    inputColorType: 6,
    bitDepth: 8,
  });
};

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

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const upstream = {
    package: "mermaid",
    version: EXPECTED_MERMAID_VERSION,
    module: "dist/mermaid.esm.mjs",
    moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
    browser: {
      product: EXPECTED_CHROME_PRODUCT,
      executableSha256: EXPECTED_CHROME_SHA256,
      headlessMode: "new",
    },
    viewport: VIEWPORT,
    fonts: [
      {
        family: FONT_FAMILY,
        file: "third_party/noto/fonts/NotoSans-Regular.ttf",
        sha256: EXPECTED_NOTO_SHA256,
      },
    ],
    generator: "scripts/generate_mermaid_timeline_fixtures.mjs",
  };

  const preparePage = async () => {
    const page = await browser.newPage();
    await page.setViewport(VIEWPORT);
    await page.goto(
      pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href,
    );
    await page.evaluate(
      async ({ family, url }) => {
        document.documentElement.setAttribute("lang", "en");
        document.body.style.margin = "0";
        document.body.innerHTML = '<div id="container"></div>';
        const style = document.createElement("style");
        style.id = "timeline-oracle-font";
        style.textContent = `@font-face{font-family:"${family}";src:url("${url}");font-style:normal;font-weight:400}html,body{margin:0;padding:0}`;
        document.head.appendChild(style);
        await document.fonts.load(`16px "${family}"`, "Timeline 0123456789");
        await document.fonts.ready;
      },
      { family: FONT_FAMILY, url: fontUrl },
    );
    return page;
  };

  const captureCases = async (cases, sourceForCase) => {
    const page = await preparePage();
    const result = await page.evaluate(
      async ({ browserCases, family, moduleUrl }) => {
        const { default: mermaid } = await import(moduleUrl);
        const round = (value) =>
          Number.isFinite(value) ? Math.round(value * 1000) / 1000 : null;
        const rect = (value) => ({
          x: round(value.x),
          y: round(value.y),
          width: round(value.width),
          height: round(value.height),
        });
        const attrs = (element, names) =>
          Object.fromEntries(
            names
              .map((name) => [name, element.getAttribute(name)])
              .filter(([, value]) => value !== null),
          );
        const bbox = (element, client = false) =>
          element
            ? rect(client ? element.getBoundingClientRect() : element.getBBox())
            : null;
        const computed = (element, names) => {
          if (!element) return null;
          const style = getComputedStyle(element);
          return Object.fromEntries(
            names.map((name) => [name, style.getPropertyValue(name)]),
          );
        };
        const shape = (element) => ({
          tag: element.tagName.toLowerCase(),
          class: element.getAttribute("class") ?? "",
          attrs: attrs(element, [
            "id",
            "x",
            "y",
            "x1",
            "y1",
            "x2",
            "y2",
            "dy",
            "d",
            "transform",
            "marker-end",
            "stroke-dasharray",
          ]),
          bbox: bbox(element),
          computed: computed(element, [
            "fill",
            "stroke",
            "stroke-width",
            "filter",
            "font-family",
            "font-size",
            "font-weight",
            "text-anchor",
            "dominant-baseline",
            "alignment-baseline",
          ]),
          ...(element.tagName.toLowerCase() === "text" ||
          element.tagName.toLowerCase() === "tspan"
            ? { text: element.textContent }
            : {}),
        });
        const snapshot = (root) => ({
          root: {
            attrs: attrs(root, [
              "id",
              "width",
              "height",
              "viewBox",
              "style",
              "role",
              "aria-roledescription",
            ]),
            bbox: bbox(root),
            clientBox: bbox(root, true),
            computed: computed(root, ["font-family", "font-size", "fill"]),
          },
          directOrder: [...root.children].map((element) => ({
            tag: element.tagName.toLowerCase(),
            class: element.getAttribute("class") ?? "",
            id: element.getAttribute("id") ?? "",
          })),
          nodes: [...root.querySelectorAll("g.timeline-node")].map((node) => {
            const outer = node.parentElement;
            const background = node.querySelector(":scope > g:first-child");
            const textGroup = node.querySelector(":scope > g:nth-child(2)");
            const text = textGroup?.querySelector(":scope > text") ?? null;
            return {
              outerClass: outer?.getAttribute("class") ?? "",
              outerTransform: outer?.getAttribute("transform") ?? null,
              class: node.getAttribute("class"),
              dataLook: node.getAttribute("data-look"),
              bbox: bbox(node),
              background: background
                ? [...background.children].map(shape)
                : [],
              textTransform: textGroup?.getAttribute("transform") ?? null,
              text: text ? shape(text) : null,
              tspans: text
                ? [...text.querySelectorAll(":scope > tspan")].map(shape)
                : [],
            };
          }),
          connectors: [...root.querySelectorAll("g.lineWrapper > line")].map(
            (line) => ({
              parentIndex: [...root.children].indexOf(line.parentElement),
              ...shape(line),
            }),
          ),
          titles: [...root.querySelectorAll(":scope > text")].map(shape),
          markers: [...root.querySelectorAll("marker")].map((marker) => ({
            attrs: attrs(marker, [
              "id",
              "refX",
              "refY",
              "markerWidth",
              "markerHeight",
              "orient",
            ]),
            path: marker.querySelector("path")?.getAttribute("d") ?? null,
          })),
          gradients: [...root.querySelectorAll("linearGradient")].map(
            (gradient) => ({
              attrs: attrs(gradient, ["id", "x1", "y1", "x2", "y2"]),
              stops: [...gradient.querySelectorAll("stop")].map((stop) =>
                attrs(stop, ["offset", "stop-color", "stop-opacity"]),
              ),
            }),
          ),
        });

        const output = [];
        for (let index = 0; index < browserCases.length; ++index) {
          const fixture = browserCases[index];
          mermaid.initialize({
            startOnLoad: false,
            securityLevel: "strict",
            look: "classic",
            theme: "default",
            fontFamily: family,
            themeVariables: { fontFamily: family },
          });
          const { svg } = await mermaid.render(
            `timeline-oracle-${index}`,
            fixture.source,
          );
          document.getElementById("container").innerHTML = svg;
          await document.fonts.ready;
          await new Promise((resolve) =>
            requestAnimationFrame(() => requestAnimationFrame(resolve)),
          );
          const root = document.querySelector("#container > svg");
          output.push({
            id: fixture.id,
            source: fixture.source,
            expected: snapshot(root),
          });
        }
        return output;
      },
      {
        browserCases: cases.map((fixture) => ({
          id: fixture.id,
          source: sourceForCase(fixture),
        })),
        family: FONT_FAMILY,
        moduleUrl: mermaidModule,
      },
    );
    await page.close();
    return result;
  };

  const captureThemeColorLimitGate = async () => {
    const output = [];
    for (const theme of THEME_COLOR_LIMIT_THEMES) {
      for (const limit of [13, 14]) {
        const page = await preparePage();
        const result = await page.evaluate(
          async ({ family, moduleUrl, theme, limit }) => {
            const { default: mermaid } = await import(moduleUrl);
            try {
              mermaid.initialize({
                startOnLoad: false,
                securityLevel: "strict",
                look: "classic",
                theme: "default",
                fontFamily: family,
                themeVariables: { fontFamily: family },
              });
              const source = `%%{init: ${JSON.stringify({
                theme,
                fontFamily: family,
                themeVariables: {
                  fontFamily: family,
                  THEME_COLOR_LIMIT: limit,
                },
              })}}%%\ntimeline\nA`;
              await mermaid.render("timeline-tcl-gate", source);
              return { theme, limit, accept: true };
            } catch (error) {
              return {
                theme,
                limit,
                accept: false,
                error: {
                  name: String(error?.name ?? "Error"),
                  message: String(error?.message ?? error),
                },
              };
            }
          },
          { family: FONT_FAMILY, moduleUrl: mermaidModule, theme, limit },
        );
        output.push(result);
        await page.close();
      }
    }
    return output;
  };

  const geometry = await captureCases(
    geometryCases,
    (fixture) => fixture.source,
  );
  const geometryById = new Map(
    geometry.map((fixture) => [fixture.id, fixture.expected]),
  );
  assertEqual(geometry.length, 16, "timeline geometry case count");
  assertEqual(
    geometryById.get("td-canonical").markers[0].attrs.id,
    "undefined-arrowhead",
    "TD marker definition quirk",
  );
  if (
    !geometryById
      .get("td-canonical")
      .connectors.every(
        (line) => line.attrs["marker-end"] === "url(#arrowhead)",
      )
  ) {
    throw new Error("TD unresolved marker-reference quirk drifted");
  }
  if (
    !geometryById
      .get("theme-color-limit-0")
      .nodes.every((node) => node.class.includes("section-NaN"))
  ) {
    throw new Error("TCL=0 section-NaN quirk drifted");
  }
  assertEqual(
    geometryById.get("eventless-task").connectors.length,
    2,
    "LR event-less connector plus axis",
  );
  if (
    geometryById
      .get("redux-square-nodes")
      .nodes.some((node) => node.background.some((shapeValue) => shapeValue.tag === "line"))
  ) {
    throw new Error("Redux unexpectedly drew classic node bottom lines");
  }
  writeJson(path.join(fixtureDir, "timeline-geometry.json"), {
    upstream,
    oracle:
      "timeline LR/TD SVG layout, text wrapping, node paths, connector/axis order, theme CSS paint, and viewBox geometry",
    notes: [
      "Geometry is font-coupled and pinned to bundled Noto Sans in headless Chrome.",
      "Whitespace is intentionally captured by upstream wrap(), so one source space becomes three rendered spaces.",
      "LR draws a dashed connector for every task, including tasks with an empty events array.",
      "TD defines #undefined-arrowhead but references #arrowhead; the missing arrowheads are upstream behavior.",
      "THEME_COLOR_LIMIT=0 produces section-NaN nodes with no generated section CSS.",
    ],
    cases: geometry,
  });

  const config = await captureCases(configCases, (fixture) =>
    stableSource(fixture.body, fixture.config ?? {}),
  );
  const themeColorLimitGate = await captureThemeColorLimitGate();
  assertEqual(
    themeColorLimitGate.length,
    THEME_COLOR_LIMIT_THEMES.length * 2,
    "theme color limit gate case count",
  );
  for (const item of themeColorLimitGate) {
    const expected = THEME_COLOR_LIMIT_GATE_EXPECTED[item.theme]?.[item.limit];
    const actual = item.accept
      ? "accept"
      : item.error?.message?.includes("reading 'l'")
        ? "l"
        : item.error?.message?.includes("reading 'r'")
          ? "r"
          : "other-error";
    assertEqual(actual, expected, `${item.theme} TCL=${item.limit}`);
  }
  const configById = new Map(
    config.map((fixture) => [fixture.id, fixture.expected]),
  );
  const normalize = (value) =>
    JSON.stringify(value, (_key, item) => {
      if (typeof item === "number") return Math.round(item * 1000) / 1000;
      return item;
    }).replace(/timeline-oracle-\d+/g, "timeline-oracle-ID");
  const baseline = normalize(configById.get("default"));
  assertEqual(
    normalize(configById.get("legacy-fields")),
    baseline,
    "combined legacy timeline config",
  );
  for (const key of Object.keys(deadTimelineConfig)) {
    assertEqual(
      normalize(configById.get(`legacy-${key}`)),
      baseline,
      `timeline.${key} inert contract`,
    );
  }
  for (const id of [
    "leftMargin",
    "padding",
    "useMaxWidth",
    "fontFamily",
    "fontSize",
    "themeColorLimit-1",
    "themeColorLimit-0",
    "cScale0",
    "cScaleLabel0",
    "cScaleInv0",
    "fontWeight",
    "theme-dark",
    "theme-redux-color",
    "look-neo-gradient",
  ]) {
    if (normalize(configById.get(id)) === baseline) {
      throw new Error(`${id}: expected live timeline renderer effect`);
    }
  }
  if (
    normalize(configById.get("disableMulticolor")) ===
    normalize(configById.get("disableMulticolor-base"))
  ) {
    throw new Error("disableMulticolor: expected no-section palette effect");
  }
  writeJson(path.join(fixtureDir, "timeline-config.json"), {
    upstream,
    oracle:
      "timeline source-entry renderer config/themeVariables liveness through full SVG snapshots",
    notes: [
      "Only timeline.leftMargin, padding, useMaxWidth, and disableMulticolor are consumed by the renderer.",
      "The inherited Journey/Sequence-shaped timeline fields remain accepted but renderer-inert.",
      "Text uses global themeVariables.fontFamily/fontSize, not timeline.taskFontFamily/taskFontSize.",
      "disableMulticolor only changes timelines without explicit sections.",
    ],
    themeColorLimitGate: {
      oracle:
        "source-entry mermaid.render theme-construction result for all 11 themes at THEME_COLOR_LIMIT 13 and 14",
      cases: themeColorLimitGate,
      nativeOnlyPolicies: [
        {
          value: "Infinity",
          result: "reject",
          reason:
            "Positive Infinity makes Redux CSS rule generation non-terminating; native rendering rejects it as an explicit resource-safety policy rather than claiming an upstream completion result.",
        },
      ],
    },
    cases: config,
  });

  const pixelManifest = [];
  for (const fixture of pixelCases) {
    const page = await preparePage();
    await page.evaluate(
      async ({ family, moduleUrl, renderId, source }) => {
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          look: "classic",
          theme: "default",
          fontFamily: family,
          themeVariables: { fontFamily: family },
        });
        const { svg } = await mermaid.render(renderId, source);
        document.getElementById("container").innerHTML = svg;
        await document.fonts.ready;
        await new Promise((resolve) =>
          requestAnimationFrame(() => requestAnimationFrame(resolve)),
        );
      },
      {
        family: FONT_FAMILY,
        moduleUrl: mermaidModule,
        renderId: fixture.renderId,
        source: fixture.source,
      },
    );
    const root = await page.$("#container > svg");
    const raw = await root.screenshot({ omitBackground: true, type: "png" });
    const bytes = canonicalPng(raw);
    const image = PNG.sync.read(bytes);
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), bytes);
    pixelManifest.push({
      id: fixture.id,
      theme: fixture.theme,
      renderId: fixture.renderId,
      source: fixture.source,
      file,
      width: image.width,
      height: image.height,
      sha256: sha256(bytes),
    });
    await page.close();
  }
  if (new Set(pixelManifest.map((entry) => entry.sha256)).size !== 3) {
    throw new Error("Expected three distinct Timeline pixel oracles");
  }
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream,
    oracle:
      "timeline fixed Noto Sans transparent RGBA raster oracle at DPR 1 for default/dark/redux-color",
    cases: pixelManifest,
  });

  console.log(`geometry cases: ${geometry.length}`);
  console.log(`config cases: ${config.length}`);
  for (const entry of pixelManifest) {
    console.log(
      `pixel ${entry.id}: ${entry.width}x${entry.height} ${entry.sha256}`,
    );
  }
} finally {
  await browser.close();
}
