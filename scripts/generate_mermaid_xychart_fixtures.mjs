import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Freezes Mermaid 11.16.0 xychart-beta renderer behavior:
//   tests/fixtures/mermaid/xychart-geometry.json
//   tests/fixtures/mermaid/xychart-config.json
//   tests/fixtures/mermaid/xychart-pixel/{default,dark,redux-color}.png
//   tests/fixtures/mermaid/xychart-pixel/manifest.json
//
// This is a renderer/layout oracle, not a parser oracle. It records the exact
// D3-derived paths, component allocation, text transforms, SVG paint cascade,
// config liveness, and raster output. Font-coupled geometry is pinned to the
// bundled Noto Sans face used by native Mermaid tests.
//
// Usage:
//   node scripts/generate_mermaid_xychart_fixtures.mjs \
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
const pixelDir = path.join(fixtureDir, "xychart-pixel");
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
const stableSource = (body, config = {}) => {
  const xyTheme = config.themeVariables?.xyChart;
  return sourceInit(
    {
      ...config,
      fontFamily: FONT_FAMILY,
      themeVariables: {
        ...(config.themeVariables ?? {}),
        fontFamily: FONT_FAMILY,
        ...(xyTheme === undefined ? {} : { xyChart: { ...xyTheme } }),
      },
    },
    body,
  );
};

const CANONICAL = [
  "xychart-beta vertical",
  'title "Sales"',
  'x-axis "Month" [Jan, Feb, Mar]',
  'y-axis "Value" 0 --> 100',
  "bar [20, 50, 80]",
  'line [10 "low", 60, 90 "high"]',
].join("\n");

const geometryCases = [
  { id: "vertical-band", source: stableSource(CANONICAL) },
  {
    id: "horizontal-band",
    source: stableSource(CANONICAL.replace("vertical", "horizontal")),
  },
  {
    id: "vertical-linear-explicit",
    source: stableSource(
      [
        "xychart-beta vertical",
        'title "Linear"',
        'x-axis "Time" -2 --> 2',
        'y-axis "Reading" -10 --> 10',
        "bar [-5, 0, 5, 10]",
        "line [-10, -2, 7, 3]",
      ].join("\n"),
    ),
  },
  {
    id: "horizontal-linear-explicit",
    source: stableSource(
      [
        "xychart-beta horizontal",
        'x-axis "Time" -2 --> 2',
        'y-axis "Reading" -10 --> 10',
        "bar [-5, 0, 5, 10]",
        "line [-10, -2, 7, 3]",
      ].join("\n"),
    ),
  },
  {
    id: "inferred-domains",
    source: stableSource(
      "xychart-beta\nbar [5, -3, 12, 4]\nline [2, 18, -7, 9]",
    ),
  },
  {
    id: "equal-linear-domains",
    source: stableSource(
      "xychart-beta\nx-axis 5 --> 5\ny-axis 2 --> 2\nbar [2]\nline [2]",
    ),
  },
  {
    id: "single-point-inferred",
    source: stableSource('xychart-beta\nline [7 "only"]'),
  },
  {
    id: "band-truncate-and-short",
    source: stableSource(
      [
        "xychart-beta",
        "x-axis [A, B, C]",
        "bar [1, 2, 3, 999]",
        'line [4 "a", 5 "b"]',
      ].join("\n"),
    ),
  },
  {
    id: "rotated-bottom-labels",
    source: stableSource(CANONICAL, {
      xyChart: { xAxis: { labelRotation: 45 } },
    }),
  },
  {
    id: "component-visibility",
    source: stableSource(CANONICAL, {
      xyChart: {
        showTitle: false,
        xAxis: { showAxisLine: false, showTick: false, showTitle: false },
        yAxis: { showLabel: false },
      },
    }),
  },
  {
    id: "reserved-space-low",
    source: stableSource(CANONICAL, {
      xyChart: { plotReservedSpacePercent: 20 },
    }),
  },
  {
    id: "reserved-space-high",
    source: stableSource(CANONICAL, {
      xyChart: { plotReservedSpacePercent: 95 },
    }),
  },
  {
    id: "vertical-data-label-inside",
    source: stableSource(CANONICAL, {
      xyChart: { showDataLabel: true, showDataLabelOutsideBar: false },
    }),
  },
  {
    id: "vertical-data-label-outside",
    source: stableSource(CANONICAL, {
      xyChart: { showDataLabel: true, showDataLabelOutsideBar: true },
    }),
  },
  {
    id: "horizontal-data-label-inside",
    source: stableSource(CANONICAL.replace("vertical", "horizontal"), {
      xyChart: { showDataLabel: true, showDataLabelOutsideBar: false },
    }),
  },
  {
    id: "data-label-first-plot-quirk",
    source: stableSource(
      [
        "xychart-beta",
        "x-axis [A, B, C]",
        "line [91, 82, 73]",
        "bar [10, 20, 30]",
        "bar [40, 50, 60]",
      ].join("\n"),
      { xyChart: { showDataLabel: true } },
    ),
  },
];

