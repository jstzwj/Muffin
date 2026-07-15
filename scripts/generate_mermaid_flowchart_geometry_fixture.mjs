import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "flowchart-geometry.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")),
);

const cases = [
  { id: "lr-chain", source: "flowchart LR\nA[Alpha] --> B[Beta] --> C[Gamma]" },
  { id: "tb-chain", source: "flowchart TB\nA[Alpha] --> B[Beta] --> C[Gamma]" },
  { id: "tb-branch", source: "flowchart TB\nA[Root] --> B[Left] & C[Right]" },
  { id: "bt-chain", source: "flowchart BT\nA[Alpha] --> B[Beta] --> C[Gamma]" },
  { id: "rl-chain", source: "flowchart RL\nA[Alpha] --> B[Beta] --> C[Gamma]" },
  { id: "cycle", source: "flowchart TB\nA[Alpha] --> B[Beta] --> C[Gamma] --> A" },
  {
    id: "crossing",
    source: "flowchart TB\nA[Top left] --> D[Bottom right]\nB[Top right] --> C[Bottom left]\nA --> C\nB --> D",
  },
  { id: "long-edge", source: "flowchart TB\nA[Start] --> B[One] --> C[Two] --> D[End]\nA --> D" },
  { id: "edge-label", source: "flowchart LR\nA[Start] -->|decision| B[Finish]" },
  {
    id: "cluster",
    source: "flowchart TB\nsubgraph S[Group]\nA[Inside A] --> B[Inside B]\nend\nB --> C[Outside]",
  },
  { id: "multiline-text", source: "flowchart TB\nA[First<br/>Second] --> B[中文标签]" },
  {
    id: "label-html",
    source: 'flowchart LR\nA["<b>Bold</b> &amp; <i>italic</i><br/>next"] --> B[Plain]',
  },
  {
    id: "label-markdown",
    source: 'flowchart LR\nA["`**Bold** and *italic*<br/>next`"] --> B[Plain]',
  },
  {
    id: "label-markdown-break-math",
    source: 'flowchart LR\nA["`**Bold**<br/>$$x^2 + 1$$`"] --> B[Plain]',
  },
  {
    id: "label-math-only",
    source: 'flowchart LR\nA["`$$x^2 + \\frac{1}{2}$$`"] --> B[Plain]',
  },
  {
    id: "label-math",
    source: 'flowchart LR\nA["`Value $$x^2 + \\frac{1}{2}$$`"] --> B[Plain]',
  },
  {
    id: "edge-label-rich",
    source: 'flowchart LR\nA[Start] -- "`**bold** $$x^2$$`" --> B[Finish]',
  },
  {
    id: "cluster-label-plain",
    source: 'flowchart TB\nsubgraph S[Group]\nA[Inside]\nend',
  },
  {
    id: "cluster-label-markdown",
    source: 'flowchart TB\nsubgraph S["`**Group**`"]\nA[Inside]\nend',
  },
  {
    id: "cluster-label-math",
    source: 'flowchart TB\nsubgraph S["`$$x^2$$`"]\nA[Inside]\nend',
  },
  {
    id: "cluster-label-rich",
    source: 'flowchart TB\nsubgraph S["`**Group** $$x^2$$`"]\nA[Inside]\nend',
  },
  {
    id: "label-cjk",
    source: 'flowchart LR\nA[中文标签] -->|处理| B[日本語テキスト]',
  },
  {
    id: "label-bidi",
    source: 'flowchart LR\nA["שלום עולם"] -->|"مرحبا بالعالم"| B["English العربية"]',
  },
  {
    id: "label-cjk-bidi-mixed",
    source: 'flowchart LR\nA["中文 abc שלום"] --> B["日本語 العربية 123"]',
  },
  {
    id: "label-font-fallback",
    source: 'flowchart LR\nA["Arial 中文 العربية 😀"] --> B["한글 Ελληνικά"]',
  },
  {
    id: "label-cjk-bidi-markdown-lines",
    source: 'flowchart LR\nA["`**中文 Bold**<br/>*שלום italic*`"] --> B[End]',
  },
  {
    id: "label-cjk-math",
    source: 'flowchart LR\nA["`值 $$x^2 + \\frac{1}{2}$$ 日本語`"] --> B[End]',
  },
  {
    id: "recursive-cluster-three-level",
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
  },
  {
    id: "compound-self-parallel",
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
  },
  {
    id: "cluster-cross-layer-explicit-direction",
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
  },
  {
    id: "recursive-cluster-mixed-boundary",
    source: [
      "flowchart TB",
      "subgraph Outer[Outer]",
      "subgraph Inner[Inner]",
      "A[Alpha] --> B[Beta]",
      "end",
      "B --> C[Gamma]",
      "end",
    ].join("\n"),
  },
  {
    id: "recursive-cluster-explicit-directions",
    source: [
      "flowchart LR",
      "subgraph Outer[Outer]",
      "direction TB",
      "subgraph Inner[Inner]",
      "direction RL",
      "A[Alpha] --> B[Beta]",
      "end",
      "C[Gamma]",
      "end",
    ].join("\n"),
  },
  {
    id: "basic-shapes",
    source: "flowchart LR\nA[Rectangle] --> B(Rounded) --> C((Circle)) --> D{Diamond}",
  },
  {
    id: "network-simplex",
    source: "flowchart TB\nA[Root] --> B[Branch] --> C[Deep]\nA --> D[Peer D]\nA --> E[Peer E]",
  },
  {
    id: "nested-cluster",
    source: "flowchart TB\nsubgraph Outer\nsubgraph Inner\nA[Alpha] --> B[Beta]\nend\nB --> C[Gamma]\nend\nC --> D[Delta]",
  },
  {
    id: "compound-crossing",
    source: "flowchart TB\nsubgraph Left\nA[Left top] --> B[Left bottom]\nend\nsubgraph Right\nC[Right top] --> D[Right bottom]\nend\nA --> D\nC --> B",
  },
  { id: "self-edge", source: "flowchart TB\nA[Loop] --> A" },
  { id: "self-edge-bt", source: "flowchart BT\nA[Loop] --> A" },
  { id: "self-edge-lr", source: "flowchart LR\nA[Loop] --> A" },
  { id: "self-edge-rl", source: "flowchart RL\nA[Loop] --> A" },
  {
    id: "parallel-labels",
    source: "flowchart LR\nA[Start] -->|one| B[Finish]\nA -->|two| B",
  },
  {
    id: "legacy-shapes",
    source: [
      "flowchart LR",
      "A[Square]",
      "B(Round)",
      "C([Stadium])",
      "D[[Subroutine]]",
      "E[(Database)]",
      "F((Circle))",
      "G>Flag]",
      "H{Decision}",
      "I{{Hexagon}}",
      "J[/Trapezoid\\]",
      "K[\\Inverse/]",
      "L[/Lean/]",
      "M[\\Left\\]",
    ].join("\n"),
  },
  { id: "edge-arrow-types", source: "flowchart LR\nA --> B --x C --o D --- E" },
  { id: "edge-styles", source: "flowchart LR\nA --> B -.-> C ==> D" },
  // Bidirectional (o--o / x--x) and invisible (~~~) edges. These have centred
  // or no markers -> no endpoint clip (markerOffsets has no arrow_circle/cross/
  // open entry), unlike arrow_point which shifts 4px.
  { id: "edge-bidirectional", source: "flowchart LR\nA --> B o--o C x--x D ~~~ E" },
  { id: "curve-linear", curve: "linear", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  { id: "curve-step", curve: "step", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  { id: "curve-cardinal", curve: "cardinal", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  { id: "curve-stepBefore", curve: "stepBefore", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  { id: "curve-stepAfter", curve: "stepAfter", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  // monotoneX needs the monotone axis (x) to vary -> horizontal edge; monotoneY
  // needs y to vary -> vertical edge. Otherwise the slope math hits 0*inf=NaN
  // (which d3 itself produces, yielding a broken path), so the two are paired
  // with LR and TB respectively.
  { id: "curve-monotoneX", curve: "monotoneX", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  { id: "curve-monotoneY", curve: "monotoneY", source: "flowchart TB\nA[Start] --> B[Middle] --> C[End]" },
  {
    id: "edge-long-label-bidirectional",
    source: 'flowchart LR\nA[Start] o--o|"A deliberately long edge label 中文 שלום"| B[Finish]\nB x--x C[Both ends]',
  },
  {
    id: "recursive-cluster-cross-boundary",
    source: [
      "flowchart TB", "subgraph Outer[Outer]", "subgraph Middle[Middle]",
      "subgraph Inner[Inner]", "A[Alpha] --> B[Beta]", "end",
      "B --> C[Gamma]", "end", "C --> D[Delta]", "end",
      "A --> D", "D --> E[Outside]",
    ].join("\n"),
  },
  { id: "curve-bumpX", curve: "bumpX", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  { id: "curve-bumpY", curve: "bumpY", source: "flowchart TB\nA[Start] --> B[Middle] --> C[End]" },
  { id: "curve-catmullRom", curve: "catmullRom", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  { id: "curve-natural", curve: "natural", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]" },
  // Diagonal edges into non-rect shapes (circle, diamond). Verifies that the
  // dagre-wrapper uses dagre's rect-intersected endpoints (not per-shape
  // intersect) — a horizontal approach can't distinguish rect from circle.
  { id: "diag-shapes", source: "flowchart TB\nA[Alpha] --> B((Beta))\nC[Gamma] --> B\nD[Delta] --> E{Zeta}\nF[Phi] --> E" },
  // Per-edge curve override: linkStyle 0 interpolate linear overrides the global
  // "basis" curve for edge 0 only. Edge 0's path is polyline (L), edge 1 is basis (C).
  { id: "edge-interpolate", source: "flowchart LR\nA[Start] --> B[Middle] --> C[End]\nlinkStyle 0 interpolate linear" },
  // Expanded shape registry (milestone E). Each node selects a shape via
  // @{ shape: NAME }. Captures mermaid's node size + shape SVG attributes +
  // alpha silhouette so the native geometry/intersect/sizing can be calibrated.
  {
    id: "expanded-shapes",
    source: [
      "flowchart LR",
      "A[Alpha]@{ shape: tri }",
      "B[Beta]@{ shape: flip-tri }",
      "C[Gamma]@{ shape: hourglass }",
      "D[Delta]@{ shape: notch-pent }",
      "E[Epsilon]@{ shape: card }",
      "F[Zeta]@{ shape: sl-rect }",
      "G[Eta]@{ shape: div-rect }",
      "H[Theta]@{ shape: bolt }",
      "I[Iota]@{ shape: dbl-circ }",
      "J[Kappa]@{ shape: f-circ }",
      "K[Lambda]@{ shape: cross-circ }",
      "M[Nu]@{ shape: text }",
      "N[Xi]@{ shape: datastore }",
      "O[Omicron]@{ shape: tag-rect }",
      "P[Pi]@{ shape: st-rect }",
      "Q[Rho]@{ shape: lin-rect }",
    ].join("\n"),
  },
  // Expanded shape registry (milestone E), batch 2: wave/arc/cylinder shapes.
  // Each node selects a shape via @{ shape: NAME }. Captures mermaid's node size
  // + shape SVG attributes + alpha silhouette so the native geometry/intersect/
  // sizing can be calibrated. Includes the SVG-arc shapes (bang, cloud) whose
  // outline is sampled into a polyline for the native silhouette.
  {
    id: "expanded-shapes-2",
    source: [
      "flowchart LR",
      "A[Alpha]@{ shape: doc }",
      "B[Beta]@{ shape: docs }",
      "C[Gamma]@{ shape: tag-doc }",
      "D[Delta]@{ shape: lin-doc }",
      "E[Epsilon]@{ shape: flag }",
      "F[Zeta]@{ shape: bow-rect }",
      "G[Eta]@{ shape: delay }",
      "H[Theta]@{ shape: curv-trap }",
      "I[Iota]@{ shape: brace }",
      "J[Kappa]@{ shape: brace-r }",
      "K[Lambda]@{ shape: braces }",
      "M[Nu]@{ shape: bang }",
      "N[Xi]@{ shape: cloud }",
      "O[Omicron]@{ shape: sm-circ }",
      "P[Pi]@{ shape: fr-circ }",
      "Q[Rho]@{ shape: h-cyl }",
      "R[Sigma]@{ shape: lin-cyl }",
      "S[Tau]@{ shape: win-pane }",
      "T[Fork]@{ shape: fork }",
    ].join("\n"),
  },
  // Diagonal edges into expanded polygon shapes (triangle, hexagon, card) to
  // exercise per-shape intersect with the intersectLine +0.5 bias. The diamond
  // (basic-shapes/diag-shapes) is the ONLY polygon whose handler undoes the bias
  // (calcIntersect subtracts 0.5); every other polygon keeps it, so its
  // re-intersected endpoints are +0.5 off the true quotient. The native port
  // replicates this (biased intersectLine + diamond-only -0.5 compensation);
  // this golden verifies the bias is applied for non-diamond polygons.
  {
    id: "diag-polygon",
    source: [
      "flowchart TB",
      "A[Alpha] --> T[Tri]@{ shape: tri }",
      "B[Beta] --> T",
      "C[Gamma] --> H[Hex]@{ shape: hexagon }",
      "D[Delta] --> H",
      "E[Epsilon] --> O[Card]@{ shape: card }",
      "F[Zeta] --> O",
    ].join("\n"),
  },
  {
    id: "grammar-double-ended",
    source: [
      "flowchart LR",
      "A[Alpha] o--o B[Beta]",
      "B x==x C[Gamma]",
      "C <--> D[Delta]",
      'D -- "`**markdown edge**`" --> A',
    ].join("\n"),
  },
  {
    id: "grammar-edge-id-action-order",
    source: [
      "flowchart TB",
      "subgraph Ordered[Ordered]",
      "A[Alpha] edgeOne@--> B[Beta]",
      "end",
      "A --> C[Gamma]",
      "edgeOne@{ animate: false, animation: slow, curve: basis }",
      "edgeOne@{ animate: true, animation: fast, curve: linear }",
    ].join("\n"),
  },
  {
    id: "grammar-linkstyle-action-order",
    source: [
      "flowchart LR",
      "A[Alpha] --> B[Beta] --> C[Gamma] --> D[Delta]",
      "linkStyle default stroke:#999",
      "linkStyle 0,1 stroke:#0f0,stroke-width:2px",
      "linkStyle default interpolate basis stroke:#999",
      "linkStyle 0,1 interpolate linear stroke:#f00",
      "linkStyle default interpolate step",
      "linkStyle 2 interpolate stepAfter",
    ].join("\n"),
  },
  {
    id: "grammar-grouped-links",
    source: "flowchart TB\nA[Alpha] & B[Beta] --> C[Gamma] & D[Delta]",
  },
];
const duplicateCaseIds = cases
  .map((fixture) => fixture.id)
  .filter((id, index, ids) => ids.indexOf(id) !== index);
if (duplicateCaseIds.length > 0)
  throw new Error(`Duplicate geometry case ids: ${[...new Set(duplicateCaseIds)].join(", ")}`);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const snapshots = await page.evaluate(
    async ({ cases, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      const number = (value) => Math.round(value * 1000) / 1000;
      const attributes = (element, names) => {
        if (!element) return {};
        const result = {};
        for (const name of names)
          if (element.hasAttribute(name)) result[name] = element.getAttribute(name);
        return result;
      };
      const computedStyle = (element, names) => {
        if (!element) return {};
        const style = getComputedStyle(element);
        const result = {};
        for (const name of names) result[name] = style.getPropertyValue(name);
        return result;
      };
      const textLayout = (label) => {
        if (!label) return [];
        const text = label.querySelector("text");
        const lineElements = text
          ? [...text.querySelectorAll("tspan.text-outer-tspan")]
          : [];
        if (lineElements.length === 0) {
          const rootMatrix = label.getScreenCTM();
          const scaleX = rootMatrix ? Math.hypot(rootMatrix.a, rootMatrix.b) : 1;
          const scaleY = rootMatrix ? Math.hypot(rootMatrix.c, rootMatrix.d) : 1;
          const chars = [];
          let logicalIndex = 0;
          const visit = (node) => {
            if (node.nodeType === Node.ELEMENT_NODE) {
              const element = node;
              if (element.matches(".katex-mathml, annotation, script, style") ||
                  getComputedStyle(element).display === "none") return;
              if (element.matches(".katex")) {
                const rect = element.getBoundingClientRect();
                const style = getComputedStyle(element);
                chars.push({
                  start: logicalIndex++, length: 1, text: "\ufffc",
                  left: rect.left / scaleX, right: rect.right / scaleX,
                  top: rect.top / scaleY, bottom: rect.bottom / scaleY,
                  math: true, fontFamily: style.fontFamily, font: style.font,
                  lineHeight: rect.height / scaleY,
                });
                return;
              }
              for (const child of element.childNodes) visit(child);
              return;
            }
            if (node.nodeType !== Node.TEXT_NODE || !node.parentElement) return;
            const parent = node.parentElement;
            for (let index = 0; index < node.data.length;) {
              const codepoint = node.data.codePointAt(index);
              const length = codepoint > 0xffff ? 2 : 1;
              const range = document.createRange();
              range.setStart(node, index);
              range.setEnd(node, index + length);
              const rect = range.getBoundingClientRect();
              if (rect.width > 0 || rect.height > 0) {
                const style = getComputedStyle(parent);
                chars.push({
                  start: logicalIndex, length, text: node.data.slice(index, index + length),
                  left: rect.left / scaleX, right: rect.right / scaleX,
                  top: rect.top / scaleY, bottom: rect.bottom / scaleY,
                  math: false, fontFamily: style.fontFamily, font: style.font,
                  lineHeight: Number.parseFloat(style.lineHeight) || rect.height / scaleY,
                });
              }
              logicalIndex += length;
              index += length;
            }
          };
          visit(label);
          const grouped = [];
          for (const char of chars) {
            const center = (char.top + char.bottom) / 2;
            let line = grouped.find((candidate) =>
              Math.abs((candidate.top + candidate.bottom) / 2 - center) <= 5.0);
            if (!line) {
              line = { top: char.top, bottom: char.bottom, chars: [] };
              grouped.push(line);
            }
            line.chars.push(char);
            line.top = Math.min(line.top, char.top);
            line.bottom = Math.max(line.bottom, char.bottom);
          }
          grouped.sort((a, b) => a.top - b.top);
          const canvas = document.createElement("canvas");
          const context = canvas.getContext("2d");
          return grouped.map((line) => {
            const left = Math.min(...line.chars.map((char) => char.left));
            const right = Math.max(...line.chars.map((char) => char.right));
            const math = line.chars.some((char) => char.math);
            const lineHeight = math
              ? line.bottom - line.top
              : Math.max(...line.chars.map((char) => char.lineHeight));
            const sample = line.chars.find((char) => !char.math) ?? line.chars[0];
            context.font = sample.font;
            const fontMetrics = context.measureText("Mg");
            const ascent = fontMetrics.actualBoundingBoxAscent;
            const descent = fontMetrics.actualBoundingBoxDescent;
            const directions = line.chars.map((char, index) => {
              const next = line.chars[index + 1];
              const previous = line.chars[index - 1];
              if (next && Math.abs(next.top - char.top) <= 1.0)
                return next.left < char.left;
              if (previous && Math.abs(previous.top - char.top) <= 1.0)
                return char.left < previous.left;
              return getComputedStyle(label).direction === "rtl";
            });
            const logicalRuns = [];
            line.chars.forEach((char, index) => {
              const rtl = directions[index];
              const current = logicalRuns.at(-1);
              if (!current || current.rtl !== rtl || current.math !== char.math ||
                  current.start + current.length !== char.start) {
                logicalRuns.push({ start: char.start, length: char.length,
                                   left: char.left, right: char.right, rtl,
                                   math: char.math, fontFamily: char.fontFamily });
              } else {
                current.length += char.length;
                current.left = Math.min(current.left, char.left);
                current.right = Math.max(current.right, char.right);
              }
            });
            return {
              text: line.chars.map((char) => char.text).join(""),
              width: number(right - left), height: number(lineHeight),
              baseline: math ? null : number((lineHeight - ascent - descent) / 2 + ascent),
              ascent: math ? null : number(ascent),
              descent: math ? null : number(descent),
              runs: logicalRuns.map((run) => ({
                start: run.start, length: run.length,
                x: number(run.left - left), width: number(run.right - run.left),
                rtl: run.rtl, math: run.math, fontFamily: run.fontFamily,
              })).sort((a, b) => a.x - b.x),
            };
          });
        }
        return lineElements.map((line) => {
          const box = line.getBBox();
          const count = line.getNumberOfChars();
          const baselineY = count > 0 ? line.getStartPositionOfChar(0).y : box.y;
          const chars = [];
          for (let index = 0; index < count; ++index) {
            const start = line.getStartPositionOfChar(index);
            const end = line.getEndPositionOfChar(index);
            chars.push({ index, left: Math.min(start.x, end.x),
                         right: Math.max(start.x, end.x), rtl: end.x < start.x });
          }
          const logicalRuns = [];
          for (const char of chars) {
            const current = logicalRuns.at(-1);
            if (!current || current.rtl !== char.rtl || current.start + current.length !== char.index) {
              logicalRuns.push({ start: char.index, length: 1, left: char.left,
                                 right: char.right, rtl: char.rtl });
            } else {
              current.length += 1;
              current.left = Math.min(current.left, char.left);
              current.right = Math.max(current.right, char.right);
            }
          }
          const family = getComputedStyle(line).fontFamily;
          return {
            text: line.textContent ?? "",
            width: number(line.getComputedTextLength()),
            height: number(box.height),
            baseline: number(baselineY - box.y),
            ascent: number(baselineY - box.y),
            descent: number(box.y + box.height - baselineY),
            runs: logicalRuns.map((run) => ({
              start: run.start, length: run.length,
              x: number(run.left - box.x), width: number(run.right - run.left),
              rtl: run.rtl, math: false, fontFamily: family,
            })).sort((a, b) => a.x - b.x),
          };
        });
      };
      const normalizeMarker = (value) => {
        if (!value) return "";
        const semantic = value.match(/(pointEnd|pointStart|circleEnd|circleStart|crossEnd|crossStart)/);
        if (semantic) return semantic[1];
        const match = value.match(/[-_]([A-Za-z]+)\)$/);
        return match ? match[1] : value;
      };
      const allAttributes = (element) => {
        if (!element) return {};
        return Object.fromEntries([...element.attributes].map((attribute) =>
          [attribute.name, attribute.value]));
      };
      const shallowStructure = (element) => element ? {
        tag: element.tagName.toLowerCase(),
        class: element.getAttribute("class") ?? "",
        children: [...element.children].map((child) => ({
          tag: child.tagName.toLowerCase(), class: child.getAttribute("class") ?? "",
        })),
      } : null;
      const relativePath = (pathData, originX, originY, matrix) => {
        let coordinate = 0;
        let pendingX = 0;
        return pathData.replace(/-?\d+(?:\.\d+)?(?:e[-+]?\d+)?/gi, (token) => {
          if (coordinate++ % 2 === 0) {
            pendingX = Number(token);
            return `__x${pendingX}__`;
          }
          const point = new DOMPoint(pendingX, Number(token)).matrixTransform(matrix);
          return `${String(number(point.x - originX))},${String(number(point.y - originY))}`;
        }).replace(/__x-?\d+(?:\.\d+)?(?:e[-+]?\d+)?__,/gi, "");
      };
      const elementMatrix = (element, rootInverse) =>
        rootInverse.multiply(element.getScreenCTM());
      const absoluteBox = (element, rootInverse) => {
        if (!element || !rootInverse) return null;
        const box = element.getBBox();
        const matrix = elementMatrix(element, rootInverse);
        const corners = [
          new DOMPoint(box.x, box.y),
          new DOMPoint(box.x + box.width, box.y),
          new DOMPoint(box.x, box.y + box.height),
          new DOMPoint(box.x + box.width, box.y + box.height),
        ].map((point) => point.matrixTransform(matrix));
        const left = Math.min(...corners.map((point) => point.x));
        const right = Math.max(...corners.map((point) => point.x));
        const top = Math.min(...corners.map((point) => point.y));
        const bottom = Math.max(...corners.map((point) => point.y));
        return { x: left, y: top, width: right - left, height: bottom - top };
      };
      const absoluteOrigin = (element, rootInverse) =>
        new DOMPoint(0, 0).matrixTransform(elementMatrix(element, rootInverse));
      const rasterizeShape = async (shape) => {
        const box = shape.getBBox();
        // Parse the shape's own translate() to place the viewBox at the
        // TRANSFORMED bbox (getBBox returns local geometry, excluding the
        // element's own transform). The regex tolerates arbitrary whitespace —
        // mermaid's tiltedCylinder emits `translate(x, y )` with a trailing
        // space before `)`, which a strict `, *)` pattern silently fails to
        // match (leaving the transform un-applied and the viewBox clipped).
        const transform = shape.getAttribute("transform")?.match(
          /translate\(\s*(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\s*[,\s]\s*(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\s*\)/,
        );
        const boxX = box.x + (transform ? Number(transform[1]) : 0);
        const boxY = box.y + (transform ? Number(transform[2]) : 0);
        const padding = 2;
        const width = Math.ceil(box.width + padding * 2);
        const height = Math.ceil(box.height + padding * 2);
        const clone = shape.cloneNode(true);
        clone.setAttribute("fill", "black");
        clone.setAttribute("stroke", "none");
        clone.removeAttribute("style");
        for (const descendant of clone.querySelectorAll("*")) {
          descendant.setAttribute("fill", "black");
          descendant.setAttribute("stroke", "none");
          descendant.removeAttribute("style");
        }
        const markup = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="${boxX - padding} ${boxY - padding} ${width} ${height}">${clone.outerHTML}</svg>`;
        const image = new Image();
        await new Promise((resolve, reject) => {
          image.onload = resolve;
          image.onerror = reject;
          image.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(markup)}`;
        });
        const canvas = document.createElement("canvas");
        canvas.width = width;
        canvas.height = height;
        canvas.getContext("2d").drawImage(image, 0, 0);
        return { width, height, png: canvas.toDataURL("image/png").split(",")[1] };
      };
      const results = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        const curve = fixture.curve || "basis";
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily: "Arial",
          flowchart: { defaultRenderer: "dagre-wrapper", htmlLabels: false, nodeSpacing: 50, rankSpacing: 50, curve },
        });
        const svgId = `flow-geometry-${index}`;
        const { svg } = await mermaid.render(svgId, fixture.source);
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");
        const rootInverse = root.getScreenCTM()?.inverse();
        const nodeElements = [...root.querySelectorAll("g.node")];
        const nodes = nodeElements.map((node) => {
          const box = node.getBBox();
          const position = absoluteOrigin(node, rootInverse);
          const labelBox = node.querySelector(".label")?.getBBox();
          const shape = node.querySelector(".label-container");
          const shapeAttributes = {};
          for (const name of ["d", "x", "y", "width", "height", "rx", "ry", "r", "cx", "cy", "points", "transform"]) {
            if (shape?.hasAttribute(name)) shapeAttributes[name] = shape.getAttribute(name);
          }
          const id = node.id.replace(`${svgId}-flowchart-`, "").replace(/-\d+$/, "");
          return {
            id,
            x: number(position.x),
            y: number(position.y),
            width: number(box.width),
            height: number(box.height),
            labelWidth: number(labelBox?.width ?? 0),
            labelHeight: number(labelBox?.height ?? 0),
            group: attributes(node, ["class", "data-id", "data-node"]),
            label: {
              tag: node.querySelector(".label")?.tagName.toLowerCase() ?? "",
              attributes: attributes(node.querySelector(".label"), ["class", "transform", "style"]),
              computed: computedStyle(node.querySelector(".label"), [
                "color", "fill", "font-family", "font-size", "font-weight",
              ]),
              lines: textLayout(node.querySelector(".label")),
            },
            shape: {
              tag: shape?.tagName.toLowerCase() ?? "",
              attributes: {
                ...shapeAttributes,
                ...attributes(shape, ["class", "style", "fill", "stroke", "stroke-width"]),
              },
              computed: computedStyle(shape, [
                "fill", "stroke", "stroke-width", "stroke-dasharray",
              ]),
            },
          };
        });
        if (fixture.id === "basic-shapes" || fixture.id === "legacy-shapes" || fixture.id === "expanded-shapes" || fixture.id === "expanded-shapes-2") {
          for (let nodeIndex = 0; nodeIndex < nodeElements.length; ++nodeIndex) {
            const shape = nodeElements[nodeIndex].querySelector(".label-container");
            // Label-less shapes (fork, filled_circle, crossed_circle, hourglass, bolt)
            // have no .label-container element — skip their silhouette.
            nodes[nodeIndex].pixel = shape && shape.tagName.toLowerCase() !== "g" &&
                                     !shape.classList.contains("outer-path")
              ? await rasterizeShape(shape)
              : null;
          }
        }
        const originX = nodes[0].x;
        const originY = nodes[0].y;
        for (const node of nodes) {
          node.dx = number(node.x - originX);
          node.dy = number(node.y - originY);
          delete node.x;
          delete node.y;
        }
        const edges = [...root.querySelectorAll(".edgePaths path")].map((edge) => {
          const edgeAttributes = allAttributes(edge);
          if (edgeAttributes["marker-start"])
            edgeAttributes["marker-start"] = normalizeMarker(edgeAttributes["marker-start"]);
          if (edgeAttributes["marker-end"])
            edgeAttributes["marker-end"] = normalizeMarker(edgeAttributes["marker-end"]);
          return {
            id: edge.id.replace(`${svgId}-`, ""),
            d: relativePath(edge.getAttribute("d"), originX, originY,
                            elementMatrix(edge, rootInverse)),
            attributes: edgeAttributes,
            computed: computedStyle(edge, [
              "fill", "stroke", "stroke-width", "stroke-dasharray",
              "animation-name", "animation-duration", "animation-timing-function",
            ]),
          };
        });
        const edgeLabels = [...root.querySelectorAll(".edgeLabels .edgeLabel")].map((label) => {
          const box = label.getBBox();
          const content = label.querySelector(".label")?.getBBox();
          const textElement = label.querySelector("text");
          const textBox = textElement?.getBBox();
          const style = getComputedStyle(textElement ?? label);
          const position = absoluteOrigin(label, rootInverse);
          return {
            width: number(box.width),
            height: number(box.height),
            contentWidth: number(content?.width ?? box.width),
            contentHeight: number(content?.height ?? box.height),
            textWidth: number(textBox?.width ?? box.width),
            textHeight: number(textBox?.height ?? box.height),
            fontFamily: style.fontFamily,
            fontSize: style.fontSize,
            lines: textLayout(label.querySelector(".label")),
            dx: number(position.x - originX),
            dy: number(position.y - originY),
          };
        });
        edgeLabels.forEach((label, index) => Object.assign(edges[index], { label }));
        const transformedBox = (element) => {
          const box = absoluteBox(element, rootInverse);
          if (!box) return null;
          return {
            x: number(box.x - originX),
            y: number(box.y - originY),
            width: number(box.width),
            height: number(box.height),
          };
        };
        const clusters = [...root.querySelectorAll("g.cluster")].map((cluster) => {
          const rect = cluster.querySelector("rect");
          const label = cluster.querySelector(".cluster-label");
          const rectBox = transformedBox(rect);
          return {
            id: cluster.id.replace(`${svgId}-`, ""),
            dx: number(rectBox.x + rectBox.width / 2),
            dy: number(rectBox.y + rectBox.height / 2),
            width: rectBox.width,
            height: rectBox.height,
            group: attributes(cluster, ["class", "data-id", "data-look"]),
            rect: rectBox,
            rectAttributes: attributes(rect, [
              "class", "style", "x", "y", "width", "height", "rx", "ry",
              "fill", "stroke", "stroke-width",
            ]),
            rectComputed: computedStyle(rect, [
              "fill", "stroke", "stroke-width", "stroke-dasharray",
            ]),
            label: {
              ...transformedBox(label),
              attributes: attributes(label, ["class", "transform", "style"]),
              computed: computedStyle(label, [
                "color", "fill", "font-family", "font-size", "font-weight",
              ]),
              lines: textLayout(label?.querySelector(".label") ?? label),
              structure: shallowStructure(label),
            },
          };
        });
        const markers = [...document.querySelectorAll("marker")]
          .filter((marker) => marker.id.includes(svgId)).map((marker) => {
          const markerAttributes = allAttributes(marker);
          markerAttributes.id = normalizeMarker(`url(#${markerAttributes.id})`);
          return {
            attributes: markerAttributes,
            children: [...marker.children].map((child) => ({
              tag: child.tagName.toLowerCase(),
              attributes: allAttributes(child),
              computed: computedStyle(child, ["fill", "stroke", "stroke-width"]),
            })),
          };
        }).sort((a, b) => a.attributes.id.localeCompare(b.attributes.id));
        results.push({
          ...fixture,
          expected: {
            svg: allAttributes(root),
            defs: { markers },
            nodes,
            edges,
            clusters,
          },
        });
      }
      return results;
    },
    { cases, mermaidModule },
  );
  const fixture = { upstream: { package: "mermaid", version: packageJson.version }, cases: snapshots };
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
  console.log(`Wrote ${output}`);
} finally {
  await browser.close();
}
