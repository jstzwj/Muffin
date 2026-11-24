import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { bundledFontArguments } from "./mermaid_bundled_fonts.mjs";

// Captures real mermaid 11.16.0 stateDiagram-v2 geometry (state node sizes +
// transition topology) via headless Chrome. Transition from/to is recovered by
// matching each edge's data-points endpoints (base64 dagre waypoints: first =
// source, last = target) to the nearest state node centre — state edges do NOT
// carry a LS-/LE- class encoding (unlike flowchart), so endpoints are matched by
// position. Node ids line up directly: mermaid's root_start/root_end/<name>
// match Muffin's StateScene node ids exactly.
//
// Output schema matches classdiagram::StateScene::toJsonObject's comparable
// fields. node height is font-independent (asserted downstream); width + the
// path itself are font-coupled (reported).
//
//   node scripts/generate_mermaid_state_geometry_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Env override: MERMAID_REFERENCE_ROOT (takes precedence over the default).

const mermaidRoot = path.resolve(
  process.env.MERMAID_REFERENCE_ROOT ??
    process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "state-geometry.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

// Corpus: basic transitions, branching, fork/join, self-loop, start/end.
const cases = [
  { id: "linear", source: "stateDiagram-v2\n[*] --> Active\nActive --> Inactive\nInactive --> [*]" },
  { id: "branching", source: "stateDiagram-v2\n[*] --> A\nA --> B : yes\nA --> C : no\nB --> [*]\nC --> [*]" },
  { id: "fork-join", source: "stateDiagram-v2\n[*] --> Active\nstate Fork <<fork>>\nActive --> Fork\nFork --> S1\nFork --> S2\nstate Join <<join>>\nS1 --> Join\nS2 --> Join\nJoin --> [*]" },
  { id: "self-loop", source: "stateDiagram-v2\n[*] --> Idle\nIdle --> Idle : retry\nIdle --> [*]" },
  { id: "labeled", source: "stateDiagram-v2\n[*] --> Off\nOff --> On : power\nOn --> Off : power\nOn --> [*] : timeout" },
  // Wider structures: a composite state, a choice, notes, a diamond, a long
  // chain, long names, two loops and a left-to-right layout.
  { id: "composite", source: "stateDiagram-v2\n[*] --> Outer\nstate Outer {\n  [*] --> Inner1\n  Inner1 --> Inner2\n  Inner2 --> [*]\n}\nOuter --> [*]" },
  { id: "choice", source: "stateDiagram-v2\n[*] --> A\nstate c <<choice>>\nA --> c\nc --> B : yes\nc --> C : no\nB --> [*]\nC --> [*]" },
  { id: "note", source: "stateDiagram-v2\n[*] --> Idle\nIdle --> Running : start\nnote right of Idle : waits for input\nnote right of Running : processing\nRunning --> [*]" },
  { id: "diamond", source: "stateDiagram-v2\n[*] --> A\nA --> B\nA --> C\nB --> D\nC --> D\nD --> [*]" },
  { id: "chain", source: "stateDiagram-v2\n[*] --> S1\nS1 --> S2\nS2 --> S3\nS3 --> S4\nS4 --> S5\nS5 --> S6\nS6 --> [*]" },
  { id: "long-names", source: "stateDiagram-v2\n[*] --> AwaitingUserConfirmation\nAwaitingUserConfirmation --> ProcessingPaymentAuthorization : submit\nProcessingPaymentAuthorization --> [*]" },
  { id: "description", source: "stateDiagram-v2\n[*] --> Idle\nIdle : waiting for a signal from the host\nIdle --> Busy : a much longer transition label with several words\nBusy --> [*]" },
  { id: "direction-lr", source: "stateDiagram-v2\ndirection LR\n[*] --> A\nA --> B\nB --> C\nC --> [*]" },
  { id: "two-loops", source: "stateDiagram-v2\n[*] --> A\nA --> A : retry\nA --> B : go\nB --> B : wait\nB --> [*]" },
  { id: "long-description", source: "stateDiagram-v2\n[*] --> A\nA : Waiting for the user to confirm the payment authorization before anything else can proceed\nA --> [*]" },
];

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const results = await page.evaluate(
    async ({ cases, mermaidModule, fontFaces, fontFamily }) => {
      const number = (v) => Math.round(v * 1000) / 1000;
      const { default: mermaid } = await import(mermaidModule);
      const elementMatrix = (element, rootInverse) =>
        rootInverse.multiply(element.getScreenCTM());
      // Absolute centre of an element in root coords.
      const absoluteCentre = (element, rootInverse) => {
        const box = element.getBBox();
        const m = elementMatrix(element, rootInverse);
        return new DOMPoint(box.x + box.width / 2, box.y + box.height / 2).matrixTransform(m);
      };
      const dist = (p, n) => Math.hypot(p.x - n.cx, p.y - n.cy);
      // marker kind from url(#.._stateDiagram-barbEnd) etc.
      const markerKind = (value) => {
        if (!value) return null;
        const m = value.match(/stateDiagram-([\w-]+)/);
        return m ? m[1] : null;
      };

      // The bundled Noto faces: what the port measures text in.
      const fontStyle = document.createElement("style");
      fontStyle.textContent = fontFaces;
      document.head.appendChild(fontStyle);
      await document.fonts.load('16px "Noto Sans"');
      await document.fonts.ready;
      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        const svgId = `sg-${index}`;
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily,
          themeVariables: { fontFamily },
        });
        const { svg } = await mermaid.render(svgId, fixture.source);
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");
        const rootInverse = root.getScreenCTM()?.inverse();
        const prefix = `${svgId}-state-`;

        // State nodes: id (strip the svgId prefix), centre, size. A state's DOM
        // id ends in its layout index, which is not part of its id; a note's
        // ends in its counter (`<state>----note-<count>`), which is.
        const nodeEls = [...root.querySelectorAll("g.node")];
        const nodes = nodeEls.map((node) => {
          const raw = node.id.startsWith(prefix) ? node.id.slice(prefix.length) : node.id;
          const id = raw.includes("----note-") ? raw : raw.replace(/-\d+$/, "");
          const box = node.querySelector(".label-container, rect, circle, ellipse, polygon") ?? node;
          const bb = box.getBBox();
          const centre = absoluteCentre(node, rootInverse);
          return { id, cx: centre.x, cy: centre.y, width: bb.width, height: bb.height };
        });

        const nearestNode = (p) => {
          let best = null;
          let bestD = Infinity;
          for (const n of nodes) {
            const d = dist(p, n);
            if (d < bestD) { bestD = d; best = n; }
          }
          return best ? best.id : null;
        };

        // Edges: data-points (base64 JSON waypoints) first/last → from/to.
        const edges = [...root.querySelectorAll('path[data-et="edge"]')].map((edge) => {
          const dpB64 = edge.getAttribute("data-points");
          let from = null;
          let to = null;
          if (dpB64) {
            try {
              const pts = JSON.parse(atob(dpB64));
              if (pts.length >= 2) {
                const m = elementMatrix(edge, rootInverse);
                const a = new DOMPoint(pts[0].x, pts[0].y).matrixTransform(m);
                const b = new DOMPoint(pts[pts.length - 1].x, pts[pts.length - 1].y).matrixTransform(m);
                from = nearestNode(a);
                to = nearestNode(b);
              }
            } catch { /* leave null */ }
          }
          return {
            from,
            to,
            markerEnd: markerKind(edge.getAttribute("marker-end")),
          };
        });

        out.push({
          id: fixture.id,
          source: fixture.source,
          expected: {
            nodes: nodes.map((n) => ({
              id: n.id,
              width: number(n.width),
              height: number(n.height),
            })),
            edges,
          },
        });
      }
      return out;
    },
    { cases, mermaidModule, ...bundledFontArguments },
  );

  const root = {
    upstream: {
      package: "mermaid",
      version: "11.16.0",
      notes:
        "Real mermaid 11.16.0 stateDiagram-v2 geometry via headless Chrome. State " +
        "node sizes + transition topology. Transition from/to recovered by matching " +
        "each edge's data-points endpoints (first=source, last=target) to the nearest " +
        "node centre — state edges carry no LS-/LE- class encoding. Node ids match " +
        "Muffin directly (root_start/root_end/<name>). width/height are font-coupled; " +
        "compare against state::StateScene::toJsonObject (nodes id/height/width, " +
        "edges start/end/markerEnd).",
    },
    mermaidVersion: "11.16.0",
    fontMode: "bundled-noto-2.13b171",
    oracle: "stateDiagram.render state nodes (size) + transition topology (from/to by endpoint match)",
    cases: results,
  };
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  console.log(`Wrote ${output} (${results.length} cases)`);
} finally {
  await browser.close();
}
