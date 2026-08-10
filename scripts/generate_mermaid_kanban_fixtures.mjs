import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Freezes Mermaid 11.16.0 Kanban renderer behavior. The renderer intentionally
// has several cross-family config reads (mindmap padding/useMaxWidth) which are
// part of the compatibility contract captured here.
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
const pixelDir = path.join(fixtureDir, "kanban-pixel");
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const fontFile = path.resolve(
  "third_party", "noto", "fonts", "NotoSans-Regular.ttf",
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
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MERMAID_MODULE_SHA256,
            "Mermaid module sha256");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256,
            "Chrome sha256");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256,
            "NotoSans-Regular.ttf sha256");

const mermaidModule = pathToFileURL(moduleFile).href;
const fontUrl = pathToFileURL(fontFile).href;
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib",
                          "puppeteer", "puppeteer.js")),
);

const sourceInit = (config, body) =>
  `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const stableSource = (body, config = {}) => sourceInit({
  ...config,
  fontFamily: FONT_FAMILY,
  handDrawnSeed: config.handDrawnSeed ?? 7,
  themeVariables: {
    ...(config.themeVariables ?? {}),
    fontFamily: FONT_FAMILY,
  },
}, body);
const frontmatterSource = (title, body) =>
  `---\ntitle: ${title}\n---\n${stableSource(body)}`;

const BASE = [
  "kanban",
  "  todo[Todo]",
  "    task1[Write docs]",
  "    task2[Review **carefully** with a long label that should wrap]",
  "  done[Done]",
  "    task3[Ship]",
].join("\n");
const META = [
  "kanban",
  "  todo[Todo]",
  "    task1[Write docs]@{ assigned: 'Ada', ticket: KAN-12, priority: 'High' }",
].join("\n");

const geometryCases = [
  { id: "canonical", source: stableSource(BASE) },
  { id: "metadata", source: stableSource(META) },
  { id: "empty-section", source: stableSource("kanban\n  todo[Todo]") },
  { id: "implicit-ids", source: stableSource("kanban\n  Todo\n    Task one") },
  {
    id: "markdown-wrap",
    source: stableSource([
      "kanban", "  todo[**Todo**]",
      "    a[alpha beta gamma delta epsilon zeta eta theta iota kappa lambda]",
      "    b[line one<br/>line two]",
    ].join("\n")),
  },
  {
    id: "unbroken-overflow",
    source: stableSource("kanban\n  todo[Todo]\n    a[SUPERCALIFRAGILISTICEXPIALIDOCIOUS0123456789]"),
  },
  {
    id: "long-section-title",
    source: stableSource("kanban\n  todo[This is a very long section heading that exceeds the column width]\n    a[A]"),
  },
  {
    id: "unbroken-section-title",
    source: stableSource("kanban\n  todo[SUPERCALIFRAGILISTICEXPIALIDOCIOUS0123456789]\n    a[A]"),
  },
  {
    id: "duplicate-section-id",
    source: stableSource("kanban\n  same[First]\n    a[A]\n  same[Second]\n    b[B]"),
  },
  {
    id: "three-sections",
    source: stableSource("kanban\n  a[A]\n    aa[one]\n  b[B]\n    bb[two]\n  c[C]\n    cc[three]"),
  },
  {
    id: "section-width-80",
    source: stableSource(BASE, { kanban: { sectionWidth: 80 } }),
  },
  {
    id: "section-width-negative",
    source: stableSource(BASE, { kanban: { sectionWidth: -20 } }),
  },
  {
    id: "section-width-invalid",
    source: stableSource(BASE, { kanban: { sectionWidth: "abc" } }),
  },
  { id: "html-labels-false", source: stableSource(BASE, { htmlLabels: false }) },
  { id: "look-neo", source: stableSource(BASE, { look: "neo" }) },
  { id: "look-neo-drop-shadow-none", source: stableSource(BASE, {
      look: "neo", themeVariables: { dropShadow: "none" },
    }) },
  { id: "look-neo-drop-shadow-custom", source: stableSource(BASE, {
      look: "neo", themeVariables: {
        dropShadow: "drop-shadow(4px 5px 3px rgba(10,20,30,0.4))",
      },
    }) },
  { id: "look-neo-drop-shadow-invalid", source: stableSource(BASE, {
      look: "neo", themeVariables: { dropShadow: "bogus" },
    }) },
  { id: "look-hand-drawn", source: stableSource(BASE, { look: "handDrawn" }) },
  {
    id: "all-priorities",
    source: stableSource([
      "kanban", "  todo[Todo]",
      "    a[A]@{ priority: 'Very High' }",
      "    b[B]@{ priority: 'High' }",
      "    c[C]@{ priority: 'Medium' }",
      "    d[D]@{ priority: 'Low' }",
      "    e[E]@{ priority: 'Very Low' }",
      "    f[F]@{ priority: 'Other' }",
    ].join("\n")),
  },
  {
    id: "ticket-link",
    source: stableSource(META, {
      kanban: { ticketBaseUrl: "https://example.test/#TICKET#" },
    }),
  },
  { id: "frontmatter-title-inert", source: frontmatterSource("Invisible Kanban title", BASE) },
];

const configCases = [
  { id: "baseline", source: stableSource(BASE) },
  ...geometryCases.filter((entry) => [
    "section-width-80", "section-width-negative", "section-width-invalid",
    "html-labels-false", "look-neo", "look-neo-drop-shadow-none",
    "look-neo-drop-shadow-custom", "look-neo-drop-shadow-invalid",
    "look-hand-drawn", "ticket-link", "unbroken-section-title",
  ].includes(entry.id)),
  { id: "section-width-zero", source: stableSource(BASE, { kanban: { sectionWidth: 0 } }) },
  { id: "section-width-string", source: stableSource(BASE, { kanban: { sectionWidth: "80" } }) },
  { id: "section-width-radix", source: stableSource(BASE, { kanban: { sectionWidth: "0x50" } }) },
  { id: "section-width-false", source: stableSource(BASE, { kanban: { sectionWidth: false } }) },
  { id: "section-width-array", source: stableSource(BASE, { kanban: { sectionWidth: [80] } }) },
  { id: "mindmap-padding-zero", source: stableSource(BASE, { mindmap: { padding: 0 } }) },
  { id: "mindmap-padding-20", source: stableSource(BASE, { mindmap: { padding: 20 } }) },
  { id: "mindmap-padding-string", source: stableSource(BASE, { mindmap: { padding: "20" } }) },
  { id: "mindmap-padding-radix", source: stableSource(BASE, { mindmap: { padding: "0x14" } }) },
  { id: "mindmap-padding-array", source: stableSource(BASE, { mindmap: { padding: [20] } }) },
  { id: "mindmap-padding-object", source: stableSource(BASE, { mindmap: { padding: {} } }) },
  { id: "mindmap-use-max-false", source: stableSource(BASE, { mindmap: { useMaxWidth: false } }) },
  { id: "mindmap-use-max-string-false", source: stableSource(BASE, { mindmap: { useMaxWidth: "false" } }) },
  { id: "kanban-padding-inert", source: stableSource(BASE, { kanban: { padding: 30 } }) },
  { id: "kanban-use-max-inert", source: stableSource(BASE, { kanban: { useMaxWidth: false } }) },
  { id: "mindmap-max-node-width-inert", source: stableSource(BASE, { mindmap: { maxNodeWidth: 80 } }) },
  { id: "flowchart-html-labels-inert", source: stableSource(BASE, { flowchart: { htmlLabels: false } }) },
  { id: "markdown-auto-wrap-false", source: stableSource(BASE, { markdownAutoWrap: false }) },
  {
    id: "theme-paint-overrides",
    source: stableSource(META, { themeVariables: {
      textColor: "#123456", background: "#abcdef", nodeBorder: "#fedcba",
      cScale2: "#112233", cScaleLabel2: "#00ff00",
    } }),
  },
  { id: "theme-font-size-20", source: stableSource(META, { themeVariables: { fontSize: "20px" } }) },
  { id: "theme-font-size-zero", source: stableSource(META, { themeVariables: { fontSize: "0px" } }) },
  { id: "theme-font-family-fallback", source: sourceInit({
      fontFamily: "DefinitelyMissing, Noto Sans",
      themeVariables: { fontFamily: "DefinitelyMissing, Noto Sans" },
    }, META) },
  { id: "tcl-zero", source: stableSource(BASE, { themeVariables: { THEME_COLOR_LIMIT: 0 } }) },
  { id: "tcl-two", source: stableSource(BASE, { themeVariables: { THEME_COLOR_LIMIT: 2 } }) },
  { id: "tcl-fraction", source: stableSource(BASE, { themeVariables: { THEME_COLOR_LIMIT: 2.5 } }) },
  { id: "dark-mode-false", source: stableSource(BASE, { themeVariables: { darkMode: false, cScale2: "#204060" } }) },
  { id: "dark-mode-true", source: stableSource(BASE, { themeVariables: { darkMode: true, cScale2: "#204060" } }) },
  { id: "dark-mode-string-false", source: stableSource(BASE, { themeVariables: { darkMode: "false", cScale2: "#204060" } }) },
  { id: "frontmatter-title-inert", source: frontmatterSource("Invisible Kanban title", BASE) },
  { id: "tcl-dark-13", source: stableSource(BASE, { theme: "dark", themeVariables: { THEME_COLOR_LIMIT: 13 } }) },
  { id: "tcl-redux-13", source: stableSource(BASE, { theme: "redux", themeVariables: { THEME_COLOR_LIMIT: 13 } }) },
  { id: "tcl-redux-14", source: stableSource(BASE, { theme: "redux", themeVariables: { THEME_COLOR_LIMIT: 14 } }) },
  ...["default", "dark", "forest", "neutral", "base", "neo", "neo-dark",
      "redux", "redux-dark", "redux-color", "redux-dark-color"].map((theme) => ({
    id: `theme-${theme}`,
    source: stableSource(META, { theme }),
  })),
  { id: "empty-error", source: stableSource("kanban") },
];

const pixelCases = [
  { id: "default", source: stableSource(BASE) },
  { id: "dark", source: stableSource(META, { theme: "dark" }) },
  { id: "neo", source: stableSource(BASE, { theme: "neo", look: "neo" }) },
  { id: "redux-color", source: stableSource(META, { theme: "redux-color" }) },
  { id: "hand-drawn", source: stableSource(BASE, { look: "handDrawn", handDrawnSeed: 7 }) },
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
    colorType: 6, inputColorType: 6, bitDepth: 8,
  });
};

fs.mkdirSync(fixtureDir, { recursive: true });
fs.mkdirSync(pixelDir, { recursive: true });
const browser = await puppeteer.launch({
  headless: "new", executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--disable-lcd-text",
         "--font-render-hinting=none", "--force-color-profile=srgb",
         "--hide-scrollbars", "--lang=en-US"],
});

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const upstream = {
    package: "mermaid", version: EXPECTED_MERMAID_VERSION,
    module: "dist/mermaid.esm.mjs", moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
    browser: { product: EXPECTED_CHROME_PRODUCT,
               executableSha256: EXPECTED_CHROME_SHA256, headlessMode: "new" },
    viewport: VIEWPORT,
    fonts: [{ family: FONT_FAMILY,
              file: "third_party/noto/fonts/NotoSans-Regular.ttf",
              sha256: EXPECTED_NOTO_SHA256 }],
    generator: "scripts/generate_mermaid_kanban_fixtures.mjs",
  };

  const preparePage = async () => {
    const page = await browser.newPage();
    await page.setViewport(VIEWPORT);
    await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
    await page.evaluate(async ({ family, url }) => {
      document.documentElement.setAttribute("lang", "en");
      document.body.style.margin = "0";
      document.body.innerHTML = '<div id="container"></div>';
      const style = document.createElement("style");
      style.textContent = `@font-face{font-family:"${family}";src:url("${url}");font-style:normal;font-weight:400}html,body{margin:0;padding:0}`;
      document.head.appendChild(style);
      await document.fonts.load(`16px "${family}"`, "Kanban 0123456789");
      await document.fonts.ready;
    }, { family: FONT_FAMILY, url: fontUrl });
    return page;
  };

  const captureCases = async (cases) => {
    const page = await preparePage();
    const output = await page.evaluate(async ({ browserCases, family, moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      const round = (value) => Number.isFinite(value) ? Math.round(value * 1e6) / 1e6 : null;
      const box = (element) => {
        const value = element.getBBox();
        return { x: round(value.x), y: round(value.y),
                 width: round(value.width), height: round(value.height) };
      };
      const client = (element) => {
        const value = element.getBoundingClientRect();
        return { x: round(value.x), y: round(value.y),
                 width: round(value.width), height: round(value.height) };
      };
      const attrs = (element, names) => Object.fromEntries(names
        .map((name) => [name, element.getAttribute(name)])
        .filter(([, value]) => value !== null));
      const computed = (element) => {
        const value = getComputedStyle(element);
        return {
          fill: value.fill, stroke: value.stroke, strokeWidth: value.strokeWidth,
          color: value.color, fontFamily: value.fontFamily,
          fontSize: value.fontSize, fontWeight: value.fontWeight,
          fontStyle: value.fontStyle, lineHeight: value.lineHeight,
          filter: value.filter, textAnchor: value.textAnchor,
          dominantBaseline: value.dominantBaseline,
        };
      };
      const label = (element) => {
        if (!element) return null;
        const foreign = element.querySelector("foreignObject");
        return {
          text: element.textContent ?? "",
          attrs: attrs(element, ["class", "transform", "style"]),
          bbox: box(element), computed: computed(element),
          foreignObject: foreign ? {
            attrs: attrs(foreign, ["width", "height"]), bbox: box(foreign),
            html: foreign.innerHTML,
          } : null,
          tspans: [...element.querySelectorAll("tspan")].map((span) => ({
            text: span.textContent ?? "",
            attrs: attrs(span, ["class", "x", "y", "dy", "font-style", "font-weight"]),
            bbox: box(span), computed: computed(span),
          })),
        };
      };
      const snapshot = (root) => ({
        root: {
          attrs: attrs(root, ["width", "height", "viewBox", "style", "role", "aria-roledescription"]),
          bbox: box(root), clientBox: client(root), computed: computed(root),
        },
        directOrder: [...root.children].map((element) => ({
          tag: element.tagName.toLowerCase(), class: element.getAttribute("class") ?? "",
        })),
        filtered: [...root.querySelectorAll("*")]
          .map((element) => ({
            tag: element.tagName.toLowerCase(),
            class: element.getAttribute("class") ?? "",
            filter: getComputedStyle(element).filter,
          }))
          .filter((entry) => entry.filter !== "none"),
        sections: [...root.querySelectorAll(":scope > g.sections > g.cluster")].map((group) => {
          const shape = group.querySelector(":scope > rect, :scope > path");
          return {
            attrs: attrs(group, ["class", "id", "data-look", "transform"]),
            bbox: box(group), computed: computed(group),
            shape: shape ? {
              tag: shape.tagName.toLowerCase(),
              attrs: attrs(shape, ["class", "x", "y", "width", "height", "rx", "ry", "d", "style", "fill", "stroke", "stroke-width"]),
              bbox: box(shape), computed: computed(shape),
            } : null,
            paths: [...group.querySelectorAll(":scope > path")].map((path) => ({
              attrs: attrs(path, ["d", "fill", "stroke", "stroke-width", "fill-opacity", "stroke-opacity"]),
              bbox: box(path), computed: computed(path),
            })),
            label: label(group.querySelector(":scope > g.cluster-label")),
          };
        }),
        items: [...root.querySelectorAll(":scope > g.items > g.node")].map((group) => {
          const shape = group.querySelector(":scope > rect.basic, :scope > path");
          const labels = [...group.querySelectorAll(":scope > g.label")];
          const priority = group.querySelector(":scope > line");
          const link = group.querySelector("a.kanban-ticket-link");
          return {
            attrs: attrs(group, ["class", "id", "data-look", "transform"]),
            bbox: box(group), computed: computed(group),
            shape: shape ? {
              tag: shape.tagName.toLowerCase(),
              attrs: attrs(shape, ["class", "x", "y", "width", "height", "rx", "ry", "d", "style", "fill", "stroke", "stroke-width"]),
              bbox: box(shape), computed: computed(shape),
            } : null,
            labels: labels.map(label),
            priority: priority ? {
              attrs: attrs(priority, ["x1", "x2", "y1", "y2", "stroke", "stroke-width"]),
              bbox: box(priority), computed: computed(priority),
            } : null,
            link: link ? attrs(link, ["class", "href", "xlink:href", "target"]) : null,
          };
        }),
      });

      const output = [];
      for (let index = 0; index < browserCases.length; ++index) {
        const fixture = browserCases[index];
        try {
          mermaid.initialize({ startOnLoad: false, securityLevel: "strict",
                               theme: "default", fontFamily: family,
                               themeVariables: { fontFamily: family } });
          const { svg } = await mermaid.render(`kanban-oracle-${index}`, fixture.source);
          document.getElementById("container").innerHTML = svg;
          await document.fonts.ready;
          await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
          output.push({ id: fixture.id, source: fixture.source, status: "ready",
                        expected: snapshot(document.querySelector("#container > svg")) });
        } catch (error) {
          output.push({ id: fixture.id, source: fixture.source, status: "error",
                        error: String(error?.message ?? error) });
        }
      }
      return output;
    }, { browserCases: cases, family: FONT_FAMILY, moduleUrl: mermaidModule });
    await page.close();
    return output;
  };

  const geometry = await captureCases(geometryCases);
  assertEqual(geometry.length, 22, "Kanban geometry case count");
  assertEqual(geometry.filter((entry) => entry.status === "ready").length, 22,
              "Kanban ready geometry count");
  const geometryById = new Map(geometry.map((entry) => [entry.id, entry.expected]));
  assertEqual(geometryById.get("canonical").root.attrs.viewBox, "90 -310 425 196",
              "canonical viewBox");
  assertEqual(geometryById.get("duplicate-section-id").items.length, 8,
              "duplicate section child replay");
  assertEqual(geometryById.get("all-priorities").items.length, 6,
              "priority item count");
  writeJson(path.join(fixtureDir, "kanban-geometry.json"), {
    upstream,
    oracle: "Kanban section/item geometry, HTML/SVG label measurement, DOM order, metadata and paint",
    notes: [
      "Renderer reads kanban.sectionWidth but mindmap.padding/useMaxWidth; the other Kanban layout keys are upstream-inert.",
      "Default labels are HTML foreignObjects despite the renderer assigning htmlLabels=false to a detached config copy.",
      "Repeated section ids replay all children with that parent id in every repeated section column.",
      "handDrawn applies only to sections and retains their initial 3*sectionWidth height because the final rect lookup finds no SVG rect.",
    ],
    cases: geometry,
  });

  const config = await captureCases(configCases);
  assertEqual(config.length, 56, "Kanban config case count");
  assertEqual(config.filter((entry) => entry.status === "error").length, 1,
              "Kanban config error count");
  writeJson(path.join(fixtureDir, "kanban-config.json"), {
    upstream,
    oracle: "Kanban source-entry config coercion, theme/TCL cascade and cross-family config reads",
    notes: [
      "sectionWidth uses JavaScript || 200; mindmap padding uses ?? and useMaxWidth is consumed by raw truthiness.",
      "TCL zero removes the repeated generic node CSS because that CSS is emitted inside genSections' loop.",
      "Section DOM classes are one-based while generated color rules use i-1; section 1 therefore consumes cScale2.",
      "Only top-level htmlLabels:false selects SVG text; flowchart.htmlLabels:false is shadowed by the defined global default.",
    ],
    cases: config,
  });

  const pixelManifest = [];
  for (const fixture of pixelCases) {
    const page = await preparePage();
    await page.evaluate(async ({ family, moduleUrl, renderId, source }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict",
                           theme: "default", fontFamily: family,
                           themeVariables: { fontFamily: family } });
      const { svg } = await mermaid.render(renderId, source);
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    }, { family: FONT_FAMILY, moduleUrl: mermaidModule,
         renderId: `kanban-pixel-${fixture.id}`, source: fixture.source });
    const root = await page.$("#container > svg");
    const bytes = canonicalPng(await root.screenshot({ omitBackground: true }));
    const image = PNG.sync.read(bytes);
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), bytes);
    pixelManifest.push({ id: fixture.id, source: fixture.source, file,
                         width: image.width, height: image.height,
                         sha256: sha256(bytes) });
    await page.close();
  }
  assertEqual(new Set(pixelManifest.map((entry) => entry.sha256)).size,
              pixelCases.length, "distinct Kanban pixel fixtures");
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream,
    oracle: "Kanban default/dark/neo/redux-color/handDrawn browser raster goldens",
    capture: { background: "transparent", deviceScaleFactor: 1,
               pngCanonicalization: "transparent RGB zeroed; PNG RGBA8 re-encoded" },
    cases: pixelManifest,
  });

  console.log(`geometry cases: ${geometry.length}`);
  console.log(`config cases: ${config.length}`);
  for (const entry of pixelManifest)
    console.log(`pixel ${entry.id}: ${entry.width}x${entry.height} ${entry.sha256}`);
} finally {
  await browser.close();
}
