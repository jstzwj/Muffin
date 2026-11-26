// A corpus of random class diagrams, captured with the geometry capture of
// the class geometry fixture: relationships of every kind with and without
// cardinalities, notes beside classes in and out of namespaces, nested
// namespaces, compartments, generics and labels in several scripts, the
// directions and a configured padding, in combinations the hand-written
// corpus does not reach.
//
//   node scripts/generate_mermaid_class_geometry_fuzz.mjs \
//     [mermaid-root] [output-json] [chrome-exe] [seed] [case-count]
// Env override: MERMAID_REFERENCE_ROOT (takes precedence over the default).
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/class-geometry-fuzz.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe,
//           seed 20261127, 200 cases.

import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { captureGeometry, generatorArguments, geometryFixture } from "./mermaid_geometry_capture.mjs";
import { describeClassGeometry } from "./mermaid_class_geometry_capture.mjs";

const { mermaidRootArgument, output, chromeArgument } = generatorArguments(
  path.join("tests", "fixtures", "mermaid", "class-geometry-fuzz.json"),
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
const shuffled = (values) => {
  const copy = [...values];
  for (let index = copy.length - 1; index > 0; --index) {
    const other = Math.floor(random() * (index + 1));
    [copy[index], copy[other]] = [copy[other], copy[index]];
  }
  return copy;
};

const names = ["Order", "Customer", "LineItem", "Payment", "Address", "Shipment", "用户", "订单", "Container~T~", "Cache~K,V~"];
// The grammar has no bare `|--` or `|..`: the implementation side is
// always written with its triangle.
const relationKinds = ["<|--", "<|..", "*--", "o--", "-->", "--", "..>", "..", "--*", "--o", "*--*", "o--o"];
const cardinalities = ["1", "0..1", "*", "0..*", "1..*", "many"];
const labels = ["owns", "contains", "uses 调用", "a rather long relationship label that wraps nothing", "calls", ""];
const notes = ["a note", "多行 note", "a note whose text is long enough to be wider than several classes put together"];
const namespaceNames = ["Domain", "Core 服务", "Outer", "Inner", "Platform"];

const cases = [];
for (let index = 0; index < caseCount; ++index) {
  const lines = ["classDiagram"];
  const direction = pick(["TB", "TB", "TB", "LR", "RL", "BT"]);
  if (direction !== "TB")
    lines.push(`direction ${direction}`);
  const padding = pick([null, null, null, 4, 8, 20]);
  const declared = [];
  const declare = (indent) => {
    const name = pick(names);
    const withLabel = chance(0.25) && !name.includes("~");
    const classLine = withLabel ? `${name}["${pick(["A label with spaces", "标签 with 文字", "Plain"])}"]` : name;
    declared.push(name);
    const members = [];
    const count = integer(0, 3);
    for (let member = 0; member < count; ++member)
      members.push(pick(["+field: String", "+method()", "*staticField$", "+姓名: 字符串", "+find(byId id) Result?", "-secret"]));
    if (members.length) {
      lines.push(`${indent}class ${classLine} {`);
      if (chance(0.2)) lines.push(`${indent}  <<interface>>`);
      for (const member of shuffled(members)) lines.push(`${indent}  ${member}`);
      lines.push(`${indent}}`);
    } else {
      lines.push(`${indent}class ${classLine}`);
    }
    return name;
  };
  // A namespace is not allowed to be empty, nor is one nested in it.
  const namespaceCount = integer(0, 2);
  for (let namespace = 0; namespace < namespaceCount; ++namespace) {
    const name = pick(namespaceNames);
    const titled = chance(0.4) ? `${name}["${name} services"]` : name;
    lines.push(`namespace ${titled} {`);
    for (let inner = 0, end = integer(1, 2); inner < end; ++inner) declare("  ");
    if (chance(0.4)) {
      const nested = pick(namespaceNames.filter((candidate) => candidate !== name));
      lines.push(`  namespace ${nested} {`);
      for (let inner = 0, end = integer(1, 2); inner < end; ++inner) declare("    ");
      lines.push("  }");
    }
    lines.push("}");
  }
  const outside = integer(1, 3);
  for (let outer = 0; outer < outside; ++outer) declare("");
  for (let note = 0, end = integer(0, 2); note < end; ++note) {
    const target = pick(declared);
    lines.push(`note for ${target} "${pick(notes)}"`);
  }
  const relationCount = integer(1, 6);
  for (let relation = 0; relation < relationCount; ++relation) {
    const kind = pick(relationKinds);
    const [left, right] = chance(0.15) ? [pick(declared), pick(declared)] : [pick(declared), pick(declared)];
    const startCardinality = chance(0.3) ? ` "${pick(cardinalities)}"` : "";
    const endCardinality = chance(0.3) ? ` "${pick(cardinalities)}"` : "";
    const label = pick(labels);
    lines.push(`${left}${startCardinality} ${kind}${endCardinality} ${right}${label ? ` : ${label}` : ""}`);
  }
  const body = lines.join("\n");
  cases.push({
    id: `fuzz-${String(index).padStart(3, "0")}`,
    source: padding === null ? body : `%%{init: {"class": {"padding": ${padding}}}}%%\n${body}`,
  });
}

const { results } = await captureGeometry({
  family: "class-fuzz",
  cases,
  describe: describeClassGeometry,
  mermaidRootArgument,
  chromeArgument,
  auxiliaryExport: "terminalLabels",
  skipFailed: true,
});
const refused = results.filter((result) => result.error);
const captured = results.filter((result) => !result.error);

const root = geometryFixture(
  "classDiagram.render fuzz corpus (random class diagrams, first-class normalized)",
  "Random class diagrams from a seeded generator, captured like class-geometry.json " +
    "(real mermaid 11.16.0, headless Chrome, the bundled Noto fonts): every class, " +
    "note, relationship path, label, cardinality, namespace and the canvas. The " +
    "cases the generator offers that mermaid refuses are left out (`refused`).",
  captured,
);
root.generator = {
  name: "seeded-random-class-diagrams",
  seed,
  caseCount: cases.length,
  refused: refused.map((result) => ({ id: result.id, source: result.source, error: result.error })),
};
root.fixtureSha256 = createHash("sha256").update(JSON.stringify(root)).digest("hex");
fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
console.log(`Wrote ${output} (${captured.length} cases, ${refused.length} refused by mermaid)`);
