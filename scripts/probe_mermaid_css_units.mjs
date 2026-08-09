// Probe the CSS length units the Pie/Quadrant painters must resolve with a REAL
// context (not the CssLengthContext{} placeholder): viewport units (vw/vh/vmin/
// vmax), font-relative ex/ch, and PERCENTAGE (which resolveCssLengthToPx does NOT
// handle in the single-value path). mmdc's default raster viewport is 800x600
// (RequirementScene.cpp:46); puppeteer is launched at the same size so vw/vh
// resolve the same way the browser raster would.
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
  defaultViewport: { width: 800, height: 600 },
});
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 800, height: 600 });
  await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;
  const pieBody = 'pie title T\n"A" : 50\n"B" : 50';
  const out = await page.evaluate(async ({ mod, pieBody }) => {
    const { default: mermaid } = await import(mod);
    const mount = (svg) => {
      const c = document.createElement("div");
      c.style.cssText = "position:absolute;left:-9999px;top:0;";
      c.innerHTML = svg;
      document.body.appendChild(c);
      return c;
    };
    const read = async (tvKey, tvValue) => {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
      const src = `%%{init: {"themeVariables": {"${tvKey}": "${tvValue}"}}}%%\n${pieBody}`;
      const { svg } = await mermaid.render("p", src);
      const c = mount(svg);
      const slice = c.querySelector("path.pieCircle");
      const title = c.querySelector("text.pieTitleText");
      const r = {
        strokeWidth: slice ? getComputedStyle(slice).strokeWidth : null,
        fontSize: title ? getComputedStyle(title).fontSize : null,
      };
      document.body.removeChild(c);
      return r;
    };
    const units = ["10vw", "10vh", "10vmin", "10vmax", "10ex", "10ch", "200%", "50%"];
    const r = { strokeWidth: {}, fontSize: {} };
    for (const u of units) {
      r.strokeWidth[u] = (await read("pieStrokeWidth", u)).strokeWidth;
      r.fontSize[u] = (await read("pieTitleTextSize", u)).fontSize;
    }
    // The pie SVG viewport (viewBox / width / height): SVG stroke-width % is
    // resolved at paint against the normalized diagonal sqrt(w^2+h^2)/sqrt(2) of
    // the SVG viewport, so capture it to derive the basis.
    mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
    const { svg } = await mermaid.render("p", pieBody);
    const c = mount(svg);
    const svgEl = c.querySelector("svg");
    r.svgViewport = svgEl
      ? { width: svgEl.getAttribute("width"), height: svgEl.getAttribute("height"),
          viewBox: svgEl.getAttribute("viewBox"),
          style: svgEl.getAttribute("style") }
      : null;
    document.body.removeChild(c);
    return r;
  }, { mod, pieBody });
  console.log(JSON.stringify(out, null, 2));
} finally {
  await browser.close();
}
