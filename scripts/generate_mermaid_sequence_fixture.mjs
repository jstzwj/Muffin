import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { loadSequenceGrammar } from "./mermaid_jison_grammar.mjs";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "sequence-db.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const grammarPath = process.argv[5];
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected Mermaid 11.16.0, found ${packageJson.version}`);
}
const grammar = await loadSequenceGrammar(packageJson.version, grammarPath);

const cases = [
  {
    id: "participants-and-arrows",
    source: [
      "sequenceDiagram",
      "participant A as Alice",
      "actor B as Bob",
      "A->>B:solid open",
      "B-->>A:dotted open",
      "A->B:solid",
      "B-->A:dotted",
      "A-xB:cross",
      "B--)A:point",
      "A<<->>B:bidirectional",
    ].join("\n"),
  },
  {
    id: "notes-and-activation",
    source: [
      "sequenceDiagram",
      "A->>+B:call",
      "activate A",
      "Note left of A: left note",
      "Note right of B: right note",
      "Note over A,B: shared note",
      "deactivate A",
      "B-->>-A:return",
    ].join("\n"),
  },
  {
    id: "fragments",
    source: [
      "sequenceDiagram",
      "loop Every minute",
      "A->>B:tick",
      "end",
      "opt Available",
      "B-->>A:ok",
      "end",
      "alt Success",
      "A->>B:yes",
      "else Failure",
      "A-->>B:no",
      "end",
      "par First",
      "A->>B:one",
      "and Second",
      "B->>A:two",
      "end",
    ].join("\n"),
  },
  {
    id: "critical-break-rect",
    source: [
      "sequenceDiagram",
      "critical Establish connection",
      "A->>B:connect",
      "option Timeout",
      "A-->>B:retry",
      "end",
      "break Abort when offline",
      "B-->>A:offline",
      "end",
      "rect rgb(240, 240, 255)",
      "A->>B:inside",
      "end",
    ].join("\n"),
  },
  {
    id: "create-destroy",
    source: [
      "sequenceDiagram",
      "participant A as Factory",
      "create participant B as Worker",
      "A->>B:create",
      "destroy B",
      "B-->>A:done",
    ].join("\n"),
  },
  {
    id: "box-and-autonumber",
    source: [
      "sequenceDiagram",
      "autonumber 10 5",
      "box rgb(230, 240, 255) Services",
      "participant A",
      "participant B",
      "end",
      "A->>B:numbered",
      "autonumber off",
    ].join("\n"),
  },
  {
    id: "wrap-title-accessibility",
    source: [
      "sequenceDiagram",
      "title: Sequence title",
      "accTitle: Accessible title",
      "accDescr: Accessible description",
      "participant A as wrap:Wrapped actor",
      "A->>A:nowrap:Do not wrap",
    ].join("\n"),
  },
  {
    id: "arrow-production-matrix",
    source: [
      "sequenceDiagram",
      "A<<-->>B:bidirectional dotted",
      "A--xB:dotted cross",
      "A-)B:solid point",
      "A--|\\B:solid top dotted",
      "A--|/B:solid bottom dotted",
      "A--\\\\B:stick top dotted",
      "A--//B:stick bottom dotted",
      "A/|--B:solid reverse top dotted",
      "A\\|--B:solid reverse bottom dotted",
      "A//--B:stick reverse top dotted",
      "A\\\\--B:stick reverse bottom dotted",
      "A-|\\B:solid top",
      "A-|/B:solid bottom",
      "A-\\\\B:stick top",
      "A-//B:stick bottom",
      "A/|-B:solid reverse top",
      "A\\|-B:solid reverse bottom",
      "A//-B:stick reverse top",
      "A\\\\-B:stick reverse bottom",
    ].join("\n"),
  },
  {
    id: "participant-config-and-statements",
    source: [
      "sequenceDiagram",
      'participant A@{ "type": "database", "alias": "Primary DB" }',
      "actor B",
      "links A: {\"Docs\":\"https://example.com/docs\"}",
      "link A: Status @ https://example.com/status",
      "properties A: {\"role\":\"storage\",\"tier\":1}",
      "details A: missing-details-element",
      "A->>B:configured",
    ].join("\n"),
  },
  {
    id: "statement-variants",
    source: [
      "sequenceDiagram;",
      "title Legacy sequence title;",
      "autonumber;",
      "A->>B:first;",
      "autonumber 7",
      "A-->>B:second;",
      "par_over Across participants;",
      "A->>B:parallel;",
      "end;",
      "box;",
      "actor C;",
      "end;",
    ].join("\n"),
  },
  {
    id: "configured-participant-variants",
    source: [
      "sequenceDiagram",
      'participant C@{ "type": "control" } as Controller',
      'actor D@{ "type": "boundary" } as Boundary',
      'actor E@{ "type": "entity", "alias": "Entity" }',
      "C->>D:one",
      "D->>E:two",
    ].join("\n"),
  },
  {
    id: "central-and-single-over",
    source: [
      "sequenceDiagram",
      "A->>()B:forward central",
      "A()->>B:reverse central",
      "A()->>()B:dual central",
      "Note over A:single actor note",
    ].join("\n"),
  },
  {
    id: "multiline-accessibility-and-spacing",
    source: [
      "sequenceDiagram",
      "accDescr {",
      "  First accessible line",
      "  Second accessible line",
      "}",
      "box Services",
      "",
      "  participant A",
      "end",
      "links   A: {\"Home\":\"https://example.com\"}",
    ].join("\n"),
  },
];

const invalidCases = [
  { id: "missing-header", source: "A->>B:hello" },
  { id: "unclosed-loop", source: "sequenceDiagram\nloop open\nA->>B:x" },
  { id: "unexpected-end", source: "sequenceDiagram\nend" },
  { id: "inactive-deactivate", source: "sequenceDiagram\ndeactivate A" },
  { id: "create-without-message", source: "sequenceDiagram\ncreate participant B\nA->>C:no" },
  { id: "destroy-without-message", source: "sequenceDiagram\nparticipant B\ndestroy B\nA->>C:no" },
  { id: "duplicate-create", source: "sequenceDiagram\nparticipant B\ncreate participant B" },
  { id: "malformed-arrow", source: "sequenceDiagram\nA=>B:bad" },
];

const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")),
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});

try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const snapshot = await page.evaluate(async ({ cases, invalidCases, mermaidModule }) => {
    const { default: mermaid } = await import(mermaidModule);
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict", sequence: { wrap: false } });
    const bootstrap = await mermaid.mermaidAPI.getDiagramFromText("sequenceDiagram\nA->>B:bootstrap");
    const generated = bootstrap.parser.parser;
    const originalPerformAction = generated.performAction;
    let reductions;
    generated.performAction = function (...args) {
      reductions?.add(args[4]);
      return originalPerformAction.apply(this, args);
    };

    const normalize = (diagram) => {
      const db = diagram.db;
      const actors = [...db.getActors()].map(([id, actor]) => ({
        id,
        name: actor.name,
        description: actor.description,
        wrap: actor.wrap,
        prevActor: actor.prevActor ?? null,
        nextActor: actor.nextActor ?? null,
        type: actor.type,
        box: actor.box?.name ?? null,
        links: actor.links ?? {},
        properties: actor.properties ?? {},
      }));
      const messages = db.getMessages().map((message) => ({
        id: message.id,
        from: message.from ?? null,
        to: message.to ?? null,
        message: message.message,
        wrap: message.wrap,
        type: message.type ?? null,
        activate: message.activate ?? false,
        centralConnection: message.centralConnection ?? 0,
        placement: message.placement ?? null,
      }));
      const boxes = db.getBoxes().map((box) => ({
        name: box.name ?? null,
        fill: box.fill,
        wrap: box.wrap,
        actorKeys: box.actorKeys,
      }));
      return {
        title: db.getDiagramTitle?.() ?? "",
        accTitle: db.getAccTitle?.() ?? "",
        accDescription: db.getAccDescription?.() ?? "",
        sequenceNumbers: db.showSequenceNumbers(),
        actors,
        messages,
        boxes,
        createdActors: Object.fromEntries(db.getCreatedActors()),
        destroyedActors: Object.fromEntries(db.getDestroyedActors()),
      };
    };

    const results = [];
    for (const fixture of cases) {
      reductions = new Set();
      const diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
      results.push({ ...fixture, reductions: [...reductions].sort((a, b) => a - b), expected: normalize(diagram) });
    }
    const invalid = [];
    for (const fixture of invalidCases) {
      try {
        await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
        invalid.push({ ...fixture, rejected: false });
      } catch (error) {
        invalid.push({
          ...fixture,
          rejected: true,
          error: {
            message: String(error?.message ?? error),
            token: error?.hash?.token ?? null,
            line: error?.hash?.loc?.first_line ?? null,
            column: error?.hash?.loc?.first_column ?? null,
          },
        });
      }
    }
    const symbolNames = Object.fromEntries(Object.entries(generated.symbols_).map(([name, id]) => [id, name]));
    const coverageOnly = [];
    for (const fixture of [
      { id: "direct-leading-space", source: " sequenceDiagram\nA->>B:x" },
      { id: "direct-leading-newline", source: "\nsequenceDiagram\nA->>B:x" },
      { id: "direct-empty-lines", source: "sequenceDiagram\n\nA->>B:x\n" },
    ]) {
      reductions = new Set();
      try {
        bootstrap.parser.parse(fixture.source);
        coverageOnly.push({ ...fixture, reductions: [...reductions] });
      } catch {
        coverageOnly.push({ ...fixture, reductions: [...reductions], upstreamError: true });
      }
    }
    reductions = undefined;
    generated.performAction = originalPerformAction;

    const fixtureIds = new Map();
    for (const fixture of [...results, ...coverageOnly])
      for (const production of fixture.reductions)
        fixtureIds.set(production, [...(fixtureIds.get(production) ?? []), fixture.id]);
    const productions = generated.productions_.slice(1).map(([lhs, rhsLength], index) => {
      const id = index + 1;
      const fixtures = fixtureIds.get(id) ?? [];
      const unreachable = {
        1: "INITIAL whitespace is skipped by the lexer before start reduction",
        2: "INITIAL newline whitespace is folded into the lexer NEWLINE path",
        6: "SPACE is skipped before the line nonterminal can shift it",
        12: "SPACE is skipped before the box_line nonterminal can shift it",
        64: "ID-state SPACE is skipped instead of returned to spaceList",
        65: "ID-state SPACE is skipped instead of returned to spaceList",
      }[id];
      const negativeOnly = id === 9
        ? "INVALID line recovery is reachable only after a lexer/parser error"
        : undefined;
      return {
        id,
        lhs: symbolNames[lhs],
        rhsLength,
        native: `parse${symbolNames[lhs].replace(/(^|_)(\w)/g, (_, __, c) => c.toUpperCase())}`,
        status: fixtures.length ? "covered" : unreachable ? "unreachable"
          : negativeOnly ? "negative-only" : "uncovered",
        fixtures,
        ...(fixtures.length ? {} : { reason: unreachable ?? negativeOnly ??
          "not yet selected by the dynamic sequence corpus" }),
      };
    });
    return { cases: results, invalidCases: invalid, coverageOnly, productions };
  }, { cases, invalidCases, mermaidModule });

  snapshot.productions = snapshot.productions.map((production, index) => {
    const grammarProduction = grammar.productions[index];
    if (production.id !== grammarProduction.id ||
        production.lhs !== grammarProduction.lhs ||
        production.rhsLength !== grammarProduction.rhsLength) {
      throw new Error(`Generated LR production ${production.id} does not match sequenceDiagram.jison`);
    }
    return { ...grammarProduction, native: production.native,
      status: production.status, fixtures: production.fixtures,
      ...(production.reason ? { reason: production.reason } : {}) };
  });
  const fixtureDigest = createHash("sha256").update(JSON.stringify(snapshot)).digest("hex");
  fs.writeFileSync(output, `${JSON.stringify({
    upstream: { package: "mermaid", version: packageJson.version, grammar: grammar.source },
    fixtureDigest,
    ...snapshot,
  }, null, 2)}\n`);
  console.log(`Wrote ${snapshot.cases.length} sequence cases and ${snapshot.productions.length} productions to ${output}`);
} finally {
  await browser.close();
}
