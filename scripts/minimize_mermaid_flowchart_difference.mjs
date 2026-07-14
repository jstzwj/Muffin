import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";
import { spawn } from "node:child_process";
import { pathToFileURL } from "node:url";

if (process.argv[2] === "--self-test") {
  const input = "flowchart LR\nnoise one\nkeep BUG here\nnoise two\n";
  const reduced = await minimizeLines(input, async (source) => source.includes("BUG"));
  if (!reduced.includes("BUG") || reduced.includes("noise")) throw new Error("minimizer self-test failed");
  console.log("Flowchart differential minimizer self-test passed");
  process.exit(0);
}

const sourcePath = process.argv[2];
if (!sourcePath) {
  throw new Error(
    "Usage: node scripts/minimize_mermaid_flowchart_difference.mjs <source.mmd> " +
      "[mermaid-root] [native-oracle.exe] [output.mmd] [chrome.exe]",
  );
}
const mermaidRoot = path.resolve(
  process.argv[3] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const nativeExecutable = path.resolve(
  process.argv[4] ?? path.join("build", "Release", "MuffinMermaidFlowchartDifferentialFuzzTest.exe"),
);
const outputPath = path.resolve(process.argv[5] ?? `${sourcePath}.min.mmd`);
const chrome = process.argv[6] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const conanRun = path.resolve("build", "generators", "conanrun.bat");
const original = fs.readFileSync(sourcePath, "utf8").replace(/\r\n?/g, "\n");

function canonical(value) {
  if (Array.isArray(value)) return value.map(canonical);
  if (value && typeof value === "object") {
    return Object.fromEntries(Object.keys(value).sort().map((key) => [key, canonical(value[key])]));
  }
  return value;
}

function differs(upstream, native) {
  if (upstream.ok && !native.ok) return `native-error:${native.class ?? native.category}`;
  if (!upstream.ok && native.ok) return `upstream-error:${upstream.class}`;
  if (!upstream.ok) {
    if (upstream.class !== native.class)
      return `error-class:${upstream.class}:${native.class ?? native.category}`;
    if (upstream.stage && native.stage && upstream.stage !== native.stage)
      return `error-stage:${upstream.stage}:${native.stage}`;
    if (upstream.line > 0 && native.line > 0 && upstream.line !== native.line)
      return "error-line";
    if (upstream.column > 0 && native.column > 0 && upstream.column !== native.column)
      return "error-column";
    return null;
  }
  return JSON.stringify(canonical(upstream.ast)) !== JSON.stringify(canonical(native.ast))
    ? "ast-mismatch"
    : null;
}

function conanEnvironment() {
  const launcher = fs.readFileSync(conanRun, "utf8");
  const match = launcher.match(/^call\s+"%~dp0[\\/]([^"\r\n]+)"/im);
  if (!match) throw new Error(`Could not resolve Conan run environment from ${conanRun}`);
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

function createNativeOracle() {
  const environment = conanEnvironment();
  const child = spawn(nativeExecutable, ["--oracle"], {
    cwd: path.resolve("build"),
    env: environment,
    stdio: ["pipe", "pipe", "inherit"],
  });
  const lines = readline.createInterface({ input: child.stdout });
  const pending = [];
  let exitError = null;
  lines.on("line", (line) => {
    const request = pending.shift();
    if (!request) return;
    try {
      request.resolve(JSON.parse(line));
    } catch (error) {
      request.reject(new Error(`Invalid native oracle response: ${line}`, { cause: error }));
    }
  });
  child.on("exit", (code) => {
    exitError = new Error(`Native oracle exited with code ${code}`);
    for (const request of pending.splice(0)) request.reject(exitError);
  });
  child.stdin.on("error", (error) => {
    exitError = new Error("Native oracle stdin failed", { cause: error });
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
    close() {
      child.stdin.end();
      lines.close();
    },
  };
}

async function ddmin(items, build, predicate) {
  let current = [...items];
  let granularity = 2;
  while (current.length > 0) {
    const chunkSize = Math.ceil(current.length / granularity);
    let reduced = false;
    for (let start = 0; start < current.length; start += chunkSize) {
      const candidate = current.slice(0, start).concat(current.slice(start + chunkSize));
      if (await predicate(build(candidate))) {
        current = candidate;
        granularity = Math.max(2, granularity - 1);
        reduced = true;
        break;
      }
    }
    if (!reduced) {
      if (granularity >= current.length) break;
      granularity = Math.min(current.length, granularity * 2);
    }
  }
  return current;
}

async function minimizeLines(source, predicate) {
  const lines = source.split("\n");
  const headerIndex = lines.findIndex((line) => /^\s*(?:flowchart|graph)\b/.test(line));
  if (headerIndex < 0) return source;
  const header = lines[headerIndex];
  const body = lines.slice(headerIndex + 1).filter((line) => line.length > 0);
  const minimized = await ddmin(body, (items) => [header, ...items].join("\n"), predicate);
  return [header, ...minimized].join("\n");
}

async function minimizeCharacters(source, predicate) {
  const lines = source.split("\n");
  for (let index = 1; index < lines.length; ++index) {
    const characters = [...lines[index]];
    const minimized = await ddmin(
      characters,
      (items) => [...lines.slice(0, index), items.join(""), ...lines.slice(index + 1)].join("\n"),
      predicate,
    );
    lines[index] = minimized.join("");
  }
  return lines.join("\n");
}

async function simplify(source, predicate) {
  let current = source;
  const transforms = [
    (value) => value.replace(/"`[^`]*`"/g, '"`x`"'),
    (value) => value.replace(/"[^"\n]*"/g, '"x"'),
    (value) => value.replace(/@\{[^}]*\}/g, ""),
    (value) => value.replace(/\[[^\]\n]*\]/g, "[x]"),
    (value) => value.replace(/\([^()\n]*\)/g, "(x)"),
    (value) => value.replace(/[ \t]+/g, " "),
  ];
  for (const transform of transforms) {
    const candidate = transform(current);
    if (candidate !== current && await predicate(candidate)) current = candidate;
  }
  return current;
}

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") throw new Error(`Expected Mermaid 11.16.0, got ${packageJson.version}`);
if (!fs.existsSync(nativeExecutable)) throw new Error(`Native oracle not found: ${nativeExecutable}`);
if (!fs.existsSync(conanRun)) throw new Error(`Conan run environment not found: ${conanRun}`);

