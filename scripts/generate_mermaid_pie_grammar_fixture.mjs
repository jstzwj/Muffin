import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Data-driven parser-leniency guard for the pie family. Renders each grammar
// case through real mermaid 11.16.0 in headless Chrome and records whether the
// parser accepts or rejects it, plus the resulting DB state for accepts and the
// classified error for rejects. The case corpus is transcribed from the Gate 0
// scout report (STEP0_REPORT.md §3) — the reject set MUST match the scout exactly.
//
// Reject classes (matching the upstream failure modes):
//   - lexer:        Langium lexer rejects a token (.5 leading dot, unquoted
//                    labels, `showdata` lowercase, trailing garbage)
//   - parser:       Langium parser rejects a production (missing value/colon,
//                    string value where NUMBER_PIE expected)
//   - runtime:      parser accepts but pieDb.addSection throws (negative values)
//   - no-diagram:   the detector regex /^\\s*pie/ does not match (no `pie`
//                    keyword, wrong keyword)
//
// For accepts, the DB contract is recorded: sections (label+value, insertion
// order, first-write-wins), showData flag, title/accTitle/accDescr, and the
// renderer-side draw/legend counts (drawCount = slices >= 1% of total;
// legendCount = all sections). Sections are derived from the source by the
// quoted-`"label":number` grammar and their count is asserted against the
// rendered legend count.
//
//   node scripts/generate_mermaid_pie_grammar_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/pie-grammar.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "pie-grammar.json"),
);
const chrome =
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";

const packageJson = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (packageJson.version !== "11.16.0")
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
const { default: puppeteer } = await import(
  pathToFileURL(
    path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js"),
  )
);

// Corpus transcribed from the Gate 0 scout grammar matrix (39 input forms → 40
// cases including the two negative-value runtime rejects). Every accept/reject
// verdict and the exact reject-message text come from live mermaid renders.
const cases = [
  { id: "g_empty", input: "pie" },
  { id: "g_empty_with_newlines", input: "pie\n\n\n" },
  { id: "g_only_title", input: "pie title My Pie" },
  { id: "g_accTitle", input: "pie\naccTitle: AT\n\"A\":1\n\"B\":2" },
  {
    id: "g_accDescr_text",
    input: "pie\naccDescr: some description\n\"A\":1",
  },
  {
    id: "g_accDescr_braces",
    input: "pie\naccDescr {multi word descr}\n\"A\":1",
  },
  {
    id: "g_title_and_acc",
    input: "pie title T\naccTitle: AT\naccDescr: AD\n\"A\":1\n\"B\":2",
  },
  { id: "g_showData", input: "pie showData\n\"Dogs\":38\n\"Cats\":26" },
  { id: "g_showData_lower", input: "pie showdata\n\"A\":1" },
  { id: "g_int", input: "pie\n\"A\":1\n\"B\":2" },
  { id: "g_fractional", input: "pie\n\"A\":1.5\n\"B\":2.5" },
  { id: "g_zero_value", input: "pie\n\"A\":0\n\"B\":10" },
  { id: "g_all_zero", input: "pie\n\"A\":0\n\"B\":0" },
  { id: "g_negative", input: "pie\n\"A\":-5\n\"B\":10" },
  { id: "g_negative_fractional", input: "pie\n\"A\":-0.5\n\"B\":1.5" },
  { id: "g_large", input: "pie\n\"A\":1000000\n\"B\":2000000" },
  { id: "g_leading_dot_value", input: "pie\n\"A\":.5\n\"B\":.5" },
  { id: "g_double_quoted", input: "pie\n\"A B\":1\n\"C D\":2" },
  { id: "g_single_quoted", input: "pie\n'A':1\n'B':2" },
  { id: "g_unquoted_label", input: "pie\nA:1\nB:2" },
  { id: "g_quoted_with_colon", input: "pie\n\"A:B\":1\n\"C\":2" },
  { id: "g_empty_label_quoted", input: "pie\n\"\":1\n\"B\":2" },
  { id: "g_unicode_label", input: "pie\n\"日本\":3\n\"中文\":2" },
  { id: "g_dup_key_same_val", input: "pie\n\"A\":1\n\"A\":1" },
  { id: "g_dup_key_diff_val", input: "pie\n\"A\":1\n\"A\":5" },
  { id: "g_inline_comment", input: "pie\n\"A\":1 %% comment\n\"B\":2" },
  {
    id: "g_line_comment",
    input: "pie\n%% whole line comment\n\"A\":1\n\"B\":2",
  },
  { id: "g_missing_value", input: "pie\n\"A\":\n\"B\":2" },
  { id: "g_missing_colon", input: "pie\n\"A 1\"\n\"B\":2" },
  { id: "g_missing_pie_keyword", input: "\"A\":1\n\"B\":2" },
  { id: "g_string_value", input: "pie\n\"A\":\"one\"\n\"B\":2" },
  { id: "g_trailing_garbage", input: "pie\n\"A\":1 garbage\n\"B\":2" },
  { id: "g_wrong_keyword", input: "pi title X\n\"A\":1" },
  { id: "g_no_space_after_colon", input: "pie\n\"A\":1\n\"B\":2" },
  { id: "g_extra_spaces", input: "pie\n  \"A\"  :  1  \n  \"B\"  :  2  " },
  { id: "g_tabs", input: "pie\n\t\"A\"\t:\t1\n\t\"B\"\t:\t2" },
  { id: "g_crlf", input: "pie\r\n\"A\":1\r\n\"B\":2" },
  {
    id: "g_fifteen_slices",
    input:
      "pie\n" +
      Array.from({ length: 15 }, (_, i) => `"S${i + 1}":${i + 1}`).join("\n"),
  },
  { id: "g_tiny_slice_below_1pct", input: "pie\n\"Big\":1000\n\"Tiny\":5" },
  { id: "g_tiny_slice_just_over_1pct", input: "pie\n\"Big\":1000\n\"Tiny\":11" },
];

