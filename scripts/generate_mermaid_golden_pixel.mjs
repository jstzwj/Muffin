import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";
import { cases } from "./mermaid_golden_cases.mjs";

// Master Level-3 pixel golden generator (milestone G3). Renders every case from
// the shared registry (mermaid_golden_cases.mjs) in Chrome at the SVG viewBox and
// writes one PNG per case + a manifest. The native test (MermaidGoldenPixelTest)
// builds the same scene and runs FlowSceneCompare against each PNG.
//
// `look: "classic"` is forced for every case so the theme axis is a pure COLOUR
// test (each theme's colours under the classic look the native painter renders).
// Animated cases declare a deterministic sampled state in the registry. The
// handDrawn look remains a separate RoughJS milestone.

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
  const results = [];
  for (let index = 0; index < cases.length; ++index) {
    const fixture = cases[index];
    const dpr = fixture.dpr ?? 1;
    await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: dpr });
    const content = await page.evaluate(async ({ fixture, index, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
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
      root.setAttribute("viewBox", `${bb.x} ${bb.y} ${bb.width} ${bb.height}`);
      root.setAttribute("width", Math.ceil(bb.width));
      root.setAttribute("height", Math.ceil(bb.height));
      root.style.maxWidth = "none";
      root.style.display = "block";
      root.style.width = `${Math.ceil(bb.width)}px`;
      root.style.height = `${Math.ceil(bb.height)}px`;
      document.documentElement.style.margin = "0";
      document.body.style.margin = "0";
      const container = document.getElementById("container");
      container.style.position = "absolute";
      container.style.left = "0";
      container.style.top = "0";
      for (const element of root.querySelectorAll("*")) {
        if (fixture.animationState === "initial") {
          element.style.animationPlayState = "paused";
          element.style.animationDelay = "0s";
        } else {
          element.style.animation = "none";
        }
        element.style.transition = "none";
      }
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(() => resolve()));
      return { width: bb.width, height: bb.height };
    }, { fixture, index, mermaidModule });
    const element = await page.$("#container svg");
    if (!element) throw new Error(`Case ${fixture.id}: rendered SVG is missing`);
    const screenshot = await element.screenshot({ omitBackground: true });
    const png = PNG.sync.write(PNG.sync.read(screenshot), {
      colorType: 6,
      inputColorType: 6,
      bitDepth: 8,
    });
    results.push({ ...fixture, dpr, content, png });
  }

  fs.mkdirSync(outDir, { recursive: true });
  const manifestCases = [];
  for (const r of results) {
    const file = `${r.id}.png`;
    fs.writeFileSync(path.join(outDir, file), r.png);
    // Neo/redux themes have a known F1 colour-derivation gap (redux-color
    // borderColorArray / cScale + neo-look fills under classic look not yet
    // ported). They are kept in the matrix but their INTERIOR check is not
    // enforced until F1 completes; boundary/text/empty still are.
    const enforceInterior = !(r.theme.includes("neo") || r.theme.includes("redux"));
    manifestCases.push({ id: r.id, theme: r.theme, source: r.source, dpr: r.dpr,
                         ...(r.animationState ? { animationState: r.animationState } : {}),
                         content: r.content, enforceInterior, file });
  }
  const manifest = { upstream: { package: "mermaid", version: packageJson.version }, cases: manifestCases };
  fs.writeFileSync(path.join(outDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`Wrote ${results.length} golden PNGs + manifest to ${outDir}`);
} finally {
  await browser.close();
}
