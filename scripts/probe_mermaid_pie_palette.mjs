// One-shot probe: dump mermaid 11.16.0 default + dark theme pie1..pie12 fill
// colors by rendering a 13-slice pie (so all 12 palette slots are exercised and
// slot 13 wraps to slot 0) in each theme and reading the path fill attributes.
// Reuses the same headless-Chrome path as generate_mermaid_pie_geometry_fixture.mjs.
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const chrome =
  process.argv[3] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

const source = "pie\n" + Array.from({ length: 13 }, (_, i) => `"S${i + 1}" : ${i + 1}`).join("\n");

const browser = await puppeteer.launch({ headless: true, executablePath: chrome, args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const result = await page.evaluate(async ({ src, mod }) => {
    const { default: mermaid } = await import(mod);
    const out = {};
    for (const theme of ["default", "dark"]) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict", theme, fontFamily: '"trebuchet ms", verdana, arial, sans-serif', look: "classic" });
      const { svg } = await mermaid.render(`pie-pal-${theme}`, src);
      const tmp = document.createElement("div");
      tmp.innerHTML = svg;
      // First 12 distinct fills (slot 13 wraps to slot 0 — skip the duplicate).
      const fills = [...tmp.querySelectorAll("path.pieCircle")].map((p) => p.getAttribute("fill"));
      out[theme] = fills.slice(0, 12);
    }
    return out;
  }, { src: source, mod: mermaidModule });
  console.log(JSON.stringify(result, null, 2));
} finally {
  await browser.close();
}
