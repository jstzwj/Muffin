import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const outDir = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "sequence-pixel"));
const chrome = process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (pkg.version !== "11.16.0") throw new Error(`Expected Mermaid 11.16.0, found ${pkg.version}`);
const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [["Noto Sans","NotoSans-Regular.ttf"],["Noto Sans CJK SC","NotoSansCJKsc-Regular.otf"],
  ["Noto Sans Arabic","NotoSansArabic-Regular.ttf"],["Noto Sans Hebrew","NotoSansHebrew-Regular.ttf"]];
const faces = fonts.map(([family,file]) => `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir,file)).href}")}`).join("\n");
const stack = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';
const mathFontPath = path.resolve("third_party", "stix", "fonts", "STIXTwoMath-Regular.otf");
const mathFace = `@font-face{font-family:"STIX Two Math";src:url("${pathToFileURL(mathFontPath).href}")}`;
const cases = [
  { id: "basic", source: "sequenceDiagram\nparticipant A as Alice\nactor B as Bob\nA->>B:Hello\nB-->>A:Return" },
  { id: "activation-note", source: "sequenceDiagram\nA->>+B:Call\nNote over A,B:Working 中文\nB-->>-A:Done" },
  { id: "nested-fragment", source: "sequenceDiagram\nalt Success\nloop Retry\nA->>B:Request\nend\nelse Failure\nA-xB:Error\nend" },
  { id: "participant-types", source: ["sequenceDiagram", "participant P as Plain", "actor A as Actor",
      'participant B@{ "type": "boundary" } as Boundary', 'participant C@{ "type": "control" } as Control',
      'participant E@{ "type": "entity" } as Entity', 'participant D@{ "type": "database" } as Database',
      'participant S@{ "type": "collections" } as Collections', 'participant Q@{ "type": "queue" } as Queue',
      "P->>Q:All types"].join("\n") },
  { id: "create-destroy-markers", source: ["sequenceDiagram", "participant A as Alice",
      "participant B as Bob", "A->>B:solid", "B-->>A:dotted", "A<<->>B:both",
      "A-)B:point", "B-xA:cross", "create participant C as Created", "A->>C:create",
      "destroy C", "C-->>A:destroy"].join("\n") },
  { id: "central-autonumber", source: ["sequenceDiagram", "autonumber 10 5",
      "A->>()B:forward central", "A()->>B:reverse central", "A()->>()B:dual central",
      "B<<-->>A:bidirectional", "autonumber off", "A-->>B:unnumbered"].join("\n") },
  { id: "self-autonumber", source: ["sequenceDiagram", "autonumber", "A->>A:self solid",
      "A-->>A:self dotted", "A-xA:self cross", "A<<->>A:self both"].join("\n") },
  { id: "label-participant-html-cjk", cropSelector: '[data-et="participant"][data-id="A"] text', cropKind: "participant", source: ["sequenceDiagram",
      "participant A as <b>Client</b><br/>\u5ba2\u6237\u7aef", "participant B as Server",
      "A->>B:ping"].join("\n") },
  { id: "label-message-wrap-bidi", cropSelector: ".messageText", cropCount: 2, cropKind: "message", source: [
      '%%{init: {"sequence": {"wrap": true, "wrapPadding": 10}}}%%', "sequenceDiagram",
      "A->>B:wrap:alpha beta gamma delta epsilon zeta eta theta",
      "B-->>A:Hello \u0645\u0631\u062d\u0628\u0627 \u05e9\u05dc\u05d5\u05dd"].join("\n") },
  { id: "label-note-markdown-math", cropSelector: '[data-et="note"] foreignObject', cropKind: "note", source: ["sequenceDiagram", "A->>B:start",
      "Note over A,B:`**Speed** $$x^2$$`", "B-->>A:done"].join("\n") },
  { id: "label-fragment-html-rtl", cropSelector: ".loopText", cropKind: "fragment", source: ["sequenceDiagram",
      "alt <b>Success</b><br/>\u0646\u062c\u0627\u062d", "A->>B:ok", "else fallback",
      "B-->>A:return", "end"].join("\n") },
  { id: "label-box-markdown-math", cropSelector: "text.text", cropKind: "box", source: ["sequenceDiagram",
      "box rgb(238, 246, 255) `**Services** $$x$$`", "participant A", "participant B", "end",
      "A->>B:call"].join("\n") },
  { id: "label-dpr-125-html-cjk", dpr: 1.25, cropSelector: '[data-et="participant"][data-id="A"] text', cropKind: "participant",
    source: "sequenceDiagram\nparticipant A as <b>Client</b><br/>客户端\nA->>B:ping" },
  { id: "label-dpr-150-math-rtl", dpr: 1.5, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:中文 $$\\frac{x}{2}$$ مرحبا" },
  { id: "label-dpr-200-dark-box-fragment", dpr: 2, theme: "dark", cropSelector: ".loopText", cropKind: "fragment",
    source: ['%%{init: {"theme": "dark"}}%%', "sequenceDiagram", "box rgb(45, 52, 64) Services",
      "participant A", "participant B", "end", "alt <b>成功</b> שלום", "A->>B:中文", "end"].join("\n") },
  { id: "label-dpr-125-dark-html", dpr: 1.25, theme: "dark", cropSelector: ".messageText", cropFirst: true, cropKind: "message",
    source: ['%%{init: {"theme": "dark"}}%%', "sequenceDiagram", "A->>B:<b>中文</b> مرحبا"].join("\n") },
  { id: "label-dpr-150-dark-math", dpr: 1.5, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: ['%%{init: {"theme": "dark"}}%%', "sequenceDiagram", "A->>B:start", "Note over A,B:$$\\sqrt{x_i^2}$$"].join("\n") },
  { id: "label-dpr-200-default-box", dpr: 2, cropSelector: "text.text", cropKind: "box",
    source: "sequenceDiagram\nbox rgb(238, 246, 255) 服务 Services\nparticipant A\nparticipant B\nend\nA->>B:call" },
  { id: "label-math-genfrac", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\genfrac{[}{]}{1pt}{0}{a+b}{c+d}$$\nB-->>A:done" },
  { id: "label-math-fraction-ops", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\frac{a+b}{c+d}$$\nB-->>A:done" },
  { id: "label-math-stack-ops", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\genfrac{}{}{0pt}{1}{n}{k}$$\nB-->>A:done" },
  { id: "label-math-nested-fraction-ops", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\frac{\\frac{a}{b}}{c}$$\nB-->>A:done" },
  { id: "label-math-fraction-script-ops", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\frac{x_i^2}{y_j^3}$$\nB-->>A:done" },
  { id: "label-math-fraction-radical-ops", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\frac{\\sqrt{x+1}}{\\sqrt{y}}$$\nB-->>A:done" },
  { id: "label-math-radical-script-fraction-ops", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\sqrt{x^{\\frac{a}{b}}}$$\nB-->>A:done" },
  { id: "label-math-fraction-cross-recursive-ops", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\frac{\\sqrt{x_i^2}}{y^{\\frac{a}{b}}}$$\nB-->>A:done" },
  { id: "label-math-underbrace", dpr: 1.5, theme: "dark",
    cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\underbrace{x+y}_{n}$$' },
  { id: "label-math-under-arrow", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\underleftrightarrow{x+y}$$" },
  { id: "label-math-overbrace", dpr: 2, theme: "dark",
    cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\overbrace{x+y}^{n}$$' },
  { id: "label-math-accent-text-shaping", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\widehat{\\text{office}}$$" },
  { id: "label-math-accent-double-right-arrow", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\Overrightarrow{x+y}$$" },
  { id: "label-math-accent-left-harpoon", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overleftharpoon{x+y}$$" },
  { id: "label-math-accent-right-harpoon", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overrightharpoon{x+y}$$" },
  { id: "label-math-accent-overgroup", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overgroup{x+y}$$" },
  { id: "label-math-accent-overlinesegment-upstream-text", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overlinesegment{x+y}$$" },
  { id: "label-math-accent-mixed-fraction-body", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overbrace{x+\\frac{a}{b}+y}^{n}$$" },
  { id: "label-math-accent-mixed-radical-body", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overrightarrow{x+\\sqrt{y}}$$" },
  { id: "label-math-accent-mixed-fraction-annotation", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overbrace{x}^{i+\\frac{a}{b}}$$" },
  { id: "label-math-root-mixed-fraction", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\frac{a}{b}+q$$" },
  { id: "label-math-root-mixed-radical", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\sqrt{x}+q$$" },
  { id: "label-math-root-multiple-semantics", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\sqrt{x}+\\frac{a}{b}+y_i$$" },
  { id: "label-math-root-mixed-accent", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\overbrace{x}^{n}+q$$" },
  { id: "label-math-root-mixed-array", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\begin{matrix}a&b\\\\c&d\\end{matrix}+q$$" },
  { id: "label-math-root-mixed-left-right", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\left(x+y\\right)+q$$" },
  { id: "label-math-root-double-fraction", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\frac{a}{b}+\\frac{c}{d}$$" },
  { id: "label-math-root-all-paint-kinds", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overbrace{x}^{n}+\\left(y+z\\right)+\\begin{matrix}a&b\\end{matrix}$$" },
  { id: "label-math-root-mixed-underbrace", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\underbrace{x}_{n}+q$$" },
  { id: "label-math-root-mixed-under-arrow", dpr: 1.5, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\underleftrightarrow{x}+q$$" },
  { id: "label-math-root-mixed-sum-limits", dpr: 2, theme: "dark", mathGlyph: "∑", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\sum_{i=1}^{n}+q$$' },
  { id: "label-math-root-mixed-integral-scripts", mathGlyph: "∫", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\int_0^1+q$$" },
  { id: "label-math-root-limits-fraction", dpr: 1.25, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\sum_{i=1}^{n}+\\frac{a}{b}$$" },
  { id: "label-math-root-product-limits", dpr: 1.5, mathGlyph: "∏", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\prod_{k=1}^{n}+q$$" },
  { id: "label-math-root-coproduct-limits", dpr: 2, theme: "dark", mathGlyph: "∐", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\coprod_{k=1}^{n}+q$$' },
  { id: "label-math-root-double-integral", dpr: 1.25, mathGlyph: "∬", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\iint_D+q$$" },
  { id: "label-math-root-triple-integral", mathGlyph: "∭", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\iiint_D+q$$" },
  { id: "label-math-root-cjk-fraction", dpr: 1.5, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\text{中文}+\\frac{a}{b}+q$$" },
  { id: "label-math-root-rtl-fraction", dpr: 2, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$p+\\text{سلام}+\\frac{a}{b}+q$$' },
  { id: "label-math-accent-fraction-recursive", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\underbrace{\\frac{a}{b}}_{n}$$" },
  { id: "label-math-accent-radical-recursive", dpr: 1.5, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overbrace{\\sqrt{x_i^2}}^{n}$$" },
  { id: "label-math-accent-array-recursive", dpr: 2, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\underbrace{\\begin{matrix}\\frac{a}{b}&\\sqrt{x_i^2}\\\\c&d\\end{matrix}}_{n}$$' },
  { id: "label-math-array-body-recursive", dpr: 2, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\begin{matrix}\\frac{a}{b}&\\sqrt{x_i^2}\\\\c&d\\end{matrix}$$' },
  { id: "label-math-array-cell-accent-recursive", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\begin{matrix}\\underbrace{a}_{n}&b\\\\c&d\\end{matrix}$$" },
  { id: "label-math-radical-accent-recursive", dpr: 1.5, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\sqrt{\\underbrace{x}_{n}}$$" },
  { id: "label-math-accent-accent-recursive", dpr: 2, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\overbrace{\\underbrace{x}_{i}}^{n}$$' },
  { id: "label-math-tall-assembly", verticalDelimiter: "brace", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\{\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right.$$" },
  { id: "label-math-tall-paren-assembly", verticalDelimiter: "paren", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left(\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right)$$" },
  { id: "label-math-tall-bracket-assembly", verticalDelimiter: "bracket", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left[\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right]$$" },
  { id: "label-math-tall-bar-assembly", verticalDelimiter: "bar", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left|\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right|$$" },
  { id: "label-math-nested-delimiters", verticalDelimiter: "nested", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left(\\begin{matrix}\\left[\\frac{a}{b}\\right]&c\\\\d&\\left\\{\\frac{x}{y}\\right\\}\\end{matrix}\\right)$$" },
  { id: "label-math-tall-double-bar", verticalDelimiter: "double-bar", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\|\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\|$$" },
  { id: "label-math-tall-floor", verticalDelimiter: "floor", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\lfloor\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\rfloor$$" },
  { id: "label-math-tall-ceil", verticalDelimiter: "ceil", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\lceil\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\rceil$$" },
  { id: "label-math-tall-angle", verticalDelimiter: "angle", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\langle\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\rangle$$" },
  { id: "label-math-nullable-delimiter", verticalDelimiter: "nullable", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left.\\begin{matrix}a\\\\b\\\\c\\\\d\\end{matrix}\\right\\rceil$$" },
  { id: "label-math-delimiter-recursive", verticalDelimiter: "recursive", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\langle\\frac{\\sqrt{x_i^2}}{\\overbrace{a+b}^{n}}\\right\\rangle$$" },
  { id: "label-math-middle-delimiter", verticalDelimiter: "middle", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\langle x\\middle|y\\right\\rangle$$" },
  { id: "label-math-multiple-middle-delimiter", verticalDelimiter: "multiple-middle", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left(x\\middle|y\\middle\\|z\\right)$$" },
  { id: "label-math-nested-plain-delimiter", verticalDelimiter: "nested-plain", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left[x+\\left(y-z\\right)\\right]$$" },
  { id: "label-math-middle-fraction", verticalDelimiter: "middle-fraction", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left(\\frac{a\\middle|b}{c}\\right)$$" },
  { id: "label-math-middle-radical", verticalDelimiter: "middle-radical", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left(\\sqrt{x\\middle|y}\\right)$$" },
  { id: "label-math-middle-script", verticalDelimiter: "middle-script", theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\left(x^{a\\middle|b}\\right)$$' },
  { id: "label-math-middle-array", verticalDelimiter: "middle-array", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left(\\begin{matrix}a\\middle|b&c\\\\d&e\\end{matrix}\\right)$$" },
  { id: "label-math-matrix-basic", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\begin{matrix}a&b\\\\c&d\\end{matrix}$$" },
  { id: "label-math-matrix-recursive", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\begin{matrix}\\frac{a}{b}&x^2\\\\\\sqrt{y}&z_i\\end{matrix}$$" },
  { id: "label-math-cases", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\begin{cases}x^2&x>0\\\\-x&x\\le0\\end{cases}$$" },
  { id: "label-math-bidi-isolates", dpr: 1.25, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\text{abc ⁧שלום 123⁩ ⁦中文⁩}$$" },
  { id: "label-math-fallback-fraction", dpr: 1.25, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\frac{\\text{中文}}{\\text{سلام}}$$" },
  { id: "label-math-fallback-radical", dpr: 1.5, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\sqrt{\\text{שלום}+x}$$" },
  { id: "label-math-fallback-supsub", dpr: 2, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$x_{\\text{中文}}^{\\text{سلام}}$$' },
  { id: "label-math-fallback-accent", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overbrace{\\text{中文}}^{\\text{שלום}}$$" },
  { id: "label-math-fallback-array", dpr: 1.5, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\begin{matrix}\\text{中文}&\\text{سلام}\\\\\\text{שלום}&x\\end{matrix}$$' },
  { id: "label-math-fallback-limits", dpr: 1.25, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\sum_{\\text{\u4e2d\u6587}}^{\\text{\u0633\u0644\u0627\u0645}}$$" },
  { id: "label-math-fallback-limits-recursive", dpr: 1.5, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\sum_{\\frac{\\text{\u4e2d\u6587}}{x}}^{\\sqrt{\\text{\u05e9\u05dc\u05d5\u05dd}}}$$' },
  { id: "label-math-fallback-under-accent", dpr: 2, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\underbrace{\\text{\u0633\u0644\u0627\u0645}}_{\\text{\u4e2d\u6587}}$$" },
  { id: "label-math-fallback-delimiter-assembly", dpr: 1.5, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\left(\\begin{matrix}\\text{\u4e2d\u6587}\\\\\\text{\u0633\u0644\u0627\u0645}\\\\\\text{\u05e9\u05dc\u05d5\u05dd}\\\\x\\end{matrix}\\right)$$' },
  { id: "label-math-fallback-product-limits", dpr: 1.25, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\prod_{\\text{\u4e2d\u6587}}^{\\text{\u0633\u0644\u0627\u0645}}$$" },
  { id: "label-math-fallback-coproduct-limits", dpr: 1.5, theme: "dark", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\coprod_{\\text{\u05e9\u05dc\u05d5\u05dd}}^{\\text{\u4e2d\u6587}}$$' },
  { id: "label-math-fallback-over-arrow", dpr: 2, mathAccent: "over", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overleftrightarrow{\\text{\u0633\u0644\u0627\u0645 \u4e2d\u6587}}$$" },
  { id: "label-math-fallback-under-arrow", dpr: 1.25, theme: "dark", mathAccent: "under", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\underleftrightarrow{\\text{\u05e9\u05dc\u05d5\u05dd \u4e2d\u6587}}$$' },
  { id: "label-math-arrow-left-dpr-100", mathAccent: "over", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overleftarrow{\\text{\u0633\u0644\u0627\u0645 \u4e2d\u6587}}$$" },
  { id: "label-math-arrow-right-dpr-125", dpr: 1.25, theme: "dark", mathAccent: "over", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\overrightarrow{\\text{\u05e9\u05dc\u05d5\u05dd \u4e2d\u6587}}$$' },
  { id: "label-math-arrow-double-dpr-150", dpr: 1.5, mathAccent: "over", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\Overrightarrow{\\text{\u4e2d\u6587 \u0633\u0644\u0627\u0645}}$$" },
  { id: "label-math-arrow-under-dpr-200", dpr: 2, theme: "dark", mathAccent: "under", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\underleftrightarrow{\\text{\u0633\u0644\u0627\u0645 \u4e2d\u6587}}$$' },
  { id: "label-math-fallback-brace-assembly", dpr: 1.5, verticalDelimiter: "brace", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\{\\begin{matrix}\\text{\u4e2d\u6587}\\\\\\text{\u0633\u0644\u0627\u0645}\\\\\\text{\u05e9\u05dc\u05d5\u05dd}\\\\x\\end{matrix}\\right\\}$$" },
  { id: "label-math-fallback-bracket-assembly", dpr: 2, theme: "dark", verticalDelimiter: "bracket", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\left[\\begin{matrix}\\text{\u4e2d\u6587}\\\\\\text{\u0633\u0644\u0627\u0645}\\\\\\text{\u05e9\u05dc\u05d5\u05dd}\\\\x\\end{matrix}\\right]$$' },
  { id: "label-math-fallback-angle-assembly", verticalDelimiter: "angle", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\left\\langle\\begin{matrix}\\text{\u4e2d\u6587}\\\\\\text{\u0633\u0644\u0627\u0645}\\\\\\text{\u05e9\u05dc\u05d5\u05dd}\\\\x\\end{matrix}\\right\\rangle$$" },
  { id: "structural-aria", source: "sequenceDiagram\naccTitle: Checkout sequence\naccDescr: Client request lifecycle\nA->>B:go" },
  { id: "structural-combined-order", source: [
      '%%{init: {"sequence": {"mirrorActors": false, "hideUnusedParticipants": true}}}%%',
      "sequenceDiagram", "autonumber", "participant UNUSED", "box rgb(238, 246, 255) Services",
      "participant A", "participant B", "end", "A->>+B:call", "alt branch",
      "Note over A,B:note", "B-->>-A:return", "end", "create participant C", "A->>C:create",
      "destroy C", "C-xA:destroy"].join("\n") },
  { id: "label-math-accent-hat", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\hat{x}$$" },
  { id: "label-math-accent-vector", dpr: 1.25, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\vec{x+y}$$" },
  { id: "label-math-accent-overline", dpr: 1.5, cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$\\overline{x+y}$$" },
  { id: "label-math-accent-underline", dpr: 2, theme: "dark",
    cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: '%%{init: {"theme": "dark"}}%%\nsequenceDiagram\nA->>B:start\nNote over A,B:$$\\underline{x+y}$$' },
  { id: "label-math-relation-overlay", cropSelector: '[data-et="note"] foreignObject', cropKind: "note",
    source: "sequenceDiagram\nA->>B:start\nNote over A,B:$$x\\le y\\ne z$$" },
];
const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")));
const browser = await puppeteer.launch({headless:true, executablePath:chrome, args:["--allow-file-access-from-files"]});
fs.mkdirSync(outDir, {recursive:true});
try {
  const page = await browser.newPage();
  await page.setViewport({width:2200,height:1600,deviceScaleFactor:1});
  const harness = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const module = pathToFileURL(path.join(mermaidRoot,"dist","mermaid.esm.mjs")).href;
  const manifestCases = [];
  for (let i=0;i<cases.length;++i) {
    await page.setViewport({width:2200,height:1600,deviceScaleFactor:cases[i].dpr ?? 1});
    await page.goto(harness);
    const dimensions = await page.evaluate(async ({fixture,i,module,faces,stack,mathFace}) => {
      const style=document.createElement("style"); style.textContent=`${faces}\n${mathFace}\nmath{font-family:"STIX Two Math",${stack} !important}.katex-html .text,.katex-html .text *{font-family:${stack} !important}`; document.head.appendChild(style);
      await Promise.all([
        document.fonts.load('16px "Noto Sans"', 'Latin'),
        document.fonts.load('16px "Noto Sans CJK SC"', '中文'),
        document.fonts.load('16px "Noto Sans Arabic"', 'سلام'),
        document.fonts.load('16px "Noto Sans Hebrew"', 'שלום'),
        document.fonts.load('16px "STIX Two Math"',
          'x+\u2211\u221a\u220f\u2210\u2190\u2192\u2194\u21d2\u23de\u23df()[]{}\u27e8\u27e9'),
      ]); await document.fonts.ready;
      const {default:mermaid}=await import(module);
      mermaid.initialize({startOnLoad:false,securityLevel:"strict",theme:fixture.theme ?? "default",fontFamily:stack,sequence:{useMaxWidth:false}});
      const {svg}=await mermaid.render(`sequence-pixel-${i}`,fixture.source);
      document.getElementById("container").innerHTML=svg;
      await document.fonts.ready;
      await new Promise((resolve)=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
      const root=document.querySelector("svg"); const b=root.getBBox();
      root.setAttribute("viewBox",`${b.x} ${b.y} ${b.width} ${b.height}`);
      root.setAttribute("width",Math.ceil(b.width)); root.setAttribute("height",Math.ceil(b.height));
      root.style.maxWidth="none"; root.style.width=`${Math.ceil(b.width)}px`; root.style.height=`${Math.ceil(b.height)}px`;
      document.body.style.margin="0"; document.documentElement.style.margin="0";
      const selected=fixture.cropSelector ? root.querySelector(fixture.cropSelector) : null;
      const styleOf=(selector)=>{
        const node=root.querySelector(selector); if(!node) return {};
        const computed=getComputedStyle(node);
        return {fill:computed.fill,stroke:computed.stroke,color:computed.color};
      };
      const structure={
        viewBox:root.getAttribute("viewBox"), role:root.getAttribute("role") ?? "",
        ariaRoleDescription:root.getAttribute("aria-roledescription") ?? "",
        ariaLabelledBy:root.getAttribute("aria-labelledby") ?? "",
        text:root.querySelectorAll("text").length, tspan:root.querySelectorAll("tspan").length,
        foreignObject:root.querySelectorAll("foreignObject").length,
        math:root.querySelectorAll("math").length, clipPath:root.querySelectorAll("clipPath").length,
        defs:root.querySelectorAll("defs").length,
        markers:[...root.querySelectorAll("marker")].map((marker)=>({
          id:marker.id,class:marker.getAttribute("class") ?? "",viewBox:marker.getAttribute("viewBox") ?? "",
          refX:marker.getAttribute("refX") ?? "",refY:marker.getAttribute("refY") ?? "",
          markerWidth:marker.getAttribute("markerWidth") ?? "",markerHeight:marker.getAttribute("markerHeight") ?? "",
          markerUnits:marker.getAttribute("markerUnits") ?? "",orient:marker.getAttribute("orient") ?? "",
          childTag:marker.firstElementChild?.tagName ?? "",
          childClass:marker.firstElementChild?.getAttribute("class") ?? "",
          childPath:marker.firstElementChild?.getAttribute("d") ?? ""})),
        classSet:[...new Set([...root.querySelectorAll("[class]")]
          .flatMap((node)=>[...node.classList]))].sort(),
        ariaTitle:root.querySelector("title")?.textContent ?? "",
        ariaDescription:root.querySelector("desc")?.textContent ?? "",
        labelTag:selected?.tagName ?? "", labelClass:selected?.getAttribute("class") ?? "",
        labelParentTag:selected?.parentElement?.tagName ?? "",
        labelAttributes:selected ? Object.fromEntries([...selected.attributes].map((attribute)=>
          [attribute.name,attribute.value])) : {},
        labelParentAttributes:selected?.parentElement
          ? Object.fromEntries([...selected.parentElement.attributes].map((attribute)=>
              [attribute.name,attribute.value])) : {},
        textNodes:[...root.querySelectorAll("text,foreignObject")].map((node)=>({
          tag:node.tagName,class:node.getAttribute("class") ?? "",text:node.textContent ?? ""})),
        mathMlBox:(()=>{ const node=root.querySelector("math"); if(!node) return {};
          const rect=node.getBoundingClientRect(); const computed=getComputedStyle(node);
          return {x:Number(rect.x.toFixed(3)),y:Number(rect.y.toFixed(3)),
            width:Number(rect.width.toFixed(3)),height:Number(rect.height.toFixed(3)),
            fontFamily:computed.fontFamily,fontSize:computed.fontSize,
            lineHeight:computed.lineHeight,direction:computed.direction}; })(),
        mathMlRuns:[...root.querySelectorAll("math mi,math mo,math mtext,math mfrac")]
          .map((node)=>{ const rect=node.getBoundingClientRect(); return {
            tag:node.tagName,text:node.textContent ?? "",
            x:Number(rect.x.toFixed(3)),y:Number(rect.y.toFixed(3)),
            width:Number(rect.width.toFixed(3)),height:Number(rect.height.toFixed(3))}; }),
        styles:{actor:styleOf(".actor"),message:styleOf(".messageLine0,.messageLine1"),
          messageText:styleOf(".messageText"),note:styleOf(".note"),loopText:styleOf(".loopText")},
        domOrder:[...root.querySelectorAll("text,foreignObject,rect,line,path,math")].slice(0,160)
          .map((node)=>`${node.tagName}:${node.getAttribute("class") ?? ""}`),
      };
      return {width:Math.ceil(b.width),height:Math.ceil(b.height),structure};
    }, {fixture:cases[i],i,module,faces,stack,mathFace});
    const file=`${cases[i].id}.png`;
    const svg = await page.$("svg");
    await svg.screenshot({path:path.join(outDir,file),omitBackground:true});
    let mathBodyFile,mathAccentFile,mathGlyphFile,mathGlyphBox;
    const captureMathComponent=async(selector,suffix)=>{
      const clip=await page.$eval(selector,(node)=>{
        const rect=node.closest("math").getBoundingClientRect();
        return {x:rect.left,y:rect.top,width:Math.max(1,rect.width),
          height:Math.max(1,rect.height)};
      });
      await page.evaluate((selector)=>{
        const root=document.querySelector("svg");
        for(const node of root.querySelectorAll("*")) node.style.visibility="hidden";
        const selected=root.querySelector(selector);
        for(let node=selected;node&&node!==root;node=node.parentElement)
          node.style.visibility="visible";
        for(const node of selected.querySelectorAll("*")) node.style.visibility="visible";
      },selector);
      const componentFile=`${cases[i].id}-${suffix}.png`;
      await page.screenshot({path:path.join(outDir,componentFile),
        omitBackground:true,clip});
      await page.evaluate(()=>{
        const root=document.querySelector("svg");
        for(const node of root.querySelectorAll("*"))
          node.style.removeProperty("visibility");
      });
      return componentFile;
    };
    if(cases[i].mathAccent) {
      mathBodyFile=await captureMathComponent("math mtext","math-body");
      mathAccentFile=await captureMathComponent("math mo","math-accent");
    }
    if(cases[i].mathGlyph) {
      mathGlyphBox=await page.$$eval("math mi,math mo",(nodes,text)=>{
        const node=nodes.find((candidate)=>(candidate.textContent ?? "")===text);
        if(!node) return null;
        node.setAttribute("data-muffin-glyph-oracle","");
        const rect=node.getBoundingClientRect();
        return {x:Number(rect.x.toFixed(3)),y:Number(rect.y.toFixed(3)),
          width:Number(rect.width.toFixed(3)),height:Number(rect.height.toFixed(3))};
      },cases[i].mathGlyph);
      if(!mathGlyphBox)
        throw new Error(`${cases[i].id}: Math glyph ${cases[i].mathGlyph} was not found`);
      const selector="[data-muffin-glyph-oracle]";
      const clip=await page.$eval(selector,(node)=>{
        const rect=node.getBoundingClientRect();
        return {x:rect.left,y:rect.top,width:Math.max(1,rect.width),
          height:Math.max(1,rect.height)};
      });
      await page.evaluate((selector)=>{
        const root=document.querySelector("svg");
        for(const node of root.querySelectorAll("*")) node.style.visibility="hidden";
        const selected=root.querySelector(selector);
        for(let node=selected;node&&node!==root;node=node.parentElement)
          node.style.visibility="visible";
        for(const node of selected.querySelectorAll("*")) node.style.visibility="visible";
      },selector);
      mathGlyphFile=`${cases[i].id}-math-glyph.png`;
      await page.screenshot({path:path.join(outDir,mathGlyphFile),
        omitBackground:true,clip});
      await page.evaluate((selector)=>{
        const root=document.querySelector("svg");
        for(const node of root.querySelectorAll("*"))
          node.style.removeProperty("visibility");
        root.querySelector(selector)?.removeAttribute("data-muffin-glyph-oracle");
      },selector);
    }
    let cropFile;
    if (cases[i].cropSelector) {
      cropFile=`${cases[i].id}-label.png`;
      const clip=await page.$$eval(cases[i].cropSelector,(elements,cropCount)=>{
        if(!elements.length) return null;
        if(cropCount>0) elements=elements.slice(0,cropCount);
        const rects=elements.map((element)=>element.getBoundingClientRect());
        const left=Math.min(...rects.map((rect)=>rect.left));
        const top=Math.min(...rects.map((rect)=>rect.top));
        const right=Math.max(...rects.map((rect)=>rect.right));
        const bottom=Math.max(...rects.map((rect)=>rect.bottom));
        return {x:left,y:top,width:Math.max(1,right-left),height:Math.max(1,bottom-top)};
      },cases[i].cropCount ?? (cases[i].cropFirst ? 1 : 0));
      if (!clip) throw new Error(`${cases[i].id}: crop selector resolved to nothing`);
      await page.$$eval(cases[i].cropSelector,(elements,cropCount)=>{
        const selected=cropCount>0 ? elements.slice(0,cropCount) : elements;
        const root=document.querySelector("svg");
        for(const node of root.querySelectorAll("*")) node.style.visibility="hidden";
        for(const node of selected) {
          node.style.visibility="visible";
          for(const child of node.querySelectorAll("*")) child.style.visibility="visible";
        }
      },cases[i].cropCount ?? (cases[i].cropFirst ? 1 : 0));
      await page.evaluate(()=>new Promise((resolve)=>
        requestAnimationFrame(()=>requestAnimationFrame(resolve))));
      await page.screenshot({path:path.join(outDir,cropFile),omitBackground:true,clip});
    }
    const sha256=createHash("sha256").update(fs.readFileSync(path.join(outDir,file))).digest("hex");
    const cropSha256=cropFile
      ? createHash("sha256").update(fs.readFileSync(path.join(outDir,cropFile))).digest("hex") : undefined;
    const mathBodySha256=mathBodyFile
      ? createHash("sha256").update(fs.readFileSync(path.join(outDir,mathBodyFile))).digest("hex") : undefined;
    const mathAccentSha256=mathAccentFile
      ? createHash("sha256").update(fs.readFileSync(path.join(outDir,mathAccentFile))).digest("hex") : undefined;
    const mathGlyphSha256=mathGlyphFile
      ? createHash("sha256").update(fs.readFileSync(path.join(outDir,mathGlyphFile))).digest("hex") : undefined;
    manifestCases.push({...cases[i],file,sha256,cropFile,cropSha256,
      mathBodyFile,mathBodySha256,mathAccentFile,mathAccentSha256,
      mathGlyphFile,mathGlyphSha256,mathGlyphBox,...dimensions});
  }
  const payload={mermaidVersion:pkg.version,fontMode:"bundled-noto-stix-two-math-2.13b171",cases:manifestCases};
  payload.fixtureSha256=createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.writeFileSync(path.join(outDir,"manifest.json"),`${JSON.stringify(payload,null,2)}\n`);
  console.log(`Wrote ${cases.length} sequence pixel goldens to ${outDir}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally { await browser.close(); }
