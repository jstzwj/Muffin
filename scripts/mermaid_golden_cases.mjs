// Single source of truth for the Level-3 pixel golden matrix (milestone G3).
// Both the Chrome golden generator (generate_mermaid_golden_pixel.mjs) and the
// native test (MermaidGoldenPixelTest) consume this via the manifest, so adding a
// case is a one-line edit here. Axes covered @1x DPR, plain labels (the look
// variants / HTML-CJK-bidi labels / 2x DPR axes are deferred to F4 / F-polish;
// the framework absorbs them by adding rows here later).

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
// Axis 5 — nested compound + self/parallel/long edge is DEFERRED: that case
// currently diverges at the LAYOUT level (native 233x572 vs golden 450x570 — a
// compound/self-edge ordering difference), which is milestone C/D territory, not
// pixel rasterization. Pixel-verifying it is blocked on the layout milestone;
// add it back here once compound-crossing / self-edge layout lands.

export const cases = table;
