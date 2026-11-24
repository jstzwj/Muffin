import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0's requirementDiagram detector, parser boundary and
// compiled database: which sources parse and render, what class of error the
// others raise and where, and the requirements, elements and relationships the
// parser hands to the renderer. Several of the boundaries recorded here are
// quirks of the Jison lexer (a space after the header line is a parse error,
// single quotes and non-ASCII text are lexical errors); they are upstream's
// observable behaviour all the same.
//
//   node scripts/generate_mermaid_requirement_grammar_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/requirement-grammar.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const EXPECTED_VERSION = "11.16.0";
const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "requirement-grammar.json"),
);
const chrome = path.resolve(process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe");

const sha256 = (value) => createHash("sha256").update(value).digest("hex");
const packageJson = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
if (packageJson.version !== EXPECTED_VERSION) {
  throw new Error(`Expected Mermaid ${EXPECTED_VERSION}, found ${packageJson.version}`);
}
if (!fs.existsSync(chrome)) throw new Error(`Chrome not found: ${chrome}`);

const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const mermaidModule = pathToFileURL(moduleFile).href;
const { default: puppeteer } = await import(
  pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "esm", "puppeteer", "puppeteer.js")).href,
);

const doc = (...lines) => ["requirementDiagram", ...lines].join("\n");
const body = (lines) => `{\n${lines.join("\n")}\n}`;
const full = (id, text, risk, verify) => body([`id: ${id}`, `text: ${text}`, `risk: ${risk}`, `verifyMethod: ${verify}`]);
const req = (name, lines, keyword = "requirement") => (lines ? `${keyword} ${name} ${body(lines)}` : `${keyword} ${name}`);
const a1 = req("a", ["id: 1"]);
const b2 = req("b", ["id: 2"]);
const elemB = `element b ${body(["type: t"])}`;
const named = (name, type, lines) => `${type} ${name} ${body(lines)}`;

