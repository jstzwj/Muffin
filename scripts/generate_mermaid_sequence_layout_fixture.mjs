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
  {
    id: "participant-painted-bounds",
    axes: ["participant-size", "participant-painted-bounds", "lifeline"],
    source: [
      "sequenceDiagram",
      "participant P as Plain",
      "actor A as Actor",
      'participant B@{ "type": "boundary" } as Boundary',
      'participant C@{ "type": "control" } as Control',
      'participant E@{ "type": "entity" } as Entity',
      'participant D@{ "type": "database" } as Database',
      'participant S@{ "type": "collections" } as Collections',
      'participant Q@{ "type": "queue" } as Queue',
      "P->>Q:all participant bounds",
    ].join("\n"),
  },
  {
    id: "markers-create-destroy",
    axes: ["message-marker", "participant-lifecycle", "lifeline"],
    source: [
      "sequenceDiagram",
      "participant A as Alice",
      "participant B as Bob",
      "A->>B:solid", "B-->>A:dotted", "A-xB:cross", "B--xA:dotted cross",
      "A-)B:point", "B--)A:dotted point", "A->B:open", "B-->A:dotted open",
      "A<<->>B:bidirectional", "B<<-->>A:bidirectional dotted",
      "A -|\\ B:solid top", "A -|/ B:solid bottom", "A -\\\\ B:stick top", "A -// B:stick bottom",
      "A /|- B:reverse solid top", "A \\|- B:reverse solid bottom",
      "A //- B:reverse stick top", "A \\\\- B:reverse stick bottom",
      "A --|\\ B:dotted solid top", "A --|/ B:dotted solid bottom",
      "A --\\\\ B:dotted stick top", "A --// B:dotted stick bottom",
      "A /|-- B:dotted reverse solid top", "A \\|-- B:dotted reverse solid bottom",
      "A //-- B:dotted reverse stick top", "A \\\\-- B:dotted reverse stick bottom",
      "create participant C as Created",
      "A->>C:create",
      "destroy C",
      "C-->>A:destroy receiver",
      "create actor D as Runtime Actor",
      "B->>D:create actor",
      "destroy D",
      "D-xB:destroy sender",
    ].join("\n"),
  },
  {
    id: "central-autonumber",
    axes: ["central-connection", "autonumber", "activation-stack", "message-marker"],
    source: [
      "sequenceDiagram",
      "autonumber 10 5",
      "A->>()B:forward central",
      "A()->>B:reverse central",
      "A()->>()B:dual central",
      "B<<-->>A:bidirectional numbered",
      "A()<<->>()B:dual bidirectional central",
      "B()/|-A:reverse marker central",
      "autonumber off",
      "A-->>B:unnumbered",
      "autonumber 1234 0.25",
      "A->>B:four digit number",
      "autonumber 123456 1",
      "B-->>A:six digit number",
    ].join("\n"),
  },
  {
    id: "self-right-angles",
    axes: ["self-message", "right-angles", "autonumber", "message-marker"],
    sequence: { rightAngles: true },
    source: [
      "sequenceDiagram",
      "autonumber",
      "A->>A:self solid",
      "A-->>A:self dotted",
      "A-xA:self cross",
      "A<<->>A:self both",
    ].join("\n"),
  },
  {
    id: "self-curved-autonumber",
    axes: ["self-message", "autonumber", "message-marker"],
    source: [
      "sequenceDiagram",
      "autonumber",
      "A->>A:self solid",
      "A<<->>A:self both",
      "A/|-A:self reverse",
    ].join("\n"),
  },
  {
    id: "participant-boxes",
    axes: ["participant-box", "participant-size", "message-spacing"],
    source: [
      "sequenceDiagram",
      "box rgb(230, 240, 255) Services",
      "participant A as API",
      "participant B as Worker",
      "end",
      "box #fff0f0",
      "actor C as Worker",
      'participant D@{ "type": "database" } as Store',
      "end",
      "A->>C:cross box",
      "C->>D:persist",
      "create participant E as Runtime",
      "D->>E:create beside boxes",
      "destroy E",
      "E-->>A:destroy beside boxes",
    ].join("\n"),
  },
  {
    id: "visibility-no-footer",
    axes: ["participant-visibility", "footer-policy", "participant-lifecycle"],
    sequence: { mirrorActors: false, hideUnusedParticipants: true },
    source: [
      "sequenceDiagram",
      "participant UNUSED as Hidden",
      "participant P as Participant",
      'participant A@{ "type": "actor" } as Actor',
      'participant B@{ "type": "boundary" } as Boundary',
      'participant C@{ "type": "control" } as Control',
      'participant E@{ "type": "entity" } as Entity',
      'participant D@{ "type": "database" } as Store',
      'participant L@{ "type": "collections" } as Collection',
      'participant Q@{ "type": "queue" } as Queue',
      "P->>A:one",
      "A->>B:two",
      "B->>C:three",
      "C->>E:four",
      "E->>D:five",
      "D->>L:six",
      "L->>Q:seven",
      "destroy Q",
      "Q-->>P:done",
    ].join("\n"),
  },
  {
    id: "activation-lifecycle",
    axes: ["activation-stack", "activation-lifecycle", "fragment-geometry"],
    source: [
      "sequenceDiagram",
      "activate A",
      "A->>+B:explicit and plus",
      "B->>+B:nested self",
      "alt branch",
      "B-->>-B:close nested",
      "end",
      "B-->>-A:close B",
      "deactivate A",
      "A->>+C:unclosed",
    ].join("\n"),
  },
  {
    id: "custom-activation-width",
    axes: ["sequence-config", "activation-stack", "activation-lifecycle"],
    sequence: { activationWidth: 14 },
    source: [
      "sequenceDiagram",
      "A->>+B:start",
      "B->>+B:nested",
      "B-->>-B:close nested",
      "B-->>-A:done",
    ].join("\n"),
  },
  {
    id: "two-stage-message-wrap",
    axes: ["wrap-margin-stage", "wrap-final-stage", "participant-size", "message-spacing"],
    sequence: { width: 100, actorMargin: 50, wrapPadding: 10, wrap: true },
    source: [
      "sequenceDiagram",
      "participant A as Alpha",
      "participant B as Beta",
      "A->>B:alpha beta gamma delta epsilon zeta eta theta",
      "B-->>A:short return",
    ].join("\n"),
  },
  {
    id: "custom-layout-and-viewport-config",
    axes: ["sequence-config", "viewport-config", "participant-size", "participant-lifecycle",
      "message-spacing", "note-geometry"],
    sequence: {
      actorMargin: 73,
      width: 184,
      height: 78,
      boxMargin: 17,
      boxTextMargin: 9,
      messageMargin: 61,
      noteMargin: 19,
      activationWidth: 14,
      diagramMarginX: 31,
      diagramMarginY: 23,
      wrapPadding: 16,
      labelBoxWidth: 67,
      labelBoxHeight: 28,
      bottomMarginAdj: 7,
      wrap: true,
    },
    source: [
      "sequenceDiagram",
      "box Configured box",
      "participant A as Config A",
      "participant B as Configured peer",
      "end",
      "A->>B:configured",
      "Note over A,B:configured note",
      "B-->>A:done",
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
        sequence: { useMaxWidth: false, ...(fixture.sequence ?? {}) },
      });
      const resolved = mermaid.mermaidAPI.getConfig().sequence;
      const { svg } = await mermaid.render(`sequence-layout-${index}`, fixture.source);
      document.getElementById("container").innerHTML = svg;
      const root = document.querySelector("svg");
      const round = (value) => Math.round(value * 1000) / 1000;
      const box = (element) => {
        const b = element.getBBox();
        return { x: round(b.x), y: round(b.y), width: round(b.width), height: round(b.height) };
      };
      const paintedBox = (element) => {
        const b = element.getBBox();
        const rootMatrix = root.getScreenCTM();
        const elementMatrix = element.getScreenCTM();
        if (!rootMatrix || !elementMatrix) return box(element);
        const matrix = rootMatrix.inverse().multiply(elementMatrix);
        const points = [
          new DOMPoint(b.x, b.y), new DOMPoint(b.x + b.width, b.y),
          new DOMPoint(b.x, b.y + b.height), new DOMPoint(b.x + b.width, b.y + b.height),
        ].map((point) => point.matrixTransform(matrix));
        const xs = points.map((point) => point.x), ys = points.map((point) => point.y);
        return { x: round(Math.min(...xs)), y: round(Math.min(...ys)),
          width: round(Math.max(...xs) - Math.min(...xs)),
          height: round(Math.max(...ys) - Math.min(...ys)) };
      };
      const number = (element, name) => round(Number(element.getAttribute(name) ?? 0));
      const text = (element) => ({ text: element?.textContent ?? "", ...(element ? box(element) : {}) });
      const textBlock = (elements) => {
        const present = [...elements].filter(Boolean);
        if (!present.length) return { text: "" };
        const boxes = present.map(box);
        const left = Math.min(...boxes.map((item) => item.x));
        const top = Math.min(...boxes.map((item) => item.y));
        const right = Math.max(...boxes.map((item) => item.x + item.width));
        const bottom = Math.max(...boxes.map((item) => item.y + item.height));
        return { text: present.map((element) => element.textContent ?? "").join("\n"),
          x: round(left), y: round(top), width: round(right - left), height: round(bottom - top) };
      };
      const participants = [...root.querySelectorAll('[data-et="participant"]')]
        .filter((element) => !element.closest("g.actor-man") || element.matches("g.actor-man"))
        .map((element, position) => {
          const shape = element.querySelector("rect, circle, path, line, polygon") ?? element;
          return { position, id: element.getAttribute("data-id") ?? "", box: box(element),
            paintedBox: paintedBox(element), shape: box(shape),
            label: textBlock(element.querySelectorAll("text")) };
        });
      const lifelines = [...root.querySelectorAll('[data-et="life-line"]')].map((element) => ({
        id: element.getAttribute("data-id") ?? "",
        x1: number(element, "x1"), y1: number(element, "y1"),
        x2: number(element, "x2"), y2: number(element, "y2"),
      }));
      const footers = [...root.querySelectorAll(".actor-bottom")].map((element) => {
        const container = element.tagName.toLowerCase() === "g" ? element : element.parentElement;
        return { id: element.getAttribute("name") ?? container?.getAttribute("name") ?? "",
          paintedBox: container ? paintedBox(container) : paintedBox(element) };
      });
      const participantBoxes = [...root.querySelectorAll("rect.rect")].map((element, position) => ({
        position, shape: { x: number(element, "x"), y: number(element, "y"),
          width: number(element, "width"), height: number(element, "height"),
          fill: element.getAttribute("fill") ?? "", stroke: element.getAttribute("stroke") ?? "" },
        label: textBlock(element.parentElement?.querySelectorAll("text.text") ?? []),
      }));
      const messages = [...root.querySelectorAll('[data-et="message"]')].map((element, position) => {
        const line = element.tagName.toLowerCase() === "line" ? element : element.querySelector("line");
        const path = element.tagName.toLowerCase() === "path" ? element : element.querySelector("path");
        const labels = [];
        for (let sibling = element.previousElementSibling;
             sibling?.classList.contains("messageText"); sibling = sibling.previousElementSibling)
          labels.unshift(sibling);
        return {
          position,
          id: element.getAttribute("data-id") ?? "",
          from: element.getAttribute("data-from") ?? "",
          to: element.getAttribute("data-to") ?? "",
          box: box(element),
          line: line ? {
            x1: number(line, "x1"), y1: number(line, "y1"),
            x2: number(line, "x2"), y2: number(line, "y2"),
          } : null,
          path: path?.getAttribute("d") ?? "",
          structure: {
            tag: element.tagName.toLowerCase(),
            className: element.getAttribute("class") ?? "",
            markerStart: element.getAttribute("marker-start") ?? "",
            markerEnd: element.getAttribute("marker-end") ?? "",
          },
          label: textBlock(labels),
        };
      });
      const centralConnections = [...root.querySelectorAll('circle[r="5"]')]
        .filter((element) => !element.closest("defs"))
        .map((element) => ({ cx: number(element, "cx"), cy: number(element, "cy"), r: number(element, "r") }));
      const sequenceNumbers = [...root.querySelectorAll("text.sequenceNumber")].map((element) => ({
        text: element.textContent ?? "", x: number(element, "x"), y: number(element, "y"),
        fontSize: element.getAttribute("font-size") ?? "",
      }));
      const activations = [...root.querySelectorAll('rect[class^="activation"]')].map((element, position) => ({
        position, className: element.getAttribute("class") ?? "",
        x: number(element, "x"), y: number(element, "y"),
        width: number(element, "width"), height: number(element, "height"),
        box: box(element),
      }));
      const notes = [...root.querySelectorAll('[data-et="note"]')].map((element, position) => ({
        position, id: element.getAttribute("data-id") ?? "", box: box(element),
        shape: (() => {
          const shape = element.querySelector("rect, path");
          return shape ? { x: number(shape, "x"), y: number(shape, "y"),
            width: number(shape, "width"), height: number(shape, "height") } : box(element);
        })(),
        label: textBlock(element.querySelectorAll("text")),
      }));
      const fragments = [...root.querySelectorAll('[data-et="control-structure"]')].map((element, position) => {
        const lines = [...element.querySelectorAll(".loopLine")];
        const xs = lines.flatMap((line) => [number(line, "x1"), number(line, "x2")]);
        const ys = lines.flatMap((line) => [number(line, "y1"), number(line, "y2")]);
        return {
          position, id: element.getAttribute("data-id") ?? "", box: box(element),
          outline: lines.length ? { x: Math.min(...xs), y: Math.min(...ys),
            width: Math.max(...xs) - Math.min(...xs), height: Math.max(...ys) - Math.min(...ys) } : box(element),
          labels: [...element.querySelectorAll("text")].map(text),
          structure: {
            lineCount: lines.length,
            sectionLineCount: lines.filter((line) => (line.getAttribute("style") ?? "").includes("stroke-dasharray")).length,
            labelClasses: [...element.querySelectorAll("text")].map((label) => label.getAttribute("class") ?? ""),
          },
        };
      });
      const markerStructure = [...root.querySelectorAll("defs marker")].map((element) => ({
        id: element.id,
        markerWidth: element.getAttribute("markerWidth") ?? "",
        markerHeight: element.getAttribute("markerHeight") ?? "",
        refX: element.getAttribute("refX") ?? "",
        refY: element.getAttribute("refY") ?? "",
        orient: element.getAttribute("orient") ?? "",
      }));
      for (const participant of participants) {
        const element = [...root.querySelectorAll('[data-et="participant"]')]
          .find((candidate) => (candidate.getAttribute("data-id") ?? "") === participant.id);
        participant.structure = {
          dataType: element?.getAttribute("data-type") ?? "",
          childTags: element ? [...element.querySelectorAll(":scope > *")].map((child) => child.tagName.toLowerCase()) : [],
        };
      }
      const viewBox = (root.getAttribute("viewBox") ?? "").trim().split(/\s+/).map(Number);
      const logicalHeight = viewBox[3] - 2 * resolved.diagramMarginY +
        (resolved.mirrorActors ? resolved.boxMargin - resolved.bottomMarginAdj : 0);
      return {
        id: fixture.id,
        axes: fixture.axes,
        source: fixture.source,
        config: {
          fontFamily: fontStack,
          fontSize: 16,
          ...Object.fromEntries([
            "actorMargin", "width", "height", "boxMargin", "boxTextMargin", "messageMargin",
            "noteMargin", "activationWidth", "diagramMarginX", "diagramMarginY", "mirrorActors",
            "hideUnusedParticipants", "wrapPadding", "labelBoxWidth", "labelBoxHeight",
            "bottomMarginAdj", "rightAngles", "wrap",
          ].map((key) => [key, resolved[key]])),
        },
        root: { viewBox: root.getAttribute("viewBox"), ...box(root) },
        viewport: {
          x: round(viewBox[0] + resolved.diagramMarginX),
          y: round(viewBox[1] + resolved.diagramMarginY),
          width: round(viewBox[2] - 2 * resolved.diagramMarginX),
          height: round(logicalHeight),
        },
        participants, footers, participantBoxes, lifelines, messages, centralConnections, sequenceNumbers,
        activations, notes, fragments,
        svgStructure: { markers: markerStructure },
      };
    }, { fixture: cases[index], index, mermaidModule, fontFaces, fontStack });
    snapshots.push(snapshot);
  }
  const payload = {
    mermaidVersion: packageJson.version,
    fontMode: "bundled-noto",
    configContract: {
      layout: ["actorMargin", "width", "height", "boxMargin", "boxTextMargin", "noteMargin",
        "activationWidth", "wrapPadding", "labelBoxWidth", "labelBoxHeight", "rightAngles", "wrap",
        "mirrorActors", "hideUnusedParticipants"],
      viewport: ["diagramMarginX", "diagramMarginY", "bottomMarginAdj"],
      upstreamInert: ["messageMargin"],
    },
    cases: snapshots,
  };
  const canonical = JSON.stringify(payload);
  payload.fixtureSha256 = createHash("sha256").update(canonical).digest("hex");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${snapshots.length} sequence layout cases to ${output}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
