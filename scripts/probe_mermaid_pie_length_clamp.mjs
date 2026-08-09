// Probe Chromium's CSS length USED-VALUE saturation caps for the pie scalars:
//  - font-size computed value saturates at ~10000px (getComputedStyle returns the
//    clamped px), so a config font-size:1e9px renders at 10000px, NOT 1e9.
//  - stroke-width computed value saturates at a much larger cap (~2^25); probe the
//    EXACT boundary (do not trust the approximate "~3.35544e7").
// Plus the cascade: a capped root (1e9px -> 10000) feeding em/ex/ch/% children --
// is the child computed value re-capped, and how does ex/ch (measured at the capped
// root size) flow into a stroke-width that is itself capped?
//
// Why it matters for native: cssFontSizePx/cssStrokeWidthPx currently return the
// raw finite positive value, which then reaches qRound()/QFont::setPixelSize(int)
// or QPen -- a >INT_MAX value overflows int (stability), and any huge value is a
// geometry divergence from the browser.
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
      c.style.cssText = "position:absolute;left:-9999px;top:0;font-size:16px;";
      c.innerHTML = svg;
      document.body.appendChild(c);
      return c;
    };
    // title font-size via pieTitleTextSize (a font-size on .pieTitleText).
    const titleFs = async (initBlock) => {
      const src = `${initBlock}\n pie title T\n"A" : 50\n"B" : 50`;
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", look: "classic" });
      const { svg } = await mermaid.render("p", src);
      const c = mount(svg);
      const title = c.querySelector("text.pieTitleText");
      const v = title ? getComputedStyle(title).fontSize : null;
      document.body.removeChild(c);
      return v;
    };
    // computed stroke-width on a slice path.
    const scalar = async (initBlock) => {
      const src = `${initBlock}\n pie title T\n"A" : 50\n"B" : 50`;
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", look: "classic" });
      const { svg } = await mermaid.render("p", src);
      const c = mount(svg);
      const el = c.querySelector("path.pieCircle");
      const typed = el?.computedStyleMap?.().get("stroke-width");
      const v = el ? {
        css: getComputedStyle(el).strokeWidth,
        // getComputedStyle serializes all values near the cap as
        // "3.35544e+07px". Typed OM preserves the exact used value.
        typedPx: typed?.unit === "px" ? typed.value : null,
      } : null;
      document.body.removeChild(c);
      return v;
    };
    const tv = (v) => `"pieTitleTextSize": "${v}"`;
    const sv = (v) => `"pieStrokeWidth": "${v}"`;
    const r = {};
    // font-size saturation boundary (cap reported ~10000).
    r.fontsize = {
      f9999: await titleFs(`%%{init: {"themeVariables": {${tv("9999px")}}}}%%`),
      f9999_9: await titleFs(`%%{init: {"themeVariables": {${tv("9999.9px")}}}}%%`),
      f10000: await titleFs(`%%{init: {"themeVariables": {${tv("10000px")}}}}%%`),
      f10000_5: await titleFs(`%%{init: {"themeVariables": {${tv("10000.5px")}}}}%%`),
      f10001: await titleFs(`%%{init: {"themeVariables": {${tv("10001px")}}}}%%`),
      f100000: await titleFs(`%%{init: {"themeVariables": {${tv("100000px")}}}}%%`),
      f1e9: await titleFs(`%%{init: {"themeVariables": {${tv("1e9px")}}}}%%`),
      f1e10: await titleFs(`%%{init: {"themeVariables": {${tv("10000000000px")}}}}%%`),
    };
    // stroke-width saturation boundary. The exact Typed OM cap is 33554428;
    // getComputedStyle alone cannot distinguish these values.
    r.strokewidth = {
      s2_24: await scalar(`%%{init: {"themeVariables": {${sv("16777216px")}}}}%%`),   // 2^24
      belowCap: await scalar(`%%{init: {"themeVariables": {${sv("33554426px")}}}}%%`),
      atCap: await scalar(`%%{init: {"themeVariables": {${sv("33554428px")}}}}%%`),
      aboveCap: await scalar(`%%{init: {"themeVariables": {${sv("33554429px")}}}}%%`),
      s2_26: await scalar(`%%{init: {"themeVariables": {${sv("67108864px")}}}}%%`),    // 2^26
      s1e8: await scalar(`%%{init: {"themeVariables": {${sv("100000000px")}}}}%%`),
      s1e9: await scalar(`%%{init: {"themeVariables": {${sv("1e9px")}}}}%%`),
    };
    // cascade: a capped root (1e9px -> 10000) feeding em/%/ex/ch children.
    r.cascade = {
      // sw3em independently proves that the capped SVG root is 10000px: its
      // stroke width is 3 * inherited root = 30000px. titleFs here would read
      // the title's own default 25px and must not be labelled as the root.
      title3em: await titleFs(`%%{init: {"themeVariables": {"fontSize": "1e9px", ${tv("3em")}}}}%%`),
      title200pct: await titleFs(`%%{init: {"themeVariables": {"fontSize": "1e9px", ${tv("200%")}}}}%%`),
      sw3em: await scalar(`%%{init: {"themeVariables": {"fontSize": "1e9px", ${sv("3em")}}}}%%`),
      sw10ex: await scalar(`%%{init: {"themeVariables": {"fontSize": "1e9px", ${sv("10ex")}}}}%%`),
      sw10ch: await scalar(`%%{init: {"themeVariables": {"fontSize": "1e9px", ${sv("10ch")}}}}%%`),
    };
    return r;
  }, { mod });
  console.log(JSON.stringify(out, null, 2));
} finally {
  await browser.close();
}
