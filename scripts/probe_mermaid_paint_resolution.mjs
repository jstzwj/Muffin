// Probe the SVG <paint> + numeric resolution the Pie/Quadrant painters must match
// for the Codex P1/P2 follow-up: none / currentColor / garbage for fill, stroke,
// and text; the outer-circle `r` attribute for a NUMBER vs STRING pieOuterStrokeWidth
// (upstream parseFontSize() branches on typeof); and opacity "50%" (percentage).
// Reads both the raw attribute and getComputedStyle so the model value (attr) and
// the paint result (computed) are both captured.
import path from "node:path";
import { pathToFileURL } from "node:url";
const mr = path.resolve("../mermaid-cli/node_modules/mermaid");
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mr), "puppeteer/lib/puppeteer/puppeteer.js"))
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe",
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;
  const pieBody = 'pie title T\n"A" : 50\n"B" : 50';
  const quadBody =
    'quadrantChart\ntitle T\nx-axis L --> R\ny-axis B --> T\nquadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4\n"P": [0.5, 0.5]';
  const out = await page.evaluate(async ({ mod, pieBody, quadBody }) => {
    const { default: mermaid } = await import(mod);
    const mount = (svg) => {
      const c = document.createElement("div");
      c.style.cssText = "position:absolute;left:-9999px;top:0;";
      c.innerHTML = svg;
      document.body.appendChild(c);
      return c;
    };
    const both = (el, p) => (el ? { attr: el.getAttribute(p), computed: getComputedStyle(el)[p] } : { attr: null, computed: null });
    const comp = (el, p) => (el ? getComputedStyle(el)[p] : null);

    const readPie = async (tvJson) => {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
      const src = (tvJson ? `%%{init: {"themeVariables": ${tvJson}}}%%\n` : "") + pieBody;
      const { svg } = await mermaid.render("p", src);
      const c = mount(svg);
      const slice = c.querySelector("path.pieCircle");
      const outer = c.querySelector("circle.pieOuterCircle");
      const section = c.querySelector("text.slice");
      const r = {
        sliceFill: both(slice, "fill"),
        sliceStroke: both(slice, "stroke"),
        sectionFill: both(section, "fill"),
        outerR: outer ? outer.getAttribute("r") : null,
        sliceOpacity: comp(slice, "opacity"),
      };
      document.body.removeChild(c);
      return r;
    };
    const readQuad = async (tvJson) => {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
      const src = (tvJson ? `%%{init: {"themeVariables": ${tvJson}}}%%\n` : "") + quadBody;
      const { svg } = await mermaid.render("q", src);
      const c = mount(svg);
      const rect = c.querySelector("g.quadrant rect");
      const borders = [...c.querySelectorAll("g.border line")];
      const extBorder = borders[0] || null;
      const r = {
        q1Fill: both(rect, "fill"),
        extBorderStroke: both(extBorder, "stroke"),
      };
      document.body.removeChild(c);
      return r;
    };

    const paints = ["none", "currentColor", "garbage", "inherit", "initial"];
    const r = { pie: {}, quadrant: {} };
    for (const v of paints) {
      const j = (k) => JSON.stringify({ [k]: v });
      r.pie["fill_" + v] = (await readPie(j("pie1"))).sliceFill;
      r.pie["stroke_" + v] = (await readPie(j("pieStrokeColor"))).sliceStroke;
      r.pie["text_" + v] = (await readPie(j("pieSectionTextColor"))).sectionFill;
      r.quadrant["fill_" + v] = (await readQuad(j("quadrant1Fill"))).q1Fill;
      r.quadrant["border_" + v] = (await readQuad(j("quadrantExternalBorderStrokeFill"))).extBorderStroke;
    }
    // Outer-circle r: NUMBER vs STRING pieOuterStrokeWidth (parseFontSize typeof branch).
    r.outerR = {
      default: (await readPie(null)).outerR,
      number_1_7: (await readPie('{"pieOuterStrokeWidth": 1.7}')).outerR,
      string_1_7: (await readPie('{"pieOuterStrokeWidth": "1.7"}')).outerR,
      string_2px: (await readPie('{"pieOuterStrokeWidth": "2px"}')).outerR,
    };
    // Opacity percentage.
    r.opacity = {
      default: (await readPie(null)).sliceOpacity,
      pct_50: (await readPie('{"pieOpacity": "50%"}')).sliceOpacity,
      pct_150: (await readPie('{"pieOpacity": "150%"}')).sliceOpacity,
    };
    return r;
  }, { mod, pieBody, quadBody });
  console.log(JSON.stringify(out, null, 2));
} finally {
  await browser.close();
}
