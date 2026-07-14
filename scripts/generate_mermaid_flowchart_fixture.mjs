import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

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
    async ({ cases, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        flowchart: { defaultRenderer: "dagre-wrapper" },
      });
      const clean = (value) => JSON.parse(JSON.stringify(value));
      const result = [];
      for (const fixture of cases) {
        const diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
        const db = diagram.db;
        result.push({
          ...fixture,
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
      return result;
    },
    { cases, mermaidModule },
  );
  const fixture = {
    upstream: {
      package: "mermaid",
      version: packageJson.version,
      license: packageJson.license,
    },
    cases: snapshots,
  };
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(`Wrote ${output}`);
} finally {
  await browser.close();
}
