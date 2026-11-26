// Shared capture for the geometry generators of diagram families that mermaid
// lays out with dagre with the origin at the first node's centre: class,
// state and sequence-like documents.
//
// A generator supplies its cases and a `describe` function, and receives the
// rendered document with the helpers a capture needs, everything measured in
// the bundled Noto fonts (what the port measures text in) and at scale 1.
//
//   const results = await captureGeometry({
//     mermaidRoot, chrome, prefix, cases, describe, configure?, auxiliaryExport?, skipFailed?
//   });
//
// `describe(root, ctx)` returns a case's `expected` object. Its result is
// paired with the case's `id` and `source` (and `config`, when it has one).
// `ctx.auxiliaryModule` is the URL of the bundle module that exports
// `auxiliaryExport`.

import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { bundledFontArguments } from "./mermaid_bundled_fonts.mjs";

export function resolveReference(mermaidRootArgument) {
  return path.resolve(
    process.env.MERMAID_REFERENCE_ROOT ||
      mermaidRootArgument ||
      path.join("..", "mermaid-cli", "node_modules", "mermaid"),
  );
}

// A generator's optional command-line arguments: the reference package, the
// output file and the browser; each falls back to its default when left out.
export function generatorArguments(defaultOutput) {
  const argv = process.argv.slice(2).map((argument) => argument || undefined);
  return {
    mermaidRootArgument: argv[0],
    output: path.resolve(argv[1] ?? defaultOutput),
    chromeArgument: argv[2],
  };
}

// Mermaid sizes the SVG with a `max-width` it rounds to thousandths of a
// pixel, short of the viewBox's own width, and Chrome snaps a CSS length to
// 1/64 px: the diagram ends up scaled by a hair, and a text laid out at any
// scale but exactly 1 has other boxes. Grow the viewBox to whole pixels from
// its own origin, so the SVG can be laid out at exactly scale 1; keep what the
// stylesheet said.
function svgAtScaleOne(root) {
  const stylesheetWidth = root.getAttribute("width");
  const stylesheetHeight = root.getAttribute("height");
  const stylesheetMaxWidth = getComputedStyle(root).maxWidth;
  const [viewX, viewY, viewWidth, viewHeight] = (root.getAttribute("viewBox") ?? "").trim().split(/\s+/).map(Number);
  root.style.maxWidth = "none";
  const wholeWidth = Math.ceil(viewWidth), wholeHeight = Math.ceil(viewHeight);
  root.setAttribute("viewBox", `${viewX} ${viewY} ${wholeWidth} ${wholeHeight}`);
  root.setAttribute("width", String(wholeWidth));
  root.setAttribute("height", String(wholeHeight));
  return {
    // The canvas upstream sizes the SVG to: its viewBox, the width and height
    // attributes it writes, and the CSS max-width it sets.
    viewBox: [viewX, viewY, viewWidth, viewHeight],
    width: stylesheetWidth,
    height: stylesheetHeight,
    maxWidth: stylesheetMaxWidth,
  };
}