const cases = [
  // ---- accepted: requirement bodies
  ["plain-body", doc(req("a", ["id: 1.1", "text: the test text.", "risk: high", "verifyMethod: test"]))],
  ["every-type", doc(a1, named("functional", "functionalRequirement", ["id: 2"]), named("interface", "interfaceRequirement", ["id: 3"]), named("performance", "performanceRequirement", ["id: 4"]), named("physical", "physicalRequirement", ["id: 5"]), named("constraint", "designConstraint", ["id: 6"]))],
  ["risk-low", doc(req("a", ["id: 1", "text: t", "risk: low", "verifyMethod: test"]))],
  ["risk-medium", doc(req("a", ["id: 1", "text: t", "risk: medium", "verifyMethod: test"]))],
  ["risk-high", doc(req("a", ["id: 1", "text: t", "risk: high", "verifyMethod: test"]))],
  ["risk-capitalised", doc(req("a", ["risk: Low"]))],
  ["verify-analysis", doc(req("a", ["verifyMethod: analysis"]))],
  ["verify-demonstration", doc(req("a", ["verifyMethod: demonstration"]))],
  ["verify-inspection", doc(req("a", ["verifyMethod: inspection"]))],
  ["verify-test-capitalised", doc(req("a", ["verifyMethod: Test"]))],
  ["id-integer-then-text", doc(req("a", ["id: 1", "text: t"]))],
  ["id-decimal", doc(req("a", ["id: 1.1"]))],
  ["id-two-dots", doc(req("a", ["id: 1.2.3"]))],
  ["id-leading-zero", doc(req("a", ["id: 01"]))],
  ["id-trailing-dot", doc(req("a", ["id: 5."]))],
  ["id-only", doc(req("a", ["id: 1"]))],
  ["text-only", doc(req("a", ["text: hello world"]))],
  ["risk-only", doc(req("a", ["risk: low"]))],
  ["verify-only", doc(req("a", ["verifyMethod: test"]))],
  ["empty-body", doc(req("a", []))],
  ["body-out-of-order", doc(req("a", ["verifyMethod: test", "id: 3", "risk: low", "text: last"]))],
  ["text-double-quoted", doc(req("a", ['text: "quoted text"']))],
  ["text-trailing-space-dropped", doc("requirement a {\ntext: hello  \n}")],
  ["text-with-hash", doc(req("a", ["text: a # not a comment"]))],
  ["text-with-percent", doc(req("a", ["text: 50% more"]))],
  ["text-unicode-arrow", doc(req("a", ["text: a → b"]))],
  ["text-currency-symbol", doc(req("a", ["text: costs 5 €"]))],
  ["text-emoji", doc(req("a", ["text: ok ✅"]))],
  ["text-tab-inside", doc("requirement a {\ntext: a\tb\n}")],
  ["text-accents", doc(req("a", ["text: coût déjá"]))],
  ["text-percent-alone", doc(req("a", ["text: 100%"]))],
  ["text-hash-inside-quotes", doc(req("a", ['text: "a # b"']))],
  // ---- accepted: elements
  ["element-body", doc("element sim {\ntype: simulation\ndocref: RFC 1\n}")],
  ["element-type-only", doc("element sim {\ntype: simulation\n}")],
  ["element-docref-only", doc("element sim {\ndocref: RFC 1\n}")],
  ["element-empty-body", doc("element sim {\n}")],
  // ---- accepted: relationships
  ["every-relationship", doc(a1, b2, "element elem_a {\ntype: t\n}", "element elem_b {\ntype: t\n}", "a - contains -> b", "a - copies -> b", "a - derives -> b", "a - satisfies -> b", "a - verifies -> b", "a - refines -> b", "a - traces -> b", "elem_a <- contains - elem_b", "elem_a <- verifies - a")],
  ["relationship-right-form-src-on-right", doc(a1, elemB, "b <- contains - a")],
  ["relationship-before-nodes", doc("a - traces -> b", a1, elemB)],
  ["relationship-no-spaces", doc(a1, elemB, "a -contains-> b")],
  ["relationship-asymmetric-spaces", doc(a1, elemB, "a - contains ->b")],
  ["relationship-type-capitalised", doc(a1, elemB, "a - Contains -> b")],
  ["relationship-to-self", doc(a1, "a - refines -> a")],
  ["relationship-to-undeclared-name", doc(a1, "a - traces -> ghost")],
  ["relationship-from-undeclared-name", doc("element b {\ntype: t\n}", "ghost - traces -> b")],
  ["relationship-undeclared-to-self", doc("ghost - traces -> ghost")],
  ["relationship-type-mixed-case", doc(a1, elemB, "a - ConTaiNs -> b")],
  ["requirement-type-mixed-case", doc(req("a", ["id: 1"], "FunctionalRequirement"))],
  // ---- accepted: direction
  ["direction-tb", doc("direction TB", a1)],
  ["direction-bt", doc("direction BT", a1)],
  ["direction-rl", doc("direction RL", a1)],
  ["direction-lr", doc("direction LR", a1)],
  ["direction-lower-case-ignored", doc("direction tb", a1)],
  ["direction-after-node", doc(a1, "direction BT")],
  ["direction-between-nodes", doc(a1, "direction LR", req("b", ["id: 2"]))],
  ["direction-extra-word-ignored", doc("direction TB extra", a1)],
  ["direction-last-wins", doc("direction TB", "direction LR", a1)],
  // ---- accepted: comments and metadata
  ["comments", doc("# a hash comment", "% a percent comment", a1, "# trailing")],
  ["comment-inside-body", doc("requirement a {\n# note\nid: 1\n}")],
  ["comment-hash-no-space", doc("#note", a1)],
  ["acc-title", doc("accTitle: A title", a1)],
  ["acc-title-with-colon", doc("accTitle: R&D: requirements", a1)],
  ["acc-title-non-ascii", doc("accTitle: 标题", a1)],
  ["acc-descr-non-ascii", doc("accDescr: 说明文字", a1)],
  ["direction-after-acc-title", doc("accTitle: T", "direction LR", a1)],
  ["acc-descr-line", doc("accDescr: one line", a1)],
  ["acc-descr-block", doc("accDescr {", "first line", "second line", "}", a1)],
  ["acc-descr-brace-inside", doc("accDescr {", "first { inner", "}", a1)],
  ["acc-descr-unterminated-swallows-rest", doc("accDescr {", "first line", a1)],
  // ---- accepted: classes and styles
  ["inline-class-with-body", doc("requirement a ::: important {\nid: 1\n}", "classDef important fill:#f96")],
  ["inline-two-classes", doc("requirement a ::: x,y {\nid: 1\n}", "classDef x fill:#111", "classDef y stroke:#222")],
  ["element-inline-class", doc("element e ::: important {\ntype: t\n}", "classDef important fill:#f96")],
  ["class-line", doc(a1, "class a important", "classDef important fill:#f96")],
  ["class-undefined", doc(a1, "class a nowhere")],
  ["class-def-two-nodes", doc(a1, b2, "classDef marked fill:#f96", "class a,b marked")],
  ["class-def-after-class", doc(a1, "class a late", "classDef late fill:#abc")],
  ["class-def-duplicate-merged", doc(a1, "classDef k fill:#111", "classDef k stroke:#222", "class a k")],
  ["class-def-redefined-appends", doc(a1, "class a k", "classDef k fill:#111", "classDef k fill:#222")],
  ["class-def-style-split-at-space", doc(a1, "classDef k fill:#111,stroke-dasharray: 5,5", "class a k")],
  ["class-def-empty-quoted-id", doc(a1, 'classDef "" fill:#111')],
  ["class-def-empty-quoted-id-then-class", doc(a1, 'classDef "" fill:#ff0000', "class A fill")],
  ["class-def-empty-quoted-id-then-word", doc(a1, 'classDef "" red blue', "class A red")],
  ["style-empty-quoted-id-is-no-op", doc(a1, 'style "" fill:#ff0000')],
  ["class-missing-node-ignored", doc(a1, "classDef k fill:#111", "class ghost k")],
  ["style-missing-node-ignored", doc(a1, "style ghost fill:#111")],
  ["style-quoted-id", doc(a1, 'style "a" fill:#111')],
  ["class-comma-list", doc(a1, b2, "classDef x fill:#111", "classDef y stroke:#222", "class a,b x,y")],
  ["style-line", doc(a1, "style a fill:#ccc,stroke:#333")],
  ["style-two-nodes", doc(a1, b2, "style a,b fill:#123")],
  // ---- accepted: names
  ["name-underscore", doc(req("test_req_2", ["id: 1"]))],
  ["name-dots", doc(req("req.name", ["id: 1"]))],
  ["name-digits", doc(req("12ab", ["id: 1"]))],
  ["name-capitalised", doc(req("Req_A", ["id: 1"]))],
  ["type-keyword-capitalised", doc(req("a", ["id: 1"], "Requirement"))],
  // ---- accepted: whitespace
  ["body-line-trailing-spaces", doc("requirement a {\nid: 1  \n}")],
  ["body-line-trailing-tab", doc("requirement a {\nid: 1\t\n}")],
  ["close-brace-trailing-spaces", doc("requirement a {\nid: 1\n}  \n")],
  ["indent-tab-body", doc("requirement a {\n\tid: 1\n}")],
  ["indent-spaces-body", doc("requirement a {\n    id: 1\n}")],
  ["indent-keyword-line", doc("    requirement a {\nid: 1\n}")],
  ["space-after-colon", doc("requirement a {\nid :1\n}")],
  ["extra-space-after-keyword", doc("requirement  a {\nid: 1\n}")],
  ["blank-lines", doc("", a1, "")],
  ["leading-blank-line", `\n\nrequirementDiagram\n${a1}`],
  ["crlf", "requirementDiagram\r\nrequirement a {\r\nid: 1\r\n}"],
  // ---- accepted: duplicates and near-empty documents
  ["duplicate-requirement-first-wins", doc(req("same", ["id: 1"]), req("same", ["id: 2"]))],
  ["duplicate-element-first-wins", doc("element same {\ntype: one\n}", "element same {\ntype: two\n}")],
  ["duplicate-id-tolerated", doc(req("a", ["id: 1"]), req("b", ["id: 1"]))],
  ["element-named-like-requirement", doc(a1, "element a {\ntype: t\n}")],
  ["empty-diagram", "requirementDiagram"],
  ["only-comments", doc("# nothing", "% here")],
  ["only-direction", doc("direction BT")],
  // ---- rejected: header line
  ["reject-header-trailing-space", "requirementDiagram \nrequirement a {\nid: 1\n}"],
  ["reject-header-trailing-tab", "requirementDiagram\t\nrequirement a {\nid: 1\n}"],
  ["reject-node-on-header-line", "requirementDiagram requirement a {\nid: 1\n}"],
  ["reject-uppercase-detector", "RequirementDiagram\nrequirement a {\nid: 1\n}"],
  ["reject-prefix", "requirementDiagrams\nrequirement a {\nid: 1\n}"],
  ["reject-colon-after-header", "requirementDiagram:\nrequirement a {\nid: 1\n}"],
  // ---- rejected: body shape
  ["reject-no-body", doc("requirement test_req")],
  ["reject-element-no-body", doc("element sim")],
  ["reject-inline-class-no-body", doc("requirement a ::: important", "classDef important fill:#f96")],
  ["reject-no-newline-before-brace", doc("requirement a{id: 1}")],
  ["reject-newline-before-brace", doc("requirement a\n{\nid: 1\n}")],
  ["reject-one-line-body", doc("requirement a { id: 1 text: t }")],
  ["reject-two-members-one-line", doc("requirement a {\nid: 1 text: t\n}")],
  ["reject-unclosed-body", doc("requirement a {\nid: 1")],
  ["reject-stray-closing-brace", doc("}")],
  ["reject-double-closing-brace", doc("requirement a {\nid: 1\n}}")],
  ["reject-keyword-without-name", doc("requirement")],
  ["reject-element-without-name", doc("element")],
  ["reject-name-with-hyphen", doc(req("req-name", ["id: 1"]))],
  // ---- rejected: body members
  ["reject-id-negative", doc(req("a", ["id: -3"]))],
  ["reject-id-with-letters", doc(req("a", ["id: ABC-1.2"]))],
  ["reject-id-empty", doc("requirement a {\nid:\n}")],
  ["reject-text-empty", doc("requirement a {\ntext:\n}")],
  ["reject-text-single-quoted", doc("requirement a {\ntext: 'single quoted'\n}")],
  ["reject-text-non-ascii", doc("requirement a {\ntext: 系统应在 3 秒内响应\n}")],
  ["reject-text-brackets", doc("requirement a {\ntext: [see 1.2] (note) {x}\n}")],
  ["reject-text-with-brace", doc("requirement a {\ntext: a { b\n}")],
  ["reject-text-with-relationship", doc("requirement a {\ntext: a - contains -> b\n}")],
  ["reject-member-without-colon", doc("requirement a {\nid 1\n}")],
  ["reject-unknown-member", doc("requirement a {\ncolor: red\n}")],
  ["reject-requirement-with-type", doc("requirement a {\ntype: x\n}")],
  ["reject-element-with-id", doc("element a {\nid: 1\n}")],
  ["reject-element-with-risk", doc("element a {\nrisk: low\n}")],
  ["reject-unknown-risk", doc(req("a", ["risk: extreme"]))],
  ["reject-unknown-verify-method", doc(req("a", ["verifyMethod: guess"]))],
  ["reject-unknown-requirement-type", doc(req("a", ["id: 1"], "constraint"))],
  // ---- rejected: relationships, classes, metadata
  ["reject-relationship-without-type", doc(a1, elemB, "a --> b")],
  ["reject-relationship-both-arrows", doc(a1, elemB, "a <- contains -> b")],
  ["reject-relationship-missing-destination", doc(a1, "a - contains ->")],
  ["reject-unknown-relationship-type", doc(a1, "element b {\ntype: t\n}", "a - breaks -> b")],
  ["reject-relationship-without-arrows", doc(a1, "a contains a")],
  ["reject-style-without-declarations", doc(a1, "style a")],
  ["reject-style-quoted-value", doc(a1, 'style a "fill:#111"')],
  ["reject-style-trailing-comma", doc(a1, "style a fill:#111,")],
  ["reject-class-without-class-list", doc(a1, "class a")],
  ["reject-class-def-without-styles", doc(a1, "classDef k")],
  ["reject-class-def-quoted-styles", doc(a1, 'classDef k ""', "class a k")],
  ["reject-class-node-with-empty-quoted-class", doc(a1, 'class A ""')],
  ["reject-class-empty-quoted-node", doc(a1, "classDef C fill:#f00", 'class "" C')],
  ["reject-acc-title-empty", doc("accTitle:", a1)],
  ["reject-acc-title-spaces-only", doc("accTitle:   ", a1)],
  ["reject-acc-descr-empty", doc("accDescr:", a1)],
  ["reject-two-relationships-one-line", doc(a1, "a - contains -> b - contains -> c")],
  ["reject-text-escaped-quotes", doc("requirement a {\ntext: \"a \\\"quoted\\\" word\"\n}")],
  ["reject-text-with-hyphen", doc(req("a", ["text: well-formed"]))],
  ["reject-name-non-ascii", doc(req("名称", ["id: 1"]))],
  ["reject-inline-title", doc("title: not allowed", a1)],
  ["reject-unknown-line", doc(a1, "something else here")],
  ["reject-id-outside-body", doc("id: 1")],
  ["reject-direction-value", doc("direction diagonally", a1)],
  ["reject-block-comment", doc("/* note */", a1)],
];