const topConfigCases = [
  ["useMaxWidth-false", "useMaxWidth", false],
  ["width", "width", 640],
  ["height", "height", 420],
  ["titleFontSize", "titleFontSize", 32],
  ["titlePadding", "titlePadding", 23],
  ["showDataLabel", "showDataLabel", true],
  ["showDataLabelOutsideBar", "showDataLabelOutsideBar", true],
  ["showTitle", "showTitle", false],
  ["chartOrientation", "chartOrientation", "horizontal"],
  ["plotReservedSpacePercent", "plotReservedSpacePercent", 70],
].map(([id, key, value]) => ({ id, config: { xyChart: { [key]: value } } }));

const axisSentinels = [
  ["showLabel", false],
  ["labelFontSize", 24],
  ["labelPadding", 17],
  ["showTitle", false],
  ["titleFontSize", 26],
  ["titlePadding", 19],
  ["showTick", false],
  ["tickLength", 13],
  ["tickWidth", 7],
  ["showAxisLine", false],
  ["axisLineWidth", 9],
  ["labelRotation", 45],
];
const axisConfigCases = ["xAxis", "yAxis"].flatMap((axis) =>
  axisSentinels.map(([key, value]) => ({
    id: `${axis}.${key}`,
    config: { xyChart: { [axis]: { [key]: value } } },
  })),
);

const themeSentinels = [
  ["backgroundColor", "#102030"],
  ["titleColor", "#112233"],
  ["dataLabelColor", "#123456"],
  ["xAxisTitleColor", "#213141"],
  ["xAxisLabelColor", "#223242"],
  ["xAxisTickColor", "#233343"],
  ["xAxisLineColor", "#243444"],
  ["yAxisTitleColor", "#314151"],
  ["yAxisLabelColor", "#324252"],
  ["yAxisTickColor", "#334353"],
  ["yAxisLineColor", "#344454"],
  ["plotColorPalette", "#ff0000, #00ff00, #0000ff"],
];
const themeConfigCases = themeSentinels.map(([key, value]) => ({
  id: `theme.${key}`,
  config: {
    xyChart: key === "dataLabelColor" ? { showDataLabel: true } : {},
    themeVariables: { xyChart: { [key]: value } },
  },
}));

const configCases = [
  { id: "default", config: {} },
  ...topConfigCases,
  ...axisConfigCases,
  ...themeConfigCases,
  {
    id: "useMaxWidth-true",
    config: { xyChart: { useMaxWidth: true } },
  },
  {
    id: "orientation-source-wins",
    config: { xyChart: { chartOrientation: "horizontal" } },
    body: CANONICAL,
  },
  {
    id: "axis-rotation-out-of-range",
    config: { xyChart: { xAxis: { labelRotation: 91 } } },
  },
  {
    id: "theme-flat-key-ignored",
    config: { themeVariables: { xAxisLineColor: "#ff0000" } },
  },
  { id: "coerce-width-string", config: { xyChart: { width: "640" } } },
  {
    id: "coerce-title-font-string",
    config: { xyChart: { titleFontSize: "32" } },
  },
  {
    id: "coerce-show-label-number",
    config: { xyChart: { xAxis: { showLabel: 0 } } },
  },
  {
    id: "coerce-show-data-label-number",
    config: { xyChart: { showDataLabel: 1 } },
  },
];

