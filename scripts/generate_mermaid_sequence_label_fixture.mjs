import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const output = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "sequence-label.json"));
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") throw new Error(`Expected Mermaid 11.16.0, found ${packageJson.version}`);

const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [
  ["Noto Sans", "NotoSans-Regular.ttf"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf"],
];
const fontFaces = fonts.map(([family, file]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}")}`
).join("\n");
const fontStack = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';
const mathFontPath = path.resolve("third_party", "stix", "fonts", "STIXTwoMath-Regular.otf");
const mathFontFace = `@font-face{font-family:"STIX Two Math";src:url("${pathToFileURL(mathFontPath).href}")}`;

const cases = [
  {
    id: "participant-html-cjk",
    kind: "participant",
    label: "<b>Client</b><br/>\u5ba2\u6237\u7aef",
    selector: '[data-et="participant"][data-id="A"] text',
    source: "sequenceDiagram\nparticipant A as <b>Client</b><br/>\u5ba2\u6237\u7aef\nA->>B:ping",
  },
  {
    id: "message-html-bidi",
    kind: "message",
    label: "Hello <i>\u4e16\u754c</i><br/>\u0645\u0631\u062d\u0628\u0627 \u05e9\u05dc\u05d5\u05dd",
    selector: ".messageText",
    source: "sequenceDiagram\nA->>B:Hello <i>\u4e16\u754c</i><br/>\u0645\u0631\u062d\u0628\u0627 \u05e9\u05dc\u05d5\u05dd",
  },
  {
    id: "note-markdown-math",
    kind: "note",
    label: "`**Speed** $$x^2$$`",
    selector: '[data-et="note"] foreignObject',
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:`**Speed** $$x^2$$`",
  },
  {
    id: "fragment-html-rtl",
    kind: "fragment",
    label: "<b>Success</b><br/>\u0646\u062c\u0627\u062d",
    selector: ".loopText",
    source: "sequenceDiagram\nalt <b>Success</b><br/>\u0646\u062c\u0627\u062d\nA->>B:ok\nend",
  },
  {
    id: "box-markdown-math",
    kind: "box",
    label: "`**Services** $$x$$`",
    selector: "text.text",
    source: "sequenceDiagram\nbox rgb(238, 246, 255) `**Services** $$x$$`\nparticipant A\nparticipant B\nend\nA->>B:call",
  },
  {
    id: "message-wrap-width",
    kind: "message",
    label: "alpha beta gamma delta epsilon zeta eta theta",
    selector: ".messageText",
    wrapWidth: 200,
    sequence: { wrap: true, wrapPadding: 10 },
    source: "sequenceDiagram\nA->>B:wrap:alpha beta gamma delta epsilon zeta eta theta",
  },
  {
    id: "message-wrap-prefix-120",
    kind: "message",
    label: "alpha beta gamma delta epsilon zeta",
    selector: ".messageText",
    wrapWidth: 140,
    sequence: { width: 70, actorMargin: 50 },
    source: "sequenceDiagram\nA->>B:wrap:alpha beta gamma delta epsilon zeta",
  },
  {
    id: "message-wrap-global-150",
    kind: "message",
    label: "alpha beta gamma delta epsilon zeta",
    selector: ".messageText",
    wrapWidth: 150,
    sequence: { wrap: true, width: 100, actorMargin: 50 },
    source: "sequenceDiagram\nA->>B:alpha beta gamma delta epsilon zeta",
  },
  {
    id: "message-wrap-global-300",
    kind: "message",
    label: "alpha beta gamma delta epsilon zeta eta theta iota",
    selector: ".messageText",
    wrapWidth: 300,
    sequence: { wrap: true, width: 250, actorMargin: 50 },
    source: "sequenceDiagram\nA->>B:alpha beta gamma delta epsilon zeta eta theta iota",
  },
  {
    id: "participant-wrap-prefix",
    kind: "participant",
    label: "alpha beta gamma delta epsilon",
    selector: '[data-et="participant"][data-id="A"] text',
    wrapWidth: 100,
    sequence: { width: 120 },
    source: "sequenceDiagram\nparticipant A as wrap:alpha beta gamma delta epsilon\nA->>B:ping",
  },
  {
    id: "note-wrap-prefix",
    kind: "note",
    label: "alpha beta gamma delta epsilon zeta",
    selector: '[data-et="note"] text',
    wrapWidth: 180,
    sequence: { width: 100, actorMargin: 50 },
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:wrap:alpha beta gamma delta epsilon zeta",
  },
  {
    id: "message-wrap-explicit-break",
    kind: "message",
    label: "alpha beta<br/>gamma delta",
    selector: ".messageText",
    wrapWidth: 140,
    sequence: { wrap: true, width: 70, actorMargin: 50 },
    source: "sequenceDiagram\nA->>B:alpha beta<br/>gamma delta",
  },
  {
    id: "message-arabic-only",
    kind: "message",
    label: "\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645",
    selector: ".messageText",
    source: "sequenceDiagram\nA->>B:\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645",
  },
  {
    id: "message-hebrew-only",
    kind: "message",
    label: "\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd",
    selector: ".messageText",
    source: "sequenceDiagram\nA->>B:\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd",
  },
  {
    id: "message-bidi-numbers-punctuation",
    kind: "message",
    label: "\u0645\u0631\u062d\u0628\u0627 123, \u05e9\u05dc\u05d5\u05dd 456!",
    selector: ".messageText",
    source: "sequenceDiagram\nA->>B:\u0645\u0631\u062d\u0628\u0627 123, \u05e9\u05dc\u05d5\u05dd 456!",
  },
  {
    id: "message-bidi-isolates",
    kind: "message",
    label: "left \u2067\u05e9\u05dc\u05d5\u05dd 42\u2069 right",
    selector: ".messageText",
    logicalComparable: false,
    source: "sequenceDiagram\nA->>B:left \u2067\u05e9\u05dc\u05d5\u05dd 42\u2069 right",
  },
  {
    id: "message-cjk-unspaced",
    kind: "message",
    label: "\u8fd9\u662f\u4e00\u6bb5\u6ca1\u6709\u7a7a\u683c\u7684\u4e2d\u6587\u6807\u7b7e",
    selector: ".messageText",
    source: "sequenceDiagram\nA->>B:\u8fd9\u662f\u4e00\u6bb5\u6ca1\u6709\u7a7a\u683c\u7684\u4e2d\u6587\u6807\u7b7e",
  },
  {
    id: "message-long-unbreakable",
    kind: "message",
    label: "SupercalifragilisticexpialidociousUnbreakableToken",
    selector: ".messageText",
    wrapWidth: 140,
    sequence: { wrap: true, width: 70, actorMargin: 50 },
    source: "sequenceDiagram\nA->>B:SupercalifragilisticexpialidociousUnbreakableToken",
  },
  {
    id: "note-math-fraction",
    kind: "note",
    label: "fraction $$\\frac{a}{b}$$",
    selector: '[data-et="note"] foreignObject',
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:fraction $$\\frac{a}{b}$$",
  },
  {
    id: "note-math-sqrt-sub-sup",
    kind: "note",
    label: "root $$\\sqrt{x_i^2+y^2}$$",
    selector: '[data-et="note"] foreignObject',
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:root $$\\sqrt{x_i^2+y^2}$$",
  },
  {
    id: "note-math-matrix",
    kind: "note",
    label: "matrix $$\\begin{matrix}a&b\\\\c&d\\end{matrix}$$",
    selector: '[data-et="note"] foreignObject',
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:matrix $$\\begin{matrix}a&b\\\\c&d\\end{matrix}$$",
  },
  {
    id: "note-math-multiple-spans",
    kind: "note",
    label: "left $$x$$ middle $$y^2$$ right",
    selector: '[data-et="note"] foreignObject',
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:left $$x$$ middle $$y^2$$ right",
  },
  {
    id: "note-math-break",
    kind: "note",
    label: "before $$x$$<br/>after $$y$$",
    selector: '[data-et="note"] foreignObject',
    logicalComparable: false,
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:before $$x$$<br/>after $$y$$",
  },
  {
    id: "note-math-cjk-rtl",
    kind: "note",
    label: "\u4e2d\u6587 $$\\frac{x}{2}$$ \u0645\u0631\u062d\u0628\u0627",
    selector: '[data-et="note"] foreignObject',
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:\u4e2d\u6587 $$\\frac{x}{2}$$ \u0645\u0631\u062d\u0628\u0627",
  },
];

const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});