const browser = await puppeteer.launch({
  headless: "new",
  executablePath: chrome,
  args: ["--allow-file-access-from-files", "--disable-gpu"],
});
const hostPage = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;

const generated = [];
for (const [id, source] of cases) {
  const page = await browser.newPage();
  await page.goto(hostPage);
  const result = await page.evaluate(
    async ({ id, source, moduleUrl }) => {
      const { default: mermaid } = await import(moduleUrl);
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict", deterministicIds: true, deterministicIDSeed: "requirement-grammar" });

      const classify = (error) => {
        const message = String(error?.message ?? error);
        if (message.startsWith("No diagram type detected")) return "no-diagram";
        if (message.includes("Lexical error")) return "lexer";
        if (message.includes("Parse error")) return "parser";
        return "runtime";
      };
      const reject = (error) => {
        const message = String(error?.message ?? error);
        // The line upstream shows the user ("Parse error on line N"). It is not
        // always error.hash.line + 1: when the parser trips after a newline, the
        // hash points at the next line while the message names the previous one.
        const shown = /error on line (\d+)/.exec(message);
        return {
          class: classify(error),
          message,
          ...(shown ? { messageLine: Number(shown[1]) } : {}),
          ...(Number.isInteger(error?.hash?.line) ? { line: error.hash.line + 1 } : {}),
          ...(Number.isInteger(error?.hash?.loc?.first_column) ? { column: error.hash.loc.first_column + 1 } : {}),
        };
      };

      let diagram;
      try {
        diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
      } catch (error) {
        return { id, source, accept: false, reject: reject(error) };
      }

      const db = diagram.db;
      const data = {
        accTitle: db.getAccTitle(),
        accDescription: db.getAccDescription(),
        direction: db.getDirection(),
        requirements: [...db.getRequirements().values()],
        elements: [...db.getElements().values()],
        relationships: db.getRelationships().map(({ type, src, dst }) => ({ type, src, dst })),
        // classDef table: id -> accumulated declarations. The Map keeps
        // insertion order, which setClass and retroactive classDef depend on.
        classes: [...db.getClasses()].map(([id, definition]) => ({ id, styles: definition.styles })),
      };

      let renderAccept = true;
      let renderReject = null;
      try {
        await mermaid.render(`requirement-${id}`, source);
      } catch (error) {
        renderAccept = false;
        renderReject = reject(error);
      }
      return { id, source, accept: true, expectedDb: data, renderAccept, ...(renderReject ? { renderReject } : {}) };
    },
    { id, source, moduleUrl: mermaidModule },
  );
  generated.push(result);
  await page.close();
}
await browser.close();

