import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ??
  path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const stateFixturePath = path.resolve(process.argv[3] ??
  path.join("tests", "fixtures", "mermaid", "state-db.json"));
const output = path.resolve(process.argv[4] ??
  path.join("tests", "fixtures", "mermaid", "state-differential-fuzz.json"));
const chrome = process.argv[5] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
const stateFixture = JSON.parse(fs.readFileSync(stateFixturePath, "utf8"));
if (pkg.version !== "11.16.0" || stateFixture.upstream.version !== pkg.version)
  throw new Error("State differential inputs must use Mermaid 11.16.0");
const productions = stateFixture.upstream.grammar.productions;
const position = (source, offset) => {
  const lines = source.slice(0, Math.max(0, offset)).split("\n");
  return { offset, line: lines.length, column: lines.at(-1).length };
};
const mutation = (id, operator, productionId, source, diagnosticOffset,
                  expectedNativeCode) => {
  const production = productions.find((item) => item.id === productionId);
  if (!production) throw new Error(`Unknown state production ${productionId}`);
  return { id, operator, targetProduction: productionId,
    targetSymbol: production.symbol, targetRhsLength: production.rhsLength,
    source, diagnosticPosition: position(source, diagnosticOffset), expectedNativeCode };
};
const candidates = [];
{
  const source = "A --> B";
  candidates.push(mutation("delete-header", "delete-required-terminal", 1,
    source, 0, "missing-header"));
}
{
  const source = "stateDiagram-v2\nA -->";
  candidates.push(mutation("delete-relation-target", "delete-required-terminal", 14,
    source, source.length, "unexpected-token"));
}
{
  const source = "stateDiagram-v2\nA --> --> B";
  candidates.push(mutation("duplicate-arrow", "duplicate-separator", 14,
    source, source.lastIndexOf("-->"), "unexpected-token"));
}
{
  const source = 'stateDiagram-v2\nstate "Name" wrong A';
  candidates.push(mutation("replace-alias-as", "replace-terminal-class", 20,
    source, source.indexOf("wrong"), "unexpected-token"));
}
{
  const source = "stateDiagram-v2\nstate A {\nB";
  candidates.push(mutation("truncate-composite", "truncate-recursive-production", 19,
    source, source.length, "missing-closing-brace"));
}
{
  const source = "stateDiagram-v2\nnote left of A\ntext";
  candidates.push(mutation("break-note-delimiter", "break-paired-delimiter", 27,
    source, source.length, "missing-end-note"));
}
{
  const source = "stateDiagram-v2\n{";
  candidates.push(mutation("insert-root-brace", "insert-nullable-boundary-token", 4,
    source, source.indexOf("{"), "unexpected-token"));
}
const { default: puppeteer } = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
const browser = await puppeteer.launch({ headless: true, executablePath: chrome,
                                         args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const negativeCases = await page.evaluate(async ({ candidates, mermaidModule }) => {
    const { default: mermaid } = await import(mermaidModule);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const classify = (error) => {
      const message = String(error?.message ?? error);
      const stage = message.includes("No diagram type detected") ? "detector"
        : message.includes("Lexical error") ? "lexer" : "parser";
      return { stage, class: stage === "detector" ? "detection" : "syntax",
        token: error?.hash?.token ?? "",
        raw: { line: Number(error?.hash?.loc?.first_line ?? 0),
               column: Number(error?.hash?.loc?.first_column ?? 0) },
        summary: message.split("\n", 1)[0] };
    };
    const result = [];
    for (const fixture of candidates) {
      try {
        await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
        throw new Error(`Mutation unexpectedly succeeded: ${fixture.id}`);
      } catch (error) {
        if (String(error).includes("Mutation unexpectedly succeeded")) throw error;
        result.push({ ...fixture, upstreamError: classify(error) });
      }
    }
    return result;
  }, { candidates, mermaidModule });
  const positionRules = {
    "delete-required-terminal": "missing-terminal-insertion-point",
    "replace-terminal-class": "replacement-token-start",
    "duplicate-separator": "duplicate-token-start",
    "truncate-recursive-production": "truncated-input-end",
    "break-paired-delimiter": "unclosed-delimiter-input-end",
    "insert-nullable-boundary-token": "inserted-token-start",
  };
  for (const item of negativeCases) {
    item.upstreamError.normalized = item.diagnosticPosition;
    item.upstreamError.positionRule = item.upstreamError.stage === "detector"
      ? "detector-source-start" : positionRules[item.operator];
  }
  const countBy = (selector) => Object.fromEntries(
    [...new Set(negativeCases.map(selector))].sort().map((key) =>
      [key, negativeCases.filter((item) => selector(item) === key).length]));
  const payload = {
    upstream: { package: "mermaid", version: pkg.version },
    generator: { name: "production-aware-state-mutation-fuzzer",
      caseCount: negativeCases.length,
      operatorCounts: countBy((item) => item.operator),
      stageCounts: countBy((item) => item.upstreamError.stage),
      positionRuleCounts: countBy((item) => item.upstreamError.positionRule),
      requiredNativeCodes: [...new Set(negativeCases.map((item) => item.expectedNativeCode))].sort(),
    },
    negativeCases,
  };
  payload.fixtureDigest = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${negativeCases.length} state mutation cases to ${output}`);
  console.log(`fixtureDigest=${payload.fixtureDigest}`);
} finally {
  await browser.close();
}
