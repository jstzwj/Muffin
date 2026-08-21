import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

// Freezes Mermaid 11.16.0's Gantt detector, Jison grammar, compiled task DB,
// metadata, link sanitization, and parser/renderer error boundary.

const EXPECTED_VERSION = "11.16.0";
const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "gantt-grammar.json"),
);
const chrome = path.resolve(
  process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe",
);

const sha256 = (value) => createHash("sha256").update(value).digest("hex");
const readJson = (file) => JSON.parse(fs.readFileSync(file, "utf8"));
const packageJson = readJson(path.join(mermaidRoot, "package.json"));
if (packageJson.version !== EXPECTED_VERSION) {
  throw new Error(`Expected Mermaid ${EXPECTED_VERSION}, found ${packageJson.version}`);
}
if (!fs.existsSync(chrome)) throw new Error(`Chrome not found: ${chrome}`);

const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const ganttModuleFile = path.join(
  mermaidRoot,
  "dist",
  "chunks",
  "mermaid.core",
  "ganttDiagram-NO4QXBWP.mjs",
);
const mermaidModule = pathToFileURL(moduleFile).href;
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
  ).href,
);

const task = (label, data) => `${label} :${data}`;
const base = (...lines) => [
  "gantt",
  "dateFormat YYYY-MM-DD",
  "todayMarker off",
  ...lines,
].join("\n");