const PIXEL_BODY = [
  "xychart-beta vertical",
  'title "Quarterly revenue"',
  'x-axis "Quarter" [Q1, Q2, Q3, Q4, Q5]',
  'y-axis "Units" -20 --> 100',
  "bar [10, 45, 80, 60, -10]",
  'line [15 "start", 40, 90 "peak", 55, 0 "end"]',
].join("\n");
const pixelCases = [
  { id: "default", theme: "default" },
  { id: "dark", theme: "dark" },
  { id: "redux-color", theme: "redux-color" },
].map(({ id, theme }) => ({
  id,
  theme,
  renderId: `xychart-pixel-${id}`,
  source: stableSource(PIXEL_BODY, { theme }),
}));
pixelCases.push(
  {
    id: "source-order-rotation",
    theme: "default",
    renderId: "xychart-pixel-source-order-rotation",
    source: stableSource(
      [
        "xychart-beta vertical",
        'x-axis "Category" [A, B, C]',
        'y-axis "Value" 0 --> 100',
        "line [100, 0, 100]",
        "bar [80, 80, 80]",
      ].join("\n"),
      {
        xyChart: { xAxis: { labelRotation: 45 } },
        themeVariables: {
          xyChart: { plotColorPalette: "#ff0000,#0000ff" },
        },
      },
    ),
  },
  {
    id: "hanging-data-label",
    theme: "default",
    renderId: "xychart-pixel-hanging-data-label",
    source: stableSource(
      [
        "xychart-beta vertical",
        "x-axis [A, B, C]",
        "line [91, 82, 73]",
        "bar [10, 20, 30]",
      ].join("\n"),
      {
        xyChart: { showDataLabel: true, showDataLabelOutsideBar: false },
        themeVariables: { xyChart: { dataLabelColor: "#ff0000" } },
      },
    ),
  },
);

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
    generator: "scripts/generate_mermaid_xychart_fixtures.mjs",
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
        style.id = "xychart-oracle-font";
        style.textContent = `@font-face{font-family:"${family}";src:url("${url}");font-style:normal;font-weight:400}html,body{margin:0;padding:0}`;
        document.head.appendChild(style);
        await document.fonts.load(`16px "${family}"`, "XYChart 0123456789");
        await document.fonts.ready;
      },
      { family: FONT_FAMILY, url: fontUrl },
    );
    return page;
  };

  const captureCases = async (cases, sourceForCase, compact = false) => {
    const page = await preparePage();
    const result = await page.evaluate(
      async ({ cases: browserCases, family, moduleUrl }) => {
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
        const box = (element, client = false) => {
          if (!element) return null;
          return rect(client ? element.getBoundingClientRect() : element.getBBox());
        };
        const computed = (element, names) => {
          if (!element) return null;
          const style = getComputedStyle(element);
          return Object.fromEntries(
            names.map((name) => [name, style.getPropertyValue(name)]),
          );
        };
        const shape = (element) => ({
          tag: element.tagName.toLowerCase(),
          attrs: attrs(element, [
            "x",
            "y",
            "width",
            "height",
            "d",
            "fill",
            "stroke",
            "stroke-width",
            "font-size",
            "dominant-baseline",
            "text-anchor",
            "transform",
          ]),
          ...(element.tagName.toLowerCase() === "text"
            ? { text: element.textContent }
            : {}),
          bbox: box(element),
          ...(element.tagName.toLowerCase() === "text"
            ? { clientBox: box(element, true) }
            : {}),
        });
        const node = (element) =>
          element.tagName.toLowerCase() === "g"
            ? {
                tag: "g",
                class: element.getAttribute("class"),
                children: [...element.children].map(node),
              }
            : shape(element);
        const group = (root, selector) => {
          const element = root.querySelector(selector);
          if (!element) return null;
          return {
            class: element.getAttribute("class"),
            children: [...element.children].map(node),
          };
        };
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
            clientBox: box(root, true),
            computed: computed(root, ["font-family", "font-size"]),
          },
          mainOrder: [...root.querySelector("g.main").children].map((element) => ({
            tag: element.tagName.toLowerCase(),
            class: element.getAttribute("class"),
          })),
          background: shape(root.querySelector("rect.background")),
          title: group(root, "g.chart-title"),
          plots: [...root.querySelectorAll("g.plot > g")].map((element) => ({
            class: element.getAttribute("class"),
            children: [...element.children].map(node),
          })),
          bottomAxis: group(root, "g.bottom-axis"),
          leftAxis: group(root, "g.left-axis"),
          topAxis: group(root, "g.top-axis"),
          rightAxis: group(root, "g.right-axis"),
        });
        const firstLast = (values) =>
          values.length < 2 ? values : [values[0], values[values.length - 1]];
        const compactAxis = (root, selector) => {
          const element = root.querySelector(selector);
          if (!element) return null;
          const children = (className) => [
            ...element.querySelectorAll(`:scope > g.${className} > *`),
          ];
          const labels = children("label");
          const ticks = children("ticks");
          const lines = [
            ...element.querySelectorAll(
              ":scope > g.axis-line > *, :scope > g.axisl-line > *",
            ),
          ];
          const titles = children("title");
          return {
            axisLines: lines.map(shape),
            labelCount: labels.length,
            labels: firstLast(labels).map(shape),
            tickCount: ticks.length,
            ticks: firstLast(ticks).map(shape),
            titles: titles.map(shape),
          };
        };
        const compactSnapshot = (root) => ({
          root: {
            attrs: attrs(root, ["width", "height", "viewBox", "style"]),
            clientBox: box(root, true),
            computed: computed(root, ["font-family", "font-size"]),
          },
          background: shape(root.querySelector("rect.background")),
          title: [...root.querySelectorAll("g.chart-title > text")].map(shape),
          plots: [...root.querySelectorAll("g.plot > g")].map((element) => ({
            class: element.getAttribute("class"),
            childCount: element.children.length,
            children: firstLast([...element.children]).map(node),
          })),
          bottomAxis: compactAxis(root, "g.bottom-axis"),
          leftAxis: compactAxis(root, "g.left-axis"),
          topAxis: compactAxis(root, "g.top-axis"),
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
            `xychart-oracle-${index}`,
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
            expected: fixture.compact ? compactSnapshot(root) : snapshot(root),
          });
        }
        return output;
      },
      {
        cases: cases.map((fixture) => ({
          id: fixture.id,
          source: sourceForCase(fixture),
          compact,
        })),
        family: FONT_FAMILY,
        moduleUrl: mermaidModule,
      },
    );
    await page.close();
    return result;
  };

  const geometry = await captureCases(geometryCases, (fixture) => fixture.source);
  const geometryById = new Map(geometry.map((fixture) => [fixture.id, fixture]));
  const canonicalOrder = geometryById
    .get("vertical-band")
    .expected.mainOrder.map(({ tag, class: className }) => `${tag}.${className}`);
  assertEqual(
    canonicalOrder.join(","),
    "rect.background,g.chart-title,g.plot,g.bottom-axis,g.left-axis",
    "xychart drawable order",
  );
  const quirkPlots = geometryById.get("data-label-first-plot-quirk").expected.plots;
  const nestedTexts = (nodeValue) =>
    nodeValue.text === undefined
      ? (nodeValue.children ?? []).flatMap(nestedTexts)
      : [nodeValue.text];
  for (const plot of quirkPlots.filter((value) => value.class.startsWith("bar-"))) {
    assertEqual(
      plot.children.flatMap(nestedTexts).join(","),
      "91,82,73",
      `${plot.class} first-plot data labels`,
    );
  }
  writeJson(path.join(fixtureDir, "xychart-geometry.json"), {
    upstream,
    oracle:
      "xychart-beta rendered SVG component order, D3 paths/scales, rect geometry, text transforms/metrics, attributes, and computed paint",
    notes: [
      "Geometry is font-coupled and is pinned to bundled Noto Sans in headless Chrome.",
      "D3 linear paths use pathRound(3); raw rect and axis attributes retain upstream double precision.",
      "Bar baselines are the plot boundary/domain minimum, not numeric zero.",
      "The data-label-first-plot-quirk case freezes the renderer's use of plots[0] labels for every bar series.",
      "plotReservedSpacePercent controls component admission before unused space is returned to the plot.",
    ],
    cases: geometry,
  });

  const config = await captureCases(
    configCases,
    (fixture) => stableSource(fixture.body ?? CANONICAL, fixture.config),
    true,
  );
  const configById = new Map(config.map((fixture) => [fixture.id, fixture.expected]));
  const normalizeNumericText = (value) =>
    value.replace(
      /-?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?/g,
      (token) => String(Math.round(Number(token) * 1000) / 1000),
    );
  const serialized = (id) =>
    JSON.stringify(configById.get(id), (_key, value) => {
      if (typeof value === "number") return Math.round(value * 1000) / 1000;
      if (typeof value === "string") return normalizeNumericText(value);
      return value;
    });
  const baseline = serialized("default");
  for (const id of [
    "useMaxWidth-false",
    "useMaxWidth-true",
    "showDataLabelOutsideBar",
    "yAxis.labelRotation",
    "orientation-source-wins",
    "axis-rotation-out-of-range",
    "theme-flat-key-ignored",
  ]) {
    assertEqual(serialized(id), baseline, `${id} inert/overridden contract`);
  }
  for (const id of [
    "width",
    "height",
    "xAxis.labelFontSize",
    "yAxis.axisLineWidth",
    "theme.backgroundColor",
    "theme.plotColorPalette",
    "coerce-width-string",
    "coerce-title-font-string",
    "coerce-show-label-number",
    "coerce-show-data-label-number",
  ]) {
    if (serialized(id) === baseline) throw new Error(`${id}: expected live config effect`);
  }
  writeJson(path.join(fixtureDir, "xychart-config.json"), {
    upstream,
    oracle:
      "xychart source-entry config/themeVariables liveness through rendered SVG snapshots",
    notes: [
      "xyChart.useMaxWidth is renderer-inert because upstream passes true to configureSvgSize unconditionally.",
      "An explicit source orientation overrides config.chartOrientation.",
      "Only themeVariables.xyChart is consumed; flat xAxis/yAxis color keys are ignored.",
      "yAxis.labelRotation is inert in both supported orientations; xAxis.labelRotation is visible only when xAxis is drawn at the bottom.",
      "dataLabelColor is probed with showDataLabel enabled because labels are disabled by default.",
    ],
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
  if (new Set(pixelManifest.map((entry) => entry.sha256)).size !== pixelCases.length) {
    throw new Error("Expected distinct XYChart pixel oracles");
  }
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream,
    oracle:
      "xychart-beta fixed Noto Sans RGBA raster oracle at DPR 1 for default/dark/redux-color",
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
