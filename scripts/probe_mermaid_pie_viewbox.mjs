// Probe the upstream pie SVG viewBox vs the diagram TITLE (pieRenderer draw()
// lines 280-286): the title <text> is centered at group-local x=0 (= pieWidth/2
// = 225 SVG px); when its rendered width exceeds the chart+legend extent the
// viewBox grows -- viewBoxX = min(0, pieWidth/2 - titleWidth/2) goes negative,
// viewBoxRight = max(chartAndLegendWidth, pieWidth/2 + titleWidth/2).
//
// SVG stroke-width % is resolved at paint against the normalized diagonal of THIS
// viewBox, so the native adapter must replicate the title expansion (not just use
// the pie+legend bounds) or its % stroke-width diverges for long titles.
//
// Also captures the title's getBoundingClientRect().width (the exact value
// upstream feeds titleWidth) and its computed font-size/family, to anchor the
// native QFontMetrics measurement and confirm font parity.
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
  const out = await page.evaluate(async ({ mod }) => {
    const { default: mermaid } = await import(mod);
    const mount = (svg) => {
      const c = document.createElement("div");
      c.style.cssText = "position:absolute;left:-9999px;top:0;";
      c.innerHTML = svg;
      document.body.appendChild(c);
      return c;
    };
    const render = async (src) => {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
      const { svg } = await mermaid.render("p", src);
      const c = mount(svg);
      const svgEl = c.querySelector("svg");
      const title = c.querySelector("text.pieTitleText");
      const r = {
        viewBox: svgEl ? svgEl.getAttribute("viewBox") : null,
        titleBBoxWidth: title ? title.getBoundingClientRect().width : null,
        titleFontSize: title ? getComputedStyle(title).fontSize : null,
        titleFontFamily: title ? getComputedStyle(title).fontFamily : null,
        svgStyle: svgEl ? svgEl.getAttribute("style") : null,
      };
      document.body.removeChild(c);
      return r;
    };
    const r = {};
    // Baseline: no title, then increasing title lengths (default title font 25px).
    const slices = '"A" : 50\n"B" : 50';
    r.noTitle = await render(`pie\n${slices}`);
    const titles = [
      "pie title T",
      "pie title Short",
      "pie title A Moderately Long Pie Chart Title",
      "pie title An Extremely Long Pie Chart Title That Exceeds The Chart Width",
    ];
    r.titles = {};
    for (const t of titles) r.titles[t] = await render(`${t}\n${slices}`);
    // Vary the title font-size: a larger title font widens the title -> wider viewBox.
    r.titleSize = {};
    for (const ts of ["10px", "25px", "40px", "60px"]) {
      const src = `%%{init: {"themeVariables": {"pieTitleTextSize": "${ts}"}}}%%\n pie title A Moderately Long Pie Chart Title\n${slices}`;
      r.titleSize[ts] = await render(src);
    }
    return r;
  }, { mod });
  console.log(JSON.stringify(out, null, 2));
} finally {
  await browser.close();
}
