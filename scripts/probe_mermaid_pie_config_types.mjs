import path from "node:path";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const chrome =
  process.argv[3] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = await import(pathToFileURL(path.join(mermaidRoot, "package.json")).href, {
  with: { type: "json" },
});
if (pkg.default.version !== "11.16.0")
  throw new Error(`Expected Mermaid 11.16.0, found ${pkg.default.version}`);

const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"),
  ).href
);
const browser = await puppeteer.launch({
  executablePath: chrome,
  headless: true,
  args: ["--allow-file-access-from-files", "--disable-gpu"],
});

const cases = [
  ["baseline", {}],
  ["text-number", { textPosition: 0.5 }],
  ["text-string", { textPosition: "0.5" }],
  ["text-false", { textPosition: false }],
  ["text-null", { textPosition: null }],
  ["donut-number", { donutHole: 0.5 }],
  ["donut-string", { donutHole: "0.5" }],
  ["donut-true", { donutHole: true }],
  ["donut-null", { donutHole: null }],
  ["legend-null", { legendPosition: null }],
  ["legend-number", { legendPosition: 0 }],
  ["highlight-null", { highlightSlice: null }],
  ["highlight-zero", { highlightSlice: 0 }],
  ["highlight-empty", { highlightSlice: "" }],
  ["max-false", { useMaxWidth: false }],
  ["max-zero", { useMaxWidth: 0 }],
  ["max-empty", { useMaxWidth: "" }],
  ["max-string-false", { useMaxWidth: "false" }],
  ["max-null", { useMaxWidth: null }],
];

try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const results = await page.evaluate(async ({ mermaidModule, cases }) => {
    const { default: mermaid } = await import(mermaidModule);
    const out = [];
    for (let i = 0; i < cases.length; ++i) {
      const [name, pie] = cases[i];
      const source = `%%{init: ${JSON.stringify({ pie })}}%%\npie title T\n"" : 50\n"B" : 50`;
      try {
        mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
        const { svg } = await mermaid.render(`pie-config-type-${i}`, source);
        const host = document.createElement("div");
        host.innerHTML = svg;
        document.body.replaceChildren(host);
        const root = host.querySelector("svg");
        const paths = [...root.querySelectorAll("path.pieCircle")];
        out.push({
          name,
          error: null,
          viewBox: root.getAttribute("viewBox"),
          width: root.getAttribute("width"),
          maxWidth: root.style.maxWidth,
          firstPath: paths[0]?.getAttribute("d") ?? null,
          firstClass: paths[0]?.getAttribute("class") ?? null,
          firstLabelTransform: root.querySelector("text.slice")?.getAttribute("transform") ?? null,
          firstLegendTransform: root.querySelector("g.legend")?.getAttribute("transform") ?? null,
        });
      } catch (error) {
        out.push({ name, error: String(error) });
      }
    }
    return out;
  }, { mermaidModule, cases });
  console.log(JSON.stringify(results, null, 2));
} finally {
  await browser.close();
}
