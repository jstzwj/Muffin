// Exhaustive themeVariables key inventory (historical Gate D — closed; this
// generator now maintains the inventory as a permanent regression oracle).
//
// Union of every resolved themeVariables key across the 11 built-in themes
// (values re-used verbatim from the flowchart-theme golden, which captures
// mermaid's resolved `calculate()` output), plus a per-key upstream-consumer
// classification derived from the 11.16.0 dist chunks: which diagram modules
// reference the key (as `options.<key>` in a stylesheet template or in
// renderer code). The C++ side (MermaidThemeTest) walks every key/value pair
// through FlowThemeVariables::get(); keys the native model does not yet
// reproduce are locked in `remaining` — the precise partial-closure list the
// config matrix's themeVariables.* row points at.
//
// Deterministic: reads only frozen inputs (flowchart-theme.json + dist
// chunks); two runs must produce byte-identical output.
import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ??
    path.join("tests", "fixtures", "mermaid", "theme-variables-inventory.json"),
);
const packageJson = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const golden = JSON.parse(
  fs.readFileSync(
    path.join(path.dirname(output), "flowchart-theme.json"),
    "utf8",
  ),
);

// Map each dist chunk to the diagram family it implements (theme/shared
// chunks get their own labels; the mermaid.core entry file is excluded — it
// only routes). Classification is by file mention: a key is "live" for every
// family whose module file references it.
const chunkDir = path.join(mermaidRoot, "dist", "chunks", "mermaid.core");
const familyOf = (file) => {
  const stem = path.basename(file, ".mjs");
  const match = /^(.*)Diagram(-[A-Z0-9]+)?$/.exec(stem);
  if (match) return match[1].replace(/([a-z])([A-Z])/g, "$1-$2").toLowerCase();
  if (stem.startsWith("swimlanes")) return "flowchart";
  if (stem.startsWith("chunk-")) return "shared";
  return "shared";
};
const chunks = fs
  .readdirSync(chunkDir)
  .filter((f) => f.endsWith(".mjs") && !f.endsWith(".min.mjs"))
  .map((f) => ({ family: familyOf(f), text: fs.readFileSync(path.join(chunkDir, f), "utf8") }));

const themes = golden.themes.map((theme) => theme.name);
const values = {};
for (const theme of golden.themes) values[theme.name] = theme.variables;

const union = new Set();
for (const theme of golden.themes)
  for (const key of Object.keys(theme.variables)) union.add(key);

const consumers = {};
for (const key of [...union].sort()) {
  const families = new Set();
  for (const chunk of chunks) {
    // `options.<key>` (stylesheet template), `themeVariables.<key>` (renderer),
    // or a bare `"<key>"` string lookup.
    if (
      chunk.text.includes(`options.${key}`) ||
      chunk.text.includes(`themeVariables.${key}`) ||
      chunk.text.includes(`"${key}"`) ||
      chunk.text.includes(`${key}:`)
    ) {
      families.add(chunk.family);
    }
  }
  consumers[key] = [...families].sort();
}

const payload = {
  upstream: {
    package: packageJson.name,
    version: packageJson.version,
    source: "flowchart-theme.json resolved variables + dist chunk references",
  },
  themes,
  keys: Object.fromEntries(
    [...union].sort().map((key) => [key, { consumers: consumers[key] }]),
  ),
  values,
};
const canonical = JSON.stringify(payload);
payload.fixtureSha256 = createHash("sha256").update(canonical).digest("hex");
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
console.log(
  `Wrote ${union.size} themeVariables keys (${themes.length} themes) to ${output}`,
);
console.log(`fixtureSha256=${payload.fixtureSha256}`);
