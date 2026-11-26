import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Captures every call erDiagram makes to rough.js under `look: "handDrawn"`
// against real mermaid 11.16.0 in headless Chrome: the method, its arguments,
// the resolved options, and the op sets it returns with their full path data.
// tests/fixtures/mermaid/er-rough.json is what a C++ test feeds the port's
// rough engine, number for number.
//
// The seed is pinned to 17 in every case: Mermaid's default is 0, which makes
// rough.js draw from Math.random(), so no two upstream renders agree and there
// is nothing to compare against.
//
//   node scripts/generate_mermaid_er_rough_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/er-rough.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const mermaidRoot = path.resolve(
  process.env.MERMAID_REFERENCE_ROOT ??
    process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "er-rough.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js")).href
);

const er = (lines) => `erDiagram\n${lines.join("\n")}`;
const block = (name, lines) => `${name} {\n${lines.join("\n")}\n}`;
const rel = (a, card, b, label) => `${a} ${card} ${b} : ${label}`;

// The structures of the family whose shapes and paths differ: rectangles with
// and without rows, the polygons of dividers, straight and curved paths, the
// three paths of a loop, a style that reaches the rough options, SVG texts.
const sources = {
  tables: er([
    block("CUSTOMER", ["string name", "string custNumber", "string sector"]),
    rel("CUSTOMER", "||--o{", "ORDER", "places"),
    "ORDER",
    rel("ORDER", "||--|{", "LINE-ITEM", "contains"),
    "LINE-ITEM",
  ]),
  "keys-comments": er([
    block("CUSTOMER", [
      'string name PK "primary name key"',
      "int age",
      "bigint account_id FK",
      'string email UK "unique email"',
    ]),
  ]),
  attributeless: er([rel("PERSON", "||--o{", "ORDER", "places"), "DELIVERY-ADDRESS"]),
  "non-identifying": er([rel("CUSTOMER", "}o..o|", "COUPON", "redeems"), "COUPON"]),
  "self-relationship": er([
    block("EMPLOYEE", ["string id PK", "string manager_id FK"]),
    "EMPLOYEE |o--o{ EMPLOYEE : manages",
  ]),
  styled: er([
    block("ORDER", ["string number PK", "date placed"]),
    'classDef d stroke-dasharray: 5 5,fill:#ff0',
    "class ORDER d",
    "style ORDER stroke-width:4",
  ]),
  // SVG texts: the rows are drawn from the boxes the texts report, here
  // with a column of every kind, and with keys and comments.
  "svg-text": er([
    block("ORDER", ['string number PK,FK "order number"', "date placed", "string status", "decimal total"]),
    block("CUSTOMER", ["string name PK"]),
    rel("CUSTOMER", "||--o{", "ORDER", "places"),
    block("LINE-ITEM", ["int quantity", "string sku PK"]),
    rel("ORDER", "|o--|{", "LINE-ITEM", "contains"),
  ]),
  "direction-lr": er([
    "direction LR",
    rel("A", "||--o{", "B", "r1"),
    "B",
    rel("B", "||--o{", "C", "r2"),
    block("A", ["int id"]),
    block("C", ["int id", "string name"]),
  ]),
};

// What a case configures beyond the look and the seed.
const inits = { "svg-text": { htmlLabels: false } };

