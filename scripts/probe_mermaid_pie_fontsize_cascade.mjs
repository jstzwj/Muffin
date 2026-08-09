// Probe the inherited font-size CASCADE for pie text (pieTitleTextSize etc.):
// the pie CSS sets `font-size: <pieTitleTextSize>` on .pieTitleText, whose
// parent (the SVG root) inherits themeVariables.fontSize. So:
//  - a VALID size (px/em/%) resolves relative to the inherited root;
//  - an INVALID / bare-number / negative value is dropped by the CSS parser and
//    INHERITS the root font-size (NOT a hardcoded 16);
//  - the root itself (themeVariables.fontSize) is resolved against the browser
//    <html> default 16px (so "2em" -> 32, neo's 14px -> 14).
//
// This pins the native helper: ctx.emPx = resolved root font-size, and the
// invalid/bare/negative fallback in cssFontSizePx must be ctx.emPx (not 16).
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
    const titleFs = async (initBlock) => {
      const src = `${initBlock}\n pie title T\n"A" : 50\n"B" : 50`;
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", look: "classic" });
      const { svg } = await mermaid.render("p", src);
      const c = mount(svg);
      const title = c.querySelector("text.pieTitleText");
      const svgEl = c.querySelector("svg");
      const r = {
        titleFontSize: title ? getComputedStyle(title).fontSize : null,
        svgFontSize: svgEl ? getComputedStyle(svgEl).fontSize : null,
      };
      document.body.removeChild(c);
      return r;
    };
    const r = {};
    // Root font-size resolution (default theme, themeVariables.fontSize varied).
    r.root = {
      default: await titleFs(`%%{init: {"themeVariables": {"fontSize": "16px"}}}%%`),
      em2: await titleFs(`%%{init: {"themeVariables": {"fontSize": "2em"}}}%%`),
      em2_pct200: await titleFs(`%%{init: {"themeVariables": {"fontSize": "2em", "pieTitleTextSize": "200%"}}}%%`),
      em2_bare25: await titleFs(`%%{init: {"themeVariables": {"fontSize": "2em", "pieTitleTextSize": "25"}}}%%`),
      invalidRoot: await titleFs(`%%{init: {"themeVariables": {"fontSize": "abc"}}}%%`),
    };
    // neo theme: root font-size 14px; invalid/bare/negative title size inherits 14.
    r.neo = {
      default: await titleFs(`%%{init: {"theme":"neo"}}%%`),
      invalid: await titleFs(`%%{init: {"theme":"neo","themeVariables": {"pieTitleTextSize": "abc"}}}%%`),
      bare: await titleFs(`%%{init: {"theme":"neo","themeVariables": {"pieTitleTextSize": "25"}}}%%`),
      neg: await titleFs(`%%{init: {"theme":"neo","themeVariables": {"pieTitleTextSize": "-2px"}}}%%`),
      empty: await titleFs(`%%{init: {"theme":"neo","themeVariables": {"pieTitleTextSize": ""}}}%%`),
      pct: await titleFs(`%%{init: {"theme":"neo","themeVariables": {"pieTitleTextSize": "200%"}}}%%`),
      em: await titleFs(`%%{init: {"theme":"neo","themeVariables": {"pieTitleTextSize": "3em"}}}%%`),
      valid: await titleFs(`%%{init: {"theme":"neo","themeVariables": {"pieTitleTextSize": "25px"}}}%%`),
    };
    return r;
  }, { mod });
  console.log(JSON.stringify(out, null, 2));
} finally {
  await browser.close();
}
