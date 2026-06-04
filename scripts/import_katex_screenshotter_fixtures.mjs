import fs from "node:fs/promises";
import path from "node:path";
import {createRequire} from "node:module";
import {fileURLToPath} from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "..");
const katexRoot = process.env.KATEX_ROOT || "D:\\github\\KaTeX";
const outputPath = process.argv[2]
  ? path.resolve(process.argv[2])
  : path.join(repoRoot, "tests", "fixtures", "math", "katex-official-screenshotter.json");

const require = createRequire(path.join(katexRoot, "package.json"));
const pnpPath = path.join(katexRoot, ".pnp.cjs");
try {
  require(pnpPath).setup();
} catch {
  // KaTeX can also be installed with node_modules; in that case PnP is absent.
}

const screenshotterData = require(path.join(katexRoot, "test", "screenshotter", "ss_data.js"));
const slug = value => value
  .replace(/([a-z0-9])([A-Z])/g, "$1-$2")
  .replace(/[^A-Za-z0-9]+/g, "-")
  .replace(/^-+|-+$/g, "")
  .toLowerCase();

const fixtures = [];
for (const [name, entry] of Object.entries(screenshotterData)) {
  if (!entry || typeof entry.tex !== "string" || entry.tex.trim().length === 0) {
    continue;
  }
  const fixture = {
    id: `official-${slug(name)}`,
    sourceName: name,
    tex: entry.tex,
    display: Boolean(entry.display),
    officialScreenshotter: true,
    compareKatexBBox: true,
    bboxWidthToleranceEm: 0.5,
    bboxHeightToleranceEm: 0.5,
    compareKatexGlyphs: true,
    glyphXToleranceEm: 0.25,
    glyphWidthToleranceEm: 0.12
  };
  if (entry.macros && typeof entry.macros === "object") {
    fixture.macros = entry.macros;
  }
  if (entry.noThrow !== undefined) {
    fixture.noThrow = Boolean(entry.noThrow);
  }
  if (entry.errorColor) {
    fixture.errorColor = entry.errorColor;
  }
  if (entry.styles) {
    fixture.styles = entry.styles;
  }
  fixtures.push(fixture);
}

await fs.mkdir(path.dirname(outputPath), {recursive: true});
await fs.writeFile(outputPath, `${JSON.stringify(fixtures, null, 2)}\n`, "utf8");
console.log(`Wrote ${fixtures.length} KaTeX official screenshotter fixtures to ${outputPath}`);
