import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { cases } from "./mermaid_golden_cases.mjs";

// Master Level-3 pixel golden generator (milestone G3). Renders every case from
// the shared registry (mermaid_golden_cases.mjs) in Chrome at the SVG viewBox and
// writes one PNG per case + a manifest. The native test (MermaidGoldenPixelTest)
// builds the same scene and runs FlowSceneCompare against each PNG.
//
// `look: "classic"` is forced for every case so the theme axis is a pure COLOUR
// test (each theme's colours under the classic look the native painter renders).
// The neo/handDrawn LOOK axis is deferred to F-polish; HTML/CJK/bidi labels and
// 2x DPR are deferred to F4. Adding those is a registry edit, not a code change.

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const outDir = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "golden-pixel"));
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

const browser = await puppeteer.launch({ headless: true, executablePath: chrome, args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const results = await page.evaluate(async ({ cases, mermaidModule }) => {
    const { default: mermaid } = await import(mermaidModule);
    const renderOne = async (fixture, index) => {
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: fixture.theme,
        look: "classic",
        fontFamily: "Arial",
        flowchart: { defaultRenderer: "dagre-wrapper", htmlLabels: false },
      });
      const { svg } = await mermaid.render(`gp-${index}`, fixture.source);
      document.getElementById("container").innerHTML = svg;
      const root = document.querySelector("svg");
      // Rasterize at the TIGHT content bbox (root.getBBox), not mermaid's padded
      // viewBox — this strips mermaid's arbitrary origin + padding so the golden
      // and the native render (first-node-relative) can be aligned by their actual
      // painted content (see FlowSceneCompare).
      const bb = root.getBBox();
      const clone = root.cloneNode(true);
      clone.setAttribute("viewBox", `${bb.x} ${bb.y} ${bb.width} ${bb.height}`);
      clone.setAttribute("width", bb.width);
      clone.setAttribute("height", bb.height);
      const img = new Image();
      await new Promise((res, rej) => { img.onload = res; img.onerror = rej; img.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(clone.outerHTML)}`; });
      const canvas = document.createElement("canvas");
      canvas.width = Math.ceil(bb.width);
      canvas.height = Math.ceil(bb.height);
      canvas.getContext("2d").drawImage(img, 0, 0, bb.width, bb.height);
      return { content: { width: bb.width, height: bb.height }, png: canvas.toDataURL("image/png").split(",")[1] };
    };
    const out = [];
    for (let i = 0; i < cases.length; ++i) {
      const r = await renderOne(cases[i], i);
      out.push({ id: cases[i].id, theme: cases[i].theme, source: cases[i].source, viewBox: r.viewBox, png: r.png });
    }
    return out;
  }, { cases, mermaidModule });

  fs.mkdirSync(outDir, { recursive: true });
  const manifestCases = [];
  for (const r of results) {
    const file = `${r.id}.png`;
    fs.writeFileSync(path.join(outDir, file), Buffer.from(r.png, "base64"));
    // Neo/redux themes have a known F1 colour-derivation gap (redux-color
    // borderColorArray / cScale + neo-look fills under classic look not yet
    // ported). They are kept in the matrix but their INTERIOR check is not
    // enforced until F1 completes; boundary/text/empty still are.
    const enforceInterior = !(r.theme.includes("neo") || r.theme.includes("redux"));
    manifestCases.push({ id: r.id, theme: r.theme, source: r.source, content: r.content, enforceInterior, file });
  }
  const manifest = { upstream: { package: "mermaid", version: packageJson.version }, cases: manifestCases };
  fs.writeFileSync(path.join(outDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`Wrote ${results.length} golden PNGs + manifest to ${outDir}`);
} finally {
  await browser.close();
}
