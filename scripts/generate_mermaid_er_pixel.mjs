import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Captures pixel-render oracles for the erDiagram family against real mermaid
// 11.16.0 in headless Chrome, mirroring generate_mermaid_pie_pixel.mjs.
// Bundled Noto fonts (including CJK) pin text metrics for cross-host parity.
// A FIXED render id per case keeps the SVG deterministic (the id is the only
// byte that would otherwise vary between runs). The classic look covers the
// canonical example in all eleven themes and fifteen structures; the neo and
// the handDrawn looks cover the themes and structures they change.
//
//   node scripts/generate_mermaid_er_pixel.mjs \
//     [mermaid-root] [out-dir] [chrome-exe]
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/er-pixel,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const outDir = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "er-pixel"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const pkg = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (pkg.version !== "11.16.0")
  throw new Error(`Expected Mermaid 11.16.0, found ${pkg.version}`);

// The canonical three-entity example of the Mermaid documentation.
const canonical = [
  "erDiagram",
  "    CUSTOMER ||--o{ ORDER : places",
  "    ORDER ||--|{ LINE-ITEM : contains",
  "    CUSTOMER {",
  "        string name",
  "        string custNumber",
  "        string sector",
  "    }",
].join("\n");

// Structures of the family, one per case, all in the default theme.
const structures = {
  "attributes-keys-comments":
    'erDiagram\n    CUSTOMER {\n        string name PK "primary key"\n        int age\n        bigint account_id FK\n        string email UK "unique"\n    }',
  // Labels stay plain: the upstream lexer turns words such as "one-or-more" or
  // "zeroOrMore" into cardinality keywords and then rejects the line.
  "cardinality-types":
    "erDiagram\n    A ||--|| B : r1\n    C |o--o| D : r2\n    E }|--|{ F : r3\n    G }o--o{ H : r4",
  "non-identifying":
    "erDiagram\n    CUSTOMER }|..|{ DELIVERY-ADDRESS : uses\n    ORDER }o..o| COUPON : redeemed",
  "single-entity": "erDiagram\n    ACCOUNT",
  "acc-title": "erDiagram\n    accTitle: Order model\n    A ||--o{ B : rel",
  "long-labels":
    "erDiagram\n    PURCHASE-ORDER ||--|{ ORDER-LINE-ITEM : contains\n    SUPPLIER ||--o{ PURCHASE-ORDER : fulfils",
  tree: "erDiagram\n    ROOT\n    ROOT ||--o{ LEFT : left\n    ROOT ||--o{ RIGHT : right\n    LEFT ||--|| LEAF-A : a\n    RIGHT ||--|| LEAF-B : b\n    RIGHT ||--o{ LEAF-C : c",
  "wide-attributes":
    'erDiagram\n    SHIPMENT {\n        string tracking_number PK "carrier tracking id"\n        date shipped_on\n        text notes "free form remarks"\n    }\n    ITEM {\n        string sku PK\n        int quantity\n    }\n    SHIPMENT ||--|{ ITEM : contains',
  "cjk-labels": "erDiagram\n    客户 ||--o{ 订单 : 下单\n    订单 ||--|{ 明细 : 包含\n    订单 {\n        string 编号 PK\n    }",
  // A relationship from an entity to itself is drawn as a loop of three paths.
  "self-relationship":
    "erDiagram\n    EMPLOYEE {\n        string id PK\n        string manager_id FK\n    }\n    EMPLOYEE |o--o{ EMPLOYEE : manages\n    EMPLOYEE ||--|| DEPARTMENT : belongs",
  ring: "erDiagram\n    A ||--o{ B : ab\n    B ||--o{ C : bc\n    C ||--o{ D : cd\n    D ||--o{ E : de\n    E ||--o{ A : ea",
  aliases:
    'erDiagram\n    customer-account["Customer Account"]\n    sales-order["Sales Order"] {\n        date placed_on\n        string status\n    }\n    customer-account ||--o{ sales-order : places',
  "direction-lr":
    "erDiagram\n    direction LR\n    A ||--o{ B : r1\n    B ||--o{ C : r2\n    A |o--o{ D : r3",
  styles:
    "erDiagram\n    ORDER {\n        string number PK\n        date placed\n    }\n    CUSTOMER ||--o{ ORDER : places\n    classDef highlighted fill:#ff0000,stroke:#00ff00,stroke-width:3px,color:#0000ff\n    class ORDER highlighted\n    style CUSTOMER fill:#00ff00,stroke:#ff00ff",
};

