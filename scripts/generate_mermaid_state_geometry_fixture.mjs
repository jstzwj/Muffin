// Captures real mermaid 11.16.0 stateDiagram-v2 geometry: every state, note
// and pseudo-state with its box and text (a titled state's title, divider and
// description rows), every composite with its boxes and title, every
// transition with the waypoints the layout gave it, the path drawn from them
// and its label, and the canvas, all normalized to the centre of the first
// state the document shows (the origin Muffin's layout oracle subtracts too).
//
//   node scripts/generate_mermaid_state_geometry_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Env override: MERMAID_REFERENCE_ROOT (takes precedence over the default).
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/state-geometry.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { captureGeometry, generatorArguments, geometryFixture } from "./mermaid_geometry_capture.mjs";
import { describeStateGeometry } from "./mermaid_state_geometry_capture.mjs";

// Corpus: linear, branching and converging machines, fork/join, choices,
// self loops, labels, descriptions, notes on either side, composites (nested
// ones too, and ones a relationship from outside enters) and the directions.
const cases = [
  { id: "linear", source: "stateDiagram-v2\n[*] --> Active\nActive --> Inactive\nInactive --> [*]" },
  { id: "branching", source: "stateDiagram-v2\n[*] --> A\nA --> B : yes\nA --> C : no\nB --> [*]\nC --> [*]" },
  { id: "fork-join", source: "stateDiagram-v2\n[*] --> Active\nstate Fork <<fork>>\nActive --> Fork\nFork --> S1\nFork --> S2\nstate Join <<join>>\nS1 --> Join\nS2 --> Join\nJoin --> [*]" },
  { id: "self-loop", source: "stateDiagram-v2\n[*] --> Idle\nIdle --> Idle : retry\nIdle --> [*]" },
  { id: "labeled", source: "stateDiagram-v2\n[*] --> Off\nOff --> On : power\nOn --> Off : power\nOn --> [*] : timeout" },
  { id: "composite", source: "stateDiagram-v2\n[*] --> Outer\nstate Outer {\n  [*] --> Inner1\n  Inner1 --> Inner2 : go\n  Inner2 --> [*]\n}\nOuter --> [*]" },
  { id: "choice", source: "stateDiagram-v2\n[*] --> A\nstate c <<choice>>\nA --> c\nc --> B : yes\nc --> C : no\nB --> [*]\nC --> [*]" },
  { id: "note", source: "stateDiagram-v2\n[*] --> Idle\nIdle --> Running : start\nnote right of Idle : waits for input\nnote right of Running : processing\nRunning --> [*]" },
  { id: "note-left", source: "stateDiagram-v2\n[*] --> Active\nActive --> Done\nnote left of Active : Left note" },
  { id: "diamond", source: "stateDiagram-v2\n[*] --> A\nA --> B\nA --> C\nB --> D\nC --> D\nD --> [*]" },
  { id: "chain", source: "stateDiagram-v2\n[*] --> S1\nS1 --> S2\nS2 --> S3\nS3 --> S4\nS4 --> S5\nS5 --> S6\nS6 --> [*]" },
  { id: "long-names", source: "stateDiagram-v2\n[*] --> AwaitingUserConfirmation\nAwaitingUserConfirmation --> ProcessingPaymentAuthorization : submit\nProcessingPaymentAuthorization --> [*]" },
  { id: "titled-state", source: "stateDiagram-v2\n[*] --> Detailed\nDetailed : first row\nDetailed : second row here\nstate Running {\n  b2 : row one\n  b2 --> C : go\n}\nnote right of C : note text\nDetailed --> Running\nRunning --> [*]" },
  { id: "direction-lr", source: "stateDiagram-v2\ndirection LR\n[*] --> A\nA --> B\nB --> C\nC --> [*]" },
  { id: "two-loops", source: "stateDiagram-v2\n[*] --> A\nA --> A : retry\nA --> B : go\nB --> B : wait\nB --> [*]" },
  { id: "long-description", source: "stateDiagram-v2\n[*] --> A\nA : Waiting for the user to confirm the payment authorization before anything else can proceed\nA --> [*]" },
  {
    id: "nested-composite",
    source: "stateDiagram-v2\n[*] --> Outer\nstate Outer {\n  [*] --> Middle\n  state Middle {\n    [*] --> Deep\n    Deep --> [*]\n  }\n  Middle --> [*]\n}\nOuter --> Done\nDone --> [*]",
  },
  {
    id: "external-into-composite",
    source: "stateDiagram-v2\nstate Target {\n  [*] --> Inner\n}\nOutside --> Inner : enter",
  },
  {
    id: "titled-into-composite",
    source: "stateDiagram-v2\n[*] --> Detailed\nDetailed : first row\nDetailed : second row here\nstate Running {\n  b2 : row one\n  b2 --> C : go\n}\nDetailed --> Running\nRunning --> [*]",
  },
  {
    id: "composite-with-note",
    source: "stateDiagram-v2\n[*] --> Start\nstate Running {\n  b2 --> C : go\n  note right of C : note text\n}\nStart --> Running\nRunning --> [*]",
  },
  { id: "choice-fork-join", source: "stateDiagram-v2\n[*] --> A\nstate c <<choice>>\nA --> c\nc --> B : yes\nc --> C : no\nstate f <<fork>>\nB --> f\nC --> f\nf --> D\nf --> E\nstate j <<join>>\nD --> j\nE --> j\nj --> [*]" },
  {
    id: "long-word",
    source: "stateDiagram-v2\n[*] --> A\nA : SupercalifragilisticexpialidociousSupercalifragilisticexpialidocious\nA --> B : AntidisestablishmentarianismAntidisestablishmentarianism\nB --> [*]",
  },
  { id: "wide-labels", source: "stateDiagram-v2\nA --> B : a much longer transition label with several words\nB --> C : short\nC --> [*]\n[*] --> A" },
];

const { mermaidRootArgument, output, chromeArgument } = generatorArguments(
  path.join("tests", "fixtures", "mermaid", "state-geometry.json"),
);
const { results } = await captureGeometry({ family: "state", cases, describe: describeStateGeometry, mermaidRootArgument, chromeArgument });

const root = geometryFixture(
  "stateDiagram.render states, notes, transitions, composites and canvas (first-state normalized)",
  "Real mermaid 11.16.0 stateDiagram-v2 geometry captured via headless Chrome " +
    "with the bundled Noto fonts. Every state, note and pseudo-state with its " +
    "centre, its box (in its own coordinates), each text box the SVG gives " +
    "its labels (a titled state's title, then the description rows, with the " +
    "height the rows take) and its divider, every composite with each " +
    "rectangle it draws and its title, every transition with the waypoints " +
    "the layout gave it (which the path carries), the path's data, its marker " +
    "and its label, and the canvas (the content's bounding box, as the SVG's " +
    "getBBox() reports it), all but the node-local parts normalized to the " +
    "centre of the first node the document shows. Compare, through the render " +
    "cache, against state::StateScene.",
  results,
);
root.fixtureSha256 = createHash("sha256").update(JSON.stringify(root)).digest("hex");
fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
console.log(`Wrote ${output} (${results.length} cases)`);
