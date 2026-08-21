import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Reproducible Mermaid 11.16.0 TreeView parser/DB/DOM/config/pixel oracle.

const EXPECTED_VERSION = "11.16.0";
const EXPECTED_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_TREEVIEW_MODULE_SHA256 =
  "00d8b483476a98b844bfedad281bc25bb97b527aa71b4cce6fc57447a4a50b70";
const EXPECTED_PARSER_TREEVIEW_SHA256 =
  "e9ab827a620482e7b69e23bc176d23dabf15b34402fae2ce58da9583cf655daf";
const EXPECTED_PARSER_SUPPORT_SHA256 =
  "06f44cd8c6afc77ea44e174f7af983c8a91368e50d32a55d4bcdd71df92c55e2";
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
const treeViewModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.core",
  "diagram-OA4YK3LP.mjs",
);
const parserRoot = path.join(path.dirname(mermaidRoot), "@mermaid-js", "parser", "dist");
const parserModuleFile = path.join(parserRoot, "mermaid-parser.esm.mjs");
const parserTreeViewFile = path.join(
  parserRoot,
  "chunks",
  "mermaid-parser.esm",
  "treeView-543TCLMP.mjs",
);
const parserSupportFile = path.join(
  parserRoot,
  "chunks",
  "mermaid-parser.esm",
  "chunk-5XZ4DK4T.mjs",
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
  sha256(fs.readFileSync(treeViewModuleFile)),
  EXPECTED_TREEVIEW_MODULE_SHA256,
  "TreeView module",
);
assertEqual(
  sha256(fs.readFileSync(parserTreeViewFile)),
  EXPECTED_PARSER_TREEVIEW_SHA256,
  "TreeView parser entry",
);
assertEqual(
  sha256(fs.readFileSync(parserSupportFile)),
  EXPECTED_PARSER_SUPPORT_SHA256,
  "TreeView parser support",
);
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome");
assertEqual(sha256(fs.readFileSync(fontFile)), EXPECTED_NOTO_SHA256, "Noto Sans");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const frontmatter = (config, body, title = undefined) => {
  const lines = ["---"];
  if (title !== undefined) lines.push(`title: ${title}`);
  if (config !== undefined) {
    lines.push("config:");
    const json = JSON.stringify(config, null, 2).split("\n");
    for (const line of json) lines.push(`  ${line}`);
  }
  lines.push("---", body);
  return lines.join("\n");
};

const grammarCases = [
  { id: "empty-tree", source: "treeView-beta" },
  { id: "canonical", source: "treeView-beta\nproject/\n  src/\n    main.cpp\n  README.md" },
  { id: "same-line-node", source: "treeView-beta root/" },
  { id: "tabs-and-spaces", source: "treeView-beta\nroot/\n\tfile-a\n  file-b" },
  { id: "quoted-names", source: "treeView-beta\n\"spaced file.txt\"\n' single quoted / '" },
  { id: "empty-quoted", source: "treeView-beta\n\"\"\n''" },
  { id: "bare-internal-spaces", source: "treeView-beta\nalpha   beta   " },
  { id: "directory", source: "treeView-beta\nfolder/\n  child" },
  { id: "annotations", source: "treeView-beta\nroot/ :::highlight icon(folder) ## Root description" },
  { id: "annotation-order", source: "treeView-beta\nfile icon(file) :::highlight ## Desc" },
  { id: "description-greedy", source: "treeView-beta\nfile ## Desc :::highlight icon(file)" },
  { id: "empty-icon", source: "treeView-beta\nfile icon()" },
  { id: "qualified-icon", source: "treeView-beta\nfile icon(logos:github-icon)" },
  { id: "class-hyphen", source: "treeView-beta\nfile :::my-class" },
  { id: "comments", source: "\n%% before\ntreeView-beta\n%% middle\nroot\n%% after" },
  {
    id: "metadata",
    source: "treeView-beta\ntitle Inline\naccTitle: Accessible\naccDescr: Description\nroot",
  },
  {
    id: "metadata-block",
    source: "treeView-beta\naccDescr { First line\n  Second line }\nroot",
  },
  {
    id: "metadata-last-wins",
    source: "treeView-beta\ntitle One\ntitle Two\naccTitle: A\naccTitle: B\nroot",
  },
  { id: "frontmatter", source: frontmatter(undefined, "treeView-beta\nroot", "Front") },
  { id: "directive", source: init({ theme: "dark" }, "treeView-beta\nroot") },
  { id: "semicolon-is-name", source: "treeView-beta\nroot;" },
  { id: "hash-is-name", source: "treeView-beta\n#file" },
  {
    id: "box-thin",
    source: "treeView-beta\nproject/\n├── src/\n│   ├── main.cpp\n│   └── app.cpp\n└── README.md",
  },
  {
    id: "box-heavy",
    source: "treeView-beta\nproject/\n┣━━ src/\n┃   ┗━━ main.cpp\n┗━━ README.md",
  },
  {
    id: "box-tabs-metadata-comment",
    source:
      "treeView-beta\ntitle Box tree\nproject/\n├── src/\n│\t├── main.cpp\n%% hidden\n│\t└── app.cpp",
  },
  { id: "reject-uppercase-detector", source: "TREEVIEW-BETA\nroot" },
  { id: "reject-prefix", source: "treeView-betamax\nroot" },
  { id: "reject-wrong-header", source: "treeView\nroot" },
  { id: "reject-unclosed-double", source: "treeView-beta\n\"root" },
  { id: "reject-unclosed-single", source: "treeView-beta\n'root" },
  { id: "reject-leading-quote-tail", source: "treeView-beta\n\"root\"tail" },
  { id: "reject-annotation-only", source: "treeView-beta\n:::highlight" },
  { id: "reject-icon-only", source: "treeView-beta\nicon(file)" },
  { id: "reject-invalid-class", source: "treeView-beta\nfile :::9bad" },
  { id: "reject-invalid-icon", source: "treeView-beta\nfile icon(a:b:c)" },
  { id: "reject-box-empty", source: "treeView-beta\nroot/\n└──" },
  { id: "reject-box-indented-plain", source: "treeView-beta\nroot/\n├── child\n    plain" },
];

