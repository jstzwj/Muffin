import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const VERSION = "11.16.0";
const MODULE_SHA = "fdd1cc80731e400d2c3e41118ed53b775446391d9a71738afaf2e855ba227a5b";
const CHROME_PRODUCT = "Chrome/151.0.7922.76";
const CHROME_SHA = "a4d3a6dd0ffb35ccb77e04c033024fabd85a2bc7191066410cf1905f901f7965";
const NOTO_SHA = "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5";

const mermaidRoot = path.resolve(process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const outputFile = path.resolve(process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "mermaid-theme-css.json"));
const pixelDir = path.join(path.dirname(outputFile), "theme-css-pixel");
const pixelManifestFile = path.join(pixelDir, "manifest.json");
const chrome = path.resolve(process.argv[4] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe");
const moduleFile = path.join(mermaidRoot, "dist", "mermaid.esm.mjs");
const fontFile = path.resolve("third_party", "noto", "fonts", "NotoSans-Regular.ttf");
const sha = (bytes) => createHash("sha256").update(bytes).digest("hex");
const assert = (condition, message) => { if (!condition) throw new Error(message); };

assert(JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8")).version === VERSION, "Mermaid version drifted");
assert(sha(fs.readFileSync(moduleFile)) === MODULE_SHA, "Mermaid module drifted");
assert(sha(fs.readFileSync(chrome)) === CHROME_SHA, "Chrome drifted");
assert(sha(fs.readFileSync(fontFile)) === NOTO_SHA, "Noto drifted");

const common = { fontFamily: "Noto Sans", themeVariables: { fontFamily: "Noto Sans", fontSize: "16px" } };
const init = (themeCSS, source, extra = {}) =>
  `%%{init: ${JSON.stringify({ ...common, ...extra, ...(themeCSS === undefined ? {} : { themeCSS }) })}}%%\n${source}`;
// gitGraph has no inline `title:` statement — the diagram title only flows
// through YAML frontmatter, which must lead the source (before directives).
const gitInit = (themeCSS) =>
  `---\ntitle: Release flow\n---\n${init(themeCSS, gitgraph)}`;
// c4 measures every label with the *config* fonts (calculateTextDimensions),
// never the DOM, so its layout is CSS-independent. The themeCSS cases still
// pin all c4 font families to the bundled Noto face so the JS measurements
// (and therefore the viewBox/client locks) stay identical to the
// pixel-verified c4 "default" case instead of drifting with the host's
// Open Sans fallback.
const c4Families = { boundaryFontFamily: "Noto Sans", messageFontFamily: "Noto Sans" };
for (const type of ["person", "external_person", "system", "external_system",
  "system_db", "external_system_db", "system_queue", "external_system_queue",
  "container", "external_container", "container_db", "external_container_db",
  "container_queue", "external_container_queue", "component", "external_component",
  "component_db", "external_component_db", "component_queue", "external_component_queue"]) {
  c4Families[`${type}FontFamily`] = "Noto Sans";
}
const c4Init = (themeCSS) => init(themeCSS, c4source, { c4: c4Families });
const c4source = `C4Context
title Banking Context
Person(customer, "Customer", "Uses the bank")
System_Boundary(bank, "Bank") {
  SystemDb(core, "Core", "Accounts")
  System(api, "API", "Public interface")
}
System_Ext(mail, "Mail", "Sends notifications")
Rel(customer, api, "Uses", "HTTPS")
BiRel(api, core, "Reads and writes", "SQL")
Rel_R(api, mail, "Sends", "SMTP")`;
// Gantt positions each task label with this.getBBox() while the <text> still
// has no class (only the font-size presentation attribute), so only
// tag/ancestor selectors can feed the measurement; the taskText class and the
// width-N token are assigned afterwards and never feed back. `todayMarker
// off` keeps the live-date line out of the deterministic fixture;
// `excludes weekends` exercises the exclude-range strip.
const ganttInit = (themeCSS) => init(themeCSS, gantt, { gantt: { useWidth: 800 } });
const gantt = `gantt
title Release plan
dateFormat YYYY-MM-DD
todayMarker off
excludes weekends
section Design
  T1 : done, t1, 2025-01-01, 6d
  T2 : active, t2, after t1, 4d
section Build
  T3 : crit, t3, after t2, 5d
  M1 : milestone, m1, after t3, 0d`;
// Eventmodeling ships no base stylesheet (getStyles returns "") and measures
// every box through calculateTextDimensions with hardcoded config fonts, so
// themeCSS only repaints; the single geometry feedback is the final
// setupGraphViewbox getBBox.
const eventInit = (themeCSS) => init(themeCSS, eventmodeling);
const eventmodeling = `eventmodeling
entity UI
entity Command
entity Event
entity Read
tf 001 ui UI.Start
tf 002 cmd Command.Submit ->> 001
tf 003 evt Event.Submitted ->> 002
tf 004 rmo Read.Order ->> 003
tf 005 ui UI.History ->> 004`;
// Treemap drives every label through getComputedTextLength shrink loops
// whose inline font-size beats non-important themeCSS, so user rules only
// repaint (plus the final svg.getBBox that sizes the viewBox). The title
// flows through YAML frontmatter like gitGraph.
const treeInit = (themeCSS) =>
  `---\ntitle: Portfolio overview\n---\n${init(themeCSS, treemap)}`;
const treemap = `treemap-beta
"Portfolio"
  "Products"
    "Alpha": 35
    "Beta": 20
  "Services"
    "Consulting": 25
    "Support": 20`;
// Cynefin never consults the final bbox for its viewBox (configureSvgSize
// uses the config dimensions), so the only geometry feedback is the item
// badge: the item text carries its class at getBBox time, so a themeCSS font
// resizes the badge rect and its group translate. Own display:none on the
// text zeroes getBBox, falling back to label.length * 7.
const cynefinInit = (themeCSS) => init(themeCSS, cynefinSource);
const cynefinSource = `cynefin-beta
title Sense making
complex
  "Probe first"
  "Safe-to-fail"
clear
  "Best practice"
confusion
  "Drift"
  "Unaware"
  "Scattered"
  "Lost"
complex --> clear : "Shift"`;
// Wardley's base sheet is almost entirely *descendant* selectors
// (.wardley-node circle, .wardley-annotation circle/text, .wardley-axes line,
// .wardley-annotations-box rect/text, .wardley-notes text) which, being author
// rules, beat the matching presentation attributes — so overlays/market-dots
// resolve to componentFill, the dashed link dasharray becomes 4 4 (not 6 6) and
// the anchor label fill becomes componentLabelColor (not #000). The dead
// `.wardley-trend line`/`.wardley-axes path` rules never match (the trend line
// wears the class itself; axes have no path). The single geometry feedback is
// the annotations-box: getComputedTextLength/getBBox on its classless texts
// drive boxWidth/Height + the clamp; a `.wardley-annotations-box text`
// font-size resizes the box and own display:none collapses it. The viewBox is
// always width x height (no bbox feedback). Config sanitizer drops the
// wardley-beta object, so showGrid/pipeline stay at defaults (no grid lines).
const wardleyInit = (themeCSS) => init(themeCSS, wardleySource);
const wardleySource = `wardley-beta
title Platform landscape
anchor User [0.95,0.08]
component Platform [0.82,0.34] label [12,-14] (build) inertia
component Service [0.62,0.58] (buy)
component Supplier [0.44,0.78] (outsource)
component Market [0.30,0.90] (market)
component Evolved [0.55,0.20]
evolve Evolved 0.85
User -> Platform
Platform -.-> Service
Service +'handoff'> Supplier
annotations [0.30,0.12]
annotation 1, [0.40,0.60] 'First point'
annotation 2, [0.70,0.75] 'Second note'
note 'Watch here' [0.20,0.30]
accelerator Fast [0.12,0.12]
deaccelerator Slow [0.88,0.88]`;
// Architecture's base sheet is LOAD-BEARING: edge/arrow/node-bkg elements
// carry ONLY their class (no presentation attrs), so stroke/sw/fill come
// purely from `.edge`/`.arrow`/`.node-bkg`. The fcose layout is CSS-independent
// (cytoscape nodes size from data(width)=iconSize), but the final
// setupGraphViewbox feeds the rendered bbox, and the XY-diagonal edge label
// reads getBoundingClientRect — both DOM feedback paths. Service titles live in
// classed label gs (no base font rule → root inheritance).
const archInit = (themeCSS) => init(themeCSS, archSource);
const archSource = `architecture-beta
group platform(cloud)[Platform]
service user(internet)[User]
service api(server)[API] in platform
service db(database)[Database] in platform
service plain[Bare service]
junction hub in platform
user:R --> L:api
api:B -- T:db
hub:R -[flow]- L:api
align row hub api`;
// Railroad (4 dialect frontends, one renderer): every element is styled ONLY
// by the base sheet — rects/circles/paths/texts carry no presentation attrs
// except the fonts on measureText's transient probe text, which appends a
// bare <text> with font-family/font-size attrs. Presentation attrs beat
// inheritance but lose to tag rules, so ONLY tag/ancestor selectors (`text{}`)
// feed the measurement (gantt pattern); class rules repaint without feedback.
// The `.railroad-diagram{font-family;font-size}` base rule dies in stylis
// scoping (#id .railroad-diagram targets a descendant, never the svg itself),
// so the root #id rule owns the svg font. viewBox = layout dims (no bbox
// union).
const railroadInit = (themeCSS) => init(themeCSS, railroadSource);
const railroadSource = `railroad-beta
root = sequence(terminal("if"), nonterminal("condition"), optional(terminal("else")), choice(terminal("yes"), terminal("no")), zeroOrMore(special("token")), oneOrMore(nonterminal("item")));`;
const railroadEbnf = `railroad-ebnf-beta
root = "if", condition, ["else"], ("yes" | "no"), { ? token ? }, item+;`;
const flow = `flowchart LR
subgraph SG[Cluster title]
A[Alpha node]:::hot -->|Edge label| B[Beta node]
end
C[Outside] --> A
classDef hot fill:#00aa00,color:#ffffff,stroke:#123456,stroke-width:4px`;
const journey = `journey
    title My working day
    section Go to work
      Make tea: 5: Me
      Go upstairs: 3: Me
    section Work
      Do work: 1: Me, Cat`;
const timeline = `timeline
    title Product history
    section Alpha section
      2020 start : event one : event two
      Long task label that wraps across several words
    section Beta
      2022 end : final event`;
const kanban = `kanban
  todo[Todo]
    task1[Write docs]@{ ticket: KAN-1, assigned: 'Ada' }
    task2[Review carefully]
  done[Done]
    task3[Ship]@{ priority: 'High' }`;
// No cherry-pick: upstream stamps the cherry-pick commit with a random id
// that lands in the class token, which would break the byte-identical
// double-run. Its CSS surface (root-fill inheritance) is covered by the
// `.commit0 { fill:inherit }` root-inherit probe instead.
const gitgraph = `gitGraph
   commit id: "ZERO"
   branch develop
   commit id: "A"
   commit id: "B" tag: "v1.0.0"
   checkout main
   commit id: "C" type: HIGHLIGHT
   checkout develop
   merge main id: "D" tag: "v1.1.0"
   commit id: "E" type: REVERSE
   branch feature
   commit id: "F"
   merge develop id: "G"`;
const cases = [
  { id: "flow-baseline", source: init(undefined, flow) },
  { id: "flow-node-paint", source: init(".node rect { fill:#ff0000; stroke:#0000ff; stroke-width:9px; }", flow) },
  { id: "flow-node-important", source: init(".node rect { fill:#ff0000 !important; stroke:#0000ff !important; stroke-width:9px !important; }", flow) },
  { id: "flow-attribute", source: init('[id*="-flowchart-B-"] rect { fill:#cc00cc !important; } [id*="-flowchart-A-"] rect { stroke:#00aacc !important; }', flow) },
  { id: "flow-structural", source: init(".nodes .node:first-child rect { fill:#ff5500 !important; } .nodes .node:nth-child(2) rect { fill:#0055ff !important; }", flow) },
  { id: "flow-theme-css-angle-bracket-stripped", source: init(".nodes > .node:first-child rect { fill:#ff5500 !important; }", flow) },
  { id: "flow-directive-single-quote-invalid", source: init(".node .label { font-family:'Noto Sans'; color:#ff0000; }", flow) },
  { id: "flow-label-size", source: init(".node .label { font-size:32px; font-weight:700; color:#884400; }", flow) },
  { id: "flow-label-important", source: init(".node .label { font-size:28px !important; font-family:Noto Sans !important; color:#008800 !important; }", flow) },
  { id: "flow-root-inherit", source: init("svg { color:#0066cc; font-size:26px; font-family:Noto Sans; } .node .label { color:inherit; font-size:inherit; }", flow) },
  { id: "flow-css-variables", source: init(":root { --box:#11aa77; --ink:#7722aa; } .node rect { fill:var(--box) !important; } .node .label { color:var(--ink) !important; }", flow) },
  { id: "flow-invalid-values", source: init(".node rect { fill:bogus; stroke-width:-7px; } .node .label { font-size:bogus; color:bogus; }", flow) },
  { id: "flow-cluster-edge", source: init(".cluster rect { fill:#ffee00 !important; stroke:#660000 !important; stroke-width:7px !important; } .edgePath path { stroke:#00aa00 !important; stroke-width:6px !important; } .edgeLabel { color:#aa00aa !important; font-size:24px !important; }", flow) },
  { id: "flow-class-normal-conflict", source: init(".hot rect { fill:#ff0000; stroke:#ff00ff; }", flow) },
  { id: "flow-class-important-conflict", source: init(".hot rect { fill:#ff0000 !important; stroke:#ff00ff !important; }", flow) },
  { id: "flow-inline-important-conflict", source: init(".node rect { fill:#ff0000 !important; stroke:#ff00ff !important; }", "flowchart LR\nA[Alpha] --> B[Beta]\nstyle A fill:#0000ff,stroke:#00aa00,stroke-width:5px") },
  { id: "flow-display-none", source: init('[id*="-flowchart-B-"] { display:none; }', flow) },
  { id: "flow-visibility-recovery", source: init(".node { visibility:hidden !important; } .node .label { visibility:visible !important; }", flow) },
  { id: "sequence-paint", source: init(".actor { fill:#ffcccc !important; stroke:#990000 !important; stroke-width:5px !important; } .messageText { fill:#006600 !important; font-size:25px !important; }", "sequenceDiagram\nAlice->>Bob: Hello") },
  { id: "sequence-hidden", source: init(".actor rect { display:none !important; } .messageText { visibility:hidden !important; } .actor { opacity:.35; }", "sequenceDiagram\nAlice->>Bob: Hello\nBob-->>Alice: Done") },
  { id: "class-paint", source: init(".node rect { fill:#ddffdd !important; stroke:#006600 !important; stroke-width:6px !important; } .nodeLabel { color:#660066 !important; font-size:23px !important; }", "classDiagram\nclass A\nclass B\nA --> B") },
  { id: "class-hidden", source: init(".node:first-child rect { display:none !important; } .nodeLabel { visibility:hidden !important; } .node rect { opacity:.4; }", "classDiagram\nclass A\nclass B\nA --> B") },
  { id: "state-paint", source: init(".node rect { fill:#ddeeff !important; stroke:#003399 !important; stroke-width:6px !important; } .nodeLabel { color:#990000 !important; font-size:23px !important; }", "stateDiagram-v2\nA --> B") },
  { id: "state-hidden", source: init(".node rect { display:none !important; } .nodeLabel { visibility:hidden !important; }", "stateDiagram-v2\nA --> B") },
  { id: "er-paint", source: init(".entityBox { fill:#ffeecc !important; stroke:#994400 !important; stroke-width:5px !important; } .relationshipLine { stroke:#008899 !important; stroke-width:7px !important; } text { fill:#550055 !important; font-size:21px !important; }", "erDiagram\nA ||--o{ B : has") },
  { id: "er-hidden", source: init(".entityBox { display:none !important; } .relationshipLine { visibility:hidden !important; } .entityBox { opacity:.3; }", "erDiagram\nA ||--o{ B : has") },
  { id: "pie-paint", source: init(".pieCircle { fill:#eeeeff !important; stroke:#333399 !important; stroke-width:8px !important; } .slice { stroke:#ff0000 !important; stroke-width:5px !important; } text { fill:#005500 !important; font-size:20px !important; }", "pie\n  \"One\" : 3\n  \"Two\" : 7") },
  { id: "pie-hidden", source: init(".pieCircle { display:none !important; } text { visibility:hidden !important; }", "pie\n  \"One\" : 3\n  \"Two\" : 7") },
  { id: "packet-paint", source: init("rect { fill:#ddffff !important; stroke:#007777 !important; stroke-width:4px !important; } text { fill:#770077 !important; font-size:20px !important; }", "packet-beta\n0-7: \"Header\"\n8-15: \"Body\"") },
  { id: "packet-hidden", source: init("rect { display:none !important; } text { visibility:hidden !important; }", "packet-beta\n0-7: \"Header\"\n8-15: \"Body\"") },
  { id: "mindmap-paint", source: init(".node rect, .node circle, .node polygon, .node path { fill:#ffeeee !important; stroke:#990000 !important; stroke-width:5px !important; } .nodeLabel { color:#000099 !important; font-size:22px !important; }", "mindmap\n  root((Root))\n    Child\n    Other") },
  { id: "mindmap-hidden", source: init(".node circle { display:none !important; } .nodeLabel { visibility:hidden !important; } .node polygon { opacity:.45; }", "mindmap\n  root((Root))\n    Child\n    Other") },
  { id: "info-paint", source: init(".version { fill:#0066aa !important; font-size:48px !important; font-weight:700 !important; opacity:.4; }", "info") },
  { id: "info-hidden", source: init(".version { display:none !important; }", "info") },
  { id: "info-root-inherit", source: init("svg { fill:#aa3300; font-size:27px; font-family:Noto Sans; } .version { fill:inherit; font-size:inherit !important; }", "info") },
  { id: "quadrant-paint", source: init(".quadrants .quadrant:first-child rect { fill:#ffcc00 !important; opacity:.6; } .quadrant text { fill:#006699 !important; font-size:25px !important; font-weight:700 !important; } .border line { stroke:#990000 !important; stroke-width:6px !important; } .data-point circle { fill:#00aa55 !important; stroke:#000099 !important; stroke-width:4px !important; } .data-point text { fill:#660066 !important; font-size:19px !important; } .labels text { fill:#3333aa !important; font-size:22px !important; } .title text { fill:#aa3300 !important; font-size:30px !important; }", "quadrantChart\ntitle Reach\nx-axis Low --> High\ny-axis Down --> Up\nquadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4\nPoint: [0.4, 0.7]") },
  { id: "quadrant-hidden", source: init(".data-points { display:none !important; } .quadrants .quadrant:nth-child(2) { visibility:hidden !important; }", "quadrantChart\nquadrant-1 Q1\nquadrant-2 Q2\nPoint: [0.4, 0.7]") },
  { id: "radar-paint", source: init(".radarGraticule { fill:#ffee00 !important; fill-opacity:.7 !important; stroke:#996600 !important; stroke-width:4px !important; } .radarAxisLine { stroke:#006699 !important; stroke-width:7px !important; } .radarAxisLabel { fill:#990066 !important; font-size:24px !important; font-weight:700 !important; } .radarCurve-0 { color:#00aa55 !important; fill:currentColor !important; fill-opacity:.3 !important; stroke:#000099 !important; stroke-width:6px !important; } .radarLegendBox-0 { fill:#ff6600 !important; fill-opacity:.8 !important; stroke:#660000 !important; stroke-width:3px !important; } .radarLegendText { fill:#4444aa !important; font-size:20px !important; } .radarTitle { fill:#aa3300 !important; font-size:29px !important; }", "radar-beta\ntitle Reach\naxis A,B,C\ncurve One {1,2,3}\ncurve Two {3,2,1}\ngraticule polygon") },
  { id: "radar-hidden", source: init(".radarGraticule:first-child { display:none !important; } .radarCurve-0 { visibility:hidden !important; } .radarLegendText { opacity:.25; }", "radar-beta\naxis A,B,C\ncurve One {1,2,3}\ngraticule circle") },
  { id: "xychart-paint", source: init(".background { fill:#f0f8ff !important; opacity:.8; } .chart-title text { fill:#aa3300 !important; font-size:31px !important; font-weight:700 !important; } .bar-plot-0 rect { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:5px !important; fill-opacity:.6; } .bar-plot-0 text { fill:#550055 !important; font-size:18px !important; } .line-plot-1 path { stroke:#0066cc !important; stroke-width:7px !important; } .line-plot-1 .labels text { fill:#009944 !important; font-size:17px !important; } .bottom-axis .label text { fill:#660099 !important; font-size:19px !important; } .bottom-axis .axis-line path { stroke:#aa0000 !important; stroke-width:4px !important; } .left-axis .title text { fill:#005588 !important; font-size:23px !important; } .left-axis .ticks path { stroke:#008855 !important; stroke-width:3px !important; }", "xychart-beta\ntitle Sales\nx-axis Month [Jan,Feb,Mar]\ny-axis Value 0 --> 100\nbar [20,50,80]\nline [10 \"low\",60,90 \"high\"]", { xyChart: { showDataLabel: true } }) },
  { id: "xychart-hidden", source: init(".plot { display:none !important; } .bottom-axis .label { visibility:hidden !important; } .background { opacity:.25; }", "xychart-beta\ntitle Sales\nx-axis [Jan,Feb,Mar]\ny-axis 0 --> 100\nbar [20,50,80]") },
  { id: "sankey-paint", source: init(".node:first-child rect { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:5px !important; fill-opacity:.6; } .node-labels { font-family:Noto Sans; font-size:22px !important; opacity:.8; } .sankey-label-bg { stroke:#ffffff !important; stroke-width:7px !important; } .sankey-label-fg { fill:#660099 !important; font-weight:700 !important; } .links { stroke-opacity:.7 !important; } .link { mix-blend-mode:normal !important; } .link path { stroke:#0088aa !important; stroke-width:8px !important; }", "sankey-beta\nA,B,10\nA,C,5\nB,D,7\nC,D,5", { sankey: { labelStyle: "outlined" } }) },
  { id: "sankey-hidden", source: init(".node:first-child { display:none !important; } .node-labels text:first-child { visibility:hidden !important; } .links { display:none !important; }", "sankey-beta\nA,B,10\nA,C,5\nB,D,7\nC,D,5") },
  { id: "treeview-paint", source: init(".treeView-node-label { fill:#006699 !important; font-size:24px !important; } .treeView-node-dir { font-weight:400 !important; } .treeView-node-description { fill:#990066 !important; font-size:18px !important; font-style:normal !important; } .treeView-node-line { stroke:#008844 !important; stroke-width:5px !important; opacity:.6; } .treeView-highlight-bg { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:4px !important; fill-opacity:.5; }", "treeView-beta\nproject/ :::highlight ## Workspace\n  src/\n    main.cpp\n  README.md") },
  { id: "treeview-hidden", source: init(".tree-view g:first-child text { display:none !important; } .treeView-node-line { visibility:hidden !important; } .treeView-node-description { display:none !important; }", "treeView-beta\nproject/ ## Workspace\n  src/\n    main.cpp\n  README.md") },
  { id: "block-paint", source: init(".node rect { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:5px !important; } .node .label { color:#006699 !important; font-size:28px !important; font-weight:700 !important; } .flowchart-link { stroke:#008844 !important; stroke-width:6px !important; }", "block-beta\ncolumns 2\na[\"Alpha\"] b[\"Beta\"]\na --> b") },
  { id: "block-hidden", source: init(".node:first-child { display:none !important; } .flowchart-link { visibility:hidden !important; }", "block-beta\ncolumns 2\na[\"Alpha\"] b[\"Beta\"]\na --> b") },
  { id: "venn-paint", source: init(".venn-title { fill:#aa3300 !important; font-size:40px !important; font-weight:700 !important; } .venn-circle:first-child path { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:7px !important; fill-opacity:.4 !important; } .venn-circle text { fill:#006699 !important; font-size:30px !important; font-weight:700 !important; } .venn-intersection path { fill:#00aa55 !important; fill-opacity:.6 !important; } .venn-intersection text { fill:#990066 !important; font-size:26px !important; } .venn-text-node { color:#0044aa !important; font-size:22px !important; font-style:italic !important; opacity:.75; }", "venn-beta\ntitle Reach\nset A[Alpha]:10\nset B[Beta]:8\nunion A,B[Both]:3\ntext A note[Inside A]\ntext A,B shared[Shared]") },
  { id: "venn-hidden", source: init(".venn-circle:first-child { display:none !important; } .venn-intersection path { visibility:hidden !important; } .venn-intersection text { opacity:.25; } .venn-text-node { display:none !important; }", "venn-beta\nset A[Alpha]:10\nset B[Beta]:8\nunion A,B[Both]:3\ntext A note[Inside A]\ntext A,B shared[Shared]") },
  { id: "swimlane-paint", source: init(".swimlane.cluster .swimlane-title { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:7px !important; } .swimlane.cluster .swimlane-body { fill:#e8f6ff !important; stroke:#006699 !important; stroke-width:5px !important; } .swimlane-label { color:#990066 !important; font-size:24px !important; font-weight:700 !important; } .node .label { color:#0044aa !important; font-size:27px !important; font-weight:700 !important; } .flowchart-link { stroke:#008844 !important; stroke-width:6px !important; }", "swimlane-beta TB\nsubgraph sales[Sales]\n a[Lead] -->|handoff| b[Quote]\nend\nsubgraph legal[Legal]\n c[Review] --> d[Approve]\nend\nb --> c") },
  { id: "swimlane-hidden", source: init(".swimlane:first-child .swimlane-title { display:none !important; } .swimlane:nth-child(2) .swimlane-body { visibility:hidden !important; } .node:first-child { display:none !important; } .flowchart-link { visibility:hidden !important; }", "swimlane-beta TB\nsubgraph sales[Sales]\n a[Lead] --> b[Quote]\nend\nsubgraph legal[Legal]\n c[Review] --> d[Approve]\nend\nb --> c") },
  { id: "ishikawa-paint", source: init(".ishikawa-spine { stroke:#006699 !important; stroke-width:8px !important; opacity:.7; } .ishikawa-branch { stroke:#008844 !important; stroke-width:6px !important; } .ishikawa-sub-branch { stroke:#aa3300 !important; stroke-width:4px !important; } .ishikawa-arrow { fill:#990066 !important; opacity:.6; } .ishikawa-head { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:7px !important; fill-opacity:.5; } .ishikawa-label-box { fill:#e8f6ff !important; stroke:#0044aa !important; stroke-width:5px !important; } .ishikawa-head-label { fill:#550055 !important; font-size:25px !important; font-weight:700 !important; font-style:italic !important; } .ishikawa-label.cause { fill:#006699 !important; font-size:23px !important; font-weight:700 !important; } .ishikawa-label.align { fill:#008844 !important; font-size:21px !important; } .ishikawa-label.up { fill:#aa3300 !important; font-size:19px !important; } .ishikawa-label.down { fill:#990066 !important; font-size:18px !important; }", "ishikawa\nEffect\n  Upper cause\n    Upper aligned\n      Upper sloped\n  Lower cause\n    Lower aligned\n      Lower sloped") },
  { id: "ishikawa-hidden", source: init(".ishikawa-head-group { display:none !important; } .ishikawa-pair .ishikawa-label-group { visibility:hidden !important; } .ishikawa-pair .ishikawa-sub-group:nth-of-type(2) { display:none !important; } .ishikawa-spine { visibility:hidden !important; }", "ishikawa\nEffect\n  Upper cause\n    Upper aligned\n      Upper sloped\n  Lower cause\n    Lower aligned\n      Lower sloped") },
  { id: "requirement-paint", source: init(".node .label-container { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:7px !important; fill-opacity:.55; } .node .label { color:#006699 !important; font-size:23px !important; font-weight:700 !important; font-style:italic !important; } .divider { stroke:#aa3300 !important; stroke-width:5px !important; opacity:.65; } .relationshipLine { stroke:#008844 !important; stroke-width:6px !important; opacity:.7; } .edgeLabel .label { background-color:#e8f6ff !important; color:#990066 !important; font-size:21px !important; font-weight:700 !important; } marker { fill:#550055 !important; stroke:#0044aa !important; opacity:.6; }", "requirementDiagram\nrequirement Parent {\n  id: P\n  text: Parent body\n  risk: high\n  verifyMethod: inspection\n}\nrequirement Child {\n  id: C\n}\nelement Module {\n  type: System\n  docref: SPEC\n}\nParent -contains-> Child\nModule -satisfies-> Parent") },
  { id: "requirement-hidden", source: init(".nodes .node:first-child { display:none !important; } .nodes .node:nth-child(2) .divider { visibility:hidden !important; } .edgePaths .relationshipLine:first-child { display:none !important; } .edgeLabels .edgeLabel:nth-child(2) { visibility:hidden !important; } marker { display:none !important; }", "requirementDiagram\nrequirement Parent {\n  id: P\n  text: Parent body\n}\nrequirement Child {\n  id: C\n}\nelement Module {\n  type: System\n}\nParent -contains-> Child\nModule -satisfies-> Parent") },
  { id: "requirement-divider-path-override", source: init(".divider path { stroke:#ff0000 !important; stroke-width:9px !important; }", "requirementDiagram\nrequirement A {\n  id: A\n  text: Body\n}") },
  { id: "requirement-nodelabel-paint", source: init(".node .label { color:#006699 !important; }", "requirementDiagram\nrequirement A {\n  id: A\n  text: Body\n}\nstyle A color:#0000ff") },
  { id: "requirement-edge-painted-bg", source: init("div.labelBkg { background-color:rgba(255,255,0,.5) !important; } span.edgeLabel { background-color:rgba(0,255,0,.5) !important; }", "requirementDiagram\nrequirement A {\n  id: A\n}\nrequirement B {\n  id: B\n}\nA -contains-> B") },
  { id: "journey-baseline", source: init(undefined, journey) },
  { id: "journey-paint", source: init(".face { fill:#ffdddd !important; stroke:#990000 !important; stroke-width:5px !important; } .mouth { stroke:#aa3300 !important; } line { stroke:#0066cc !important; } .legend { fill:#660066 !important; font-size:22px !important; font-weight:700 !important; } .task-type-0 { fill:#ffee00 !important; } rect { stroke:#880044 !important; stroke-width:3px !important; } .actor-0 { fill:#00aa55 !important; } .label { color:#000099 !important; font-size:19px !important; } text.task { fill:#ffffff !important; }", journey) },
  { id: "journey-hidden", source: init(".face { display:none !important; } .legend { visibility:hidden !important; } rect.task { visibility:hidden !important; } .task-line { opacity:.3; } path.mouth { visibility:hidden !important; } .actor-1 { display:none !important; }", journey) },
  { id: "journey-structural", source: init("g:nth-of-type(2) rect { fill:#ff5500 !important; } g:nth-of-type(4) circle.actor-1 { stroke:#0000ff !important; stroke-width:4px !important; }", journey) },
  { id: "journey-root-inherit", source: init("text { font-size:26px; fill:#aa3300; } line { stroke:inherit; } circle { fill:inherit; }", journey) },
  { id: "timeline-baseline", source: init(undefined, timeline) },
  { id: "timeline-paint", source: init(".node-bkg { fill:#ffddaa !important; stroke:#880044 !important; stroke-width:4px !important; } .section--1 text { fill:#990000 !important; font-size:22px !important; font-weight:700 !important; } .lineWrapper line { stroke:#00aa55 !important; stroke-width:6px !important; } .timeline-node { opacity:.85; }", timeline) },
  { id: "timeline-hidden", source: init(".node-bkg { display:none !important; } .section--1 text { visibility:hidden !important; } .lineWrapper line { visibility:hidden !important; } .eventWrapper { display:none !important; }", timeline) },
  { id: "timeline-structural", source: init("g:nth-of-type(3) path { fill:#ff5500 !important; } g:nth-of-type(4) line { stroke:#0000ff !important; stroke-width:9px !important; }", timeline) },
  { id: "timeline-root-inherit", source: init("text { font-size:26px; fill:#aa3300; } path { fill:inherit; } line { stroke-width:9px; }", timeline) },
  { id: "kanban-baseline", source: init(undefined, kanban) },
  { id: "kanban-paint", source: init(".sections rect { fill:#ffddaa !important; stroke:#880044 !important; stroke-width:4px !important; } .items .node rect { fill:#ddeeff !important; stroke:#004488 !important; stroke-width:2px !important; } .items .nodeLabel { color:#990000 !important; font-size:15px !important; font-weight:700 !important; } .section-1 { opacity:.9; }", kanban) },
  { id: "kanban-hidden", source: init(".sections rect { display:none !important; } .section-1 .nodeLabel { visibility:hidden !important; } .items .node { visibility:hidden !important; } .items .node:last-child { visibility:visible !important; }", kanban) },
  { id: "kanban-structural", source: init(".sections g:nth-child(2) rect { fill:#ff5500 !important; } .items g:first-child rect { stroke:#0000ff !important; stroke-width:6px !important; }", kanban) },
  { id: "kanban-root-inherit", source: init("span { font-size:17px; color:#aa3300; } rect { stroke:inherit; } .node rect { fill:inherit; }", kanban) },
  { id: "gitgraph-baseline", source: gitInit(undefined) },
  { id: "gitgraph-paint", source: gitInit(".commit-label { fill:#006600 !important; font-size:13px !important; font-weight:700 !important; } .commit-label-bkg { fill:#ddeeff !important; opacity:.9 !important; } .tag-label { fill:#990000 !important; font-size:13px !important; } .tag-label-bkg { fill:#ffddaa !important; stroke:#880044 !important; } .tag-hole { fill:#0000ff !important; } .branch { stroke:#0066cc !important; stroke-width:3px !important; } .branch-label2 { fill:#ff00ff !important; font-size:20px !important; } .branchLabelBkg { stroke:#333300 !important; stroke-width:2px !important; } .commit-highlight0 { fill:#00aa55 !important; } .commit-highlight-inner { stroke:#aa0000 !important; } .commit-merge { fill:#ffffff !important; } .commit-reverse { stroke:#ff8800 !important; stroke-width:2px !important; } .arrow1 { stroke:#00aa55 !important; } .arrow { stroke-width:4px !important; } .gitTitleText { fill:#aa3300 !important; font-size:24px !important; } .commit-labels { opacity:.85; } .commit2 { fill:#ffdd00 !important; }") },
  { id: "gitgraph-hidden", source: gitInit(".commit-label-bkg { display:none !important; } .tag-label { display:none !important; } .branch-label1 { display:none !important; } .arrow2 { visibility:hidden !important; } .commit-reverse { display:none !important; } .commit-merge { visibility:hidden !important; } .tag-hole { visibility:hidden !important; } .commit1 { opacity:.35; }") },
  { id: "gitgraph-structural", source: gitInit(".commit-bullets circle:first-child { fill:#ff5500 !important; } .commit-labels g:nth-child(3) rect { fill:#0055ff !important; } .commit-arrows path:nth-child(2) { stroke:#0000ff !important; stroke-width:6px !important; } .branchLabel:first-child .label { fill:#00cc88 !important; }") },
  { id: "gitgraph-root-inherit", source: gitInit("svg { fill:#aa3300; } text { font-size:19px; fill:#aa3300; } .commit0 { fill:inherit; } .arrow { fill:inherit; } .branch { stroke:inherit; } rect { stroke:inherit; }") },
  // c4 label texts carry *inline* font styles, so only !important themeCSS
  // moves them; fill is always a presentation attribute, so any rule wins.
  // The base `.person { stroke; fill }` rule is dead upstream (shape groups
  // are hardcoded to class person-man).
  { id: "c4-baseline", source: c4Init(undefined) },
  { id: "c4-paint", source: c4Init(".person-man rect { fill:#ff8800 !important; stroke:#002244 !important; stroke-width:3px !important; } .person-man path { fill:#00aa88 !important; } rect { stroke:#770077 !important; } text { fill:#ffee00 !important; } .person-man text { font-size:20px !important; font-weight:400 !important; } line { stroke:#0066cc !important; stroke-width:4px !important; } image { opacity:.5; } .person-man { opacity:.6; }") },
  { id: "c4-hidden", source: c4Init("g:nth-of-type(2) { display:none !important; } line { display:none !important; } text { visibility:hidden !important; } image { display:none !important; }") },
  { id: "c4-structural", source: c4Init("g:nth-of-type(5) rect { fill:#ff5500 !important; } .person-man path:first-of-type { stroke:#0000ff !important; stroke-width:4px !important; } text:nth-of-type(2) { fill:#00cc88 !important; }") },
  { id: "c4-root-inherit", source: c4Init("svg { fill:#aa3300; } text { font-size:19px; fill:#aa3300; } .person-man text { font-size:19px; } rect { stroke:inherit; } image { fill:inherit; } line { fill:inherit; }") },
  // gantt: the paint case pins the user sheet beating the base sheet at equal
  // specificity (taskText fill over the base !important done/active/critText
  // rules, .grid .tick opacity) and the classless-probe measurement staying
  // at 11px while the drawn font-size moves; root-inherit's `text` tag rule
  // moves the measurement itself, flipping the milestone label outside.
  { id: "gantt-baseline", source: ganttInit(undefined) },
  { id: "gantt-paint", source: ganttInit(".done0 { fill:#ff8800 !important; stroke:#002244 !important; stroke-width:4px !important; } .crit1 { stroke:#770077 !important; } .grid .tick text { fill:#006699 !important; font-size:20px !important; } .section0 { fill:#ffee00 !important; } .sectionTitle1 { fill:#00aa55 !important; } .titleText { fill:#aa3300 !important; font-size:24px !important; } .taskText { fill:#880044 !important; font-size:22px !important; } .section { opacity:.5; } .grid .tick { opacity:.4; } .exclude-range { fill:#00ccdd !important; }") },
  { id: "gantt-hidden", source: ganttInit("g:nth-of-type(2) { display:none !important; } .done0 { display:none !important; } .milestone { visibility:hidden !important; } .grid .tick { visibility:hidden !important; } .milestoneText { visibility:hidden !important; } .sectionTitle1 { visibility:hidden !important; } .titleText { opacity:.35; }") },
  { id: "gantt-structural", source: ganttInit("g:nth-of-type(3) line { stroke:#0000ff !important; stroke-width:4px !important; } g:nth-of-type(4) rect { fill:#ff5500 !important; } g:nth-of-type(6) text { fill:#00cc88 !important; } g:nth-of-type(5) rect:nth-of-type(2) { fill:#0055ff !important; }") },
  { id: "gantt-root-inherit", source: ganttInit("text { font-size:26px; fill:#aa3300 !important; } rect { stroke:inherit; } line { stroke:#0066cc; }") },
  // eventmodeling: every painted value is a presentation attribute or an
  // inherited root property (no base sheet), so plain author rules win the
  // attrs; the box labels live in foreignObject spans, where `color` (not
  // `fill`) semantics apply.
  { id: "eventmodeling-baseline", source: eventInit(undefined) },
  { id: "eventmodeling-paint", source: eventInit(".em-box rect { fill:#ff8800; stroke:#002244; stroke-width:3px; } .em-swimlane rect { fill:#e8f6ff; } .em-relation { stroke:#0066cc; stroke-width:2px; fill:none; } .em-swimlane text { fill:#990066; font-size:20px; } .em-box span { color:#006699; font-size:14px; } marker polygon { fill:#aa0000; } .em-box { opacity:.6; }") },
  { id: "eventmodeling-hidden", source: eventInit(".em-box rect { display:none; } .em-relation { visibility:hidden; } .em-swimlane text { visibility:hidden; } .em-box { opacity:.4; }") },
  { id: "eventmodeling-structural", source: eventInit("g:nth-of-type(2) rect { fill:#ff5500; } g:nth-of-type(7) rect { fill:#0055ff; } path:nth-of-type(2) { stroke:#0000ff; stroke-width:4px; }") },
  { id: "eventmodeling-root-inherit", source: eventInit("text { font-size:26px; fill:#aa3300; } rect { stroke:inherit; } path { stroke:#0066cc; } div { color:#aa3300; }") },
  // treemap: label/value fills and font sizes are inline styles, so only
  // !important rules move them; the title keeps the sole live base rule
  // (.treemapTitle). Group-level display:none shrinks the final getBBox.
  { id: "treemap-baseline", source: treeInit(undefined) },
  { id: "treemap-paint", source: treeInit(".treemapSection rect { fill:#ddeeff !important; stroke:#004488 !important; stroke-width:4px !important; } .treemapLeaf { fill:#ffeedd !important; stroke:#884400 !important; stroke-width:5px !important; } .treemapLabel { fill:#006699 !important; } .treemapValue { fill:#990066 !important; } .treemapTitle { fill:#aa3300 !important; font-size:20px !important; } .treemapSectionLabel { fill:#0000ff !important; }") },
  { id: "treemap-hidden", source: treeInit("g.treemapContainer g:nth-of-type(2) { display:none !important; } .treemapSectionLabel { visibility:hidden !important; } .treemapValue { display:none !important; } .treemapTitle { opacity:.4; }") },
  { id: "treemap-structural", source: treeInit("g.treemapContainer g:nth-of-type(3) rect { fill:#ff5500 !important; } g.treemapContainer g:nth-of-type(6) rect { stroke:#0000ff !important; stroke-width:6px !important; } text.treemapLabel:first-of-type { fill:#00cc88 !important; }") },
  { id: "treemap-root-inherit", source: treeInit("text { fill:#aa3300 !important; } rect { stroke:inherit; } svg { color:#0066cc; }") },
  // cynefin: the paint case resizes the item badges through the classed
  // getBBox measurement; hidden's own display:none on the item text drops
  // getBBox to 0x0 so the length*7 fallback shrinks every badge.
  { id: "cynefin-baseline", source: cynefinInit(undefined) },
  { id: "cynefin-paint", source: cynefinInit(".cynefinDomain { fill:#ffcc00 !important; fill-opacity:.7 !important; } .cynefinDomainLabel { fill:#006699 !important; font-size:24px !important; } .cynefinSubtitle { fill:#990066 !important; font-size:14px !important; } .cynefinItem { fill:#ddeeff !important; stroke:#004488 !important; stroke-width:3px !important; } .cynefinItemText { fill:#880044 !important; font-size:18px !important; } .cynefinBoundary { stroke:#aa3300 !important; stroke-width:5px !important; } .cynefinCliff { stroke:#0000ff !important; stroke-width:6px !important; } .cynefinConfusion { stroke:#00aa55 !important; stroke-width:2px !important; } .cynefinArrowLine { stroke:#ff00ff !important; stroke-width:4px !important; } .cynefinArrowLabel { fill:#00cc88 !important; font-size:16px !important; } .cynefinArrowHead { fill:#660000 !important; } .cynefinTitle { fill:#550055 !important; font-size:28px !important; }") },
  { id: "cynefin-hidden", source: cynefinInit(".cynefin-boundaries { visibility:hidden; } .cynefinItemText { display:none; } .cynefinItem { visibility:hidden; } .cynefinDomainLabel { visibility:hidden; } .cynefinConfusion { opacity:.3; } .cynefinTitle { opacity:.35; }") },
  { id: "cynefin-structural", source: cynefinInit(".cynefin-backgrounds rect:nth-child(2) { fill:#ff5500 !important; } .cynefin-boundaries path:first-of-type { stroke:#0000ff !important; stroke-width:6px !important; } .cynefin-items g:nth-child(2) rect { stroke:#0055ff !important; stroke-width:4px !important; } .cynefin-labels text:nth-of-type(3) { fill:#00cc88 !important; }") },
  { id: "cynefin-root-inherit", source: cynefinInit("text { font-size:26px; fill:#aa3300; } rect { stroke:inherit; } path { fill:inherit; } svg { color:#0066cc; }") },
  // wardley is themeCSS-INERT: its draw() begins with svg.selectAll("*")
  // .remove(), which nukes the <style> element createUserStyles injected
  // before draw — so neither the base sheet nor user themeCSS ever reaches a
  // wardley element (scopedThemeRules == 0; paint == baseline byte-for-byte).
  // Every value below is therefore a bare presentation attribute. The inert
  // probe carries an aggressive sheet that WOULD recolor most surfaces; the
  // comparator locks that it has no effect (regression guard against a future
  // overlay wiring that would break 1:1 parity).
  { id: "wardley-baseline", source: wardleyInit(undefined) },
  { id: "wardley-inert", source: wardleyInit(".wardley-background { fill:#fff7e6 !important; } .wardley-node circle { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:4px !important; } .wardley-node-label { fill:#006699 !important; font-size:18px !important; } .wardley-link { stroke:#00aa55 !important; stroke-width:3px !important; } .wardley-link--dashed { stroke-dasharray:8 8 !important; } .wardley-trend { stroke:#ff00ff !important; } .wardley-annotation circle { fill:#ddeeff !important; } .wardley-annotations-box rect { fill:#e8f6ff !important; } .wardley-annotations-box text { font-size:13px !important; } .wardley-title { fill:#aa0000 !important; }") },
  // architecture: the label class param is dead for SVG labels (createText
  // drops it on the useHtmlLabels:false path — the texts are bare <text> with
  // everything inherited from the root rule), so the paint/hidden cases target
  // them through `.architecture-service text`. font-size moves the rendered
  // bbox → setupGraphViewbox (viewBox feedback).
  { id: "architecture-baseline", source: archInit(undefined) },
  { id: "architecture-paint", source: archInit(".edge { stroke:#00aa55 !important; stroke-width:6px !important; } .arrow { fill:#ff00ff !important; } .node-bkg { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:4px !important; } .architecture-service text { fill:#006699 !important; font-size:20px !important; font-weight:700 !important; }") },
  { id: "architecture-hidden", source: archInit(".architecture-edges { display:none !important; } .node-bkg { visibility:hidden !important; } .architecture-service text { visibility:hidden !important; } .arrow { opacity:.3; }") },
  { id: "architecture-structural", source: archInit(".architecture-services g:nth-child(2) text { fill:#0055ff !important; } .architecture-edges g:nth-child(3) path { stroke:#0000ff !important; stroke-width:5px !important; }") },
  { id: "architecture-root-inherit", source: archInit("text { fill:#aa3300; font-size:19px; } svg { color:#0066cc; } path { fill:inherit; } rect { fill:inherit; }") },
  // railroad: paint recolors every class surface; hidden drops lines via
  // visibility, terminal rects via display and the end markers via opacity;
  // root-inherit's `text` TAG rule beats the probe's font presentation attrs
  // and resizes the measurement → whole layout + viewBox shift.
  { id: "railroad-baseline", source: railroadInit(undefined) },
  { id: "railroad-paint", source: railroadInit(".railroad-terminal rect { fill:#ffcc00 !important; stroke:#663300 !important; stroke-width:4px !important; } .railroad-terminal text { fill:#880044 !important; font-size:18px !important; } .railroad-nonterminal rect { fill:#ddeeff !important; stroke:#004488 !important; } .railroad-nonterminal text { fill:#006699 !important; } .railroad-line { stroke:#00aa55 !important; stroke-width:3px !important; } .railroad-start circle, .railroad-end circle { fill:#ff00ff !important; } .railroad-special rect { fill:#ffee00 !important; stroke:#884400 !important; } .railroad-special text { fill:#550055 !important; } .railroad-rule-name { fill:#aa0000 !important; }") },
  { id: "railroad-hidden", source: railroadInit(".railroad-line { visibility:hidden !important; } .railroad-terminal rect { display:none !important; } .railroad-rule-name { visibility:hidden !important; } .railroad-end circle { opacity:.3; }") },
  { id: "railroad-root-inherit", source: railroadInit("text { font-size:20px; fill:#aa3300; } svg { color:#0066cc; } rect { stroke:inherit; }") },
  { id: "railroad-ebnf-baseline", source: init(undefined, railroadEbnf) },
  { id: "railroad-abnf-baseline", source: init(undefined, `railroad-abnf-beta
root = "if" condition ["else"] ("yes" / "no") *item %x41-5A ;`) },
  { id: "railroad-peg-baseline", source: init(undefined, `railroad-peg-beta
root <- "if" condition "else"? ("yes" / "no") item* .+;`) },
];

const selectors = [
  "svg", ".root", ".nodes", ".node", ".node rect", ".node circle", ".node ellipse",
  ".node polygon", ".node path", ".node .label", ".nodeLabel", ".edgePath path", ".flowchart-link",
  ".edgeLabel", ".cluster", ".cluster rect", ".cluster-label", ".actor", ".messageText",
  ".entityBox", ".relationshipLine", ".reqBox", ".reqTitle", ".reqLabel", ".reqLabelBox", ".req-title-line", ".divider", ".divider > path", ".relationshipLabel", ".edgePaths", ".edgePath", ".edgeLabels", ".edgeLabels > .edgeLabel", ".edgeLabel .label", ".edgeLabel .label rect", ".edgeLabel .label text", "div.labelBkg", "span.edgeLabel", ".label > span", ".node .label-container", ".node .label-container > path", "marker", "marker.cross", "marker path", "marker circle", "marker line", ".pieCircle", ".slice", ".version", ".background", ".quadrants", ".quadrant", ".quadrant rect", ".quadrant text", ".border line", ".data-points", ".data-point", ".data-point circle", ".data-point text", ".labels text", ".title text", ".radarTitle", ".radarGraticule", ".radarAxisLine", ".radarAxisLabel", "[class^=radarCurve-]", "[class^=radarLegendBox-]", ".radarLegendText", ".chart-title text", ".plot", ".plot rect", ".plot path", ".plot text", ".bar-plot-0 rect", ".bar-plot-0 text", ".line-plot-1 path", ".line-plot-1 .labels text", ".bottom-axis .label text", ".bottom-axis .axis-line path", ".left-axis .title text", ".left-axis .ticks path", ".nodes", ".node", ".node rect", ".node-labels", ".node-labels text", ".sankey-label-bg", ".sankey-label-fg", ".links", ".link", ".link path", ".tree-view", ".tree-view > g", ".treeView-node-label", ".treeView-node-dir", ".treeView-node-line", ".treeView-node-description", ".treeView-highlight-bg", ".venn-title", ".venn-circle", ".venn-circle path", ".venn-circle text", ".venn-intersection", ".venn-intersection path", ".venn-intersection text", ".venn-text-nodes", ".venn-text-area", ".venn-text-node-fo", ".venn-text-node", ".swimlane.cluster", ".swimlane-title", ".swimlane-body", ".swimlane-label", ".swimlane-label span", ".swimlane-label text", ".ishikawa", ".ishikawa-spine", ".ishikawa-branch", ".ishikawa-sub-branch", ".ishikawa-arrow", ".ishikawa-head-group", ".ishikawa-head", ".ishikawa-head-label", ".ishikawa-pair", ".ishikawa-label-group", ".ishikawa-label-box", ".ishikawa-label.cause", ".ishikawa-sub-group", ".ishikawa-label.align", ".ishikawa-label.up", ".ishikawa-label.down", ".face", ".mouth", "line", ".task-line", ".legend", "rect.task", "text.task", ".journey-section", ".task-type-0", ".section-type-0", ".actor-0", ".actor-1", ".label", ".timeline-node", ".node-bkg", ".node-line--1", ".node-line-0", ".section--1", ".section-0", ".taskWrapper", ".eventWrapper", ".lineWrapper", ".lineWrapper line", ".timeline-node text", ".timeline-node path", ".timeline-node line", ".sections", ".items", ".sections rect", ".items rect", ".items .node", ".section-1", ".section-2", ".cluster", ".cluster-label", ".kanban-ticket-link", "span", ".nodeLabel", ".markdown-node-label", ".items line", ".items .node .label", ".commit-bullets", ".commit-labels", ".commit-arrows", ".branch", ".branch0", ".branch1", ".branch2", ".branchLabel", ".branch-label0", ".branch-label1", ".branch-label2", ".branchLabelBkg", ".label0", ".label1", ".label2", ".commit-label", ".commit-label-bkg", ".tag-label", ".tag-label-bkg", ".tag-hole", ".arrow", ".arrow0", ".arrow1", ".arrow2", ".gitTitleText", ".commit0", ".commit1", ".commit2", ".commit-merge", ".commit-reverse", ".commit-cherry-pick", ".commit-highlight-outer", ".commit-highlight-inner", ".commit-highlight0", ".person-man", ".person", "image", "text", "rect", "path", "polygon", "circle", "line",
  ".grid", ".grid .tick", ".grid .tick text", ".grid path", ".section", ".section0", ".section1",
  ".sectionTitle", ".sectionTitle0", ".sectionTitle1", ".task", ".task0", ".task1", ".done0",
  ".active0", ".crit1", ".milestone", ".taskText", ".taskText0", ".taskText1", ".doneText0",
  ".activeText0", ".critText1", ".milestoneText", ".taskTextOutsideLeft", ".taskTextOutsideRight",
  ".exclude-range", ".titleText", ".vert", ".vertText", ".today",
  ".em-box", ".em-box rect", ".em-swimlane", ".em-swimlane rect", ".em-swimlane text",
  ".em-relation", "marker", "marker polygon", "foreignObject", ".em-box div", ".em-box span",
  ".treemapContainer", ".treemapSection", ".treemapSection rect", ".treemapSectionHeader",
  ".treemapSectionLabel", ".treemapSectionValue", ".treemapNode", ".treemapLeafGroup",
  ".treemapLeaf", ".treemapLabel", ".treemapValue", ".treemapTitle", "svg.flowchart", "clipPath",
  ".cynefin-backgrounds", ".cynefinDomain", ".cynefin-boundaries", ".cynefinBoundary",
  ".cynefinCliff", ".cynefinConfusion", ".cynefin-labels", ".cynefinDomainLabel",
  ".cynefin-subtitles", ".cynefinSubtitle", ".cynefin-items", ".cynefinItem",
  ".cynefinItemText", ".cynefinItemOverflow", ".cynefin-arrows", ".cynefinArrowLine",
  ".cynefinArrowHead", ".cynefinArrowLabel", ".cynefinTitle",
  ".wardley-map", ".wardley-background", ".wardley-title", ".wardley-axes",
  ".wardley-axes line", ".wardley-axes path", ".wardley-axis-label",
  ".wardley-axis-label-x", ".wardley-axis-label-y", ".wardley-stages",
  ".wardley-stage-label", ".wardley-grid", ".wardley-grid line",
  ".wardley-pipelines", ".wardley-pipeline-box", ".wardley-pipeline-links",
  ".wardley-pipeline-evolution-link", ".wardley-links", ".wardley-link",
  ".wardley-link--dashed", ".wardley-link-label", ".wardley-trends",
  ".wardley-trend", ".wardley-trend line", ".wardley-nodes", ".wardley-node",
  ".wardley-node circle", ".wardley-node-label", ".wardley-annotations",
  ".wardley-annotation", ".wardley-annotation circle", ".wardley-annotation text",
  ".wardley-annotations-box", ".wardley-annotations-box rect",
  ".wardley-annotations-box text", ".wardley-notes", ".wardley-notes text",
  ".wardley-accelerators", ".wardley-deaccelerators", ".wardley-outsource-overlay",
  ".wardley-buy-overlay", ".wardley-build-overlay", ".wardley-market-overlay",
  ".wardley-market-line", ".wardley-market-dot", ".wardley-inertia",
  "defs marker path", ".architecture-edges", ".edge", ".arrow",
  ".architecture-services", ".architecture-service", ".architecture-junction",
  ".architecture-junction rect", ".architecture-groups", ".node-bkg",
  ".architecture-service-label", ".architecture-service-label text",
  ".railroad-diagram", ".railroad-rule", ".railroad-terminal",
  ".railroad-terminal rect", ".railroad-terminal text", ".railroad-nonterminal",
  ".railroad-nonterminal rect", ".railroad-nonterminal text", ".railroad-line",
  ".railroad-start", ".railroad-start circle", ".railroad-end",
  ".railroad-end circle", ".railroad-special", ".railroad-special rect",
  ".railroad-special text", ".railroad-rule-name",
];

const { default: puppeteer } = await import(pathToFileURL(path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true, args: ["--no-sandbox", "--allow-file-access-from-files", "--disable-gpu", "--force-device-scale-factor=1"] });
assert(await browser.version() === CHROME_PRODUCT, "Chrome product drifted");
const moduleUrl = pathToFileURL(moduleFile).href;
const fontUrl = pathToFileURL(fontFile).href;
const hostUrl = pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href;

const results = [];
const pixelCases = [];
for (const test of cases) {
  const page = await browser.newPage();
  await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
  await page.goto(hostUrl);
  const result = await page.evaluate(async ({ source, id, moduleUrl, fontUrl, selectors }) => {
    document.body.style.margin = "0";
    document.body.innerHTML = '<div id="container"></div>';
    const font = document.createElement("style");
    font.textContent = `@font-face{font-family:"Noto Sans";src:url("${fontUrl}");font-weight:400;font-style:normal}`;
    document.head.appendChild(font);
    await document.fonts.load('16px "Noto Sans"', "Alpha node Beta node Cluster title Edge label Outside Alice Bob Hello Root Child Other Header Body Probe first Safe-to-fail Best practice Drift Unaware Scattered Lost Shift Sense making Disorder Emergent → User Platform Service Supplier Market Evolved Genesis Custom Built Product Commodity Evolution Visibility First point Second note Watch here Fast Slow handoff landscape 1. 2. API Database Bare service flow if else yes no token item condition");
    await document.fonts.ready;
    const mermaid = (await import(`${moduleUrl}?theme-css=${id}`)).default;
    mermaid.initialize({ startOnLoad: false, logLevel: "fatal" });
    try {
      const rendered = await mermaid.render(`theme-css-${id}`, source);
      document.querySelector("#container").innerHTML = rendered.svg;
      const svg = document.querySelector("svg");
      const rootBox = svg.getBBox();
      const client = svg.getBoundingClientRect();
      const snapshot = (element) => {
        const style = getComputedStyle(element);
        const bbox = element.getBBox?.();
        const clientBox = element.getBoundingClientRect();
        // getIconSVG stamps each render with a random IconifyId* token; drop
        // the random suffix or the byte-identical double-run breaks (same
        // class of instability as gitGraph's cherry-pick commit id).
        const stableId = (value) =>
          typeof value === "string" && value.startsWith("IconifyId")
            ? "IconifyId"
            : value;
        const normalizedDomPart = (node) => {
          const tag = node.tagName.toLowerCase();
          const classes = [...node.classList].sort();
          return tag + classes.map((name) => `.${name}`).join("");
        };
        const domPath = (() => {
          const parts = [];
          for (let node = element; node && node.nodeType === Node.ELEMENT_NODE;
               node = node.parentElement) {
            parts.push(normalizedDomPart(node));
            if (node === svg) break;
          }
          return parts.reverse().join(" > ");
        })();
        const parent = element.parentElement;
        const effectiveOpacity = (() => {
          let value = 1;
          for (let node = element; node && node !== svg; node = node.parentElement)
            value *= Number.parseFloat(getComputedStyle(node).opacity || "1");
          return value;
        })();
        return {
          tag: element.tagName,
          id: stableId(element.id) || null,
          class: element.getAttribute("class"),
          dataId: element.getAttribute("data-id"),
          dataLook: element.getAttribute("data-look"),
          // RoughJS path data is intentionally random when no handDrawnSeed
          // is supplied. Geometry is frozen by each family's pixel/geometry
          // oracle; this cross-family CSS fixture records only stable attrs.
          attributes: Object.fromEntries([...element.attributes]
            .filter((attribute) => attribute.name !== "d")
            .map((attribute) => [attribute.name, stableId(attribute.value)])),
          ownerNodeId: stableId(element.closest?.(".node")?.id) || null,
          ownerClusterId: stableId(element.closest?.(".cluster")?.id) || null,
          parentTag: parent?.tagName ?? null,
          parentClass: parent?.getAttribute("class") ?? null,
          siblingIndex: parent ? [...parent.children].indexOf(element) : -1,
          domPath,
          effectiveOpacity,
          text: element.textContent,
          textLength: typeof element.getComputedTextLength === "function"
            ? element.getComputedTextLength() : null,
          bbox: bbox ? { x: bbox.x, y: bbox.y, width: bbox.width, height: bbox.height } : null,
          client: { x: clientBox.x, y: clientBox.y, width: clientBox.width, height: clientBox.height },
          ancestorDisplayed: (() => {
            for (let node = element; node && node !== svg; node = node.parentElement) {
              const ancestor = getComputedStyle(node);
              if (ancestor.display === "none" || ancestor.visibility === "hidden" ||
                  ancestor.visibility === "collapse") return false;
            }
            return true;
          })(),
          ancestorHasBox: (() => {
            for (let node = element; node && node !== svg; node = node.parentElement) {
              if (getComputedStyle(node).display === "none") return false;
            }
            return true;
          })(),
          computed: {
            fill: style.fill, stroke: style.stroke, strokeWidth: style.strokeWidth,
            color: style.color, backgroundColor: style.backgroundColor,
            fontFamily: style.fontFamily, fontSize: style.fontSize,
            fontWeight: style.fontWeight, fontStyle: style.fontStyle,
            display: style.display, visibility: style.visibility,
            opacity: style.opacity, fillOpacity: style.fillOpacity,
            strokeOpacity: style.strokeOpacity, filter: style.filter,
          },
        };
      };
      return {
        status: "ready",
        effectiveConfig: (() => {
          const config = mermaid.mermaidAPI.getConfig();
          return { fontFamily: config.fontFamily ?? null,
            themeFontFamily: config.themeVariables?.fontFamily ?? null,
            themeCSS: config.themeCSS ?? null,
            maxEdges: config.maxEdges ?? null,
            maxTextSize: config.maxTextSize ?? null,
            securityLevel: config.securityLevel ?? null };
        })(),
        diagramType: svg.getAttribute("aria-roledescription"),
        viewBox: svg.getAttribute("viewBox"),
        client: { width: client.width, height: client.height },
        bbox: { x: rootBox.x, y: rootBox.y, width: rootBox.width, height: rootBox.height },
        matches: Object.fromEntries(selectors.map((selector) => [selector, [...svg.querySelectorAll(selector)].map(snapshot)])),
        scopedThemeRules: [...svg.querySelectorAll("style")].map((node) => node.textContent)
          .flatMap((css) => css.split("}").map((rule) => rule.trim()).filter((rule) =>
            rule.includes("#theme-css-") &&
            (rule.includes("!important") || rule.includes("data-id") ||
             rule.includes("first-child") || rule.includes("nth-child") ||
             rule.includes("var(--") || rule.includes("display:none") ||
             rule.includes("font-size:32") || rule.includes("font-size:26")))),
      };
    } catch (error) {
      return { status: "error", name: error?.name ?? "", message: String(error?.message ?? error), hash: error?.hash ?? null };
    }
  }, { source: test.source, id: test.id, moduleUrl, fontUrl, selectors });
  if (test.id === "requirement-edge-painted-bg" && result.status === "ready") {
    const capture = await page.evaluate(() => {
      const svg = document.querySelector("svg");
      const span = svg.querySelector("span.edgeLabel");
      const svgRect = svg.getBoundingClientRect();
      const spanRect = span.getBoundingClientRect();
      return {
        clip: { x: svgRect.x, y: svgRect.y,
          width: Math.ceil(svgRect.width), height: Math.ceil(svgRect.height) },
        roi: { x: Math.floor(spanRect.x - svgRect.x + 2),
          y: Math.floor(spanRect.y - svgRect.y + 2), width: 2, height: 2 },
      };
    });
    fs.mkdirSync(pixelDir, { recursive: true });
    const file = `${test.id}.png`;
    const filePath = path.join(pixelDir, file);
    await page.screenshot({ path: filePath, clip: capture.clip,
      omitBackground: true, captureBeyondViewport: true });
    pixelCases.push({ id: test.id, source: test.source, file,
      sha256: sha(fs.readFileSync(filePath)),
      width: capture.clip.width, height: capture.clip.height,
      roi: capture.roi });
    result.pixel = { file, roi: capture.roi };
  }
  results.push({ id: test.id, source: test.source, ...result });
  await page.close();
}
await browser.close();

assert(results.length === cases.length, "Theme CSS case count drifted");
assert(results.every((test) => test.status === "ready"),
  `Every themeCSS probe must render: ${results.filter((test) => test.status !== "ready")
    .map((test) => `${test.id}: ${test.name}: ${test.message}`).join(" | ")}`);
const payload = {
  upstream: { version: VERSION, moduleSha256: MODULE_SHA, chromeProduct: CHROME_PRODUCT,
    chromeSha256: CHROME_SHA, fontSha256: NOTO_SHA },
  cases: results,
};
payload.fixtureSha256 = sha(JSON.stringify(payload));
fs.mkdirSync(path.dirname(outputFile), { recursive: true });
fs.writeFileSync(outputFile, `${JSON.stringify(payload, null, 2)}\n`);
const pixelPayload = {
  upstream: payload.upstream,
  cases: pixelCases,
};
pixelPayload.fixtureSha256 = sha(JSON.stringify(pixelPayload));
fs.mkdirSync(pixelDir, { recursive: true });
fs.writeFileSync(pixelManifestFile, `${JSON.stringify(pixelPayload, null, 2)}\n`);
console.log(JSON.stringify({ outputFile, cases: results.length, fixtureSha256: payload.fixtureSha256,
  fileSha256: sha(fs.readFileSync(outputFile)), pixelManifestFile,
  pixelFixtureSha256: pixelPayload.fixtureSha256,
  pixelFileSha256: sha(fs.readFileSync(pixelManifestFile)) }));
