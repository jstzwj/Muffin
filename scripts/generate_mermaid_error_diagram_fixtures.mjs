import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Error-diagram oracle (11.16.0). The "error" diagram type is registered
// FIRST by addDiagrams(): mermaid.core's render() routes Diagram.fromText()
// failures to `Diagram.fromText("error")`, leaving the lightbulb SVG in the
// DOM while rethrowing. This generator freezes:
//   - the literal "error" diagram's DOM contract (svg attrs, six .error-icon
//     path d + bboxes, two .error-text positions/sizes/computed styles);
//   - the effective errorBkgColor/errorTextColor chain for all 11 themes;
//   - the failure semantics: which failure classes leave the fallback svg
//     (parse/detector) and which do not (frontmatter YAML throws before the
//     try/catch; suppressErrorRendering:true removes the temp elements);
//   - a themeCSS case proving the user sheet competes with the base rules;
//   - a default-theme PNG at the max-width client box for the pixel test.
//
// Repo font convention: every render pins the bundled Noto Sans face via an
// init directive (the native renderer resolves unpinned sources to the
// deterministic Noto stack), so text geometry is font-deterministic.
//
// Determinism: no dates, no random tokens (the error renderer emits none);
// run twice and diff to confirm byte identity.

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "error-diagram.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

const common = { fontFamily: "Noto Sans", themeVariables: { fontFamily: "Noto Sans", fontSize: "16px" } };
const pinned = (source, extra = {}) =>
  `%%{init: ${JSON.stringify({ ...common, ...extra })}}%%\n${source}`;

const browser = await puppeteer.launch({
  executablePath: chrome,
  headless: true,
  args: ["--no-sandbox", "--allow-file-access-from-files", "--disable-gpu",
         "--force-device-scale-factor=1"],
});
const page = await browser.newPage();
await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
const moduleUrl = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const fontUrl = pathToFileURL(fontFile).href;

