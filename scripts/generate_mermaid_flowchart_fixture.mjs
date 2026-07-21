import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { loadFlowchartGrammar } from "./mermaid_jison_grammar.mjs";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "flowchart-db.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}
const grammar = await loadFlowchartGrammar(packageJson.version, process.argv[5]);
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

const cases = [
  {
    id: "keyword-prefix-node-production",
    source: "flowchart LR\nstyleA-->classDefB-->clickC-->endD",
  },
  {
    id: "semicolon-statements",
    source: "flowchart TD; A[Alpha]-->B[Beta]; B-->C[Gamma];",
  },
  {
    id: "semicolon-style-separators",
    source: "graph\nA[Alpha]; style A fill:#f00,stroke:#000,stroke-width:2px;",
  },
  {
    id: "semicolon-inside-label",
    source: 'flowchart LR; A["one; two"] --> B["`**three; four**`"]',
  },
  {
    id: "semicolon-subgraph",
    source: "flowchart TB; subgraph cluster[Group]; A-->B; end;",
  },
  {
    id: "basic-labelled-edge",
    source: "flowchart LR\nA[Start] -->|go| B((End))",
  },
  {
    id: "duplicate-bare-reference",
    source: "flowchart TB\nA[Alpha] --> B[Beta] --> C[Gamma] --> A",
  },
  {
    id: "directions",
    source: "graph TD\nA --> B",
  },
  {
    id: "legacy-shapes",
    source: [
      "flowchart TB",
      "square[Square]",
      "round(Round)",
      "stadium([Stadium])",
      "subroutine[[Subroutine]]",
      "cylinder[(Database)]",
      "circle((Circle))",
      "asymmetric>Flag]",
      "diamond{Decision}",
      "hexagon{{Hexagon}}",
      "trapezoid[/Trapezoid\\]",
      "invTrapezoid[\\Inverse/]",
      "leanRight[/Lean/]",
      "leanLeft[\\Lean\\]",
    ].join("\n"),
  },
  {
    id: "remaining-vertex-productions",
    source: [
      "flowchart TB",
      "double(((Double circle)))",
      "ellipse(-Ellipse-)",
      "props[|role:service|Service]",
    ].join("\n"),
  },
  {
    id: "multiline-accessibility",
    source: "flowchart LR\naccDescr {\n  First line\n  Second line\n}\nA-->B",
  },
  {
    id: "link-style-interpolate-productions",
    source: [
      "flowchart LR",
      "A-->B-->C-->D",
      "linkStyle default stroke:#999",
      "linkStyle 0,1 stroke:#0f0",
      "linkStyle default interpolate basis stroke:#999",
      "linkStyle 0,1 interpolate linear stroke:#f00",
      "linkStyle default interpolate step",
      "linkStyle 2 interpolate stepAfter",
    ].join("\n"),
  },
  {
    id: "token-list-productions",
    source: [
      "flowchart TB",
      "v",
      "default",
      "a,b",
      "a:b",
      "hash#id",
      "star*id",
      "123",
      "-minus",
      "#hash",
      "*star",
      "A&B",
      "-",
      "&",
      "unicode节点",
      "tagStart[<]",
      "tagEnd[>]",
      "unicodeLabel[中]",
      "htmlLabel[<b>bold</b>]",
      "A -- 中文 --> B",
      "B-->|pipe|C",
      "style A fill: red,width:12px,color:#fff,opacity:50%",
      "style A px",
      "style A %",
      "style A style",
      "click A 1中callback-v,:&#*",
      "click B v",
      "click C -callback",
      "click C -",
    ].join("\n"),
  },
  {
    id: "subgraph-token-productions",
    source: [
      "flowchart TB",
      "subgraph plain",
      "A-->B",
      "end",
      "subgraph \"quoted title\"",
      "C-->D",
      "end",
      "subgraph \"`**markdown title**`\"",
      "E-->F",
      "end",
      "subgraph style-linkStyle-classDef-class-click-graph-subgraph-end-v-^",
      "G-->H",
      "end",
    ].join("\n"),
  },
  {
    id: "subgraph-keyword-productions",
    source: [
      "flowchart TB",
      ...["linkStyle", "classDef", "class", "graph", "subgraph"].flatMap(
        (keyword, index) => [
          `subgraph ${keyword}`,
          `K${index}-->L${index}`,
          "end",
        ],
      ),
    ].join("\n"),
  },
  {
    id: "subgraph-text-token-productions",
    source: [
      "flowchart TB",
      "subgraph 123",
      "A-->B",
      "end",
      "subgraph two words",
      "C-->D",
      "end",
      "subgraph -",
      "E-->F",
      "end",
      "subgraph &",
      "G-->H",
      "end",
      "subgraph 中",
      "I-->J",
      "end",
      "subgraph :",
      "K-->L",
      "end",
      "subgraph *",
      "M-->N",
      "end",
      "subgraph #",
      "O-->P",
      "end",
      "subgraph v",
      "Q-->R",
      "end",
    ].join("\n"),
  },
  {
    id: "link-id-labelled-production",
    source: "flowchart LR\nA e1@-- \"edge\" --> B\nA e2@-. 中文 .-> B",
  },
  {
    id: "shape-data-group-production",
    source: "flowchart LR\nA@{ shape: rounded } & B --> C",
  },
  {
    id: "leading-and-trailing-space-productions",
    source: "\n flowchart LR   \nA --> B   \n",
  },
  ...["TB", "BT", "RL", "LR", "TD"].map((direction) => ({
    id: `direction-production-${direction.toLowerCase()}`,
    source: `flowchart ${direction}\nA-->B`,
  })),
  {
    id: "subgraph-direction-productions",
    source: [
      "flowchart TB",
      ...["TB", "BT", "RL", "LR", "TD"].flatMap((direction, index) => [
        `subgraph direction${index}`,
        `direction ${direction}`,
        `D${index}-->E${index}`,
        "end",
      ]),
    ].join("\n"),
  },
  {
    id: "click-production-matrix",
    source: [
      "flowchart LR",
      "A-->B-->C-->D-->E-->F-->G-->H-->I-->J-->K-->L-->M-->N",
      "click A call first()",
      "click B call second() \"Second\"",
      "click C call third(1, two)",
      "click D call fourth(1, two) \"Fourth\"",
      "click E href \"https://example.com/e\"",
      "click F href \"https://example.com/f\" \"Sixth\"",
      "click G href \"https://example.com/g\" _self",
      "click H href \"https://example.com/h\" \"Eighth\" _blank",
      "click I ninth",
      "click J tenth \"Tenth\"",
      "click K \"https://example.com/k\"",
      "click L \"https://example.com/l\" \"Twelfth\"",
      "click M \"https://example.com/m\" _parent",
      "click N \"https://example.com/n\" \"Fourteenth\" _top",
    ].join("\n"),
  },
  {
    id: "edge-forms",
    source: [
      "flowchart LR",
      "A --> B",
      "B --- C",
      "C -. dotted .-> D",
      "D ==> E",
      "E -- open --- F",
      "F --x G",
      "G --o H",
      "H <--> I",
    ].join("\n"),
  },
  {
    id: "edge-label-type-productions",
    source: [
      "flowchart LR",
      "A -- \"quoted\" --> B",
      "B -- \"`**markdown**`\" --> C",
      "C -->|\"pipe quoted\"| D",
      "D -->|\"`*pipe markdown*`\"| E",
    ].join("\n"),
  },
  {
    id: "chains-and-groups",
    source: "flowchart LR\nA & B --> C & D --> E",
  },
  {
    id: "parallel-edge-ids",
    source: "flowchart LR\nA --> B\nA --> B\nA --> B",
  },
  {
    id: "classes-and-styles",
    source: [
      "flowchart LR",
      "A[Alpha]:::hot --> B[Beta]",
      "classDef hot fill:#f00,color:#fff,stroke-width:2px",
      "class B hot",
      "style B fill:#0f0,stroke:#333",
      "linkStyle 0 stroke:#00f,stroke-width:3px",
    ].join("\n"),
  },
  {
    id: "subgraphs",
    source: [
      "flowchart TB",
      "subgraph outer[Outer group]",
      "direction LR",
      "A --> B",
      "subgraph inner[Inner]",
      "C --> D",
      "end",
      "end",
      "B --> C",
    ].join("\n"),
  },
  {
    id: "quoted-and-markdown-labels",
    source: "flowchart LR\nA[\"quoted label\"] --> B[\"`**bold** and text`\"]",
  },
  {
    id: "accessibility",
    source: [
      "flowchart LR",
      "accTitle: Accessible title",
      "accDescr: Accessible description",
      "A --> B",
    ].join("\n"),
  },
  {
    id: "click-and-links",
    source: [
      "flowchart LR",
      "A --> B",
      "click A href \"https://example.com\" \"Open example\" _blank",
      "click B callback \"Run callback\"",
    ].join("\n"),
  },
  {
    id: "node-metadata",
    source: [
      "flowchart LR",
      "A@{ shape: rounded, label: \"Alpha\" } --> B@{ shape: bang, label: \"Beta\" }",
    ].join("\n"),
  },
  {
    id: "edge-id-and-metadata",
    source: [
      "flowchart LR",
      "A e1@--> B",
      "e1@{ animate: true, animation: fast, curve: basis }",
    ].join("\n"),
  },
  {
    id: "long-links",
    source: [
      "flowchart LR",
      "A ----> B",
      "B -..-> C",
      "C ====> D",
    ].join("\n"),
  },
  {
    id: "default-link-style",
    source: [
      "flowchart LR",
      "A --> B --> C",
      "linkStyle default stroke:#999,stroke-width:2px",
    ].join("\n"),
  },
];

