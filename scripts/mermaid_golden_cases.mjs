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

// Themes whose node fill = mainBkg (so the native node-fill default matches
// mermaid) and which therefore have a complete Level-3 pixel golden. The
// neo/redux family is DEFERRED: those themes set nodeBkg ≠ mainBkg and rely on
// the neo look + cScale/borderColorArray colour derivation that F1 has not
// ported; pixel-verifying them is blocked on that F1 follow-up (the registry
// absorbs them by adding rows here once F1 lands).
export const pixelThemes = ["default", "base", "dark", "forest", "neutral"];

const table = [];
const cjkSource = 'flowchart LR\nA[中文标签] -->|处理| B[日本語テキスト]';
const bidiSource = 'flowchart LR\nA["שלום עולם"] -->|"مرحبا بالعالم"| B["English العربية"]';
const mathSource = 'flowchart LR\nA["`Value $$x^2 + \\frac{1}{2}$$`"] --> B[Plain]';
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
// Axis 4 — canonical shapes (a representative spread; full 49 are L2-verified).
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
  id: "label-math",
  theme: "default",
  source: mathSource,
});
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
// Axis 6 - CJK and bidirectional shaping.
table.push({ id: "label-cjk", theme: "default", source: cjkSource });
table.push({ id: "label-bidi", theme: "default", source: bidiSource });
// Axis 7 - true high-DPI rasterization (Chrome and native both render at 2x).
table.push({ id: "integration-2x", theme: "default", source: integrationSource, dpr: 2 });
table.push({ id: "label-cjk-2x", theme: "default", source: cjkSource, dpr: 2 });
table.push({ id: "label-bidi-2x", theme: "default", source: bidiSource, dpr: 2 });
table.push({ id: "label-math-2x", theme: "default", source: mathSource, dpr: 2 });
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
// Axis 5 — nested compound + self/parallel/long edge is DEFERRED: that case
// currently diverges at the LAYOUT level (native 233x572 vs golden 450x570 — a
// compound/self-edge ordering difference), which is milestone C/D territory, not
// pixel rasterization. Pixel-verifying it is blocked on the layout milestone;
// add it back here once compound-crossing / self-edge layout lands.

export const cases = table;
