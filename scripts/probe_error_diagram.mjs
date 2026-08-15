// Dev-only probe (P1 error-surface recon): upstream 11.16 error rendering.
// Captures the literal "error" diagram DOM/computed/geometry, the 11-theme
// errorBkgColor/errorTextColor effective chain, and the parse-failure fallback
// semantics (leftover error svg in the temp element + thrown exception).
import path from "node:path";
import { pathToFileURL } from "node:url";
import fs from "node:fs";

const mr = path.resolve("../mermaid-cli/node_modules/mermaid");
const { default: puppeteer } = await import(
  pathToFileURL(path.join(mr, "..", "puppeteer/lib/puppeteer/puppeteer.js")).href
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe",
  args: ["--no-sandbox", "--allow-file-access-from-files", "--disable-gpu",
         "--force-device-scale-factor=1"],
});
const page = await browser.newPage();
await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;

const out = await page.evaluate(async ({ mod }) => {
  document.body.style.margin = "0";
  const mermaid = (await import(mod)).default;
  const container = () => {
    document.body.innerHTML = '<div id="container"></div>';
    return document.querySelector("#container");
  };
  const analyzeSvg = (svg) => {
    const attrs = Object.fromEntries([...svg.attributes].map((a) => [a.name, a.value]));
    const cs = getComputedStyle(svg);
    const rootBBox = svg.getBBox();
    const rect = svg.getBoundingClientRect();
    const styleEl = svg.querySelector("style");
    const paths = [...svg.querySelectorAll("path")].map((p) => {
      const s = getComputedStyle(p);
      const b = p.getBBox();
      return { class: p.getAttribute("class"), d: p.getAttribute("d"),
               computed: { fill: s.fill, stroke: s.stroke, strokeWidth: s.strokeWidth,
                           fillOpacity: s.fillOpacity },
               bbox: { x: b.x, y: b.y, width: b.width, height: b.height } };
    });
    const texts = [...svg.querySelectorAll("text")].map((t) => {
      const s = getComputedStyle(t);
      const b = t.getBBox();
      return { class: t.getAttribute("class"), text: t.textContent,
               attributes: Object.fromEntries([...t.attributes].map((a) => [a.name, a.value])),
               computed: { fontFamily: s.fontFamily, fontSize: s.fontSize,
                           fontWeight: s.fontWeight, fill: s.fill, stroke: s.stroke,
                           textAnchor: s.textAnchor, dominantBaseline: s.dominantBaseline,
                           alignmentBaseline: s.alignmentBaseline },
               textLength: t.getComputedTextLength(),
               bbox: { x: b.x, y: b.y, width: b.width, height: b.height },
               client: (() => { const r = t.getBoundingClientRect();
                                return { x: r.x, y: r.y, width: r.width, height: r.height }; })() };
    });
    return { attributes: attrs, rootComputed: { display: cs.display, fontFamily: cs.fontFamily },
             viewBoxBBox: { x: rootBBox.x, y: rootBBox.y, width: rootBBox.width, height: rootBBox.height },
             clientRect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height },
             styleContent: styleEl ? styleEl.textContent : null,
             childSummary: [...svg.children].map((c) => c.tagName + (c.getAttribute("class") ? "." + c.getAttribute("class") : "") + (c.id ? "#" + c.id : "")),
             paths, texts };
  };
  const result = { cases: {}, themes: {} };

  // 1. literal "error", default theme — full analysis.
  mermaid.initialize({ startOnLoad: false, logLevel: "fatal" });
  {
    const rendered = await mermaid.render("probe-literal", "error");
    result.cases["literal-error"] = { status: "ready", diagramType: rendered.diagramType,
                                      bindFunctions: typeof rendered.bindFunctions };
    const host = container();
    host.innerHTML = rendered.svg;
    result.cases["literal-error"].analysis = analyzeSvg(host.querySelector("svg"));
  }
  // 2. all 11 themes — effective icon/text colors only.
  for (const theme of ["default", "base", "dark", "forest", "neutral", "neo", "neo-dark",
                       "redux", "redux-dark", "redux-color", "redux-dark-color"]) {
    mermaid.initialize({ startOnLoad: false, logLevel: "fatal", theme });
    try {
      const rendered = await mermaid.render("probe-theme-" + theme, "error");
      const host = container();
      host.innerHTML = rendered.svg;
      const svg = host.querySelector("svg");
      const icon = getComputedStyle(svg.querySelector("path.error-icon"));
      const text = getComputedStyle(svg.querySelector("text.error-text"));
      result.themes[theme] = { iconFill: icon.fill, textFill: text.fill, textStroke: text.stroke };
    } catch (e) {
      result.themes[theme] = { error: e.message };
    }
  }
  // 3-8. failure semantics.
  const failure = async (label, source, init) => {
    mermaid.initialize({ startOnLoad: false, logLevel: "fatal", ...(init || {}) });
    try {
      const rendered = await mermaid.render("probe-" + label, source);
      return { status: "ready", diagramType: rendered.diagramType };
    } catch (e) {
      const leftovers = [...document.querySelectorAll("body > div")].map((d) => {
        const svg = d.querySelector("svg");
        return { id: d.id, hasSvg: !!svg,
                 svgClass: svg ? svg.getAttribute("class") : null,
                 viewBox: svg ? svg.getAttribute("viewBox") : null,
                 errorIconCount: svg ? svg.querySelectorAll("path.error-icon").length : 0,
                 errorTexts: svg ? [...svg.querySelectorAll("text.error-text")].map((t) => t.textContent) : [] };
      }).filter((d) => d.id);
      return { status: "error", name: e.name, message: e.message, leftovers };
    }
  };
  result.cases["dash-only"] = await failure("dash-only", "---");
  result.cases["unclosed-frontmatter"] = await failure(
      "unclosed-frontmatter", "---\ntitle: x\nflowchart TB\nA --> B");
  const invalidFlowchart = "flowchart TB\nsubgraph S[Group]\nA --> B";
  result.cases["invalid-flowchart"] = await failure("invalid-flowchart", invalidFlowchart);
  result.cases["invalid-suppressed"] = await failure("invalid-suppressed", invalidFlowchart,
      { suppressErrorRendering: true });
  result.cases["bad-yaml"] = await failure(
      "bad-yaml", "---\nconfig: [unclosed\n---\nflowchart TB\nA --> B");
  result.cases["unknown-diagram"] = await failure("unknown-diagram", "this is not a diagram");
  return result;
}, { mod });