// The helpers a capture reads the document with.
function helpers(rootInverse) {
  const number = (v) => Math.round(v * 1000) / 1000;
  const elementMatrix = (element) => rootInverse.multiply(element.getScreenCTM());
  // A shape's bounding box, in root user space.
  const absoluteBox = (element) => {
    if (!element) return null;
    const box = element.getBBox();
    const matrix = elementMatrix(element);
    const corners = [
      new DOMPoint(box.x, box.y),
      new DOMPoint(box.x + box.width, box.y),
      new DOMPoint(box.x, box.y + box.height),
      new DOMPoint(box.x + box.width, box.y + box.height),
    ].map((p) => p.matrixTransform(matrix));
    const left = Math.min(...corners.map((p) => p.x));
    const right = Math.max(...corners.map((p) => p.x));
    const top = Math.min(...corners.map((p) => p.y));
    const bottom = Math.max(...corners.map((p) => p.y));
    return { x: left, y: top, width: right - left, height: bottom - top };
  };
  // Where an element's own origin lies, in root user space.
  const absoluteOrigin = (element) => new DOMPoint(0, 0).matrixTransform(elementMatrix(element));
  // An attribute of an element as a point, in root user space.
  const absolutePoint = (element, xName, yName) =>
    new DOMPoint(Number(element.getAttribute(xName)), Number(element.getAttribute(yName))).matrixTransform(
      elementMatrix(element),
    );
  // The translate of an element's transform attribute, if it has one.
  const translate = (element) => {
    const m = /translate\(([^,)]+)(?:[ ,]\s*([^)]+))?\)/.exec(element?.getAttribute("transform") ?? "");
    return m ? [Number(m[1]), m[2] === undefined ? 0 : Number(m[2])] : [0, 0];
  };
  const rect = (x, y, width, height) => ({
    x: number(x),
    y: number(y),
    width: number(width),
    height: number(height),
  });
  // The numbers of a path's data, each a point in root user space.
  const pathPoints = (pathData, element) => {
    const tokens = pathData.match(/-?\d+(?:\.\d+)?(?:e[-+]?\d+)?/gi) ?? [];
    const points = [];
    const matrix = elementMatrix(element);
    for (let index = 0; index + 1 < tokens.length; index += 2)
      points.push(new DOMPoint(Number(tokens[index]), Number(tokens[index + 1])).matrixTransform(matrix));
    return points;
  };
  // A path's data about the origin: every second number is an x and the one
  // after it its y, whatever the commands between them.
  const relativePath = (pathData, element, originX, originY) => {
    const numberPattern = /-?\d+(?:\.\d+)?(?:e[-+]?\d+)?/gi;
    const points = pathPoints(pathData, element);
    let pair = 0;
    let seen = 0;
    return pathData.replace(numberPattern, () => {
      const rest = seen++ % 2;
      const value = rest === 0 ? points[pair].x - originX : points[pair++].y - originY;
      return String(number(value));
    });
  };
  // A label's box: that of its <foreignObject>, else of the background
  // rectangle an SVG label is laid out by, else of its <text>.
  const labelBox = (label) => {
    if (!label) return null;
    const foreign = label.querySelector("foreignObject");
    if (foreign && Number(foreign.getAttribute("width")) !== 0)
      return rect(0, 0, Number(foreign.getAttribute("width")), Number(foreign.getAttribute("height")));
    const background = label.querySelector("rect.background[width]");
    if (background)
      return rect(
        Number(background.getAttribute("x")),
        Number(background.getAttribute("y")),
        Number(background.getAttribute("width")),
        Number(background.getAttribute("height")),
      );
    const textElement = label.querySelector("text");
    if (!textElement) return null;
    const box = textElement.getBBox();
    const [tx, ty] = translate(textElement);
    return box.width === 0 && box.height === 0
      ? null
      : rect(tx + box.x, ty + box.y, box.width, box.height);
  };
  const text = (label) => (label?.textContent ?? "").trim();
  // rgb(r, g, b) as mermaid's stylesheet variables are written.
  const rgb = (value) => {
    const m = /^rgba?\(([^)]+)\)$/.exec(value ?? "");
    return m ? `rgb(${m[1].trim()})` : value;
  };
  // What Chrome paints of the elements: their fill and the widest stroke.
  const paint = (...elements) => {
    elements = elements.filter(Boolean);
    const styleOf = (element) => getComputedStyle(element);
    const fill = elements.find((element) => styleOf(element).fill !== "none");
    const stroking = elements
      .filter((element) => styleOf(element).stroke !== "none")
      .sort((a, b) => Number.parseFloat(styleOf(b).strokeWidth) - Number.parseFloat(styleOf(a).strokeWidth))[0];
    const computed = stroking ? styleOf(stroking) : null;
    return {
      fill: fill ? rgb(styleOf(fill).fill) : null,
      stroke: computed ? rgb(computed.stroke) : null,
      strokeWidth: computed ? Number.parseFloat(computed.strokeWidth) : null,
      strokeDasharray: computed ? computed.strokeDasharray : null,
    };
  };
  return { number, elementMatrix, absoluteBox, absoluteOrigin, absolutePoint, translate, rect, pathPoints, relativePath, labelBox, text, rgb, paint };
}

