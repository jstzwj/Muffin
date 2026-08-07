// Source-entry probe: confirm how THEME_COLOR_LIMIT controls the pie slice-fill
// distribution for the cScale-derived themes (dark, neutral). Upstream writes
// `for (i=0; i<THEME_COLOR_LIMIT; i++) this["pie"+i] = this["cScale"+i]` (0-based
// keys), then the renderer maps slice k -> pie{k+1}. So a small TCL leaves the
// later slices unset (null attr). This locks the exact distribution the native
// FlowTheme port (populatePieFromCScale) must reproduce.
import path from "node:path";
import { pathToFileURL } from "node:url";
const mr = path.resolve("../mermaid-cli/node_modules/mermaid");
const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mr), "puppeteer/lib/puppeteer/puppeteer.js")));
const browser = await puppeteer.launch({ headless: true, executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe", args: ["--allow-file-access-from-files"] });
const THEMES = ["dark", "neutral"];
const TCLS = [0, 1, 2, 13];
// 13 slices so we can observe where fills stop being assigned.
const pieSrc = "pie\n" + Array.from({ length: 13 }, (_, i) => `"S${i + 1}" : ${i + 1}`).join("\n");

try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;
  const out = await page.evaluate(async ({ THEMES, TCLS, pieSrc, mod }) => {
    const mount = (svg) => { const c = document.createElement("div"); c.style.cssText = "position:absolute;left:-9999px;top:0;width:1000px;height:1000px;"; c.innerHTML = svg; document.body.appendChild(c); return c; };
    const { default: mermaid } = await import(mod);
    const result = {};
    for (const th of THEMES) {
      result[th] = {};
      for (const tcl of TCLS) {
        // initialize() runs the theme calculate with the merged themeVariables;
        // getConfig().themeVariables holds the RESOLVED pie1..pie12 (model layer,
        // no render). Some TCL values (e.g. 0) crash upstream's calculate itself;
        // catch and record so the native port knows where upstream is undefined.
        try {
          mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: th, look: "classic",
            themeVariables: { THEME_COLOR_LIMIT: tcl } });
          const tv = mermaid.mermaidAPI.getConfig().themeVariables;
          result[th][tcl] = { ok: true, pie: Array.from({ length: 13 }, (_, i) => tv["pie" + i]) };
        } catch (e) {
          result[th][tcl] = { ok: false, error: String(e && e.message ? e.message : e) };
        }
      }
    }
    return result;
  }, { THEMES, TCLS, pieSrc, mod });

  for (const th of THEMES) {
    console.log("=== " + th + " ===");
    for (const tcl of TCLS) {
      const r = out[th][tcl];
      if (!r.ok) { console.log(`TCL=${String(tcl).padEnd(3)} UPSTREAM CRASH: ${r.error}`); continue; }
      const pie1to12 = r.pie.slice(1, 13);
      const setCount = pie1to12.filter((f) => f != null && f !== undefined).length;
      console.log(`TCL=${String(tcl).padEnd(3)} pie1..pie12 set=${setCount}/12  pie0=${JSON.stringify(r.pie[0])}  pie1..12=${JSON.stringify(pie1to12)}`);
    }
  }
} finally { await browser.close(); }
