import fs from "node:fs";
import path from "node:path";
import {createHash} from "node:crypto";
import {pathToFileURL} from "node:url";
import {PNG} from "pngjs";

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
  ["sqrt-plain", "\\sqrt{x}"],
  ["root-index", "\\sqrt[3]{x+1}"],
  ["root-index-fraction", "\\sqrt[\\frac{a}{b}]{x+1}"],
  ["root-index-radical", "\\sqrt[\\sqrt{n}]{x+1}"],
  ["root-index-supsub", "\\sqrt[x_i^2]{y}"],
  ["matrix-2x2", "\\begin{matrix}a&b\\\\c&d\\end{matrix}"],
  ["matrix-3x3", "\\begin{matrix}a&b&c\\\\d&e&f\\\\g&h&i\\end{matrix}"],
  ["fraction-sup", "\\frac{x_i^2}{y_j^3}"],
  ["fraction-radical", "\\frac{\\sqrt{x+1}}{\\sqrt{y}}"],
  ["radical-script-fraction", "\\sqrt{x^{\\frac{a}{b}}}"],
  ["script-radical-fraction", "x_{\\sqrt{\\frac{a}{b}}}"],
  ["fraction-cross-recursive", "\\frac{\\sqrt{x_i^2}}{y^{\\frac{a}{b}}}"],
  ["sqrt-fraction", "\\sqrt{\\frac{a}{b}}"],
  ["greek-row", "\\alpha+\\beta=\\gamma"],
  ["relations", "x\\le y\\ne z"],
  ["large-operators", "\\sum+\\prod+\\int"],
  ["sum-limits", "\\sum_{i=1}^{n}i"],
  ["integral-limits", "\\int_0^1x^2\\,dx"],
  ["accent-hat", "\\hat{x}"],
  ["accent-vector", "\\vec{x+y}"],
  ["delimiter-row", "\\left(x+y\\right)"],
  ["left-right-nullable-plain", "\\left.x+y\\right\\rangle"],
  ["left-right-middle", "\\left\\langle x\\middle|y\\right\\rangle"],
  ["left-right-multiple-middle", "\\left(x\\middle|y\\middle\\|z\\right)"],
  ["left-right-nested-plain", "\\left[ x+\\left(y-z\\right)\\right]"],
  ["left-right-middle-fraction", "\\left(\\frac{a\\middle|b}{c}\\right)"],
  ["left-right-middle-radical", "\\left(\\sqrt{x\\middle|y}\\right)"],
  ["left-right-middle-script", "\\left(x^{a\\middle|b}\\right)"],
  ["left-right-middle-array", "\\left(\\begin{matrix}a\\middle|b&c\\\\d&e\\end{matrix}\\right)"],
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
  ["accent-text-shaping", "\\widehat{\\text{office}}"],
  ["accent-double-right-arrow", "\\Overrightarrow{x+y}"],
  ["accent-left-harpoon", "\\overleftharpoon{x+y}"],
  ["accent-right-harpoon", "\\overrightharpoon{x+y}"],
  ["accent-overgroup", "\\overgroup{x+y}"],
  ["accent-overlinesegment-upstream-text", "\\overlinesegment{x+y}"],
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
  ["tall-paren-assembly", "\\left(\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right)"],
  ["tall-bracket-assembly", "\\left[\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right]"],
  ["tall-bar-assembly", "\\left|\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right|"],
  ["nested-delimiter-array", "\\left(\\begin{matrix}\\left[\\frac{a}{b}\\right]&c\\\\d&\\left\\{\\frac{x}{y}\\right\\}\\end{matrix}\\right)"],
  ["tall-double-bar", "\\left\\|\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\|"],
  ["tall-floor", "\\left\\lfloor\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\rfloor"],
  ["tall-ceil", "\\left\\lceil\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\rceil"],
  ["tall-angle", "\\left\\langle\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\rangle"],
  ["nullable-delimiter", "\\left.\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\rceil"],
  ["delimiter-recursive", "\\left\\langle\\frac{\\sqrt{x_i^2}}{\\overbrace{a+b}^{n}}\\right\\rangle"],
  ["nested-mathml-structure", "\\left(\\frac{\\underbrace{x+y}_{n}}{\\genfrac{[}{]}{0pt}{}{a}{b}}\\right)"],
  ["accent-fraction-recursive", "\\underbrace{\\frac{a}{b}}_{n}"],
  ["accent-radical-recursive", "\\overbrace{\\sqrt{x_i^2}}^{n}"],
  ["accent-array-recursive", "\\underbrace{\\begin{matrix}\\frac{a}{b}&\\sqrt{x_i^2}\\\\c&d\\end{matrix}}_{n}"],
  ["array-cell-accent-recursive", "\\begin{matrix}\\underbrace{a}_{n}&b\\\\c&d\\end{matrix}"],
  ["radical-accent-recursive", "\\sqrt{\\underbrace{x}_{n}}"],
  ["supsub-accent-recursive", "x^{\\overbrace{a}^{n}}"],
  ["accent-accent-recursive", "\\overbrace{\\underbrace{x}_{i}}^{n}"],
  ["fraction-array-accent-recursive", "\\frac{\\begin{matrix}\\underbrace{a}_{n}&b\\end{matrix}}{c}"],
  ["accent-arrow-recursive", "\\overbrace{\\underleftrightarrow{x+y}}^{n}"],
  ["accent-mixed-fraction-body", "\\overbrace{x+\\frac{a}{b}+y}^{n}"],
  ["accent-mixed-radical-body", "\\overrightarrow{x+\\sqrt{y}}"],
  ["accent-mixed-fraction-annotation", "\\overbrace{x}^{i+\\frac{a}{b}}"],
  ["root-mixed-fraction", "p+\\frac{a}{b}+q"],
  ["root-mixed-radical", "p+\\sqrt{x}+q"],
  ["root-multiple-semantics", "\\sqrt{x}+\\frac{a}{b}+y_i"],
  ["root-mixed-accent", "p+\\overbrace{x}^{n}+q"],
  ["root-mixed-array", "p+\\begin{matrix}a&b\\\\c&d\\end{matrix}+q"],
  ["root-mixed-left-right", "p+\\left(x+y\\right)+q"],
  ["root-double-fraction", "\\frac{a}{b}+\\frac{c}{d}"],
  ["root-all-paint-kinds", "\\overbrace{x}^{n}+\\left(y+z\\right)+\\begin{matrix}a&b\\end{matrix}"],
  ["root-mixed-underbrace", "p+\\underbrace{x}_{n}+q"],
  ["root-mixed-under-arrow", "p+\\underleftrightarrow{x}+q"],
  ["root-mixed-sum-limits", "p+\\sum_{i=1}^{n}+q"],
  ["root-mixed-integral-scripts", "p+\\int_0^1+q"],
  ["root-limits-fraction", "\\sum_{i=1}^{n}+\\frac{a}{b}"],
  ["root-mixed-product-limits", "p+\\prod_{k=1}^{n}+q"],
  ["root-mixed-coproduct-limits", "p+\\coprod_{k=1}^{n}+q"],
  ["root-mixed-double-integral", "p+\\iint_D+q"],
  ["root-mixed-triple-integral", "p+\\iiint_D+q"],
  ["root-mixed-cjk-fraction", "p+\\text{中文}+\\frac{a}{b}+q"],
  ["root-mixed-rtl-fraction", "p+\\text{سلام}+\\frac{a}{b}+q"],
  ["text-hebrew", "\\text{שלום}"],
  ["text-arabic-hebrew", "\\text{سلام שלום}"],
  ["text-bidi-digits-punctuation", "\\text{سلام (123), שלום!}"],
  ["text-bidi-isolates", "\\text{\u2067سلام 123\u2069, \u2066ABC\u2069}"],
  ["fraction-fallback-text", "\\frac{\\text{中文}}{\\text{سلام}}"],
  ["radical-fallback-text", "\\sqrt{\\text{שלום}+x}"],
  ["supsub-fallback-text", "x_{\\text{中文}}^{\\text{سلام}}"],
  ["accent-fallback-text", "\\overbrace{\\text{中文}}^{\\text{שלום}}"],
  ["array-fallback-text", "\\begin{matrix}\\text{中文}&\\text{سلام}\\\\\\text{שלום}&x\\end{matrix}"],
  ["limits-fallback-text", "\\sum_{\\text{\u4e2d\u6587}}^{\\text{\u0633\u0644\u0627\u0645}}"],
  ["limits-fallback-recursive", "\\sum_{\\frac{\\text{\u4e2d\u6587}}{x}}^{\\sqrt{\\text{\u05e9\u05dc\u05d5\u05dd}}}"],
  ["under-accent-fallback-text", "\\underbrace{\\text{\u0633\u0644\u0627\u0645}}_{\\text{\u4e2d\u6587}}"],
  ["delimiter-assembly-fallback-text", "\\left(\\begin{matrix}\\text{\u4e2d\u6587}\\\\\\text{\u0633\u0644\u0627\u0645}\\\\\\text{\u05e9\u05dc\u05d5\u05dd}\\\\x\\end{matrix}\\right)"],
  ["product-fallback-limits", "\\prod_{\\text{\u4e2d\u6587}}^{\\text{\u0633\u0644\u0627\u0645}}"],
  ["coproduct-fallback-limits", "\\coprod_{\\text{\u05e9\u05dc\u05d5\u05dd}}^{\\text{\u4e2d\u6587}}"],
  ["over-arrow-fallback-text", "\\overleftrightarrow{\\text{\u0633\u0644\u0627\u0645 \u4e2d\u6587}}"],
  ["under-arrow-fallback-text", "\\underleftrightarrow{\\text{\u05e9\u05dc\u05d5\u05dd \u4e2d\u6587}}"],
  ["brace-assembly-fallback-text", "\\left\\{\\begin{matrix}\\text{\u4e2d\u6587}\\\\\\text{\u0633\u0644\u0627\u0645}\\\\\\text{\u05e9\u05dc\u05d5\u05dd}\\\\x\\end{matrix}\\right\\}"],
  ["bracket-assembly-fallback-text", "\\left[\\begin{matrix}\\text{\u4e2d\u6587}\\\\\\text{\u0633\u0644\u0627\u0645}\\\\\\text{\u05e9\u05dc\u05d5\u05dd}\\\\x\\end{matrix}\\right]"],
  ["angle-assembly-fallback-text", "\\left\\langle\\begin{matrix}\\text{\u4e2d\u6587}\\\\\\text{\u0633\u0644\u0627\u0645}\\\\\\text{\u05e9\u05dc\u05d5\u05dd}\\\\x\\end{matrix}\\right\\rangle"],
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
const pixelBounds = (png, predicate) => {
  let left = png.width, top = png.height, right = -1, bottom = -1;
  for (let y = 0; y < png.height; ++y) {
    for (let x = 0; x < png.width; ++x) {
      const offset = 4 * (y * png.width + x);
      if (!predicate(png.data[offset], png.data[offset + 1],
                     png.data[offset + 2], png.data[offset + 3], x, y)) continue;
      left = Math.min(left, x); top = Math.min(top, y);
      right = Math.max(right, x); bottom = Math.max(bottom, y);
    }
  }
  return right < left ? null : {left, top, right, bottom};
};
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
      const rootIndexOracle = fixture.id.startsWith("root-index");
      style.textContent = `${fontFaces}\n${mathFontFace}\nmath{font-family:"STIX Two Math",${fontStack} !important}` +
        (rootIndexOracle
          ? `\nmath mroot{color:rgb(255,0,255)!important}` +
            `\nmath mroot>*{color:transparent!important}`
          : "");
      document.head.appendChild(style);
      await Promise.all([
        document.fonts.load('16px "Noto Sans"', "Latin"),
        document.fonts.load('16px "Noto Sans CJK SC"', "\u4e2d\u6587"),
        document.fonts.load('16px "Noto Sans Arabic"', "\u0633\u0644\u0627\u0645"),
        document.fonts.load('16px "Noto Sans Hebrew"', "\u05e9\u05dc\u05d5\u05dd"),
        document.fonts.load('16px "STIX Two Math"',
          "x+\u2211\u221a\u220f\u2210\u2194\u23de\u23df()[]{}\u27e8\u27e9"),
      ]);
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
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(() =>
        requestAnimationFrame(resolve)));
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
        screenMath: rootIndexOracle ? (() => {
          const rect = math.getBoundingClientRect();
          return {x: rect.x, y: rect.y, width: rect.width, height: rect.height};
        })() : undefined,
      };
    }, {fixture, index, mermaidModule, fontFaces, fontStack, mathFontFace});
    if (snapshot.screenMath) {
      const clip = snapshot.screenMath;
      const png = PNG.sync.read(await page.screenshot({
        omitBackground: true,
        clip: {x: clip.x, y: clip.y, width: clip.width, height: clip.height},
      }));
      const mroot = snapshot.tree.children?.[0]?.children?.find(
        child => child.tag === "mroot");
      if (!mroot || !mroot.children?.length)
        throw new Error(`${fixture.id}: serialized mroot not found`);
      const bodyBoundary = mroot.children[0].x / snapshot.math.width * png.width;
      const isDecoration = (red, green, blue, alpha) =>
        alpha > 0 && red - green > 12 && blue - green > 12 &&
        Math.abs(red - blue) < 20;
      const glyph = pixelBounds(png, (r, g, b, a, x) =>
        x < bodyBoundary && isDecoration(r, g, b, a));
      const rule = pixelBounds(png, (r, g, b, a, x) =>
        x >= bodyBoundary && isDecoration(r, g, b, a));
      const toMathRect = bounds => bounds ? {
        x: Math.round(bounds.left / png.width * snapshot.math.width * 1000) / 1000,
        y: Math.round(bounds.top / png.height * snapshot.math.height * 1000) / 1000,
        width: Math.round((bounds.right - bounds.left + 1) / png.width *
                          snapshot.math.width * 1000) / 1000,
        height: Math.round((bounds.bottom - bounds.top + 1) / png.height *
                           snapshot.math.height * 1000) / 1000,
      } : null;
      snapshot.radicalGlyphInk = toMathRect(glyph);
      snapshot.radicalRuleInk = toMathRect(rule);
      if (!snapshot.radicalGlyphInk || !snapshot.radicalRuleInk)
        throw new Error(`${fixture.id}: radical decoration pixels not found`);
      delete snapshot.screenMath;
    }
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