export async function captureGeometry({
  mermaidRootArgument,
  outputArgument,
  chromeArgument,
  family,
  cases,
  describe,
  configure,
  auxiliaryExport,
  skipFailed,
}) {
  const mermaidRoot = resolveReference(mermaidRootArgument);
  const chrome = chromeArgument ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
  const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
  if (packageJson.version !== "11.16.0")
    throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
  const { default: puppeteer } = await import(
    pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")),
  );
  const { fontFaces, fontFamily } = bundledFontArguments;
  // The bundle mermaid.esm.mjs loads its internals from separate modules. A
  // capture that needs one of their tables (say, the groups a renderer keeps
  // by id) imports that module by the bundle's own URL, which is the instance
  // mermaid itself filled.
  let auxiliaryModule;
  if (auxiliaryExport) {
    const directory = path.join(mermaidRoot, "dist", "chunks", "mermaid.esm");
    const exported = new RegExp(`export\\s*\\{[^}]*\\b${auxiliaryExport}\\b[^}]*\\}`);
    const chunk = fs.readdirSync(directory).find((file) => {
      if (!file.endsWith(".mjs")) return false;
      return exported.test(fs.readFileSync(path.join(directory, file), "utf8"));
    });
    if (!chunk) throw new Error(`No module of the bundle exports ${auxiliaryExport}`);
    auxiliaryModule = pathToFileURL(path.join(directory, chunk)).href;
  }

  const browser = await puppeteer.launch({
    headless: true,
    executablePath: chrome,
    args: ["--allow-file-access-from-files"],
  });
  try {
    const page = await browser.newPage();
    // An SVG is as wide as `max-width` lets it, which a narrow page cuts down
    // and scales: a text laid out at another scale has other boxes. A page
    // wider than any diagram keeps the scale at 1.
    await page.setViewport({ width: 3000, height: 2200 });
    await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
    const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
    // Nothing of this module reaches the page: the function evaluated there is
    // built from source, the helpers' source included.
    const inPage = new Function(`
      return (async ({ cases, mermaidModule, fontFaces, fontFamily, family, configureSource, describeSource, auxiliaryModule, skipFailed }) => {
        ${svgAtScaleOne}
        ${helpers}
        ${renderCases}
        return renderCases({ cases, mermaidModule, fontFaces, fontFamily, family, configureSource, describeSource, auxiliaryModule, skipFailed });
      });
    `)();
    const results = await page.evaluate(inPage, {
      family,
      cases,
      mermaidModule,
      fontFaces,
      fontFamily,
      configureSource: `${configure ?? ((mermaid, defaults) => mermaid.initialize(defaults))}`,
      describeSource: `${describe}`,
      auxiliaryModule,
      skipFailed: Boolean(skipFailed),
    });
    return { mermaidRoot, output: outputArgument, results };
  } finally {
    await browser.close();
  }
}

// Runs in the page: registers the faces, renders each case and describes it.
async function renderCases({ cases, mermaidModule, fontFaces, fontFamily, family, configureSource, describeSource, auxiliaryModule, skipFailed }) {
  const style = document.createElement("style");
  style.textContent = fontFaces;
  document.head.appendChild(style);
  // Chrome loads a face when a text first needs it, and measures that text in
  // the fallback font until then: load each face first.
  for (const [familyName, sample] of [
    ["Noto Sans", "CUSTOMER"],
    ["Noto Sans CJK SC", "编号"],
    ["Noto Sans Arabic", "مرحبا"],
    ["Noto Sans Hebrew", "שלום"],
  ])
    await document.fonts.load(`16px "${familyName}"`, sample);
  await document.fonts.ready;
  const { default: mermaid } = await import(mermaidModule);
  const configure = new Function(`return (${configureSource});`)();
  const describe = new Function(`return (${describeSource});`)();
  const out = [];
  for (let index = 0; index < cases.length; ++index) {
    const fixture = cases[index];
    configure(
      mermaid,
      { startOnLoad: false, securityLevel: "strict", theme: "default", fontFamily, themeVariables: { fontFamily } },
      fixture,
    );
    // A fuzz corpus has inputs mermaid refuses: with `skipFailed` such a
    // case comes back with its error instead of stopping the capture.
    let svg;
    try {
      ({ svg } = await mermaid.render(`${family}-geometry-${index}`, fixture.source));
    } catch (error) {
      if (!skipFailed)
        throw error;
      out.push({ id: fixture.id, source: fixture.source, error: String(error?.message ?? error).split("\n", 1)[0] });
      continue;
    }
    document.getElementById("container").innerHTML = svg;
    const root = document.querySelector("svg");
    const canvas = svgAtScaleOne(root);
    const rootInverse = root.getScreenCTM()?.inverse();
    const expected = await describe(root, helpers(rootInverse), canvas, { mermaid, fixture, auxiliaryModule });
    out.push({
      id: fixture.id,
      source: fixture.source,
      ...(fixture.config ? { config: fixture.config } : {}),
      expected,
    });
  }
  return out;
}

// The root object a geometry fixture is written from.
export function geometryFixture(oracle, notes, cases) {
  return {
    upstream: { package: "mermaid", version: "11.16.0", notes },
    mermaidVersion: "11.16.0",
    fontMode: "bundled-noto-2.13b171",
    oracle,
    cases,
  };
}
