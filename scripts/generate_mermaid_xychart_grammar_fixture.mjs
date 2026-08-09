import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0 xychart source-entry grammar and DB behavior.
// Usage:
//   node scripts/generate_mermaid_xychart_grammar_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]

const EXPECTED_MERMAID_VERSION = "11.16.0";
const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ??
    path.join("tests", "fixtures", "mermaid", "xychart-grammar.json"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const packageJson = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (packageJson.version !== EXPECTED_MERMAID_VERSION) {
  throw new Error(
    `Expected Mermaid ${EXPECTED_MERMAID_VERSION}, found ${packageJson.version}`,
  );
}

const cases = [
  { id: "simple-beta", source: "xychart-beta\nline [1,2]" },
  { id: "simple-stable", source: "xychart\nbar [3,4]" },
  {
    id: "horizontal-same-line",
    source: "xychart-beta horizontal\nx-axis [A,B]\nline [1,2]",
  },
  {
    id: "vertical-same-line",
    source: "xychart vertical\nx-axis [A,B]\nbar [1,2]",
  },
  {
    id: "uppercase-orientation-token-selects-vertical",
    source: "xychart-beta HORIZONTAL\nx-axis [A,B]\nline [1,2]",
  },
  {
    id: "signed-decimals",
    source: "xychart-beta\nline [-1,+.5,.25,1.0]",
  },
  {
    id: "point-label-adjacent",
    source: 'xychart-beta\nline "ignored title" [1 "one",2,3 "three"]',
  },
  {
    id: "point-label-empty",
    source: 'xychart-beta\nline [1 "",2] ',
  },
  {
    id: "empty-qstring-transparent-title",
    source: 'xychart-beta\ntitle "" X\nline [1]',
  },
  {
    id: "empty-qstring-transparent-plot-title",
    source: 'xychart-beta\nline "" X [1]',
  },
  {
    id: "empty-qstring-transparent-band-text",
    source: 'xychart-beta\nx-axis ["" A,B]\nline [1,2]',
  },
  {
    id: "empty-qstring-transparent-axis-title",
    source: 'xychart-beta\nx-axis "" [A,B]\nline [1,2]',
  },
  {
    id: "bar-labels-discarded",
    source: 'xychart-beta\nbar "ignored" [1 "one",2 "two"]',
  },
  {
    id: "band-unquoted-concatenates",
    source: 'xychart-beta\nx-axis [A,B C,"D E",F-G]\nline [1,2,3,4]',
  },
  {
    id: "axis-titles-and-band",
    source: 'xychart-beta\nx-axis "X Axis" [A,B]\ny-axis "Y Axis" 0 --> 10\nline [1,2]',
  },
  {
    id: "unquoted-axis-titles-and-ranges",
    source:
      "xychart-beta\nx-axis Time 0 --> 10\ny-axis Value -1 --> 1\nline [1,2]",
  },
  {
    id: "x-title-only",
    source: 'xychart-beta\nx-axis "Only X"\nline [1,2]',
  },
  {
    id: "y-title-only",
    source: 'xychart-beta\ny-axis "Only Y"\nline [1,2]',
  },
  {
    id: "linear-ranges",
    source: "xychart-beta\nx-axis -1.5 --> +2.5\ny-axis -10 --> 10\nline [1,2,3]",
  },
  {
    id: "descending-x-range",
    source: "xychart-beta\nx-axis 5 --> 1\nline [1,2]",
  },
  {
    id: "single-linear-point",
    source: "xychart-beta\nx-axis 7 --> 9\nline [42]",
  },
  {
    id: "multiple-auto-range-plots",
    source: "xychart-beta\nline [10,20]\nbar [30,40,50]",
  },
  {
    id: "band-truncates-extra-values",
    source: "xychart-beta\nx-axis [A,B]\nline [1,2,999]",
  },
  {
    id: "band-retains-undefined-tail",
    source: "xychart-beta\nx-axis [A,B,C]\nline [1,2]",
  },
  {
    id: "repeated-axis-last-wins",
    source: "xychart-beta\nx-axis [A,B]\nx-axis 10 --> 20\ny-axis 0 --> 5\ny-axis -2 --> 8\nline [1,2]",
  },
  {
    id: "line-and-bar-titles-ignored",
    source: 'xychart-beta\nline "Line Name" [1,2]\nbar "Bar Name" [3,4]',
  },
  {
    id: "unquoted-title-collapses-space",
    source: "xychart-beta\ntitle Hello  world\nline [1]",
  },
  {
    id: "quoted-title-preserves-space",
    source: 'xychart-beta\ntitle "Hello  world"\nline [1]',
  },
  {
    id: "quoted-title-edge-space",
    source: 'xychart-beta\ntitle "  Hello  "\nline [1]',
  },
  {
    id: "quoted-band-edge-space",
    source: 'xychart-beta\nx-axis ["  A  ",B]\nline [1,2]',
  },
  {
    id: "quoted-point-label-edge-space",
    source: 'xychart-beta\nline [1 "  one  ",2]',
  },
  {
    id: "repeated-title-last-wins",
    source: 'xychart-beta\ntitle "First"\ntitle "Second"\nline [1]',
  },
  {
    id: "accessibility",
    source:
      "xychart-beta\naccTitle:  XY title  \naccDescr:  XY description  \nline [1]",
  },
  {
    id: "accessibility-block",
    source:
      "xychart-beta\naccDescr {\n first line\n second line \n}\nline [1]",
  },
  {
    id: "frontmatter-title",
    source: "---\ntitle: Front Matter\n---\nxychart-beta\nline [1]",
  },
  {
    id: "inline-title-wins-frontmatter",
    source:
      '---\ntitle: Front Matter\n---\nxychart-beta\ntitle "Inline Heading"\nline [1]',
  },
  {
    id: "whitespace-inline-title-wins-frontmatter",
    source:
      '---\ntitle: Front Matter\n---\nxychart-beta\ntitle "   "\nline [1]',
  },
  {
    id: "db-text-sanitizer",
    source:
      'xychart-beta\nx-axis "<script>x</script>Axis" ["<script>x</script>A"]\nline [1 "<script>x</script>"]',
  },
  {
    id: "db-text-sanitizer-event-attribute",
    source: 'xychart-beta\nx-axis ["<img src=x onerror=bad>A"]\nline [1]',
  },
  {
    id: "db-text-sanitizer-unclosed-script",
    source: 'xychart-beta\nx-axis ["<script>A"]\nline [1]',
  },
  {
    id: "db-text-sanitizer-style",
    source: 'xychart-beta\nx-axis ["<style>x</style>A"]\nline [1]',
  },
  {
    id: "db-text-sanitizer-dangerous-url",
    source:
      'xychart-beta\nx-axis ["<a href=javascript:bad>x</a>A"]\nline [1]',
  },
  {
    id: "db-text-sanitizer-safe-markup",
    source: 'xychart-beta\nx-axis ["<b>B</b>A"]\nline [1]',
  },
  {
    id: "comments",
    source:
      "%% before\nxychart-beta %% header\n%% middle\nline [1,2] %% tail",
  },
  {
    id: "semicolon-separators",
    source: "xychart-beta; x-axis [A,B]; line [1,2]; bar [3,4];",
  },
  {
    id: "bom-crlf",
    source: "\ufeffxychart-beta\r\nx-axis [A,B]\r\nline [1,2]\r\n",
  },
  {
    id: "palette-wrap",
    source:
      "xychart-beta\n" +
      Array.from({ length: 12 }, (_, i) =>
        `${i % 2 === 0 ? "line" : "bar"} [${i + 1}]`,
      ).join("\n"),
  },
  { id: "no-plot-runtime", source: "xychart-beta\nx-axis [A]" },

  { id: "reject-uppercase-detector", source: "XYCHART-BETA\nline [1]" },
  { id: "reject-headerless-detector", source: "line [1]" },
  { id: "reject-leading-semicolon-detector", source: ";xychart-beta\nline [1]" },
  { id: "reject-header-prefix", source: "xychart-betaX\nline [1]" },
  {
    id: "reject-orientation-next-line",
    source: "xychart-beta\nhorizontal\nline [1]",
  },
  { id: "reject-unknown-orientation", source: "xychart-beta diagonal\nline [1]" },
  { id: "reject-empty-plot", source: "xychart-beta\nline []" },
  { id: "reject-empty-title", source: 'xychart-beta\ntitle ""\nline [1]' },
  { id: "reject-leading-comma", source: "xychart-beta\nline [,1]" },
  { id: "reject-trailing-comma", source: "xychart-beta\nline [1,]" },
  { id: "reject-double-comma", source: "xychart-beta\nline [1,,2]" },
  { id: "reject-colon-label", source: 'xychart-beta\nline [1:"one"]' },
  { id: "reject-exponent", source: "xychart-beta\nline [1e2]" },
  { id: "reject-trailing-dot", source: "xychart-beta\nline [1.]" },
  { id: "reject-nan", source: "xychart-beta\nline [NaN]" },
  { id: "reject-y-band", source: "xychart-beta\ny-axis [A,B]\nline [1,2]" },
  { id: "reject-empty-x-axis", source: "xychart-beta\nx-axis\nline [1]" },
  { id: "reject-empty-band", source: "xychart-beta\nx-axis []\nline [1]" },
  {
    id: "reject-empty-band-qstring",
    source: 'xychart-beta\nx-axis ["",A]\nline [1,2]',
  },
  {
    id: "reject-empty-axis-title-qstring",
    source: 'xychart-beta\nx-axis ""\nline [1]',
  },
  { id: "reject-single-quote", source: "xychart-beta\nx-axis ['A']\nline [1]" },
  { id: "reject-unterminated-string", source: 'xychart-beta\nx-axis ["A]\nline [1]' },
  { id: "reject-unknown-statement", source: "xychart-beta\narea [1]" },
  {
    id: "reject-reserved-token-in-title",
    source: "xychart-beta\ntitle Inline Title\nline [1]",
  },
];

const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(
      path.dirname(mermaidRoot),
      "puppeteer",
      "lib",
      "puppeteer",
      "puppeteer.js",
    ),
  )
);
const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-web-security", "--no-sandbox"],
});