const out = await page.evaluate(async ({ moduleUrl, fontUrl, common }) => {
  const pinned = (source, extra = {}) =>
    `%%{init: ${JSON.stringify({ ...common, ...extra })}}%%\n${source}`;
  document.body.style.margin = "0";
  const font = document.createElement("style");
  font.textContent = `@font-face{font-family:"Noto Sans";src:url("${fontUrl}");font-weight:400;font-style:normal}`;
  document.head.appendChild(font);
  await document.fonts.load('16px "Noto Sans"', "Syntax error in text mermaid version 0123456789.");
  await document.fonts.ready;
  const mermaid = (await import(moduleUrl)).default;
  const container = () => {
    document.body.innerHTML = '<div id="container"></div>';
    return document.querySelector("#container");
  };
  const analyzeSvg = (svg) => ({
    attributes: Object.fromEntries(
      [...svg.attributes].filter((a) => a.name !== "id").map((a) => [a.name, a.value])),
    childSummary: [...svg.children].map((c) =>
      c.tagName + (c.getAttribute("class") ? "." + c.getAttribute("class") : "")),
    clientRect: (() => {
      const r = svg.getBoundingClientRect();
      return { width: r.width, height: r.height };
    })(),
    paths: [...svg.querySelectorAll("path.error-icon")].map((p) => {
      const b = p.getBBox();
      const s = getComputedStyle(p);
      return { d: p.getAttribute("d"), fill: s.fill, stroke: s.stroke,
               bbox: { x: b.x, y: b.y, width: b.width, height: b.height } };
    }),
    texts: [...svg.querySelectorAll("text.error-text")].map((t) => {
      const s = getComputedStyle(t);
      const b = t.getBBox();
      return { text: t.textContent,
               attributes: Object.fromEntries([...t.attributes].map((a) => [a.name, a.value])),
               computed: { fontFamily: s.fontFamily, fontSize: s.fontSize,
                           fontWeight: s.fontWeight, fill: s.fill, stroke: s.stroke,
                           textAnchor: s.textAnchor },
               textLength: t.getComputedTextLength(),
               bbox: { x: b.x, y: b.y, width: b.width, height: b.height } };
    }),
  });
  const result = { cases: {}, themes: {}, failures: {}, themeCss: null };

  mermaid.initialize({ startOnLoad: false, logLevel: "fatal" });
  {
    const rendered = await mermaid.render("error-fixture-literal", pinned("error"));
    result.cases["literal-error"] = { status: "ready",
                                      diagramType: rendered.diagramType };
    const host = container();
    host.innerHTML = rendered.svg;
    result.cases["literal-error"].analysis = analyzeSvg(host.querySelector("svg"));
  }
  for (const theme of ["default", "base", "dark", "forest", "neutral", "neo", "neo-dark",
                       "redux", "redux-dark", "redux-color", "redux-dark-color"]) {
    const rendered = await mermaid.render(
        "error-fixture-theme-" + theme, pinned("error", { theme }));
    const host = container();
    host.innerHTML = rendered.svg;
    const svg = host.querySelector("svg");
    const icon = getComputedStyle(svg.querySelector("path.error-icon"));
    const text = getComputedStyle(svg.querySelector("text.error-text"));
    result.themes[theme] = { iconFill: icon.fill, textFill: text.fill,
                             textStroke: text.stroke };
  }
  const failure = async (label, source) => {
    try {
      await mermaid.render("error-fixture-" + label, source);
      return { status: "ready" };
    } catch (e) {
      // The render id's temp div (`d<id>`) persists exactly when mermaid's
      // error path drew the fallback svg into it.
      const own = document.querySelector("#derror-fixture-" + label + " svg");
      return { status: "error", name: e.name, message: e.message,
               fallbackSvg: !!own,
               fallbackTexts: own
                 ? [...own.querySelectorAll("text.error-text")].map((t) => t.textContent)
                 : [] };
    }
  };
  result.failures["dash-only"] = await failure("dash-only", "---");
  result.failures["unclosed-frontmatter"] = await failure(
      "unclosed-frontmatter", "---\ntitle: x\nflowchart TB\nA --> B");
  const invalidFlowchart = "flowchart TB\nsubgraph S[Group]\nA --> B";
  result.failures["invalid-flowchart"] = await failure("invalid-flowchart", invalidFlowchart);
  result.failures["invalid-suppressed"] = await failure(
      "invalid-suppressed", invalidFlowchart,
  );
  result.failures["bad-yaml"] = await failure(
      "bad-yaml", "---\nconfig: [unclosed\n---\nflowchart TB\nA --> B");
  result.failures["unknown-diagram"] = await failure("unknown-diagram", "this is not a diagram");
  {
    const themeCss =
        ".error-icon { fill: rgb(255, 0, 0) !important; } " +
        ".error-text { font-size: 90px !important; }";
    const rendered = await mermaid.render(
        "error-fixture-theme-css", pinned("error", { themeCSS: themeCss }));
    const host = container();
    host.innerHTML = rendered.svg;
    const svg = host.querySelector("svg");
    const icon = getComputedStyle(svg.querySelector("path.error-icon"));
    const text = getComputedStyle(svg.querySelector("text.error-text"));
    result.themeCss = { themeCss, iconFill: icon.fill, textFontSize: text.fontSize,
                        analysis: analyzeSvg(svg) };
  }
  {
    // Structural + stroke-channel probe. `g:nth-of-type(2)` only matches the
    // icon paths if the content group is the SECOND g child of the svg root
    // (the empty scaffold is g#1; the <style> element is a different tag and
    // does not shift nth-of-type). The bare `path` tag rule proves icons
    // carry a stroke channel; the split fill/stroke on `.error-text` proves
    // the two lines paint BOTH channels. Icon computed styles are captured
    // PER PATH (all six) so a renderer that folds them into one shared style
    // cannot pass by sampling only the first.
    const themeCss =
        "g:nth-of-type(2) path { fill: rgb(0, 128, 128) !important; } " +
        "path { stroke: rgb(128, 0, 128); stroke-width: 2px; } " +
        ".error-text { fill: rgb(255, 0, 0) !important; " +
        "stroke: rgb(0, 0, 255) !important; }";
    const rendered = await mermaid.render(
        "error-fixture-theme-css-structure", pinned("error", { themeCSS: themeCss }));
    const host = container();
    host.innerHTML = rendered.svg;
    const svg = host.querySelector("svg");
    const text = getComputedStyle(svg.querySelector("text.error-text"));
    result.themeCssStructure = {
      themeCss,
      childSummary: [...svg.children].map((c) =>
        c.tagName + (c.getAttribute("class") ? "." + c.getAttribute("class") : "")),
      icons: [...svg.querySelectorAll("path.error-icon")].map((p) => {
        const s = getComputedStyle(p);
        return { fill: s.fill, stroke: s.stroke,
                 strokeWidth: parseFloat(s.strokeWidth) };
      }),
      textFill: text.fill, textStroke: text.stroke, textStrokeWidth: text.strokeWidth,
    };
  }
  {
    // Per-path differential probe. The six .error-icon paths are sibling
    // <path> children of the content group, so structural selectors style
    // them INDIVIDUALLY: :nth-of-type(2) reaches only the second path, the
    // adjacent-sibling combinator every path except the first, and display /
    // opacity / stroke-width each diverge on exactly one path. Only a
    // per-path CSS model can reproduce this pattern.
    const themeCss =
        ".error-icon:nth-of-type(2) { fill: rgb(0, 128, 128) !important; } " +
        ".error-icon + .error-icon { stroke: rgb(255, 0, 255); } " +
        ".error-icon:nth-of-type(3) { opacity: 0.5; } " +
        "path:nth-of-type(6) { stroke-width: 4px; } " +
        ".error-icon:nth-of-type(4) { display: none; }";
    const rendered = await mermaid.render(
        "error-fixture-theme-css-per-path", pinned("error", { themeCSS: themeCss }));
    const host = container();
    host.innerHTML = rendered.svg;
    const svg = host.querySelector("svg");
    result.themeCssPerPath = {
      themeCss,
      icons: [...svg.querySelectorAll("path.error-icon")].map((p) => {
        const s = getComputedStyle(p);
        return { fill: s.fill, stroke: s.stroke,
                 strokeWidth: parseFloat(s.strokeWidth),
                 opacity: parseFloat(s.opacity), display: s.display };
      }),
    };
  }
  return result;
}, { moduleUrl, fontUrl, common });

