import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";
import { enrichProductions, loadClassGrammar } from "./mermaid_jison_grammar.mjs";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "class-db.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const grammarPath = process.argv[5];
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected Mermaid 11.16.0, found ${packageJson.version}`);
}
const loadCachedGrammar = (error) => {
  if (grammarPath || !fs.existsSync(output)) throw error;
  const cached = JSON.parse(fs.readFileSync(output, "utf8"));
  if (cached.upstream?.version !== packageJson.version ||
      cached.upstream?.grammar?.sha256 !==
        "c2ea20022e4adf501dbbcda909a1bdb74e05a2ac53ff5276d09d00fd54a3e21e" ||
      cached.productions?.length !== 138) {
    throw error;
  }
  const structuralProductions = cached.productions.map(
    ({ native, status, fixtures, reason, ...production }) => production,
  );
  const cachedGrammar = {
    source: cached.upstream.grammar,
    productions: enrichProductions(structuralProductions),
  };
  console.warn(`Using hash-locked cached class grammar: ${error.message}`);
  return cachedGrammar;
};
let grammar;
if (process.env.MERMAID_GRAMMAR_OFFLINE === "1") {
  grammar = loadCachedGrammar(new Error("offline mode requested"));
} else {
  try {
    grammar = await loadClassGrammar(packageJson.version, grammarPath);
  } catch (error) {
    grammar = loadCachedGrammar(error);
  }
}

const cases = [
  {
    id: "classes-members-relations",
    source: [
      "classDiagram",
      "direction LR",
      'class Animal["Animal label"] {',
      "  <<interface>>",
      "  +String name",
      "  +speak(sound) String*",
      "}",
      "class Dog~T~",
      'Animal "1" <|-- "many" Dog : inherits',
      "Dog o.. Animal : observes",
    ].join("\n"),
  },
  {
    id: "direct-members-annotations",
    source: [
      "classDiagram-v2",
      "class Service",
      "Service : -String secret$",
      "Service : +run(input) bool$",
      "<<service>> Service",
      "Service -- Client",
      "Client ..> Service : uses",
      "Port ()-- Service",
    ].join("\n"),
  },
  {
    id: "namespace-nesting-notes",
    source: [
      "classDiagram",
      'namespace Company.Core["Core services"] {',
      "  class Api {",
      "    +get() Result",
      "  }",
      "  note for Api \"API note\"",
      "  namespace Internal {",
      "    class Worker",
      "  }",
      "}",
      'note "Diagram note"',
    ].join("\n"),
  },
  {
    id: "relation-marker-matrix",
    source: [
      "classDiagram",
      "A <|-- B",
      "C *-- D",
      "E o-- F",
      "G --> H",
      "I ..> J",
      "K <.. L",
      "M --|> N",
      "O --() Port",
    ].join("\n"),
  },
  {
    id: "styles-and-classes",
    source: [
      "classDiagram",
      "class A",
      "class B:::hot",
      'cssClass "A" hot',
      "style A fill:#f00,stroke-width:2px,color:#fff",
      "classDef hot fill:#0f0,stroke:#333,color:#111",
    ].join("\n"),
  },
  {
    id: "nested-style-functions",
    source: [
      "classDiagram",
      "class A",
      "style A fill:rgb(1,2,3),stroke:hsl(4,5%,6%),color:#fff",
    ].join("\n"),
  },
  {
    id: "links-clicks-tooltips",
    source: [
      "classDiagram",
      "class A",
      "class B",
      'link A "https://example.com/a" "A docs" _self',
      'click B href "https://example.com/b" "B docs" _blank',
      'callback A "legacyFn" "Legacy tip"',
      'click B call handler("x", 2) "Callback tip"',
    ].join("\n"),
  },
  {
    id: "quoted-unicode-generic",
    source: [
      "classDiagram",
      'class `\u8ba2\u5355 \u670d\u52a1`~\u5217\u8868~["\u663e\u793a \u540d\u79f0"] {',
      "  +\u7f16\u53f7",
      "  +\u5904\u7406(\u8f93\u5165) \u8f93\u51fa",
      "}",
      "`\u8ba2\u5355 \u670d\u52a1` --> \u0645\u0633\u062a\u062e\u062f\u0645 : \u0645\u0639\u0627\u0644\u062c\u0629",
    ].join("\n"),
  },
  {
    id: "accessibility-comments",
    source: [
      "classDiagram",
      "title Class model",
      "accTitle: Accessible classes",
      "accDescr {",
      "  First line",
      "  Second line",
      "}",
      "%% ignored comment",
      "class Root",
    ].join("\n"),
  },
  {
    id: "class-statement-forms",
    source: [
      "classDiagram",
      "class Empty {}",
      "class Styled:::hot {",
      "  +value",
      "}",
      "class Annotated<<entity>>",
      "class Detailed<<service>> {",
      "  +run() Result",
      "}",
      "class AnnotatedEmpty<<boundary>> {}",
    ].join("\n"),
  },
  {
    id: "relation-orthogonal-forms",
    source: [
      "classDiagram",
      'A "source" *..> "target" B : observes',
      'C "left" -- D',
      'E .. "right" F',
      "G <|--* H",
      "I o..|> J",
    ].join("\n"),
  },
  {
    id: "interaction-production-matrix",
    source: [
      "classDiagram",
      "class A",
      "class B",
      'callback A "legacy"',
      'link A "https://example.com/one"',
      'link A "https://example.com/two" _self',
      'link B "https://example.com/three" "tip"',
      "click A call handler()",
      'click A call handler() "callback tip"',
      'click B call handler("x", 2)',
      'click A href "https://example.com/four"',
      'click A href "https://example.com/five" _parent',
      'click B href "https://example.com/six" "href tip"',
    ].join("\n"),
  },
  {
    id: "directives-and-identifiers",
    source: [
      "classDiagram",
      "direction TB",
      "direction BT",
      "direction RL",
      "accDescr: One-line description",
      "namespace `Quoted Space` {",
      "  class Number1",
      "}",
      "class A",
      "class B",
      "classDef hot,cold fill:#123,stroke-width:3px,color:rgb(1,2,3)",
      'cssClass "A,B" hot',
    ].join("\n"),
  },
];

const invalidCases = [
  { id: "missing-header", source: "class A" },
  { id: "invalid-direction", source: "classDiagram\ndirection SIDEWAYS\nclass A" },
  { id: "unclosed-class", source: "classDiagram\nclass A {\n+x" },
  { id: "unclosed-namespace", source: "classDiagram\nnamespace N {\nclass A" },
  { id: "missing-relation-target", source: "classDiagram\nA -->" },
  { id: "malformed-class-label", source: "classDiagram\nclass A[open" },
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
    mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
    const bootstrap = await mermaid.mermaidAPI.getDiagramFromText("classDiagram\nclass Bootstrap");
    const generated = bootstrap.parser.parser;
    const originalPerformAction = generated.performAction;
    let reductions;
    generated.performAction = function (...args) {
      reductions?.add(args[4]);
      return originalPerformAction.apply(this, args);
    };

    const member = (value) => ({
      id: value.id ?? "",
      memberType: value.memberType ?? "",
      visibility: value.visibility ?? "",
      classifier: value.classifier ?? "",
      parameters: value.parameters ?? "",
      returnType: value.returnType ?? "",
      text: value.text ?? "",
    });
    const normalize = (diagram) => {
      const db = diagram.db;
      const classes = [...db.getClasses()].map(([id, value]) => ({
        id,
        type: value.type,
        label: value.label,
        text: value.text,
        cssClasses: value.cssClasses,
        methods: value.methods.map(member),
        members: value.members.map(member),
        annotations: value.annotations,
        styles: value.styles,
        parent: value.parent ?? null,
        link: value.link ?? null,
        linkTarget: value.linkTarget ?? null,
        haveCallback: value.haveCallback ?? false,
        tooltip: value.tooltip ?? null,
      }));
      const relations = db.getRelations().map((value) => ({
        id1: value.id1,
        id2: value.id2,
        relationTitle1: value.relationTitle1,
        relationTitle2: value.relationTitle2,
        title: value.title ?? "",
        relation: value.relation,
      }));
      const notes = [...db.getNotes()].map(([id, value]) => ({
        id,
        class: value.class ?? null,
        text: value.text,
        index: value.index,
        parent: value.parent ?? null,
      }));
      const namespaces = [...db.getNamespaces()].map(([id, value]) => ({
        id,
        label: value.label,
        parent: value.parent ?? null,
        explicit: value.explicit,
        classKeys: [...value.classes.keys()],
        noteKeys: [...value.notes.keys()],
        childKeys: [...value.children.keys()],
      }));
      return {
        title: db.getDiagramTitle?.() ?? "",
        accTitle: db.getAccTitle?.() ?? "",
        accDescription: db.getAccDescription?.() ?? "",
        direction: db.getDirection(),
        classes,
        relations,
        notes,
        namespaces,
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
    const coverageOnly = [];
    for (const fixture of [
      { id: "direct-statement", source: "class Direct" },
      { id: "concatenated-namespace", source: "classDiagram\nnamespace Foo Bar {\nclass A\n}" },
      { id: "dotted-class-name", source: "classDiagram\nA.B --> C" },
      { id: "inline-namespace-body", source: "classDiagram\nnamespace N {class A\n}" },
      { id: "inline-class-close", source: "classDiagram\nnamespace N {\nclass A}" },
      { id: "inline-note-close", source: 'classDiagram\nnamespace N {\nnote "inside"}' },
      { id: "namespace-note-newline", source: 'classDiagram\nnamespace N {\nnote "inside"\n}' },
      { id: "nested-namespace-terminal", source: "classDiagram\nnamespace Outer {\nnamespace Inner {\nclass A\n}}" },
      { id: "nested-namespace-recursive", source: "classDiagram\nnamespace Outer {\nnamespace Inner {\nclass A\n}\nclass B\n}" },
      { id: "style-list-alpha", source: "classDiagram\nclass A\nstyle A red,blue" },
      { id: "style-number", source: "classDiagram\nclass A\nstyle A 123" },
      { id: "style-colon", source: "classDiagram\nclass A\nstyle A :" },
      { id: "style-bracket", source: "classDiagram\nclass A\nstyle A #" },
      { id: "style-keyword", source: "classDiagram\nclass A\nstyle A style" },
      { id: "style-percent", source: "classDiagram\nclass A\nstyle A %" },
      { id: "numeric-class", source: "classDiagram\nclass 123" },
      { id: "minus-class", source: "classDiagram\nclass -" },
      { id: "standalone-member", source: "classDiagram\n+value" },
    ]) {
      reductions = new Set();
      try {
        bootstrap.db.clear();
        const diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
        coverageOnly.push({ ...fixture, reductions: [...reductions].sort((a, b) => a - b),
          expected: normalize(diagram) });
      } catch (error) {
        try {
          bootstrap.db.clear();
          bootstrap.parser.parse(fixture.source);
        } catch {
          // The detector and raw parser expose different errors; reductions come from the latter.
        }
        coverageOnly.push({ ...fixture, reductions: [...reductions].sort((a, b) => a - b),
          upstreamError: {
            message: String(error?.message ?? error),
            token: error?.hash?.token ?? null,
            line: error?.hash?.loc?.first_line ?? null,
            column: error?.hash?.loc?.first_column ?? null,
          } });
      }
    }
    reductions = undefined;
    generated.performAction = originalPerformAction;

    const symbolNames = Object.fromEntries(Object.entries(generated.symbols_).map(([name, id]) => [id, name]));
    const fixtureIds = new Map();
    for (const fixture of [...results, ...coverageOnly])
      for (const production of fixture.reductions)
        fixtureIds.set(production, [...(fixtureIds.get(production) ?? []), fixture.id]);
    const unreachableReasons = {
      57: "emptyBody is not referenced by any production reachable from start",
      58: "emptyBody is not referenced by any production reachable from start",
      59: "emptyBody is not referenced by any production reachable from start",
      65: "MEMBER is emitted only in class-body state, where the parser reduces members rather than memberStatement",
      66: "SEPARATOR has no lexer rule in classDiagram.jison",
      112: "NUM is shadowed by the earlier \\w+ ALPHA lexer rule",
      114: "UNIT has no lexer rule in classDiagram.jison",
      115: "SPACE is shadowed by the earlier INITIAL whitespace skip rule",
      120: "commentToken is not referenced by any production reachable from start",
      121: "commentToken is not referenced by any production reachable from start",
      122: "textToken is reachable only from disconnected commentToken",
      123: "textToken is reachable only from disconnected commentToken",
      124: "textToken is reachable only from disconnected commentToken",
      125: "textToken is reachable only from disconnected commentToken",
      126: "textToken is reachable only from disconnected commentToken",
      127: "textToken is reachable only from disconnected commentToken",
      128: "textToken is reachable only from disconnected commentToken",
      129: "textNoTagsToken is reachable only from disconnected textToken",
      130: "textNoTagsToken is reachable only from disconnected textToken",
      131: "textNoTagsToken is reachable only from disconnected textToken",
      132: "textNoTagsToken is reachable only from disconnected textToken",
      134: "NUM is shadowed by the earlier \\w+ ALPHA lexer rule",
    };
    const productions = generated.productions_.slice(1).map(([lhs, rhsLength], index) => {
      const id = index + 1;
      const fixtures = fixtureIds.get(id) ?? [];
      const reason = unreachableReasons[id];
      return {
        id,
        lhs: symbolNames[lhs],
        rhsLength,
        native: `parse${symbolNames[lhs].replace(/(^|_)(\w)/g, (_, __, c) => c.toUpperCase())}`,
        status: fixtures.length ? "covered" : reason ? "unreachable" : "uncovered",
        fixtures,
        ...(reason ? { reason } : {}),
      };
    });
    return { cases: results, invalidCases: invalid, coverageOnly, productions };
  }, { cases, invalidCases, mermaidModule });

  snapshot.productions = snapshot.productions.map((production, index) => {
    const grammarProduction = grammar.productions[index];
    if (production.id !== grammarProduction.id || production.lhs !== grammarProduction.lhs ||
        production.rhsLength !== grammarProduction.rhsLength) {
      throw new Error(`Generated LR production ${production.id} does not match classDiagram.jison`);
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
  console.log(`Wrote ${snapshot.cases.length} class cases and ${snapshot.productions.length} productions to ${output}`);
} finally {
  await browser.close();
}
