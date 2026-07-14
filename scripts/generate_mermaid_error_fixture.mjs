import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Level-1 error-category golden (milestone G1). For each malformed input the
// generator records whether UPSTREAM mermaid 11.16.0 also rejects it
// (`upstreamError`), plus the CURATED native contract (`expectedCategory` +
// `expectedLine`). The native test (MermaidParserErrorTest) asserts the native
// parser throws FlowchartParseError with the expected category (+ line where
// determinable). Messages may diverge between JS and C++; the contract is the
// CATEGORY + position (docs/mermaid-flowchart-remaining-plan.md §10).
//
// `upstreamError: false` on a case the native parser rejects is INTENTIONAL for
// milestone H (resource limits / stricter security) — Muffin adds protection
// upstream lacks. For pure-syntax cases upstreamError should be true.

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "flowchart-errors.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

// Curated case table. `expectedCategory` mirrors muffin::FlowchartErrorCategory.
// `options` (optional) overrides FlowchartParseOptions for the native call.
const cases = [
  { id: "no-header", source: "This is not a mermaid diagram at all.", expectedCategory: "MissingHeader", expectedStage: "detector", expectedCode: "missing-header", expectedLine: 1, expectedColumn: 1 },
  { id: "unclosed-subgraph", source: "flowchart TB\nsubgraph S[Group]\nA --> B", expectedCategory: "UnclosedSubgraph", expectedStage: "parser", expectedCode: "unclosed-subgraph", expectedLine: 3, expectedColumn: 8 },
  { id: "unexpected-end", source: "flowchart TB\nend", expectedCategory: "UnexpectedEnd", expectedStage: "parser", expectedCode: "unexpected-end", expectedLine: 2, expectedColumn: 1 },
  { id: "linkstyle-oob", source: "flowchart TB\nA --> B\nlinkStyle 5 stroke:red", expectedCategory: "LinkStyleBounds", expectedStage: "semantic", expectedCode: "link-style-bounds", expectedLine: 3, expectedColumn: 11 },
  { id: "bad-classdef", source: "flowchart TB\nclassDef x", expectedCategory: "InvalidDirective", expectedStage: "parser", expectedCode: "missing-token", expectedLine: 2, expectedColumn: 11 },
  // Resource-limit cases: upstream has NO such limits (parses fine); native
  // throws LimitExceeded. Muffin's protection, intentionally stricter.
  { id: "text-limit", source: "flowchart TB\nA --> B", expectedCategory: "LimitExceeded", expectedStage: "resource", expectedCode: "limit-exceeded", expectedLine: 0, expectedColumn: 0, options: { maxTextSize: 8 } },
  { id: "edge-limit", source: "flowchart TB\nA --> B\nC --> D", expectedCategory: "LimitExceeded", expectedStage: "resource", expectedCode: "limit-exceeded", expectedLine: 3, expectedColumn: 0, options: { maxEdges: 1 } },
  // FlowchartLimits overrides (milestone H1): upstream parses fine, native
  // throws LimitExceeded at the growth site.
  { id: "vertex-limit", source: "flowchart TB\nA --> B", expectedCategory: "LimitExceeded", expectedStage: "resource", expectedCode: "limit-exceeded", expectedLine: 2, expectedColumn: 0, limits: { maxVertices: 1 } },
  { id: "subgraph-depth-limit", source: "flowchart TB\nsubgraph S1\nsubgraph S2\nend\nend", expectedCategory: "LimitExceeded", expectedStage: "resource", expectedCode: "limit-exceeded", expectedLine: 3, expectedColumn: 0, limits: { maxSubgraphDepth: 1 } },
  { id: "nodeid-length-limit", source: "flowchart TB\nABC", expectedCategory: "LimitExceeded", expectedStage: "resource", expectedCode: "limit-exceeded", expectedLine: 2, expectedColumn: 0, limits: { maxNodeIdLength: 2 } },
];

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const observed = await page.evaluate(
    async ({ cases, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
      const results = [];
      for (const fixture of cases) {
        let upstreamError = null;
        try {
          await mermaid.parse(fixture.source);
        } catch (error) {
          const message = String(error?.message ?? error);
          const stage = message.includes("No diagram type detected") ? "detector"
            : message.includes("Lexical error") ? "lexer"
            : message.includes("Parse error") ? "parser" : "semantic";
          const hashLine = error?.hash?.loc?.first_line;
          const hashColumn = error?.hash?.loc?.first_column;
          const messageLine = message.match(/(?:on line|line:)\s*(\d+)/i)?.[1];
          upstreamError = {
            stage,
            line: Number(hashLine ?? messageLine ?? (stage === "detector" ? 1 : 0)),
            column: Number(hashColumn === undefined ? (stage === "detector" ? 1 : 0) : hashColumn + 1),
            summary: message.split("\n", 1)[0],
          };
        }
        results.push({ id: fixture.id, upstreamError });
      }
      return results;
    },
    { cases, mermaidModule },
  );
  const merged = cases.map((fixture, index) => {
    const { id, upstreamError } = observed[index];
    return {
      id,
      source: fixture.source,
      upstreamError,
      expectedCategory: fixture.expectedCategory,
      expectedStage: fixture.expectedStage,
      expectedCode: fixture.expectedCode,
      expectedLine: fixture.expectedLine,
      expectedColumn: fixture.expectedColumn,
      ...(fixture.options ? { options: fixture.options } : {}),
      ...(fixture.limits ? { limits: fixture.limits } : {}),
    };
  });
  const fixture = { upstream: { package: "mermaid", version: packageJson.version }, cases: merged };
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(`Wrote ${output} (${merged.length} error cases)`);
} finally {
  await browser.close();
}
