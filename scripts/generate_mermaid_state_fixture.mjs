import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ??
  path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const output = path.resolve(process.argv[3] ??
  path.join("tests", "fixtures", "mermaid", "state-db.json"));
const chrome = process.argv[4] ??
  "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (pkg.version !== "11.16.0")
  throw new Error(`Expected Mermaid 11.16.0, found ${pkg.version}`);

const chunkDir = path.join(mermaidRoot, "dist", "chunks", "mermaid.core");
let grammarSource;
let grammarSourceName;
for (const file of fs.readdirSync(chunkDir).filter((name) => name.endsWith(".mjs.map"))) {
  const sourceMap = JSON.parse(fs.readFileSync(path.join(chunkDir, file), "utf8"));
  const index = sourceMap.sources.findIndex((name) =>
    name.endsWith("/state/parser/stateDiagram.jison"));
  if (index < 0) continue;
  grammarSource = sourceMap.sourcesContent[index];
  grammarSourceName = sourceMap.sources[index];
  break;
}
if (!grammarSource) throw new Error("Mermaid stateDiagram.jison source map was not found");
const productionMatch = grammarSource.match(/productions_:\s*(\[0,[^\n]+\])/);
if (!productionMatch) throw new Error("Generated state production table was not found");
const generatedProductions = JSON.parse(productionMatch[1]).slice(1);
if (generatedProductions.length !== 49)
  throw new Error(`Expected 49 state productions, found ${generatedProductions.length}`);

const cases = [
  { id: "transitions-start-end", source: [
    "stateDiagram-v2", "[*] --> Idle", "Idle --> Active : start", "Active --> [*]",
  ].join("\n") },
  { id: "aliases-descriptions", source: [
    "stateDiagram-v2", 'state "Long state name" as long_id',
    "long_id : first description", "long_id : second description",
  ].join("\n") },
  { id: "composite-direction-concurrency", source: [
    "stateDiagram-v2", "state Running {", "  direction LR", "  [*] --> Warmup",
    "  Warmup --> Ready", "  --", "  Waiting --> Done", "}",
  ].join("\n") },
  { id: "pseudostates", source: [
    "stateDiagram-v2", "state fork_state <<fork>>", "state join_state <<join>>",
    "state choice_state <<choice>>", "A --> fork_state", "fork_state --> B",
    "B --> join_state", "join_state --> choice_state",
  ].join("\n") },
  { id: "notes", source: [
    "stateDiagram-v2", "state Active", "note right of Active : Inline note",
    "note left of Active", "  Multiline note", "end note",
  ].join("\n") },
  { id: "styles-classes", source: [
    "stateDiagram-v2", "state A", "state B", "classDef hot fill:#f00,color:#fff",
    "class A,B hot", "style B fill:#0f0,stroke:#333",
  ].join("\n") },
  { id: "accessibility-click", source: [
    "stateDiagram-v2", "accTitle: State model", "accDescr: Accessible state model",
    "state Docs", 'click Docs "https://example.com" "Open docs"',
  ].join("\n") },
  { id: "unicode-comments", source: [
    "stateDiagram-v2", "%% ignored", "待机 --> 运行 : تشغيل", "运行 --> שלום",
  ].join("\n") },
];

const { default: puppeteer } = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
const browser = await puppeteer.launch({
  executablePath: chrome, headless: true, args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const snapshots = await page.evaluate(async ({ cases, mermaidModule }) => {
    const { default: mermaid } = await import(mermaidModule);
    const clean = (value) => value === undefined ? null : value;
    const state = (value) => ({
      id: value.id, type: value.type, descriptions: value.descriptions ?? [],
      doc: clean(value.doc), note: clean(value.note), classes: value.classes ?? [],
      styles: value.styles ?? [], textStyles: value.textStyles ?? [],
    });
    const normalizeGeneratedIds = (value, ids = new Map()) => {
      if (typeof value === "string") {
        const match = value.match(/^(id-[a-z0-9]+-\d+)(.*)$/i);
        if (!match) return value;
        if (!ids.has(match[1])) ids.set(match[1], `$generated-${ids.size + 1}`);
        return ids.get(match[1]) + match[2];
      }
      if (Array.isArray(value))
        return value.map((child) => normalizeGeneratedIds(child, ids));
      if (value && typeof value === "object")
        return Object.fromEntries(Object.entries(value).map(([key, child]) =>
          [key, normalizeGeneratedIds(child, ids)]));
      return value;
    };
    const result = [];
    for (const fixture of cases) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
      const diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
      const db = diagram.db;
      const expected = {
        root: db.rootDoc,
        direction: db.getDirection(),
        accTitle: db.getAccTitle(),
        accDescription: db.getAccDescription(),
        states: [...db.getStates()].map(([id, value]) => ({ id, ...state(value) })),
        relations: db.getRelations().map((value) => ({
          id1: value.id1, id2: value.id2,
          relationTitle: clean(value.relationTitle),
        })),
        classes: [...db.getClasses()].map(([id, value]) => ({
          id, styles: value.styles, textStyles: value.textStyles,
        })),
        links: [...db.getLinks()].map(([id, value]) => ({ id, ...value })),
      };
      result.push({ ...fixture, expected: normalizeGeneratedIds(expected) });
    }
    return result;
  }, { cases, mermaidModule });

  const payload = {
    upstream: {
      version: pkg.version,
      grammar: {
        source: grammarSourceName,
        sha256: createHash("sha256").update(grammarSource).digest("hex"),
        productionCount: generatedProductions.length,
        productions: generatedProductions.map(([symbol, rhsLength], index) => ({
          id: index + 1, symbol, rhsLength,
        })),
      },
    },
    cases: snapshots,
  };
  payload.fixtureSha256 = createHash("sha256")
    .update(JSON.stringify(payload)).digest("hex");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${snapshots.length} state cases to ${output}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
