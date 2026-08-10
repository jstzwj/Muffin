import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0's Jison Kanban grammar and KanbanDB behavior.
// Usage: node scripts/generate_mermaid_kanban_grammar_fixture.mjs
//   [mermaid-root] [output-json] [chrome-exe]

const EXPECTED_MERMAID_VERSION = "11.16.0";
const EXPECTED_MERMAID_MODULE_SHA256 =
  "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const EXPECTED_KANBAN_MODULE_SHA256 =
  "e72b92f9e32d5de4cdd57de283144e44bb868a82ac00871e7859f1294670ebcf";
const EXPECTED_CHROME_PRODUCT = "Chrome/151.0.7922.76";
const EXPECTED_CHROME_SHA256 =
  "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "kanban-grammar.json"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const kanbanModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.esm",
  "kanban-definition-353KVIO4.mjs",
);

const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assertEqual = (actual, expected, label) => {
  if (actual !== expected)
    throw new Error(`${label}: expected ${expected}, found ${actual}`);
};
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
assertEqual(pkg.version, EXPECTED_MERMAID_VERSION, "Mermaid version");
assertEqual(sha256(fs.readFileSync(moduleFile)), EXPECTED_MERMAID_MODULE_SHA256, "Mermaid module sha256");
assertEqual(sha256(fs.readFileSync(kanbanModuleFile)), EXPECTED_KANBAN_MODULE_SHA256, "Kanban module sha256");
assertEqual(sha256(fs.readFileSync(chrome)), EXPECTED_CHROME_SHA256, "Chrome sha256");

