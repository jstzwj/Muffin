// Probe: quadrantPointFill is an UNCONDITIONAL reassignment upstream
//   (this.quadrantPointFill || isDark(this.quadrant1Fill)) ? lighten(q1) : darken(q1)
// not a ||-guarded assign-if-empty. So a user quadrant1Fill override must
// re-derive quadrantPointFill from the NEW q1 (Default's double pass: pointFill
// is already set on the second updateColors -> non-empty -> lighten branch ->
// new q1's hue/saturation). A direct quadrantPointFill override still wins.
//
// Captures pointFill for: base (no override), quadrant1Fill="#112233", and a
// direct quadrantPointFill="#aabbcc" override. The hue/saturation of #112233's
// pointFill must match across Default (double pass) and Forest/Base (single pass),
// and must DIFFER from each theme's own base pointFill (which derives from its
// default primaryColor).
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
  const out = await page.evaluate(async ({ mod }) => {
    const { default: mermaid } = await import(mod);
    const tv = (theme, vars) => {
      mermaid.initialize({
        startOnLoad: false, securityLevel: "loose", theme, look: "classic",
        themeVariables: vars,
      });
      const c = mermaid.mermaidAPI.getConfig().themeVariables;
      return { q1: c.quadrant1Fill, pointFill: c.quadrantPointFill };
    };
    // Extract hsl(h, s%) hue/saturation from a "hsl(h, s%, NaN%)" pointFill.
    const parseHS = (s) => {
      const m = /^hsl\(([^,]+),\s*([^,]+)%/.exec(s || "");
      return m ? `${m[1].trim()}|${m[2].trim()}` : null;
    };
    const r = {};
    for (const th of ["default", "forest", "base"]) {
      const base = tv(th, {});
      const q1ov = tv(th, { quadrant1Fill: "#112233" });
      const direct = tv(th, { quadrantPointFill: "#aabbcc" });
      r[th] = {
        base,
        q1Override: q1ov,
        q1OverrideHS: parseHS(q1ov.pointFill),
        baseHS: parseHS(base.pointFill),
        direct,
      };
    }
    return r;
  }, { mod });
  console.log(JSON.stringify(out, null, 2));
} finally {
  await browser.close();
}
