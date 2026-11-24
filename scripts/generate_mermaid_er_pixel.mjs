import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { PNG } from "pngjs";

// Captures pixel-render oracles for the erDiagram family against real mermaid
// 11.16.0 in headless Chrome, mirroring generate_mermaid_pie_pixel.mjs.
// Bundled Noto fonts (including CJK) pin text metrics for cross-host parity.
// A FIXED render id per case keeps the SVG deterministic (the id is the only
// byte that would otherwise vary between runs).
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

// Structures of the family, one per case, all in the default theme. They stay
// inside the part of the grammar the port reads like upstream: no aliases, no
// `direction`, no quoted relationship labels, and no ring or self-relationship
// (whose layout still differs; MermaidErGeometryOracleTest lists those).
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
};

// A canvas the port sizes differently from the browser, in pixels (native
// minus browser). The port measures this diagram's widest label 0.04 px
// narrower than Chrome; the extent lands on a half pixel and rounds one pixel
// smaller. The test compares the common area and fails if the difference
// grows, changes or disappears.
const canvasDelta = {
  "structure-cjk-labels": { width: -1, height: 0 },
};

// All 11 built-in themes on the canonical source (mirroring the pie oracle).
const themes = [
  "default", "base", "dark", "forest", "neutral",
  "neo", "neo-dark", "redux", "redux-dark", "redux-color", "redux-dark-color",
];
const themed = (id, theme, source) => ({
  id,
  dpr: 1,
  theme,
  renderId: `er-pixel-${id}`,
  source: theme === "default" ? source : `%%{init: {"theme":"${theme}"}}%%\n${source}`,
  ...(canvasDelta[id] ? { canvasDelta: canvasDelta[id] } : {}),
});
const cases = [
  ...themes.map((theme) => themed(theme, theme, canonical)),
  ...Object.entries(structures).map(([id, source]) => themed(`structure-${id}`, "default", source)),
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
        await document.fonts.load('16px "Noto Sans"', "CUSTOMER");
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
      renderId: fixture.renderId,
      source: fixture.source,
      file,
      width,
      height,
      sha256: createHash("sha256").update(png).digest("hex"),
      ...(fixture.canvasDelta ? { canvasDelta: fixture.canvasDelta } : {}),
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
