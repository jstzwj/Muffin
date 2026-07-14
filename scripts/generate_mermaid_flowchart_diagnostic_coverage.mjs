import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";
import { spawn } from "node:child_process";

const fixturePath = path.resolve(
  process.argv[2] ?? path.join("tests", "fixtures", "mermaid", "flowchart-differential-fuzz.json"),
);
const nativeExecutable = path.resolve(
  process.argv[3] ?? path.join("build", "Release", "MuffinMermaidFlowchartDifferentialFuzzTest.exe"),
);
const conanRun = path.resolve("build", "generators", "conanrun.bat");

function conanEnvironment() {
  const launcher = fs.readFileSync(conanRun, "utf8");
  const match = launcher.match(/^call\s+"%~dp0[\\/]([^"\r\n]+)"/im);
  if (!match) throw new Error(`Could not resolve Conan environment from ${conanRun}`);
  const environmentFile = path.join(path.dirname(conanRun), match[1]);
  const result = { ...process.env };
  const assigned = new Set();
  const environmentValue = (name) => {
    const key = Object.keys(result).find((candidate) => candidate.toUpperCase() === name.toUpperCase());
    return key ? result[key] : "";
  };
  for (const line of fs.readFileSync(environmentFile, "utf8").split(/\r?\n/)) {
    const assignment = line.match(/^\s*set\s+"([^=]+)=(.*)"\s*$/i);
    if (!assignment || assigned.has(assignment[1].toUpperCase())) continue;
    const key = assignment[1];
    const value = assignment[2].replace(/%([^%]+)%/g, (_, name) => environmentValue(name));
    for (const existing of Object.keys(result))
      if (existing !== key && existing.toUpperCase() === key.toUpperCase()) delete result[existing];
    result[key] = value;
    assigned.add(key.toUpperCase());
  }
  return result;
}

function createOracle() {
  const child = spawn(nativeExecutable, ["--oracle"], {
    cwd: path.dirname(nativeExecutable),
    env: conanEnvironment(),
    stdio: ["pipe", "pipe", "inherit"],
  });
  const lines = readline.createInterface({ input: child.stdout });
  const pending = [];
  let exitError = null;
  lines.on("line", (line) => {
    const request = pending.shift();
    if (!request) return;
    try { request.resolve(JSON.parse(line)); }
    catch (error) { request.reject(new Error(`Invalid native oracle response: ${line}`, { cause: error })); }
  });
  child.on("exit", (code) => {
    exitError = new Error(`Native oracle exited with code ${code}`);
    for (const request of pending.splice(0)) request.reject(exitError);
  });
  return {
    query(source) {
      if (exitError) return Promise.reject(exitError);
      return new Promise((resolve, reject) => {
        pending.push({ resolve, reject });
        child.stdin.write(`${JSON.stringify({ source })}\n`);
      });
    },
    close() { child.stdin.end(); lines.close(); },
  };
}

const requiredCodes = [
  "unexpected-character", "invalid-direction", "unterminated-string",
  "unterminated-shape-data", "unterminated-callback-arguments", "unexpected-token",
  "missing-token", "missing-value", "missing-list-item", "invalid-node",
  "invalid-metadata", "missing-link-endpoint", "missing-header", "unclosed-subgraph",
  "unexpected-end",
];

if (!fs.existsSync(nativeExecutable)) throw new Error(`Native oracle not found: ${nativeExecutable}`);
const fixture = JSON.parse(fs.readFileSync(fixturePath, "utf8"));
const oracle = createOracle();
try {
  const results = await Promise.all(fixture.negativeCases.map((item) => oracle.query(item.source)));
  const codeCounts = new Map();
  const matrixCounts = new Map();
  const exclusions = [];
  let stableLineCount = 0;
  let stableColumnCount = 0;
  for (let index = 0; index < fixture.negativeCases.length; ++index) {
    const item = fixture.negativeCases[index];
    const native = results[index];
    if (process.env.MUFFIN_DIAGNOSTIC_DEBUG === item.id)
      console.log(item.id, native);
    if (native.ok) throw new Error(`Native unexpectedly accepted diagnostic case ${item.id}`);
    codeCounts.set(native.code, (codeCounts.get(native.code) ?? 0) + 1);
    const upstream = item.upstreamError;
    const position = upstream.stage === "parser" && native.stage === "lexer"
      ? upstream.refinement : upstream.normalized;
    upstream.compareLine = native.line === position.line;
    upstream.compareColumn = upstream.compareLine && native.column === position.column;
    if (upstream.compareLine) ++stableLineCount;
    if (upstream.compareColumn) ++stableColumnCount;
    if (!upstream.compareColumn) {
      exclusions.push({
        id: item.id,
        basis: position.basis,
        native: { stage: native.stage, code: native.code, line: native.line, column: native.column },
        normalized: { line: position.line, column: position.column },
      });
    }
    const productions = item.originProductions.length > 0 ? item.originProductions : ["curated"];
    for (const production of productions) {
      const key = `${production}|${item.operator}|${upstream.stage}|${native.code}`;
      matrixCounts.set(key, (matrixCounts.get(key) ?? 0) + 1);
    }
  }
  const missingCodes = requiredCodes.filter((code) => !codeCounts.has(code));
  if (missingCodes.length > 0)
    throw new Error(
      `Diagnostic corpus is missing native codes: ${missingCodes.join(", ")}; observed: ` +
      [...codeCounts].map(([code, count]) => `${code}=${count}`).sort().join(", "),
    );
  const sortedObject = (map) => Object.fromEntries([...map].sort(([left], [right]) =>
    left.localeCompare(right)));
  fixture.diagnosticCoverage = {
    requiredCodes,
    codeCounts: sortedObject(codeCounts),
    matrixEntryCount: matrixCounts.size,
    matrixCounts: sortedObject(matrixCounts),
    stableLineCount,
    stableColumnCount,
    positionExclusions: exclusions,
  };
  fs.writeFileSync(fixturePath, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(
    `Updated ${fixturePath}: ${matrixCounts.size} matrix entries, ` +
    `${stableLineCount} stable lines, ${stableColumnCount} stable columns`,
  );
} finally {
  oracle.close();
}
