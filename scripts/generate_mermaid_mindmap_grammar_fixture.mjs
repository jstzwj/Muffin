import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0's Jison Mindmap grammar and MindmapDB behavior.
// Usage: node scripts/generate_mermaid_mindmap_grammar_fixture.mjs
//   [mermaid-root] [output-json] [chrome-exe]

const EXPECTED_MERMAID_VERSION = "11.16.0";
const EXPECTED_MERMAID_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_MINDMAP_MODULE_SHA256 =
  "584e20b0980902d5749171aca4d48dcdfe9e674df21cdbade0077e7214511ea8";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "mindmap-grammar.json"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const mindmapModuleFile = path.join(
  mermaidRoot, "dist", "chunks", "mermaid.esm",
  "mindmap-definition-CKRMFO7I.mjs",
);

const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assertEqual = (actual, expected, label) => {
  if (actual !== expected)
    throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assertEqual(pkg.version, EXPECTED_MERMAID_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MERMAID_MODULE_SHA256, "Mermaid module sha256");
assertEqual(sha256(fs.readFileSync(mindmapModuleFile)), EXPECTED_MINDMAP_MODULE_SHA256, "Mindmap module sha256");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome sha256");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const frontmatter = (yaml, body) => `---\n${yaml}\n---\n${body}`;
const cases = [
  { id: "bare-root", source: "mindmap\n root" },
  { id: "header-same-line", source: "mindmap root" },
  { id: "leading-comment", source: "%% pre\nmindmap\n root\n  child" },
  { id: "leading-preamble-post-header-blank", source: "%% pre\n\nmindmap\n\n  root" },
  { id: "post-header-blank-no-preamble", source: "mindmap\n\n  root" },
  { id: "crlf", source: "mindmap\r\n root\r\n  child" },
  { id: "blank-and-comments", source: "%% pre\n\nmindmap\n\n  root\n  %% note\n    child\n\n" },
  { id: "blank-between-nodes", source: "mindmap\n root\n\n  child" },
  { id: "comment-between-nodes", source: "mindmap\n root\n %% note\n  child" },
  { id: "tab-levels", source: "mindmap\n\troot\n\t\tchild\n\t\t\tleaf" },
  { id: "base-indent-normalized", source: "mindmap\n      root\n        child\n          leaf" },
  { id: "skip-indent-level", source: "mindmap\n root\n          child\n                    leaf" },
  { id: "dedent-nearest-parent", source: "mindmap\n root\n  a\n   aa\n  b\n   bb" },
  { id: "second-root-runtime", source: "mindmap\n root\n other" },
  { id: "shallower-than-base-runtime", source: "mindmap\n   root\n child" },
  { id: "all-no-id-shapes", source: "mindmap\n root\n  [Rect]\n  (Rounded)\n  ((Circle))\n  )Cloud(\n  ))Bang((\n  {{Hexagon}}\n  (-Cloud2-)\n  -)Bang2(-" },
  { id: "all-id-shapes", source: "mindmap\n root\n  a[Rect]\n  b(Rounded)\n  c((Circle))\n  d)Cloud(\n  e))Bang((\n  f{{Hexagon}}\n  g(-Cloud2-)\n  h-)Bang2(-" },
  { id: "mismatched-shape-end", source: "mindmap\n root\n  a[Rect)\n  b(Round]" },
  { id: "quoted-markdown", source: "mindmap\n root[\"`line **one**`\"]\n  child[\"quoted text\"]" },
  { id: "quoted-multiline", source: "mindmap\n root[\"`line one\n line two`\"]\n  child[\"task one\n task two\"]" },
  { id: "unicode-markdown", source: "mindmap\n root[日本 **Ready**]\n  child[中文 _Task_]" },
  { id: "raw-whitespace", source: "mindmap\n id  [  Root  ]\n    child  " },
  { id: "duplicate-node-ids", source: "mindmap\n same[A]\n  same[B]\n  same[C]" },
  { id: "decorations", source: "mindmap\n root[Root]\n ::icon(root-icon)\n :::root-class\n  child[Child]\n  ::icon(child icon)\n  :::child-class" },
  { id: "decoration-last-wins", source: "mindmap\n root\n ::icon(one)\n ::icon(two)\n :::one\n :::two" },
  { id: "icon-before-node-runtime", source: "mindmap\n::icon(star)\n root" },
  { id: "class-before-node-runtime", source: "mindmap\n:::red\n root" },
  { id: "html-sanitizer", source: "mindmap\n root[<img src=javascript:bad onerror=x><a href=javascript:bad onclick=x>safe</a>]\n ::icon(<script>x</script><b>icon</b>)\n :::<i onmouseover=x>klass</i>" },
  { id: "sanitized-auto-id", source: "mindmap\n [<script>x</script><b>Root</b>]\n  [<a href=javascript:bad onclick=x>Child</a>]" },
  { id: "safe-html-anchor", source: "mindmap\n root[\"<a href='https://example.org/path?q=1'>HtmlLink</a>\"]" },
  { id: "empty-html-anchor", source: "mindmap\n root[\"<a href=''>EmptyLink</a>\"]" },
  { id: "mixed-html-anchor", source: "mindmap\n root[\"pre <a href='https://example.org'>Link</a> post\"]" },
  { id: "unsafe-html-anchor", source: "mindmap\n root[\"<a href='javascript:alert(1)'>BadLink</a>\"]" },
  { id: "markdown-link-is-not-html-anchor", source: "mindmap\n root[\"[MarkdownLink](https://example.org)\"]" },
  { id: "metadata-are-nodes", source: "mindmap\n title Hello\n  accTitle: Board\n  accDescr: Details" },
  { id: "shape-data-is-grammar", source: "mindmap\n root@{ shape: rect }\n  child" },
  { id: "frontmatter-title-not-db", source: frontmatter("title: Board title", "mindmap\n root\n  child") },
  { id: "twelve-root-children", source: "mindmap\n root\n" + Array.from({ length: 12 }, (_, i) => `  child${i}`).join("\n") },
  { id: "init-padding-string", source: init({ mindmap: { padding: "6", maxNodeWidth: "333", useMaxWidth: false } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-radix", source: init({ mindmap: { padding: "0xA" } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-binary", source: init({ mindmap: { padding: "0b1010" } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-octal", source: init({ mindmap: { padding: "0o12" } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-decimal", source: init({ mindmap: { padding: "2.5" } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-exponent", source: init({ mindmap: { padding: "1e1" } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-bool", source: init({ mindmap: { padding: true, maxNodeWidth: false } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-array", source: init({ mindmap: { padding: [7], maxNodeWidth: [321] } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-object", source: init({ mindmap: { padding: {}, maxNodeWidth: {} } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-usemax-invalid-containers", source: init({ mindmap: { useMaxWidth: [false] } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-invalid-and-scalar-mixed", source: init({ mindmap: { padding: {}, maxNodeWidth: "333", useMaxWidth: false } }, "mindmap\n root\n  child[Rect]") },
  { id: "init-padding-null", source: init({ mindmap: { padding: null, maxNodeWidth: null } }, "mindmap\n root\n  child[Rect]") },
  { id: "frontmatter-config", source: frontmatter("config:\n  mindmap:\n    padding: 7\n    maxNodeWidth: 321\n    useMaxWidth: false", "mindmap\n root\n  child[Rect]") },
  { id: "init-look-handdrawn", source: init({ look: "handDrawn" }, "mindmap\n root\n  child") },
  { id: "theme-redux-default-shape", source: init({ theme: "redux" }, "mindmap\n root\n  child") },
  { id: "layout-user-elk", source: init({ layout: "elk" }, "mindmap\n root\n  child") },
  { id: "layout-empty", source: init({ layout: "" }, "mindmap\n root\n  child") },
  { id: "empty-document", source: "mindmap" },
  { id: "single-newline-document", source: "mindmap\n" },
  { id: "comment-only-document", source: "mindmap\n%% only" },
  { id: "blank-only-document", source: "mindmap\n\n" },
  { id: "reject-no-header", source: "root\n child" },
  { id: "reject-header-case-detector", source: "Mindmap\n root" },
  { id: "reject-header-prefix", source: "mindmapXYZ\n root" },
  { id: "reject-unclosed-shape", source: "mindmap\n root[Open" },
  { id: "reject-leading-comment-unclosed-shape", source: "%% pre\nmindmap\n root[Open" },
  { id: "reject-frontmatter-unclosed-shape", source: frontmatter("title: Board", "mindmap\n root[Open") },
  { id: "reject-init-unclosed-shape", source: init({ mindmap: { padding: 7 } }, "mindmap\n root[Open") },
  { id: "class-same-line-is-literal", source: "mindmap\n root:::red" },
  { id: "icon-same-line-is-shape", source: "mindmap\n root::icon(star)" },
  { id: "lone-shape-end-is-literal", source: "mindmap\n ]" },
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
  assertEqual(await browser.version(), EXPECTED_CHROME_PRODUCT, "Chrome product");
  const page = await browser.newPage();
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const results = await page.evaluate(
    async ({ cases, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      const own = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
      const scalar = (value) => {
        if (typeof value === "number" && !Number.isFinite(value))
          return { $number: Number.isNaN(value) ? "NaN" : value < 0 ? "-Infinity" : "Infinity" };
        if (Object.is(value, -0)) return { $number: "-0" };
        if (Array.isArray(value)) return value.map(scalar);
        if (value && typeof value === "object")
          return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, scalar(item)]));
        return value;
      };
      const rawNode = (value) => {
        if (!value) return null;
        const fields = ["id", "nodeId", "level", "descr", "type", "width", "padding", "isRoot", "icon", "class", "section"];
        const out = Object.fromEntries(fields.filter((key) => own(value, key)).map((key) => [key, scalar(value[key])]));
        const template = document.createElement("template");
        template.innerHTML = value.descr ?? "";
        let visibleOffset = 0;
        out.anchors = [];
        const walk = (node) => {
          if (node.nodeType === Node.TEXT_NODE) {
            visibleOffset += node.data.length;
            return;
          }
          const href = node.nodeType === Node.ELEMENT_NODE && node.localName === "a"
            ? node.getAttribute("href") : null;
          const start = visibleOffset;
          for (const child of node.childNodes) walk(child);
          if (href !== null)
            out.anchors.push({ href, label: node.textContent ?? "", start, length: visibleOffset - start });
        };
        walk(template.content);
        out.children = (value.children ?? []).map(rawNode);
        return out;
      };
      const flatNode = (value) => {
        const fields = ["id", "domId", "label", "labelType", "isGroup", "shape", "width", "height", "padding", "cssClasses", "cssStyles", "look", "icon", "level", "nodeId", "type", "section"];
        return Object.fromEntries(fields.filter((key) => own(value, key)).map((key) => [key, scalar(value[key])]));
      };
      const edge = (value) => {
        const fields = ["id", "start", "end", "type", "curve", "thickness", "look", "classes", "depth", "section"];
        return Object.fromEntries(fields.filter((key) => own(value, key)).map((key) => [key, scalar(value[key])]));
      };
      const classify = (error, message) => {
        if (message.startsWith("No diagram type detected")) return "no-diagram";
        if (error?.name === "YAMLException") return "yaml";
        if (message.startsWith("Lexical error")) return "lexer";
        if (message.startsWith("Parse error")) return "parser";
        return "runtime";
      };
      const out = [];
      for (const fixture of cases) {
        mermaid.initialize({ startOnLoad: false, securityLevel: "strict", theme: "default", look: "classic" });
        try {
          // parse() is the source-entry operation which resets and applies the
          // frontmatter/directive config. getDiagramFromText() then gives us
          // the otherwise-private per-parse MindmapDB instance while reusing
          // that effective config.
          await mermaid.parse(fixture.source);
          const parsed = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
          const data = parsed.db.getData();
          out.push({
            id: fixture.id,
            source: fixture.source,
            accept: true,
            expectedDb: {
              root: rawNode(data.rootNode),
              nodes: data.nodes.map(flatNode),
              edges: data.edges.map(edge),
              config: scalar({
                theme: data.config.theme,
                look: data.config.look,
                layout: data.config.layout,
                mindmapPadding: data.config.mindmap?.padding,
                mindmapMaxNodeWidth: data.config.mindmap?.maxNodeWidth,
                mindmapUseMaxWidth: data.config.mindmap?.useMaxWidth,
              }),
            },
          });
        } catch (error) {
          const message = String(error?.message ?? error).replace(/\s+/g, " ").trim();
          const line = error?.hash?.line === undefined ? 0 : Number(error.hash.line) + 1;
          const column = error?.hash?.loc?.first_column === undefined ? 0 : Number(error.hash.loc.first_column) + 1;
          out.push({
            id: fixture.id,
            source: fixture.source,
            accept: false,
            reject: {
              class: classify(error, message),
              name: error?.name ?? "Error",
              message,
              line,
              column,
              token: String(error?.hash?.token ?? ""),
            },
          });
        }
      }
      return out;
    },
    {
      cases,
      mermaidModule: pathToFileURL(moduleFile).href,
    },
  );

  const byId = new Map(results.map((item) => [item.id, item]));
  for (const id of ["bare-root", "all-id-shapes", "decorations", "init-padding-radix"])
    assertEqual(byId.get(id)?.accept, true, `${id} accept`);
  for (const id of ["second-root-runtime", "icon-before-node-runtime", "reject-header-case-detector"])
    assertEqual(byId.get(id)?.accept, false, `${id} reject`);
  assertEqual(byId.get("init-padding-radix").expectedDb.root.children[0].padding, 20, "radix padded child");
  assertEqual(byId.get("init-padding-binary").expectedDb.root.children[0].padding, 20, "binary padded child");
  assertEqual(byId.get("init-padding-octal").expectedDb.root.children[0].padding, 20, "octal padded child");
  assertEqual(byId.get("init-padding-decimal").expectedDb.root.children[0].padding, 5, "decimal padded child");
  assertEqual(byId.get("init-padding-exponent").expectedDb.root.children[0].padding, 20, "exponent padded child");
  assertEqual(byId.get("safe-html-anchor").expectedDb.root.anchors[0].href, "https://example.org/path?q=1", "safe HTML anchor href");
  assertEqual(byId.get("mixed-html-anchor").expectedDb.root.anchors[0].start, 4, "mixed HTML anchor start");
  assertEqual(byId.get("mixed-html-anchor").expectedDb.root.anchors[0].length, 4, "mixed HTML anchor length");
  assertEqual(byId.get("unsafe-html-anchor").expectedDb.root.anchors.length, 0, "unsafe HTML anchor removed");
  assertEqual(byId.get("markdown-link-is-not-html-anchor").expectedDb.root.anchors.length, 0, "Markdown link not HTML anchor");
  assertEqual(byId.get("twelve-root-children").expectedDb.nodes[12].section, 0, "section modulo 11");

  const payload = {
    upstream: {
      package: "mermaid",
      version: EXPECTED_MERMAID_VERSION,
      moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
      mindmapModuleSha256: EXPECTED_MINDMAP_MODULE_SHA256,
      chromeProduct: EXPECTED_CHROME_PRODUCT,
      chromeSha256: EXPECTED_CHROME_SHA256,
      parser: "Jison mindmap.jison + mindmapDb.ts",
      sourceEntry: true,
    },
    oracle: "source-entry detection/accept/reject plus MindmapDB tree, flattened nodes/edges and parser-visible config",
    cases: results,
  };
  payload.fixtureSha256 = sha256(JSON.stringify(payload));
  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  const accepted = results.filter((item) => item.accept).length;
  console.log(`Wrote ${output} (${results.length} cases: ${accepted} accept, ${results.length - accepted} reject, sha=${payload.fixtureSha256})`);
} finally {
  await browser.close();
}
