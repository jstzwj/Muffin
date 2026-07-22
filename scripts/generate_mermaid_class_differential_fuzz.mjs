import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const classFixturePath = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "class-db.json"),
);
const output = path.resolve(
  process.argv[4] ?? path.join("tests", "fixtures", "mermaid", "class-differential-fuzz.json"),
);
const chrome = process.argv[5] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
const classFixture = JSON.parse(fs.readFileSync(classFixturePath, "utf8"));
if (packageJson.version !== "11.16.0" || classFixture.upstream.version !== packageJson.version)
  throw new Error("Class differential inputs must use Mermaid 11.16.0");

const productionFor = (lhs, ordinal = 0) => {
  const values = classFixture.productions.filter((production) =>
    production.lhs === lhs && production.status === "covered");
  if (!values.length) throw new Error(`No covered class production for ${lhs}`);
  return values[Math.min(ordinal, values.length - 1)];
};
const position = (source, offset) => {
  const lines = source.slice(0, Math.max(0, offset)).split("\n");
  return { offset, line: lines.length, column: lines.at(-1).length };
};
const mutation = (id, operator, lhs, source, anchor, expectedNativeCode,
                  diagnosticOffset = anchor, productionOrdinal = 0) => {
  const production = productionFor(lhs, productionOrdinal);
  return {
    id,
    operator,
    targetProduction: production.id,
    targetLhs: lhs,
    targetRhsLength: production.rhsLength,
    source,
    mutationPosition: position(source, anchor),
    diagnosticPosition: position(source, diagnosticOffset),
    expectedNativeCode,
  };
};

const candidates = [];
{
  const source = "class A";
  candidates.push(mutation("delete-header", "delete-required-terminal", "graphConfig",
    source, 0, "missing-header"));
}
{
  const source = "classDiagram\nclass";
  candidates.push(mutation("delete-class-name", "delete-required-terminal", "classIdentifier",
    source, source.length, "unexpected-token", source.length));
}
{
  const source = "classDiagram\nclass A @";
  candidates.push(mutation("replace-token-with-invalid-character", "replace-terminal-class",
    "classStatement", source, source.indexOf("@"), "unexpected-token"));
}
{
  const source = "classDiagram\nclass A::::::hot";
  candidates.push(mutation("duplicate-style-separator", "duplicate-separator", "classStatement",
    source, source.indexOf(":::", source.indexOf(":::") + 3), "unexpected-token",
    source.indexOf(":::", source.indexOf(":::") + 3)));
}
{
  const source = "classDiagram\nclass A {\n+value";
  candidates.push(mutation("truncate-class-body", "truncate-recursive-production", "classStatement",
    source, source.indexOf("{"), "missing-closing-brace", source.length));
}
{
  const source = "classDiagram\nnamespace N {\nclass A";
  candidates.push(mutation("truncate-namespace", "truncate-recursive-production", "namespaceStatement",
    source, source.indexOf("{"), "missing-closing-brace", source.length));
}
{
  const source = "classDiagram\nclass A[open";
  candidates.push(mutation("break-class-label", "break-paired-delimiter", "classLabel",
    source, source.indexOf("["), "invalid-class-label", source.indexOf("[")));
}
{
  const source = "classDiagram\n}";
  candidates.push(mutation("insert-close-at-nullable-boundary", "insert-nullable-boundary-token",
    "statements", source, source.indexOf("}"), "unexpected-token"));
}
{
  const source = "classDiagram\nA -->";
  candidates.push(mutation("delete-relation-target", "delete-required-terminal", "relationStatement",
    source, source.length, "missing-relation-target", source.length));
}
{
  const source = "classDiagram\nA == B";
  candidates.push(mutation("replace-line-type", "replace-terminal-class", "lineType",
    source, source.indexOf("=="), "unexpected-token", source.indexOf("A")));
}

const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")),
);
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
      return {
        class: stage === "detector" ? "detection" : "syntax",
        stage,
        code: stage === "detector" ? "missing-header"
          : stage === "lexer" ? "lexical-error" : "parse-error",
        token: error?.hash?.token ?? "",
        raw: {
          line: Number(error?.hash?.loc?.first_line ?? 0),
          column: Number(error?.hash?.loc?.first_column ?? 0),
        },
        summary: message.split("\n", 1)[0],
      };
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
    detector: "detector-source-start",
    lexer: "invalid-character-start",
    "delete-required-terminal": "missing-terminal-insertion-point",
    "replace-terminal-class": "replacement-token-start",
    "duplicate-separator": "duplicate-token-start",
    "truncate-recursive-production": "end-of-input",
    "break-paired-delimiter": "opening-delimiter-start",
    "insert-nullable-boundary-token": "inserted-token-start",
  };
  for (const item of negativeCases) {
    item.upstreamError.normalized = item.diagnosticPosition;
    item.upstreamError.positionRule = item.upstreamError.stage === "detector"
      ? positionRules.detector : item.upstreamError.stage === "lexer"
        ? positionRules.lexer : positionRules[item.operator];
    if (!item.upstreamError.positionRule) throw new Error(`No position rule for ${item.id}`);
  }
  const counts = (selector) => Object.fromEntries([...new Set(negativeCases.map(selector))]
    .sort().map((key) => [key, negativeCases.filter((item) => selector(item) === key).length]));
  const matrix = {};
  for (const item of negativeCases) {
    const key = `${item.targetProduction}:${item.targetRhsLength}|${item.operator}|${item.upstreamError.stage}|${item.expectedNativeCode}`;
    matrix[key] = (matrix[key] ?? 0) + 1;
  }
  const payload = {
    upstream: { package: "mermaid", version: packageJson.version },
    generator: {
      name: "production-aware-class-mutation-fuzzer",
      caseCount: negativeCases.length,
      operatorCounts: counts((item) => item.operator),
      stageCounts: counts((item) => item.upstreamError.stage),
      matrixCounts: matrix,
      positionRuleCounts: counts((item) => item.upstreamError.positionRule),
      rawLocationCounts: {
        lines: negativeCases.filter((item) => item.upstreamError.raw.line > 0).length,
        columns: negativeCases.filter((item) => item.upstreamError.raw.column > 0).length,
      },
      requiredNativeCodes: [...new Set(negativeCases.map((item) => item.expectedNativeCode))].sort(),
    },
    negativeCases,
  };
  payload.fixtureDigest = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${negativeCases.length} class mutation cases to ${output}`);
} finally {
  await browser.close();
}
