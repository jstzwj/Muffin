import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const sequenceFixturePath = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "sequence-db.json"),
);
const output = path.resolve(
  process.argv[4] ?? path.join("tests", "fixtures", "mermaid", "sequence-differential-fuzz.json"),
);
const chrome = process.argv[5] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
const sequenceFixture = JSON.parse(fs.readFileSync(sequenceFixturePath, "utf8"));
if (packageJson.version !== "11.16.0" || sequenceFixture.upstream.version !== packageJson.version)
  throw new Error("Sequence differential inputs must use Mermaid 11.16.0");

const productionFor = (lhs, ordinal = 0) => {
  const values = sequenceFixture.productions.filter((production) =>
    production.lhs === lhs && production.status === "covered");
  if (!values.length) throw new Error(`No covered sequence production for ${lhs}`);
  return values[Math.min(ordinal, values.length - 1)];
};
const position = (source, offset) => {
  const prefix = source.slice(0, Math.max(0, offset));
  const lines = prefix.split("\n");
  return { offset, line: lines.length, column: lines.at(-1).length };
};
const mutation = (id, operator, lhs, source, anchor, extra = {}) => {
  const production = productionFor(lhs, extra.productionOrdinal ?? 0);
  return {
    id,
    operator,
    targetProduction: production.id,
    targetLhs: lhs,
    targetRhsLength: production.rhsLength,
    source,
    mutationPosition: position(source, Math.max(0, anchor)),
    diagnosticPosition: position(source, Math.max(0, extra.diagnosticOffset ?? anchor)),
    ...extra,
  };
};

const candidates = [];
{
  const source = "A->>B:x";
  candidates.push(mutation("delete-header", "delete-required-terminal", "start", source, 0));
}
{
  const source = "sequenceDiagram\nA=>B:bad";
  candidates.push(mutation("replace-arrow-class", "replace-terminal-class", "signaltype",
    source, source.indexOf("=")));
}
{
  const source = "sequenceDiagram\nNote over A,,B:bad";
  candidates.push(mutation("duplicate-actor-separator", "duplicate-separator", "actor_pair",
    source, source.indexOf(",,") + 1));
}
{
  const source = "sequenceDiagram\nloop open\nA->>B:x";
  candidates.push(mutation("truncate-loop", "truncate-recursive-production", "statement",
    source, source.indexOf("loop")));
}
{
  const source = 'sequenceDiagram\nparticipant A@{ "type": "actor"\nA->>B:x';
  candidates.push(mutation("break-config-delimiter", "break-paired-delimiter", "config_object",
    source, source.indexOf("@{")));
}
{
  const source = "sequenceDiagram\nend";
  candidates.push(mutation("insert-end-at-nullable-boundary", "insert-nullable-boundary-token",
    "document", source, source.indexOf("end")));
}
{
  const source = "sequenceDiagram\nA->>:missing target";
  candidates.push(mutation("delete-required-actor", "delete-required-terminal", "signal",
    source, source.indexOf(":")));
}
{
  const source = "sequenceDiagram\nA->>B missing colon";
  candidates.push(mutation("delete-message-colon", "delete-required-terminal", "signal",
    source, source.indexOf(" missing")));
}
for (const [id, keyword, lhs] of [
  ["orphan-else", "else orphan", "else_sections"],
  ["orphan-and", "and orphan", "par_sections"],
  ["orphan-option", "option orphan", "option_sections"],
]) {
  const source = `sequenceDiagram\n${keyword}`;
  candidates.push(mutation(id, "insert-nullable-boundary-token", lhs,
    source, source.indexOf(keyword)));
}
{
  const source = "sequenceDiagram\ndeactivate A";
  candidates.push(mutation("inactive-deactivate", "replace-terminal-class", "statement",
    source, source.indexOf("A")));
}
{
  const source = "sequenceDiagram\ncreate participant B\nA->>C:no";
  candidates.push(mutation("create-target-mismatch", "replace-terminal-class", "participant_statement",
    source, source.lastIndexOf("C")));
}
{
  const source = "sequenceDiagram\nparticipant B\ndestroy B\nA->>C:no";
  candidates.push(mutation("destroy-target-mismatch", "replace-terminal-class", "statement",
    source, source.lastIndexOf("C")));
}
{
  const source = "sequenceDiagram\nparticipant B\ncreate participant B";
  candidates.push(mutation("duplicate-created-participant", "duplicate-separator", "participant_statement",
    source, source.lastIndexOf("B")));
}

