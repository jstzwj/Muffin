// A corpus of random state diagrams, captured with the geometry capture of
// the state geometry fixture: plain, described and titled states, start and
// end pseudo-states, choices, forks and joins, notes on either side of states
// in and out of composites, nested composites, transitions into and out of
// composites, self transitions and labels that wrap, in combinations the
// hand-written corpus does not reach.
//
//   node scripts/generate_mermaid_state_geometry_fuzz.mjs \
//     [mermaid-root] [output-json] [chrome-exe] [seed] [case-count]
// Env override: MERMAID_REFERENCE_ROOT (takes precedence over the default).
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/state-geometry-fuzz.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe,
//           seed 20261127, 200 cases.

import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { captureGeometry, generatorArguments, geometryFixture } from "./mermaid_geometry_capture.mjs";
import { describeStateGeometry } from "./mermaid_state_geometry_capture.mjs";

const { mermaidRootArgument, output, chromeArgument } = generatorArguments(
  path.join("tests", "fixtures", "mermaid", "state-geometry-fuzz.json"),
);
const seed = Number(process.argv[4] ?? "20261127") >>> 0;
const caseCount = Number(process.argv[5] ?? "200");
if (!Number.isInteger(caseCount) || caseCount < 1 || caseCount > 5000)
  throw new Error(`Invalid case count: ${caseCount}`);

// xorshift32: the same seed gives the same corpus, on every platform.
let state = seed || 1;
const random = () => {
  state ^= state << 13;
  state ^= state >>> 17;
  state ^= state << 5;
  state >>>= 0;
  return state / 4294967296;
};
const chance = (probability) => random() < probability;
const integer = (min, max) => min + Math.floor(random() * (max - min + 1));
const pick = (values) => values[Math.floor(random() * values.length)];

const stateNames = ["Idle", "Running", "Done", "Pending", "空闲", "处理中", "AwaitingUserConfirmation"];
const compositeNames = ["Outer", "Inner", "Region", "区域"];
const descriptions = ["waiting for input", "等待输入", "a description long enough to be wrapped by the two hundred pixel limit of a state's label, several words over", "row one"];
const labels = ["go", "完成", "a transition label long enough to be wrapped by the two hundred pixel limit of the label, and then some", "retry", "submit", ""];
const notes = ["a note", "备注", "a note whose text is longer than most states are wide"];

const cases = [];
for (let index = 0; index < caseCount; ++index) {
  const lines = ["stateDiagram-v2"];
  if (chance(0.2)) lines.push("direction LR");
  const plain = [];
  const composites = [];
  const pseudo = [];
  const everything = [];
  const declare = (indent, list) => {
    const name = pick(stateNames);
    list.push(name);
    everything.push(name);
    const rows = integer(0, 2);
    lines.push(`${indent}state ${name}`);
    for (let row = 0; row < rows; ++row) lines.push(`${indent}${name} : ${pick(descriptions)}`);
    return name;
  };
  const note = (indent, target) =>
    lines.push(`${indent}note ${pick(["right", "left"])} of ${target} : ${pick(notes)}`);

  const compositeCount = integer(0, 2);
  for (let composite = 0; composite < compositeCount; ++composite) {
    const name = pick(compositeNames);
    composites.push(name);
    everything.push(name);
    lines.push(`state ${name} {`);
    const own = [];
    const innerCount = integer(1, 2);
    for (let inner = 0; inner < innerCount; ++inner) own.push(declare("  ", own));
    if (chance(0.4)) lines.push(`  [*] --> ${own[0]}`);
    if (chance(0.3)) {
      const nested = pick(compositeNames.filter((candidate) => candidate !== name));
      composites.push(nested);
      everything.push(nested);
      lines.push(`  state ${nested} {`);
      const deep = declare("    ", own);
      lines.push(`    [*] --> ${deep}`);
      lines.push("  }");
      if (chance(0.5)) lines.push(`  ${own[0]} --> ${nested}`);
    }
    if (chance(0.4)) lines.push(`  ${own[own.length - 1]} --> [*]`);
    if (chance(0.3)) note("  ", pick(own));
    lines.push("}");
    if (chance(0.3)) lines.push(`${name} : ${pick(descriptions)}`);
  }
  const plainCount = integer(1, 3);
  for (let outer = 0; outer < plainCount; ++outer) declare("", plain);
  if (chance(0.25)) {
    pseudo.push("c");
    everything.push("c");
    lines.push("state c <<choice>>");
  }
  if (chance(0.2)) {
    pseudo.push("f");
    everything.push("f");
    lines.push("state f <<fork>>");
  }
  if (chance(0.2)) {
    pseudo.push("j");
    everything.push("j");
    lines.push("state j <<join>>");
  }
  if (chance(0.8)) lines.push(`[*] --> ${pick(everything)}`);
  const transitionCount = integer(2, 7);
  for (let transition = 0; transition < transitionCount; ++transition) {
    const from = chance(0.15) ? "[*]" : pick(everything);
    const to = chance(0.15) ? "[*]" : pick(everything);
    if (from === "[*]" && to === "[*]") continue;
    const label = pick(labels);
    lines.push(`${from} --> ${to}${label ? ` : ${label}` : ""}`);
  }
  for (let index2 = 0, end = integer(0, 2); index2 < end; ++index2) note("", pick(everything.filter((name) => !pseudo.includes(name))));

  const body = lines.join("\n");
  cases.push({ id: `fuzz-${String(index).padStart(3, "0")}`, source: body });
}

const { results } = await captureGeometry({
  family: "state-fuzz",
  cases,
  describe: describeStateGeometry,
  mermaidRootArgument,
  chromeArgument,
  skipFailed: true,
});
const refused = results.filter((result) => result.error);
const captured = results.filter((result) => !result.error);

const root = geometryFixture(
  "stateDiagram.render fuzz corpus (random state diagrams, first-state normalized)",
  "Random state diagrams from a seeded generator, captured like state-geometry.json " +
    "(real mermaid 11.16.0, headless Chrome, the bundled Noto fonts): every state, " +
    "note and pseudo-state with its box and text, every composite, every transition " +
    "and the canvas. The cases the generator offers that mermaid refuses are left " +
    "out (`refused`).",
  captured,
);
root.generator = {
  name: "seeded-random-state-diagrams",
  seed,
  caseCount: cases.length,
  refused: refused.map((result) => ({ id: result.id, source: result.source, error: result.error })),
};
root.fixtureSha256 = createHash("sha256").update(JSON.stringify(root)).digest("hex");
fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
console.log(`Wrote ${output} (${captured.length} cases, ${refused.length} refused by mermaid)`);
