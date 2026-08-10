import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Freezes Mermaid 11.16.0 packet-beta renderer behavior:
//   tests/fixtures/mermaid/packet-geometry.json
//   tests/fixtures/mermaid/packet-config.json
//   tests/fixtures/mermaid/packet-pixel/{default,dark,forest}.png
//   tests/fixtures/mermaid/packet-pixel/manifest.json
//
// Usage:
//   node scripts/generate_mermaid_packet_fixtures.mjs \
//     [mermaid-root] [fixture-dir] [chrome-exe]

const EXPECTED_MERMAID_VERSION = "11.16.0";
const EXPECTED_MERMAID_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA256 =
  "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";
const VIEWPORT = { width: 1400, height: 1000, deviceScaleFactor: 1 };
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
const pixelDir = path.join(fixtureDir, "packet-pixel");
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
  if (actual !== expected)
    throw new Error(`${label}: expected ${expected}, found ${actual}`);
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
  const rootFamily = config.fontFamily ?? FONT_FAMILY;
  const themeFamily = config.themeVariables?.fontFamily ?? rootFamily;
  return sourceInit(
    {
      ...config,
      fontFamily: rootFamily,
      themeVariables: {
        ...(config.themeVariables ?? {}),
        fontFamily: themeFamily,
      },
    },
    body,
  );
};

const CANONICAL = [
  "packet-beta",
  "title Packet header",
  '0-7: "Version"',
  '8-15: "Type"',
  '16-31: "Payload"',
].join("\n");
const BASE = [
  "packet-beta",
  '0-7: "Version"',
  '8-15: "Type"',
  '16-31: "Payload"',
].join("\n");

const geometryCases = [
  { id: "canonical", source: stableSource(CANONICAL) },
  { id: "single-bit", source: stableSource('packet-beta\n0: "Flag"') },
  {
    id: "implicit-fields",
    source: stableSource('packet-beta\n+8: "A"\n+8: "B"\n+16: "C"'),
  },
  { id: "cross-row-explicit", source: stableSource('packet-beta\n0-39: "Across"') },
  { id: "cross-row-bits", source: stableSource('packet-beta\n+40: "Across"') },
  { id: "empty", source: stableSource("packet-beta") },
  { id: "title-only", source: stableSource("packet-beta\ntitle Packet title") },
  {
    id: "show-bits-false",
    source: stableSource(BASE, { packet: { showBits: false } }),
  },
  {
    id: "custom-geometry",
    source: stableSource('packet-beta\n0-3: "A"\n4-11: "B"', {
      packet: {
        rowHeight: 40,
        bitWidth: 20,
        bitsPerRow: 8,
        paddingX: 2,
        paddingY: 3,
      },
    }),
  },
  {
    id: "fractional-bits-per-row",
    source: stableSource('packet-beta\n0-19: "Fractional"', {
      packet: { bitsPerRow: 8.5, bitWidth: 10 },
    }),
  },
  {
    id: "string-arithmetic",
    source: stableSource('packet-beta\n0-7: "String"', {
      packet: {
        rowHeight: "40",
        bitWidth: "20",
        bitsPerRow: "8",
        paddingX: "2",
        paddingY: "3",
      },
    }),
  },
  {
    id: "negative-padding",
    source: stableSource(BASE, { packet: { paddingX: -5, paddingY: -3 } }),
  },
  {
    id: "zero-dimensions",
    source: stableSource('packet-beta\n0-31: "Zero"', {
      packet: {
        rowHeight: 0,
        bitWidth: 0,
        paddingX: 0,
        paddingY: 0,
        showBits: false,
      },
    }),
  },
  {
    id: "invalid-height-negative",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { rowHeight: -100, paddingY: 0, showBits: false },
    }),
  },
  {
    id: "invalid-height-nan",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { rowHeight: "abc" },
    }),
  },
  {
    id: "invalid-width-zero",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { bitWidth: -0.0625 },
    }),
  },
  {
    id: "invalid-width-negative",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { bitWidth: -1 },
    }),
  },
  {
    id: "invalid-width-nan",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { bitWidth: "abc" },
    }),
  },
  {
    id: "invalid-both-nan",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { bitWidth: "abc", rowHeight: "abc" },
    }),
  },
  {
    id: "fixed-zero-viewport",
    source: stableSource('packet-beta\n0: "A"', {
      packet: {
        bitWidth: -0.0625,
        rowHeight: 0,
        paddingY: 0,
        showBits: false,
        useMaxWidth: false,
      },
    }),
  },
  {
    id: "fixed-nan-viewport",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { bitWidth: "abc", rowHeight: "abc", useMaxWidth: false },
    }),
  },
  {
    id: "fixed-negative-width",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { bitWidth: -1, useMaxWidth: false },
    }),
  },
  {
    id: "fixed-negative-height",
    source: stableSource('packet-beta\n0: "A"', {
      packet: {
        rowHeight: -100,
        paddingY: 0,
        showBits: false,
        useMaxWidth: false,
      },
    }),
  },
  {
    id: "fixed-negative-both",
    source: stableSource('packet-beta\n0: "A"', {
      packet: {
        bitWidth: -1,
        rowHeight: -100,
        paddingY: 0,
        showBits: false,
        useMaxWidth: false,
      },
    }),
  },
  {
    id: "negative-row-height",
    source: stableSource('packet-beta\n0-7: "Negative"', {
      packet: { rowHeight: -4 },
    }),
  },
  {
    id: "over-padding",
    source: stableSource('packet-beta\n0: "A"', {
      packet: { bitWidth: 4, bitsPerRow: 8, paddingX: 10 },
    }),
  },
  {
    id: "long-label-clips",
    source: stableSource(
      'packet-beta\n0: "THIS IS AN EXTREMELY LONG LABEL THAT MUST OVERFLOW THE SINGLE BIT"',
    ),
  },
  {
    id: "long-title-clips",
    source: stableSource(
      'packet-beta\ntitle THIS IS AN EXTREMELY LONG PACKET TITLE THAT MUST OVERFLOW THE VIEWBOX\n0: "A"',
    ),
  },
  {
    id: "fixed-size",
    source: stableSource(BASE, { packet: { useMaxWidth: false } }),
  },
];

