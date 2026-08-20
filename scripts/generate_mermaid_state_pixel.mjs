import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

const mermaidRoot = path.resolve(process.argv[2] ??
  path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const outDir = path.resolve(process.argv[3] ??
  path.join("tests", "fixtures", "mermaid", "state-pixel"));
const chrome = process.argv[4] ??
  "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (pkg.version !== "11.16.0") throw new Error(`Expected Mermaid 11.16.0, found ${pkg.version}`);

const cases = [
  { id: "transitions", dpr: 1, theme: "default", source:
    "stateDiagram-v2\n[*] --> Idle\nIdle --> Active : start\nActive --> [*]" },
  { id: "descriptions", dpr: 1.25, theme: "default", source:
    "stateDiagram-v2\nstate \"Long state name\" as long_id\nlong_id : first description\nlong_id : second description" },
  { id: "compound", dpr: 1.5, theme: "default", source:
    "stateDiagram-v2\nstate Running {\n  direction LR\n  [*] --> Warmup\n  Warmup --> Ready\n  --\n  Waiting --> Done\n}" },
  { id: "pseudostates-dark", dpr: 2, theme: "dark", source:
    "%%{init: {\"theme\":\"dark\"}}%%\nstateDiagram-v2\nstate fork_state <<fork>>\nstate join_state <<join>>\nstate choice_state <<choice>>\nfork_state --> A\nA --> choice_state\nchoice_state --> B\nB --> join_state" },
  { id: "note", dpr: 1, theme: "default", source:
    "stateDiagram-v2\nActive --> Done\nnote right of Active : Inline note" },
  { id: "neo-look", dpr: 1, theme: "default", look: "neo", source:
    // The look rides the init directive so the native production parse
    // (%%{init}%% config merge) renders the same neo pipeline as the
    // browser's initialize() — the manifest's `look` is documentation.
    // handDrawnSeed freezes the browser's rough.js RNG: neo rough strokes
    // carry random (but collinear) control points, and Skia's cubic AA
    // jitters sub-pixel between renders unless seeded.
    "%%{init: {\"look\":\"neo\",\"handDrawnSeed\":42}}%%\nstateDiagram-v2\n[*] --> Idle\nIdle --> Active : start\nstate Running {\n  Idle2 --> Ready\n}\nActive --> Running\nRunning --> [*]" },
  { id: "neo-pseudostates", dpr: 1, theme: "default", look: "neo", source:
    "%%{init: {\"look\":\"neo\",\"handDrawnSeed\":42}}%%\nstateDiagram-v2\nstate fork_state <<fork>>\nstate join_state <<join>>\nstate choice_state <<choice>>\nfork_state --> A\nA --> choice_state\nchoice_state --> B\nB --> join_state\njoin_state --> [*]" },
  // Title band: the production PNG composites the title strip above the
  // content — the browser client box grows by 25 + font ascent + 8 (52 for
  // the pinned Noto 18px title) and widens for the title box.
  { id: "titled", dpr: 1, theme: "default", title: true, source:
    "---\ntitle: Some Title\n---\nstateDiagram-v2\nA --> B" },
  // redux-dark neo: the url(#drop-shadow) form reference resolves to a
  // flat feDropShadow (dx4 dy4 stdDeviation 0, flood white 6%) — the
  // synthesized flat drop-shadow must paint it.
  { id: "redux-dark-neo", dpr: 1, theme: "redux-dark", look: "neo", source:
    "%%{init: {\"theme\":\"redux-dark\",\"look\":\"neo\",\"handDrawnSeed\":42}}%%\nstateDiagram-v2\n[*] --> Idle\nIdle --> Active : start\nActive --> [*]" },
  // neo + visibility:hidden: the drop-shadow filter input is the element's
  // own rendering — a hidden rect paints nothing AND casts no shadow.
  { id: "neo-shadow-off", dpr: 1, theme: "default", look: "neo", source:
    `%%{init: ${JSON.stringify({ look: "neo", handDrawnSeed: 42,
      themeCSS: ".node rect { visibility: hidden; }" })}}%%\n` +
    "stateDiagram-v2\n[*] --> Idle\nIdle --> Active : start\nActive --> [*]" },
];

const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [
  ["Noto Sans", "NotoSans-Regular.ttf"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf"],
];
const faces = fonts.map(([family, file]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}")}`
).join("\n");
const fontFamily = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';

function canonicalPng(bytes) {
  const image = PNG.sync.read(bytes);
  for (let offset = 0; offset < image.data.length; offset += 4)
    if (image.data[offset + 3] === 0) image.data.fill(0, offset, offset + 3);
  return PNG.sync.write(image, { colorType: 6, inputColorType: 6, bitDepth: 8 });
}

const { default: puppeteer } = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
fs.mkdirSync(outDir, { recursive: true });
const browser = await puppeteer.launch({ executablePath: chrome, headless: true,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--disable-lcd-text",
    "--font-render-hinting=none"] });
try {
  const manifestCases = [];
  for (const fixture of cases) {
    const page = await browser.newPage();
    await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: fixture.dpr });
    await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
    const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
    await page.evaluate(async ({ mermaidModule, faces, fontFamily, fixture }) => {
      const { default: mermaid } = await import(mermaidModule);
      const style = document.createElement("style");
      style.textContent = faces;
      document.head.appendChild(style);
      await document.fonts.load('16px "Noto Sans"', "State");
      await document.fonts.ready;
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict",
        theme: fixture.theme, fontFamily, themeVariables: { fontFamily },
        look: fixture.look ?? "classic", state: { padding: 8 } });
      const { svg } = await mermaid.render(`state-pixel-${fixture.id}`, fixture.source);
      document.body.style.margin = "0";
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    }, { mermaidModule, faces, fontFamily, fixture });
    const dimensions = await page.$eval("svg", (svg) => ({
      cssWidth: svg.getBoundingClientRect().width,
      cssHeight: svg.getBoundingClientRect().height,
      viewBox: svg.getAttribute("viewBox") ?? "",
    }));
    const element = await page.$("svg");
    const png = canonicalPng(await element.screenshot({ omitBackground: true }));
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(outDir, file), png);
    manifestCases.push({ ...fixture, file, ...dimensions,
      width: PNG.sync.read(png).width, height: PNG.sync.read(png).height,
      sha256: createHash("sha256").update(png).digest("hex") });
    await page.close();
  }
  const manifest = { upstream: { version: pkg.version },
    fontMode: "bundled-noto-2.13b171", cases: manifestCases };
  manifest.fixtureSha256 = createHash("sha256")
    .update(JSON.stringify(manifest)).digest("hex");
  fs.writeFileSync(path.join(outDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`Wrote ${manifestCases.length} state pixel cases to ${outDir}`);
  console.log(`fixtureSha256=${manifest.fixtureSha256}`);
} finally {
  await browser.close();
}
