import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Reproducible Mermaid 11.16.0 Gantt renderer/config/pixel oracle.

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA256 =
  "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";
const VIEWPORT = { width: 1600, height: 1000, deviceScaleFactor: 1 };

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
const ganttModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.core",
  "ganttDiagram-NO4QXBWP.mjs",
);
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const pixelDir = path.join(fixtureDir, "gantt-pixel");
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const readJson = (file) => JSON.parse(fs.readFileSync(file, "utf8"));
const assertEqual = (actual, expected, label) => {
  if (actual !== expected) throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const writeJson = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = readJson(path.join(mermaidRoot, "package.json"));
assertEqual(pkg.version, EXPECTED_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MODULE_SHA256, "Mermaid module");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans");

const mermaidModule = pathToFileURL(moduleFile).href;
const fontUrl = pathToFileURL(fontFile).href;
const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js"),
  ).href,
);

const canonical = [
  "gantt",
  "title Release plan",
  "dateFormat YYYY-MM-DD",
  "axisFormat %m/%d",
  "tickInterval 2day",
  "todayMarker off",
  "excludes weekends",
  "section Planning",
  "Research :done, crit, research, 2024-01-01, 3d",
  "Design with a long label :active, design, after research, 4d",
  "section Delivery<br>Track",
  "Milestone :milestone, mark, 2024-01-10, 0d",
  "Ship :ship, 2024-01-11, 2d",
  "Vertical marker :vert, vertical, 2024-01-07, 1d",
].join("\n");
const overlaps = [
  "gantt",
  "dateFormat YYYY-MM-DD",
  "todayMarker off",
  "section One",
  "A :a, 2024-01-01, 5d",
  "B :b, 2024-01-02, 2d",
  "C :c, 2024-01-05, 3d",
  "section Two",
  "D :d, 2024-01-01, 2d",
  "E :e, 2024-01-04, 2d",
].join("\n");
const tags = [
  "gantt",
  "dateFormat YYYY-MM-DD",
  "todayMarker off",
  "section Status",
  "Normal :normal, 2024-01-01, 1d",
  "Active :active, active, 2024-01-02, 1d",
  "Done :done, done, 2024-01-03, 1d",
  "Crit :crit, crit, 2024-01-04, 1d",
  "Active crit :active, crit, ac, 2024-01-05, 1d",
  "Done crit :done, crit, dc, 2024-01-06, 1d",
  "Milestone :milestone, milestone, 2024-01-07, 0d",
].join("\n");

const stableSource = (body, config = {}) => {
  const merged = {
    ...config,
    fontFamily: config.fontFamily ?? "Noto Sans",
    gantt: { useWidth: 900, ...(config.gantt ?? {}) },
    themeVariables: {
      fontFamily: "Noto Sans",
      ...(config.themeVariables ?? {}),
    },
  };
  return `%%{init: ${JSON.stringify(merged)}}%%\n${body}`;
};

const geometryCases = [
  { id: "canonical", source: stableSource(canonical) },
  { id: "compact", source: stableSource(overlaps, { gantt: { displayMode: "compact" } }) },
  { id: "top-axis-config", source: stableSource(canonical, { gantt: { topAxis: true } }) },
  { id: "status-tags", source: stableSource(tags) },
  {
    id: "excludes-includes",
    source: stableSource([
      "gantt", "dateFormat YYYY-MM-DD", "todayMarker off",
      "excludes weekends, 2024-01-08", "includes 2024-01-06, 2024-01-08",
      "section Work", "Task :a, 2024-01-05, 4d",
    ].join("\n")),
  },
  {
    id: "label-outside-right",
    source: stableSource([
      "gantt", "dateFormat YYYY-MM-DD", "todayMarker off",
      "A label far wider than its task bar :a, 2024-01-01, 1d",
      "Domain :b, 2024-01-01, 20d",
    ].join("\n")),
  },
  {
    id: "label-outside-left",
    source: stableSource([
      "gantt", "dateFormat YYYY-MM-DD", "todayMarker off",
      "Domain :a, 2024-01-01, 20d",
      "A label far wider than the ending bar :b, 2024-01-20, 1d",
    ].join("\n")),
  },
  { id: "no-title", source: stableSource(canonical.replace("title Release plan\n", "")) },
  {
    id: "frontmatter-title",
    source: `---\ntitle: Front title\n---\n${stableSource(canonical.replace("title Release plan\n", ""))}`,
  },
  {
    id: "empty",
    source: stableSource("gantt\ntodayMarker off"),
  },
  {
    id: "empty-section",
    source: stableSource("gantt\ntodayMarker off\nsection Empty"),
  },
  {
    id: "custom-size-fixed",
    source: stableSource(canonical, {
      gantt: {
        useWidth: 640, useMaxWidth: false, barHeight: 28, barGap: 9,
        topPadding: 65, leftPadding: 30, rightPadding: 20,
        gridLineStartPadding: 14, titleTopMargin: 7,
      },
    }),
  },
  {
    id: "section-line-break",
    source: stableSource("gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\nsection A<br>B<br>C\nOne :a, 2024-01-01, 2d"),
  },
  {
    id: "tick-week-monday",
    source: stableSource("gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\nweekday monday\ntickInterval 1week\nOne :a, 2024-01-01, 2M"),
  },
  {
    id: "tick-month",
    source: stableSource("gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\ntickInterval 1month\nOne :a, 2024-01-01, 1y"),
  },
  {
    id: "subday",
    source: stableSource("gantt\ndateFormat YYYY-MM-DD HH:mm\naxisFormat %H:%M\ntodayMarker off\ntickInterval 6hour\nOne :a, 2024-01-01 00:00, 18h"),
  },
  {
    id: "equal-domain",
    source: stableSource("gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\nOne :a, 2024-01-01, 0d"),
  },
  {
    id: "safe-link",
    source: stableSource("gantt\ndateFormat YYYY-MM-DD\ntodayMarker off\nOne :a, 2024-01-01, 2d\nclick a href \"https://example.com\""),
  },
];