const canonicalBody =
  "treeView-beta\nproject/ :::highlight ## Workspace\n  src/\n    main.cpp\n    util.js\n  README.md";
const geometryCases = [
  { id: "canonical", source: init({ fontFamily: "Noto Sans" }, canonicalBody) },
  { id: "empty-tree", source: init({ fontFamily: "Noto Sans" }, "treeView-beta") },
  {
    id: "quoted-whitespace",
    source: init(
      { fontFamily: "Noto Sans" },
      "treeView-beta\n\" alpha   beta \"\n  ' child name '",
    ),
  },
  {
    id: "descriptions",
    source: init(
      { fontFamily: "Noto Sans" },
      "treeView-beta\nroot/ ## Root details\n  short ## One\n  a-very-long-label ## Two",
    ),
  },
  {
    id: "highlight-nested",
    source: init(
      { fontFamily: "Noto Sans" },
      "treeView-beta\nroot/\n  child :::highlight\n    grandchild",
    ),
  },
  {
    id: "built-in-icons",
    source: init(
      { fontFamily: "Noto Sans", treeView: { showIcons: true } },
      "treeView-beta\nroot/\n  file.txt\n  hidden icon()\n  explicit icon(folder)",
    ),
  },
  {
    id: "detected-icons",
    source: init(
      {
        fontFamily: "Noto Sans",
        treeView: {
          showIcons: true,
          filenameIcons: { README: "folder" },
          extensionIcons: { ".js": "file", txt: "none" },
        },
      },
      "treeView-beta\nroot/\n  README\n  app.JS\n  notes.txt",
    ),
  },
  {
    id: "box-drawing",
    source: init(
      { fontFamily: "Noto Sans" },
      "treeView-beta\nproject/\n├── src/\n│   ├── main.cpp\n│   └── app.cpp\n└── README.md",
    ),
  },
  {
    id: "metadata",
    source: init(
      { fontFamily: "Noto Sans" },
      "treeView-beta\ntitle Inline\naccTitle: Accessible\naccDescr: Description\nroot",
    ),
  },
  {
    id: "frontmatter",
    source: frontmatter({ fontFamily: "Noto Sans" }, "treeView-beta\nroot", "Front"),
  },
];

