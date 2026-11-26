import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Captures, from headless Chrome with the bundled Noto fonts, the three widths
// erDiagram asks of one text when `htmlLabels` is false and true:
//
//   html    the inline box of a table-cell div, which erBox lays an HTML label
//           out by;
//   insert  SVGTextElement.getBBox() as erBox calls it, right after createText
//           appended the text: the box the table is laid out by;
//   drawn   the same getBBox() after the layout at scale 1: the box the text
//           is drawn in, which is the one it was laid out by.
//
// The strings cover single glyphs, words, kerned capitals and the other
// bundled scripts. The port measures the same strings in
// tests/mermaid/MermaidErTextOracleTest.cpp.
//
//   node scripts/generate_mermaid_er_text_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/er-text.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const mermaidRoot = path.resolve(
  process.env.MERMAID_REFERENCE_ROOT ??
    process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output =
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "er-text.json");
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js")).href
);

// A lone "y" is left out: as erBox measures it, before the layout, Blink
// reports a box of its own for it (4.57px to the left and 19 high) that no
// rule accounts for; in a word, and once drawn, it is as any other glyph.
// A quoted label is the one way a space reaches an ER text, and the grammar
// takes no comma, dot or colon in an attribute name: each group is used where
// it parses.
const words = [
  "ORDER", "CUSTOMER", "LINE-ITEM", "DELIVERY-ADDRESS", "string", "number", "order number", "date", "placed",
  "status", "total", "decimal", "name", "int", "quantity", "sku", "custNumber", "account_id", "email", "places",
  "contains", "uses", "has", "a", "x", "AV", "VA", "LT", "TO", "Yo", "AW", "WAY", "Tu", "FF", "YE", "PE", "LU",
  "rl", "lo", "Al", "MA", "Noto", "Muffin", "Ily", "WAV", "TT", "rr", "ft", "ry", "kP", "e-mail", "co-operate",
  "AVAWA", "Do", "OD", "LASER", "café", "naïve", "编号", "客户", "订单", "备注", "مرحبا", "שלום", "PK FK",
  "a comment long enough to be noticed", "places orders for customers across regions",
];
const glyphs = [
  "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w",
  "x", "z", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
  "U", "V", "W", "X", "Y", "Z", "编", "客",
];
const strings = [...new Set([...words, ...glyphs])];

const notoDir = path.resolve(import.meta.dirname, "..", "third_party", "noto", "fonts");
const fontFaces = [
  ["Noto Sans", "NotoSans-Regular.ttf"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf"],
]
  .map(
    ([family, file]) =>
      `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}")}`,
  )
  .join("\n");
const fontFamily =
  '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const results = await page.evaluate(
    async ({ strings, mermaidModule, fontFaces, fontFamily }) => {
      const style = document.createElement("style");
      style.textContent = fontFaces;
      document.head.appendChild(style);
      // Chrome loads a face when a text first needs it, and measures that
      // text in the fallback font until then: load each face first.
      for (const [family, sample] of [
        ["Noto Sans", "CUSTOMER"],
        ["Noto Sans CJK SC", "编号"],
        ["Noto Sans Arabic", "مرحبا"],
        ["Noto Sans Hebrew", "שלום"],
      ])
        await document.fonts.load(`16px "${family}"`, sample);
      await document.fonts.ready;
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: "default",
        fontFamily,
        themeVariables: { fontFamily },
      });

      // Every getBBox() on a <text>, with the text's content.
      const native = SVGGraphicsElement.prototype.getBBox;
      let calls = [];
      const record = function (...args) {
        const box = native.apply(this, args);
        if (this.tagName === "text")
          calls.push({ text: this.textContent, x: box.x, y: box.y, width: box.width, height: box.height });
        return box;
      };

      // The inline box of an HTML label: a table cell that never wraps.
      const measure = document.createElement("div");
      measure.style.cssText = `position:absolute;left:0;top:0;font-family:${fontFamily};font-size:16px;display:table-cell;white-space:nowrap;line-height:1.5`;
      document.body.appendChild(measure);
      const htmlWidth = (text) => {
        measure.textContent = text;
        return measure.getBoundingClientRect().width;
      };

      const rows = [];
      for (const text of strings) {
        // The attribute's type carries the text. A space is written as a
        // quoted comment, which is the other text an attribute has.
        const attribute = text.includes(" ") ? `string note "${text}"` : `${text} name`;
        const source = `%%{init: {"htmlLabels": false}}%%\nerDiagram\n    E {\n        ${attribute}\n    }\n    x ||--o{ E : y`;
        calls = [];
        SVGGraphicsElement.prototype.getBBox = record;
        let svg;
        try {
          ({ svg } = await mermaid.render("er-text", source));
        } catch (error) {
          SVGGraphicsElement.prototype.getBBox = native;
          rows.push({ text, error: String(error).slice(0, 160) });
          continue;
        }
        document.getElementById("container").innerHTML = svg;
        // An SVG is scaled by a hair whenever its `max-width` and its viewBox
        // disagree, as Mermaid's rounded one does, and Blink then lays its
        // texts out again at another scale. Grow the viewBox to whole pixels
        // from its origin and size the SVG to it, so it is laid out at exactly
        // scale 1, as a diagram is drawn.
        const root = document.querySelector("svg");
        const [viewX, viewY, viewWidth, viewHeight] = (root.getAttribute("viewBox") ?? "").trim().split(/\s+/).map(Number);
        root.style.maxWidth = "none";
        root.setAttribute("viewBox", `${viewX} ${viewY} ${Math.ceil(viewWidth)} ${Math.ceil(viewHeight)}`);
        root.setAttribute("width", String(Math.ceil(viewWidth)));
        root.setAttribute("height", String(Math.ceil(viewHeight)));
        // createText measures its text once more, in a detached SVG in the
        // page's font: erBox's own measurement is the last one, made in the
        // document.
        const insert = calls.findLast((call) => call.text === text) ?? null;
        SVGGraphicsElement.prototype.getBBox = native;
        const element = [...root.querySelectorAll("g.node text")].find((node) => node.textContent === text);
        if (!element || !insert) {
          rows.push({ text, error: "no SVG text" });
          continue;
        }
        const drawn = element.getBBox();
        rows.push({
          text,
          html: htmlWidth(text),
          insert: { x: insert.x, y: insert.y, width: insert.width, height: insert.height },
          drawn: { x: drawn.x, y: drawn.y, width: drawn.width, height: drawn.height },
        });
      }
      return rows;
    },
    { strings, mermaidModule: pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href, fontFaces, fontFamily }
  );

  const failed = results.filter((row) => row.error);
  if (failed.length > 0) throw new Error(`Mermaid rejected ${failed.length} strings: ${JSON.stringify(failed)}`);
  const root = {
    upstream: {
      package: "mermaid",
      version: "11.16.0",
      notes:
        "Real mermaid 11.16.0 erDiagram text widths, captured in headless Chrome with the bundled Noto fonts " +
        "at 16px: the HTML inline box of a table-cell div, and SVGTextElement.getBBox() as erBox measures it " +
        "(insert) and after the layout (drawn). Compare against er::measureErText and er::measureErSvgTextDrawnBox.",
    },
    mermaidVersion: "11.16.0",
    fontMode: "bundled-noto-2.13b171",
    fontSize: 16,
    oracle: "erDiagram text widths: HTML inline box, SVG text box at insert and after layout",
    cases: results,
  };
  root.fixtureSha256 = createHash("sha256").update(JSON.stringify(root)).digest("hex");
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  console.log(`Wrote ${output} (${results.length} cases)`);
} finally {
  await browser.close();
}
