import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0's source-entry detector, generated Jison parser,
// first-read timeline DB, metadata omissions, and live timeline config values.
// Each case gets a fresh page because upstream clear() does not reset task IDs.

const EXPECTED_MERMAID_VERSION = "11.16.0";
const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ??
    path.join("tests", "fixtures", "mermaid", "timeline-grammar.json"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);

const sha256 = (bytes) =>
  createHash("sha256").update(bytes).digest("hex");
const pkg = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (pkg.version !== EXPECTED_MERMAID_VERSION)
  throw new Error(`Expected Mermaid ${EXPECTED_MERMAID_VERSION}, found ${pkg.version}`);
if (!fs.existsSync(chrome)) throw new Error(`Chrome not found: ${chrome}`);

const mermaidModule = pathToFileURL(
  path.join(mermaidRoot, "dist", "mermaid.esm.mjs"),
).href;
const moduleSha256 = sha256(
  fs.readFileSync(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")),
);
const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(
      path.dirname(mermaidRoot),
      "puppeteer",
      "lib",
      "esm",
      "puppeteer",
      "puppeteer.js",
    ),
  ).href
);

const cases = [
  ["basic", "timeline\nA\n: E\nB"],
  ["header-lr", "timeline LR\nA"],
  ["header-td", "timeline TD\nA"],
  ["header-direction-case-insensitive", "timeline tD\nA"],
  ["header-extra-is-task", "timeline LR junk\nA"],
  ["header-horizontal-is-task", "timeline horizontal\nA"],
  ["header-js-ascii-boundary", "timeline\u00e9\nA"],
  ["empty-document", "timeline"],
  ["leading-space-newline", " \n\ttimeline\nA"],
  ["leading-nbsp-detector", "\u00a0timeline\nA"],
  ["leading-bom", "\ufefftimeline\nA"],
  ["directive-before-header", "%%{init:{\"theme\":\"dark\"}}%%\ntimeline\nA"],
  ["percent-comment-before-header", "%% comment\ntimeline\nA"],
  ["title", "timeline\ntitle Heading\nA"],
  ["title-last-wins", "timeline\ntitle First\ntitle Second\nA"],
  ["title-spacing-preserved", "timeline\ntitle  Heading  \nA"],
  ["title-tab", "timeline\ntitle\tHeading\nA"],
  [
    "ecmascript-whitespace-title",
    "timeline\n\u00a0\ufefftitle\u00a0\u00a0Heading\u00a0\ufeff\nA",
  ],
  ["title-crosses-newline", "timeline\ntitle\nA"],
  ["bare-title-is-task", "timeline\ntitle"],
  ["frontmatter-title-db-inert", "---\ntitle: Front\n---\ntimeline\nA"],
  [
    "inline-title-wins-frontmatter",
    "---\ntitle: Front\n---\ntimeline\ntitle Inline\nA",
  ],
  ["section-and-binding", "timeline\nsection S\nA\nsection T\nB"],
  ["duplicate-sections", "timeline\nsection S\nA\nsection S\nB"],
  ["section-spacing-preserved", "timeline\nsection  S  \nA"],
  ["section-tab", "timeline\nsection\tS\nA"],
  ["section-crosses-newline", "timeline\nsection\nA"],
  ["bare-section-is-task", "timeline\nsection"],
  ["section-hash-is-text", "timeline\nsection S#x\nA"],
  ["task-trailing-space", "timeline\nA  "],
  ["task-semicolon-literal", "timeline;A"],
  ["task-quotes-html-literal", "timeline\n\"<b>A</b>\""],
  ["hash-comment-truncates-task", "timeline\nA # tail\n# whole\nB"],
  ["single-percent-comment-after-header", "timeline\n% ignored\nA"],
  ["one-char-percent-comment-quirk", "timeline\nA%% ignored\nB"],
  ["two-char-percent-not-comment", "timeline\nAB%% kept\nB"],
  ["spaced-percent-not-comment", "timeline\nA %% kept\nB"],
  ["event-same-line", "timeline\nA : E"],
  ["event-two-spaces-preserved", "timeline\nA\n:  E"],
  ["event-tab", "timeline\nA\n:\tE"],
  ["event-inner-colons", "timeline\nA\n: E:x\n: F: G"],
  ["event-hash-is-text", "timeline\nA\n: E#x"],
  ["event-crosses-newline", "timeline\nA\n:\nE"],
  [
    "event-after-new-section-attaches-last-task",
    "timeline\nsection A\nT\nsection B\n: E\nU",
  ],
  ["acc-single-line", "timeline\naccTitle:  AT  \naccDescr:  AD  \nA"],
  [
    "acc-ecmascript-trim",
    "timeline\naccTitle:\u00a0\ufeff AT \u00a0\ufeff\naccDescr:\u00a0\ufeff AD \u00a0\ufeff\nA",
  ],
  ["acc-values-cross-newline", "timeline\naccTitle:\nAT\naccDescr:\nAD\nA"],
  ["acc-block", "timeline\naccDescr {  first\n second  }\nA"],
  [
    "acc-block-common-db-deindent",
    "timeline\naccDescr {\n  first\n\tsecond\n\u00a0\ufeffthird\n\n  fourth\n}\nA",
  ],
  ["acc-empty-block", "timeline\naccDescr {}\nA"],
  ["acc-block-tail-is-task", "timeline\naccDescr {D}Tail"],
  [
    "metadata-sanitize-active-content",
    "timeline\ntitle <script>alert(1)</script><style>bad</style><b onclick=\"bad()\">Title</b>\naccTitle: <script>bad</script>  <img src=x onerror=\"bad()\"><i>AT</i>\naccDescr {<script>bad()</script><style>bad</style><u>AD</u>}\nA",
  ],
  ["metadata-sanitize-unclosed-script", "timeline\ntitle <script>bad"],
  [
    "metadata-sanitize-unclosed-style",
    "timeline\naccTitle: <style>bad\nA",
  ],
  [
    "metadata-sanitize-urls-and-safe-markup",
    "timeline\ntitle <a href=\"javascript:alert(1)\">Bad</a><a href=\"https://example.com\">Good</a>\naccTitle: <strong class=\"ok\">AT</strong>\naccDescr: <em id=\"safe\">AD</em>\nA",
  ],
  ["ascii-casefold-section", "timeline\n\u017fection Weird\nA"],
  ["ascii-casefold-acc-descr", "timeline\naccDe\u017fcr: value\nA"],
  [
    "config-init-number",
    '%%{init:{"timeline":{"leftMargin":77,"padding":33,"useMaxWidth":false,"disableMulticolor":true}}}%%\ntimeline\nA\nB',
    true,
  ],
  [
    "config-init-string-coercion",
    '%%{init:{"timeline":{"leftMargin":"77","padding":"33","useMaxWidth":"false","disableMulticolor":"false"}}}%%\ntimeline\nA\nB',
    true,
  ],
  [
    "config-init-zero",
    '%%{init:{"timeline":{"leftMargin":0,"padding":0,"useMaxWidth":0,"disableMulticolor":0}}}%%\ntimeline\nA',
    true,
  ],
  [
    "config-init-null-defaults",
    '%%{init:{"timeline":{"leftMargin":null,"padding":null,"useMaxWidth":null,"disableMulticolor":null}}}%%\ntimeline\nA',
    true,
  ],
  [
    "config-frontmatter",
    "---\nconfig:\n  timeline:\n    leftMargin: 81\n    padding: 31\n    useMaxWidth: false\n    disableMulticolor: true\n---\ntimeline\nA",
    true,
  ],
  [
    "config-multiple-init-last-wins",
    '%%{init:{"timeline":{"leftMargin":61}}}%%\n%%{init:{"timeline":{"leftMargin":72}}}%%\ntimeline\nA',
    true,
  ],
  [
    "config-init-wins-frontmatter",
    '---\nconfig:\n  timeline:\n    leftMargin: 81\n---\n%%{init:{"timeline":{"leftMargin":92}}}%%\ntimeline\nA',
    true,
  ],
  [
    "config-top-level-ignored",
    '%%{init:{"leftMargin":77,"padding":33,"useMaxWidth":false}}%%\ntimeline\nA',
    true,
  ],
  ["reject-uppercase-detector", "TIMELINE\nA"],
  ["reject-leading-hash-detector", "# comment\ntimeline\nA"],
  ["reject-leading-single-percent-detector", "% comment\ntimeline\nA"],
  ["leading-newline-preprocessed", "\ntimeline\nA"],
  ["reject-adjacent-header", "timelinex\nA"],
  ["reject-header-ascii-word", "timeline_\nA"],
  ["reject-second-header", "timeline\nTIMELINE"],
  ["reject-colon-without-space", "timeline\nA:B"],
  ["reject-colon-after-header", "timeline:B"],
  [
    "reject-colon-decorated-frontmatter",
    "---\ntitle: Front\n---\ntimeline\nA:B",
  ],
  [
    "reject-colon-decorated-directive",
    '%%{init:{"theme":"dark"}}%%\ntimeline\nA:B',
  ],
  ["reject-empty-event", "timeline\nA\n: "],
  ["runtime-event-before-task", "timeline\n: E"],
  ["runtime-event-after-section-before-task", "timeline\nsection S\n: E"],
  ["reject-acc-title-eof", "timeline\naccTitle:"],
  ["reject-acc-descr-eof", "timeline\naccDescr:"],
  ["reject-acc-block-unclosed", "timeline\naccDescr { value"],
];

