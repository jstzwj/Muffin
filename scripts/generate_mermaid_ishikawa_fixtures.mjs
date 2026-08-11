import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 Ishikawa source-entry oracle.

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_ISHIKAWA_MODULE_SHA256 =
  "1cc5d5c76ee381aad9b17580ebda09e2913674ccc4366a95ca507e4cb1747844";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const EXPECTED_NOTO_SHA256 =
  "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const fixtureDir = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const ishikawaModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.core",
  "ishikawaDiagram-FXEZZL3T.mjs",
);
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assertEqual = (actual, expected, label) => {
  if (actual !== expected) throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const writeJson = (file, value) => {
  const payload = { ...value };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.writeFileSync(file, `${JSON.stringify(payload, null, 2)}\n`);
};

const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assertEqual(pkg.version, EXPECTED_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MODULE_SHA256, "Mermaid module");
assertEqual(
  sha256(fs.readFileSync(ishikawaModuleFile)),
  EXPECTED_ISHIKAWA_MODULE_SHA256,
  "Ishikawa module",
);
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const frontmatter = (config, body, title = undefined) => {
  const lines = ["---"];
  if (title !== undefined) lines.push(`title: ${title}`);
  if (config !== undefined) {
    lines.push("config:");
    for (const line of JSON.stringify(config, null, 2).split("\n")) lines.push(`  ${line}`);
  }
  lines.push("---", body);
  return lines.join("\n");
};

const canonicalBody = `ishikawa
Delayed release
    People
        Missing reviewer
        Training gap
    Process
        Manual approval
    Platform
        Slow build
        Flaky tests
    Policy
        Change freeze`;

const grammarCases = [
  { id: "header-only", source: "ishikawa" },
  { id: "header-newline", source: "ishikawa\n" },
  { id: "root-only", source: "ishikawa\nEffect" },
  { id: "beta", source: "ishikawa-beta\nEffect\n  Cause" },
  { id: "uppercase", source: "ISHIKAWA-BETA\nEffect\n  Cause" },
  { id: "same-line-root", source: "ishikawa Effect" },
  { id: "canonical", source: canonicalBody },
  { id: "unindented-causes", source: "ishikawa\nEffect\nCause A\nCause B" },
  { id: "effect-more-indented", source: "ishikawa\n      Effect\n  Cause A\n    Child" },
  { id: "first-cause-base", source: "ishikawa\nEffect\n    Cause A\n      Child\n  Cause B" },
  { id: "deep-tree", source: "ishikawa\nE\n A\n  B\n   C\n    D\n E2" },
  { id: "tab-level", source: "ishikawa\nEffect\n\tCause\n\t\tChild" },
  { id: "comments", source: "%% pre\nishikawa\nEffect\n%% comment\n  Cause" },
  { id: "inline-percent", source: "ishikawa\nEffect %% literal\n  Cause" },
  { id: "blank-between", source: "ishikawa\nEffect\n\n  Cause" },
  { id: "leading-blank", source: "\n\nishikawa\nEffect" },
  { id: "leading-comment-blank", source: "%% one\n\nishikawa\nEffect" },
  { id: "trailing-spaces", source: "ishikawa\nEffect   \n  Cause   " },
  { id: "metadata-looking", source: "ishikawa\ntitle Heading\n  accTitle: A\n  accDescr: D" },
  {
    id: "sanitizer",
    source:
      "ishikawa\n<script>bad</script><b>Effect</b><img src=x onerror=bad>\n  <style>x</style><i>Cause</i>",
  },
  {
    id: "frontmatter",
    source: frontmatter(undefined, "ishikawa\nEffect\n  Cause", "Front title"),
  },
  {
    id: "directive",
    source: init({ theme: "dark" }, "ishikawa\nEffect\n  Cause"),
  },
  { id: "unicode", source: "ishikawa\n效果\n  人员\n    审核" },
  { id: "prefix-reject", source: "ishikawaX\nEffect" },
  { id: "hyphen-reject", source: "ishikawa-gamma\nEffect" },
  { id: "header-twice", source: "ishikawa\nEffect\nishikawa\n  Cause" },
  { id: "semicolon-literal", source: "ishikawa\nEffect; literal\n  Cause" },
  { id: "only-comment", source: "ishikawa\n%% nothing" },
  { id: "crlf", source: "ishikawa\r\nEffect\r\n  Cause" },
  { id: "spaceline-after-header", source: "ishikawa\n  \nEffect" },
];

const geometryCases = [
  { id: "root-only", source: init({ fontFamily: "Noto Sans" }, "ishikawa\nEffect") },
  { id: "canonical", source: init({ fontFamily: "Noto Sans" }, canonicalBody) },
  {
    id: "odd-causes",
    source: init(
      { fontFamily: "Noto Sans" },
      "ishikawa\nEffect\n  One\n  Two\n  Three\n  Four\n  Five",
    ),
  },
  {
    id: "deep-alternating",
    source: init(
      { fontFamily: "Noto Sans" },
      "ishikawa\nEffect\n  Parent\n    Child A\n      Grandchild A\n        Leaf A\n    Child B\n      Grandchild B",
    ),
  },
  {
    id: "unbalanced",
    source: init(
      { fontFamily: "Noto Sans" },
      "ishikawa\nEffect\n  Large\n    A\n    B\n      C\n      D\n    E\n  Small\n  Other",
    ),
  },
  {
    id: "long-wrap",
    source: init(
      { fontFamily: "Noto Sans" },
      "ishikawa\nA very long effect label that wraps into multiple lines\n  A cause label with several words that should wrap\n    A nested label with several words",
    ),
  },
  {
    id: "font-size-32",
    source: init(
      { fontFamily: "Noto Sans", fontSize: 32 },
      "ishikawa\nEffect\n  Cause\n    Child",
    ),
  },
  {
    id: "font-size-zero",
    source: init(
      { fontFamily: "Noto Sans", fontSize: 0 },
      "ishikawa\nEffect\n  Cause",
    ),
  },
  {
    id: "sanitized-html",
    source: init(
      { fontFamily: "Noto Sans" },
      "ishikawa\n<b>Effect</b><script>bad</script>\n  <i>Cause</i><img src=x onerror=bad>",
    ),
  },
  {
    id: "hand-drawn",
    source: init(
      { fontFamily: "Noto Sans", look: "handDrawn", handDrawnSeed: 17 },
      "ishikawa\nEffect\n  Cause A\n    Child\n  Cause B",
    ),
  },
];

const configCases = [
  { id: "defaults", config: {} },
  { id: "padding-50", config: { ishikawa: { diagramPadding: 50 } } },
  { id: "padding-zero", config: { ishikawa: { diagramPadding: 0 } } },
  { id: "padding-negative", config: { ishikawa: { diagramPadding: -10 } } },
  { id: "padding-string", config: { ishikawa: { diagramPadding: "12" } } },
  { id: "padding-true", config: { ishikawa: { diagramPadding: true } } },
  { id: "padding-null", config: { ishikawa: { diagramPadding: null } } },
  { id: "padding-array", config: { ishikawa: { diagramPadding: [12] } } },
  { id: "padding-object", config: { ishikawa: { diagramPadding: { value: 12 } } } },
  { id: "use-max-width-false", config: { ishikawa: { useMaxWidth: false } } },
  { id: "use-max-width-zero", config: { ishikawa: { useMaxWidth: 0 } } },
  { id: "use-max-width-string", config: { ishikawa: { useMaxWidth: "false" } } },
  { id: "use-max-width-null", config: { ishikawa: { useMaxWidth: null } } },
  { id: "use-width-inert", config: { ishikawa: { useWidth: 900 } } },
  { id: "font-size-28", config: { fontSize: 28 } },
  { id: "font-size-string", config: { fontSize: "28px" } },
  {
    id: "theme-font-size-28",
    config: { themeVariables: { fontSize: "28px", fontFamily: "Noto Sans" } },
  },
  {
    id: "theme-font-size-zero",
    config: { themeVariables: { fontSize: "0px", fontFamily: "Noto Sans" } },
  },
  { id: "font-family", config: { fontFamily: "DefinitelyMissing, Noto Sans" } },
  { id: "theme-dark", config: { theme: "dark" } },
  { id: "theme-forest", config: { theme: "forest" } },
  { id: "theme-neutral", config: { theme: "neutral" } },
  {
    id: "colors",
    config: {
      themeVariables: {
        lineColor: "#ff0000",
        mainBkg: "#00ff00",
        textColor: "#0000ff",
      },
    },
  },
  { id: "line-empty", config: { themeVariables: { lineColor: "" } } },
  { id: "background-none", config: { themeVariables: { mainBkg: "none" } } },
  { id: "text-none", config: { themeVariables: { textColor: "none" } } },
  {
    id: "look-hand-drawn",
    config: { look: "handDrawn", handDrawnSeed: 17 },
  },
  { id: "look-case-inert", config: { look: "HandDrawn", handDrawnSeed: 17 } },
  {
    id: "frontmatter-config",
    source: frontmatter(
      {
        fontFamily: "Noto Sans",
        ishikawa: { diagramPadding: 45, useMaxWidth: false },
      },
      canonicalBody,
      "Front",
    ),
  },
];

const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js"),
  ).href,
);
const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"],
});

