import fs from "node:fs";
import path from "node:path";
import {createHash} from "node:crypto";
import {pathToFileURL} from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ??
  path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const output = path.resolve(process.argv[3] ??
  path.join("tests", "fixtures", "mermaid", "mathml-css-box.json"));
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected Mermaid 11.16.0, found ${packageJson.version}`);

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
if (!fs.existsSync(mathFontPath)) throw new Error(`Missing bundled Math font: ${mathFontPath}`);
const mathFontFace = `@font-face{font-family:"STIX Two Math";src:url("${pathToFileURL(mathFontPath).href}")}`;

const formulas = [
  ["symbol", "x"],
  ["glyph-a", "a"],
  ["glyph-b", "b"],
  ["glyph-c", "c"],
  ["glyph-d", "d"],
  ["glyph-f", "f"],
  ["glyph-g", "g"],
  ["glyph-i", "i"],
  ["glyph-j", "j"],
  ["glyph-l", "l"],
  ["glyph-m", "m"],
  ["glyph-w", "w"],
  ["glyph-y", "y"],
  ["glyph-digit", "2"],
  ["glyph-plus", "+"],
  ["row", "x+y"],
  ["sup", "x^2"],
  ["sub", "x_i"],
  ["subsup", "x_i^2"],
  ["fraction", "\\frac{a}{b}"],
  ["fraction-wide", "\\frac{x+y}{a+b}"],
  ["fraction-nested", "\\frac{\\frac{a}{b}}{c}"],
  ["sqrt", "\\sqrt{x_i^2+y^2}"],
  ["root-index", "\\sqrt[3]{x+1}"],
  ["matrix-2x2", "\\begin{matrix}a&b\\\\c&d\\end{matrix}"],
  ["matrix-3x3", "\\begin{matrix}a&b&c\\\\d&e&f\\\\g&h&i\\end{matrix}"],
  ["fraction-sup", "\\frac{x_i^2}{y_j^3}"],
  ["fraction-radical", "\\frac{\\sqrt{x+1}}{\\sqrt{y}}"],
  ["sqrt-fraction", "\\sqrt{\\frac{a}{b}}"],
  ["greek-row", "\\alpha+\\beta=\\gamma"],
  ["relations", "x\\le y\\ne z"],
  ["large-operators", "\\sum+\\prod+\\int"],
  ["sum-limits", "\\sum_{i=1}^{n}i"],
  ["integral-limits", "\\int_0^1x^2\\,dx"],
  ["accent-hat", "\\hat{x}"],
  ["accent-vector", "\\vec{x+y}"],
  ["delimiter-row", "\\left(x+y\\right)"],
  ["nested-script", "x^{y_i^2}"],
  ["fraction-greek", "\\frac{\\alpha+\\beta}{\\gamma}"],
  ["sqrt-matrix", "\\sqrt{\\begin{matrix}a&b\\\\c&d\\end{matrix}}"],
  ["matrix-fractions", "\\begin{matrix}\\frac{a}{b}&x^2\\\\\\sqrt{y}&z_i\\end{matrix}"],
  ["aligned-equations", "\\begin{aligned}a&=b+c\\\\d&=e-f\\end{aligned}"],
  ["cases-piecewise", "\\begin{cases}x^2&x>0\\\\-x&x\\le0\\end{cases}"],
  ["product-limits", "\\prod_{k=1}^{n}k"],
  ["limit-below", "\\lim_{x\\to0}\\frac{\\sin x}{x}"],
  ["operator-name", "\\operatorname{rank}(A)=n"],
  ["accent-overline", "\\overline{x+y}"],
  ["accent-widehat", "\\widehat{x+y+z}"],
  ["nested-delimiters", "\\left[\\frac{x}{\\left(y+1\\right)}\\right]"],
  ["greek-variants", "\\varepsilon+\\vartheta+\\varphi+\\varrho"],
  ["binomial", "\\binom{n}{k}"],
  ["genfrac-display-rule", "\\genfrac{[}{]}{1pt}{0}{a+b}{c+d}"],
  ["genfrac-text-stack", "\\genfrac{}{}{0pt}{1}{n}{k}"],
  ["display-fraction", "\\dfrac{a+b}{c+d}"],
  ["text-fraction", "\\tfrac{a+b}{c+d}"],
  ["display-binomial", "\\dbinom{n+1}{k}"],
  ["text-binomial", "\\tbinom{n+1}{k}"],
  ["accent-underline", "\\underline{x+y}"],
  ["accent-underbrace", "\\underbrace{x+y}_{n}"],
  ["accent-overbrace", "\\overbrace{x+y}^{n}"],
  ["accent-under-arrow", "\\underleftrightarrow{x+y}"],
  ["tall-delimiter-assembly", "\\left\\{\\begin{matrix}a\\\\b\\\\c\\\\d\\\\e\\\\f\\end{matrix}\\right."],
  ["nested-mathml-structure", "\\left(\\frac{\\underbrace{x+y}_{n}}{\\genfrac{[}{]}{0pt}{}{a}{b}}\\right)"],
];
const cases = [];
for (const [id, tex] of formulas) cases.push({id, tex, fontSize: 16, dpr: 1});
for (const fontSize of [18, 20]) {
  for (const [id, tex] of formulas.filter(([name]) =>
    ["sup", "subsup", "fraction", "sqrt", "matrix-2x2"].includes(name)))
    cases.push({id: `${id}-${fontSize}px`, tex, fontSize, dpr: 1});
}
for (const dpr of [1.25, 1.5, 2]) {
  for (const [id, tex] of formulas.filter(([name]) =>
    ["subsup", "fraction", "sqrt", "matrix-2x2"].includes(name)))
    cases.push({id: `${id}-${String(dpr).replace(".", "") }x`, tex, fontSize: 16, dpr});
}

const {default: puppeteer} = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"))
);
const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});

const snapshots = [];
try {
  const page = await browser.newPage();
  const harness = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  for (let index = 0; index < cases.length; ++index) {
    const fixture = cases[index];
    await page.setViewport({width: 1200, height: 800, deviceScaleFactor: fixture.dpr});
    await page.goto(harness);
    const snapshot = await page.evaluate(async ({fixture, index, mermaidModule, fontFaces, fontStack, mathFontFace}) => {
      const style = document.createElement("style");
      style.textContent = `${fontFaces}\n${mathFontFace}\nmath{font-family:"STIX Two Math" !important}`;
      document.head.appendChild(style);
      await document.fonts.load('16px "STIX Two Math"', "x+∑√");
      await document.fonts.ready;
      const {default: mermaid} = await import(mermaidModule);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: "default",
        fontFamily: fontStack,
        sequence: {useMaxWidth: false, noteFontSize: fixture.fontSize},
      });
      const source = `sequenceDiagram\nA->>B:start\nNote over A,B:Hg $$${fixture.tex}$$ Hg`;
      const {svg} = await mermaid.render(`mathml-box-${index}`, source);
      document.getElementById("container").innerHTML = svg;
      const svgRoot = document.querySelector("svg");
      const foreignObject = svgRoot.querySelector('[data-et="note"] foreignObject');
      const math = foreignObject?.querySelector("math");
      if (!foreignObject || !math) throw new Error(`${fixture.id}: MathML note not found`);
      const rootInverse = svgRoot.getScreenCTM().inverse();
      const round = value => Math.round(value * 1000) / 1000;
      const toRootRect = element => {
        const rect = element.getBoundingClientRect();
        const corners = [new DOMPoint(rect.left, rect.top), new DOMPoint(rect.right, rect.top),
          new DOMPoint(rect.left, rect.bottom), new DOMPoint(rect.right, rect.bottom)]
          .map(point => point.matrixTransform(rootInverse));
        const xs = corners.map(point => point.x), ys = corners.map(point => point.y);
        return {x: round(Math.min(...xs)), y: round(Math.min(...ys)),
          width: round(Math.max(...xs) - Math.min(...xs)),
          height: round(Math.max(...ys) - Math.min(...ys))};
      };
      const mathRect = toRootRect(math);
      const serialize = (element, pathParts = []) => {
        const rect = toRootRect(element);
        const parent = element.parentElement?.closest("math, mrow, mfrac, msup, msub, msubsup, msqrt, mroot, mtable, mtr, mtd");
        const parentRect = parent && parent !== element ? toRootRect(parent) : mathRect;
        const computed = getComputedStyle(element);
        const tag = element.localName;
        const siblingIndex = element.parentElement
          ? [...element.parentElement.children].filter(child => child.localName === tag).indexOf(element) : 0;
        const currentPath = [...pathParts, `${tag}:${siblingIndex}`];
        const result = {
          path: currentPath.join("/"), tag,
          x: round(rect.x - mathRect.x), y: round(rect.y - mathRect.y),
          parentX: round(rect.x - parentRect.x), parentY: round(rect.y - parentRect.y),
          width: rect.width, height: rect.height,
          display: computed.display, fontSize: computed.fontSize,
          fontFamily: computed.fontFamily,
          lineHeight: computed.lineHeight, verticalAlign: computed.verticalAlign,
        };
        const attributes = {};
        for (const name of element.getAttributeNames())
          attributes[name] = element.getAttribute(name);
        if (Object.keys(attributes).length) result.attributes = attributes;
        const text = [...element.childNodes]
          .filter(node => node.nodeType === Node.TEXT_NODE).map(node => node.data).join("").trim();
        if (text) {
          result.text = text;
          const range = document.createRange();
          range.selectNodeContents(element);
          const textClientRect = range.getBoundingClientRect();
          const points = [new DOMPoint(textClientRect.left, textClientRect.top),
            new DOMPoint(textClientRect.right, textClientRect.bottom)]
            .map(point => point.matrixTransform(rootInverse));
          result.textRect = {x: round(points[0].x - mathRect.x),
            y: round(points[0].y - mathRect.y),
            width: round(points[1].x - points[0].x),
            height: round(points[1].y - points[0].y)};
        }
        result.children = [...element.children]
          .filter(child => child.namespaceURI === "http://www.w3.org/1998/Math/MathML")
          .map(child => serialize(child, currentPath));
        if (result.children.length === 0) delete result.children;
        return result;
      };
      const textNode = [...foreignObject.querySelectorAll("div")]
        .flatMap(element => [...element.childNodes])
        .find(node => node.nodeType === Node.TEXT_NODE && node.data.includes("Hg"));
      let textRect = null;
      let textBaseline = null;
      if (textNode) {
        const range = document.createRange();
        range.setStart(textNode, 0);
        range.setEnd(textNode, Math.min(2, textNode.data.length));
        const rect = range.getBoundingClientRect();
        const points = [new DOMPoint(rect.left, rect.top), new DOMPoint(rect.right, rect.bottom)]
          .map(point => point.matrixTransform(rootInverse));
        textRect = {x: round(points[0].x), y: round(points[0].y),
          width: round(points[1].x - points[0].x), height: round(points[1].y - points[0].y)};
        const container = toRootRect(foreignObject);
        const containerStyle = getComputedStyle(foreignObject.querySelector("div"));
        const canvas = document.createElement("canvas");
        const context = canvas.getContext("2d");
        context.font = `${containerStyle.fontStyle} ${containerStyle.fontWeight} ${containerStyle.fontSize} ${containerStyle.fontFamily}`;
        const textMetrics = context.measureText("Hg");
        const inkHeight = textMetrics.fontBoundingBoxAscent + textMetrics.fontBoundingBoxDescent;
        textBaseline = round(container.y + Math.max(0, (container.height - inkHeight) / 2) +
          textMetrics.fontBoundingBoxAscent);
      }
      return {
        ...fixture,
        source,
        foreignObject: toRootRect(foreignObject),
        math: mathRect,
        text: textRect,
        textBaseline,
        tree: serialize(math),
      };
    }, {fixture, index, mermaidModule, fontFaces, fontStack, mathFontFace});
    snapshots.push(snapshot);
  }
} finally {
  await browser.close();
}

const payload = {mermaidVersion: packageJson.version, katexVersion: "0.16.45",
  fontMode: "bundled-noto-stix-two-math-2.13b171", cases: snapshots};
payload.fixtureSha256 = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
console.log(`Wrote ${snapshots.length} MathML CSS box cases to ${output}`);
console.log(`fixtureSha256=${payload.fixtureSha256}`);