const invalidCases = [
  { id: "style-extra-space", source: "flowchart LR\nstyle  A fill:#fff" },
  { id: "class-def-extra-space", source: "flowchart LR\nclassDef  hot fill:#fff" },
  { id: "class-extra-space", source: "flowchart LR\nclass  A hot" },
  { id: "link-style-extra-space", source: "flowchart LR\nA-->B\nlinkStyle  0 stroke:red" },
  { id: "style-missing-space", source: "flowchart LR\nstyle Afill:#fff" },
];

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  const blankPage = pathToFileURL(
    path.join(path.dirname(mermaidRoot), "..", "index.html"),
  ).href;
  await page.goto(blankPage);
  const mermaidModule = pathToFileURL(
    path.join(mermaidRoot, "dist", "mermaid.esm.mjs"),
  ).href;
  const snapshots = await page.evaluate(
    async ({ cases, invalidCases, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        flowchart: { defaultRenderer: "dagre-wrapper" },
      });
      const clean = (value) => JSON.parse(JSON.stringify(value));
      const result = [];
      const bootstrap = await mermaid.mermaidAPI.getDiagramFromText(
        "flowchart LR\nbootstrapA-->bootstrapB",
      );
      const parser = bootstrap.parser.parser;
      const originalPerformAction = parser.performAction;
      let reductions;
      parser.performAction = function (...args) {
        reductions?.add(args[4]);
        return originalPerformAction.apply(this, args);
      };
      for (const fixture of cases) {
        reductions = new Set();
        const diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
        const db = diagram.db;
        result.push({
          ...fixture,
          productions: [...reductions].sort((a, b) => a - b),
          expected: {
            direction: db.getDirection() ?? null,
            title: db.getDiagramTitle?.() ?? "",
            accTitle: db.getAccTitle?.() ?? "",
            accDescription: db.getAccDescription?.() ?? "",
            vertices: clean([...db.getVertices().values()]),
            edges: clean(db.getEdges()),
            classes: clean([...db.getClasses().values()]),
            subgraphs: clean(db.getSubGraphs()),
            tooltips: Object.fromEntries(
              [...db.getVertices().keys()]
                .map((id) => [id, db.getTooltip(id)])
                .filter(([, tooltip]) => tooltip !== undefined),
            ),
          },
        });
      }
      const invalidResults = [];
      for (const fixture of invalidCases) {
        try {
          await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
          invalidResults.push({ ...fixture, rejected: false });
        } catch {
          invalidResults.push({ ...fixture, rejected: true });
        }
      }
      const coverageOnly = [];
      for (const fixture of [
        { id: "direct-leading-space", source: " flowchart LR\nA-->B" },
        { id: "direct-leading-newline", source: "\nflowchart LR\nA-->B" },
        { id: "direct-eof-separator", source: "flowchart LR\nA" },
      ]) {
        reductions = new Set();
        let status = "covered";
        try {
          bootstrap.parser.parse(fixture.source);
        } catch {
          status = "upstream-error";
        }
        coverageOnly.push({
          id: fixture.id,
          status,
          productions: [...reductions].sort((a, b) => a - b),
        });
      }
      reductions = undefined;
      parser.performAction = originalPerformAction;
      const symbolNames = Object.fromEntries(
        Object.entries(parser.symbols_).map(([name, id]) => [id, name]),
      );
      const fixturesByProduction = new Map();
      const errorsByProduction = new Map();
      for (const fixture of [...result, ...coverageOnly]) {
        for (const production of fixture.productions) {
          const target = fixture.status === "upstream-error"
            ? errorsByProduction
            : fixturesByProduction;
          if (!target.has(production)) target.set(production, [fixture.id]);
        }
      }
      const nativeByLhs = {
        start: "Parser::parse",
        document: "Parser::parseDocument",
        line: "scanStatements",
        graphConfig: "Parser::parseGraphConfig",
        ending: "unreachable",
        endToken: "unreachable",
        FirstStmtSeparator: "Parser::parseGraphConfig",
        spaceListNewline: "unreachable",
        spaceList: "TokenCursor::skipSpace",
        statement: "Parser::parseStatement",
        separator: "scanStatements",
        shapeData: "FlowchartTokenizer::lexShapeData",
        vertexStatement: "Parser::parseGraphStatement",
        node: "Parser::parseNodeGroup",
        styledVertex: "parseNode",
        vertex: "parseNode",
        link: "findTokenLinks",
        edgeText: "findTokenLinks",
        linkStatement: "findTokenLinks",
        arrowText: "Parser::parseGraphStatement",
        text: "parseNode",
        keywords: "TokenCursor::consumeUntil",
        textNoTags: "parseNode",
        classDefStatement: "Parser::parseClassDefStatement",
        classStatement: "Parser::parseClassStatement",
        clickStatement: "Parser::parseClickStatement",
        styleStatement: "Parser::parseStyleStatement",
        linkStyleStatement: "Parser::parseLinkStyleStatement",
        numList: "Parser::parseLinkStyleStatement",
        stylesOpt: "Parser::parseStyles",
        style: "Parser::parseStyles",
        styleComponent: "TokenCursor::parseList",
        idStringToken: "TokenCursor::consumeUntil",
        textToken: "parseNode",
        textNoTagsToken: "parseNode",
        edgeTextToken: "findTokenLinks",
        alphaNumToken: "TokenCursor::consumeUntil",
        idString: "TokenCursor::consumeUntil",
        alphaNum: "TokenCursor::consumeUntil",
        direction: "Parser::parseStatement",
      };
      const exclusions = {
        9: "Mermaid API preprocessing strips leading SPACE before graphConfig",
        10: "Mermaid API preprocessing strips leading NEWLINE before graphConfig",
        13: "ending is not referenced by any production",
        14: "ending is not referenced by any production",
        15: "endToken is reachable only through unused ending",
        16: "endToken is reachable only through unused ending",
        17: "endToken is reachable only through unused ending",
        21: "spaceListNewline is not referenced by any production",
        22: "spaceListNewline is not referenced by any production",
        23: "spaceListNewline is not referenced by any production",
        24: "spaceListNewline is not referenced by any production",
        35: "anonymous subgraph action dereferences undefined.text in Mermaid 11.16.0",
        42: "EOF is reduced as line EOF instead of separator EOF",
        74: "TESTSTR has no lexer rule",
        94: "click enters click lexer state and cannot serve as stable textNoTags",
        96: "DIR is emitted only in graph-header lexer state",
        98: "end is resolved as the subgraph terminator before textNoTags",
        137: "UNIT has no lexer rule",
        141: "PCT has no lexer rule",
        145: "MINUS is consumed by the longer NODE_STRING rule in idString contexts",
        154: "TAGSTART is consumed by the earlier TEXT rule in text state",
        155: "TAGEND is consumed by the earlier TEXT rule in text state",
        156: "UNICODE_TEXT is consumed by the earlier TEXT rule in text state",
        160: "MINUS is consumed by NODE_STRING in textNoTags contexts",
        167: "START_LINK enters edgeText state and cannot terminate textNoTags",
        169: "UNICODE_TEXT loses the lexer tie to EDGE_TEXT in edgeText state",
        173: "DIR is emitted only in graph-header lexer state",
        175: "MINUS is consumed by NODE_STRING in alphaNum contexts",
      };
      const alternativeBySymbol = {};
      const productions = parser.productions_.slice(1).map(([symbol, rhsLength], offset) => {
        const id = offset + 1;
        const lhs = symbolNames[symbol];
        const alternative = (alternativeBySymbol[lhs] ?? 0) + 1;
        alternativeBySymbol[lhs] = alternative;
        const fixtures = fixturesByProduction.get(id) ?? [];
        const errors = errorsByProduction.get(id) ?? [];
        const reason = exclusions[id] ?? "";
        return {
          id,
          lhs,
          alternative,
          rhsLength,
          native: nativeByLhs[lhs] ?? "",
          status: fixtures.length > 0 ? "covered" : id === 35 ? "upstream-error" : reason ? "unreachable" : errors.length > 0 ? "upstream-error" : "uncovered",
          reason,
          fixtures,
          errors,
        };
      });
      return { result, invalidResults, productions };
    },
    { cases, invalidCases, mermaidModule },
  );
  const productions = snapshots.productions.map((production, index) => {
    const grammarProduction = grammar.productions[index];
    if (!grammarProduction || production.id !== grammarProduction.id ||
        production.lhs !== grammarProduction.lhs ||
        production.rhsLength !== grammarProduction.rhsLength) {
      throw new Error(
        `flow.jison production ${production.id} does not match generated parser ` +
        `(${production.lhs}/${production.rhsLength} vs ` +
        `${grammarProduction?.lhs}/${grammarProduction?.rhsLength})`,
      );
    }
    return { ...production, ...grammarProduction };
  });
  const fixture = {
    upstream: {
      package: "mermaid",
      version: packageJson.version,
      license: packageJson.license,
      grammar: grammar.source,
    },
    productions,
    invalidCases: snapshots.invalidResults,
    cases: snapshots.result,
  };
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(`Wrote ${output}`);
} finally {
  await browser.close();
}
