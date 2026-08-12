import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "preprocess-and-detect.json"),
);
const configKeysOutput = path.resolve(
  process.argv[4] ?? path.join("src", "mermaid", "MermaidConfigKeys.inc"),
);
const packageJson = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}

const { default: mermaid } = await import(
  pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.core.mjs"))
);
const { JSON_SCHEMA, load } = await import(
  pathToFileURL(
    path.join(
      mermaidRoot,
      "dist",
      "chunks",
      "mermaid.core",
      "chunk-ZIRB5QZD.mjs",
    ),
  )
);
const { cleanAndMerge, removeDirectives, utils_default: utils } = await import(
  pathToFileURL(
    path.join(
      mermaidRoot,
      "dist",
      "chunks",
      "mermaid.core",
      "chunk-ICXQ74PX.mjs",
    ),
  )
);
const { defaultConfig } = await import(
  pathToFileURL(
    path.join(
      mermaidRoot,
      "dist",
      "chunks",
      "mermaid.core",
      "chunk-WYO6CB5R.mjs",
    ),
  )
);

mermaid.initialize({ startOnLoad: false });

const frontMatterRegex =
  /^([^\S\n\r]*)-{3}\s*[\n\r](.*?)[\n\r]\1-{3}\s*[\n\r]+/s;
const cleanupText = (code) =>
  code
    .replace(/\r\n?/g, "\n")
    .replace(
      /<(\w+)([^>]*)>/g,
      (_match, tag, attributes) =>
        "<" + tag + attributes.replace(/="([^"]*)"/g, "='$1'") + ">",
    );
