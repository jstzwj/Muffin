import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

const mermaidRoot = path.resolve(process.argv[2] ??
  path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const outDir = path.resolve(process.argv[3] ??
  path.join("tests", "fixtures", "mermaid", "requirement-pixel"));
const chrome = process.argv[4] ??
  "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (pkg.version !== "11.16.0") throw new Error(`Expected Mermaid 11.16.0, found ${pkg.version}`);

// One default-theme case exercising requirement boxes (type/name/body/divider),
// an element, and both relationship markers (contains = solid + start marker;
// copies = dashed + end marker).
const cases = [
  { id: "default", dpr: 1, theme: "default", source:
    "requirementDiagram\n" +
    "requirement TheSystem {\n  id: \"REQ-001\"\n  text: shall do the thing\n  risk: high\n  verifyMethod: test\n}\n" +
    "functionalRequirement SubFunc {\n  id: \"REQ-002\"\n  text: sub function\n  risk: medium\n  verifyMethod: analysis\n}\n" +
    "element HardwareModule {\n  type: Hardware\n  docref: \"DOC-1\"\n}\n" +
    "TheSystem -contains-> SubFunc\nSubFunc -copies-> HardwareModule" },
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
      await document.fonts.load('16px "Noto Sans"', "Requirement");
      await document.fonts.ready;
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict",
        theme: fixture.theme, fontFamily, themeVariables: { fontFamily }, look: "classic" });
      const { svg } = await mermaid.render(`requirement-pixel-${fixture.id}`, fixture.source);
      document.body.style.margin = "0";
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    }, { mermaidModule, faces, fontFamily, fixture });
    const element = await page.$("svg");
    const png = canonicalPng(await element.screenshot({ omitBackground: true }));
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(outDir, file), png);
    manifestCases.push({ ...fixture, file,
      width: PNG.sync.read(png).width, height: PNG.sync.read(png).height,
      sha256: createHash("sha256").update(png).digest("hex") });
    await page.close();
  }
  const manifest = { upstream: { version: pkg.version },
    fontMode: "bundled-noto-2.13b171", cases: manifestCases };
  manifest.fixtureSha256 = createHash("sha256")
    .update(JSON.stringify(manifest)).digest("hex");
  fs.writeFileSync(path.join(outDir, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`Wrote ${manifestCases.length} requirement pixel cases to ${outDir}`);
  console.log(`fixtureSha256=${manifest.fixtureSha256}`);
} finally {
  await browser.close();
}