const ids = new Set(generated.map((entry) => entry.id));
if (ids.size !== generated.length) throw new Error("duplicate case id");
for (const entry of generated) {
  // ids state upstream's answer, so a mislabelled case is a bug in this corpus
  if (entry.id.startsWith("reject-") === entry.accept) {
    throw new Error(
      `case ${entry.id} is ${entry.accept ? "accepted" : "rejected"} by upstream despite its id` +
        (entry.reject ? `: ${entry.reject.message.replace(/\n/g, " ").slice(0, 200)}` : ""),
    );
  }
}

const fixture = {
  upstream: {
    package: "mermaid",
    version: packageJson.version,
    license: packageJson.license,
    moduleSha256: sha256(fs.readFileSync(moduleFile)),
  },
  cases: generated,
};
fixture.fixtureSha256 = sha256(JSON.stringify(fixture));
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
const accepted = generated.filter((entry) => entry.accept);
console.log(
  `Wrote ${output}: ${generated.length} cases, ${accepted.length} DB accepts, ` +
    `${accepted.filter((entry) => entry.renderAccept).length} render accepts, ` +
    `${generated.length - accepted.length} rejects ` +
    `(${[...new Set(generated.filter((entry) => !entry.accept).map((entry) => entry.reject.class))].join(", ")})`,
);
