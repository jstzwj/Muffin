// Single source of truth for the Level-3 pixel golden matrix (milestone G3).
// Both the Chrome golden generator (generate_mermaid_golden_pixel.mjs) and the
// native test (MermaidGoldenPixelTest) consume this via the manifest, so adding a
// case is a one-line edit here. Axes covered @1x DPR include rich node, edge,
// and cluster labels. Look variants, CJK/bidi, and 2x DPR remain separate axes.

export const integrationSource = [
  "flowchart TB",
  "subgraph S[Group]",
  "A[Alpha] -->|go| B((Beta)):::clsB",
  "end",
  "B --x C{Decision}",
  "C --o D[Delta]",
  "D --- E[Plain]",
  "E -.-> F[Dotted]",
  "F ==> G[Thick]",
  "style A fill:#ff0000,stroke:#000000,stroke-width:2px",
  "classDef clsB fill:#aaaaaa,color:#ffffff",
  "linkStyle 4 stroke:#0000ff,stroke-width:3px",
].join("\n");

export const allThemes = [
  "default", "base", "dark", "forest", "neutral",
  "neo", "neo-dark", "redux", "redux-dark", "redux-color", "redux-dark-color",
];

export const pixelThemes = allThemes;

export const neoShapeShortNames = [
  "rect", "rounded", "stadium", "fr-rect", "circle", "diam", "hex",
  "trap-b", "trap-t", "lean-r", "lean-l", "odd", "flag", "dbl-circ",
  "fr-circ", "sm-circ", "notch-rect", "lin-rect", "text", "bang", "cloud",
  "doc", "docs", "tag-doc", "lin-doc", "cyl", "datastore", "h-cyl",
  "lin-cyl", "bow-rect", "tri", "flip-tri", "hourglass", "fork", "f-circ",
  "notch-pent", "curv-trap", "sl-rect", "win-pane", "div-rect", "delay",
  "brace", "brace-r", "braces", "bolt", "cross-circ", "st-rect", "tag-rect",
];

function shapeSource(shapes, direction = "LR") {
  const nodes = shapes.map((shape, index) =>
    `N${index}@{ shape: ${shape}, label: "${shape}" }`);
  const chain = shapes.map((_, index) => `N${index}`).join(" --> ");
  return [`flowchart ${direction}`, ...nodes, chain].join("\n");
}

