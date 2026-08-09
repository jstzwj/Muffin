import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ??
    path.join("tests", "fixtures", "mermaid", "xychart-theme.json"),
);
const packageJson = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}

const puppeteerPath = path.join(
  path.dirname(mermaidRoot),
  "puppeteer",
  "lib",
  "puppeteer",
  "puppeteer.js",
);
const { default: puppeteer } = await import(pathToFileURL(puppeteerPath));
const themes = [
  "default",
  "dark",
  "forest",
  "neutral",
  "neo",
  "neo-dark",
  "base",
  "redux",
  "redux-dark",
  "redux-color",
  "redux-dark-color",
];
const fields = [
  "backgroundColor",
  "titleColor",
  "dataLabelColor",
  "xAxisTitleColor",
  "xAxisLabelColor",
  "xAxisTickColor",
  "xAxisLineColor",
  "yAxisTitleColor",
  "yAxisLabelColor",
  "yAxisTickColor",
  "yAxisLineColor",
  "plotColorPalette",
];
const baseSource = [
  "xychart-beta",
  'title "Theme probe"',
  'x-axis "X" [a, b, c]',
  'y-axis "Y" 0 --> 10',
  ...Array.from({ length: 10 }, (_, index) =>
    `bar [${index + 1}, ${index + 2}, ${index + 3}]`),
].join("\n");

const browser = await puppeteer.launch({
  headless: true,
  executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe",
  args: ["--allow-file-access-from-files"],
});

try {
  const moduleUrl = pathToFileURL(
    path.join(mermaidRoot, "dist", "mermaid.esm.mjs"),
  ).href;
  const harnessUrl =
    pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const withFreshPage = async (input, callback) => {
    const page = await browser.newPage();
    try {
      await page.goto(harnessUrl);
      return await page.evaluate(callback, input);
    } finally {
      await page.close();
    }
  };
  const captureTheme = (theme) =>
    withFreshPage(
      { moduleUrl, theme, fields, baseSource },
      async ({ moduleUrl, theme, fields, baseSource }) => {
        const { default: mermaid } = await import(moduleUrl);
        const normalize = (value) =>
          Object.fromEntries(fields.map((field) => [field, value[field] ?? null]));
        mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme });
        const diagram = await mermaid.mermaidAPI.getDiagramFromText(baseSource);
        return normalize(diagram.db.getChartThemeConfig());
      },
    );
  const captureRendered = (source, nestedVariables) =>
    withFreshPage(
      { moduleUrl, source, nestedVariables },
      async ({ moduleUrl, source, nestedVariables }) => {
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "loose",
          theme: "default",
          xyChart: { showDataLabel: true },
          ...(nestedVariables
            ? { themeVariables: { xyChart: nestedVariables } }
            : {}),
        });
        const { svg } = await mermaid.render("xytheme-probe", source);
        const root = new DOMParser().parseFromString(svg, "image/svg+xml");
        const attr = (selector, name) =>
          root.querySelector(selector)?.getAttribute(name) ?? null;
        return {
          backgroundColor: attr("rect.background", "fill"),
          titleColor: attr(".chart-title text", "fill"),
          dataLabelColor: attr(".bar-plot-0 text", "fill"),
          xAxisTitleColor: attr(".bottom-axis .title text", "fill"),
          xAxisLabelColor: attr(".bottom-axis .label text", "fill"),
          xAxisTickColor: attr(".bottom-axis .ticks path", "stroke"),
          xAxisLineColor: attr(".bottom-axis .axis-line path", "stroke"),
          yAxisTitleColor: attr(".left-axis .title text", "fill"),
          yAxisLabelColor: attr(".left-axis .label text", "fill"),
          yAxisTickColor: attr(".left-axis .ticks path", "stroke"),
          yAxisLineColor: attr(".left-axis .axisl-line path", "stroke"),
          plotColorPalette: Array.from({ length: 10 }, (_, index) =>
            attr(`.bar-plot-${index} rect`, "fill")).join(","),
        };
      },
    );

  const byTheme = {};
  for (const theme of themes) byTheme[theme] = await captureTheme(theme);
  const sourcePrefix =
    '%%{init: {"themeVariables":{"xyChart":{"titleColor":"#123456"}}}}%%\n';
  const frontmatterPrefix = [
    "---", "config:", "  themeVariables:", "    xyChart:",
    '      titleColor: "#123456"', "---", "",
  ].join("\n");
  const dependencyPrefix =
    '%%{init: {"themeVariables":{"primaryTextColor":"#654321"}}}%%\n';
  const emptyPrefix =
    '%%{init: {"themeVariables":{"xyChart":{"titleColor":""}}}}%%\n';
  const overrides = {
    initializeSparse: await captureRendered(baseSource, { titleColor: "#123456" }),
    sourceSparse: await captureRendered(sourcePrefix + baseSource),
    frontmatterSparse: await captureRendered(frontmatterPrefix + baseSource),
    dependency: await captureRendered(dependencyPrefix + baseSource),
    sourceEmpty: await captureRendered(emptyPrefix + baseSource),
  };
  const first = { byTheme, overrides };
  for (const theme of themes) {
    const value = first.byTheme[theme];
    if (!value || Object.keys(value).length !== fields.length) {
      throw new Error(`${theme}: incomplete XYChart theme object`);
    }
  }
  if (first.overrides.initializeSparse.titleColor !== "#123456")
    throw new Error("Nested XYChart initialize() override did not apply");

  const payload = {
    upstream: { package: packageJson.name, version: packageJson.version },
    fields,
    themes: first.byTheme,
    overrides: first.overrides,
  };
  const canonical = JSON.stringify(payload);
  payload.fixtureSha256 = createHash("sha256").update(canonical).digest("hex");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${themes.length} XYChart themes to ${output}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
