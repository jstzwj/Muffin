import fs from "node:fs/promises";
import path from "node:path";
import os from "node:os";
import {createRequire} from "node:module";
import {fileURLToPath, pathToFileURL} from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "..");
const fixturePath = process.argv[2] ? path.resolve(process.argv[2]) : path.join(repoRoot, "tests", "fixtures", "math", "core.json");
const outputDir = process.argv[3] ? path.resolve(process.argv[3]) : path.join(repoRoot, "tests", "golden", "katex");
const vendorKatexRoot = path.join(repoRoot, "tests", "vendor", "katex");
const katexRoot = process.env.KATEX_ROOT || "D:\\github\\KaTeX";

const fixtures = JSON.parse(await fs.readFile(fixturePath, "utf8"));
await fs.mkdir(outputDir, {recursive: true});

const exists = async file => {
  try {
    await fs.access(file);
    return true;
  } catch {
    return false;
  }
};

const round = value => {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return undefined;
  }
  return Number(value.toFixed(6));
};

const scanInkBBox = async buffer => {
  const {PNG} = await import("pngjs");
  const png = PNG.sync.read(buffer);
  let left = png.width;
  let top = png.height;
  let right = -1;
  let bottom = -1;
  for (let y = 0; y < png.height; ++y) {
    for (let x = 0; x < png.width; ++x) {
      const i = (png.width * y + x) << 2;
      const alpha = png.data[i + 3];
      const r = png.data[i];
      const g = png.data[i + 1];
      const b = png.data[i + 2];
      if (alpha === 0 || (r > 248 && g > 248 && b > 248)) {
        continue;
      }
      left = Math.min(left, x);
      top = Math.min(top, y);
      right = Math.max(right, x);
      bottom = Math.max(bottom, y);
    }
  }
  if (right < left || bottom < top) {
    return null;
  }
  return {
    x: left,
    y: top,
    width: right - left + 1,
    height: bottom - top + 1
  };
};

const parseEm = value => {
  if (typeof value !== "string" || !value.endsWith("em")) {
    return undefined;
  }
  const number = Number(value.slice(0, -2));
  return Number.isFinite(number) ? round(number) : undefined;
};

const fontMimeType = file => {
  const ext = path.extname(file).toLowerCase();
  if (ext === ".woff2") {
    return "font/woff2";
  }
  if (ext === ".woff") {
    return "font/woff";
  }
  if (ext === ".ttf") {
    return "font/ttf";
  }
  return "application/octet-stream";
};

