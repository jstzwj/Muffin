// Probe: are the pie SCALAR themeVariables actually CONSUMED by the upstream
// mermaid 11.16.0 pie renderer (LIVE), or silently ignored (DEAD)? And for the
// live ones, what does the browser compute for edge cases (zero / unitless /
// non-px / invalid / negative)?
//
// This gates the Pie adapter wiring (Codex step 3). A DEAD key must NOT be wired:
// if the browser ignores pieStrokeWidth but the native adapter applied an
// override, native would diverge. A LIVE key's computed semantics define the C++
// helper (pixelValue for "Npx", a new opacityValue for unitless opacity).
//
// Method: render a 2-slice pie with each scalar overridden to a distinctive
// value, read getComputedStyle off the slice path / outer circle / title /
// section / legend text, and compare to the no-override baseline.
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
  const out = await page.evaluate(async ({ mod, pieBody }) => {
    const { default: mermaid } = await import(mod);
    const mount = (svg) => {
      const c = document.createElement("div");
      c.style.cssText = "position:absolute;left:-9999px;top:0;";
      c.innerHTML = svg;
      document.body.appendChild(c);
      return c;
    };
    const read = async (vars) => {
      mermaid.initialize({
        startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic",
        themeVariables: vars,
      });
      const { svg } = await mermaid.render("p", pieBody);
      const c = mount(svg);
      const slice = c.querySelector("path.pieCircle");
      const outer = c.querySelector("circle.pieOuterCircle");
      const title = c.querySelector("text.pieTitleText");
      const section = c.querySelector("text.slice");
      const legend = c.querySelector("g.legend text");
      const cs = (el, p) => (el ? getComputedStyle(el)[p] : null);
      const r = {
        sliceStroke: cs(slice, "stroke"),
        sliceStrokeWidth: cs(slice, "stroke-width"),
        sliceOpacity: cs(slice, "opacity"),
        outerStroke: cs(outer, "stroke"),
        outerStrokeWidth: cs(outer, "stroke-width"),
        titleFontSize: cs(title, "font-size"),
        sectionFontSize: cs(section, "font-size"),
        legendFontSize: cs(legend, "font-size"),
      };
      document.body.removeChild(c);
      return r;
    };
    const r = {};
    // Liveness: override each scalar with a distinctive value; compare to baseline.
    r.liveness = {
      baseline: await read({}),
      sw: await read({ pieStrokeWidth: "5px" }),
      osw: await read({ pieOuterStrokeWidth: "7px" }),
      op: await read({ pieOpacity: "0.4" }),
      sc: await read({ pieStrokeColor: "#ff0000" }),
      osc: await read({ pieOuterStrokeColor: "#00ff00" }),
      ts: await read({ pieTitleTextSize: "30px" }),
      ss: await read({ pieSectionTextSize: "20px" }),
      ls: await read({ pieLegendTextSize: "18px" }),
    };
    // Edge cases for the width + opacity keys.
    r.swEdges = {
      zeroPx: (await read({ pieStrokeWidth: "0px" })).sliceStrokeWidth,
      zeroNaked: (await read({ pieStrokeWidth: "0" })).sliceStrokeWidth,
      em: (await read({ pieStrokeWidth: "3em" })).sliceStrokeWidth,
      unitless: (await read({ pieStrokeWidth: "1.7" })).sliceStrokeWidth,
      invalid: (await read({ pieStrokeWidth: "abc" })).sliceStrokeWidth,
    };
    r.opEdges = {
      zero: (await read({ pieOpacity: "0" })).sliceOpacity,
      clamp: (await read({ pieOpacity: "1.7" })).sliceOpacity,
      neg: (await read({ pieOpacity: "-0.5" })).sliceOpacity,
      invalid: (await read({ pieOpacity: "abc" })).sliceOpacity,
    };
    return r;
  }, { mod, pieBody });
  console.log(JSON.stringify(out, null, 2));
} finally {
  await browser.close();
}