const init = (config, body) => `%%{init: ${JSON.stringify(config)}}%%\n${body}`;
const cases = [
  { id: "bare-section", source: "kanban\nTodo" },
  { id: "header-same-line", source: "kanban Todo" },
  { id: "reject-inline-comment-is-node-text", source: "%% pre\n\nkanban\n\n  A\n  %% comment\n    a1 %% tail\n\n  B" },
  { id: "reject-comment-blank-section-gap", source: "%% pre\n\nkanban\n\n  A\n  %% comment\n    a1\n\n  B" },
  { id: "blank-after-header", source: "kanban\n\n A" },
  { id: "blank-between-sections", source: "kanban\n A\n\n B" },
  { id: "comment-without-blank-gap", source: "kanban\n A\n %% comment\n  task" },
  { id: "sections-and-tasks", source: "kanban\n  Todo\n    docs\n    code\n  Done\n    ship" },
  { id: "tab-levels", source: "kanban\n\tTodo\n\t\tTask" },
  { id: "deep-level-is-still-task", source: "kanban\n A\n          deep\n B" },
  { id: "id-and-no-id-square", source: "kanban\n s[Section]\n  [Task]" },
  { id: "all-shapes", source: "kanban\n a[A]\n  b(B)\n  c((C))\n  d{{D}}\n  e)E(\n  f))F((\n  g(-G-)\n  h-)H(-" },
  { id: "mismatched-shape-end", source: "kanban\n s[Section)\n  t(Task]" },
  { id: "quoted-multiline", source: "kanban\n s[\"`line one\n line two`\"]\n  t[\"task one\n task two\"]" },
  { id: "unicode-markdown", source: "kanban\n s[\u65e5\u672c **Ready**]\n  t[\u4e2d\u6587 _Task_]" },
  { id: "raw-whitespace", source: "kanban\n id  [  Section  ]\n   task  " },
  { id: "inline-metadata-is-nodes", source: "kanban\n title Hello\n  accTitle: Board\n  accDescr: Details" },
  { id: "duplicate-ids", source: "kanban\n s[A]\n  x[T1]\n s[B]\n  x[T2]" },
  { id: "shallower-last-is-task", source: "kanban\n  A\n    task\nB" },
  { id: "shape-data-basic", source: "kanban\n s[Section]\n  t[Old]@{ label: New, ticket: K-1, assigned: bob, priority: High, icon: fa-star, shape: kanbanitem }" },
  { id: "shape-data-multiline", source: "kanban\n s[Section]\n  t[Old]@{\n label: New\n ticket: K-1\n assigned: bob\n priority: 2\n icon: star\n}" },
  { id: "shape-data-js-coercion", source: "kanban\n s[Section]\n  t[Old]@{ label: [a,b], ticket: [K,2], assigned: true, priority: -2, icon: 12 }" },
  { id: "shape-data-falsy-inert", source: "kanban\n s[Section]\n  t[Old]@{ label: false, ticket: 0, assigned: null, priority: 0, icon: false }" },
  { id: "shape-data-empty", source: "kanban\n s[Section]@{}\n  t[Task]" },
  { id: "shape-data-quoted-newline", source: "kanban\n s[Section]\n  t[Old]@{\n label: \"line one\n   line two\"\n}" },
  { id: "section-shape-data-projection", source: "kanban\n s[Old]@{ label: \"<b>New</b><script>x</script>\", ticket: S-1, assigned: bob, priority: High, icon: star }\n  t[Task]" },
  { id: "decorations-next-statements", source: "kanban\n s[Section]\n :::section-class\n ::icon(section-icon)\n  t[Task]\n  :::task-class\n  ::icon(task icon)" },
  { id: "decoration-last-wins", source: "kanban\n s[Section]\n  t[Task]\n  ::icon(one)\n  ::icon(two)\n  :::one\n  :::two" },
  { id: "sanitized-auto-id", source: "kanban\n [<script>x</script>]\n  t[<b>ok</b><script>x</script>]" },
  { id: "html-sanitizer", source: "kanban\n s[<img src=javascript:bad onerror=x><a href=javascript:bad onclick=x>safe</a>]\n  t[<style>x</style><i onmouseover=x>ok</i>]\n  ::icon(<script>x</script><b>icon</b>)" },
  { id: "frontmatter-title-not-db", source: "---\ntitle: Board title\n---\nkanban\n A\n  task" },
  { id: "frontmatter-mindmap-config", source: "---\nconfig:\n  mindmap:\n    padding: 7\n    maxNodeWidth: 321\n---\nkanban\n A[Section]\n  task" },
  { id: "init-mindmap-string-config", source: init({ mindmap: { padding: "6", maxNodeWidth: "333" } }, "kanban\n A[Section]\n  task") },
  { id: "init-mindmap-hex-padding", source: init({ mindmap: { padding: "0xA" } }, "kanban\n A[Section]\n  task") },
  { id: "init-mindmap-binary-padding", source: init({ mindmap: { padding: "0b1010" } }, "kanban\n A[Section]\n  task") },
  { id: "init-mindmap-octal-padding", source: init({ mindmap: { padding: "0o12" } }, "kanban\n A[Section]\n  task") },
  { id: "init-mindmap-bool-config", source: init({ mindmap: { padding: true, maxNodeWidth: false } }, "kanban\n A[Section]\n  task") },
  { id: "init-look-handdrawn", source: init({ look: "handDrawn" }, "kanban\n A\n  task") },
  { id: "comment-only-document", source: "kanban\n%% only" },
  { id: "blank-only-document", source: "kanban\n\n" },
  { id: "reject-empty-no-newline", source: "kanban" },
  { id: "reject-no-header", source: "A\n task" },
  { id: "reject-header-case-detector", source: "Kanban\nA" },
  { id: "reject-header-prefix", source: "kanbanXYZ\nA" },
  { id: "header-colon-is-node", source: "kanban:\nA" },
  { id: "reject-empty-square", source: "kanban\n []" },
  { id: "reject-unclosed-square", source: "kanban\n s[Section" },
  { id: "reject-lone-at", source: "kanban\n @" },
  { id: "reject-class-same-line", source: "kanban\n s[Section]:::red" },
  { id: "reject-icon-same-line", source: "kanban\n s[Section]::icon(star)" },
  { id: "runtime-class-before-node", source: "kanban\n:::red\n s[Section]" },
  { id: "runtime-icon-before-node", source: "kanban\n::icon(star)\n s[Section]" },
  { id: "runtime-shallower-followed", source: "kanban\n  A\n    task\nB\n  C" },
  { id: "runtime-invalid-shape-case", source: "kanban\n A\n  t[Task]@{ shape: KanbanItem }" },
  { id: "runtime-invalid-shape-underscore", source: "kanban\n A\n  t[Task]@{ shape: kanban_item }" },
  { id: "runtime-invalid-shape-type", source: "kanban\n A\n  t[Task]@{ shape: 12 }" },
  { id: "yaml-invalid", source: "kanban\n A\n  t[Task]@{ label: [ }" },
  { id: "yaml-duplicate-key", source: "kanban\n A\n  t[Task]@{ label: one, label: two }" },
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
    async ({ cases, mermaidModule, kanbanModule }) => {
      const { default: mermaid } = await import(mermaidModule);
      const { diagram } = await import(kanbanModule);
      const own = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
      const node = (value, raw) => {
        const keys = raw
          ? ["id", "level", "label", "parentId", "width", "padding", "isGroup", "type", "shape", "ticket", "priority", "assigned", "icon", "cssClasses"]
          : ["id", "level", "label", "parentId", "isGroup", "shape", "ticket", "priority", "assigned", "icon", "look"];
        return Object.fromEntries(keys.filter((key) => own(value, key)).map((key) => [key, value[key]]));
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
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          look: "classic",
        });
        try {
          await mermaid.parse(fixture.source);
          const data = diagram.db.getData();
          out.push({
            id: fixture.id,
            source: fixture.source,
            accept: true,
            expectedDb: {
              sections: diagram.db.getSections().map((value) => node(value, true)),
              nodes: data.nodes.map((value) => node(value, false)),
              config: {
                look: data.config.look,
                mindmapPadding: data.config.mindmap?.padding,
                mindmapMaxNodeWidth: data.config.mindmap?.maxNodeWidth,
              },
            },
          });
        } catch (error) {
          const message = String(error?.message ?? error).replace(/\s+/g, " ").trim();
          const line = error?.hash?.line === undefined ? 0 : Number(error.hash.line) + 1;
          const column = error?.hash?.loc?.first_column === undefined
            ? 0
            : Number(error.hash.loc.first_column) + 1;
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
              token: error?.hash?.token ?? "",
            },
          });
        }
      }
      return out;
    },
    {
      cases,
      mermaidModule: pathToFileURL(moduleFile).href,
      kanbanModule: pathToFileURL(kanbanModuleFile).href,
    },
  );

  const byId = new Map(results.map((item) => [item.id, item]));
  for (const id of ["sections-and-tasks", "all-shapes", "shape-data-basic", "duplicate-ids", "comment-only-document"])
    assertEqual(byId.get(id)?.accept, true, `${id} accept`);
  for (const id of ["reject-empty-no-newline", "reject-header-case-detector", "runtime-shallower-followed", "yaml-duplicate-key"])
    assertEqual(byId.get(id)?.accept, false, `${id} reject`);
  assertEqual(byId.get("duplicate-ids").expectedDb.nodes.length, 6, "duplicate-id expansion");
  assertEqual(byId.get("runtime-shallower-followed").reject.message, 'Items without section detected, found section ("B")', "shallower runtime");
  for (const id of ["init-mindmap-hex-padding", "init-mindmap-binary-padding", "init-mindmap-octal-padding"])
    assertEqual(byId.get(id).expectedDb.sections[0].padding, 20, `${id} Number(radix) padding`);

  const payload = {
    upstream: {
      package: "mermaid",
      version: EXPECTED_MERMAID_VERSION,
      moduleSha256: EXPECTED_MERMAID_MODULE_SHA256,
      kanbanModuleSha256: EXPECTED_KANBAN_MODULE_SHA256,
      chromeProduct: EXPECTED_CHROME_PRODUCT,
      chromeSha256: EXPECTED_CHROME_SHA256,
      parser: "Jison kanban.jison + kanbanDb.ts",
      sourceEntry: true,
    },
    oracle: "source-entry detection/accept/reject plus KanbanDB sections/nodes and parser-visible config",
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