// The suppressed variant needs initialize()-level config (the secure-source
// sanitizer strips the key from directives/frontmatter), which is outside
// Muffin's Markdown API — capture it separately for the semantics record.
{
  const semantics = await page.evaluate(async ({ moduleUrl }) => {
    const mermaid = (await import(moduleUrl)).default;
    mermaid.initialize({ startOnLoad: false, logLevel: "fatal",
                         suppressErrorRendering: true });
    try {
      await mermaid.render(
          "error-fixture-invalid-suppressed",
          "flowchart TB\nsubgraph S[Group]\nA --> B");
      return { status: "ready" };
    } catch (e) {
      const own = document.querySelector(
          "#derror-fixture-invalid-suppressed svg");
      return { status: "error", name: e.name,
               message: e.message.slice(0, 80), fallbackSvg: !!own };
    }
  }, { moduleUrl });
  out.failures["invalid-suppressed"] = semantics;
}

// Default-theme PNG at the max-width client box (512 wide; the replaced
// element's height follows the viewBox aspect). Element screenshot with
// omitBackground — the repo convention for pixel oracles — so the image has
// the same transparent background as the native raster.
{
  await page.evaluate(async ({ moduleUrl, fontUrl, common }) => {
    const pinned = (source, extra = {}) =>
      `%%{init: ${JSON.stringify({ ...common, ...extra })}}%%\n${source}`;
    document.body.innerHTML = "";
    document.body.style.background = "transparent";
    const font = document.createElement("style");
    font.textContent = `@font-face{font-family:"Noto Sans";src:url("${fontUrl}");font-weight:400;font-style:normal}`;
    document.head.appendChild(font);
    await document.fonts.load('16px "Noto Sans"', "Syntax error in text mermaid version 0123456789.");
    await document.fonts.ready;
    const div = document.createElement("div");
    div.id = "shot";
    div.style.width = "512px";
    // Keep the SVG away from the page origin: the per-path crop clips subtract
    // a 2px guard from the path rect, and a negative clip coordinate makes
    // Puppeteer capture from the page origin instead of the requested window.
    div.style.margin = "8px";
    document.body.appendChild(div);
    const mermaid = (await import(moduleUrl)).default;
    mermaid.initialize({ startOnLoad: false, logLevel: "fatal" });
    const rendered = await mermaid.render("error-fixture-shot", pinned("error"));
    document.getElementById("shot").innerHTML = rendered.svg;
  }, { moduleUrl, fontUrl, common });
  const svg = await page.$("svg");
  const pngDir = path.join(path.dirname(output), "error-diagram");
  fs.mkdirSync(pngDir, { recursive: true });
  const pngPath = path.join(pngDir, "default.png");
  await svg.screenshot({ path: pngPath, omitBackground: true });
  const bounds = await svg.boundingBox();
  out.png = {
    file: "error-diagram/default.png",
    sha256: createHash("sha256").update(fs.readFileSync(pngPath)).digest("hex"),
    width: Math.round(bounds.width),
    height: Math.round(bounds.height),
  };

  // Per-path isolated crops for the per-icon pixel oracle: same visibility-
  // isolation capture as the golden-pixel harness (hide every SVG descendant,
  // re-show the target + ancestors), so each browser PNG contains exactly one
  // icon — the native test renders the matching single path through
  // error::paintErrorIcon into the same window. getBoundingClientRect excludes
  // stroke, which is fine for the default theme (icons are fill-only,
  // stroke:none). Clips are recorded relative to the SVG client origin so the
  // native side can reproduce the window in the 512-wide client-box space.
  const svgOrigin = await page.evaluate(() => {
    const rect = document.querySelector("svg").getBoundingClientRect();
    return { x: rect.left, y: rect.top };
  });
  const icons = [];
  for (let nth = 1; nth <= 6; nth++) {
    const selector = `path.error-icon:nth-of-type(${nth})`;
    const clip = await page.$eval(selector, (node) => {
      const rect = node.getBoundingClientRect();
      const guard = 2;
      return {
        x: rect.left - guard,
        y: rect.top - guard,
        width: Math.max(1, rect.width + 2 * guard),
        height: Math.max(1, rect.height + 2 * guard),
      };
    });
    await page.evaluate((selector) => {
      const root = document.querySelector("svg");
      for (const node of root.querySelectorAll("*"))
        node.style.visibility = "hidden";
      const selected = root.querySelector(selector);
      for (let node = selected; node && node !== root; node = node.parentElement)
        node.style.visibility = "visible";
      for (const child of selected.querySelectorAll("*"))
        child.style.visibility = "visible";
    }, selector);
    const shot = await page.screenshot({ omitBackground: true, clip });
    const png = PNG.sync.write(PNG.sync.read(shot), {
      colorType: 6,
      inputColorType: 6,
      bitDepth: 8,
    });
    const file = `icon-${nth - 1}.png`;
    fs.writeFileSync(path.join(pngDir, file), png);
    icons.push({
      index: nth - 1,
      file: `error-diagram/${file}`,
      sha256: createHash("sha256").update(png).digest("hex"),
      width: PNG.sync.read(png).width,
      height: PNG.sync.read(png).height,
      clip: {
        x: clip.x - svgOrigin.x,
        y: clip.y - svgOrigin.y,
        width: clip.width,
        height: clip.height,
      },
    });
  }
  await page.evaluate(() => {
    for (const node of document.querySelector("svg").querySelectorAll("*"))
      node.style.visibility = "";
  });
  out.png.icons = icons;
}
await browser.close();

const payload = {
  upstream: { version: "11.16.0" },
  ...out,
};
const canonical = JSON.stringify(payload);
payload.fixtureSha256 = createHash("sha256").update(canonical).digest("hex");
fs.writeFileSync(output, JSON.stringify(payload, null, 2) + "\n");
console.log(`wrote ${output}`);
console.log(`fixtureSha256=${payload.fixtureSha256}`);
