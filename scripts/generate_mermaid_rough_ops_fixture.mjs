import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "rough-ops.json"),
);
const roughBundle = path.join(path.dirname(mermaidRoot), "roughjs", "bundled", "rough.esm.js");
const { default: rough } = await import(pathToFileURL(roughBundle));
const generator = rough.generator();
const cases = [
  { id: "line-default", kind: "line", args: [1, 2, 101, 42], options: { seed: 17 } },
  { id: "line-custom", kind: "line", args: [-8, 4, 620, 25], options: { seed: 91, roughness: 1.7, bowing: 0.35, maxRandomnessOffset: 3.5 } },
  { id: "line-single", kind: "line", args: [0, 0, 80, 60], options: { seed: 7, disableMultiStroke: true } },
  { id: "line-preserve", kind: "line", args: [5, 9, 95, 41], options: { seed: 11, preserveVertices: true } },
  { id: "rect-hachure", kind: "rectangle", args: [-20, -10, 90, 55], options: { seed: 17, fill: "#ececff", stroke: "#9370db", strokeWidth: 2 } },
  { id: "rect-hachure-single-fill", kind: "rectangle", args: [2, 4, 83, 47], options: { seed: 19, fill: "#ddd", fillWeight: 1.25, disableMultiStrokeFill: true } },
  { id: "rect-solid", kind: "rectangle", args: [3, 7, 75, 40], options: { seed: 23, fill: "#fff", fillStyle: "solid", disableMultiStrokeFill: true } },
  { id: "polygon-hachure", kind: "polygon", args: [[[0, 0], [80, 10], [62, 70], [8, 58]]], options: { seed: 31, fill: "#ddd", hachureAngle: -23, hachureGap: 5 } },
  { id: "polygon-solid", kind: "polygon", args: [[[0, 0], [70, 5], [55, 52], [9, 61]]], options: { seed: 37, fill: "#ddd", fillStyle: "solid" } },
  { id: "ellipse", kind: "ellipse", args: [20, 30, 120, 65], options: { seed: 43, fill: "#eef" } },
  { id: "ellipse-solid", kind: "ellipse", args: [-10, 3, 88, 48], options: { seed: 44, fill: "#eef", fillStyle: "solid" } },
  { id: "arc-open", kind: "arc", args: [10, 20, 100, 70, -0.7, 2.8, false], options: { seed: 47 } },
  { id: "arc-closed-solid", kind: "arc", args: [0, 0, 90, 50, 0.2, 4.2, true], options: { seed: 53, fill: "#abc", fillStyle: "solid" } },
  { id: "arc-closed-hachure", kind: "arc", args: [4, 8, 110, 64, -0.4, 3.7, true], options: { seed: 57, fill: "#abc" } },
  { id: "svg-path", kind: "path", args: ["M0 0 L80 0 C90 10 90 50 80 60 L0 60 Z"], options: { seed: 59, fill: "#eee", fillStyle: "solid" } },
  { id: "svg-path-hachure", kind: "path", args: ["M0 0 L80 0 C90 10 90 50 80 60 L0 60 Z"], options: { seed: 61, fill: "#eee" } },
  { id: "svg-rounded-path-hachure", kind: "path", args: [
    "M -43.4375 -32.5 H 43.4375 A 5 5 0 0 1 48.4375 -27.5 " +
      "V 27.5 A 5 5 0 0 1 43.4375 32.5 H -43.4375 " +
      "A 5 5 0 0 1 -48.4375 27.5 V -27.5 " +
      "A 5 5 0 0 1 -43.4375 -32.5 Z",
  ], options: { seed: 42, roughness: 0.7, fill: "#000", fillWeight: 4,
    hachureGap: 5.2, stroke: "#000", strokeWidth: 1.3 } },
];

for (const item of cases) {
  item.drawable = generator[item.kind](...item.args, item.options);
}

const operationDigest = createHash("sha256").update(JSON.stringify(cases)).digest("hex");
fs.writeFileSync(output, `${JSON.stringify({
  upstream: { package: "roughjs", version: "4.6.6" },
  operationDigest,
  cases,
}, null, 2)}\n`);
console.log(`Wrote ${cases.length} RoughJS operation cases to ${output}`);
