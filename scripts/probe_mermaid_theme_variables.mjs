// Theme-0 probe (corrected): getComputedStyle requires an ATTACHED DOM — the
// previous version read CSS-driven fields off a detached div (all empty). This
// mounts each rendered SVG offscreen (position:absolute; left:-9999px, NOT
// display:none), records BOTH the attribute value and the computed value, and
// self-checks that no field is null/empty unless the upstream value is genuinely
// empty. It also verifies the two override entry points SEPARATELY:
// initialize({themeVariables}) vs %%{init}%% source-entry vs frontmatter.
// Output: tests/fixtures/mermaid/theme-probe.json (golden oracle for the port).
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
const mr = path.resolve("../mermaid-cli/node_modules/mermaid");
const out = path.resolve("tests/fixtures/mermaid/theme-probe.json");
const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mr), "puppeteer/lib/puppeteer/puppeteer.js")));
const browser = await puppeteer.launch({ headless: true, executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe", args: ["--allow-file-access-from-files"] });
const THEMES = ["default", "dark", "forest", "neutral", "neo", "neo-dark", "base", "redux", "redux-dark", "redux-color", "redux-dark-color"];
const pieSrc = "pie\n" + Array.from({ length: 13 }, (_, i) => `"S${i + 1}" : ${i + 1}`).join("\n");
const quadSrc = "quadrantChart\ntitle T\nx-axis L --> R\ny-axis B --> T\nquadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4\n\"P\": [0.5, 0.5]";

function mount(svg) {
  const c = document.createElement("div");
  c.style.cssText = "position:absolute;left:-9999px;top:0;width:1000px;height:1000px;";
  c.innerHTML = svg;
  document.body.appendChild(c);
  return c;
}

try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;
  const data = await page.evaluate(async ({ THEMES, pieSrc, quadSrc, mod }) => {
    const mount = (svg) => {
      const c = document.createElement("div");
      c.style.cssText = "position:absolute;left:-9999px;top:0;width:1000px;height:1000px;";
      c.innerHTML = svg;
      document.body.appendChild(c);
      return c;
    };
    const { default: mermaid } = await import(mod);
    // attr + computed for a node, with the container attached.
    const readFields = async (id, src) => {
      const { svg } = await mermaid.render(id, src);
      const c = mount(svg);
      // effective value: attribute if present, else computed (CSS-applied fields).
      const val = (sel, a) => { const e = c.querySelector(sel); if (!e) return null; const av = e.getAttribute(a); return av !== null ? av : getComputedStyle(e)[a]; };
      const allV = (sel, a) => [...c.querySelectorAll(sel)].map((e) => { const av = e.getAttribute(a); return av !== null ? av : getComputedStyle(e)[a]; });
      const comp = (sel, p) => { const e = c.querySelector(sel); return e ? getComputedStyle(e)[p] : null; };
      const out = {};
      // PIE
      out.pie = {
        fills: allV("path.pieCircle", "fill").slice(0, 12),
        titleTextFill: val("text.pieTitleText", "fill"), titleFontSize: comp("text.pieTitleText", "font-size"),
        sectionTextFill: val("text.slice", "fill"), sectionFontSize: comp("text.slice", "font-size"),
        legendTextFill: val("g.legend text", "fill"), legendFontSize: comp("g.legend text", "font-size"),
        sliceStroke: val("path.pieCircle", "stroke"), sliceStrokeWidth: comp("path.pieCircle", "stroke-width"),
        sliceOpacity: comp("path.pieCircle", "opacity"),
        outerStroke: val("circle.pieOuterCircle", "stroke"), outerStrokeWidth: comp("circle.pieOuterCircle", "stroke-width"),
      };
      // QUADRANT
      out.quadrant = {
        fills: allV("g.quadrant rect", "fill"),
        textFills: allV("g.quadrant text", "fill"),
        pointFill: val("g.data-point circle", "fill"), pointStroke: val("g.data-point circle", "stroke"),
        pointStrokeWidth: val("g.data-point circle", "stroke-width"),
        pointTextFill: val("g.data-point text", "fill"),
        axisTextFill: val("g.label text", "fill"),
        borderStroke: val("g.border line", "stroke"), borderStrokeWidth: comp("g.border line", "stroke-width"),
        titleFill: val("g.title text", "fill"),
      };
      document.body.removeChild(c);
      return out;
    };
    const themes = {};
    for (const th of THEMES) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: th, look: "classic" });
      themes[th] = { pie: (await readFields("tp" + th, pieSrc)).pie, quadrant: (await readFields("tq" + th, quadSrc)).quadrant };
    }
    // Override entry points (separate): initialize API vs %%{init}%% source-entry.
    mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic", themeVariables: { pie1: "#abcdef", quadrant1Fill: "#112233" } });
    const initApi = { pie1: (await readFields("oi1", pieSrc)).pie.fills[0], quadrant1Fill: (await readFields("oi2", quadSrc)).quadrant.fills[0] };
    mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
    const sePie = (await readFields("se1", '%%{init: {"themeVariables":{"pie1":"#abcdef"}}}%%\n' + pieSrc)).pie.fills[0];
    const seQuad = (await readFields("se2", '%%{init: {"themeVariables":{"quadrant1Fill":"#112233"}}}%%\n' + quadSrc)).quadrant.fills[0];
    const fmPie = (await readFields("fm1", "---\nconfig:\n  themeVariables:\n    pie1: \"#abcdef\"\n---\n" + pieSrc)).pie.fills[0];
    return { upstream: "mermaid 11.16.0", note: "attr=getAttribute; comp=getComputedStyle on attached DOM", themes,
      overrides: { initializeApi: initApi, sourceEntryInit: { pie1: sePie, quadrant1Fill: seQuad }, frontmatter: { pie1: fmPie } } };
  }, { THEMES, pieSrc, quadSrc, mod });
  // Self-check: flag null/empty fields.
  const empties = [];
  for (const [th, tv] of Object.entries(data.themes)) {
    for (const [k, v] of Object.entries(tv.pie)) if (k !== "fills") (Array.isArray(v) ? v : [v]).forEach((x, i) => { if (x === null || x === "" || x === "none") empties.push(`${th}.pie.${k}${Array.isArray(v) ? "["+i+"]" : ""}=${JSON.stringify(x)}`); });
    for (const [k, v] of Object.entries(tv.quadrant)) (Array.isArray(v) ? v : [v]).forEach((x, i) => { if (x === null || x === "" || x === "none") empties.push(`${th}.quadrant.${k}${Array.isArray(v) ? "["+i+"]" : ""}=${JSON.stringify(x)}`); });
  }
  data.selfCheck = { emptyFieldCount: empties.length, empties: empties.slice(0, 40) };
  fs.writeFileSync(out, JSON.stringify(data, null, 2) + "\n");
  for (const t of THEMES) {
    const p = data.themes[t].pie, q = data.themes[t].quadrant;
    console.log(`${t.padEnd(16)} pie1=${p.fills[0]} titleFill=${p.titleTextFill} sliceStroke=${p.sliceStroke} op=${p.sliceOpacity} | q1=${q.fills[0]} ptStroke=${q.pointStroke} border=${q.borderStroke}`);
  }
  console.log("selfCheck empty fields:", empties.length, empties.slice(0, 8));
  console.log("override initialize:", JSON.stringify(data.overrides.initializeApi));
  console.log("override %%{init}%%:", JSON.stringify(data.overrides.sourceEntryInit));
  console.log("override frontmatter:", JSON.stringify(data.overrides.frontmatter));
} finally { await browser.close(); }