// Screenshot the default-theme literal error svg at natural width.
{
  const host = await page.evaluate(() => {
    document.body.innerHTML = "";
    const div = document.createElement("div");
    div.id = "shot";
    div.style.width = "2412px";
    document.body.appendChild(div);
    return div.id;
  });
  const inserted = await page.evaluate(async ({ mod, hostId }) => {
    const mermaid = (await import(mod)).default;
    mermaid.initialize({ startOnLoad: false, logLevel: "fatal" });
    const rendered = await mermaid.render("probe-shot", "error");
    document.getElementById(hostId).innerHTML = rendered.svg;
    const svg = document.querySelector("svg");
    const r = svg.getBoundingClientRect();
    return { x: r.x, y: r.y, width: r.width, height: r.height,
             w: svg.getAttribute("width"), h: svg.getAttribute("height"),
             style: svg.getAttribute("style") };
  }, { mod, hostId: host });
  console.log("shot rect:", JSON.stringify(inserted));
  const shotDir = process.env.TEMP.replace(/\\/g, "/");
  await page.screenshot({
    path: shotDir + "/error-diagram-browser.png",
    clip: { x: inserted.x, y: inserted.y, width: inserted.width, height: inserted.height },
  });
}
await browser.close();
fs.writeFileSync(process.env.TEMP.replace(/\\/g, "/") + "/error-diagram-probe.json",
                 JSON.stringify(out, null, 2));
console.log("cases:", Object.keys(out.cases).join(", "));
console.log("themes:", Object.keys(out.themes).length);
console.log(JSON.stringify(out.cases["literal-error"].analysis.attributes));
console.log("client:", JSON.stringify(out.cases["literal-error"].analysis.clientRect));
console.log("childSummary:", out.cases["literal-error"].analysis.childSummary.join(" | "));
