// Probe: does a taskTextDarkColor / mainContrastColor override propagate to the
// resolved pieTitleTextColor / pieLegendTextColor (model layer), and does a
// direct pieTitleTextColor override win? Settles the two-pass derivation order.
import path from "node:path";
import { pathToFileURL } from "node:url";
const mr = path.resolve("../mermaid-cli/node_modules/mermaid");
const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mr), "puppeteer/lib/puppeteer/puppeteer.js")));
const browser = await puppeteer.launch({ headless: true, executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe", args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;
  const out = await page.evaluate(async ({ mod }) => {
    const { default: mermaid } = await import(mod);
    const tv = (theme, vars) => {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme, look: "classic", themeVariables: vars });
      const c = mermaid.mermaidAPI.getConfig().themeVariables;
      return { pieTitle: c.pieTitleTextColor, pieLegend: c.pieLegendTextColor, pieSection: c.pieSectionTextColor, ttdc: c.taskTextDarkColor, mcc: c.mainContrastColor };
    };
    const r = {};
    for (const th of ["default", "forest", "neutral", "base", "neo", "redux-color"]) {
      r[th] = {
        base: tv(th, {}),
        ttdc: tv(th, { taskTextDarkColor: "#abcdef" }),
        direct: tv(th, { taskTextDarkColor: "#abcdef", pieTitleTextColor: "#111111" }),
      };
    }
    r.dark = {
      base: tv("dark", {}),
      mcc: tv("dark", { mainContrastColor: "#fedcba" }),
      direct: tv("dark", { mainContrastColor: "#fedcba", pieTitleTextColor: "#111111" }),
    };
    return r;
  }, { mod });
  console.log(JSON.stringify(out, null, 2));
} finally { await browser.close(); }