const table = [];
const cjkSource = 'flowchart LR\nA[中文标签] -->|处理| B[日本語テキスト]';
const bidiSource = 'flowchart LR\nA["שלום עולם"] -->|"مرحبا بالعالم"| B["English العربية"]';
const mathSource = 'flowchart LR\nA["`Value $$x^2 + \\frac{1}{2}$$`"] --> B[Plain]';
const mixedSource = 'flowchart LR\nA["中文 abc שלום"] --> B["日本語 العربية 123"]';
const complexClusterSource = [
  "flowchart TB", "subgraph Outer[Outer 中文]",
  "subgraph Middle[Middle שלום]", "subgraph Inner[Inner العربية]",
  "A[中文] --> B[שלום]", "end", "C[Gamma]", "end", "D[Delta]", "end",
].join("\n");
// Axis 1 — the legacy-compatible themes on the style-matrix integration diagram.
for (const theme of pixelThemes) {
  table.push({ id: `theme-${theme}`, theme, source: integrationSource });
}
// Axis 2 — the four rank directions (isolated chain so direction is the only variable).
for (const dir of ["TB", "BT", "LR", "RL"]) {
  table.push({
    id: `dir-${dir}`,
    theme: "default",
    source: `flowchart ${dir}\nA[Alpha] --> B[Beta] --> C[Gamma] --> D[Delta]`,
  });
}
// Axis 3 — edge styles + arrowhead markers (point/cross/circle/open, normal/dotted/thick).
table.push({
  id: "edge-marker-matrix",
  theme: "default",
  source: [
    "flowchart LR",
    "A[Start] --> B[Point]",
    "B --x C[Cross]",
    "C --o D[Circle]",
    "D --- E[Open]",
    "E -.-> F[Dotted]",
    "F ==> G[Thick]",
    "A o--o H[CircleBoth]",
    "H x--x I[CrossBoth]",
  ].join("\n"),
});
table.push({
  id: "look-hand-drawn-cluster-1_5x",
  theme: "default",
  look: "handDrawn",
  handDrawnSeed: 23,
  fontMode: "noto",
  dpr: 1.5,
  source: [
    "flowchart TB",
    'subgraph G["Sketch Group"]',
    'A@{ shape: doc, label: "Document" } --> B@{ shape: cyl, label: "Store" }',
    "end",
    'B --> C@{ shape: braces, label: "Brace" }',
  ].join("\n"),
});
// Axis 4 - canonical shapes (a representative spread; all 48 are L2-verified).
table.push({
  id: "shape-matrix",
  theme: "default",
  source: [
    "flowchart LR",
    "A[Rect] --> B(Round)",
    "B --> C((Circle))",
    "C --> D{Diamond}",
    "D --> E[/Trapezoid/]",
    "E --> F[(Database)]",
    "F --> G{{Hexagon}}",
    "G --> H([Stadium])",
  ].join("\n"),
});
// Axis 4b - neo look is a geometry/rendering axis, independent from theme.
table.push({
  id: "look-neo-shape-matrix",
  theme: "neo",
  look: "neo",
  source: [
    "flowchart LR",
    "A[Rect] --> B(Round)",
    "B --> C((Circle))",
    "C --> D{Diamond}",
    "D --> E[(Database)]",
    "E --> F{{Hexagon}}",
    "F --> G([Stadium])",
  ].join("\n"),
});
table.push({
  id: "look-neo-edge-markers",
  theme: "neo",
  look: "neo",
  source: [
    "flowchart LR",
    "A[Start] --> B[Point]",
    "B --x C[Cross]",
    "C --o D[Circle]",
    "A o--o E[CircleBoth]",
    "E x--x F[CrossBoth]",
  ].join("\n"),
});
for (let start = 0; start < neoShapeShortNames.length; start += 7) {
  const ordinal = String(start / 7 + 1).padStart(2, "0");
  table.push({
    id: `look-neo-shapes-${ordinal}`,
    theme: "neo",
    look: "neo",
    fontMode: "noto",
    source: shapeSource(neoShapeShortNames.slice(start, start + 7)),
  });
}
const neoDarkShapeDprs = [1, 1.25, 1.5, 2, 1.25, 1.5, 2];
for (let start = 0; start < neoShapeShortNames.length; start += 7) {
  const index = start / 7;
  const ordinal = String(index + 1).padStart(2, "0");
  table.push({
    id: `look-neo-dark-shapes-${ordinal}-${String(neoDarkShapeDprs[index]).replace(".", "_")}x`,
    theme: "neo-dark",
    look: "neo",
    fontMode: "noto",
    dpr: neoDarkShapeDprs[index],
    source: shapeSource(neoShapeShortNames.slice(start, start + 7)),
  });
}
const reduxStructureSource = [
  "flowchart TB",
  "subgraph Group[Redux Group]",
  'A@{ shape: doc, label: "Document" } --> B@{ shape: cyl, label: "Store" }',
  'B --> C@{ shape: st-rect, label: "Stacked" }',
  "end",
  'C --> D@{ shape: diam, label: "Decision" }',
].join("\n");
for (const theme of ["redux", "redux-dark", "redux-color", "redux-dark-color"]) {
  table.push({
    id: `look-${theme}-structure`,
    theme,
    look: "neo",
    fontMode: "noto",
    source: reduxStructureSource,
  });
}
table.push({
  id: "look-hand-drawn-seed-17",
  theme: "default",
  look: "handDrawn",
  handDrawnSeed: 17,
  fontMode: "noto",
  emptyMaxMismatchRatio: 0.15,
  source: [
    "flowchart LR",
    "A[Rectangle] --> B((Circle))",
    "B --> C{Decision}",
    "C --> D[(Store)]",
  ].join("\n"),
});
const handDrawnDirections = ["TB", "BT", "LR", "RL", "TB", "BT", "LR"];
const handDrawnDprs = [1, 1.5, 2, 1, 1.5, 2, 1];
for (let start = 0; start < neoShapeShortNames.length; start += 7) {
  const index = start / 7;
  const ordinal = String(index + 1).padStart(2, "0");
  table.push({
    id: `look-hand-drawn-shapes-${ordinal}-${handDrawnDirections[index]}-${String(handDrawnDprs[index]).replace(".", "_")}x`,
    theme: "default",
    look: "handDrawn",
    handDrawnSeed: 101 + index,
    fontMode: "noto",
    dpr: handDrawnDprs[index],
    emptyMaxMismatchRatio: 0.15,
    ...(index === 5 ? { textGlyphIou: 0.5 } : {}),
    source: shapeSource(neoShapeShortNames.slice(start, start + 7),
                        handDrawnDirections[index]),
  });
}
table.push({
  id: "look-hand-drawn-cluster-self-marker-cjk-bidi-2x",
  theme: "default",
  look: "handDrawn",
  handDrawnSeed: 131,
  fontMode: "noto",
  dpr: 2,
  emptyMaxMismatchRatio: 0.15,
  source: [
    "flowchart TB",
    'subgraph Outer["\u4e2d\u6587 cluster"]',
    'subgraph Inner["\u05e9\u05dc\u05d5\u05dd \u0627\u0644\u0639\u0627\u0644\u0645"]',
    'A@{ shape: doc, label: "\u4e2d\u6587" } --> A',
    'A o--o B@{ shape: cyl, label: "\u0645\u0631\u062d\u0628\u0627" }',
    "end", "end",
  ].join("\n"),
});
// Axis 5 - HTML, Markdown, and Math label rendering.
table.push({
  id: "label-html",
  theme: "default",
  source: 'flowchart LR\nA["<b>Bold</b> &amp; <i>italic</i><br/>next"] --> B[Plain]',
});
table.push({
  id: "label-markdown",
  theme: "default",
  source: 'flowchart LR\nA["`**Bold** and *italic*<br/>next`"] --> B[Plain]',
});
table.push({
  id: "label-markdown-break-math",
  theme: "default",
  source: 'flowchart LR\nA["`**Bold**<br/>$$x^2 + 1$$`"] --> B[Plain]',
});
table.push({
  id: "label-math",
  theme: "default",
  source: mathSource,
});
for (const fixture of [
  { id: "flow-math-crop-fraction", mathCropKind: "fraction",
    source: 'flowchart LR\nA["`$$\\frac{x+1}{y-1}$$`"] --> B[Plain]' },
  { id: "flow-math-crop-radical", mathCropKind: "radical", dpr: 1.25,
    source: 'flowchart LR\nA["`$$\\sqrt{x+1}$$`"] --> B[Plain]' },
  { id: "flow-math-crop-supsub", mathCropKind: "supsub", dpr: 1.5,
    source: 'flowchart LR\nA["`$$x_i^2$$`"] --> B[Plain]' },
  { id: "flow-math-crop-array", mathCropKind: "array", dpr: 2,
    source: 'flowchart LR\nA["`$$\\begin{matrix}a\\end{matrix}$$`"] --> B[Plain]' },
  { id: "flow-math-crop-accent", mathCropKind: "accent", theme: "dark",
    source: 'flowchart LR\nA["`$$\\overbrace{x+y}^{n}$$`"] --> B[Plain]' },
  { id: "flow-math-crop-root-index", mathCropKind: "root-index", dpr: 2,
    theme: "dark",
    source: 'flowchart LR\nA["`$$\\sqrt[3]{x+1}$$`"] --> B[Plain]' },
]) {
  table.push({ ...fixture, theme: fixture.theme ?? "default",
               fontMode: "noto", mathCrop: true });
}
table.push({
  id: "edge-label-rich",
  theme: "default",
  source: 'flowchart LR\nA[Start] -- "`**bold** $$x^2$$`" --> B[Finish]',
});
table.push({
  id: "cluster-label-rich",
  theme: "default",
  source: 'flowchart TB\nsubgraph S["`**Group** $$x^2$$`"]\nA[Inside]\nend',
});
table.push({
  id: "terminal-and-long-edge-label",
  theme: "default",
  source: [
    "flowchart LR",
    'A@{ shape: terminal, label: "Terminal label" }',
    'A -->|"A deliberately long edge label that wraps at the upstream limit"| B[Finish]',
  ].join("\n"),
});
// Axis 6 - CJK and bidirectional shaping.
table.push({ id: "label-cjk", theme: "default", source: cjkSource });
table.push({ id: "label-bidi", theme: "default", source: bidiSource });
// Axis 7 - true high-DPI rasterization (Chrome and native both render at 2x).
table.push({ id: "integration-2x", theme: "default", source: integrationSource, dpr: 2 });
table.push({ id: "label-cjk-2x", theme: "default", source: cjkSource, dpr: 2 });
table.push({ id: "label-bidi-2x", theme: "default", source: bidiSource, dpr: 2 });
table.push({ id: "label-math-2x", theme: "default", source: mathSource, dpr: 2 });
table.push({ id: "integration-1_25x", theme: "default", source: integrationSource, dpr: 1.25 });
table.push({ id: "integration-1_5x", theme: "dark", source: integrationSource, dpr: 1.5 });
table.push({ id: "label-mixed-1_5x", theme: "forest", source: mixedSource, dpr: 1.5 });
table.push({ id: "label-cjk-dark", theme: "dark", source: cjkSource });
table.push({ id: "label-bidi-neutral", theme: "neutral", source: bidiSource });
// Axis 7b - bundled fonts. These cases are independent of host font fallback.
table.push({
  id: "font-noto-latin",
  theme: "default",
  fontMode: "noto",
  source: "flowchart LR\nA[Fixed Noto] --> B[Deterministic metrics]",
});
table.push({ id: "font-noto-cjk", theme: "default", fontMode: "noto", source: cjkSource });
table.push({
  id: "font-noto-arabic",
  theme: "default",
  fontMode: "noto",
  source: 'flowchart LR\nA["مرحبا"] --> B["العالم"]',
});
table.push({
  id: "font-noto-hebrew",
  theme: "default",
  fontMode: "noto",
  source: 'flowchart LR\nA["שלום"] --> B["עולם"]',
});
table.push({ id: "font-noto-mixed", theme: "default", fontMode: "noto", source: mixedSource });
table.push({
  id: "font-system-fallback-mixed",
  theme: "default",
  fontMode: "system",
  enforceInterior: false,
  source: mixedSource,
});
const neoDarkClusterSource = [
  "flowchart TB",
  'subgraph Outer["外层 Outer"]',
  "direction LR",
  'subgraph Inner["שלום Inner"]',
  "direction TB",
  'A@{ shape: doc, label: "文档" } --> B@{ shape: lin-cyl, label: "قاعدة" }',
  'B --> C@{ shape: st-rect, label: "Stacked" }',
  "end",
  'D@{ shape: braces, label: "Brace" }',
  "end",
  "A --> D",
  'D --> E@{ shape: tag-doc, label: "Tagged" }',
].join("\n");
for (const dpr of [1, 1.25, 1.5, 2]) {
  table.push({
    id: `look-neo-dark-cluster-${String(dpr).replace(".", "_")}x`,
    theme: "neo-dark",
    look: "neo",
    fontMode: "noto",
    dpr,
    source: neoDarkClusterSource,
  });
}
table.push({ id: "complex-cluster-1_25x", theme: "neutral", source: complexClusterSource, dpr: 1.25 });
// Axis 8 - recursively extracted nested clusters.
table.push({
  id: "recursive-cluster-three-level",
  theme: "default",
  source: [
    "flowchart TB",
    "subgraph Outer[Outer Group]",
    "subgraph Middle[Middle Group]",
    "subgraph Inner[Inner Group]",
    "A[Alpha] --> B[Beta]",
    "end",
    "C[Gamma]",
    "end",
    "D[Delta]",
    "end",
  ].join("\n"),
});
table.push({
  id: "compound-self-parallel",
  theme: "default",
  source: [
    "flowchart TB",
    "subgraph Outer[Outer]",
    "direction LR",
    "subgraph Inner[Inner]",
    "direction TB",
    "A[Alpha] --> A",
    "A --> B[Beta]",
    "A --> B",
    "end",
    "B --> C[Gamma]",
    "end",
    "C --> D[Delta]",
  ].join("\n"),
});
table.push({
  id: "cluster-cross-layer-explicit-direction",
  theme: "default",
  source: [
    "flowchart TB",
    "subgraph Left[Left]",
    "direction LR",
    "A[Alpha] --> B[Beta]",
    "end",
    "subgraph Right[Right]",
    "direction RL",
    "C[Gamma] --> D[Delta]",
    "end",
    "A --> D",
    "B --> C",
  ].join("\n"),
});
table.push({
  id: "animated-edge-static-initial",
  theme: "default",
  animationState: "initial",
  source: [
    "flowchart LR",
    "A[Alpha] edgeFast@--> B[Beta]",
    "edgeFast@{ animate: true, animation: fast, curve: linear }",
  ].join("\n"),
});
export const cases = table;