const cases = [
  ["basic", base("section Alpha", task("One", "a, 2024-01-01, 2d"))],
  ["header-only", "gantt"],
  ["header-inline", "gantt dateFormat YYYY-MM-DD\nA :a, 2024-01-01, 1d"],
  ["leading-whitespace", " \n\tgantt\nA :2024-01-01, 1d"],
  ["leading-bom", "\ufeffgantt\nA :2024-01-01, 1d"],
  ["directive-before-header", "%%{init:{\"theme\":\"dark\"}}%%\ngantt\nA :2024-01-01, 1d"],
  ["comment-before-header", "%% comment\ngantt\nA :2024-01-01, 1d"],
  ["statements", base(
    "title Project plan",
    "accTitle: Accessible plan",
    "accDescr { First line\n  Second line }",
    "axisFormat %m/%d",
    "tickInterval 2day",
    "inclusiveEndDates",
    "weekday monday",
    "weekend friday",
    "excludes weekends, 2024-01-03",
    "includes 2024-01-06",
    "todayMarker stroke:#f00,stroke-width:3px",
    "section Alpha",
    task("One", "done, crit, a, 2024-01-01, 2d"),
  )],
  ["runtime-top-axis-upstream-bug", base(
    "topAxis",
    task("One", "a, 2024-01-01, 1d"),
  )],
  ["repeated-settings", base(
    "dateFormat DD/MM/YYYY",
    "dateFormat YYYY-MM-DD",
    "axisFormat %Y",
    "axisFormat %m",
    "tickInterval 1day",
    "tickInterval 2week",
    "excludes monday, tuesday",
    "excludes tuesday, 2024-01-03",
    "includes 2024-01-03",
    "section A",
    task("One", "2024-01-01, 2d"),
  )],
  ["sections-preserve-spacing", base(
    "section  Alpha  ",
    task(" One  ", "a, 2024-01-01, 1d"),
    "section Beta<br>Line",
    task("Two", "b, 2024-01-02, 1d"),
  )],
  ["task-implicit-sequential", base(
    task("One", "2024-01-01, 2d"),
    task("Two", "3d"),
    task("Three", "1w"),
  )],
  ["task-explicit-forms", base(
    task("Two fields", "2024-01-01, 2d"),
    task("Three fields", "named, 2024-01-03, 2024-01-05"),
    task("One field", "1d"),
  )],
  ["tags-all", base(
    task("Active", "active, a, 2024-01-01, 1d"),
    task("Done", "done, b, 2024-01-02, 1d"),
    task("Critical", "crit, c, 2024-01-03, 1d"),
    task("Milestone", "milestone, m, 2024-01-04, 0d"),
    task("Vertical", "vert, v, 2024-01-05, 1d"),
    task("Combined", "active, crit, done, z, 2024-01-06, 1d"),
  )],
  ["tag-case-sensitive", base(task("Tag", "Done, active , x, 2024-01-01, 1d"))],
  ["dependencies-after", base(
    task("First", "a, 2024-01-01, 3d"),
    task("Second", "b, after a, 2d"),
    task("Third", "c, after a b, 1d"),
  )],
  ["forward-dependency", base(
    task("First", "a, after b, 1d"),
    task("Second", "b, 2024-01-03, 2d"),
  )],
  ["until-dependency", base(
    task("First", "a, 2024-01-01, until b"),
    task("Second", "b, 2024-01-05, 2d"),
  )],
  ["duplicate-id-last-wins", base(
    task("First", "same, 2024-01-01, 2d"),
    task("Second", "same, 2024-01-05, 2d"),
    task("After", "after same, 1d"),
  )],
  ["durations", base(
    task("Milliseconds", "ms, 2024-01-01, 5ms"),
    task("Seconds", "s, 2024-01-01, 5s"),
    task("Minutes", "m, 2024-01-01, 5m"),
    task("Hours", "h, 2024-01-01, 5h"),
    task("Days", "d, 2024-01-01, 1.5d"),
    task("Weeks", "w, 2024-01-01, 2w"),
    task("Months", "M, 2024-01-01, 1M"),
    task("Years", "y, 2024-01-01, 1y"),
  )],
  ["inclusive-end-date", base(
    "inclusiveEndDates",
    task("One", "a, 2024-01-01, 2024-01-02"),
  )],
  ["excludes-weekends", base(
    "excludes weekends",
    task("One", "a, 2024-01-05, 3d"),
  )],
  ["includes-overrides-excludes", base(
    "excludes weekends, 2024-01-08",
    "includes 2024-01-06, 2024-01-08",
    task("One", "a, 2024-01-05, 3d"),
  )],
  ["manual-end-bypasses-excludes", base(
    "excludes weekends",
    task("One", "a, 2024-01-05, 2024-01-08"),
  )],
  ["custom-date-format", [
    "gantt",
    "dateFormat DD/MM/YYYY",
    "todayMarker off",
    task("One", "a, 31/01/2024, 2d"),
  ].join("\n")],
  ["timestamp-x", [
    "gantt",
    "dateFormat x",
    "todayMarker off",
    task("One", "a, 1704067200000, 86400000ms"),
  ].join("\n")],
  ["timestamp-X", [
    "gantt",
    "dateFormat X",
    "todayMarker off",
    task("One", "a, 1704067200, 1d"),
  ].join("\n")],
  ["click-safe-link", base(
    task("One", "a, 2024-01-01, 1d"),
    "click a href \"https://example.com/path?q=1\"",
  )],
  ["click-dangerous-link", base(
    task("One", "a, 2024-01-01, 1d"),
    "click a href \"javascript:alert(1)\"",
  )],
  ["click-callback-strict", base(
    task("One", "a, 2024-01-01, 1d"),
    "click a call callback(\"one, two\", three)",
  )],
  ["click-missing-id", base(
    task("One", "a, 2024-01-01, 1d"),
    "click missing href \"https://example.com\"",
  )],
  ["metadata-sanitizer", base(
    "title <script>bad()</script><b onclick=bad()>Plan</b>",
    "accTitle: <img src=x onerror=bad()><i>AT</i>",
    "accDescr {<style>bad</style><u>AD</u>}",
    task("<script>Task</script><b>Visible</b>", "a, 2024-01-01, 1d"),
  )],
  ["frontmatter-title", [
    "---",
    "title: Front title",
    "---",
    base(task("One", "a, 2024-01-01, 1d")),
  ].join("\n")],
  ["frontmatter-display-compact", [
    "---",
    "displayMode: compact",
    "---",
    base(task("One", "a, 2024-01-01, 1d")),
  ].join("\n"), true],
  ["config-init", [
    "%%{init:{\"gantt\":{\"barHeight\":31,\"useMaxWidth\":false,\"displayMode\":\"compact\"}}}%%",
    base(task("One", "a, 2024-01-01, 1d")),
  ].join("\n"), true],
  ["comments-and-semicolon", "gantt\n%% comment\ndateFormat YYYY-MM-DD;\nA :a, 2024-01-01, 1d;"],
  ["empty-section", base("section Empty")],
  ["no-section", base(task("One", "a, 2024-01-01, 1d"))],
  ["reverse-date", base(task("One", "a, 2024-01-05, 2024-01-01"))],
  ["invalid-duration-falls-zero", base(task("One", "a, 2024-01-01, bogus"))],
  ["reject-uppercase-detector", "GANTT\nA :2024-01-01, 1d"],
  ["reject-prefix", "ganttx\nA :2024-01-01, 1d"],
  ["reject-leading-hash", "# comment\ngantt\nA :2024-01-01, 1d"],
  ["reject-colon-after-header", "gantt:\nA :2024-01-01, 1d"],
  ["reject-task-without-data", "gantt\nA"],
  ["reject-task-empty-data", "gantt\nA :"],
  ["reject-section-empty", "gantt\nsection"],
  ["reject-dateformat-empty", "gantt\ndateFormat"],
  ["runtime-invalid-date", base(task("One", "a, definitely-invalid, 1d"))],
  ["runtime-first-one-field", base(task("One", "1d"))],
  ["runtime-bad-prev-reference-depth", base(
    task("A", "a, after b, 1d"),
    task("B", "b, after a, 1d"),
  )],
  ["reject-unterminated-href", base(
    task("One", "a, 2024-01-01, 1d"),
    "click a href \"https://example.com",
  )],
  ["reject-acc-block-unclosed", "gantt\naccDescr {value"],
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
        deterministicIds: true,
        deterministicIDSeed: "gantt-grammar",
      });

      const classify = (error) => {
        const message = String(error?.message ?? error);
        if (message.startsWith("No diagram type detected")) return "no-diagram";
        if (message.includes("Lexical error")) return "lexer";
        if (message.includes("Parse error")) return "parser";
        return "runtime";
      };
      const reject = (error) => ({
        class: classify(error),
        message: String(error?.message ?? error),
        ...(Number.isInteger(error?.hash?.line) ? { line: error.hash.line + 1 } : {}),
        ...(Number.isInteger(error?.hash?.loc?.first_column)
          ? { column: error.hash.loc.first_column + 1 }
          : {}),
      });
      // Timezone-independent serialization, split by upstream's parse mode:
      //  - a declared dateFormat routes through dayjs WITH the format tokens,
      //    which parses in the browser's LOCAL zone — record the local wall
      //    clock re-expressed as UTC (date-only walls are midnight on every
      //    host, so this is stable; the true instant is host-dependent and
      //    deliberately not recorded);
      //  - no dateFormat falls back to JS ISO parsing, where date-only
      //    strings are UTC midnight — record the UTC instant directly.
      // The native side constructs both as Qt::UTC wall clocks, so
      // QDateTime::toUTC() comparisons hold on runners in any timezone.
      const localDate = (value, isoSemantics) => {
        if (!(value instanceof Date) || Number.isNaN(value.valueOf())) return null;
        const source = isoSemantics
          ? value
          : new Date(value.getTime() - value.getTimezoneOffset() * 60000);
        const p = (number, width = 2) => String(number).padStart(width, "0");
        return `${p(source.getUTCFullYear(), 4)}-${p(source.getUTCMonth() + 1)}-${p(source.getUTCDate())}` +
          `T${p(source.getUTCHours())}:${p(source.getUTCMinutes())}:${p(source.getUTCSeconds())}.${p(source.getUTCMilliseconds(), 3)}`;
      };
      const serializeTask = (task) => ({
        section: task.section,
        type: task.type,
        processed: task.processed,
        manualEndTime: task.manualEndTime,
        renderEndTime: localDate(task.renderEndTime, isoSemantics),
        raw: task.raw,
        task: task.task,
        classes: [...task.classes],
        id: task.id,
        prevTaskId: task.prevTaskId ?? null,
        active: Boolean(task.active),
        done: Boolean(task.done),
        crit: Boolean(task.crit),
        milestone: Boolean(task.milestone),
        vert: Boolean(task.vert),
        order: task.order,
        startTime: localDate(task.startTime, isoSemantics),
        endTime: localDate(task.endTime, isoSemantics),
        startEpoch: task.startTime instanceof Date ? task.startTime.valueOf() : null,
        endEpoch: task.endTime instanceof Date ? task.endTime.valueOf() : null,
      });

      let diagram;
      try {
        diagram = await mermaid.mermaidAPI.getDiagramFromText(source);
      } catch (error) {
        return { id, source, accept: false, reject: reject(error) };
      }

      let db;
      // Epoch formats (x = ms, X = s) parse absolute instants — their truth is
      // the UTC instant, not the wall clock.
      const dateFormat = diagram.db.getDateFormat();
      const isoSemantics = !dateFormat || dateFormat === "x" || dateFormat === "X";
      try {
        const gantt = diagram.db;
        db = {
          title: gantt.getDiagramTitle(),
          accTitle: gantt.getAccTitle(),
          accDescr: gantt.getAccDescription(),
          dateFormat: gantt.getDateFormat(),
          axisFormat: gantt.getAxisFormat(),
          tickInterval: gantt.getTickInterval() ?? null,
          todayMarker: gantt.getTodayMarker(),
          includes: [...gantt.getIncludes()],
          excludes: [...gantt.getExcludes()],
          sections: [...gantt.getSections()],
          inclusiveEndDates: gantt.endDatesAreInclusive(),
          topAxis: gantt.topAxisEnabled(),
          weekday: gantt.getWeekday(),
          displayMode: gantt.getDisplayMode(),
          links: [...gantt.getLinks()].map(([key, value]) => [key, value]),
          tasks: gantt.getTasks().map((task) => serializeTask(task)),
        };
      } catch (error) {
        return { id, source, accept: false, reject: reject(error) };
      }

      let renderAccept = true;
      let renderReject = null;
      try {
        await mermaid.render(`gantt-${id}`, source);
      } catch (error) {
        renderAccept = false;
        renderReject = reject(error);
      }
      const config = mermaid.mermaidAPI.getConfig();
      return {
        id,
        source,
        accept: true,
        expectedDb: db,
        renderAccept,
        ...(renderReject ? { renderReject } : {}),
        ...(captureConfig ? { effectiveGanttConfig: config.gantt } : {}),
      };
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
    version: packageJson.version,
    license: packageJson.license,
    moduleSha256: sha256(fs.readFileSync(moduleFile)),
    ganttModuleSha256: sha256(fs.readFileSync(ganttModuleFile)),
  },
  cases: generated,
};
fixture.fixtureSha256 = sha256(JSON.stringify(fixture));
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, `${JSON.stringify(fixture, null, 2)}\n`);
const accepted = generated.filter((entry) => entry.accept).length;
const renderAccepted = generated.filter((entry) => entry.renderAccept).length;
console.log(
  `Wrote ${output}: ${generated.length} cases, ${accepted} DB accepts, ${renderAccepted} render accepts`,
);
