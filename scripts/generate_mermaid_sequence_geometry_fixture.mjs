import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Captures real mermaid 11.16.0 sequenceDiagram geometry (participants + message
// topology) via headless Chrome. mermaid's legacy sequence renderer emits each
// message as <line class="messageLineN" data-et="message" data-id="iN"
// data-from="A" data-to="B" marker-end="url(#..-arrowhead|crosshead)"
// style="[stroke-dasharray:..]"> — so from/to, order, arrow kind, and dashed are
// read DIRECTLY (no coordinate matching). Participants come from text.actor-box.
//
// Output schema matches the comparable fields of sequence::SequenceScene::
// toJsonObject. Everything asserted downstream is font-independent (the
// conversation structure); anchorX/lineY/lifeline Y are font-coupled (reported).
//
//   node scripts/generate_mermaid_sequence_geometry_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Env override: MERMAID_REFERENCE_ROOT (takes precedence over the default).

const mermaidRoot = path.resolve(
  process.env.MERMAID_REFERENCE_ROOT ??
    process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "sequence-geometry.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);

// Corpus: arrow kinds (->> / -->> / -> / -x / --x), multi-actor, autonumber. No
// aliases (participant id == label). Self-loops (A->>A) render as a loop path,
// not a messageLine, so they are excluded here (a follow-up special-case).
const cases = [
  { id: "basic-arrows", source: "sequenceDiagram\nAlice->>Bob: hello\nBob-->>Alice: hi" },
  { id: "three-actors", source: "sequenceDiagram\nA->>B: one\nB->>C: two\nC-->>A: three" },
  { id: "no-arrowhead", source: "sequenceDiagram\nA->B: nudge\nB->A: back" },
  { id: "cross", source: "sequenceDiagram\nA--xB: err1\nB-xA: err2" },
  { id: "autonumber", source: "sequenceDiagram\nautonumber\nA->>B: first\nB->>A: second" },
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
    async ({ cases, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      const markerKind = (value) => {
        if (!value) return null;
        const m = value.match(/(arrowhead|crosshead)/);
        return m ? m[1] : null;
      };
      const indexFromDataId = (value) => {
        const m = (value || "").match(/i?(\d+)/);
        return m ? Number(m[1]) : Infinity;
      };

      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily: '"trebuchet ms", verdana, arial, sans-serif',
          flowchart: { htmlLabels: false },
        });
        const { svg } = await mermaid.render(`sq-${index}`, fixture.source);
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");

        // Participants: distinct actor labels in source order (x ascending).
        const seen = new Map();
        for (const t of [...root.querySelectorAll("text.actor-box")]) {
          const label = (t.textContent || "").trim();
          const x = Number(t.getAttribute("x") || 0);
          if (label && !seen.has(label)) seen.set(label, x);
        }
        const participants = [...seen.entries()]
          .sort((a, b) => a[1] - b[1])
          .map(([id]) => ({ id }));

        // Messages: data-from/data-to/order/dashed/marker directly off the lines.
        const messages = [...root.querySelectorAll('line[data-et="message"]')]
          .map((line) => ({
            idx: indexFromDataId(line.getAttribute("data-id")),
            from: line.getAttribute("data-from") || "",
            to: line.getAttribute("data-to") || "",
            dashed: (line.getAttribute("style") || "").includes("stroke-dasharray"),
            markerEnd: markerKind(line.getAttribute("marker-end")),
          }))
          .sort((a, b) => a.idx - b.idx);
        // Stable order: strip the helper idx.
        const edges = messages.map(({ from, to, dashed, markerEnd }) => ({ from, to, dashed, markerEnd }));

        const autonumber = root.querySelector("text.sequenceNumber") != null;

        out.push({
          id: fixture.id,
          source: fixture.source,
          expected: { participants, messages: edges, autonumber },
        });
      }
      return out;
    },
    { cases, mermaidModule },
  );

  const root = {
    upstream: {
      package: "mermaid",
      version: "11.16.0",
      notes:
        "Real mermaid 11.16.0 sequenceDiagram geometry via headless Chrome. The " +
        "legacy sequence renderer emits each message as a <line> carrying " +
        "data-from/data-to/data-id/marker-end/style, so message topology, order, " +
        "arrow kind, and dashed are read directly — no coordinate matching. " +
        "Participants from text.actor-box (id == label; alias-free corpus). " +
        "Everything here is font-independent; anchorX/lineY/lifeline Y are " +
        "font-coupled and not captured. Compare against sequence::SequenceScene::" +
        "toJsonObject (participants id; messages from/to/dashed/markerEnd).",
    },
    mermaidVersion: "11.16.0",
    fontMode: "trebuchet-ms",
    oracle: "sequenceDiagram.render participants + message topology (from/to/dashed/markerEnd, ordered)",
    cases: results,
  };
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  console.log(`Wrote ${output} (${results.length} cases)`);
} finally {
  await browser.close();
}