const katexCssWithEmbeddedFonts = async cssPath => {
  const css = await fs.readFile(cssPath, "utf8");
  const fontUrlPattern = /url\((['"]?)(fonts\/[^)'"]+)\1\)/g;
  let result = "";
  let lastIndex = 0;
  for (const match of css.matchAll(fontUrlPattern)) {
    const url = match[2];
    const fontPath = path.join(path.dirname(cssPath), ...url.split("/"));
    const font = await fs.readFile(fontPath);
    result += css.slice(lastIndex, match.index);
    result += `url(data:${fontMimeType(fontPath)};base64,${font.toString("base64")})`;
    lastIndex = match.index + match[0].length;
  }
  result += css.slice(lastIndex);
  return result;
};

const serializeStyle = style => {
  const result = {};
  if (!style) {
    return result;
  }
  for (const key of Object.keys(style).sort()) {
    const value = style[key];
    if (value !== undefined) {
      result[key] = value;
    }
  }
  return result;
};

const serializeKatexBuilderNode = node => {
  if (!node) {
    return null;
  }
  const style = serializeStyle(node.style);
  const result = {
    kind: node.constructor?.name || "Node"
  };
  if (Array.isArray(node.classes) && node.classes.length > 0) {
    result.classes = [...node.classes];
  }
  if (node.attributes && Object.keys(node.attributes).length > 0) {
    result.attributes = {...node.attributes};
  }
  if (Object.keys(style).length > 0) {
    result.style = style;
  }
  for (const key of ["text", "pathName", "alternate", "src", "alt"]) {
    if (typeof node[key] === "string" && node[key].length > 0) {
      result[key] = node[key];
    }
  }
  for (const key of ["width", "height", "depth", "maxFontSize", "italic", "skew"]) {
    const value = round(node[key]);
    if (value !== undefined) {
      result[key] = value;
    }
  }
  const top = parseEm(style.top);
  if (top !== undefined) {
    const pstrutSize = Array.isArray(node.children) &&
        node.children.length > 0 &&
        Array.isArray(node.children[0].classes) &&
        node.children[0].classes.includes("pstrut")
      ? parseEm(node.children[0].style?.height)
      : undefined;
    result.top = top;
    result.shift = pstrutSize === undefined ? top : round(top + pstrutSize);
  }
  const marginLeft = parseEm(style.marginLeft);
  if (marginLeft !== undefined) {
    result.marginLeft = marginLeft;
  }
  const marginRight = parseEm(style.marginRight);
  if (marginRight !== undefined) {
    result.marginRight = marginRight;
  }
  if (Array.isArray(node.children) && node.children.length > 0) {
    result.children = node.children.map(serializeKatexBuilderNode).filter(Boolean);
  }
  return result;
};

const flattenMetrics = (node, pathParts = []) => {
  if (!node) {
    return [];
  }
  const pathName = pathParts.join("/");
  const metric = {
    path: pathName,
    kind: node.kind,
    classes: node.classes || [],
    width: node.width,
    height: node.height,
    depth: node.depth,
    shift: node.shift
  };
  const children = Array.isArray(node.children) ? node.children : [];
  return [
    metric,
    ...children.flatMap((child, index) => flattenMetrics(child, [...pathParts, String(index)]))
  ];
};

const loadKatexDist = async distRoot => {
  const katexMjs = path.join(distRoot, "katex.mjs");
  if (!(await exists(katexMjs))) {
    return null;
  }
  const katexModule = await import(`${pathToFileURL(katexMjs).href}?mtime=${Date.now()}`);
  const renderToHTMLTree = katexModule.__renderToHTMLTree || katexModule.default?.__renderToHTMLTree;
  if (typeof renderToHTMLTree !== "function") {
    throw new Error(`KaTeX dist at ${katexMjs} does not expose __renderToHTMLTree.`);
  }
  return {
    renderToHTMLTree,
    source: katexMjs
  };
};

const loadKatexSourceInternal = async () => {
  const previousCwd = process.cwd();
  process.chdir(katexRoot);
  try {
    const require = createRequire(path.join(katexRoot, "package.json"));
    const pnpPath = path.join(katexRoot, ".pnp.cjs");
    if (await exists(pnpPath)) {
      require(pnpPath).setup();
    }

    let babelRegister;
    try {
      babelRegister = require("@babel/register");
    } catch {
      throw new Error(
        `KaTeX dependencies are not installed at ${katexRoot}. Run "yarn install" in that repository before generating internal builder metrics.`
      );
    }

    babelRegister({
      cwd: katexRoot,
      extensions: [".js", ".ts"],
      ignore: [/node_modules/],
      configFile: path.join(katexRoot, "babel.config.js")
    });

    const katexModule = require(path.join(katexRoot, "katex.ts"));
    const renderToHTMLTree = katexModule.__renderToHTMLTree || katexModule.default?.__renderToHTMLTree;
    return {
      renderToHTMLTree,
      source: path.join(katexRoot, "katex.ts")
    };
  } finally {
    process.chdir(previousCwd);
  }
};

const loadKatexInternal = async () => {
  const vendorDist = await loadKatexDist(path.join(vendorKatexRoot, "dist"));
  if (vendorDist) {
    return vendorDist;
  }

  const externalDist = await loadKatexDist(path.join(katexRoot, "dist"));
  if (externalDist) {
    return externalDist;
  }

  return loadKatexSourceInternal();
};

const writeInternalMetrics = async () => {
  const {renderToHTMLTree, source} = await loadKatexInternal();
  if (typeof renderToHTMLTree !== "function") {
    throw new Error("KaTeX __renderToHTMLTree internal builder entry was not found.");
  }

  const metrics = fixtures.map(fixture => {
    const root = serializeKatexBuilderNode(renderToHTMLTree(fixture.tex, {
      displayMode: !!fixture.display,
      throwOnError: false,
      strict: "ignore",
      output: "html"
    }));
    return {
      id: fixture.id,
      tex: fixture.tex,
      display: !!fixture.display,
      source,
      root,
      flat: flattenMetrics(root)
    };
  });
  await fs.writeFile(path.join(outputDir, "metrics.json"), `${JSON.stringify(metrics, null, 2)}\n`, "utf8");
  return metrics.length;
};

const chooseScreenshotDist = async () => {
  const candidates = [
    path.join(vendorKatexRoot, "dist"),
    path.join(katexRoot, "dist")
  ];
  for (const candidate of candidates) {
    if (await exists(path.join(candidate, "katex.min.js")) && await exists(path.join(candidate, "katex.min.css"))) {
      return candidate;
    }
  }
  return null;
};

const writeScreenshots = async () => {
  const screenshotDist = await chooseScreenshotDist();
  if (!screenshotDist) {
    console.warn(`Skipping browser bboxes: KaTeX dist files were not found under ${path.join(vendorKatexRoot, "dist")} or ${path.join(katexRoot, "dist")}.`);
    return 0;
  }
  const katexJs = path.join(screenshotDist, "katex.min.js");
  const katexCss = path.join(screenshotDist, "katex.min.css");
  if (!(await exists(katexJs)) || !(await exists(katexCss))) {
    console.warn(`Skipping browser bboxes: KaTeX dist files were not found under ${screenshotDist}.`);
    return 0;
  }
  const embeddedKatexCss = await katexCssWithEmbeddedFonts(katexCss);

  let chromium;
  try {
    ({chromium} = await import("playwright"));
  } catch {
    console.warn("Skipping browser bboxes: Playwright is not installed in this workspace.");
    return 0;
  }
  const html = `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <style>${embeddedKatexCss}</style>
  <style>
    html, body { margin: 0; padding: 0; background: white; }
    body { font-size: 16px; color: #111; }
    .wrap { display: inline-block; padding: 24px; }
    #math { display: inline-block; }
  </style>
</head>
<body>
  <div class="wrap"><span id="math"></span></div>
</body>
</html>`;

  const htmlPath = path.join(os.tmpdir(), `muffin-katex-bbox-${process.pid}.html`);
  await fs.writeFile(htmlPath, html, "utf8");

  const browser = await chromium.launch();
  const page = await browser.newPage({viewport: {width: 1200, height: 800}, deviceScaleFactor: 1});
  await page.goto(pathToFileURL(htmlPath).href, {waitUntil: "load"});
  await page.addScriptTag({path: katexJs});
  await page.waitForFunction(() => Boolean(window.katex));

  const bboxes = [];
  const glyphs = [];
  for (const fixture of fixtures) {
    await page.evaluate(({tex, display}) => {
      const math = document.getElementById("math");
      math.innerHTML = "";
      katex.render(tex, math, {
        displayMode: display,
        throwOnError: false,
        strict: "ignore",
        output: "html"
      });
    }, {tex: fixture.tex, display: !!fixture.display});
    const fontLoadStatus = await page.evaluate(async () => {
      if (document.fonts) {
        const requiredFonts = [
          "normal 400 1em KaTeX_Main",
          "italic 400 1em KaTeX_Math",
          "normal 400 1em KaTeX_Size1",
          "normal 400 1em KaTeX_Size2",
          "normal 400 1em KaTeX_Size3",
          "normal 400 1em KaTeX_Size4"
        ];
        const loaded = await Promise.all(requiredFonts.map(font => document.fonts.load(font)));
        await document.fonts.ready;
        return requiredFonts.map((font, index) => ({
          font,
          loaded: loaded[index].length > 0
        }));
      }
      return [];
    });
    const missingRequiredFonts = fontLoadStatus.filter(font => !font.loaded);
    if (missingRequiredFonts.length > 0) {
      throw new Error(`KaTeX browser bbox required fonts failed to load for ${fixture.id}: ${JSON.stringify(missingRequiredFonts)}`);
    }

    const browserMetrics = await page.evaluate(() => {
      const roundRect = rect => ({
        x: Number(rect.x.toFixed(4)),
        y: Number(rect.y.toFixed(4)),
        width: Number(rect.width.toFixed(4)),
        height: Number(rect.height.toFixed(4))
      });
      const math = document.getElementById("math");
      const katexRoot = math.querySelector(".katex");
      const htmlRoot = math.querySelector(".katex-html");
      const displayRoot = math.querySelector(".katex-display");
      const target = htmlRoot || katexRoot || math;
      const rootFontPx = katexRoot ? Number.parseFloat(getComputedStyle(katexRoot).fontSize) : 16;
      const error = Boolean(math.querySelector(".katex-error"));
      const fontFaces = document.fonts ? [...document.fonts].map(face => ({
        family: face.family.replace(/^"|"$/g, ""),
        style: face.style,
        weight: face.weight,
        status: face.status
      })) : [];
      const failedFonts = fontFaces.filter(face => face.family.startsWith("KaTeX_") && face.status === "error");
      return {
        rootFontPx,
        error,
        fontStatus: document.fonts?.status || "unsupported",
        failedFonts,
        bbox: roundRect(target.getBoundingClientRect()),
        katexBBox: katexRoot ? roundRect(katexRoot.getBoundingClientRect()) : null,
        displayBBox: displayRoot ? roundRect(displayRoot.getBoundingClientRect()) : null
      };
    });
    if (browserMetrics.failedFonts.length > 0) {
      throw new Error(`KaTeX browser bbox font load failed for ${fixture.id}: ${JSON.stringify(browserMetrics.failedFonts)}`);
    }
    const captureBox = {
      x: Math.max(0, Math.floor(browserMetrics.bbox.x - 4)),
      y: Math.max(0, Math.floor(browserMetrics.bbox.y - 4)),
      width: Math.ceil(browserMetrics.bbox.width + 8),
      height: Math.ceil(browserMetrics.bbox.height + 8)
    };
    const screenshotBuffer = await page.screenshot({clip: captureBox});
    const inkBBox = await scanInkBBox(screenshotBuffer);
    const glyphMetrics = await page.evaluate(() => {
      const round = value => Number(value.toFixed(4));
      const math = document.getElementById("math");
      const katexRoot = math.querySelector(".katex");
      const htmlRoot = math.querySelector(".katex-html") || katexRoot || math;
      const rootRect = htmlRoot.getBoundingClientRect();
      const rootFontPx = katexRoot ? Number.parseFloat(getComputedStyle(katexRoot).fontSize) : 16;
      const isVisibleGlyphText = text => text && !/[\s\u200b\ufeff]/u.test(text);
      const classesFor = node => {
        const element = node.nodeType === Node.TEXT_NODE ? node.parentElement : node;
        return element ? Array.from(element.classList) : [];
      };
      const rectFor = rect => ({
        x: round((rect.x - rootRect.x) / rootFontPx),
        y: round((rect.y - rootRect.y) / rootFontPx),
        width: round(rect.width / rootFontPx),
        height: round(rect.height / rootFontPx)
      });
      const pageRectFor = rect => ({
        x: rect.x,
        y: rect.y,
        width: rect.width,
        height: rect.height
      });
      const result = [];
      const walker = document.createTreeWalker(htmlRoot, NodeFilter.SHOW_TEXT | NodeFilter.SHOW_ELEMENT, {
        acceptNode(node) {
          if (node.nodeType === Node.TEXT_NODE) {
            return node.nodeValue && node.nodeValue.length > 0 ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_REJECT;
          }
          if (node.nodeType === Node.ELEMENT_NODE && node.classList.contains("mspace")) {
            return NodeFilter.FILTER_ACCEPT;
          }
          return NodeFilter.FILTER_SKIP;
        }
      });
      for (let node = walker.nextNode(); node; node = walker.nextNode()) {
        if (node.nodeType === Node.TEXT_NODE) {
          for (let index = 0; index < node.nodeValue.length; ++index) {
            const text = node.nodeValue[index];
            if (!isVisibleGlyphText(text)) {
              continue;
            }
            const range = document.createRange();
            range.setStart(node, index);
            range.setEnd(node, index + 1);
            const rect = range.getBoundingClientRect();
            if (rect.width > 0.01 && rect.height > 0.01) {
              result.push({
                kind: "glyph",
                text,
                classes: classesFor(node),
                domRect: rectFor(rect),
                pageRect: pageRectFor(rect)
              });
            }
          }
        } else {
          const rect = node.getBoundingClientRect();
          if (rect.width > 0) {
            result.push({
              kind: "space",
              text: "mspace",
              classes: classesFor(node),
              domRect: rectFor(rect),
              pageRect: pageRectFor(rect)
            });
          }
        }
      }
      return {
        rootPageRect: pageRectFor(rootRect),
        rootFontPx,
        items: result
      };
    });
    const glyphItems = [];
    for (const item of glyphMetrics.items) {
      const resultItem = {
        kind: item.kind,
        text: item.text,
        classes: item.classes,
        domRect: item.domRect
      };
      if (item.kind === "glyph" && item.pageRect.width > 0 && item.pageRect.height > 0) {
        const padding = 2;
        const clip = {
          x: Math.max(0, Math.floor(item.pageRect.x - padding)),
          y: Math.max(0, Math.floor(item.pageRect.y - padding)),
          width: Math.ceil(item.pageRect.width + padding * 2),
          height: Math.ceil(item.pageRect.height + padding * 2)
        };
        const glyphScreenshot = await page.screenshot({clip});
        const glyphInk = await scanInkBBox(glyphScreenshot);
        if (glyphInk) {
          resultItem.inkRect = {
            x: round((clip.x + glyphInk.x - glyphMetrics.rootPageRect.x) / glyphMetrics.rootFontPx),
            y: round((clip.y + glyphInk.y - glyphMetrics.rootPageRect.y) / glyphMetrics.rootFontPx),
            width: round(glyphInk.width / glyphMetrics.rootFontPx),
            height: round(glyphInk.height / glyphMetrics.rootFontPx)
          };
        }
      }
      glyphItems.push(resultItem);
    }
    bboxes.push({
      id: fixture.id,
      tex: fixture.tex,
      display: !!fixture.display,
      source: screenshotDist,
      rootFontPx: Number(browserMetrics.rootFontPx.toFixed(4)),
      error: !!browserMetrics.error,
      fontStatus: browserMetrics.fontStatus,
      bbox: browserMetrics.bbox,
      inkBBox,
      katexBBox: browserMetrics.katexBBox,
      displayBBox: browserMetrics.displayBBox
    });
    glyphs.push({
      id: fixture.id,
      tex: fixture.tex,
      display: !!fixture.display,
      source: screenshotDist,
      rootFontPx: Number(browserMetrics.rootFontPx.toFixed(4)),
      glyphs: glyphItems
    });
  }

  await fs.writeFile(path.join(outputDir, "bbox.json"), `${JSON.stringify(bboxes, null, 2)}\n`, "utf8");
  await fs.writeFile(path.join(outputDir, "glyphs.json"), `${JSON.stringify(glyphs, null, 2)}\n`, "utf8");
  await browser.close();
  return bboxes.length;
};

const metricsCount = await writeInternalMetrics();
const screenshotCount = await writeScreenshots();

console.log(`Wrote ${metricsCount} KaTeX internal builder metrics to ${path.join(outputDir, "metrics.json")}`);
if (screenshotCount > 0) {
  console.log(`Wrote ${screenshotCount} KaTeX browser bboxes to ${path.join(outputDir, "bbox.json")}`);
  console.log(`Wrote ${screenshotCount} KaTeX browser glyph boxes to ${path.join(outputDir, "glyphs.json")}`);
}