const configCases = [
  { id: "defaults", config: {} },
  { id: "row-indent", config: { treeView: { rowIndent: 30 } } },
  { id: "padding-x", config: { treeView: { paddingX: 20 } } },
  { id: "padding-y", config: { treeView: { paddingY: 15 } } },
  { id: "line-thickness", config: { treeView: { lineThickness: 5 } } },
  { id: "show-icons", config: { treeView: { showIcons: true } } },
  { id: "use-max-width-false", config: { treeView: { useMaxWidth: false } } },
  { id: "use-width-inert", config: { treeView: { useWidth: 900 } } },
  { id: "row-indent-string", config: { treeView: { rowIndent: "30" } } },
  { id: "padding-x-string", config: { treeView: { paddingX: "8" } } },
  { id: "padding-y-string", config: { treeView: { paddingY: "8" } } },
  { id: "line-thickness-string", config: { treeView: { lineThickness: "3" } } },
  { id: "show-icons-string-false", config: { treeView: { showIcons: "false" } } },
  { id: "use-max-width-string-false", config: { treeView: { useMaxWidth: "false" } } },
  { id: "zero-layout", config: { treeView: { rowIndent: 0, paddingX: 0, paddingY: 0 } } },
  { id: "negative-layout", config: { treeView: { rowIndent: -5, paddingX: -2, paddingY: -3 } } },
  { id: "null-layout", config: { treeView: { rowIndent: null, paddingX: null } } },
  { id: "array-layout", config: { treeView: { rowIndent: [30], paddingX: [8] } } },
  { id: "theme-dark", config: { theme: "dark" } },
  { id: "theme-forest", config: { theme: "forest" } },
  {
    id: "style-all",
    config: {
      themeVariables: {
        treeView: {
          labelFontSize: "24px",
          labelColor: "#ff0000",
          lineColor: "#00aa00",
          iconColor: "#0000ff",
          descriptionColor: "#aa00aa",
          highlightBg: "rgba(0, 255, 255, 0.25)",
          highlightStroke: "#ff8800",
        },
      },
    },
  },
  {
    id: "style-partial-replacement",
    config: { theme: "dark", themeVariables: { treeView: { labelColor: "#123456" } } },
  },
  {
    id: "font-family-fallback",
    config: { fontFamily: "DefinitelyMissing, Noto Sans" },
  },
  {
    id: "frontmatter-config",
    source: frontmatter(
      { fontFamily: "Noto Sans", treeView: { rowIndent: 25, showIcons: true } },
      canonicalBody,
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
  treeViewModuleSha256: EXPECTED_TREEVIEW_MODULE_SHA256,
  parserTreeViewSha256: EXPECTED_PARSER_TREEVIEW_SHA256,
  parserSupportSha256: EXPECTED_PARSER_SUPPORT_SHA256,
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
      async ({ source, moduleUrl, parserModuleUrl }) => {
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({ startOnLoad: false, securityLevel: "strict" });
        try {
          await mermaid.parse(source);
          const diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
          const pick = (node) => ({
            id: node.id,
            level: node.level,
            name: node.name,
            nodeType: node.nodeType,
            icon: node.icon ?? null,
            cssClass: node.cssClass ?? null,
            description: node.description ?? null,
            children: node.children.map(pick),
          });
          const { parse } = await import(parserModuleUrl);
          const ast = await parse("treeView", source.replace(/^---[\s\S]*?---\s*/, ""));
          return {
            accept: true,
            ast: {
              title: ast.title ?? "",
              accTitle: ast.accTitle ?? "",
              accDescr: ast.accDescr ?? "",
              nodes: ast.nodes.map((node) => ({
                indent: node.indent ?? null,
                name: node.name,
                classAnnotation: node.classAnnotation ?? null,
                iconAnnotation: node.iconAnnotation ?? null,
                descAnnotation: node.descAnnotation ?? null,
              })),
            },
            db: {
              title: diagram.db.getDiagramTitle(),
              accTitle: diagram.db.getAccTitle(),
              accDescr: diagram.db.getAccDescription(),
              root: pick(diagram.db.getRoot()),
            },
          };
        } catch (error) {
          const raw = String(error?.message ?? error);
          const message = raw.replace(/\s+/g, " ").trim();
          const match = raw.match(/(?:Lexer|Parse) error on line (\d+|\?), column (\d+|\?)/i);
          const boxMatch = raw.match(/Line (\d+):/);
          let kind = "unknown";
          if (message.startsWith("No diagram type detected")) kind = "no-diagram";
          else if (message.includes("Lexer error")) kind = "lexer";
          else if (message.includes("Parse error") || message.includes("Parsing failed")) kind = "parser";
          else if (boxMatch) kind = "preprocess";
          return {
            accept: false,
            reject: {
              kind,
              message,
              line: match && match[1] !== "?" ? Number(match[1]) : Number(boxMatch?.[1] ?? 0),
              column: match && match[2] !== "?" ? Number(match[2]) : 0,
            },
          };
        }
      },
      {
        source: fixture.source,
        moduleUrl: pathToFileURL(moduleFile).href,
        parserModuleUrl: pathToFileURL(parserModuleFile).href,
      },
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
        await document.fonts.load("bold 16px 'Noto Sans'");
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          deterministicIds: true,
          deterministicIDSeed: "treeview-fixture",
        });
        const rendered = await mermaid.render(`treeview-${id}`, source);
        document.body.style.margin = "0";
        document.body.innerHTML = rendered.svg;
        const root = document.querySelector("svg");
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
            color: value.color,
            fontFamily: value.fontFamily,
            fontSize: value.fontSize,
            fontWeight: value.fontWeight,
            fontStyle: value.fontStyle,
            whiteSpace: value.whiteSpace,
          };
        };
        const rootClient = root.getBoundingClientRect();
        const labels = [...root.querySelectorAll("text.treeView-node-label")].map((label) => {
          const group = label.parentElement;
          const description = group.querySelector("text.treeView-node-description");
          const icon = group.querySelector("use.treeView-node-icon");
          const highlight = group.querySelector("rect.treeView-highlight-bg");
          return {
            group: { attrs: attrs(group), bbox: bbox(group) },
            label: {
              attrs: attrs(label),
              value: label.textContent,
              textLength: label.getComputedTextLength(),
              bbox: bbox(label),
              computed: style(label),
            },
            description: description
              ? {
                  attrs: attrs(description),
                  value: description.textContent,
                  textLength: description.getComputedTextLength(),
                  bbox: bbox(description),
                  computed: style(description),
                }
              : null,
            icon: icon
              ? { attrs: attrs(icon), bbox: bbox(icon), computed: style(icon) }
              : null,
            highlight: highlight
              ? { attrs: attrs(highlight), bbox: bbox(highlight), computed: style(highlight) }
              : null,
          };
        });
        return {
          root: {
            attrs: attrs(root),
            bbox: bbox(root),
            client: {
              x: rootClient.x,
              y: rootClient.y,
              width: rootClient.width,
              height: rootClient.height,
            },
          },
          childTags: [...root.children].map((child) => child.tagName.toLowerCase()),
          tree: {
            attrs: attrs(root.querySelector("g.tree-view")),
            bbox: bbox(root.querySelector("g.tree-view")),
          },
          labels,
          lines: [...root.querySelectorAll("line.treeView-node-line")].map((line) => ({
            attrs: attrs(line),
            bbox: bbox(line),
            computed: style(line),
          })),
          defs: [...root.querySelectorAll(":scope > defs > g")].map((group) => ({
            attrs: attrs(group),
            html: group.innerHTML,
          })),
          metadata: {
            title: root.querySelector(":scope > title")?.textContent ?? "",
            desc: root.querySelector(":scope > desc")?.textContent ?? "",
            ariaLabelledby: root.getAttribute("aria-labelledby") ?? "",
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
    const source = fixture.source ?? init(
      { fontFamily: "Noto Sans", themeVariables: { fontFamily: "Noto Sans" }, ...fixture.config },
      canonicalBody,
    );
    config.push({ id: fixture.id, source, expected: await capture(source, `config-${fixture.id}`) });
  }

  writeJson(path.join(fixtureDir, "treeview-grammar.json"), {
    upstream: provenance,
    oracle: "source-entry detector, box-drawing preprocessor, Langium AST, DB tree, and diagnostics",
    cases: grammar,
  });
  writeJson(path.join(fixtureDir, "treeview-geometry.json"), {
    upstream: provenance,
    oracle: "source-entry SVG DOM, preorder geometry, icons, descriptions, highlights, and metadata",
    cases: geometry,
  });
  writeJson(path.join(fixtureDir, "treeview-config.json"), {
    upstream: provenance,
    oracle: "source-entry TreeView layout, sizing, theme, raw coercion, and nested style configuration",
    cases: config,
  });

  const pixelDir = path.join(fixtureDir, "treeview-pixel");
  fs.mkdirSync(pixelDir, { recursive: true });
  const pixelCases = [
    geometryCases.find((item) => item.id === "canonical"),
    {
      ...geometryCases.find((item) => item.id === "built-in-icons"),
      iconOracle: true,
    },
    {
      id: "styled",
      source: init(
        {
          fontFamily: "Noto Sans",
          themeVariables: {
            fontFamily: "Noto Sans",
            treeView: {
              labelColor: "#b91c1c",
              lineColor: "#166534",
              iconColor: "#1d4ed8",
              descriptionColor: "#7e22ce",
              highlightBg: "rgba(250, 204, 21, 0.35)",
              highlightStroke: "#a16207",
            },
          },
          treeView: { showIcons: true },
        },
        canonicalBody,
      ),
    },
  ];
  const pixels = [];
  const capturePixel = async (fixture) => {
    await page.goto(hostPage);
    await page.evaluate(
      async ({ source, id, moduleUrl, fontUrl }) => {
        const font = new FontFace("Noto Sans", `url(${fontUrl})`);
        await font.load();
        document.fonts.add(font);
        await document.fonts.load("16px 'Noto Sans'");
        await document.fonts.load("bold 16px 'Noto Sans'");
        const { default: mermaid } = await import(moduleUrl);
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          deterministicIds: true,
          deterministicIDSeed: "treeview-pixel",
        });
        const rendered = await mermaid.render(`treeview-pixel-${id}`, source);
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
    const iconContract = fixture.iconOracle
      ? await page.$eval("svg", (root) => {
          const attrs = (element) =>
            element
              ? Object.fromEntries(
                  [...element.attributes].map((attr) => [attr.name, attr.value]),
                )
              : null;
          return {
            uses: [...root.querySelectorAll("use.treeView-node-icon")].map((use) => ({
              attrs: attrs(use),
              bbox: (() => {
                const box = use.getBBox();
                return { x: box.x, y: box.y, width: box.width, height: box.height };
              })(),
            })),
            labels: [...root.querySelectorAll("text.treeView-node-label")].map(
              (label) => ({
                value: label.textContent,
                x: label.getAttribute("x"),
                y: label.getAttribute("y"),
                hasIconUse: Boolean(
                  label.parentElement?.querySelector("use.treeView-node-icon"),
                ),
              }),
            ),
            defs: [...root.querySelectorAll(":scope > defs > g")].map((group) => {
              const svg = group.querySelector("svg");
              const iconPath = svg?.querySelector("path");
              return {
                id: group.getAttribute("id"),
                svg: attrs(svg),
                path: attrs(iconPath),
              };
            }),
          };
        })
      : null;
    const bytes = await element.screenshot({ omitBackground: true });
    const box = await element.boundingBox();
    let labelMask = null;
    if (fixture.iconOracle) {
      await page.$eval("svg", (root) => {
        const transparent = "rgba(0, 0, 0, 0)";
        for (const child of root.querySelectorAll("*")) {
          child.style.setProperty("fill", transparent, "important");
          child.style.setProperty("stroke", transparent, "important");
          child.style.setProperty("color", transparent, "important");
          child.style.setProperty("background", transparent, "important");
        }
        for (const label of root.querySelectorAll("text.treeView-node-label"))
          label.style.setProperty("fill", "#000000", "important");
      });
      labelMask = await element.screenshot({ omitBackground: true });
    }
    return { bytes, width: Math.round(box.width), height: Math.round(box.height),
      iconContract, labelMask };
  };
  for (const fixture of pixelCases) {
    const first = await capturePixel(fixture);
    const second = await capturePixel(fixture);
    if (!first.bytes.equals(second.bytes) || first.width !== second.width ||
        first.height !== second.height ||
        JSON.stringify(first.iconContract) !== JSON.stringify(second.iconContract) ||
        Boolean(first.labelMask) !== Boolean(second.labelMask) ||
        (first.labelMask && !first.labelMask.equals(second.labelMask)))
      throw new Error(`${fixture.id}: pixel/icon oracle is not byte deterministic`);
    const file = `${fixture.id}.png`;
    fs.writeFileSync(path.join(pixelDir, file), first.bytes);
    const labelMaskFile = first.labelMask ? `${fixture.id}-label-mask.png` : null;
    if (labelMaskFile)
      fs.writeFileSync(path.join(pixelDir, labelMaskFile), first.labelMask);
    pixels.push({
      id: fixture.id,
      source: fixture.source,
      file,
      width: first.width,
      height: first.height,
      sha256: sha256(first.bytes),
      ...(labelMaskFile
        ? {
            iconContract: first.iconContract,
            labelMaskFile,
            labelMaskSha256: sha256(first.labelMask),
          }
        : {}),
    });
  }
  const referencedPngs = new Set(
    pixels.flatMap((fixture) =>
      [fixture.file, fixture.labelMaskFile].filter(Boolean),
    ),
  );
  for (const name of fs.readdirSync(pixelDir).filter((name) => name.endsWith(".png")))
    if (!referencedPngs.has(name)) fs.rmSync(path.join(pixelDir, name));
  writeJson(path.join(pixelDir, "manifest.json"), {
    upstream: provenance,
    oracle: "transparent TreeView element screenshots at DPR 1",
    cases: pixels,
  });

  const accepted = grammar.filter((item) => item.accept).length;
  console.log(
    `Wrote TreeView fixtures: ${grammar.length} grammar (${accepted} accept), ` +
      `${geometry.length} geometry, ${config.length} config, ${pixels.length} pixel`,
  );
} finally {
  await browser.close();
}
