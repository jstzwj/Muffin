// Freezes the mermaid 11.16.0 quadrantChart Gate-1 oracle in ONE headless-Chrome
// pass, writing three fixtures (mirroring the pie Gate-1 layout):
//   tests/fixtures/mermaid/quadrant-grammar.json  — accept/reject + DB state
//   tests/fixtures/mermaid/quadrant-geometry.json — deterministic layout (rects,
//     points, borders, title, axis labels) extracted from the rendered SVG
//   tests/fixtures/mermaid/quadrant-pixel/{default,dark}.png + manifest.json
//
// The layout is pure formula (config constants + d3 scaleLinear [0,1]); geometry
// is font-independent and the byte-parity target. The implementer (Gate 2/3) must
// reproduce these values, NOT re-derive them — author/implementer separation.
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { createHash } from "node:crypto";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const outDir = path.resolve(process.argv[3] ?? "tests/fixtures/mermaid");
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (pkg.version !== "11.16.0") throw new Error(`Expected 11.16.0, found ${pkg.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);
fs.mkdirSync(path.join(outDir, "quadrant-pixel"), { recursive: true });

// ---- grammar cases (real mermaid decides accept/reject + records the DB) ----
const grammarCases = [
  { id: "g_empty", src: "quadrantChart" },
  { id: "g_title", src: "quadrantChart\ntitle Reach vs Engagement" },
  { id: "g_acc", src: "quadrantChart\naccTitle: AT\naccDescr: AD" },
  { id: "g_axes", src: "quadrantChart\nx-axis Low --> High\ny-axis Down --> Up" },
  { id: "g_quadrants", src: "quadrantChart\nquadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4" },
  { id: "g_point_simple", src: "quadrantChart\npoint1: [0.5, 0.7]" },
  { id: "g_point_text", src: 'quadrantChart\n"Point A": [0.3, 0.8]' },
  { id: "g_point_corners", src: "quadrantChart\na: [0, 0]\nb: [1, 1]\nc: [0.0, 1.0]\nd: [1, 0]" },
  { id: "g_point_full", src: "quadrantChart\nclassDef red color: #ff0000, stroke-color: #00ff00, stroke-width: 2px, radius: 7\n\"P\": [0.4, 0.6]: red" },
  { id: "g_two_points", src: "quadrantChart\na: [0.2, 0.3]\nb: [0.8, 0.9]" },
  { id: "g_xaxis_single", src: "quadrantChart\nx-axis Only" },
  { id: "g_comment", src: "quadrantChart\n%% a comment\nx-axis A --> B" },
  { id: "g_lower_keyword", src: "quadrantchart\nx-axis A --> B" }, // case-insensitive?
  { id: "g_bad_header", src: "quadrnt\nx-axis A" },
  { id: "g_point_oob_high", src: "quadrantChart\na: [1.5, 0.7]" },
  { id: "g_point_oob_neg", src: "quadrantChart\na: [-0.1, 0.7]" },
  { id: "g_point_oob_over1", src: "quadrantChart\na: [0.5, 2]" },
  { id: "g_point_missing_bracket", src: "quadrantChart\na: 0.5, 0.7" },
  { id: "g_bad_classdef", src: "quadrantChart\nclassDef red color: notahex" },
  { id: "g_bad_style_key", src: "quadrantChart\nclassDef red bogus: 5" },
  { id: "g_no_space_header", src: "quadrantChartx" },
  // classDef + ::: class application (the canonical styling path).
  { id: "g_classdef_color", src: "quadrantChart\nclassDef red color: #ff0000\n\"P\":::red: [0.4, 0.6]" },
  { id: "g_classdef_multi", src: "quadrantChart\nclassDef red color: #ff0000, radius: 8, stroke-color: #00ff00, stroke-width: 3px\n\"P\":::red: [0.4, 0.6]" },
  { id: "g_classdef_radius", src: "quadrantChart\nclassDef big radius: 8\n\"P\":::big: [0.4, 0.6]" },
  { id: "g_two_points_classed", src: "quadrantChart\nclassDef a color: #ff0000, radius: 8\nclassDef b color: #0000ff\n\"A\":::a: [0.2, 0.8]\n\"B\":::b: [0.7, 0.2]" },
  // Single-colon class suffix AFTER the bracket is upstream-INVALID (::: is required).
  { id: "g_point_class_single_colon", src: "quadrantChart\n\"P\": [0.4, 0.6]: red" },
  // Multi-class :::a,b is upstream-INVALID (single class only).
  { id: "g_multi_class", src: "quadrantChart\nclassDef a radius: 8\nclassDef b color: #0000ff\n\"P\":::a,b: [0.4, 0.6]" },
  // classDef with the point referencing it via ::: but invalid style value still rejects.
  { id: "g_classdef_bad_hex_applied", src: "quadrantChart\nclassDef bad color: notahex\n\"P\":::bad: [0.4, 0.6]" },
  // Point inline stylesOpt (after ']'): space then comma-separated key:value.
  { id: "g_inline_style", src: "quadrantChart\n\"P\": [0.4, 0.6] radius: 8" },
  { id: "g_inline_multi", src: "quadrantChart\n\"P\": [0.4, 0.6] color: #ff0000, radius: 8, stroke-color: #00ff00, stroke-width: 3px" },
  // Inline overrides classDef.
  { id: "g_inline_over_class", src: "quadrantChart\nclassDef red color: #ff0000\n\"P\":::red: [0.4, 0.6] color: #0000ff" },
  // Duplicate classDef last-wins.
  { id: "g_dup_last_wins", src: "quadrantChart\nclassDef c color: #ff0000\nclassDef c color: #0000ff\n\"P\":::c: [0.4, 0.6]" },
];

// ---- geometry / pixel sources ----
const geomSources = [
  { id: "basic", src: "quadrantChart\ntitle Reach vs Engagement\nx-axis Low --> High\ny-axis Down --> Up\nquadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4" },
  { id: "points", src: "quadrantChart\nx-axis Low --> High\ny-axis Down --> Up\n\"A\": [0.2, 0.9]\n\"B\": [0.8, 0.3]\n\"C\": [0.5, 0.5]" },
  { id: "points-reverse", src: "quadrantChart\nx-axis Low --> High\ny-axis Down --> Up\n\"P1\": [0.1, 0.1]\n\"P2\": [0.9, 0.9]\n\"P3\": [0.2, 0.8]\n\"P4\": [0.8, 0.2]" },
  { id: "empty-axes", src: "quadrantChart\nquadrant-1 First\nquadrant-2 Second\nquadrant-3 Third\nquadrant-4 Fourth" },
  { id: "styled-points", src: "quadrantChart\nclassDef red color: #ff0000, radius: 8, stroke-color: #00ff00, stroke-width: 3px\nclassDef blue color: #0000ff\n\"R\":::red: [0.2, 0.8]\n\"B\":::blue: [0.7, 0.2]" },
  { id: "inline-styled-points", src: "quadrantChart\n\"A\": [0.2, 0.8] color: #ff0000, radius: 8, stroke-color: #00ff00, stroke-width: 3px\n\"B\": [0.7, 0.2] color: #0000ff" },
  // Title precedence: frontmatter-only, both (inline wins), showTitle:false.
  { id: "title-frontmatter", src: "---\ntitle: FromFrontmatter\n---\nquadrantChart\nquadrant-1 Q1" },
  { id: "title-both", src: "---\ntitle: FromFrontmatter\n---\nquadrantChart\ntitle FromInline\nquadrant-1 Q1" },
  { id: "title-showtitle-false", src: "%%{init: {\"quadrantChart\": {\"showTitle\": false}}}%%\nquadrantChart\ntitle Hidden\nquadrant-1 Q1" },
];
const pixelSrc = "quadrantChart\ntitle Reach vs Engagement\nx-axis Low --> High\ny-axis Down --> Up\nquadrant-1 Plan\nquadrant-2 Strategy\nquadrant-3 Hold\nquadrant-4 Harvest\n\"Fast\": [0.8, 0.85]\n\"Slow\": [0.2, 0.15]\n\"Steady\": [0.5, 0.5]";

const browser = await puppeteer.launch({ headless: true, executablePath: chrome, args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;

  // ===== GRAMMAR =====
  const grammar = await page.evaluate(async (cases2, mod) => {
    const { default: mermaid } = await import(mod);
    const out = [];
    for (const c of cases2) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
      try {
        await mermaid.parse(c.src);  // parse() validates without rendering
        out.push({ id: c.id, input: c.src, accept: true });
      } catch (e) {
        out.push({ id: c.id, input: c.src, accept: false, reject: { message: String(e.message || e).split("\n")[0] } });
      }
    }
    return out;
  }, grammarCases, mod);
  // For accepts, render and read the DB-derived structure (quadrant/point/axis counts + text).
  const grammarWithDb = await page.evaluate(async (cases2, mod) => {
    const { default: mermaid } = await import(mod);
    const res = [];
    for (const c of cases2) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
      let accept = true, reject = null, db = null;
      try {
        await mermaid.parse(c.src);
      } catch (e) { accept = false; reject = { message: String(e.message || e).split("\n")[0] }; }
      if (accept) {
        try {
          const { svg } = await mermaid.render(`qg-${c.id}`, c.src);
          const tmp = document.createElement("div"); tmp.innerHTML = svg;
          const txt = (sel) => [...tmp.querySelectorAll(sel)].map((n) => n.textContent);
          db = {
            quadrantTexts: txt("g.quadrant text"),
            pointTexts: txt("g.data-point text"),
            axisTexts: txt("g.label text"),
            title: (tmp.querySelector("g.title text")?.textContent) ?? null,
            quadrantRectCount: tmp.querySelectorAll("g.quadrant rect").length,
            pointCount: tmp.querySelectorAll("g.data-point circle").length,
            borderCount: tmp.querySelectorAll("g.border line").length,
          };
        } catch (e) { reject = { message: "render:" + String(e.message || e).split("\n")[0] }; accept = false; }
      }
      res.push({ id: c.id, input: c.src, accept, ...(reject ? { reject } : {}), ...(db ? { expectedDb: db } : {}) });
    }
    return res;
  }, grammarCases, mod);
  fs.writeFileSync(path.join(outDir, "quadrant-grammar.json"),
    JSON.stringify({ upstream: { package: "mermaid", version: "11.16.0", notes: "quadrantChart jison parser accept/reject + rendered DB-derived structure (quadrant/point/axis text + counts). Point coords ∈ [0,1]." },
      oracle: "quadrantChart parser accept/reject + quadrantDb data", cases: grammarWithDb }, null, 2) + "\n");
  console.log(`grammar: ${grammarWithDb.filter((c) => c.accept).length}/${grammarWithDb.length} accept`);

  // ===== GEOMETRY =====
  const geom = await page.evaluate(async (cases2, mod) => {
    const { default: mermaid } = await import(mod);
    const num = (v) => Math.round(v * 1000) / 1000;
    const res = [];
    for (const c of cases2) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
      const { svg } = await mermaid.render(`qx-${c.id}`, c.src);
      const tmp = document.createElement("div"); tmp.innerHTML = svg;
      const root = tmp.querySelector("svg");
      const pick = (el, attrs) => Object.fromEntries(attrs.map((a) => [a, el.getAttribute(a)]));
      res.push({
        id: c.id, source: c.src,
        expected: {
          viewBox: root.getAttribute("viewBox"),
          quadrants: [...tmp.querySelectorAll("g.quadrant")].map((g) => {
            const r = g.querySelector("rect"); const t = g.querySelector("text");
            return { rect: pick(r, ["x", "y", "width", "height", "fill"]),
                     text: t?.textContent ?? "", transform: t?.getAttribute("transform") ?? "" };
          }),
          points: [...tmp.querySelectorAll("g.data-point")].map((g) => {
            const c2 = g.querySelector("circle"); const t = g.querySelector("text");
            const sw = c2.getAttribute("stroke-width");
            return { cx: num(parseFloat(c2.getAttribute("cx"))), cy: num(parseFloat(c2.getAttribute("cy"))),
                     r: num(parseFloat(c2.getAttribute("r"))), fill: c2.getAttribute("fill"),
                     stroke: c2.getAttribute("stroke"), strokeWidth: sw ? num(parseFloat(sw)) : 0,
                     text: t?.textContent ?? "", transform: t?.getAttribute("transform") ?? "" };
          }),
          borders: [...tmp.querySelectorAll("g.border line")].map((l) => pick(l, ["x1", "y1", "x2", "y2"]).x1 !== null
            ? { x1: num(+l.getAttribute("x1")), y1: num(+l.getAttribute("y1")), x2: num(+l.getAttribute("x2")), y2: num(+l.getAttribute("y2")) } : null),
          axisLabels: [...tmp.querySelectorAll("g.label text")].map((t) => ({ text: t.textContent, transform: t.getAttribute("transform") })),
          title: (tmp.querySelector("g.title text")?.textContent) ?? null,
        },
      });
    }
    return res;
  }, geomSources, mod);
  fs.writeFileSync(path.join(outDir, "quadrant-geometry.json"),
    JSON.stringify({ upstream: { package: "mermaid", version: "11.16.0", notes: "quadrantChart deterministic layout (config constants + d3 scaleLinear [0,1]). Quadrant rects, point cx/cy/r, border lines, title — font-independent geometry (byte-parity target). Point insertion is REVERSE source order (addPoints prepends)." },
      mermaidVersion: "11.16.0", oracle: "quadrantChart.build quadrants+points+borders+title", cases: geom }, null, 2) + "\n");
  console.log(`geometry: ${geom.length} cases`);

  // ===== PIXEL =====
  // Each case's `source` self-declares its theme via %%{init}%%, so the native
  // pipeline (which renders from `source`) produces the same theme as the golden.
  const manifest = { upstream: { version: "11.16.0" }, fontMode: "bundled-noto-2.13b171", cases: [] };
  const pixelCases = [
    { id: "default", theme: "default", src: pixelSrc },
    { id: "dark", theme: "dark", src: '%%{init: {"theme":"dark"}}%%\n' + pixelSrc },
  ];
  for (const pc of pixelCases) {
    const buf = await page.evaluate(async (src, id, mod) => {
      const { default: mermaid } = await import(mod);
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic" });
      const { svg } = await mermaid.render(`qp-${id}`, src);
      const blob = new Blob([svg], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      const img = new Image();
      await new Promise((res, rej) => { img.onload = res; img.onerror = rej; img.src = url; });
      const W = 500, H = 500;
      const canvas = document.createElement("canvas"); canvas.width = W; canvas.height = H;
      const ctx = canvas.getContext("2d"); ctx.drawImage(img, 0, 0, W, H);
      URL.revokeObjectURL(url);
      const dataUrl = canvas.toDataURL("image/png");
      return dataUrl.slice(dataUrl.indexOf(",") + 1);
    }, pc.src, pc.id, mod);
    const file = `${pc.id}.png`;
    fs.writeFileSync(path.join(outDir, "quadrant-pixel", file), Buffer.from(buf, "base64"));
    const bytes = fs.readFileSync(path.join(outDir, "quadrant-pixel", file));
    manifest.cases.push({ id: pc.id, dpr: 1, theme: pc.theme, source: pc.src, file, width: 500, height: 500,
      sha256: createHash("sha256").update(bytes).digest("hex") });
  }
  const manStr = JSON.stringify(manifest);
  manifest.fixtureSha256 = createHash("sha256").update(manStr.replace(/\s+/g, "")).digest("hex");
  fs.writeFileSync(path.join(outDir, "quadrant-pixel", "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  console.log(`pixel: ${manifest.cases.length} cases`);
} finally {
  await browser.close();
}
console.log("done");