// All 11 built-in themes on the canonical source (mirroring the pie oracle).
const themes = [
  "default", "base", "dark", "forest", "neutral",
  "neo", "neo-dark", "redux", "redux-dark", "redux-color", "redux-dark-color",
];
const themed = (id, theme, source, look = "classic") => ({
  id,
  dpr: 1,
  theme,
  look,
  renderId: `er-pixel-${id}`,
  source:
    theme === "default" && look === "classic"
      ? source
      : `%%{init: ${JSON.stringify(initOf(theme, look))}}%%\n${source}`,
});
// A case's configuration as its directive writes it. The handDrawn look pins
// its seed: Mermaid's default, 0, makes rough.js draw at random, so no two
// renders of a diagram agree and there is no image to compare against.
function initOf(theme, look) {
  const init = theme === "default" ? {} : { theme };
  if (look !== "classic")
    init.look = look;
  if (look === "handDrawn")
    init.handDrawnSeed = 17;
  return init;
}
// The neo look: over the themes whose shadow and strokes differ, and over the
// structures whose lines and markers it changes.
const neoLooks = [
  ["look-neo", "default", canonical],
  ["look-neo-neo", "neo", canonical],
  ["look-neo-neo-dark", "neo-dark", canonical],
  ["look-neo-redux-dark", "redux-dark", canonical],
  ["look-neo-forest", "forest", canonical],
  ["look-neo-non-identifying", "default", structures["non-identifying"]],
  ["look-neo-cardinality-types", "neo", structures["cardinality-types"]],
  ["look-neo-self-relationship", "default", structures["self-relationship"]],
  ["look-neo-styles", "default", structures["styles"]],
];
// The handDrawn look: the themes whose copy behind the shapes shows, and the
// structures whose paths and markers it changes.
const handDrawn = [
  ["look-hand-drawn", "default", canonical],
  ["look-hand-drawn-dark", "dark", canonical],
  ["look-hand-drawn-neo", "neo", canonical],
  ["look-hand-drawn-attributeless", "default", structures["single-entity"]],
  ["look-hand-drawn-cardinality-types", "default", structures["cardinality-types"]],
  ["look-hand-drawn-non-identifying", "default", structures["non-identifying"]],
  ["look-hand-drawn-self-relationship", "default", structures["self-relationship"]],
  ["look-hand-drawn-styles", "default", structures["styles"]],
];
const cases = [
  ...themes.map((theme) => themed(theme, theme, canonical)),
  ...Object.entries(structures).map(([id, source]) => themed(`structure-${id}`, "default", source)),
  ...neoLooks.map(([id, theme, source]) => themed(id, theme, source, "neo")),
  ...handDrawn.map(([id, theme, source]) => themed(id, theme, source, "handDrawn")),
];

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

function canonicalPng(bytes) {
  const image = PNG.sync.read(bytes);
  for (let offset = 0; offset < image.data.length; offset += 4)
    if (image.data[offset + 3] === 0)
      image.data.fill(0, offset, offset + 3);
  return PNG.sync.write(image, {
    colorType: 6,
    inputColorType: 6,
    bitDepth: 8,
  });
}

const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"),
  ).href,
);
fs.mkdirSync(outDir, { recursive: true });
const browser = await puppeteer.launch({
  executablePath: chrome,
  headless: true,
  args: [
    "--allow-file-access-from-files",
    "--disable-gpu",
    "--disable-lcd-text",
    "--font-render-hinting=none",
  ],
});
try {
  const manifestCases = [];
  for (const fixture of cases) {
    const page = await browser.newPage();
    await page.setViewport({
      width: 1600,
      height: 1200,
      deviceScaleFactor: fixture.dpr,
    });
    await page.goto(
      pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href,
    );
    const mermaidModule = pathToFileURL(
      path.join(mermaidRoot, "dist", "mermaid.esm.mjs"),
    ).href;
    await page.evaluate(
      async ({ mermaidModule, faces, fontFamily, fixture }) => {
        const { default: mermaid } = await import(mermaidModule);
        const style = document.createElement("style");
        style.textContent = faces;
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
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: fixture.theme,
          fontFamily,
          themeVariables: { fontFamily },
          look: "classic",
        });
        // FIXED render id per case for determinism (otherwise the SVG id — the
        // only byte that varies between renders — would change the raster).
        const { svg } = await mermaid.render(fixture.renderId, fixture.source);
        document.body.style.margin = "0";
        document.getElementById("container").innerHTML = svg;
        await document.fonts.ready;
        await new Promise((resolve) =>
          requestAnimationFrame(() => requestAnimationFrame(resolve)),
        );
      },
      { mermaidModule, faces, fontFamily, fixture },
    );
    const element = await page.$("svg");
    const png = canonicalPng(await element.screenshot({ omitBackground: true }));
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(outDir, file), png);
    const { width, height } = PNG.sync.read(png);
    manifestCases.push({
      id: fixture.id,
      dpr: fixture.dpr,
      theme: fixture.theme,
      look: fixture.look,
      renderId: fixture.renderId,
      source: fixture.source,
      file,
      width,
      height,
      sha256: createHash("sha256").update(png).digest("hex"),
    });
    await page.close();
  }
  const manifest = {
    upstream: { version: pkg.version },
    fontMode: "bundled-noto-2.13b171",
    cases: manifestCases,
  };
  manifest.fixtureSha256 = createHash("sha256")
    .update(JSON.stringify(manifest))
    .digest("hex");
  fs.writeFileSync(
    path.join(outDir, "manifest.json"),
    `${JSON.stringify(manifest, null, 2)}\n`,
  );
  console.log(`Wrote ${manifestCases.length} ER pixel cases to ${outDir}`);
  console.log(`fixtureSha256=${manifest.fixtureSha256}`);
} finally {
  await browser.close();
}
