import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const output = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "flowchart-label.json"));
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0") throw new Error(`Expected Mermaid 11.16.0, found ${packageJson.version}`);

const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [
  ["Noto Sans", "NotoSans-Regular.ttf", "U+0000-058F,U+0900-2E7F"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf", "U+2E80-9FFF,U+AC00-D7AF"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf", "U+0600-08FF"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf", "U+0590-05FF"],
];
const fontFaces = fonts.map(([family, file, range]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}");unicode-range:${range}}`
).join("\n");
const fontStack = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';
const mathFontPath = path.resolve("third_party", "stix", "fonts", "STIXTwoMath-Regular.otf");
const mathFontFace = `@font-face{font-family:"STIX Two Math";src:url("${pathToFileURL(mathFontPath).href}")}`;

const cases = [
  { id: "plain", labelType: "markdown", label: "$$x+1$$" },
  { id: "fraction", labelType: "markdown", label: "$$\\frac{x+1}{y-1}$$" },
  { id: "radical", labelType: "markdown", label: "$$\\sqrt{x+1}$$" },
  { id: "root-index", labelType: "markdown", label: "$$\\sqrt[3]{x+1}$$" },
  { id: "supsub", labelType: "markdown", label: "$$x_i^2$$" },
  { id: "sup-expression", labelType: "markdown", label: "$$x^2 + 1$$" },
  { id: "array", labelType: "markdown", label: "$$\\begin{matrix}a&b\\end{matrix}$$" },
  { id: "mixed-fraction", labelType: "markdown", label: "before $$\\frac{x}{2}$$ after" },
  { id: "multiple", labelType: "markdown", label: "left $$x$$ middle $$y^2$$ right" },
  { id: "break", labelType: "markdown", label: "before $$x$$<br/>after $$y$$" },
  { id: "formatted", labelType: "markdown", label: "**bold** $$x_i^2$$ tail" },
  { id: "cjk", labelType: "markdown", label: "\u4e2d\u6587 $$\\frac{x}{2}$$ \u7ed3\u679c" },
  { id: "rtl", labelType: "markdown", label: "\u0645\u0631\u062d\u0628\u0627 $$x^2$$ \u05e9\u05dc\u05d5\u05dd" },
  { id: "html-math", labelType: "string", label: "<b>Bold</b> $$\\sqrt{x}$$ <i>tail</i>" },
  { id: "html-block-whitespace", labelType: "string",
    label: "<b>Left middle</b> <i>Tail end</i> $$x$$ <strong>Done now</strong>" },
  { id: "markdown-html-math", labelType: "markdown", label: "**Bold** CJK\u4e2d\u6587 $$x_i^2$$" },
  { id: "cjk-no-space", labelType: "markdown", label: "\u4e2d\u6587\u7ed3\u679c$$\\frac{x}{2}$$\u65e5\u672c\u8a9e" },
  { id: "arabic-only", labelType: "markdown", label: "\u0645\u0631\u062d\u0628\u0627 $$\\sqrt{x}$$ \u0628\u0627\u0644\u0639\u0627\u0644\u0645" },
  { id: "hebrew-only", labelType: "markdown", label: "\u05e9\u05dc\u05d5\u05dd $$x^2$$ \u05e2\u05d5\u05dc\u05dd" },
  { id: "bidi-numeric", labelType: "markdown", label: "ABC \u0645\u0631\u062d\u0628\u0627 123 $$x+1$$ \u05e9\u05dc\u05d5\u05dd" },
  { id: "mixed-format-math", labelType: "markdown", label: "**\u4e2d\u6587** \u0645\u0631\u062d\u0628\u0627 $$\\frac{x}{y}$$ \u05e9\u05dc\u05d5\u05dd" },
];

const sourceFor = (fixture) => {
  const escaped = fixture.label.replaceAll('"', "&quot;");
  const body = fixture.labelType === "markdown" ? `\`${escaped}\`` : escaped;
  return `flowchart LR\nA["${body}"]`;
};

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
  await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
  const harness = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const snapshots = [];
  for (let index = 0; index < cases.length; ++index) {
    const fixture = { ...cases[index], source: sourceFor(cases[index]) };
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
        flowchart: { defaultRenderer: "dagre-wrapper", htmlLabels: false },
      });
      const { svg } = await mermaid.render(`flowchart-label-${index}`, fixture.source);
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(resolve));
      const root = document.querySelector("svg");
      const rootCtm = root.getScreenCTM();
      const mathElements = [...root.querySelectorAll("g.node math")];
      if (mathElements.length === 0) throw new Error(`${fixture.id}: Math label is missing`);
      const label = mathElements[0].closest("foreignObject") ?? mathElements[0].parentElement;
      const content = label.querySelector("div") ?? label;
      const round = (value) => Math.round(value * 1000) / 1000;
      const rootInverse = rootCtm.inverse();
      const clientRectToRoot = (rect) => {
        const points = [new DOMPoint(rect.left, rect.top),
          new DOMPoint(rect.right, rect.top),
          new DOMPoint(rect.left, rect.bottom),
          new DOMPoint(rect.right, rect.bottom)]
          .map((point) => point.matrixTransform(rootInverse));
        const xs = points.map((point) => point.x);
        const ys = points.map((point) => point.y);
        return { left: Math.min(...xs), top: Math.min(...ys),
          right: Math.max(...xs), bottom: Math.max(...ys) };
      };
      const labelRect = clientRectToRoot(content.getBoundingClientRect());
      const relativeRect = (clientRect) => {
        const rect = clientRectToRoot(clientRect);
        return { x: round(rect.left - labelRect.left),
          y: round(rect.top - labelRect.top),
          width: round(rect.right - rect.left),
          height: round(rect.bottom - rect.top) };
      };

      const walker = document.createTreeWalker(content, NodeFilter.SHOW_TEXT);
      const chars = [];
      const textRuns = [];
      let node;
      let textNodeIndex = 0;
      while ((node = walker.nextNode())) {
        if (node.parentElement.closest("math")) continue;
        const nodeRange = document.createRange();
        nodeRange.selectNodeContents(node);
        const nodeRect = nodeRange.getBoundingClientRect();
        const nodeStyle = getComputedStyle(node.parentElement);
        const ancestors = [];
        for (let ancestor = node.parentElement;
             ancestor && ancestor !== label; ancestor = ancestor.parentElement) {
          const ancestorStyle = getComputedStyle(ancestor);
          ancestors.push({ tag: ancestor.localName, className: ancestor.className,
            display: ancestorStyle.display, transform: ancestorStyle.transform,
            zoom: ancestorStyle.zoom, fontSize: ancestorStyle.fontSize,
            lineHeight: ancestorStyle.lineHeight });
        }
        if (nodeRect.width > 0 || nodeRect.height > 0) {
          const canvas = document.createElement("canvas");
          const context = canvas.getContext("2d");
          const familyWidths = {};
          for (const family of ["Noto Sans", "Arial", "Trebuchet MS", "sans-serif"]) {
            context.font = `400 16px "${family}"`;
            familyWidths[family] = round(context.measureText(node.data).width);
          }
          textRuns.push({ text: node.data, ...relativeRect(nodeRect),
            screenWidth: round(nodeRect.width),
            fontFamily: nodeStyle.fontFamily, fontSize: nodeStyle.fontSize,
            fontWeight: nodeStyle.fontWeight,
            fontStretch: nodeStyle.fontStretch,
            fontVariationSettings: nodeStyle.fontVariationSettings,
            fontKerning: nodeStyle.fontKerning,
            letterSpacing: nodeStyle.letterSpacing, familyWidths, ancestors });
        }
        for (let offset = 0; offset < node.data.length; ++offset) {
          const range = document.createRange();
          range.setStart(node, offset);
          range.setEnd(node, offset + 1);
          const rect = range.getBoundingClientRect();
          if (rect.width === 0 && rect.height === 0) continue;
          chars.push({ value: node.data[offset], rect: relativeRect(rect),
            fontFamily: nodeStyle.fontFamily, textNodeIndex,
            textNodeOffset: offset });
        }
        ++textNodeIndex;
      }
      const visualRuns = [];
      const nodeIndexes = [...new Set(chars.map((item) => item.textNodeIndex))];
      for (const nodeIndex of nodeIndexes) {
        const nodeChars = chars.filter((item) => item.textNodeIndex === nodeIndex);
        const nodeTrimStart = Math.min(...nodeChars
          .filter((item) => !/^[\t\n\f\r ]$/u.test(item.value))
          .map((item) => item.textNodeOffset));
        const visualChars = chars
          .filter((item) => item.textNodeIndex === nodeIndex && item.rect.width > 0.01)
          .sort((left, right) => left.rect.x - right.rect.x ||
            left.textNodeOffset - right.textNodeOffset);
        for (let offset = 0; offset < visualChars.length;) {
          let end = offset + 1;
          let logicalStep = 0;
          while (end < visualChars.length) {
            const step = visualChars[end].textNodeOffset -
              visualChars[end - 1].textNodeOffset;
            if (Math.abs(step) !== 1 ||
                (logicalStep !== 0 && step !== logicalStep)) break;
            logicalStep = step;
            ++end;
          }
          const slice = visualChars.slice(offset, end);
          const logicalChars = slice.filter((item) => !/^[\t\n\f\r ]$/u.test(item.value));
          const logicalStart = Math.min(...logicalChars.map((item) => item.textNodeOffset));
          const logicalEnd = Math.max(...logicalChars.map((item) => item.textNodeOffset)) + 1;
          const left = Math.min(...slice.map((item) => item.rect.x));
          const right = Math.max(...slice.map((item) => item.rect.x + item.rect.width));
          visualRuns.push({ start: logicalStart - nodeTrimStart,
            length: logicalEnd - logicalStart,
            x: round(left), width: round(right - left), rightToLeft: logicalStep < 0,
            fontFamily: slice[0].fontFamily, textNodeIndex: nodeIndex });
          offset = end;
        }
      }
      visualRuns.sort((left, right) => left.x - right.x ||
        left.textNodeIndex - right.textNodeIndex);
      const math = mathElements.map((element) => {
        const rect = relativeRect(element.getBoundingClientRect());
        const style = getComputedStyle(element);
        const parentStyle = getComputedStyle(element.parentElement);
        const elements = [...element.querySelectorAll(
          "mrow,mfrac,msqrt,mroot,msub,msup,msubsup,mtable,mtr,mtd,mi,mo,mn")]
          .map((child) => ({ tag: child.localName,
            ...relativeRect(child.getBoundingClientRect()) }));
        return { ...rect, text: element.textContent,
          fontSize: style.fontSize, lineHeight: style.lineHeight,
          display: style.display, verticalAlign: style.verticalAlign,
          parentDisplay: parentStyle.display,
          parentFontSize: parentStyle.fontSize,
          parentLineHeight: parentStyle.lineHeight, elements };
      });
      const items = [
        ...chars.map((char) => ({ kind: "text", ...char.rect })),
        ...math.map((box) => ({ kind: "math", ...box })),
      ].sort((left, right) => left.y - right.y || left.x - right.x);
      const lines = [];
      for (const item of items) {
        let line = lines.find((candidate) =>
          item.y < candidate.bottom - 0.5 && item.y + item.height > candidate.top + 0.5);
        if (!line) {
          line = { top: item.y, bottom: item.y + item.height, items: [] };
          lines.push(line);
        }
        line.top = Math.min(line.top, item.y);
        line.bottom = Math.max(line.bottom, item.y + item.height);
        line.items.push(item);
      }
      lines.sort((left, right) => left.top - right.top);
      const serializedLines = [{ x: 0, y: 0,
        width: round(labelRect.right - labelRect.left),
        height: round(labelRect.bottom - labelRect.top) }];
      return {
        id: fixture.id, labelType: fixture.labelType, label: fixture.label,
        source: fixture.source,
        text: content.textContent,
        rootCtm: { scaleX: round(rootCtm.a), scaleY: round(rootCtm.d) },
        box: { width: round(labelRect.right - labelRect.left),
          height: round(labelRect.bottom - labelRect.top) },
        lines: serializedLines, chars, visualRuns, textRuns, math,
      };
    }, { fixture, index, mermaidModule, fontFaces, fontStack, mathFontFace });
    snapshots.push(snapshot);
  }
  const payload = { mermaidVersion: packageJson.version,
    fontMode: "bundled-noto-stix-two-math-2.13b171", cases: snapshots };
  payload.fixtureSha256 = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${snapshots.length} flowchart label cases to ${output}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
