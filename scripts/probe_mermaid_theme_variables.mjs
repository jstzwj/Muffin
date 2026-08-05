// Theme-0 probe (render-based): getThemeVariables() is not public in 11.16.0,
// so capture pie + quadrant themeVariables by rendering a 13-slice pie + a full
// quadrant per theme and reading the actual fill/stroke attributes. Raw JSON:
// tests/fixtures/mermaid/theme-probe.json.
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
const mr = path.resolve("../mermaid-cli/node_modules/mermaid");
const out = path.resolve("tests/fixtures/mermaid/theme-probe.json");
const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mr), "puppeteer/lib/puppeteer/puppeteer.js")));
const browser = await puppeteer.launch({ headless: true, executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe", args: ["--allow-file-access-from-files"] });
const THEMES = ["default","dark","forest","neutral","neo","neo-dark","base","redux","redux-dark","redux-color","redux-dark-color"];
const pieSrc = "pie\n" + Array.from({ length: 13 }, (_, i) => `"S${i + 1}" : ${i + 1}`).join("\n");
const quadSrc = "quadrantChart\ntitle T\nx-axis L --> R\ny-axis B --> T\nquadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4\n\"P\": [0.5, 0.5]";
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;
  const data = await page.evaluate(async ({ THEMES, pieSrc, quadSrc, mod }) => {
    const { default: mermaid } = await import(mod);
    const render_ = async (id, src) => {
      const { svg } = await mermaid.render(id, src);
      const t = document.createElement("div"); t.innerHTML = svg;
      const attr = (el, a) => el?.getAttribute(a) ?? null;
      const styleOf = (el) => { const cs = getComputedStyle(el); return { stroke: cs.stroke, strokeWidth: cs.strokeWidth, opacity: cs.opacity, fill: cs.fill }; };
      return { tmp: t, attr, styleOf };
    };
    const themes = {};
    for (const th of THEMES) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: th, look: "classic" });
      // PIE: 12 distinct slice fills (slot 13 wraps to 0), text fills, stroke/opacity.
      const pie = await mermaid.render("tp" + th, pieSrc);
      const pt = document.createElement("div"); pt.innerHTML = pie.svg;
      const pieFills = [...pt.querySelectorAll("path.pieCircle")].slice(0, 12).map((p) => p.getAttribute("fill"));
      const pieCircleStyle = pt.querySelector("path.pieCircle") ? (() => { const cs = getComputedStyle(pt.querySelector("path.pieCircle")); return { stroke: cs.stroke, strokeWidth: cs.strokeWidth, opacity: cs.opacity }; })() : {};
      const pieTitleFill = pt.querySelector("text.pieTitleText")?.getAttribute("fill") ?? null;
      const pieSliceTextFill = pt.querySelector("text.slice")?.getAttribute("fill") ?? null;
      const pieLegendTextFill = pt.querySelector("g.legend text")?.getAttribute("fill") ?? null;
      const pieOuterStroke = pt.querySelector("circle.pieOuterCircle")?.getAttribute("stroke")
        ?? (() => { const cs = getComputedStyle(pt.querySelector("circle.pieOuterCircle")); return cs.stroke; })();
      // QUADRANT: rect fills, text fills, point fill, border stroke, title fill.
      const quad = await mermaid.render("tq" + th, quadSrc);
      const qt = document.createElement("div"); qt.innerHTML = quad.svg;
      const qRects = [...qt.querySelectorAll("g.quadrant rect")].map((r) => r.getAttribute("fill"));
      const qTextFills = [...qt.querySelectorAll("g.quadrant text")].map((t) => t.getAttribute("fill"));
      const qPointFill = qt.querySelector("g.data-point circle")?.getAttribute("fill") ?? null;
      const qPointText = qt.querySelector("g.data-point text")?.getAttribute("fill") ?? null;
      const qAxisText = qt.querySelector("g.label text")?.getAttribute("fill") ?? null;
      const qBorder = qt.querySelector("g.border line") ? (() => { const cs = getComputedStyle(qt.querySelector("g.border line")); return cs.stroke; })() : null;
      const qTitle = ([...qt.querySelectorAll("text")].find((t) => t.getAttribute("font-weight") === "bold"))?.getAttribute("fill") ?? null;
      themes[th] = {
        pie: { fills: pieFills, titleTextFill: pieTitleFill, sectionTextFill: pieSliceTextFill, legendTextFill: pieLegendTextFill, sliceStroke: pieCircleStyle.stroke, sliceStrokeWidth: pieCircleStyle.strokeWidth, sliceOpacity: pieCircleStyle.opacity, outerStroke: pieOuterStroke },
        quadrant: { fills: qRects, textFills: qTextFills, pointFill: qPointFill, pointTextFill: qPointText, axisTextFill: qAxisText, borderStroke: qBorder, titleFill: qTitle },
      };
    }
    // Custom themeVariables override (default theme): pie1 + quadrant1Fill.
    mermaid.initialize({ startOnLoad: false, securityLevel: "loose", theme: "default", look: "classic", themeVariables: { pie1: "#abcdef", quadrant1Fill: "#112233" } });
    const po = await mermaid.render("tov", pieSrc); const pto = document.createElement("div"); pto.innerHTML = po.svg;
    const qo = await mermaid.render("qov", quadSrc); const qto = document.createElement("div"); qto.innerHTML = qo.svg;
    const overrides = { pie1: pto.querySelector("path.pieCircle")?.getAttribute("fill") ?? null, quadrant1Fill: qto.querySelector("g.quadrant rect")?.getAttribute("fill") ?? null };
    return { upstream: "mermaid 11.16.0", themes, overrides };
  }, { THEMES, pieSrc, quadSrc, mod });
  fs.writeFileSync(out, JSON.stringify(data, null, 2) + "\n");
  for (const t of THEMES) {
    const p = data.themes[t].pie, q = data.themes[t].quadrant;
    console.log(`${t.padEnd(16)} pie: [${(p.fills||[]).slice(0,4).join(",")}...]  quad: [${(q.fills||[]).join(",")}]  pt:${q.pointFill}`);
  }
  console.log("override pie1 (expect #abcdef):", data.overrides.pie1, "| quadrant1Fill (expect #112233):", data.overrides.quadrant1Fill);
} finally { await browser.close(); }