const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu"],
});
const hostPage = pathToFileURL(
  path.join(path.dirname(mermaidRoot), "..", "index.html"),
).href;

const generated = [];
for (const [id, source, captureConfig = false] of cases) {
  const page = await browser.newPage();
  await page.goto(hostPage);
  const result = await page.evaluate(
    async ({ id, source, moduleUrl, captureConfig }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: "default",
        look: "classic",
      });
      const classify = (error) => {
        const message = String(error?.message ?? error);
        if (message.startsWith("No diagram type detected")) return "no-diagram";
        if (message.includes("Lexical error")) return "lexer";
        if (message.includes("Parse error")) return "parser";
        return "runtime";
      };
      const rejected = (error) => ({
        id,
        source,
        accept: false,
        reject: {
          class: classify(error),
          message: String(error?.message ?? error),
          ...(Number.isInteger(error?.hash?.line)
            ? { line: error.hash.line + 1 }
            : {}),
          ...(Number.isInteger(error?.hash?.loc?.first_column)
            ? { column: error.hash.loc.first_column + 1 }
            : {}),
        },
      });

      let diagram;
      try {
        diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
      } catch (error) {
        return rejected(error);
      }

      try {
        const db = diagram.db;
        // This is intentionally the sole getTasks() call. Upstream appends the
        // same raw tasks again on every subsequent call.
        const tasks = db.getTasks().map((task) => ({
          id: task.id,
          section: task.section,
          type: task.type,
          task: task.task,
          score: task.score,
          events: [...task.events],
        }));
        const common = db.getCommonDb();
        const item = {
          id,
          source,
          accept: true,
          expectedDb: {
            direction: db.getDirection(),
            title: common.getDiagramTitle(),
            accTitle: common.getAccTitle(),
            accDescr: common.getAccDescription(),
            sections: [...db.getSections()],
            tasks,
          },
        };

        try {
          const { svg } = await mermaid.render(`timeline-${id}`, source);
          const host = document.createElement("div");
          host.innerHTML = svg;
          item.expectedRender = {
            accept: true,
            svgTitles: [...host.querySelectorAll("title")].map(
              (node) => node.textContent ?? "",
            ),
            svgDescriptions: [...host.querySelectorAll("desc")].map(
              (node) => node.textContent ?? "",
            ),
            texts: [...host.querySelectorAll("text")].map(
              (node) => node.textContent ?? "",
            ),
          };
          if (captureConfig) {
            const config = mermaid.mermaidAPI.getConfig().timeline ?? {};
            item.effectiveTimelineConfig = Object.fromEntries(
              ["leftMargin", "padding", "useMaxWidth", "disableMulticolor"]
                .filter((key) => Object.hasOwn(config, key))
                .map((key) => [key, config[key]]),
            );
          }
        } catch (error) {
          item.expectedRender = {
            accept: false,
            class: classify(error),
            message: String(error?.message ?? error),
          };
        }
        return item;
      } catch (error) {
        return rejected(error);
      }
    },
    { id, source, moduleUrl: mermaidModule, captureConfig },
  );
  generated.push(result);
  await page.close();
}
await browser.close();

const fixture = {
  upstream: {
    package: "mermaid",
    version: EXPECTED_MERMAID_VERSION,
    module: "dist/mermaid.esm.mjs",
    moduleSha256,
  },
  oracle:
    "source-entry detector + generated timeline Jison + first-read timelineDb + render metadata/config",
  generator: "scripts/generate_mermaid_timeline_grammar_fixture.mjs",
  cases: generated,
};
fixture.fixtureSha256 = sha256(JSON.stringify(fixture));
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
console.log(
  `Wrote ${output} (${generated.length} cases, sha256 ${fixture.fixtureSha256})`,
);