const configCases = [
  { id: "default", body: canonical },
  ...[
    ["useWidth", { useWidth: 640 }],
    ["useMaxWidth", { useMaxWidth: false }],
    ["titleTopMargin", { titleTopMargin: 3 }],
    ["barHeight", { barHeight: 31 }],
    ["barGap", { barGap: 13 }],
    ["topPadding", { topPadding: 70 }],
    ["rightPadding", { rightPadding: 5 }],
    ["leftPadding", { leftPadding: 5 }],
    ["gridLineStartPadding", { gridLineStartPadding: 5 }],
    ["fontSize", { fontSize: 19 }],
    ["sectionFontSize", { sectionFontSize: 21 }],
    ["numberSectionStyles", { numberSectionStyles: 2 }],
    ["axisFormat", { axisFormat: "%Y" }],
    ["tickInterval", { tickInterval: "1week" }],
    ["topAxis", { topAxis: true }],
    ["displayMode", { displayMode: "compact" }],
    ["weekday", { weekday: "monday" }],
  ].map(([id, gantt]) => ({ id, body: canonical, config: { gantt } })),
  { id: "barHeight-string", body: canonical, config: { gantt: { barHeight: "31" } } },
  { id: "barGap-zero", body: canonical, config: { gantt: { barGap: 0 } } },
  { id: "useMaxWidth-string-false", body: canonical, config: { gantt: { useMaxWidth: "false" } } },
  { id: "fontFamily", body: canonical, config: { themeVariables: { fontFamily: "monospace" } } },
  ...[
    "sectionBkgColor", "sectionBkgColor2", "altSectionBkgColor", "excludeBkgColor",
    "taskBkgColor", "taskBorderColor", "taskTextColor", "taskTextDarkColor",
    "taskTextOutsideColor", "taskTextClickableColor", "activeTaskBkgColor",
    "activeTaskBorderColor", "doneTaskBkgColor", "doneTaskBorderColor",
    "critBkgColor", "critBorderColor", "gridColor", "todayLineColor", "titleColor",
  ].map((key, index) => ({
    id: `theme-${key}`,
    body: tags,
    config: { themeVariables: { [key]: index % 2 ? "#12ab34" : "#b321ef" } },
  })),
  { id: "theme-default", body: canonical, config: { theme: "default" } },
  { id: "theme-dark", body: canonical, config: { theme: "dark" } },
  { id: "theme-forest", body: canonical, config: { theme: "forest" } },
  { id: "theme-neutral", body: canonical, config: { theme: "neutral" } },
  { id: "theme-redux-color", body: canonical, config: { theme: "redux-color" } },
];

const pixelCases = [
  { id: "default", source: stableSource(canonical) },
  { id: "dark", source: stableSource(canonical, { theme: "dark" }) },
  { id: "forest", source: stableSource(canonical, { theme: "forest" }) },
  { id: "compact", source: stableSource(overlaps, { theme: "redux-color", gantt: { displayMode: "compact" } }) },
];

const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"],
});
assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
const hostPage = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;

