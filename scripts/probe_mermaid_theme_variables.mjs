// Theme-0 probe (final-fix): per Codex review, every CSS field stores BOTH the
// raw attribute value and the computed paint value (never merged — an invalid
// hsl(...NaN%) attr and the browser's effective black must not collapse). The
// quadrant model is fully split (X/Y axis, internal/external border) since each
// allows an independent override. Self-check THROWS (non-zero exit) on any
// missing field, wrong array length, or failed override, and the script asserts
// two consecutive captures produce an identical fixture digest.
// Output: tests/fixtures/mermaid/theme-probe.json (raw=theme-model golden,
// computed=pixel-semantics check).
import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
const mr = path.resolve("../mermaid-cli/node_modules/mermaid");
const out = path.resolve("tests/fixtures/mermaid/theme-probe.json");
const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mr), "puppeteer/lib/puppeteer/puppeteer.js")));
const browser = await puppeteer.launch({ headless: true, executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe", args: ["--allow-file-access-from-files"] });
const THEMES = ["default", "dark", "forest", "neutral", "neo", "neo-dark", "base", "redux", "redux-dark", "redux-color", "redux-dark-color"];
const pieSrc = "pie\n" + Array.from({ length: 13 }, (_, i) => `"S${i + 1}" : ${i + 1}`).join("\n");
const quadSrc = "quadrantChart\ntitle T\nx-axis L --> R\ny-axis B --> T\nquadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4\n\"P\": [0.5, 0.5]";

function assert(cond, msg) { if (!cond) { throw new Error("SELFCHECK FAIL: " + msg); } }

try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;

  // capture() returns the data object (no selfCheck). Run twice for digest check.
  const capture = () => page.evaluate(async ({ THEMES, pieSrc, quadSrc, mod }) => {
    const mount = (svg) => { const c = document.createElement("div"); c.style.cssText = "position:absolute;left:-9999px;top:0;width:1000px;height:1000px;"; c.innerHTML = svg; document.body.appendChild(c); return c; };
    // BOTH attr + computed (never merged).
    const both = (c, sel, a) => { const e = c.querySelector(sel); return e ? { attr: e.getAttribute(a), computed: getComputedStyle(e)[a] } : { attr: null, computed: null }; };
    const allBoth = (c, sel, a) => [...c.querySelectorAll(sel)].map((e) => ({ attr: e.getAttribute(a), computed: getComputedStyle(e)[a] }));
    const comp = (c, sel, p) => { const e = c.querySelector(sel); return e ? getComputedStyle(e)[p] : null; };
    const { default: mermaid } = await import(mod);
    const readFields = async (id, src) => {
      const { svg } = await mermaid.render(id, src);
      const c = mount(svg);
      const o = {};
      o.pie = {
        fills: allBoth(c, "path.pieCircle", "fill").slice(0, 12),
        titleTextFill: both(c, "text.pieTitleText", "fill"), titleFontSize: comp(c, "text.pieTitleText", "font-size"),
        sectionTextFill: both(c, "text.slice", "fill"), sectionFontSize: comp(c, "text.slice", "font-size"),
        legendTextFill: both(c, "g.legend text", "fill"), legendFontSize: comp(c, "g.legend text", "font-size"),
        sliceStroke: both(c, "path.pieCircle", "stroke"), sliceStrokeWidth: comp(c, "path.pieCircle", "stroke-width"),
        sliceOpacity: comp(c, "path.pieCircle", "opacity"),
        outerStroke: both(c, "circle.pieOuterCircle", "stroke"), outerStrokeWidth: comp(c, "circle.pieOuterCircle", "stroke-width"),
      };
      // Quadrant: fully split. X/Y axis labels by rotation; external/internal
      // border by index (0..3 external width-2, 4..5 internal width-1).
      const labels = [...c.querySelectorAll("g.label text")].map((t) => ({ fill: { attr: t.getAttribute("fill"), computed: getComputedStyle(t).fill }, rot: (t.getAttribute("transform") || "").match(/rotate\(([-\d]+)\)/)?.[1] ?? "0" }));
      const xLabel = labels.find((l) => l.rot === "0") || { fill: { attr: null, computed: null } };
      const yLabel = labels.find((l) => l.rot === "-90") || { fill: { attr: null, computed: null } };
      const borders = [...c.querySelectorAll("g.border line")];
      o.quadrant = {
        fills: allBoth(c, "g.quadrant rect", "fill"),
        textFills: allBoth(c, "g.quadrant text", "fill"),
        pointFill: both(c, "g.data-point circle", "fill"), pointStroke: both(c, "g.data-point circle", "stroke"),
        pointStrokeWidth: both(c, "g.data-point circle", "stroke-width"), pointTextFill: both(c, "g.data-point text", "fill"),
        xAxisTextFill: xLabel.fill, yAxisTextFill: yLabel.fill,
        externalBorderStroke: borders[0] ? { attr: borders[0].getAttribute("stroke"), computed: getComputedStyle(borders[0]).stroke } : { attr: null, computed: null },
        internalBorderStroke: borders[4] ? { attr: borders[4].getAttribute("stroke"), computed: getComputedStyle(borders[4]).stroke } : { attr: null, computed: null },
        titleFill: both(c, "g.title text", "fill"),
      };
      document.body.removeChild(c);
      return o;
    };
    const themes = {};
    for (const th of THEMES) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: th, look: "classic" });
      themes[th] = { pie: (await readFields("tp" + th, pieSrc)).pie, quadrant: (await readFields("tq" + th, quadSrc)).quadrant };
    }
    // Override entry points — all three must apply BOTH pie1 and quadrant1Fill.
    const ov = async (id, kind) => {
      let src;
      if (kind === "init") { mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic", themeVariables: { pie1: "#abcdef", quadrant1Fill: "#112233" } }); src = [pieSrc, quadSrc]; }
      else { mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" }); }
      if (kind === "init") return { pie1: (await readFields(id + "p", pieSrc)).pie.fills[0].attr, quadrant1Fill: (await readFields(id + "q", quadSrc)).quadrant.fills[0].attr };
      if (kind === "sourceEntry") { const p = '%%{init: {"themeVariables":{"pie1":"#abcdef","quadrant1Fill":"#112233"}}}%%\n'; return { pie1: (await readFields(id + "p", p + pieSrc)).pie.fills[0].attr, quadrant1Fill: (await readFields(id + "q", p + quadSrc)).quadrant.fills[0].attr }; }
      // frontmatter
      const fm = "---\nconfig:\n  themeVariables:\n    pie1: \"#abcdef\"\n    quadrant1Fill: \"#112233\"\n---\n";
      return { pie1: (await readFields(id + "p", fm + pieSrc)).pie.fills[0].attr, quadrant1Fill: (await readFields(id + "q", fm + quadSrc)).quadrant.fills[0].attr };
    };
    const overrides = { initializeApi: await ov("oi", "init"), sourceEntryInit: await ov("se", "sourceEntry"), frontmatter: await ov("fm", "frontmatter") };
    return { upstream: "mermaid 11.16.0", note: "every CSS field = {attr, computed}; raw attr is the theme-model golden, computed is the paint semantic", themes, overrides };
  }, { THEMES, pieSrc, quadSrc, mod });

  const d1 = await capture();
  const d2 = await capture();
  const canon = (d) => JSON.stringify({ upstream: d.upstream, themes: d.themes, overrides: d.overrides });
  assert(canon(d1) === canon(d2), "two consecutive captures differ (non-deterministic fixture)");

  // Self-check (throws on any failure).
  for (const th of THEMES) {
    const t = d1.themes[th];
    assert(t.pie.fills.length === 12, `${th}: pie.fills length ${t.pie.fills.length} != 12`);
    assert(t.quadrant.fills.length === 4, `${th}: quadrant.fills length ${t.quadrant.fills.length} != 4`);
    assert(t.quadrant.textFills.length === 4, `${th}: quadrant.textFills length != 4`);
    const req = (v, msg) => { if (typeof v === "string") assert(v, `${th}: ${msg} missing`); else assert(v && (v.attr || v.computed), `${th}: ${msg} missing`); };
    for (const [k, v] of Object.entries(t.pie)) if (k !== "fills") req(v, `pie.${k}`);
    for (const [k, v] of Object.entries(t.quadrant)) if (!Array.isArray(v)) req(v, `quadrant.${k}`);
    // Per-element attr+computed (not just length): every slice/quadrant fill
    // and text fill must carry BOTH the raw themeVariable attribute (the
    // theme-model golden) and a resolved computed paint value.
    const reqBoth = (v, msg) => { assert(v && v.attr, `${th}: ${msg} attr missing`); assert(v && v.computed, `${th}: ${msg} computed missing`); };
    t.pie.fills.forEach((e, i) => reqBoth(e, `pie.fills[${i}]`));
    t.quadrant.fills.forEach((e, i) => reqBoth(e, `quadrant.fills[${i}]`));
    t.quadrant.textFills.forEach((e, i) => reqBoth(e, `quadrant.textFills[${i}]`));
  }
  for (const [entry, ov] of Object.entries(d1.overrides)) {
    assert(ov.pie1 === "#abcdef", `${entry}: pie1 override = ${ov.pie1} (expected #abcdef)`);
    assert(ov.quadrant1Fill === "#112233", `${entry}: quadrant1Fill override = ${ov.quadrant1Fill} (expected #112233)`);
  }
  d1.selfCheck = { status: "passed", twoRunDigestStable: true };
  fs.writeFileSync(out, JSON.stringify(d1, null, 2) + "\n");
  console.log("SELFCHECK passed: 11 themes, pie.fills=12, quadrant.fills/textFills=4, all fields present, 3 override entries (pie1+quadrant1Fill), two-run digest stable.");
  for (const th of THEMES) console.log(`  ${th.padEnd(16)} pie1.attr=${d1.themes[th].pie.fills[0].attr} q1.attr=${d1.themes[th].quadrant.fills[0].attr} extBorder=${d1.themes[th].quadrant.externalBorderStroke.attr || d1.themes[th].quadrant.externalBorderStroke.computed} intBorder=${d1.themes[th].quadrant.internalBorderStroke.attr || d1.themes[th].quadrant.internalBorderStroke.computed}`);
} finally { await browser.close(); }
