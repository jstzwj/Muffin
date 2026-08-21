import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(process.argv[2] ??
  path.join("..", "mermaid-cli", "node_modules", "mermaid"));
const stateDbPath = path.resolve(process.argv[3] ??
  path.join("tests", "fixtures", "mermaid", "state-db.json"));
const output = path.resolve(process.argv[4] ??
  path.join("tests", "fixtures", "mermaid", "state-layout.json"));
const chrome = process.argv[5] ?? "C:/Program Files/Google/Chrome/Application/chrome.exe";
const pkg = JSON.parse(fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"));
const stateDb = JSON.parse(fs.readFileSync(stateDbPath, "utf8"));
if (pkg.version !== "11.16.0" || stateDb.upstream.version !== pkg.version)
  throw new Error("State layout oracle requires Mermaid 11.16.0 fixtures");

const selected = new Set([
  "transitions-start-end", "aliases-descriptions",
  "composite-direction-concurrency", "pseudostates", "accessibility-click",
  "external-edge-into-composite", "external-edge-into-explicit-composite",
  "note-after-composite",
]);
const cases = stateDb.cases.filter((item) => selected.has(item.id)).map((item) => ({
  id: item.id, source: item.source,
  expectedNodeIds: item.layoutInput.nodes.map((node) => node.id),
  expectedGroupFlags: item.layoutInput.nodes.map((node) => node.isGroup),
  expectedEdgeIds: item.layoutInput.edges.map((edge) => edge.id),
}));
cases.push({
  id: "renderable-note",
  source: "stateDiagram-v2\nActive --> Done\nnote right of Active : Inline note",
  expectedNodeIds: ["Active", "Done", "Active----parent", "Active----note-1"],
  expectedGroupFlags: [false, false, true, false],
  expectedEdgeIds: ["edge0", "Active-Active----note-1"],
});
cases.push({
  // The left-note edge direction, not a post-layout mirror, determines the
  // side. This guards the right-only fixture after removing native reflection.
  id: "renderable-left-note",
  source: "stateDiagram-v2\nActive --> Done\nnote left of Active : Left note",
  expectedNodeIds: ["Active", "Done", "Active----parent", "Active----note-1"],
  expectedGroupFlags: [false, false, true, false],
  expectedEdgeIds: ["edge0", "Active----note-1-Active"],
});
cases.push({
  // The note group participates in the same rank ordering as the fork/join
  // chain. Mermaid's insertion order places B diagonally, producing the
  // characteristic zig-zag rather than a straight vertical spine.
  id: "fork-join-note-zigzag",
  source: "stateDiagram-v2\nstate fork_state <<fork>>\n" +
    "state join_state <<join>>\nA --> fork_state\nfork_state --> B\n" +
    "B --> join_state\nnote right of B : Branch note",
  expectedNodeIds: ["fork_state", "join_state", "A", "B",
    "B----parent", "B----note-3"],
  expectedGroupFlags: [false, false, false, false, true, false],
  expectedEdgeIds: ["edge0", "edge1", "edge2", "B-B----note-3"],
});
cases.push({
  // Under handDrawn, State's rectWithTitle dispatch has historically differed
  // from the classic DOM. Capture the real node tree and rough ink extents.
  id: "handdrawn-rect-with-title",
  source: `%%{init: ${JSON.stringify({ look: "handDrawn", handDrawnSeed: 42 })}}%%\n` +
    "stateDiagram-v2\nDetailed : first row\nDetailed : second row",
  expectedNodeIds: ["Detailed"],
  expectedGroupFlags: [false],
  expectedEdgeIds: [],
});
cases.push({
  id: "handdrawn-basic-ink",
  source: `%%{init: ${JSON.stringify({ look: "handDrawn", handDrawnSeed: 42 })}}%%\n` +
    "stateDiagram-v2\n[*] --> Idle\nIdle --> Active : go\n" +
    "note right of Idle : Ink note\nActive --> [*]",
  expectedNodeIds: ["root_start", "Idle", "Active", "Idle----parent",
    "Idle----note-2", "root_end"],
  expectedGroupFlags: [false, false, false, true, false, false],
  expectedEdgeIds: ["edge0", "edge1", "Idle-Idle----note-2", "edge3"],
});
cases.push({
  // Title band contract: upstream insertTitle puts the text baseline
  // titleTopMargin above the content bbox and setupViewPortForSVG pads the
  // union — viewBox y = -(25 + font ascent + 8), height +52 (Noto 18px).
  id: "titled",
  source: "---\ntitle: Some Title\n---\nstateDiagram-v2\nA --> B",
  expectedNodeIds: ["A", "B"],
  expectedGroupFlags: [false, false],
  expectedEdgeIds: ["edge0"],
});

const notoDir = path.resolve("third_party", "noto", "fonts");
const fonts = [
  ["Noto Sans", "NotoSans-Regular.ttf", "U+0000-024F,U+1E00-1EFF"],
  ["Noto Sans CJK SC", "NotoSansCJKsc-Regular.otf", "U+2E80-9FFF,U+3040-30FF,U+AC00-D7AF"],
  ["Noto Sans Arabic", "NotoSansArabic-Regular.ttf", "U+0600-06FF,U+0750-077F,U+08A0-08FF"],
  ["Noto Sans Hebrew", "NotoSansHebrew-Regular.ttf", "U+0590-05FF"],
];
const fontFaces = fonts.map(([family, file, range]) =>
  `@font-face{font-family:"${family}";src:url("${pathToFileURL(path.join(notoDir, file)).href}");unicode-range:${range};}`
).join("\n");
const fontFamily = '"Noto Sans", "Noto Sans CJK SC", "Noto Sans Arabic", "Noto Sans Hebrew", sans-serif';

const { default: puppeteer } = await import(pathToFileURL(
  path.join(path.dirname(mermaidRoot), "puppeteer", "lib", "puppeteer", "puppeteer.js")).href);
const browser = await puppeteer.launch({ executablePath: chrome, headless: true,
  args: ["--allow-file-access-from-files"] });
try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1600, height: 1200, deviceScaleFactor: 1 });
  await page.goto(pathToFileURL(path.join(path.dirname(mermaidRoot), "..", "index.html")).href);
  const mermaidModule = pathToFileURL(path.join(mermaidRoot, "dist", "mermaid.esm.mjs")).href;
  const snapshots = await page.evaluate(async ({ cases, mermaidModule, fontFaces, fontFamily }) => {
    const { default: mermaid } = await import(mermaidModule);
    const style = document.createElement("style");
    style.textContent = fontFaces;
    document.head.appendChild(style);
    await document.fonts.load('16px "Noto Sans"', "State");
    await document.fonts.ready;
    const round = (value) => Math.round(value * 1000) / 1000;
    const point = (element, x = 0, y = 0) => {
      const value = new DOMPoint(x, y).matrixTransform(element.getCTM());
      return { x: value.x, y: value.y };
    };
    const relative = (value, origin) => ({
      x: round(value.x - origin.x), y: round(value.y - origin.y),
    });
    const bbox = (element) => {
      const value = element.getBBox();
      return { x: round(value.x), y: round(value.y),
        width: round(value.width), height: round(value.height) };
    };
    const svgTree = (element) => ({
      tag: element.tagName.toLowerCase(),
      classes: element.getAttribute("class") ?? "",
      style: element.getAttribute("style") ?? "",
      fill: element.getAttribute("fill") ?? "",
      stroke: element.getAttribute("stroke") ?? "",
      strokeWidth: element.getAttribute("stroke-width") ?? "",
      children: [...element.children]
        .filter((child) => child.namespaceURI === "http://www.w3.org/2000/svg")
        .map(svgTree),
    });
    const result = [];
    for (const fixture of cases) {
      mermaid.initialize({ startOnLoad: false, securityLevel: "strict",
        fontFamily, look: "classic", state: { padding: 8 } });
      const diagram = await mermaid.mermaidAPI.getDiagramFromText(fixture.source);
      const graph = diagram.db.getData();
      const { svg } = await mermaid.render(`state-layout-${result.length}`, fixture.source);
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(resolve));

      const regularData = graph.nodes.filter((node) => !node.isGroup);
      const groupData = graph.nodes.filter((node) => node.isGroup);
      const regularIds = fixture.expectedNodeIds.filter((_, index) =>
        !fixture.expectedGroupFlags[index]);
      const groupIds = fixture.expectedNodeIds.filter((_, index) =>
        fixture.expectedGroupFlags[index]);
      // click-wrapped nodes sit inside an <a>: g.nodes > a > g.node.
      const nodeElements = [...document.querySelectorAll(
        "g.nodes > g.node, g.nodes > a > g.node, " +
        "g.nodes > g.rough-node, g.nodes > a > g.rough-node")];
      const clusterElements = [...document.querySelectorAll("g.clusters > g[id]")];
      if (regularData.length !== regularIds.length || nodeElements.length !== regularIds.length ||
          groupData.length !== groupIds.length || clusterElements.length !== groupIds.length)
        throw new Error(`${fixture.id}: cannot associate state layout elements ` +
          `regular=${regularData.length}/${regularIds.length}/${nodeElements.length}, ` +
          `groups=${groupData.length}/${groupIds.length}/${clusterElements.length}`);
      const nodeCenters = nodeElements.map((element) => point(element));
      const origin = nodeCenters[0] ?? { x: 0, y: 0 };
      const nodes = nodeElements.map((element, index) => ({
        id: regularIds[index], center: relative(nodeCenters[index], origin),
        // updateNodeBounds consumes the first shape child, while the full
        // node getBBox may additionally include a rough divider or label ink.
        bbox: bbox(element.firstElementChild ?? element),
        inkBBox: bbox(element), shape: regularData[index].shape ?? "",
      }));
      const clusters = clusterElements.map((element, index) => {
        const box = element.getBBox();
        const center = point(element, box.x + box.width / 2, box.y + box.height / 2);
        return { id: groupIds[index], center: relative(center, origin),
          bbox: bbox(element), shape: groupData[index].shape ?? "" };
      });
      const edges = graph.edges.map((edge, index) => {
        const id = fixture.expectedEdgeIds[index];
        const pathElement = document.querySelector(`g.edgePaths path[data-id="${edge.id}"]`);
        const labelElement = [...document.querySelectorAll("g.edgeLabel > g.label")]
          .find((element) => element.getAttribute("data-id") === edge.id)?.parentElement;
        if (!pathElement) throw new Error(`${fixture.id}: missing edge path ${edge.id}`);
        const length = pathElement.getTotalLength();
        const start = point(pathElement, pathElement.getPointAtLength(0).x,
          pathElement.getPointAtLength(0).y);
        const endPoint = pathElement.getPointAtLength(length);
        const end = point(pathElement, endPoint.x, endPoint.y);
        const label = labelElement?.hasAttribute("transform") ? point(labelElement) : null;
        return { id, path: pathElement.getAttribute("d") ?? "",
          start: relative(start, origin), end: relative(end, origin),
          labelCenter: label ? relative(label, origin) : null };
      });
      const svgElement = document.querySelector("svg");
      const computedRect = (element) => {
        const s = getComputedStyle(element);
        return { fill: s.fill, stroke: s.stroke, strokeWidth: s.strokeWidth,
                 dasharray: s.strokeDasharray, rx: s.rx };
      };
      const structure = {
        root: {
          role: svgElement.getAttribute("role") ?? "",
          ariaRoledescription: svgElement.getAttribute("aria-roledescription") ?? "",
          viewBox: svgElement.getAttribute("viewBox") ?? "",
        },
        markers: [...svgElement.querySelectorAll("defs marker")].map((marker) => ({
          markerWidth: marker.getAttribute("markerWidth") ?? "",
          markerHeight: marker.getAttribute("markerHeight") ?? "",
          orient: marker.getAttribute("orient") ?? "",
          refX: marker.getAttribute("refX") ?? "",
          refY: marker.getAttribute("refY") ?? "",
          viewBox: marker.getAttribute("viewBox") ?? "",
          childTag: marker.firstElementChild?.tagName.toLowerCase() ?? "",
          childPathD: marker.firstElementChild?.getAttribute("d") ?? "",
        })),
        // Upstream click contract: the rendered node's <g> is wrapped in an
        // <a xlink:href title> AFTER layout (draw(), state chunk).
        anchors: [...svgElement.querySelectorAll("a")].map((a) => ({
          href: a.getAttribute("href") ?? a.getAttribute("xlink:href") ?? "",
          title: a.getAttribute("title") ?? "",
          wrapsClass: a.firstElementChild?.getAttribute("class") ?? "",
        })),
        nodes: nodeElements.map((element, index) => ({
          id: regularIds[index], classes: element.getAttribute("class") ?? "",
          childTags: [...element.children].map((child) => child.tagName.toLowerCase()),
          foreignObjectCount: element.querySelectorAll("foreignObject").length,
          textCount: element.querySelectorAll("text").length,
          rectCount: element.querySelectorAll("rect").length,
          lineCount: element.querySelectorAll("line").length,
          tree: svgTree(element),
          paths: [...element.querySelectorAll("path")].map((path) => ({
            classes: path.getAttribute("class") ?? "",
            bbox: bbox(path),
            strokeLinecap: getComputedStyle(path).strokeLinecap,
            strokeLinejoin: getComputedStyle(path).strokeLinejoin,
            strokeMiterlimit: getComputedStyle(path).strokeMiterlimit,
          })),
        })),
        clusters: clusterElements.map((element, index) => ({
          id: groupIds[index], classes: element.getAttribute("class") ?? "",
          childTags: [...element.children].map((child) => child.tagName.toLowerCase()),
          rects: [...element.querySelectorAll("rect")].map((rect) => ({
            class: rect.getAttribute("class") ?? "",
            computed: computedRect(rect),
          })),
        })),
        edges: graph.edges.map((edge, index) => {
          const element = document.querySelector(`g.edgePaths path[data-id="${edge.id}"]`);
          const markerEnd = element?.getAttribute("marker-end") ?? "";
          const s = element ? getComputedStyle(element) : null;
          return { id: fixture.expectedEdgeIds[index],
            classes: element?.getAttribute("class") ?? "",
            markerEnd: markerEnd.includes("barbEnd") ? "barbEnd" : "",
            pathCommands: (element?.getAttribute("d")?.match(/[A-Za-z]/g) ?? []).join(""),
            computed: s ? { stroke: s.stroke, strokeWidth: s.strokeWidth,
                            dasharray: s.strokeDasharray } : null,
          };
        }),
      };
      result.push({ id: fixture.id, source: fixture.source,
        geometry: { nodes, clusters, edges, viewBox: svgElement.getAttribute("viewBox") },
        structure });
    }
    // ---- themeCSS differential: per-element computed styles against the
    // real state DOM (structural selectors must resolve like the browser).
    const themeCssCases = [
      // note+composite and external-edge-into-cluster combinations avoided:
      // both have open pre-existing layout divergences (tracked in
      // docs/mermaid-architecture.md), not themeCSS channels.
      { id: "state-theme-structural",
        source: "stateDiagram-v2\nstate Running {\n  A --> B : label\n  B --> C\n}",
        themeCSS: ".node:nth-of-type(1) rect { fill: rgb(255,0,0) !important; } " +
          ".node + .node rect { stroke: rgb(0,255,0); } " +
          "path.transition { stroke: rgb(255,0,255); } " +
          // The hidden second transition must not leave a dangling
          // arrowhead in the SVG marker overlay.
          "g.edgePaths path:nth-of-type(2) { display: none !important; } " +
          ".edgeLabel p { background-color: rgb(128,0,128) !important; } " +
          ".statediagram-cluster rect.inner { fill: rgb(255,255,0) !important; }" },
      { id: "state-theme-hidden",
        source: "stateDiagram-v2\nA --> B\nnote right of A : note text",
        themeCSS: ".node rect { display:none !important; } " +
          ".statediagram-note rect { fill: rgb(0,0,255) !important; } " +
          ".note-edge { display:none !important; }" },
      { id: "state-theme-font",
        source: "stateDiagram-v2\nA --> B : label",
        themeCSS: ".nodeLabel { font-size: 23px !important; }" },
      // Cluster titles measure with their span font: the composite box and
      // the viewBox grow (browser: 113.5625 -> 130.796875 for this case).
      { id: "state-theme-cluster-font",
        source: "stateDiagram-v2\nstate Running {\n  A --> B\n}",
        themeCSS: ".cluster-label { font-size: 31px !important; }" },
      // Paint semantics: stroke:none disables the pen (no black fallback),
      // visibility:hidden hides ONLY paint (label text AND rect frames —
      // the layout box/viewBox must stay untouched), and a transparent p
      // background clears the edge-label chip.
      { id: "state-theme-paint-semantics",
        source: "stateDiagram-v2\nA --> B : label\nnote right of A : note text",
        themeCSS: ".node rect { stroke: none; visibility: hidden; } " +
          ".nodeLabel { visibility: hidden; } " +
          ".edgeLabel p { background-color: transparent; }" },
      // The marker defs rule is the raster arrowhead color channel;
      // stroke:none on the transition itself removes the line (and with it
      // the marker-end rendering) without degrading to a black hairline.
      { id: "state-theme-marker",
        source: "stateDiagram-v2\nA --> B",
        themeCSS: "defs [id$=\"-barbEnd\"] { fill: rgb(0,128,0) !important; " +
          "stroke: rgb(0,128,0) !important; opacity: 0.2; " +
          "stroke-width: 4px; } " +
          "path.transition { stroke: none; }" },
      // Opacity channel composition: element `opacity` and the per-channel
      // fill-opacity / stroke-opacity are INDEPENDENT factors — the used
      // channel alpha is color alpha x opacity x channel, each applied
      // exactly once (a model that stores the engine's effective channel
      // and multiplies opacity again squares it: 0.2 renders 0.04). The
      // marker's fill-opacity reaches the referenced path by INHERITANCE
      // (fill-opacity inherits; opacity does not).
      { id: "state-theme-opacity",
        source: "stateDiagram-v2\nstate Running {\n  A --> B : label\n}",
        themeCSS: ".node rect { opacity: 0.2; } " +
          "path.transition { stroke-opacity: 0.4; } " +
          ".statediagram-cluster rect.outer { fill-opacity: 0.6; } " +
          "defs [id$=\"-barbEnd\"] { fill-opacity: 0.3; } " +
          ".edgeLabel p { opacity: 0.5; }" },
      // DOM gaps: clicked nodes are wrapped in <a> (g.nodes > a > g.node
      // carrying xlink:href + title — ATTRIBUTE selectors like a[title]
      // must match), and rectWithTitle descriptions are the second
      // foreignObject INSIDE g.label — both selectable and hideable
      // independently.
      { id: "state-theme-dom",
        source: "stateDiagram-v2\nstate Running {\n  b2 : row one\n" +
          "  b2 : row two\n  b2 --> C\n}\n" +
          "click b2 \"https://example.com\" \"tip\"",
        themeCSS: "a[title=\"tip\"] .node rect { fill: rgb(255,0,0) !important; } " +
          "g.label foreignObject:nth-of-type(2) { visibility: hidden; }" },
      // handDrawn CSS channels: the rough pair's SECOND path (the outline)
      // hides via :nth-of-type while the hachure fill path stays, and the
      // transition/marker channels restyle as usual. Computed-style only:
      // rough INK-extent parity (canvas size) stays open — the rough
      // geometry is a separate workstream.
      { id: "state-theme-handdrawn", look: "handDrawn",
        source: "stateDiagram-v2\nA --> B : go\nnote right of A : n",
        themeCSS: "defs [id$=\"-barbEnd\"] { opacity: 0.5; } " +
          ".rough-node path:nth-of-type(2) { stroke: none; } " +
          "path.transition { stroke-width: 2px; }" },
      // rectWithTitle is handDrawn's DOM exception: it remains `.node`,
      // with a classless rough pair in child g #1 and the rough divider in
      // child g #2. Lock all three path channels independently; a folded
      // rect/line model or `.rough-node` class cannot satisfy this case.
      { id: "state-theme-handdrawn-titled", look: "handDrawn",
        source: "stateDiagram-v2\nDetailed : first row\nDetailed : second row",
        themeCSS: ".rough-node path { display: none; } " +
          ".node g:first-child path:first-child { " +
          "stroke: rgb(0,128,0) !important; stroke-width: 3px !important; } " +
          ".node g:first-child path + path { " +
          "stroke: rgb(255,0,255) !important; stroke-width: 2px !important; } " +
          ".node g:first-child + g path { " +
          "stroke: rgb(0,0,255) !important; stroke-width: 5px !important; }" },
      { id: "state-theme-handdrawn-titled-style", look: "handDrawn",
        source: "stateDiagram-v2\nDetailed : first row\nDetailed : second row\n" +
          "style Detailed fill:#ff0000,stroke:#0000ff,stroke-width:3px",
        themeCSS: "" },
      // The label <p> carries the TEXT: its own font-size/color restyle the
      // glyphs AND feed the label-box measurement (the fo renders at the p's
      // computed font), so the edge-label chip, the node boxes, and the
      // viewBox all grow (edge p 31px: 41.4375x170 -> 49.78125x192.5).
      { id: "state-theme-label-p",
        source: "stateDiagram-v2\nA --> B : label",
        themeCSS: ".edgeLabel p { font-size: 31px; color: rgb(255,0,0); }" },
      { id: "state-theme-label-p-node-cluster",
        source: "stateDiagram-v2\nstate Running {\n  A --> B : label\n}",
        themeCSS: ".nodeLabel p { font-size: 24px; color: rgb(0,0,255); } " +
          ".cluster-label p { font-size: 28px; }" },
      // display:none on the p collapses the LABEL BOX (the fo renders
      // nothing): the edge label reserves no space — the edge paths and the
      // viewBox shrink, not just the paint.
      { id: "state-theme-label-p-hidden",
        source: "stateDiagram-v2\nA --> B : label",
        themeCSS: ".edgeLabel p { display: none; }" },
      { id: "state-theme-label-p-hidden-node",
        source: "stateDiagram-v2\nA --> B : label",
        themeCSS: ".nodeLabel p { display: none; }" },
      // rectWithTitle description rows are the second fo's own p — its font
      // feeds the row measurement (titled node grows) and the row paint.
      { id: "state-theme-desc-p",
        source: "stateDiagram-v2\nstate Running {\n  b2 : row one\n" +
          "  b2 : row two\n  b2 --> C\n}",
        themeCSS: "g.label foreignObject:nth-of-type(2) p { font-size: 24px; " +
          "color: rgb(0,128,0); }" },
      // display:none on the DESC p collapses the description block in the
      // LAYOUT too: the titled node measures title-only (browser: the node
      // box drops from 64.921875x65 to 64.921875x32, viewBox height 271 ->
      // 238) — dagre keeps no reserved height for the hidden rows.
      { id: "state-theme-desc-p-hidden",
        source: "stateDiagram-v2\nstate Running {\n  b2 : row one\n" +
          "  b2 : row two\n  b2 --> C\n}",
        themeCSS: "g.label foreignObject:nth-of-type(2) p { display: none; }" },
    ];
    const themeCssResults = [];
    for (const themeCase of themeCssCases) {
      const init = { themeCSS: themeCase.themeCSS };
      if (themeCase.look) {
        init.look = themeCase.look;
        init.handDrawnSeed = 42;
      }
      const directive = `%%{init: ${JSON.stringify(init)}}%%\n`;
      const { svg } = await mermaid.render(
        "state-theme-" + themeCase.id, directive + themeCase.source);
      document.getElementById("container").innerHTML = svg;
      await document.fonts.ready;
      await new Promise((resolve) => requestAnimationFrame(resolve));
      const svgElement = document.querySelector("svg");
      const client = svgElement.getBoundingClientRect();
      themeCssResults.push({
        id: themeCase.id, source: themeCase.source, themeCSS: themeCase.themeCSS,
        look: themeCase.look ?? "classic",
        client: { width: client.width, height: client.height },
        viewBox: svgElement.getAttribute("viewBox") ?? "",
        nodes: [...svgElement.querySelectorAll(
            "g.nodes > g.node, g.nodes > a > g.node, g.nodes > g.rough-node, g.nodes > a > g.rough-node")]
          .map((g) => {
            // The node's SHAPE element in document order — a rect for
            // rect/rectWithTitle (the 0x0 label-background rect sits inside
            // g.label AFTER the shape), circle.state-start, and the rough
            // FILL path first for note/choice/fork/end.
            const shape = g.querySelector("rect, circle, path");
            const span = g.querySelector("span.nodeLabel");
            const s = shape ? getComputedStyle(shape) : null;
            // Rough pairs (note/fork/choice/end + every shape under the
            // handDrawn look) carry a SECOND path — the outline element.
            const roughPaths = g.querySelectorAll("path");
            const strokeShape = roughPaths.length > 1 ? roughPaths[1] : null;
            const ss = strokeShape ? getComputedStyle(strokeShape) : null;
            const dividerShape = g.children[1]?.querySelector(":scope > path") ?? null;
            const ds = dividerShape ? getComputedStyle(dividerShape) : null;
            // rectWithTitle descriptions: the second foreignObject inside
            // g.label (visibility/display gate the description rows).
            // querySelector lowercases tags inside inline SVG in HTML.
            const desc = g.querySelector("g.label foreignobject:nth-of-type(2)");
            // The label's <p> (the text wrapper) and the description rows'
            // p (inside the second fo): their OWN computed font/color/display
            // channels — the text's used style and the label-box measurement
            // both read these, not the span's.
            const labelP = g.querySelector("span.nodeLabel p");
            const descP = g.querySelector(
                "g.label foreignobject:nth-of-type(2) p");
            return { label: g.textContent.trim(),
              // Local getBBox: the node box (shape + label) in the g's own
              // coordinate space — grounds the display:none collapse sizes.
              bbox: ((b) => ({ width: b.width, height: b.height }))(g.getBBox()),
              shapeTag: shape ? shape.tagName.toLowerCase() : "",
              shape: s ? { fill: s.fill, stroke: s.stroke, strokeWidth: s.strokeWidth,
                           dasharray: s.strokeDasharray, display: s.display,
                           visibility: s.visibility, opacity: s.opacity,
                           fillOpacity: s.fillOpacity,
                           strokeOpacity: s.strokeOpacity } : null,
              span: span ? { color: getComputedStyle(span).color,
                             fontSize: getComputedStyle(span).fontSize,
                             visibility: getComputedStyle(span).visibility } : null,
              desc: desc ? { visibility: getComputedStyle(desc).visibility,
                             display: getComputedStyle(desc).display } : null,
              p: labelP ? { fontSize: getComputedStyle(labelP).fontSize,
                            fontFamily: getComputedStyle(labelP).fontFamily,
                            color: getComputedStyle(labelP).color,
                            display: getComputedStyle(labelP).display,
                            visibility: getComputedStyle(labelP).visibility,
                            opacity: getComputedStyle(labelP).opacity } : null,
              descP: descP ? { fontSize: getComputedStyle(descP).fontSize,
                               fontFamily: getComputedStyle(descP).fontFamily,
                               color: getComputedStyle(descP).color,
                               display: getComputedStyle(descP).display,
                               visibility: getComputedStyle(descP).visibility } : null,
              strokeShape: ss ? { stroke: ss.stroke, strokeWidth: ss.strokeWidth,
                                  display: ss.display,
                                  visibility: ss.visibility, opacity: ss.opacity,
                                  fillOpacity: ss.fillOpacity,
                                  strokeOpacity: ss.strokeOpacity } : null,
              dividerShape: ds ? { stroke: ds.stroke,
                                   strokeWidth: ds.strokeWidth,
                                   display: ds.display,
                                   visibility: ds.visibility,
                                   opacity: ds.opacity,
                                   strokeOpacity: ds.strokeOpacity } : null };
          }),
        clusters: [...svgElement.querySelectorAll("g.clusters > g")].map((g) => ({
          classes: g.getAttribute("class") ?? "",
          outer: g.querySelector("rect.outer")
            ? { fill: getComputedStyle(g.querySelector("rect.outer")).fill,
                opacity: getComputedStyle(g.querySelector("rect.outer")).opacity,
                fillOpacity: getComputedStyle(g.querySelector("rect.outer")).fillOpacity }
            : null,
          inner: g.querySelector("rect.inner")
            ? { fill: getComputedStyle(g.querySelector("rect.inner")).fill } : null,
          divider: g.querySelector("rect.divider")
            ? { fill: getComputedStyle(g.querySelector("rect.divider")).fill,
                dasharray: getComputedStyle(g.querySelector("rect.divider")).strokeDasharray }
            : null,
          label: g.querySelector(".cluster-label span")
            ? { fontSize: getComputedStyle(g.querySelector(".cluster-label span")).fontSize,
                color: getComputedStyle(g.querySelector(".cluster-label span")).color }
            : null,
          labelP: g.querySelector(".cluster-label p")
            ? { fontSize: getComputedStyle(g.querySelector(".cluster-label p")).fontSize,
                color: getComputedStyle(g.querySelector(".cluster-label p")).color,
                display: getComputedStyle(g.querySelector(".cluster-label p")).display }
            : null,
        })),
        edges: [...svgElement.querySelectorAll("g.edgePaths path")].map((p) => {
          const s = getComputedStyle(p);
          return { class: p.getAttribute("class") ?? "", stroke: s.stroke,
                   dasharray: s.strokeDasharray, display: s.display,
                   strokeOpacity: s.strokeOpacity };
        }),
        edgeLabelP: [...svgElement.querySelectorAll("g.edgeLabel p")].map((p) => {
          const s = getComputedStyle(p);
          // The p's OWN channels: opacity composes onto background AND text
          // (it sits inside the span), display/visibility hide the whole
          // label, and the font pair is the TEXT's used style (feeding the
          // label-box measurement and the paint font).
          return { background: s.backgroundColor, opacity: s.opacity,
                   display: s.display, visibility: s.visibility,
                   fontSize: s.fontSize, fontFamily: s.fontFamily,
                   color: s.color };
        }),
        markers: [...svgElement.querySelectorAll("defs marker path")].map((p) => {
          const s = getComputedStyle(p);
          // `defs [id$="-barbEnd"]` matches the MARKER element (the id
          // carrier): opacity is NOT inherited so the path's computed value
          // stays 1 — the marker's own opacity is what fades the rendered
          // arrowhead (used value = marker.opacity x path.opacity).
          const m = getComputedStyle(p.parentElement);
          return { fill: s.fill, stroke: s.stroke, opacity: s.opacity,
                   strokeWidth: s.strokeWidth, fillOpacity: s.fillOpacity,
                   strokeOpacity: s.strokeOpacity, display: s.display,
                   visibility: s.visibility, markerOpacity: m.opacity };
        }),
      });
    }
    return { cases: result, themeCss: themeCssResults };
  }, { cases, mermaidModule, fontFaces, fontFamily });
  const payload = { upstream: { version: pkg.version },
    fontMode: "bundled-noto-2.13b171", cases: snapshots.cases,
    themeCss: snapshots.themeCss };
  payload.fixtureSha256 = createHash("sha256").update(JSON.stringify(payload)).digest("hex");
  fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
  console.log(`Wrote ${snapshots.cases.length} state layout cases to ${output}`);
  console.log(`fixtureSha256=${payload.fixtureSha256}`);
} finally {
  await browser.close();
}