const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [
  ["Noto Sans", "NotoSans-Regular.ttf"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf"],
];
const faces = fonts
  .map(
    ([family, file]) =>
      `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}")}`,
  )
  .join("\n");
const fontFamily =
  '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';

// The chunk that holds rough.js: the one with its seeded random generator,
// imported by the URL mermaid imports it by, so that it is the same module
// instance and its `svg` renderer can be wrapped.
const chunksDir = path.join(mermaidRoot, "dist", "chunks", "mermaid.esm");
const isRoughChunk = (file) =>
  file.endsWith(".mjs") && /imul\(48271/.test(fs.readFileSync(path.join(chunksDir, file), "utf8"));
const roughChunk = fs.readdirSync(chunksDir).find(isRoughChunk);
if (!roughChunk) throw new Error("rough.js chunk not found");

const browser = await puppeteer.launch({
  executablePath: chrome,
  headless: true,
  args: ["--allow-file-access-from-files", "--disable-gpu"],
});
try {
  const cases = await Promise.all(
    Object.entries(sources).map(async ([id, source]) => {
      const page = await browser.newPage();
      try {
        await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
        await page.goto(
          pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href,
        );
        return await page.evaluate(
          async ({ mermaidModule, roughUrl, faces, fontFamily, id, source, init }) => {
            const style = document.createElement("style");
            style.textContent = faces;
            document.head.appendChild(style);
            for (const [family, sample] of [
              ["Noto Sans", "CUSTOMER"],
              ["Noto Sans CJK SC", "编号"],
              ["Noto Sans Arabic", "مرحبا"],
              ["Noto Sans Hebrew", "שלום"],
            ])
              await document.fonts.load(`16px "${family}"`, sample);
            await document.fonts.ready;
            const { default: mermaid } = await import(mermaidModule);
            // Wrap the svg renderer: every call is recorded with the Drawable
            // its generator returned, as the path data rough.js writes.
            const rough = await import(roughUrl);
            const original = rough.at.svg;
            const log = [];
            rough.at.svg = function (svg, config) {
              const rc = original.call(rough.at, svg, config);
              const element = svg && typeof svg.node === "function" ? svg.node() : svg;
              const ownerId = element && element.getAttribute ? element.getAttribute("id") : null;
              let last = null;
              rc.gen = new Proxy(rc.gen, {
                get(gen, prop) {
                  const value = Reflect.get(gen, prop, gen);
                  if (typeof value !== "function") return value;
                  return function (...args) {
                    const drawable = value.apply(gen, args);
                    if (drawable && typeof drawable === "object" && "sets" in drawable) last = drawable;
                    return drawable;
                  };
                },
              });
              return new Proxy(rc, {
                get(target, prop) {
                  const value = Reflect.get(target, prop, target);
                  if (typeof value !== "function") return value;
                  return function (...args) {
                    const result = value.apply(target, args);
                    const { randomizer, ...options } = last.options;
                    log.push({
                      owner: ownerId,
                      method: String(prop),
                      args: args.filter((a) => typeof a !== "object" || a === null || Array.isArray(a)),
                      options: JSON.parse(JSON.stringify(options)),
                      sets: last.sets.map((set) => ({ type: set.type, d: rc.opsToPath(set) })),
                    });
                    return result;
                  };
                },
              });
            };
            mermaid.initialize({
              startOnLoad: false,
              securityLevel: "strict",
              theme: "default",
              fontFamily,
              themeVariables: { fontFamily },
              look: "handDrawn",
              handDrawnSeed: 17,
              ...init,
            });
            await mermaid.render(`er-rough-${id}`, source);
            return { id, source, calls: log };
          },
          {
            mermaidModule: pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href,
            roughUrl: pathToFileURL(path.join(chunksDir, roughChunk)).href,
            faces,
            fontFamily,
            id,
            init: inits[id] ?? {},
            source: `%%{init: ${JSON.stringify({ look: "handDrawn", handDrawnSeed: 17, ...inits[id] })}}%%\n${source}`,
          },
        );
      } finally {
        await page.close();
      }
    }),
  );
  const root = {
    upstream: { version: packageJson.version },
    oracle: "rough.svg calls of erDiagram under look handDrawn, seed 17",
    cases,
  };
  root.fixtureSha256 = createHash("sha256").update(JSON.stringify(root)).digest("hex");
  fs.writeFileSync(output, `${JSON.stringify(root, null, 1)}\n`);
  const calls = cases.reduce((sum, c) => sum + c.calls.length, 0);
  console.log(`Wrote ${cases.length} ER rough cases, ${calls} calls, to ${output}`);
  console.log(`fixtureSha256=${root.fixtureSha256}`);
} finally {
  await browser.close();
}
