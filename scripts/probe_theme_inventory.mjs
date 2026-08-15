// Dev-only probe (Gate D recon): end/start/fork special shapes across all 11
// themes, classic look — fill/stroke attrs + computed, to build the exact
// specialStateColor/innerEndBackground consumption table.
import path from "node:path";
import { pathToFileURL } from "node:url";

const mr = path.resolve("../mermaid-cli/node_modules/mermaid");
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mr), "puppeteer/lib/puppeteer/puppeteer.js")));
const browser = await puppeteer.launch({
  headless: true,
  executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe",
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mr), "..", "index.html")).href);
  const mod = pathToFileURL(path.join(mr, "dist/mermaid.esm.mjs")).href;
  const out = await page.evaluate(async ({ mod }) => {
    const { default: mermaid } = await import(mod);
    const themes = ["base", "dark", "default", "forest", "neutral", "neo",
      "neo-dark", "redux", "redux-dark", "redux-color", "redux-dark-color"];
    const src = "stateDiagram-v2\n[*] --> A\nA --> [*]";
    const results = {};
    for (const theme of themes) {
      mermaid.initialize({
        startOnLoad: false, securityLevel: "loose", theme, look: "classic",
      });
      const { svg } = await mermaid.render(`sp-${theme}`, src);
      const container = document.createElement("div");
      container.innerHTML = svg;
      document.body.appendChild(container);
      const startCircle = container.querySelector("circle.state-start");
      const endNode = [...container.querySelectorAll("g.node")].find((n) =>
        (n.getAttribute("id") || "").includes("end"));
      const paths = endNode ? [...endNode.querySelectorAll("path")] : [];
      results[theme] = {
        start: startCircle
          ? {
              fill: getComputedStyle(startCircle).fill,
              stroke: getComputedStyle(startCircle).stroke,
            }
          : null,
        endPaths: paths.map((p) => ({
          fill: p.getAttribute("fill"),
          stroke: p.getAttribute("stroke"),
          sw: p.getAttribute("stroke-width"),
        })),
      };
      container.remove();
    }
    return results;
  }, { mod });
  console.log(JSON.stringify(out, null, 1));
} finally {
  await browser.close();
}
