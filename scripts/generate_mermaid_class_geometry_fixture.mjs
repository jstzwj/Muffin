// Captures real mermaid 11.16.0 classDiagram geometry: every class and note
// with its box, compartment dividers and text rows, every relationship path
// with its label and cardinalities, every namespace and the canvas, normalized
// to the first declared class's centre (the origin Muffin's layout subtracts
// too).
//
//   node scripts/generate_mermaid_class_geometry_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Env override: MERMAID_REFERENCE_ROOT (takes precedence over the default).
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/class-geometry.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { captureGeometry, generatorArguments, geometryFixture } from "./mermaid_geometry_capture.mjs";
import { describeClassGeometry } from "./mermaid_class_geometry_capture.mjs";

// Corpus: each relationship marker and line pattern, both ends, labels,
// compartments in full, notes, namespaces (nested ones too), the directions,
// cardinalities, generics, self relationships and scripts beyond Latin.
const cases = [
  { id: "inheritance", source: "classDiagram\nAnimal <|-- Duck" },
  { id: "realization", source: "classDiagram\nShape <|.. Shape2" },
  { id: "composition", source: "classDiagram\nCar *-- Wheel" },
  { id: "aggregation", source: "classDiagram\nCompany o-- Employee" },
  { id: "association", source: "classDiagram\nA --> B" },
  { id: "dependency-dashed", source: "classDiagram\nC ..> D" },
  { id: "solid-link", source: "classDiagram\nE -- F" },
  { id: "composition-both-ends", source: "classDiagram\nP *--* Q" },
  { id: "aggregation-both-ends", source: "classDiagram\nR o--o S" },
  { id: "labeled-edge", source: "classDiagram\nOwner --> Pet : owns" },
  {
    id: "compartments",
    source: "classDiagram\nclass Duck {\n  +String beakColor\n  +swim()\n  +quack()\n}",
  },
  { id: "bare-classes", source: "classDiagram\nclass X\nclass Y" },
  {
    id: "compartments-full",
    source:
      "classDiagram\nclass Animal {\n  <<interface>>\n  +int age\n  +String gender\n  +isMammal() bool\n  +mate()\n}\nAnimal <|-- Duck",
  },
  { id: "note-for", source: 'classDiagram\nclass Duck\nnote for Duck "can fly"' },
  {
    id: "note-beside",
    source: 'classDiagram\nclass Service\nnote for Service "A folded note"\nclass Client\nClient --> Service : uses',
  },
  {
    id: "namespace",
    source: "classDiagram\nnamespace Base {\n  class Inside\n}\nclass Outside\nOutside ..> Inside",
  },
  {
    id: "namespace-nested",
    source:
      "classDiagram\nnamespace Outer {\n  namespace Inner {\n    class Deep\n  }\n  class Shallow\n}\nclass User\nUser --> Deep\nUser --> Shallow",
  },
  { id: "direction-rl", source: "classDiagram\ndirection RL\nclass A\nclass B\nclass C\nA --> B\nB --> C" },
  { id: "direction-bt", source: "classDiagram\ndirection BT\nclass A\nclass B\nclass C\nA --> B\nB --> C" },
  { id: "cardinality", source: 'classDiagram\nOrder "1" --> "*" LineItem : contains' },
  { id: "generics", source: "classDiagram\nclass Container~T~ {\n  +T item\n  +get() T\n}" },
  {
    id: "diamond",
    source: "classDiagram\nBase <|-- Left\nBase <|-- Right\nLeft ..> Right : uses\nRight ..> Base : calls",
  },
  { id: "cjk-labels", source: "classDiagram\nclass 用户 {\n  +姓名\n  +登录()\n}\n用户 --> 订单 : 下单" },
  { id: "self-relationship", source: 'classDiagram\nNode "0..1" -- "0..*" Node : links' },
  {
    id: "many-classes",
    source:
      "classDiagram\nclass Alpha\nclass Beta\nclass Gamma\nclass Delta\nclass Epsilon\nAlpha --> Beta\nAlpha --> Gamma\nBeta --> Delta\nGamma --> Delta\nDelta --> Epsilon : finally",
  },
  // A namespace no relationship leaves is laid out as a unit of its own,
  // which is where its padding shows: with a sibling, nested, with a title
  // wider than its content and across directions.
  {
    id: "namespace-closed",
    source: "classDiagram\nnamespace Region {\n  class Alpha\n  class Beta\n}\nclass Gamma\nAlpha --> Beta : inside",
  },
  {
    id: "namespace-chain",
    source:
      "classDiagram\nnamespace Outer {\n  class A\n  namespace Inner {\n    class B\n    class C\n  }\n}\nB --> C : inner",
  },
  {
    id: "namespace-wide-title",
    source:
      'classDiagram\nnamespace Domain["A namespace label far wider than its only class"] {\n  class X\n}\nclass Y',
  },
  {
    id: "namespace-direction-lr",
    source: "classDiagram\ndirection LR\nnamespace Base {\n  class Inside\n}\nclass Outside\nOutside ..> Inside",
  },
  {
    id: "note-in-namespace",
    source:
      'classDiagram\nnamespace Host {\n  class Service\n  note for Service "listens on 8080"\n}\nclass Client\nClient --> Service : call',
  },
  { id: "cardinality-markers", source: 'classDiagram\nOrder "1" *-- "0..*" LineItem : contains\nCustomer "1" o-- "1..*" Order : places' },
  {
    id: "padding-config",
    source:
      '%%{init: {"class": {"padding": 20}}}%%\nclassDiagram\nclass Wide {\n  +int value\n}\nnote for Wide "padded"\nnamespace Box {\n  class Inner\n}\nWide --> Inner : holds',
  },
];

const { mermaidRootArgument, output, chromeArgument } = generatorArguments(
  path.join("tests", "fixtures", "mermaid", "class-geometry.json"),
);
const { results } = await captureGeometry({
  family: "class",
  cases,
  describe: describeClassGeometry,
  mermaidRootArgument,
  chromeArgument,
  // The renderer's own tables of label and cardinality groups, by path id.
  auxiliaryExport: "terminalLabels",
});

const root = geometryFixture(
  "classDiagram.render classes, notes, relationship paths, namespaces and canvas (first-class normalized)",
  "Real mermaid 11.16.0 classDiagram geometry captured via headless Chrome with " +
    "the bundled Noto fonts. Every class and note with its centre, its outline " +
    "(in the node's own coordinates), its dividers (from the node's left edge to " +
    "its right, as the first and last point of the divider path) and its text rows " +
    "(annotation, name, members, methods; the note's text), every relationship " +
    "with its paths (the waypoints the layout gave each, which the path carries, " +
    "and the path's data), its pattern, markers, label and cardinalities (their " +
    "text box and the box clipped to), every " +
    "namespace with its box and title, and the canvas (the content's bounding " +
    "box, as the SVG's getBBox() reports it), all but the node-local " +
    "parts normalized to the centre of the first declared class. Compare, " +
    "through the render cache, against classdiagram::ClassScene.",
  results,
);
root.fixtureSha256 = createHash("sha256").update(JSON.stringify(root)).digest("hex");
fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
console.log(`Wrote ${output} (${results.length} cases)`);
