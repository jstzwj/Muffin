import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "sequence-layout.json"),
);
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected Mermaid 11.16.0, found ${packageJson.version}`);
}

const notoDir = path.resolve("third_party", "noto", "fonts");
const fontFiles = [
  ["Noto Sans", "NotoSans-Regular.ttf", "U+0000-024F,U+1E00-1EFF"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf", "U+2E80-9FFF,U+3040-30FF,U+AC00-D7AF"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf", "U+0600-06FF,U+0750-077F,U+08A0-08FF"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf", "U+0590-05FF"],
];
for (const [, file] of fontFiles) {
  if (!fs.existsSync(path.join(notoDir, file))) throw new Error(`Missing bundled Noto font: ${file}`);
}
const fontStack = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';
const fontFaces = fontFiles.map(([family, file, range]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}");font-weight:400;font-style:normal;unicode-range:${range};}`
).join("\n");

const cases = [
  {
    id: "participants-and-spacing",
    axes: ["participant-size", "lifeline", "message-spacing"],
    source: [
      "sequenceDiagram",
      "participant A as Alice",
      "actor B as Bob Builder",
      "participant C as 中文参与者",
      "A->>B:short",
      "B-->>C:A deliberately long message label",
      "C->A:return",
    ].join("\n"),
  },
  {
    id: "activation-stack",
    axes: ["activation-stack", "message-spacing"],
    source: [
      "sequenceDiagram",
      "A->>+B:first",
      "B->>+B:nested",
      "B-->>-B:nested return",
      "B-->>-A:final return",
    ].join("\n"),
  },
  {
    id: "notes-all-placements",
    axes: ["note-geometry", "participant-size"],
    source: [
      "sequenceDiagram",
      "participant A as Alice",
      "participant B as Bob",
      "Note left of A: left note",
      "Note right of B: right note",
      "Note over A: single note",
      "Note over A,B: shared note 中文",
    ].join("\n"),
  },
  {
    id: "nested-fragments",
    axes: ["fragment-geometry", "message-spacing"],
    source: [
      "sequenceDiagram",
      "alt success",
      "loop retry",
      "A->>B:request",
      "opt cached",
      "B-->>A:response",
      "end",
      "end",
      "else failure",
      "A-xB:error",
      "end",
    ].join("\n"),
  },
  {
    id: "parallel-critical-break",
    axes: ["fragment-geometry", "activation-stack"],
    source: [
      "sequenceDiagram",
      "par first branch",
      "A->>+B:one",
      "and second branch",
      "C->>D:two",
      "end",
      "critical must pass",
      "B-->>-A:ok",
      "option timeout",
      "break abort",
      "A-xB:stop",
      "end",
      "end",
    ].join("\n"),
  },
  {
    id: "bidi-cjk-geometry",
    axes: ["participant-size", "message-spacing", "note-geometry"],
    source: [
      "sequenceDiagram",
      "participant A as 中文 Alice",
      "participant B as مرحبا Bob",
      "A->>B:שלום 世界 mixed message",
      "Note over A,B: العربية 中文 note",
    ].join("\n"),
  },
];

const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")),
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});

try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1800, height: 1400, deviceScaleFactor: 1 });
  const harnessUrl = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const snapshots = [];
  for (let index = 0; index < cases.length; ++index) {
    await page.goto(harnessUrl);
    const snapshot = await page.evaluate(async ({ fixture, index, mermaidModule, fontFaces, fontStack }) => {
      const style = document.createElement("style");
      style.textContent = fontFaces;
      document.head.appendChild(style);
      await Promise.all([
        document.fonts.load('16px "Noto Sans"', "Fixed Noto"),
        document.fonts.load('16px "Noto Sans CJK SC"', "中文日本語"),
        document.fonts.load('16px "Noto Sans Arabic"', "مرحبا"),
        document.fonts.load('16px "Noto Sans Hebrew"', "שלום"),
      ]);
      await document.fonts.ready;
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: "default",
        look: "classic",
        fontFamily: fontStack,
        sequence: { useMaxWidth: false },
      });
      const { svg } = await mermaid.render(`sequence-layout-${index}`, fixture.source);
      document.getElementById("container").innerHTML = svg;
      const root = document.querySelector("svg");
      const round = (value) => Math.round(value * 1000) / 1000;
      const box = (element) => {
        const b = element.getBBox();
        return { x: round(b.x), y: round(b.y), width: round(b.width), height: round(b.height) };
      };
      const number = (element, name) => round(Number(element.getAttribute(name) ?? 0));
      const text = (element) => ({ text: element?.textContent ?? "", ...(element ? box(element) : {}) });
      const participants = [...root.querySelectorAll('[data-et="participant"]')]
        .filter((element) => !element.closest("g.actor-man") || element.matches("g.actor-man"))
        .map((element, position) => {
          const shape = element.querySelector("rect, circle, path, line, polygon") ?? element;
          return { position, id: element.getAttribute("data-id") ?? "", box: box(element), shape: box(shape), label: text(element.querySelector("text")) };
        });
      const lifelines = [...root.querySelectorAll('[data-et="life-line"]')].map((element) => ({
        id: element.getAttribute("data-id") ?? "",
        x1: number(element, "x1"), y1: number(element, "y1"),
        x2: number(element, "x2"), y2: number(element, "y2"),
      }));
      const messages = [...root.querySelectorAll('[data-et="message"]')].map((element, position) => ({
        position,
        box: box(element),
        line: element.querySelector("line") ? {
          x1: number(element.querySelector("line"), "x1"), y1: number(element.querySelector("line"), "y1"),
          x2: number(element.querySelector("line"), "x2"), y2: number(element.querySelector("line"), "y2"),
        } : null,
        path: element.querySelector("path")?.getAttribute("d") ?? "",
        label: text(element.querySelector("text")),
      }));
      const activations = [...root.querySelectorAll('rect[class^="activation"]')].map((element, position) => ({
        position, className: element.getAttribute("class") ?? "", ...box(element),
      }));
      const notes = [...root.querySelectorAll('[data-et="note"]')].map((element, position) => ({
        position, box: box(element), shape: box(element.querySelector("rect, path") ?? element), label: text(element.querySelector("text")),
      }));
      const fragments = [...root.querySelectorAll('[data-et="control-structure"]')].map((element, position) => ({
        position, id: element.getAttribute("data-id") ?? "", box: box(element),
        outline: box(element.querySelector("rect, path") ?? element),
        labels: [...element.querySelectorAll("text")].map(text),
      }));
      return {
        id: fixture.id,
        axes: fixture.axes,
        source: fixture.source,
        config: {
          fontFamily: fontStack, fontSize: 16, actorMargin: 50, width: 150, height: 65,
          boxMargin: 10, messageMargin: 35, noteMargin: 10, activationWidth: 10,
          diagramMarginX: 50, diagramMarginY: 10, mirrorActors: true,
        },
        root: { viewBox: root.getAttribute("viewBox"), ...box(root) },
        participants, lifelines, messages, activations, notes, fragments,
      };
    }, { fixture: cases[index], index, mermaidModule, fontFaces, fontStack });
    snapshots.push(snapshot);
  }
  const payload = { mermaidVersion: packageJson.version, fontMode: "bundled-noto", cases: snapshots };
  const canonical = JSON.stringify(payload);
  payload.fixtureSha256 = createHash("sha256").update(canonical).digest("hex");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${snapshots.length} sequence layout cases to ${output}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