const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")),
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
const native = createNativeOracle();
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  await page.evaluate(async (moduleUrl) => {
    const { default: mermaid } = await import(moduleUrl);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    globalThis.__flowchartMermaid = mermaid;
  }, mermaidModule);

  const cache = new Map();
  let expectedDifference = null;
  const predicate = async (source) => {
    if (cache.has(source)) return cache.get(source);
    const [upstream, nativeResult] = await Promise.all([
      page.evaluate(async (text) => {
        try {
          const diagram = await globalThis.__flowchartMermaid.mermaidAPI.getDiagramFromText(text);
          const db = diagram.db;
          return {
            ok: true,
            ast: {
              direction: db.getDirection() ?? null,
              title: db.getDiagramTitle?.() ?? "",
              accTitle: db.getAccTitle?.() ?? "",
              accDescription: db.getAccDescription?.() ?? "",
              vertices: JSON.parse(JSON.stringify([...db.getVertices().values()])),
              edges: JSON.parse(JSON.stringify(db.getEdges())),
              classes: JSON.parse(JSON.stringify([...db.getClasses().values()])),
              subgraphs: JSON.parse(JSON.stringify(db.getSubGraphs())),
              tooltips: Object.fromEntries(
                [...db.getVertices().keys()]
                  .map((id) => [id, db.getTooltip(id)])
                  .filter(([, tooltip]) => tooltip !== undefined),
              ),
            },
          };
        } catch (error) {
          const message = String(error);
          const errorClass = message.includes("No diagram type detected")
            ? "detection"
            : message.includes("Parse error") || message.includes("Lexical error")
              ? "syntax"
              : "semantic";
          const stage = message.includes("No diagram type detected") ? "detector"
            : message.includes("Lexical error") ? "lexer"
            : message.includes("Parse error") ? "parser" : "semantic";
          const hashLine = error?.hash?.loc?.first_line;
          const hashColumn = error?.hash?.loc?.first_column;
          const messageLine = message.match(/(?:on line|line:)\s*(\d+)/i)?.[1];
          return {
            ok: false,
            class: errorClass,
            stage,
            line: Number(hashLine ?? messageLine ?? (stage === "detector" ? 1 : 0)),
            column: Number(hashColumn === undefined ? (stage === "detector" ? 1 : 0) : hashColumn + 1),
            error: message,
          };
        }
      }, source),
      native.query(source),
    ]);
    const difference = differs(upstream, nativeResult);
    if (expectedDifference === null && difference !== null) expectedDifference = difference;
    const result = difference === expectedDifference && difference !== null;
    cache.set(source, result);
    if (cache.size % 100 === 0)
      console.log(`Checked ${cache.size} candidates; preserving ${expectedDifference}`);
    return result;
  };

  if (!await predicate(original)) throw new Error("Input does not currently differ between upstream and native");
  let minimized = await minimizeLines(original, predicate);
  minimized = await simplify(minimized, predicate);
  minimized = await minimizeCharacters(minimized, predicate);
  fs.writeFileSync(outputPath, `${minimized.trimEnd()}\n`);
  console.log(`Minimized ${original.length} -> ${minimized.length} characters after ${cache.size} comparisons`);
  console.log(`Wrote ${outputPath}`);
} finally {
  native.close();
  await browser.close();
}