const browser = await puppeteer.launch({
  headless: true,
  executablePath: chrome,
  args: ["--allow-file-access-from-files"],
});
try {
  const page = await browser.newPage();
  await page.goto(
    pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href,
  );
  const mermaidModule = pathToFileURL(
    path.join(mermaidRoot, "dist", "mermaid.esm.mjs"),
  ).href;
  const results = await page.evaluate(
    async ({ cases, mermaidModule }) => {
      const { default: mermaid } = await import(mermaidModule);

      // Derive the parser-level sections from the source by the
      // quoted-"label":NUMBER_PIE grammar, applying first-write-wins (pieDb
      // line 55: if (!sections.has(label)) sections.set(label, value)).
      const deriveSections = (input) => {
        const sections = [];
        let showData = false;
        let title = null;
        let accTitle = null;
        let accDescr = null;
        for (const raw of input.split(/\r?\n/)) {
          // Strip SINGLE_LINE_COMMENT (%% to end of line).
          const line = raw.replace(/%%[^\n\r]*$/, "").trim();
          if (!line) continue;
          if (/^pie\b/.test(line)) {
            if (/\bshowData\b/.test(line)) showData = true;
            const tm = line.match(/title\s+(.+)$/);
            if (tm) title = tm[1].trim();
            continue;
          }
          if (/^title\s+/.test(line)) {
            title = line.replace(/^title\s+/, "").trim();
            continue;
          }
          if (/^accTitle\s*:/.test(line)) {
            accTitle = line.replace(/^accTitle\s*:\s*/, "").trim();
            continue;
          }
          if (/^accDescr\s*:/.test(line)) {
            accDescr = line.replace(/^accDescr\s*:\s*/, "").trim();
            continue;
          }
          if (/^accDescr\s*\{/.test(line)) {
            const m = line.match(/^accDescr\s*\{([^}]*)\}/);
            if (m) accDescr = m[1].trim();
            continue;
          }
          const m = line.match(/^['"]([^'"]*)['"]\s*:\s*(-?\d+(?:\.\d+)?)/);
          if (m) {
            const label = m[1];
            const value = parseFloat(m[2]);
            if (!sections.some((s) => s.label === label))
              sections.push({ label, value });
          }
        }
        return { sections, showData, title, accTitle, accDescr };
      };

      const classify = (err) => {
        const msg = String(err?.message ?? err);
        if (msg.startsWith("No diagram type detected")) return "no-diagram";
        if (msg.includes("Lexer error")) return "lexer";
        if (msg.includes("Parse error") || msg.includes("Parsing failed"))
          return msg.includes("Lexer error") ? "lexer" : "parser";
        if (msg.includes("Negative values are not allowed")) return "runtime";
        return "unknown";
      };

      const out = [];
      for (const fixture of cases) {
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily: '"trebuchet ms", verdana, arial, sans-serif',
          look: "classic",
        });
        let svg = null;
        let err = null;
        try {
          const r = await mermaid.render(`pie-grammar-${fixture.id}`, fixture.input);
          svg = r.svg;
        } catch (e) {
          err = e;
        }
        if (err !== null) {
          out.push({
            id: fixture.id,
            input: fixture.input,
            accept: false,
            reject: {
              class: classify(err),
              message: String(err?.message ?? err),
            },
          });
          continue;
        }
        // Accept: extract DB-rendered state from the SVG.
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");
        const drawCount = root.querySelectorAll("path.pieCircle").length;
        const legendCount = root.querySelectorAll("g.legend").length;
        const titleEl = root.querySelector("text.pieTitleText");
        const renderedTitle = titleEl ? titleEl.textContent : null;
        const accTitleEl = root.querySelector("title");
        const accDescrEl = root.querySelector("desc");
        const accTitle = accTitleEl ? accTitleEl.textContent : null;
        const accDescr = accDescrEl ? accDescrEl.textContent : null;
        const derived = deriveSections(fixture.input);
        // Assert: derived section count must equal the rendered legend count
        // (the legend lists ALL sections regardless of the <1% draw filter).
        if (derived.sections.length !== legendCount) {
          throw new Error(
            `${fixture.id}: derived sections (${derived.sections.length}) != legend count (${legendCount})`,
          );
        }
        out.push({
          id: fixture.id,
          input: fixture.input,
          accept: true,
          expectedDb: {
            sections: derived.sections,
            showData: derived.showData,
            title: renderedTitle ?? derived.title,
            accTitle: accTitle ?? derived.accTitle,
            accDescr: accDescr ?? derived.accDescr,
            drawCount,
            legendCount,
          },
        });
      }
      return out;
    },
    { cases, mermaidModule },
  );

  const root = {
    upstream: {
      package: "mermaid",
      version: "11.16.0",
      notes:
        "Parser-leniency guard for the pie family. Each case is rendered live " +
        "through mermaid 11.16.0; accept/reject verdicts and exact reject " +
        "messages are the ground truth (not transcribed numbers). Reject classes: " +
        "lexer (Langium tokenization fails: .5 leading dot, unquoted labels, " +
        "lowercase showdata, trailing garbage), parser (production mismatch: " +
        "missing value/colon, string where NUMBER_PIE expected), runtime (parser " +
        "accepts but pieDb.addSection throws on negative values), no-diagram " +
        "(detector regex /^\\s*pie/ fails). For accepts, expectedDb records the " +
        "parser contract: sections (label+value, insertion order, first-write-wins " +
        "duplicates), the showData flag, title/accTitle/accDescr, and the " +
        "renderer-side draw/legend counts (drawCount = slices >= 1% of total; " +
        "legendCount = all sections). Section count is asserted to equal the " +
        "rendered legend count. accTitle/accDescr are read from the SVG <title>/\n<desc> " +
        "elements mermaid emits (the scout grammar probe left accTitle null — a " +
        "probe read-side artifact, not parser truth; this generator reads the " +
        "rendered accessibility elements directly).",
    },
    oracle:
      "pieDiagram parser accept/reject + pieDb sections/showData/title/acc state",
    cases: results,
  };
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  const accepts = results.filter((c) => c.accept).length;
  const rejects = results.length - accepts;
  const byClass = {};
  for (const c of results)
    if (!c.accept) byClass[c.reject.class] = (byClass[c.reject.class] ?? 0) + 1;
  console.log(
    `Wrote ${output} (${results.length} cases: ${accepts} accept, ${rejects} reject ${JSON.stringify(byClass)})`,
  );
} finally {
  await browser.close();
}