const nativeCodes = {
  "delete-header": "missing-header",
  "replace-arrow-class": "unexpected-token",
  "duplicate-actor-separator": "unexpected-token",
  "truncate-loop": "missing-end",
  "break-config-delimiter": "unexpected-token",
  "insert-end-at-nullable-boundary": "unexpected-end",
  "delete-required-actor": "unexpected-token",
  "delete-message-colon": "unexpected-token",
  "orphan-else": "unexpected-token",
  "orphan-and": "unexpected-token",
  "orphan-option": "unexpected-token",
  "inactive-deactivate": "inactive-participant",
  "create-target-mismatch": "invalid-create-message",
  "destroy-target-mismatch": "invalid-destroy-message",
  "duplicate-created-participant": "duplicate-participant",
};
for (const candidate of candidates) candidate.expectedNativeCode = nativeCodes[candidate.id];

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
        : message.includes("Lexical error") ? "lexer"
        : message.includes("Parse error") ? "parser" : "semantic";
      return {
        class: stage === "detector" ? "detection" : stage === "semantic" ? "semantic" : "syntax",
        stage,
        code: stage === "detector" ? "missing-header"
          : stage === "lexer" ? "lexical-error"
          : stage === "parser" ? "parse-error" : "semantic-error",
        token: error?.hash?.token ?? "",
        raw: {
          line: Number(error?.hash?.loc?.first_line ?? 0),
          column: Number(error?.hash?.loc?.first_column ?? 0),
        },
        summary: message.split("\n", 1)[0],
      };
    };
    const output = [];
    for (const fixture of candidates) {
      try {
        await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
        throw new Error(`Mutation unexpectedly succeeded: ${fixture.id}`);
      } catch (error) {
        if (String(error).includes("Mutation unexpectedly succeeded")) throw error;
        output.push({ ...fixture, upstreamError: classify(error) });
      }
    }
    return output;
  }, { candidates, mermaidModule });

  const positionRules = {
    detector: "detector-source-start",
    semantic: "semantic-offending-token",
    "delete-required-terminal": "missing-terminal-insertion-point",
    "replace-terminal-class": "replacement-token-start",
    "duplicate-separator": "duplicate-token-start",
    "truncate-recursive-production": "unclosed-production-start",
    "break-paired-delimiter": "opening-delimiter-start",
    "insert-nullable-boundary-token": "inserted-token-start",
  };
  for (const item of negativeCases) {
    item.upstreamError.normalized = item.diagnosticPosition;
    item.upstreamError.positionRule = item.upstreamError.stage === "detector"
      ? positionRules.detector
      : item.upstreamError.stage === "semantic"
        ? positionRules.semantic
        : positionRules[item.operator];
    if (!item.upstreamError.positionRule) throw new Error(`No position normalizer for ${item.id}`);
  }

  const operators = Object.fromEntries([...new Set(negativeCases.map((item) => item.operator))]
    .sort().map((operator) => [operator, negativeCases.filter((item) => item.operator === operator).length]));
  const stages = Object.fromEntries([...new Set(negativeCases.map((item) => item.upstreamError.stage))]
    .sort().map((stage) => [stage, negativeCases.filter((item) => item.upstreamError.stage === stage).length]));
  const matrix = {};
  for (const item of negativeCases) {
    const key = `${item.targetProduction}:${item.targetRhsLength}|${item.operator}|${item.upstreamError.stage}|${item.expectedNativeCode}`;
    matrix[key] = (matrix[key] ?? 0) + 1;
  }
  const normalizedPositions = Object.fromEntries([...new Set(negativeCases.map((item) => item.upstreamError.positionRule))]
    .sort().map((rule) => [rule, negativeCases.filter((item) => item.upstreamError.positionRule === rule).length]));
  const payload = {
    upstream: { package: "mermaid", version: packageJson.version },
    generator: {
      name: "production-aware-sequence-mutation-fuzzer",
      caseCount: negativeCases.length,
      operatorCounts: operators,
      stageCounts: stages,
      matrixCounts: matrix,
      positionRuleCounts: normalizedPositions,
      rawLocationCounts: {
        lines: negativeCases.filter((item) => item.upstreamError.raw.line > 0).length,
        columns: negativeCases.filter((item) => item.upstreamError.raw.column > 0).length,
      },
      unreachableStages: {
        lexer: "sequenceDiagram.jison has a final catch-all INVALID rule; invalid input reaches parser recovery",
      },
      requiredNativeCodes: [...new Set(negativeCases.map((item) => item.expectedNativeCode))].sort(),
    },
    negativeCases,
  };
  payload.fixtureDigest = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${negativeCases.length} sequence mutation cases to ${output}`);
} finally {
  await browser.close();
}