const cleanupComments = (text) =>
  text.replace(/^\s*%%(?!{)[^\n]+\n?/gm, "").trimStart();

function extractFrontMatter(text) {
  const matches = text.match(frontMatterRegex);
  if (!matches) return { text, metadata: {} };
  const indent = matches[1];
  const yamlBody = indent
    ? matches[2]
        .split("\n")
        .map((line) =>
          line.startsWith(indent) ? line.slice(indent.length) : line,
        )
        .join("\n")
    : matches[2];
  let parsed = load(yamlBody, { schema: JSON_SCHEMA }) ?? {};
  parsed = typeof parsed === "object" && !Array.isArray(parsed) ? parsed : {};
  const metadata = {};
  if (parsed.displayMode) metadata.displayMode = parsed.displayMode.toString();
  if (parsed.title) metadata.title = parsed.title.toString();
  if (parsed.config) metadata.config = parsed.config;
  return { text: text.slice(matches[0].length), metadata };
}

function preprocessDiagram(source) {
  const cleaned = cleanupText(source);
  const { text, metadata } = extractFrontMatter(cleaned);
  const { displayMode, title, config = {} } = metadata;
  if (displayMode) {
    config.gantt ??= {};
    config.gantt.displayMode = displayMode;
  }
  const directive = utils.detectInit(text) ?? {};
  const wraps = utils.detectDirective(text, "wrap");
  if (Array.isArray(wraps)) {
    directive.wrap = wraps.some(({ type }) => type === "wrap");
  } else if (wraps?.type === "wrap") {
    directive.wrap = true;
  }
  return {
    code: cleanupComments(removeDirectives(text)),
    ...(title === undefined ? {} : { title }),
    config: cleanAndMerge(config, directive),
  };
}

const detectInputs = [
  ["error", "error"],
  ["invalid-frontmatter", "---\nnot closed", {}, "---"],
  ["flowchart-elk-explicit", "flowchart-elk LR\na-->b", {}, "flowchart-elk"],
  ["flowchart-elk-config", "graph LR\na-->b", { flowchart: { defaultRenderer: "elk" } }, "flowchart-elk"],
  ["mindmap", "mindmap\n root"],
  ["architecture", "architecture-beta\n group api(cloud)"],
  ["c4", "C4Context\nPerson(a, A)"],
  ["c4-upstream-alternation", "prefix C4Container suffix", {}, "c4"],
  ["kanban", "kanban\n column"],
  ["class-v2-explicit", "classDiagram-v2\nA <|-- B", {}, "classDiagram"],
  ["class-v2-config", "classDiagram\nA <|-- B", { class: { defaultRenderer: "dagre-wrapper" } }, "classDiagram"],
  ["class-legacy", "classDiagram\nA <|-- B", {}, "class"],
  ["er", "erDiagram\nA ||--o{ B : has"],
  ["gantt", "gantt\ntitle x"],
  ["info", "info"],
  ["pie", "pie\n\"A\" : 1"],
  ["requirement", "requirementDiagram\nrequirement x {}"],
  ["sequence", "sequenceDiagram\nA->>B: hi"],
  ["swimlane", "swimlane-beta\nA"],
  ["flow-v2", "flowchart LR\na-->b", {}, "flowchart-v2"],
  ["flow-v2-config", "graph LR\na-->b", { flowchart: { defaultRenderer: "dagre-wrapper" } }, "flowchart-v2"],
  ["flow-legacy", "graph LR\na-->b", {}, "flowchart"],
  ["timeline", "timeline\n2024 : x"],
  ["git", "gitGraph\ncommit"],
  ["state-v2", "stateDiagram-v2\n[*] --> A", {}, "stateDiagram"],
  ["state-v2-config", "stateDiagram\n[*] --> A", { state: { defaultRenderer: "dagre-wrapper" } }, "stateDiagram"],
  ["state-legacy", "stateDiagram\n[*] --> A", {}, "state"],
  ["journey", "journey\ntitle x"],
  ["quadrant", "quadrantChart\nx-axis a --> b"],
  ["sankey", "sankey-beta\na,b,1"],
  ["packet", "packet-beta\n0-7: x"],
  ["xychart", "xychart-beta\nx-axis [1]"],
  ["block", "block-beta\na"],
  ["eventmodeling", "eventmodeling\ncommands:"],
  ["tree", "treeView-beta\nroot"],
  ["radar", "radar-beta\naxis a"],
  ["ishikawa-case", "ISHIKAWA-beta\nproblem: x", {}, "ishikawa"],
  ["treemap", "treemap\nroot"],
  ["railroad", "railroad-beta\nA=terminal('a');"],
  ["railroad-ebnf", "railroad-ebnf-beta\nA = 'a';"],
  ["railroad-abnf", "railroad-abnf-beta\nA = \"a\";"],
  ["railroad-peg", "railroad-peg-beta\nA <- 'a';"],
  ["venn", "venn-beta\nset A"],
  ["wardley", "WARDLEY-beta\ntitle x", {}, "wardley"],
  ["cynefin", "cynefin-beta:\nclear"],
  ["preamble", "---\ntitle: T\n---\n%% comment\n%%{init: {\"theme\": \"dark\"}}%%\nsequenceDiagram\nA->>B: hi", {}, "sequence"],
  ["unknown", "definitely not a diagram", {}, null],
];

const detection = detectInputs.map(([id, source, config = {}, expected]) => {
  let diagramType = expected;
  if (expected === undefined) {
    diagramType = mermaid.detectType(source, config);
  } else if (expected === null) {
    try {
      diagramType = mermaid.detectType(source, config);
    } catch {
      diagramType = null;
    }
  } else {
    const actual = mermaid.detectType(source, config);
    if (actual !== expected) {
      throw new Error(`${id}: expected ${expected}, upstream returned ${actual}`);
    }
  }
  return { id, source, config, diagramType };
});

const preprocessInputs = [
  ["plain", "graph LR\r\nA[\"x\"] --> B\r\n"],
  ["comments", "  %% first\n%% second\nsequenceDiagram\nA->>B: hi\n%% tail"],
  ["html-attributes", "flowchart LR\nA[<span class=\"x\" data-v=\"a b\">Hi</span>]"],
  ["frontmatter", "---\ntitle: Example\ndisplayMode: compact\nconfig:\n  theme: forest\n  flowchart:\n    curve: basis\n---\ngantt\ntitle Work"],
  ["indented-frontmatter", "  ---\n  title: Indented\n  config:\n    theme: dark\n  ---\nflowchart LR\na-->b"],
  ["init", "%%{init: {\"theme\": \"dark\", \"flowchart\": {\"curve\": \"linear\"}}}%%\nflowchart LR\na-->b"],
  ["legacy-config-init", "%%{initialize: {\"config\": {\"curve\": \"step\"}}}%%\nflowchart LR\na-->b"],
  ["multiple-and-wrap", "%%{init: {\"theme\": \"forest\"}}%%\n%%{init: {\"flowchart\": {\"curve\": \"basis\"}}}%%\n%%{wrap}%%\nflowchart LR\na-->b"],
  ["sanitize", "%%{init: {\"theme\": \"dark\", \"unknownKey\": 1, \"__proto__\": {\"x\": 1}}}%%\nflowchart LR\na-->b"],
  ["json-schema-scalars", "---\ntitle: yes\nconfig:\n  darkMode: true\n  handDrawnSeed: 12\n---\nflowchart LR\na-->b"],
  ["quoted-scalars", "---\ntitle: \"true\"\nconfig:\n  darkMode: \"false\"\n---\nflowchart LR\na-->b"],
  ["numeric-title", "---\ntitle: 42\n---\npie\n\"A\" : 1"],
  ["empty-title", "---\ntitle: \"\"\n---\ninfo"],
  ["non-map-frontmatter", "---\n- one\n- two\n---\ninfo"],
  ["frontmatter-closing-indent-guard", "---\ntitle: Outer\nvalue: |\n  inside\n  ---\n---\ninfo"],
  ["carriage-returns", "%% comment\rgraph LR\rA-->B\r"],
  ["directive-is-not-comment", "%%{wrap}%%\n%% ordinary\nflowchart LR\na-->b"],
  ["single-quoted-init", "%%{init: {'theme': 'neutral'}}%%\nflowchart LR\na-->b"],
  ["malformed-init-json", "%%{init: {broken: true}}%%\nflowchart LR\na-->b"],
  ["multiple-init-sanitizes", "%%{init: {\"theme\": \"dark\", \"unknownKey\": 1}}%%\n%%{init: {\"flowchart\": {\"curve\": \"linear\"}}}%%\nflowchart LR\na-->b"],
  ["multiple-init-full-sanitizer", "%%{init: {\"themeCSS\": \"a {\", \"radar\": {\"graticuleOpacity\": 0.5}, \"themeVariables\": {\"primaryColor\": \"#fff\", \"unknownThemeKey\": \"red\", \"lineColor\": \"url(x)\"}, \"treeView\": {\"filenameIcons\": {\"README\": \"logos:readme\", \"bad\": \"x y\"}}}}%%\n%%{init: {\"theme\": \"dark\"}}%%\nflowchart LR\na-->b"],
  ["frontmatter-init-merge", "---\nconfig:\n  theme: forest\n  flowchart:\n    curve: basis\n---\n%%{init: {\"theme\": \"dark\", \"flowchart\": {\"defaultRenderer\": \"dagre-wrapper\"}}}%%\ngraph LR\na-->b"],
  ["comment-without-final-newline", "flowchart LR\na-->b\n%% tail"],
];

const fixture = {
  upstream: {
    package: "mermaid",
    version: packageJson.version,
    license: packageJson.license,
  },
  detection,
  preprocess: preprocessInputs.map(([id, source]) => ({
    id,
    source,
    expected: preprocessDiagram(source),
  })),
};

fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
console.log(`Wrote ${output}`);

const configKeys = new Set();
const collectConfigKeys = (object) => {
  for (const key of Object.keys(object)) {
    if (Array.isArray(object[key])) continue;
    configKeys.add(key);
    if (object[key] && typeof object[key] === "object") {
      collectConfigKeys(object[key]);
    }
  }
};
collectConfigKeys(defaultConfig);
const configKeysSource = [
  "// Generated from Mermaid 11.16.0 defaultConfig by",
  "// scripts/generate_mermaid_compatibility_fixture.mjs. Do not hand-edit.",
  ...[...configKeys]
    .sort()
    .map((key) => `QStringLiteral(${JSON.stringify(key)}),`),
  "",
].join("\n");
fs.mkdirSync(path.dirname(configKeysOutput), { recursive: true });
fs.writeFileSync(configKeysOutput, configKeysSource);
console.log(`Wrote ${configKeysOutput}`);
