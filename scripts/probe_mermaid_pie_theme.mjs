// Probe mermaid 11.16.0 default-theme pie theme variables (font sizes, stroke
// widths, colors, opacity) so the native PieSceneStyle defaults match. Reuses
// the headless-Chrome path; reads themeVariables via mermaid.getThemeVariables.
import path from "node:path";
import fs from "node:fs";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const chrome = process.argv[3] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

const browser = await puppeteer.launch({ headless: true, executablePath: chrome, args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const out = await page.evaluate(async (mod) => {
    const { default: mermaid } = await import(mod);
    const all = {};
    for (const theme of ["default", "dark"]) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict", theme, look: "classic" });
      // getThemeVariables is internal; read it off the configured theme via a
      // throwaway render then inspect computed styles is unreliable, so use the
      // public-ish mermaid externalDiagramWidth... Instead read the raw config
      // themeVariables through getThemeVariables if present.
      const tv = (mermaid.getThemeVariables ? mermaid.getThemeVariables() : null);
      const pick = (k) => (tv && tv[k] !== undefined ? tv[k] : null);
      all[theme] = {
        fontFamily: pick("fontFamily"),
        pieTitleTextSize: pick("pieTitleTextSize"),
        pieSectionTextSize: pick("pieSectionTextSize"),
        pieLegendTextSize: pick("pieLegendTextSize"),
        pieTitleTextColor: pick("pieTitleTextColor"),
        pieSectionTextColor: pick("pieSectionTextColor"),
        pieLegendTextColor: pick("pieLegendTextColor"),
        pieStrokeColor: pick("pieStrokeColor"),
        pieStrokeWidth: pick("pieStrokeWidth"),
        pieOpacity: pick("pieOpacity"),
        pieOuterStrokeColor: pick("pieOuterStrokeColor"),
        pieOuterStrokeWidth: pick("pieOuterStrokeWidth"),
      };
    }
    return all;
  }, mod);
  console.log(JSON.stringify(out, null, 2));
} finally {
  await browser.close();
}