const configCases = [
  { id: "default", source: stableSource(BASE) },
  {
    id: "style-baseline",
    source: stableSource("packet-beta\ntitle Styled\n0-7: \"A\""),
  },
  ...geometryCases.filter((entry) =>
    [
      "show-bits-false",
      "custom-geometry",
      "fractional-bits-per-row",
      "string-arithmetic",
      "negative-padding",
      "zero-dimensions",
      "invalid-height-negative",
      "invalid-height-nan",
      "invalid-width-zero",
      "invalid-width-negative",
      "invalid-width-nan",
      "invalid-both-nan",
      "fixed-zero-viewport",
      "fixed-nan-viewport",
      "fixed-negative-width",
      "fixed-negative-height",
      "fixed-negative-both",
      "negative-row-height",
      "over-padding",
      "fixed-size",
    ].includes(entry.id),
  ),
  {
    id: "show-bits-string-false",
    source: stableSource(BASE, { packet: { showBits: "false" } }),
  },
  {
    id: "use-max-width-string-false",
    source: stableSource(BASE, { packet: { useMaxWidth: "false" } }),
  },
  {
    id: "row-height-array",
    source: stableSource('packet-beta\n0-7: "A"', { packet: { rowHeight: [40] } }),
  },
  {
    id: "row-height-bool",
    source: stableSource('packet-beta\n0-7: "A"', { packet: { rowHeight: true } }),
  },
  {
    id: "padding-arrays",
    source: stableSource('packet-beta\n0-7: "A"', {
      packet: { paddingX: [2], paddingY: [3] },
    }),
  },
  {
    id: "font-fallback-ex-ch",
    source: stableSource("packet-beta\ntitle Fallback\n0-7: \"A\"", {
      fontFamily: "DefinitelyMissing, Noto Sans",
      themeVariables: {
        packet: { labelFontSize: "10ex", titleFontSize: "10ch" },
      },
    }),
  },
  ...["default", "dark", "forest", "neutral", "base", "neo", "neo-dark", "redux", "redux-dark", "redux-color", "redux-dark-color"].map(
    (theme) => ({
      id: `theme-${theme}`,
      source: stableSource("packet-beta\ntitle Theme\n0-7: \"A\"", { theme }),
    }),
  ),
];

