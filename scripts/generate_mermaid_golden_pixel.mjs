import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";
import { cases } from "./mermaid_golden_cases.mjs";

// Master Level-3 pixel golden generator (milestone G3). Renders every case from
// the shared registry (mermaid_golden_cases.mjs) in Chrome at the SVG viewBox and
// writes one PNG per case + a manifest. The native test (MermaidGoldenPixelTest)
// builds the same scene and runs FlowSceneCompare against each PNG.
//
// `look` defaults to classic so the theme axis remains a pure colour test.
// Dedicated cases opt into neo and exercise its independent geometry axis.
// Animated cases declare a deterministic sampled state in the registry. The
// handDrawn look remains a separate RoughJS milestone.

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const outDir = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "golden-pixel"));
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
const notoDir = path.resolve("third_party", "noto", "fonts");
const fontFiles = [
  ["Noto Sans", "NotoSans-Regular.ttf", "U+0000-024F,U+1E00-1EFF"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf", "U+2E80-9FFF,U+3040-30FF,U+AC00-D7AF"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf", "U+0600-06FF,U+0750-077F,U+08A0-08FF"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf", "U+0590-05FF"],
];
for (const [, file] of fontFiles) {
  if (!fs.existsSync(path.join(notoDir, file))) throw new Error(`Missing bundled Noto font: ${file}`);
}
const fixedFontStack = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';
const fontFaces = fontFiles.map(([family, file, range]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}");font-weight:400;font-style:normal;unicode-range:${range};}`
).join("\n");
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  const harnessUrl = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const results = [];
  for (let index = 0; index < cases.length; ++index) {
    const fixture = cases[index];
    const dpr = fixture.dpr ?? 1;
    await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: dpr });
    await page.goto(harnessUrl);
    const content = await page.evaluate(async ({ fixture, index, mermaidModule, fontFaces, fixedFontStack }) => {
      if (fixture.fontMode === "noto") {
        const style = document.createElement("style");
        style.textContent = fontFaces;
        document.head.appendChild(style);
        await Promise.all([
          document.fonts.load('16px "Noto Sans"', "Fixed Noto"),
          document.fonts.load('16px "Noto Sans CJK SC"', "中文日本語"),
          document.fonts.load('16px "Noto Sans Arabic"', "مرحبا بالعالم"),
          document.fonts.load('16px "Noto Sans Hebrew"', "שלום עולם"),
        ]);
        await document.fonts.ready;
      }
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: fixture.theme,
        look: fixture.look ?? "classic",
        handDrawnSeed: fixture.handDrawnSeed ?? 17,
        fontFamily: fixture.fontMode === "noto" ? fixedFontStack : "Arial",
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
      const nodeBoxes = (fixture.id.startsWith("look-neo-shapes-") ||
                         fixture.id.startsWith("look-neo-dark-shapes-"))
        ? [...root.querySelectorAll("g.node")].map((node) => {
            const box = node.getBBox();
            return { width: box.width, height: box.height };
          })
        : undefined;
      const mathBox = fixture.mathCrop ? (() => {
        const math = root.querySelector("g.node math");
        if (!math) throw new Error(`Case ${fixture.id}: Math label is missing`);
        const box = math.getBoundingClientRect();
        const elements = [...math.querySelectorAll("mtable,mtr,mtd,mi,mo,mn")]
          .map((element) => {
            const rect = element.getBoundingClientRect();
            return { tag: element.localName, width: rect.width,
                     height: rect.height };
          });
        return { width: box.width, height: box.height, elements };
      })() : undefined;
      const computedStyles = fixture.id.startsWith("look-redux") ? (() => {
        const nodePath = root.querySelector("g.node path, g.node rect, g.node polygon");
        const clusterRect = root.querySelector("g.cluster rect");
        const nodeStyle = nodePath ? getComputedStyle(nodePath) : null;
        const clusterStyle = clusterRect ? getComputedStyle(clusterRect) : null;
        return {
          nodeFill: nodeStyle?.fill ?? "",
          nodeStroke: nodeStyle?.stroke ?? "",
          nodeFillOpacity: nodeStyle?.fillOpacity ?? "",
          nodeOpacity: nodeStyle?.opacity ?? "",
          nodeFilter: nodeStyle?.filter ?? "",
          clusterFill: clusterStyle?.fill ?? "",
          clusterStroke: clusterStyle?.stroke ?? "",
          clusterFillOpacity: clusterStyle?.fillOpacity ?? "",
          clusterOpacity: clusterStyle?.opacity ?? "",
          clusterFilter: clusterStyle?.filter ?? "",
        };
      })() : undefined;
      return { width: bb.width, height: bb.height,
               ...(nodeBoxes ? { nodeBoxes } : {}),
               ...(mathBox ? { mathBox } : {}),
               ...(computedStyles ? { computedStyles } : {}) };
    }, { fixture, index, mermaidModule, fontFaces, fixedFontStack });
    const element = await page.$("#container svg");
    if (!element) throw new Error(`Case ${fixture.id}: rendered SVG is missing`);
    const screenshot = await element.screenshot({ omitBackground: true });
    const png = PNG.sync.write(PNG.sync.read(screenshot), {
      colorType: 6,
      inputColorType: 6,
      bitDepth: 8,
    });
    let mathCropPng;
    if (fixture.mathCrop) {
      const mathClip = await page.$eval("g.node math", (node) => {
        const rect = node.getBoundingClientRect();
        const guard = 2;
        return { x: rect.left - guard, y: rect.top - guard,
                 width: Math.max(1, rect.width + 2 * guard),
                 height: Math.max(1, rect.height + 2 * guard) };
      });
      await page.evaluate(() => {
        const root = document.querySelector("svg");
        for (const node of root.querySelectorAll("*"))
          node.style.visibility = "hidden";
        const selected = root.querySelector("g.node math");
        for (let node = selected; node && node !== root; node = node.parentElement)
          node.style.visibility = "visible";
        for (const child of selected.querySelectorAll("*"))
          child.style.visibility = "visible";
      });
      const cropScreenshot = await page.screenshot({
        omitBackground: true,
        clip: mathClip,
      });
      mathCropPng = PNG.sync.write(PNG.sync.read(cropScreenshot), {
        colorType: 6,
        inputColorType: 6,
        bitDepth: 8,
      });
    }
    results.push({ ...fixture, dpr, content, png, mathCropPng });
  }

  fs.mkdirSync(outDir, { recursive: true });
  const manifestCases = [];
  for (const r of results) {
    const file = `${r.id}.png`;
    fs.writeFileSync(path.join(outDir, file), r.png);
    const mathCropFile = r.mathCropPng ? `${r.id}-label.png` : undefined;
    if (mathCropFile)
      fs.writeFileSync(path.join(outDir, mathCropFile), r.mathCropPng);
    const enforceInterior = r.enforceInterior ?? true;
    manifestCases.push({ id: r.id, theme: r.theme, look: r.look ?? "classic",
                         fontMode: r.fontMode ?? "system",
                         source: r.source, dpr: r.dpr,
                         ...(r.handDrawnSeed !== undefined
                           ? { handDrawnSeed: r.handDrawnSeed } : {}),
                         ...(r.animationState ? { animationState: r.animationState } : {}),
                         ...(r.textGlyphIou !== undefined ? { textGlyphIou: r.textGlyphIou } : {}),
                         ...(r.emptyMaxMismatchRatio !== undefined
                           ? { emptyMaxMismatchRatio: r.emptyMaxMismatchRatio } : {}),
                         ...(mathCropFile
                           ? { mathCropKind: r.mathCropKind, mathCropFile,
                               mathCropSha256: createHash("sha256")
                                 .update(r.mathCropPng).digest("hex") }
                           : {}),
                         content: r.content, enforceInterior, file });
  }
  const manifest = {
    upstream: { package: "mermaid", version: packageJson.version },
    font: { family: fixedFontStack, source: "third_party/noto", mode: "bundled" },
    cases: manifestCases,
  };
  fs.writeFileSync(path.join(outDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`Wrote ${results.length} golden PNGs + manifest to ${outDir}`);
} finally {
  await browser.close();
}