const provenance = {
  package: "mermaid",
  version: EXPECTED_VERSION,
  moduleSha256: EXPECTED_MODULE_SHA256,
  ishikawaModuleSha256: EXPECTED_ISHIKAWA_MODULE_SHA256,
  chromeProduct: EXPECTED_CHROME_PRODUCT,
  chromeSha256: EXPECTED_CHROME_SHA256,
  notoSansSha256: EXPECTED_NOTO_SHA256,
  sourceEntry: true,
};

try {
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const hostPage = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;
  const page = await browser.newPage();
  await page.setViewport({ width: 1400, height: 1000, deviceScaleFactor: 1 });

  const grammar = [];
  for (const fixture of grammarCases) {
    await page.goto(hostPage);
    const result = await page.evaluate(
      async ({ source, caseId, moduleUrl }) => {
        const clean = source
          .replace(/^---[\s\S]*?---\s*/, "")
          .replace(/^%%\{[\s\S]*?\}%%\s*/, "");
        const project = (node) =>
          node
            ? {
                text: node.text,
                children: (node.children ?? []).map(project),
              }
            : null;
        try {
          const { default: mermaid } = await import(moduleUrl);
          mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
          await mermaid.parse(source);
          const diagram = await mermaid.mermaidAPI.getDiagramFromText(clean);
          const root = project(diagram.db.getRoot());
          const rendered = await mermaid.render(`grammar-${caseId}`, source);
          document.body.innerHTML = rendered.svg;
          return {
            accept: true,
            db: {
              root,
              title: diagram.db.getDiagramTitle(),
              accTitle: diagram.db.getAccTitle(),
              accDescr: diagram.db.getAccDescription(),
            },
            rendered: {
              ishikawaGroups: document.querySelectorAll("g.ishikawa").length,
              svgText: [...document.querySelectorAll("g.ishikawa text")].map(
                (item) => item.textContent ?? "",
              ),
            },
          };
        } catch (error) {
          const raw = String(error?.message ?? error);
          const message = raw.replace(/\s+/g, " ").trim();
          const lineMatch = raw.match(/(?:Parse|Lexical) error on line (\d+)/i);
          const columnMatch = error?.hash?.loc?.first_column;
          let kind = "runtime";
          if (message.startsWith("No diagram type detected")) kind = "no-diagram";
          else if (/Lexical error/i.test(message)) kind = "lexer";
          else if (/Parse error/i.test(message)) kind = "parser";
          return {
            accept: false,
            reject: {
              kind,
              message,
              line: Number(lineMatch?.[1] ?? error?.hash?.line + 1 ?? 0),
              column: Number.isFinite(columnMatch) ? columnMatch + 1 : 0,
              token: error?.hash?.token ?? "",
            },
          };
        }
      },
      { source: fixture.source, caseId: fixture.id, moduleUrl: pathToFileURL(moduleFile).href },
    );
    grammar.push({ id: fixture.id, source: fixture.source, ...result });
  }

  const capture = async (source, id) => {
    await page.goto(hostPage);
    return page.evaluate(
      async ({ source, id, moduleUrl, fontUrl }) => {
        const font = new FontFace("Noto Sans", `url(${fontUrl})`);
        await font.load();
        document.fonts.add(font);
        await document.fonts.load("16px 'Noto Sans'");
        await document.fonts.load("600 14px 'Noto Sans'");
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          deterministicIds: true,
          deterministicIDSeed: "ishikawa-fixture",
        });
        const rendered = await mermaid.render(`ishikawa-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
        const root = document.querySelector("svg");
        const graph = root.querySelector("g.ishikawa");
        const attrs = (element) =>
          element
            ? Object.fromEntries([...element.attributes].map((attr) => [attr.name, attr.value]))
            : null;
        const bbox = (element) => {
          if (!element) return null;
          const value = element.getBBox();
          return { x: value.x, y: value.y, width: value.width, height: value.height };
        };
        const style = (element) => {
          if (!element) return null;
          const value = getComputedStyle(element);
          return {
            fill: value.fill,
            stroke: value.stroke,
            strokeWidth: value.strokeWidth,
            fontFamily: value.fontFamily,
            fontSize: value.fontSize,
            fontWeight: value.fontWeight,
            dominantBaseline: value.dominantBaseline,
            textAnchor: value.textAnchor,
          };
        };
        const client = root.getBoundingClientRect();
        return {
          root: {
            attrs: attrs(root),
            bbox: bbox(root),
            client: { width: client.width, height: client.height },
          },
          graph: graph
            ? {
                attrs: attrs(graph),
                bbox: bbox(graph),
                order: [...graph.children].map(
                  (child) => `${child.tagName.toLowerCase()}.${child.getAttribute("class") ?? ""}`,
                ),
              }
            : null,
          groups: graph
            ? [...graph.querySelectorAll("g")].map((group) => ({
                attrs: attrs(group),
                bbox: bbox(group),
              }))
            : [],
          lines: graph
            ? [...graph.querySelectorAll("line")].map((line) => ({
                attrs: attrs(line),
                bbox: bbox(line),
                computed: style(line),
              }))
            : [],
          rects: graph
            ? [...graph.querySelectorAll("rect")].map((rect) => ({
                attrs: attrs(rect),
                bbox: bbox(rect),
                computed: style(rect),
              }))
            : [],
          paths: graph
            ? [...graph.querySelectorAll("path")].map((item) => ({
                attrs: attrs(item),
                bbox: bbox(item),
                computed: style(item),
              }))
            : [],
          texts: graph
            ? [...graph.querySelectorAll("text")].map((text) => ({
                attrs: attrs(text),
                value: text.textContent ?? "",
                tspans: [...text.querySelectorAll("tspan")].map((span) => ({
                  attrs: attrs(span),
                  value: span.textContent ?? "",
                  bbox: bbox(span),
                })),
                bbox: bbox(text),
                computed: style(text),
              }))
            : [],
          marker: (() => {
            const marker = graph?.querySelector("defs marker");
            return marker
              ? {
                  attrs: attrs(marker),
                  path: {
                    attrs: attrs(marker.querySelector("path")),
                    computed: style(marker.querySelector("path")),
                  },
                }
              : null;
          })(),
          metadata: {
            title: root.querySelector(":scope > title")?.textContent ?? "",
            desc: root.querySelector(":scope > desc")?.textContent ?? "",
            ariaLabelledby: root.getAttribute("aria-labelledby") ?? "",
            ariaDescribedby: root.getAttribute("aria-describedby") ?? "",
            role: root.getAttribute("role") ?? "",
          },
        };
      },
      {
        source,
        id,
        moduleUrl: pathToFileURL(moduleFile).href,
        fontUrl: pathToFileURL(fontFile).href,
      },
    );
  };

  const geometry = [];
  for (const fixture of geometryCases) {
    geometry.push({
      id: fixture.id,
      source: fixture.source,
      expected: await capture(fixture.source, `geometry-${fixture.id}`),
    });
  }

  const config = [];
  for (const fixture of configCases) {
    const source =
      fixture.source ??
      init(
        {
          fontFamily: "Noto Sans",
          themeVariables: { fontFamily: "Noto Sans" },
          ...fixture.config,
        },
        canonicalBody,
      );
    let expected;
    try {
      expected = { status: "ready", dom: await capture(source, `config-${fixture.id}`) };
    } catch (error) {
      expected = {
        status: "error",
        message: String(error?.message ?? error).replace(/\s+/g, " ").trim(),
      };
    }
    config.push({ id: fixture.id, source, expected });
  }

  writeJson(path.join(fixtureDir, "ishikawa-grammar.json"), {
    upstream: provenance,
    oracle: "source-entry detector, Jison tree DB, sanitizer, metadata, and diagnostics",
    cases: grammar,
  });
  writeJson(path.join(fixtureDir, "ishikawa-geometry.json"), {
    upstream: provenance,
    oracle: "source-entry SVG spine, branches, labels, marker, DOM order, viewBox, and metadata",
    cases: geometry,
  });
  writeJson(path.join(fixtureDir, "ishikawa-config.json"), {
    upstream: provenance,
    oracle: "source-entry Ishikawa sizing, JS coercion, generic theme colors, font, and look",
    cases: config,
  });

  const pixelDir = path.join(fixtureDir, "ishikawa-pixel");
  fs.mkdirSync(pixelDir, { recursive: true });
  const pixelCases = [
    geometryCases.find((item) => item.id === "canonical"),
    { id: "dark", source: init({ theme: "dark", fontFamily: "Noto Sans" }, canonicalBody) },
    geometryCases.find((item) => item.id === "hand-drawn"),
  ];
  const pixels = [];
  for (const fixture of pixelCases) {
    await page.goto(hostPage);
    await page.evaluate(
      async ({ source, id, moduleUrl, fontUrl }) => {
        const font = new FontFace("Noto Sans", `url(${fontUrl})`);
        await font.load();
        document.fonts.add(font);
        await document.fonts.load("16px 'Noto Sans'");
        await document.fonts.load("600 14px 'Noto Sans'");
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          deterministicIds: true,
          deterministicIDSeed: "ishikawa-pixel",
        });
        const rendered = await mermaid.render(`ishikawa-pixel-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
      },
      {
        source: fixture.source,
        id: fixture.id,
        moduleUrl: pathToFileURL(moduleFile).href,
        fontUrl: pathToFileURL(fontFile).href,
      },
    );
    const element = await page.$("svg");
    const file = `${fixture.id}.png`;
    const bytes = await element.screenshot({
      path: path.join(pixelDir, file),
      omitBackground: true,
    });
    const box = await element.boundingBox();
    pixels.push({
      id: fixture.id,
      source: fixture.source,
      file,
      width: Math.round(box.width),
      height: Math.round(box.height),
      sha256: sha256(bytes),
    });
  }
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream: provenance,
    oracle: "transparent Ishikawa element screenshots at DPR 1",
    cases: pixels,
  });

  const accepted = grammar.filter((item) => item.accept).length;
  console.log(
    `Wrote Ishikawa fixtures: ${grammar.length} grammar (${accepted} accept), ${geometry.length} geometry, ${config.length} config, ${pixels.length} pixel`,
  );
} finally {
  await browser.close();
}