const packetStyleValues = {
  byteFontSize: "18px",
  startByteColor: "#f00",
  endByteColor: "#0f0",
  labelColor: "#00f",
  labelFontSize: "20px",
  titleColor: "#f0f",
  titleFontSize: "24px",
  blockStrokeColor: "#0ff",
  blockStrokeWidth: "4",
  blockFillColor: "#ff0",
};
for (const [key, value] of Object.entries(packetStyleValues)) {
  configCases.push({
    id: `style-${key}`,
    source: stableSource("packet-beta\ntitle Styled\n0-7: \"A\"", {
      themeVariables: { packet: { [key]: value } },
    }),
  });
}

const pixelCases = [
  { id: "default", theme: "default", source: stableSource(CANONICAL) },
  {
    id: "dark",
    theme: "dark",
    source: stableSource('packet-beta\ntitle Dark packet\n0-39: "Across rows"', {
      theme: "dark",
    }),
  },
  {
    id: "forest",
    theme: "forest",
    source: stableSource('packet-beta\ntitle Forest packet\n0: "Flag"\n1-31: "Payload"', {
      theme: "forest",
    }),
  },
];

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
    generator: "scripts/generate_mermaid_packet_fixtures.mjs",
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
        style.textContent = `@font-face{font-family:"${family}";src:url("${url}");font-style:normal;font-weight:400}html,body{margin:0;padding:0}`;
        document.head.appendChild(style);
        await document.fonts.load(`16px "${family}"`, "Packet 0123456789");
        await document.fonts.ready;
      },
      { family: FONT_FAMILY, url: fontUrl },
    );
    return page;
  };

  const captureCases = async (cases) => {
    const page = await preparePage();
    const output = await page.evaluate(
      async ({ browserCases, family, moduleUrl }) => {
        const { default: mermaid } = await import(moduleUrl);
        const round = (value) =>
          Number.isFinite(value) ? Math.round(value * 1000) / 1000 : null;
        const box = (value) => ({
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
        const style = (element) => {
          const value = getComputedStyle(element);
          return {
            fill: value.fill,
            stroke: value.stroke,
            strokeWidth: value.strokeWidth,
            fontFamily: value.fontFamily,
            fontSize: value.fontSize,
            textAnchor: value.textAnchor,
            dominantBaseline: value.dominantBaseline,
            overflow: value.overflow,
          };
        };
        const snapshot = (root) => ({
          root: {
            attrs: attrs(root, ["width", "height", "viewBox", "style", "role", "aria-roledescription"]),
            bbox: box(root.getBBox()),
            clientBox: box(root.getBoundingClientRect()),
            computed: style(root),
          },
          directOrder: [...root.children].map((element) => ({
            tag: element.tagName.toLowerCase(),
            class: element.getAttribute("class") ?? "",
          })),
          groups: [...root.querySelectorAll(":scope > g")].map((group) => ({
            order: [...group.children].map((element) => ({
              tag: element.tagName.toLowerCase(),
              class: element.getAttribute("class") ?? "",
              text: element.textContent ?? "",
            })),
          })),
          rects: [...root.querySelectorAll("rect")].map((element) => ({
            class: element.getAttribute("class") ?? "",
            attrs: attrs(element, ["x", "y", "width", "height"]),
            bbox: box(element.getBBox()),
            computed: style(element),
          })),
          texts: [...root.querySelectorAll("text")].map((element) => ({
            class: element.getAttribute("class") ?? "",
            text: element.textContent,
            attrs: attrs(element, ["x", "y", "text-anchor", "dominant-baseline"]),
            bbox: box(element.getBBox()),
            computed: style(element),
          })),
        });

        const result = [];
        for (let index = 0; index < browserCases.length; ++index) {
          const fixture = browserCases[index];
          mermaid.initialize({
            startOnLoad: false,
            securityLevel: "strict",
            theme: "default",
            fontFamily: family,
            themeVariables: { fontFamily: family },
          });
          const { svg } = await mermaid.render(`packet-oracle-${index}`, fixture.source);
          document.getElementById("container").innerHTML = svg;
          await document.fonts.ready;
          await new Promise((resolve) =>
            requestAnimationFrame(() => requestAnimationFrame(resolve)),
          );
          result.push({
            id: fixture.id,
            source: fixture.source,
            expected: snapshot(document.querySelector("#container > svg")),
          });
        }
        return result;
      },
      { browserCases: cases, family: FONT_FAMILY, moduleUrl: mermaidModule },
    );
    await page.close();
    return output;
  };

  const geometry = await captureCases(geometryCases);
  assertEqual(geometry.length, 29, "packet geometry case count");
  const geometryById = new Map(geometry.map((entry) => [entry.id, entry.expected]));
  assertEqual(geometryById.get("canonical").root.attrs.viewBox, "0 0 1026 94", "canonical viewBox");
  assertEqual(geometryById.get("cross-row-explicit").rects.length, 2, "cross-row split");
  assertEqual(geometryById.get("empty").root.attrs.viewBox, "0 0 1026 15", "empty viewBox");
  assertEqual(geometryById.get("fractional-bits-per-row").rects.length, 3, "fractional rows");
  const expectedInvalidClients = {
    "zero-dimensions": [2, 150],
    "invalid-height-negative": [1026, 150],
    "invalid-height-nan": [1026, 150],
    "invalid-width-zero": [0, 150],
    "invalid-width-negative": [1400, 150],
    "invalid-width-nan": [1400, 150],
    "invalid-both-nan": [1400, 150],
    "fixed-zero-viewport": [0, 0],
    "fixed-nan-viewport": [1400, 150],
    "fixed-negative-width": [0, 62],
    "fixed-negative-height": [1026, 0],
    "fixed-negative-both": [0, 0],
  };
  for (const [id, expected] of Object.entries(expectedInvalidClients)) {
    const client = geometryById.get(id).root.clientBox;
    assertEqual(client.width, expected[0], `${id} client width`);
    assertEqual(client.height, expected[1], `${id} client height`);
  }
  writeJson(path.join(fixtureDir, "packet-geometry.json"), {
    upstream,
    oracle: "packet fixed-viewBox row/block geometry, DOM order, SVG text anchors and clipping",
    notes: [
      "Packet never measures text for layout; labels and titles remain one SVG text run and clip at the fixed root viewport.",
      "showBits truthiness adds 10 to paddingY before the row-stride calculation.",
      "String-valued scalar config retains JavaScript + concatenation quirks.",
      "Cross-row fields repeat their label in every generated row fragment.",
    ],
    cases: geometry,
  });

  const config = await captureCases(configCases);
  assertEqual(config.length, 49, "packet config case count");
  const configById = new Map(config.map((entry) => [entry.id, entry.expected]));
  const normalized = (value) => JSON.stringify(value);
  const baseline = normalized(configById.get("style-baseline"));
  for (const id of ["style-labelColor", "style-labelFontSize", "style-titleColor", "style-titleFontSize"])
    if (normalized(configById.get(id)) === baseline)
      throw new Error(`${id}: expected live packet style effect`);
  for (const id of ["style-byteFontSize", "style-startByteColor", "style-endByteColor", "style-blockStrokeColor", "style-blockStrokeWidth", "style-blockFillColor"])
    if (normalized(configById.get(id)) !== baseline)
      throw new Error(`${id}: expected upstream-inert packet style key`);
  writeJson(path.join(fixtureDir, "packet-config.json"), {
    upstream,
    oracle: "packet source-entry config/theme/style liveness and JavaScript raw-type behavior",
    notes: [
      "Live nested packet theme keys are labelColor, labelFontSize, titleColor and titleFontSize.",
      "byteFontSize/startByteColor/endByteColor/blockStrokeColor/blockStrokeWidth/blockFillColor are overwritten by renderer style defaults in source-entry rendering.",
      "Only dark and forest provide packet-specific theme colors; newer neo/redux themes use packet defaults.",
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
        renderId: `packet-pixel-${fixture.id}`,
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
      source: fixture.source,
      file,
      width: image.width,
      height: image.height,
      sha256: sha256(bytes),
    });
    await page.close();
  }
  assertEqual(new Set(pixelManifest.map((entry) => entry.sha256)).size, 3, "distinct packet pixels");
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream,
    oracle: "packet fixed Noto Sans transparent RGBA raster oracle at DPR 1 for default/dark/forest",
    cases: pixelManifest,
  });

  console.log(`geometry cases: ${geometry.length}`);
  console.log(`config cases: ${config.length}`);
  for (const entry of pixelManifest)
    console.log(`pixel ${entry.id}: ${entry.width}x${entry.height} ${entry.sha256}`);
} finally {
  await browser.close();
}