try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1800, height: 1200, deviceScaleFactor: 1 });
  const harness = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const snapshots = [];
  for (let index = 0; index < cases.length; ++index) {
    await page.goto(harness);
    const snapshot = await page.evaluate(async ({ fixture, index, mermaidModule, fontFaces, fontStack, mathFontFace }) => {
      const style = document.createElement("style");
      style.textContent = `${fontFaces}\n${mathFontFace}\nmath{font-family:"STIX Two Math" !important}`;
      document.head.appendChild(style);
      await Promise.all([
        document.fonts.load('16px "Noto Sans"', "Fixed Noto"),
        document.fonts.load('16px "Noto Sans CJK SC"', "\u4e2d\u6587"),
        document.fonts.load('16px "Noto Sans Arabic"', "\u0645\u0631\u062d\u0628\u0627"),
        document.fonts.load('16px "Noto Sans Hebrew"', "\u05e9\u05dc\u05d5\u05dd"),
        document.fonts.load('16px "STIX Two Math"', "x+\u2211\u221a"),
      ]);
      await document.fonts.ready;
      const { default: mermaid } = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: "default",
        fontFamily: fontStack,
        sequence: { useMaxWidth: false, ...(fixture.sequence ?? {}) },
      });
      const { svg } = await mermaid.render(`sequence-label-${index}`, fixture.source);
      document.getElementById("container").innerHTML = svg;
      const root = document.querySelector("svg");
      const selected = [...root.querySelectorAll(fixture.selector)];
      const element = selected[0];
      if (!element) throw new Error(`${fixture.id}: selector ${fixture.selector} resolved to nothing`);

      const round = (value) => Math.round(value * 1000) / 1000;
      const hasExplicitBreak = /<br\s*\/?>/i.test(fixture.label);
      const rootInverse = root.getScreenCTM().inverse();
      const clientRectToRoot = (rect) => {
        const points = [new DOMPoint(rect.left, rect.top), new DOMPoint(rect.right, rect.top),
          new DOMPoint(rect.left, rect.bottom), new DOMPoint(rect.right, rect.bottom)]
          .map((point) => point.matrixTransform(rootInverse));
        const xs = points.map((point) => point.x), ys = points.map((point) => point.y);
        return { left: Math.min(...xs), top: Math.min(...ys), right: Math.max(...xs), bottom: Math.max(...ys) };
      };
      const chars = [];
      let measuredBox;
      if (element instanceof SVGTextContentElement) {
        const matrix = element.getScreenCTM();
        const toRoot = (point) => new DOMPoint(point.x, point.y).matrixTransform(matrix).matrixTransform(rootInverse);
        const rectToRoot = (rect) => {
          const points = [new DOMPoint(rect.x, rect.y), new DOMPoint(rect.x + rect.width, rect.y),
            new DOMPoint(rect.x, rect.y + rect.height), new DOMPoint(rect.x + rect.width, rect.y + rect.height)]
            .map((point) => point.matrixTransform(matrix).matrixTransform(rootInverse));
          const xs = points.map((point) => point.x), ys = points.map((point) => point.y);
          return { left: Math.min(...xs), top: Math.min(...ys), right: Math.max(...xs), bottom: Math.max(...ys) };
        };
        for (let logical = 0; logical < element.getNumberOfChars(); ++logical) {
          const start = toRoot(element.getStartPositionOfChar(logical));
          const end = toRoot(element.getEndPositionOfChar(logical));
          const extent = rectToRoot(element.getExtentOfChar(logical));
          chars.push({ logical, start, end, extent, math: false });
        }
        const bbox = element.getBBox();
        measuredBox = { width: round(bbox.width), height: round(bbox.height) };
      } else {
        const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT);
        const canvas = document.createElement("canvas");
        const context = canvas.getContext("2d");
        const container = clientRectToRoot(element.getBoundingClientRect());
        const containerStyle = getComputedStyle(element.querySelector("div") ?? element);
        context.font = `${containerStyle.fontStyle} ${containerStyle.fontWeight} ${containerStyle.fontSize} ${containerStyle.fontFamily}`;
        const containerMetrics = context.measureText("Hg");
        const containerInkHeight = containerMetrics.fontBoundingBoxAscent + containerMetrics.fontBoundingBoxDescent;
        const containerBaseline = container.top +
          Math.max(0, (container.bottom - container.top - containerInkHeight) / 2) +
          containerMetrics.fontBoundingBoxAscent;
        let node, logical = 0;
        while ((node = walker.nextNode())) {
          for (let offset = 0; offset < node.data.length; ++offset, ++logical) {
            const range = document.createRange();
            range.setStart(node, offset);
            range.setEnd(node, offset + 1);
            const rect = range.getBoundingClientRect();
            if (rect.width === 0 && rect.height === 0) continue;
            const extent = clientRectToRoot(rect);
            const mathElement = node.parentElement.closest("math");
            const mathExtent = mathElement
              ? clientRectToRoot(mathElement.getBoundingClientRect()) : null;
            const measuredTextBaseline = extent.top +
              Math.max(0, (extent.bottom - extent.top - containerInkHeight) / 2) +
              containerMetrics.fontBoundingBoxAscent;
            const textBaseline = hasExplicitBreak ? measuredTextBaseline : containerBaseline;
            chars.push({ logical, start: { x: extent.left, y: mathElement ? containerBaseline : textBaseline },
              end: { x: extent.right, y: mathElement ? containerBaseline : textBaseline }, extent,
              math: Boolean(mathElement), mathExtent });
          }
        }
        const rect = clientRectToRoot(element.getBoundingClientRect());
        measuredBox = { width: round(rect.right - rect.left), height: round(rect.bottom - rect.top) };
      }
      let renderedText = element.textContent;
      for (const extra of selected.slice(1)) {
        if (!(extra instanceof SVGTextContentElement)) {
          const logicalBase = renderedText.length + 1;
          renderedText += `\n${extra.textContent}`;
          const walker = document.createTreeWalker(extra, NodeFilter.SHOW_TEXT);
          const canvas = document.createElement("canvas");
          const context = canvas.getContext("2d");
          const container = clientRectToRoot(extra.getBoundingClientRect());
          const containerStyle = getComputedStyle(extra.querySelector("div") ?? extra);
          context.font = `${containerStyle.fontStyle} ${containerStyle.fontWeight} ${containerStyle.fontSize} ${containerStyle.fontFamily}`;
          const containerMetrics = context.measureText("Hg");
          const inkHeight = containerMetrics.fontBoundingBoxAscent + containerMetrics.fontBoundingBoxDescent;
          const baseline = container.top + Math.max(0, (container.bottom - container.top - inkHeight) / 2) +
            containerMetrics.fontBoundingBoxAscent;
          let node, logical = logicalBase;
          while ((node = walker.nextNode())) {
            for (let offset = 0; offset < node.data.length; ++offset, ++logical) {
              const range = document.createRange();
              range.setStart(node, offset); range.setEnd(node, offset + 1);
              const rect = range.getBoundingClientRect();
              if (rect.width === 0 && rect.height === 0) continue;
              const extent = clientRectToRoot(rect);
              const mathElement = node.parentElement.closest("math");
              const measuredTextBaseline = extent.top +
                Math.max(0, (extent.bottom - extent.top - inkHeight) / 2) +
                containerMetrics.fontBoundingBoxAscent;
              const textBaseline = hasExplicitBreak ? measuredTextBaseline : baseline;
              chars.push({ logical, start: { x: extent.left, y: mathElement ? baseline : textBaseline },
                end: { x: extent.right, y: mathElement ? baseline : textBaseline }, extent, math: Boolean(mathElement),
                mathExtent: mathElement ? clientRectToRoot(mathElement.getBoundingClientRect()) : null });
            }
          }
          continue;
        }
        const matrix = extra.getScreenCTM();
        const toRoot = (point) => new DOMPoint(point.x, point.y).matrixTransform(matrix).matrixTransform(rootInverse);
        const logicalBase = renderedText.length + 1;
        renderedText += `\n${extra.textContent}`;
        for (let logical = 0; logical < extra.getNumberOfChars(); ++logical) {
          const start = toRoot(extra.getStartPositionOfChar(logical));
          const end = toRoot(extra.getEndPositionOfChar(logical));
          const local = extra.getExtentOfChar(logical);
          const client = extra.getScreenCTM();
          const corners = [new DOMPoint(local.x, local.y), new DOMPoint(local.x + local.width, local.y),
            new DOMPoint(local.x, local.y + local.height), new DOMPoint(local.x + local.width, local.y + local.height)]
            .map((point) => point.matrixTransform(client).matrixTransform(rootInverse));
          const xs = corners.map((point) => point.x), ys = corners.map((point) => point.y);
          chars.push({ logical: logicalBase + logical, start, end,
            extent: { left: Math.min(...xs), top: Math.min(...ys),
              right: Math.max(...xs), bottom: Math.max(...ys) }, math: false });
        }
      }
      if (chars.length === 0) throw new Error(`${fixture.id}: label contains no measurable characters`);

      const textChars = chars.filter((char) => !char.math);
      for (const char of chars.filter((candidate) => candidate.math)) {
        if (textChars.length === 0) continue;
        const center = (char.extent.top + char.extent.bottom) / 2;
        const nearest = textChars.reduce((best, candidate) => {
          const candidateCenter = (candidate.extent.top + candidate.extent.bottom) / 2;
          const bestCenter = (best.extent.top + best.extent.bottom) / 2;
          return Math.abs(candidateCenter - center) < Math.abs(bestCenter - center) ? candidate : best;
        });
        char.start.y = char.end.y = nearest.start.y;
      }

      const lineGroups = [];
      for (const char of chars) {
        let line = lineGroups.find((candidate) => Math.abs(candidate.baseline - char.start.y) <= 0.5);
        if (!line) {
          line = { baseline: char.start.y, chars: [] };
          lineGroups.push(line);
        }
        line.chars.push(char);
      }
      lineGroups.sort((left, right) => left.baseline - right.baseline);
      const lines = lineGroups.map((line) => {
        const logicalChars = [...line.chars].sort((left, right) => left.logical - right.logical);
        const left = Math.min(...logicalChars.map((char) => char.extent.left));
        const right = Math.max(...logicalChars.map((char) => char.extent.right));
        const top = Math.min(...logicalChars.map((char) => char.extent.top));
        const bottom = Math.max(...logicalChars.map((char) => char.extent.bottom));
        const directions = logicalChars.map((char, charIndex) => {
          const value = renderedText.at(char.logical) ?? "";
          if (/^[\u0590-\u08ff]$/u.test(value)) return true;
          if (/^\p{N}$/u.test(value)) return false;
          if (/^[\p{L}\p{M}]$/u.test(value)) return false;
          const next = logicalChars[charIndex + 1];
          const previous = logicalChars[charIndex - 1];
          const delta = next ? next.start.x - char.start.x
                             : previous ? char.start.x - previous.start.x : char.end.x - char.start.x;
          return delta < -0.01;
        });
        const directionByLogical = new Map(logicalChars.map((char, index) => [char.logical, directions[index]]));
        const visualChars = [...logicalChars].sort((leftChar, rightChar) =>
          leftChar.extent.left - rightChar.extent.left || leftChar.logical - rightChar.logical);
        const runs = [];
        for (const char of visualChars) {
          const direction = directionByLogical.get(char.logical);
          let run = runs.at(-1);
          if (!run || run.rightToLeft !== direction || run.math !== char.math) {
            run = { start: char.logical, length: 0, rightToLeft: direction,
              math: char.math, chars: [] };
            runs.push(run);
          }
          run.length += 1;
          run.chars.push(char);
        }
        return {
          start: logicalChars[0].logical,
          length: logicalChars.length,
          width: round(right - left),
          baseline: round(line.baseline - top),
          ascent: round(line.baseline - top),
          descent: round(bottom - line.baseline),
          runs: runs.map((run) => {
            const boxes = run.chars.map((char) => run.math && char.mathExtent
              ? char.mathExtent : char.extent);
            const runLeft = Math.min(...boxes.map((box) => box.left));
            const runRight = Math.max(...boxes.map((box) => box.right));
            return { start: run.start, length: run.length, x: round(runLeft - left),
              width: round(runRight - runLeft), rightToLeft: run.rightToLeft, math: run.math };
          }),
        };
      });
      if (selected.length > 1) {
        measuredBox = { width: round(Math.max(...lines.map((line) => line.width))),
          height: round(lines.reduce((sum, line) => sum + line.ascent + line.descent, 0)) };
      }
      return {
        id: fixture.id,
        kind: fixture.kind,
        label: fixture.label,
        source: fixture.source,
        sequence: fixture.sequence ?? {},
        wrapWidth: fixture.wrapWidth ?? 0,
        logicalComparable: fixture.logicalComparable ?? true,
        text: renderedText,
        elementCount: selected.length,
        box: measuredBox,
        lines,
      };
    }, { fixture: cases[index], index, mermaidModule, fontFaces, fontStack, mathFontFace });
    snapshots.push(snapshot);
  }
  const payload = { mermaidVersion: packageJson.version,
    fontMode: "bundled-noto-stix-two-math-2.13b171", cases: snapshots };
  payload.fixtureSha256 = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${snapshots.length} sequence label cases to ${output}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