async function renderCase(page, source, id) {
  await page.setViewport(VIEWPORT);
  await page.goto(hostPage);
  return page.evaluate(async ({ source, id, moduleUrl, fontUrl }) => {
    const font = new FontFace("Noto Sans", `url(${fontUrl})`);
    await font.load();
    document.fonts.add(font);
    await document.fonts.load("16px 'Noto Sans'");
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({
      startOnLoad: false,
      securityLevel: "strict",
      deterministicIds: true,
      deterministicIDSeed: "gantt-fixture",
    });
    const rendered = await mermaid.render(`gantt-${id}`, source);
    document.body.style.margin = "0";
    document.body.innerHTML = rendered.svg;
    const root = document.querySelector("svg");
    const attrs = (element) => Object.fromEntries([...element.attributes].map((a) => [a.name, a.value]));
    const box = (element) => {
      try {
        const b = element.getBBox();
        return { x: b.x, y: b.y, width: b.width, height: b.height };
      } catch {
        return null;
      }
    };
    const elementSnapshot = (element, order) => {
      const style = getComputedStyle(element);
      return {
        order,
        tag: element.tagName.toLowerCase(),
        text: element.textContent,
        attrs: attrs(element),
        bbox: box(element),
        computed: {
          fill: style.fill,
          stroke: style.stroke,
          strokeWidth: style.strokeWidth,
          opacity: style.opacity,
          fontFamily: style.fontFamily,
          fontSize: style.fontSize,
          fontWeight: style.fontWeight,
          fontStyle: style.fontStyle,
          textAnchor: style.textAnchor,
        },
      };
    };
    const client = root.getBoundingClientRect();
    const config = mermaid.mermaidAPI.getConfig();
    return {
      root: {
        attrs: attrs(root),
        bbox: box(root),
        client: { x: client.x, y: client.y, width: client.width, height: client.height },
        computed: {
          fontFamily: getComputedStyle(root).fontFamily,
          fontSize: getComputedStyle(root).fontSize,
        },
      },
      elements: [...root.querySelectorAll("g,rect,line,path,text,tspan,a")].map(elementSnapshot),
      ganttConfig: config.gantt,
      themeVariables: Object.fromEntries([
        "sectionBkgColor", "sectionBkgColor2", "altSectionBkgColor", "excludeBkgColor",
        "taskBkgColor", "taskBorderColor", "taskTextColor", "taskTextDarkColor",
        "taskTextOutsideColor", "taskTextClickableColor", "activeTaskBkgColor",
        "activeTaskBorderColor", "doneTaskBkgColor", "doneTaskBorderColor",
        "critBkgColor", "critBorderColor", "gridColor", "todayLineColor", "titleColor",
        "fontFamily", "textColor",
      ].map((key) => [key, config.themeVariables[key]])),
    };
  }, { source, id, moduleUrl: mermaidModule, fontUrl });
}

const geometry = [];
for (const fixture of geometryCases) {
  const page = await browser.newPage();
  geometry.push({ ...fixture, expected: await renderCase(page, fixture.source, fixture.id) });
  await page.close();
}

const config = [];
for (const fixture of configCases) {
  const page = await browser.newPage();
  const source = stableSource(fixture.body, fixture.config ?? {});
  config.push({ ...fixture, source, expected: await renderCase(page, source, `config-${fixture.id}`) });
  await page.close();
}

fs.mkdirSync(pixelDir, { recursive: true });
const pixelManifest = [];
for (const fixture of pixelCases) {
  const page = await browser.newPage();
  const snapshot = await renderCase(page, fixture.source, `pixel-${fixture.id}`);
  const width = Math.max(1, Math.round(snapshot.root.client.width));
  const height = Math.max(1, Math.round(snapshot.root.client.height));
  const bytes = await page.screenshot({
    clip: { x: 0, y: 0, width, height },
    omitBackground: true,
  });
  const decoded = PNG.sync.read(bytes);
  assertEqual(decoded.width, width, `${fixture.id} PNG width`);
  assertEqual(decoded.height, height, `${fixture.id} PNG height`);
  const filename = `${fixture.id}.png`;
  fs.writeFileSync(path.join(pixelDir, filename), bytes);
  pixelManifest.push({
    id: fixture.id,
    source: fixture.source,
    file: filename,
    width,
    height,
    sha256: sha256(bytes),
  });
  await page.close();
}
await browser.close();

const provenance = {
  package: "mermaid",
  version: pkg.version,
  license: pkg.license,
  moduleSha256: sha256(fs.readFileSync(moduleFile)),
  ganttModuleSha256: sha256(fs.readFileSync(ganttModuleFile)),
  chromeProduct: EXPECTED_CHROME_PRODUCT,
  chromeSha256: sha256(fs.readFileSync(chrome)),
  font: "Noto Sans",
  fontSha256: sha256(fs.readFileSync(fontFile)),
  viewport: VIEWPORT,
};
writeJson(path.join(fixtureDir, "gantt-geometry.json"), { upstream: provenance, cases: geometry });
writeJson(path.join(fixtureDir, "gantt-config.json"), { upstream: provenance, cases: config });
writeJson(path.join(pixelDir, "manifest.json"), { upstream: provenance, cases: pixelManifest });
console.log(`Wrote ${geometry.length} geometry, ${config.length} config, ${pixelManifest.length} pixel cases`);