const classify = (message) => {
  if (message.startsWith("No diagram type detected")) return "no-diagram";
  if (message.includes("Lexical error") || message.includes("Lexer error"))
    return "lexer";
  if (message.includes("Parse error") || message.includes("Parsing failed"))
    return "parser";
  return "runtime";
};

try {
  const page = await browser.newPage();
  await page.goto(
    pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href,
  );
  const moduleUrl = pathToFileURL(moduleFile).href;
  const run = async (suffix) =>
    page.evaluate(
      async ({ fixtures, moduleUrl: mod, suffix: runSuffix }) => {
        const { default: mermaid } = await import(mod);
        const initialize = () =>
          mermaid.initialize({
            startOnLoad: false,
            securityLevel: "strict",
            theme: "default",
            look: "classic",
            deterministicIds: true,
            deterministicIDSeed: "xychart-grammar",
          });
        const numberValue = (value) => {
          if (value === undefined) return { kind: "undefined" };
          if (Number.isNaN(value)) return { kind: "nan" };
          if (value === Infinity) return { kind: "infinity", sign: 1 };
          if (value === -Infinity) return { kind: "infinity", sign: -1 };
          return {
            kind: "finite",
            value,
            ...(Object.is(value, -0) ? { negativeZero: true } : {}),
          };
        };
        const normalizeDb = (diagram) => {
          const db = diagram.db;
          const data = db.getXYChartData();
          const axis = (value) =>
            value.type === "band"
              ? {
                  type: "band",
                  title: value.title,
                  categories: [...value.categories],
                }
              : {
                  type: "linear",
                  title: value.title,
                  min: numberValue(value.min),
                  max: numberValue(value.max),
                };
          return {
            title: db.getDiagramTitle?.() ?? "",
            accTitle: db.getAccTitle?.() ?? "",
            accDescr: db.getAccDescription?.() ?? "",
            orientation: db.getChartConfig().chartOrientation,
            xAxis: axis(data.xAxis),
            yAxis: axis(data.yAxis),
            plots: data.plots.map((plot, paletteIndex) => ({
              type: plot.type,
              paletteIndex,
              points: plot.data.map(([category, value]) => ({
                category,
                value: numberValue(value),
              })),
              hasPointLabels: Object.hasOwn(plot, "pointLabels"),
              pointLabels: [...(plot.pointLabels ?? [])],
            })),
          };
        };
        const errorValue = (error) => {
          const message = String(error?.message ?? error);
          const loc = error?.hash?.loc;
          return {
            class:
              message.startsWith("No diagram type detected")
                ? "no-diagram"
                : message.includes("Lexical error") || message.includes("Lexer error")
                  ? "lexer"
                  : message.includes("Parse error") || message.includes("Parsing failed")
                    ? "parser"
                    : "runtime",
            message,
            ...(error?.hash?.line !== undefined
              ? { line: Number(error.hash.line) + 1 }
              : {}),
            ...(loc?.first_column !== undefined
              ? { column: Number(loc.first_column) + 1 }
              : {}),
          };
        };

        const out = [];
        for (const fixture of fixtures) {
          initialize();
          let diagram;
          let parseError = null;
          try {
            diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
          } catch (error) {
            parseError = errorValue(error);
          }
          const result = {
            id: fixture.id,
            source: fixture.source,
            parseAccept: parseError === null,
          };
          if (parseError !== null) {
            result.reject = parseError;
            result.renderAccept = false;
            out.push(result);
            continue;
          }
          result.expectedDb = normalizeDb(diagram);
          try {
            initialize();
            await mermaid.render(
              `xychart-grammar-${runSuffix}-${fixture.id}`,
              fixture.source,
            );
            result.renderAccept = true;
          } catch (error) {
            result.renderAccept = false;
            result.renderReject = errorValue(error);
          }
          out.push(result);
        }
        return out;
      },
      { fixtures: cases, moduleUrl, suffix },
    );

  const first = await run("a");
  const second = await run("b");
  if (JSON.stringify(first) !== JSON.stringify(second)) {
    throw new Error("xychart grammar oracle is not stable across two runs");
  }

  // Node-side classification is intentionally repeated as a guard against a
  // typo in the browser evaluator's transport-only classifier.
  for (const item of first) {
    for (const rejected of [item.reject, item.renderReject].filter(Boolean)) {
      if (classify(rejected.message) !== rejected.class) {
        throw new Error(`${item.id}: inconsistent reject classification`);
      }
    }
  }

  const root = {
    upstream: {
      package: "mermaid",
      version: packageJson.version,
      moduleSha256: createHash("sha256")
        .update(fs.readFileSync(moduleFile))
        .digest("hex"),
      generator: "scripts/generate_mermaid_xychart_grammar_fixture.mjs",
      doubleRunStable: true,
    },
    oracle:
      "xychart source-entry detector/parser/render verdicts plus normalized xychartDb state",
    cases: first,
  };
  const body = JSON.stringify(root);
  root.fixtureSha256 = createHash("sha256").update(body).digest("hex");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(root, null, 2)}\n`);
  const parsed = first.filter((item) => item.parseAccept).length;
  const rendered = first.filter((item) => item.renderAccept).length;
  console.log(
    `Wrote ${output}: ${first.length} cases, ${parsed} parser accepts, ` +
      `${rendered} render accepts, sha256 ${root.fixtureSha256}`,
  );
} finally {
  await browser.close();
}
