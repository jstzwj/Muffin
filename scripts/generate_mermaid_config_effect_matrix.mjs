import { createHash } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const mermaidRoot = path.resolve(
  process.argv[2] ?? path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ??
    path.join("tests", "fixtures", "mermaid", "config-effect-matrix.json"),
);
const packageJson = JSON.parse(
  fs.readFileSync(path.join(mermaidRoot, "package.json"), "utf8"),
);
if (packageJson.version !== "11.16.0") {
  throw new Error(`Expected mermaid 11.16.0, found ${packageJson.version}`);
}

const configTypesPath = path.join(mermaidRoot, "dist", "config.type.d.ts");
// Treemap's diagram-local config interface is not emitted into Mermaid's main
// generated config.type.d.ts in 11.16.0. Keep it in the same upstream-driven
// interface audit by reading the shipped diagram declaration as well.
const treemapTypesPath = path.join(
  mermaidRoot,
  "dist",
  "diagrams",
  "treemap",
  "types.d.ts",
);
const configTypes =
  fs.readFileSync(configTypesPath, "utf8") +
  "\n" +
  fs.readFileSync(treemapTypesPath, "utf8");
const { defaultConfig } = await import(
  pathToFileURL(
    path.join(
      mermaidRoot,
      "dist",
      "chunks",
      "mermaid.core",
      "chunk-WYO6CB5R.mjs",
    ),
  )
);

const dimensions = [
  "parsed",
  "layout",
  "text",
  "paint",
  "viewport",
  "interaction",
  "export",
];

const policy = (status, upstream, native, note) => ({
  status,
  upstream,
  native,
  ...(note ? { note } : {}),
});
const parity = (...effects) => policy("parity", effects, effects);
const inert = (note) => policy("upstream-inert", [], [], note);
const deferred = (effects, note) => policy("deferred", effects, [], note);
const unsupported = (effects, note) =>
  policy("unsupported", effects, [], note);
const partial = (upstream, native, note) =>
  policy("partial", upstream, native, note);
const legacyOnly = (effects, note) =>
  policy("legacy-only", effects, [], note);
const apiOnly = (effects, note) => policy("api-only", effects, [], note);

const layout = ["layout", "paint", "viewport", "export"];
const interactiveLayout = [
  "layout",
  "paint",
  "viewport",
  "interaction",
  "export",
];
const textLayout = ["text", "layout", "paint", "viewport", "export"];
const railroadFamilies = [
  "railroad",
  "railroadEbnf",
  "railroadAbnf",
  "railroadPeg",
];
const allRailroad = (classification) => ({
  ...classification,
  families: railroadFamilies,
});

const c4ShapeTypes = [
  "person", "external_person", "system", "external_system",
  "system_db", "external_system_db", "system_queue", "external_system_queue",
  "container", "external_container", "container_db", "external_container_db",
  "container_queue", "external_container_queue", "component", "external_component",
  "component_db", "external_component_db", "component_queue", "external_component_queue",
];
const c4Policies = {
  useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
  useMaxWidth: parity("viewport", "export"),
  diagramMarginX: parity(...layout),
  diagramMarginY: parity(...layout),
  c4ShapeMargin: parity(...layout),
  c4ShapePadding: parity(...layout),
  width: parity(...layout),
  height: parity(...layout),
  boxMargin: parity(...layout),
  c4ShapeInRow: inert(
    "The renderer reads C4DB's diagram-local value; source UpdateLayoutConfig changes it, but config.c4ShapeInRow does not.",
  ),
  nextLinePaddingX: parity(...layout),
  c4BoundaryInRow: inert(
    "The renderer reads C4DB's diagram-local value; source UpdateLayoutConfig changes it, but config.c4BoundaryInRow does not.",
  ),
  wrap: parity(...textLayout),
  wrapPadding: parity(...textLayout),
};
for (const shape of [...c4ShapeTypes, "boundary", "message"]) {
  c4Policies[`${shape}FontSize`] = parity(...textLayout);
  c4Policies[`${shape}FontFamily`] = parity(...textLayout);
  c4Policies[`${shape}FontWeight`] = parity(...textLayout);
  c4Policies[`${shape}Font`] = apiOnly(
    textLayout,
    "Function-valued Mermaid API hooks cannot be represented in Markdown JSON/YAML config.",
  );
}
for (const shape of c4ShapeTypes) {
  c4Policies[`${shape}_bg_color`] = parity("paint", "export");
  c4Policies[`${shape}_border_color`] = parity("paint", "export");
}

// This table is the reviewed semantic policy. Interface membership and defaults
// are generated from Mermaid itself below, so adding or removing an upstream
// field makes generation fail until its effect is classified here.
const familyPolicies = {
  flowchart: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: {
      ...parity("viewport", "export"),
      families: ["flowchart", "swimlane"],
      note: "Swimlane reuses flowchart root sizing; swimlane.useMaxWidth itself is inert.",
    },
    titleTopMargin: {
      ...parity("paint", "viewport", "export"),
      families: ["flowchart", "swimlane"],
    },
    subGraphTitleMargin: parity("layout", "viewport", "export"),
    arrowMarkerAbsolute: inert(
      "Mermaid 11.16.0 reads only the root arrowMarkerAbsolute key for flowchart SVG markers.",
    ),
    diagramPadding: {
      ...parity("viewport", "export"),
      families: ["flowchart", "swimlane"],
    },
    htmlLabels: parity(...textLayout),
    nodeSpacing: {
      ...parity(...interactiveLayout),
      families: ["flowchart", "swimlane"],
    },
    rankSpacing: {
      ...parity(...interactiveLayout),
      families: ["flowchart", "swimlane"],
    },
    curve: {
      ...parity(...interactiveLayout),
      families: ["flowchart", "swimlane"],
    },
    padding: {
      ...parity("text", ...interactiveLayout),
      families: ["flowchart", "swimlane"],
    },
    defaultRenderer: parity(...interactiveLayout),
    wrappingWidth: parity(...textLayout),
    inheritDir: parity(...interactiveLayout),
  },
  swimlane: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: inert(
      "Swimlane 11.16.0 configures its SVG from flowchart.useMaxWidth; the same-named Swimlane field is retained but inert.",
    ),
    lineHops: parity(...layout),
    ignoreCrossLaneEdges: parity(...interactiveLayout),
    optimizeRanksByCrossings: inert(
      "The 11.16.0 crossing lift is unreachable after longest-path initialization: every node has lower-bound >= current rank.",
    ),
    automaticLaneOrdering: parity(...interactiveLayout),
  },
  sequence: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    arrowMarkerAbsolute: inert(
      "Mermaid 11.16.0 reads only the root arrowMarkerAbsolute key for sequence SVG markers.",
    ),
    hideUnusedParticipants: parity(...layout),
    activationWidth: parity(...layout),
    diagramMarginX: parity("viewport", "export"),
    diagramMarginY: parity("viewport", "export"),
    actorMargin: parity(...layout),
    width: parity("text", ...layout),
    height: parity(...layout),
    boxMargin: parity(...layout),
    boxTextMargin: parity(...layout),
    noteMargin: parity(...layout),
    messageMargin: inert(
      "Mermaid 11.16.0 resolves this value but does not consume it in sequence layout.",
    ),
    messageAlign: {
      ...parity("text", "paint", "export"),
      note: "Upstream start/middle/end maps to Left/Center/Right; paint-only horizontal placement within the full message span [min(startx,stopx), max(startx,stopx)], inset by wrapPadding. Plain-text labels only — Math labels stay centered (drawKatex ignores the anchor).",
    },
    mirrorActors: parity(...layout),
    forceMenus: parity("paint", "interaction", "export"),
    bottomMarginAdj: parity("viewport", "export"),
    rightAngles: parity("layout", "paint", "export"),
    showSequenceNumbers: parity("text", ...layout),
    // Mermaid 11.16.0 sequence per-label fonts are governed by setConf()
    // (sequenceDiagram-DXCB7GA4.mjs), called as setConf(getConfig2()) in the
    // sequence DB init. It deep-merges the resolved config, then mirrors the
    // GLOBAL fontFamily/fontSize/fontWeight into all three per-label fields
    // whenever the global value is truthy:
    //     if (cnf.fontFamily) conf.actorFontFamily=conf.noteFontFamily=conf.messageFontFamily=cnf.fontFamily;
    //     (likewise fontSize, fontWeight)
    // The global fontFamily ("trebuchet ms, ...") and fontSize (16) defaults are
    // non-empty, so those mirrors fire unconditionally -> the six per-label
    // Family/Size keys are DEAD config: the renderer reads conf.actorFontSize
    // etc., but the value is always the global, never the user's per-label
    // setting. Verified via an 11-key headless-Chrome probe (mermaid 11.16.0):
    // actorFontSize:8 leaves the actor at 16px; actorFontFamily:"Courier New"
    // leaves trebuchet; same for note/message Size/Family; and setting the
    // global always propagates to all three. The global fontWeight default is
    // undefined, so its mirror is skipped and the three per-label FontWeight
    // keys are CONDITIONALLY live (effective only when global fontWeight is
    // unset; a truthy global fontWeight overrides all three — verified). Native
    // honors each per-kind weight: participant/box/menu -> actor, note -> note,
    // message/fragment -> message (fragment kind tag included); "normal"->400,
    // "bold"->700, numerics pass through on the CSS 1..1000 scale (Qt 6 uses the
    // same scale). Math note/message labels render Normal regardless, because
    // mermaid drawKatex() ignores font-weight (verified). messageAlign/noteAlign
    // are direct text-anchor consumption.
    actorFontSize: inert(
      "Dead config in mermaid 11.16.0 — setConf() unconditionally mirrors the truthy global fontSize into actorFontSize/noteFontSize/messageFontSize, so the per-label value is never the user's.",
    ),
    actorFontFamily: inert(
      "Dead config in mermaid 11.16.0 — setConf() unconditionally mirrors the truthy global fontFamily into all three per-label families, so the per-label value is never the user's.",
    ),
    actorFontWeight: {
      ...parity(...textLayout),
      note: "Conditionally live: effective only when global fontWeight is unset (setConf mirrors a truthy global into all three). Native honors it for participant/box/menu labels. Math labels render Normal (drawKatex ignores weight).",
    },
    noteFontSize: inert("Dead config — setConf() mirrors the global fontSize; see actorFontSize."),
    noteFontFamily: inert("Dead config — setConf() mirrors the global fontFamily; see actorFontFamily."),
    noteFontWeight: {
      ...parity(...textLayout),
      note: "Conditionally live when global fontWeight is unset (a truthy global overrides it). Native honors it for note labels. Math labels render Normal (drawKatex ignores weight).",
    },
    noteAlign: {
      ...parity("text", "paint", "export"),
      note: "Upstream start/middle/end maps to Left/Center/Right; paint-only horizontal placement within the note rect, inset by noteMargin. Plain-text labels only — Math labels stay centered (drawKatex ignores the anchor).",
    },
    messageFontSize: inert("Dead config — setConf() mirrors the global fontSize; see actorFontSize."),
    messageFontFamily: inert("Dead config — setConf() mirrors the global fontFamily; see actorFontFamily."),
    messageFontWeight: {
      ...parity(...textLayout),
      note: "Conditionally live when global fontWeight is unset (a truthy global overrides it). Native honors it for message and fragment labels (the fragment kind tag included). Math labels render Normal (drawKatex ignores weight).",
    },
    wrap: parity(...textLayout),
    wrapPadding: parity(...textLayout),
    labelBoxWidth: parity(...layout),
    labelBoxHeight: parity(...layout),
    messageFont: apiOnly(
      textLayout,
      "Function-valued Mermaid API hooks cannot be represented in Markdown JSON/YAML config.",
    ),
    noteFont: apiOnly(
      textLayout,
      "Function-valued Mermaid API hooks cannot be represented in Markdown JSON/YAML config.",
    ),
    actorFont: apiOnly(
      textLayout,
      "Function-valued Mermaid API hooks cannot be represented in Markdown JSON/YAML config.",
    ),
  },
  journey: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    diagramMarginX: parity(...interactiveLayout),
    diagramMarginY: parity(...interactiveLayout),
    leftMargin: parity(...interactiveLayout),
    maxLabelWidth: parity("text", ...interactiveLayout),
    width: parity(...interactiveLayout),
    height: parity(...interactiveLayout),
    boxMargin: inert("Sequence-era field; Journey 11.16.0 never consumes it."),
    boxTextMargin: parity("text", "paint", "export"),
    noteMargin: inert("Sequence-era field; Journey 11.16.0 never consumes it."),
    messageMargin: inert("Sequence-era field; Journey 11.16.0 never consumes it."),
    messageAlign: inert("Sequence-era field; Journey 11.16.0 never consumes it."),
    bottomMarginAdj: inert("Sequence-era field; Journey 11.16.0 never consumes it."),
    rightAngles: inert("Sequence-era field; Journey 11.16.0 never consumes it."),
    taskFontSize: parity("text", "paint", "export"),
    taskFontFamily: parity("text", "paint", "export"),
    taskMargin: parity(...interactiveLayout),
    activationWidth: inert("Sequence-era field; Journey 11.16.0 never consumes it."),
    textPlacement: parity("text", "paint", "export"),
    actorColours: inert(
      "Array-valued Journey config is removed by the Mermaid source-entry sanitizer.",
    ),
    sectionFills: inert(
      "Array-valued Journey config is removed by the Mermaid source-entry sanitizer.",
    ),
    sectionColours: inert(
      "Array-valued Journey config is removed by the Mermaid source-entry sanitizer.",
    ),
    titleColor: parity("paint", "export"),
    titleFontFamily: parity("text", "paint", "export"),
    titleFontSize: parity("text", "paint", "export"),
  },
  radar: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    width: parity("layout", "paint", "viewport", "export"),
    height: parity("layout", "paint", "viewport", "export"),
    marginTop: parity("layout", "paint", "viewport", "export"),
    marginRight: parity("layout", "paint", "viewport", "export"),
    marginBottom: parity("layout", "paint", "viewport", "export"),
    marginLeft: parity("layout", "paint", "viewport", "export"),
    axisScaleFactor: parity("layout", "paint", "export"),
    axisLabelFactor: parity("text", "layout", "paint", "export"),
    curveTension: {
      ...parity("layout", "paint", "export"),
      note: "Controls the closed Catmull-Rom cubic path for circle graticules; polygon curves intentionally ignore it.",
    },
  },
  class: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: inert(
      "The 11.16 unified class renderer reads state.useMaxWidth instead of class.useMaxWidth.",
    ),
    titleTopMargin: inert(
      "The Mermaid 11.16 unified class renderer reads state.titleTopMargin; the class field is retained but inert.",
    ),
    arrowMarkerAbsolute: inert(
      "The unified class renderer always emits fragment marker references; the family key is retained but inert.",
    ),
    dividerMargin: legacyOnly(
      layout,
      "Consumed by the legacy class renderer, not the unified native scene.",
    ),
    padding: parity("text", ...layout),
    textHeight: legacyOnly(
      textLayout,
      "Consumed by the legacy class renderer, not the unified native scene.",
    ),
    defaultRenderer: parity(...interactiveLayout),
    nodeSpacing: inert(
      "The Mermaid 11.16 class renderer currently resets this value to 50 before Dagre.",
    ),
    rankSpacing: inert(
      "The Mermaid 11.16 class renderer currently resets this value to 50 before Dagre.",
    ),
    diagramPadding: legacyOnly(
      ["viewport", "export"],
      "The unified renderer uses a fixed 8 px viewport padding.",
    ),
    htmlLabels: legacyOnly(
      textLayout,
      "The unified renderer consumes the global htmlLabels option instead.",
    ),
    hideEmptyMembersBox: parity("layout", "paint", "viewport", "export"),
    hierarchicalNamespaces: parity(...interactiveLayout),
  },
  state: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    titleTopMargin: parity("paint", "viewport", "export"),
    arrowMarkerAbsolute: inert(
      "The unified state renderer always emits fragment marker references; the family key is retained but inert.",
    ),
    dividerMargin: legacyOnly(layout, "Legacy state renderer geometry option."),
    sizeUnit: legacyOnly(layout, "Legacy state renderer geometry option."),
    padding: legacyOnly(textLayout, "Legacy state renderer text-box option."),
    textHeight: legacyOnly(textLayout, "Legacy state renderer text metric option."),
    titleShift: legacyOnly(layout, "Legacy state renderer geometry option."),
    noteMargin: legacyOnly(layout, "Legacy state renderer note geometry option."),
    nodeSpacing: parity(...interactiveLayout),
    rankSpacing: parity(...interactiveLayout),
    forkWidth: legacyOnly(layout, "Legacy state renderer fork geometry option."),
    forkHeight: legacyOnly(layout, "Legacy state renderer fork geometry option."),
    miniPadding: legacyOnly(layout, "Legacy state renderer geometry option."),
    fontSizeFactor: legacyOnly(textLayout, "Legacy state renderer text estimate option."),
    fontSize: legacyOnly(textLayout, "Legacy state renderer font option."),
    labelHeight: legacyOnly(textLayout, "Legacy state renderer label option."),
    edgeLengthFactor: legacyOnly(layout, "Legacy state renderer edge option."),
    compositTitleSize: legacyOnly(textLayout, "Legacy state renderer title option."),
    radius: legacyOnly(["layout", "paint", "export"], "Legacy state corner radius option."),
    defaultRenderer: parity(...interactiveLayout),
  },
  er: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    titleTopMargin: parity("paint", "viewport", "export"),
    // Entity-internal box padding (mermaid erBox PADDING); drives entity size.
    diagramPadding: parity(...layout),
    layoutDirection: inert(
      "Dead config in mermaid 11.16.0 — er.layoutDirection is never read; ER direction is always the parser `direction` keyword (default TB), like er.fill/er.fontSize.",
    ),
    // Empty-entity width floor (mermaid clamps only the attribute-less path).
    minEntityWidth: parity(...layout),
    minEntityHeight: inert(
      "Present in ErDiagramConfig but unused by mermaid erBox and Muffin.",
    ),
    entityPadding: parity(...layout),
    nodeSpacing: parity(...interactiveLayout),
    rankSpacing: parity(...interactiveLayout),
    stroke: inert(
      "Dead config in mermaid 11.16.0 — er.stroke is never consumed; the ER entity stroke resolves to theme nodeBorder (or classDef/inline), exactly like er.fontSize.",
    ),
    fill: inert(
      "Dead config in mermaid 11.16.0 — er.fill is never consumed; the ER entity fill resolves to theme mainBkg (or classDef/inline), exactly like er.fontSize.",
    ),
    fontSize: inert(
      "er.fontSize is dead in mermaid 11.16; the theme fontSize is used.",
    ),
  },
  pie: {
    useWidth: inert(
      "Dead config in mermaid 11.16.0 — pieDiagram never reads " +
        "BaseDiagramConfig.useWidth (grep on the pie chunk returns 0 hits); " +
        "only Gantt consumes it.",
    ),
    useMaxWidth: parity("viewport", "export"),
    // Slice-percentage label radial position (labelRadius = R*textPosition).
    // Moves the slice text transform only; arc paths and viewBox are unchanged.
    textPosition: {
      ...parity("text", "paint", "export"),
      note: "Slice-percentage label radial position (labelRadius = R*textPosition, default 0.75 -> r=138.75). 0 -> label at chart center (0,0); 1 -> outer edge (r=185); 0.5 -> r=92.5. Moves the text transform only; arc paths and viewBox are unchanged (font-independent geometry).",
    },
    // Donut inner radius = donutHole*R. Clamps to (0, 0.9].
    donutHole: {
      ...parity("layout", "paint", "export"),
      note: "Inner radius = donutHole*R (R=185). Clamps to (0, 0.9]: 0.5 -> innerR 92.5, 0.9 -> 166.5, but 0.95/1.0/-0.5 clamp to solid. Changes the arc path shape (appends a counter-clockwise inner arc LinnerEnd A innerR innerR 0 0 0 innerStart Z); viewBox unchanged.",
    },
    legendPosition: {
      ...parity("layout", "viewport", "export"),
      note: "top/bottom/left/center/right reshape the viewBox and the pie group translate; unrecognized values fall to default (right). top/bottom add legend height (n*22) -> viewBox height 516; left/right add legend width (22+longestTextWidth) -> viewBox width grows; center leaves both unchanged (490x450).",
    },
    highlightSlice: {
      ...parity("paint", "export"),
      note: "=label -> that slice gets CSS class 'highlighted' (scale 1.05 about the chart center, opacity 1; bbox grows ~5%); ='hover' -> 'highlightedOnHover' on all slices (CSS :hover only, no static paint change). Default '' highlights nothing — unless a section label is itself empty, which matches ''.",
    },
  },
  quadrantChart: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    chartWidth: parity("layout", "paint", "viewport", "export"),
    chartHeight: parity("layout", "paint", "viewport", "export"),
    titleFontSize: parity("text", "layout", "paint", "export"),
    titlePadding: parity("layout", "paint", "export"),
    quadrantPadding: parity("layout", "paint", "export"),
    xAxisLabelPadding: parity("layout", "paint", "export"),
    yAxisLabelPadding: parity("layout", "paint", "export"),
    xAxisLabelFontSize: parity("text", "layout", "paint", "export"),
    yAxisLabelFontSize: parity("text", "layout", "paint", "export"),
    quadrantLabelFontSize: parity("text", "paint", "export"),
    quadrantTextTopPadding: parity("layout", "paint", "export"),
    pointTextPadding: parity("layout", "paint", "export"),
    pointLabelFontSize: parity("text", "paint", "export"),
    pointRadius: parity("layout", "paint", "export"),
    xAxisPosition: parity("layout", "paint", "export"),
    yAxisPosition: parity("layout", "paint", "export"),
    quadrantInternalBorderStrokeWidth: parity("layout", "paint", "export"),
    quadrantExternalBorderStrokeWidth: parity("layout", "paint", "export"),
  },
  xyChart: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: inert(
      "xychartRenderer passes true to configureSvgSize unconditionally, so xyChart.useMaxWidth is retained but has no effect.",
    ),
    width: parity("layout", "paint", "viewport", "export"),
    height: parity("layout", "paint", "viewport", "export"),
    titleFontSize: parity("text", "layout", "paint", "export"),
    titlePadding: parity("layout", "paint", "export"),
    showDataLabel: parity("text", "paint", "export"),
    showDataLabelOutsideBar: parity("text", "paint", "export"),
    showTitle: parity("text", "layout", "paint", "export"),
    xAxis: parity("text", "layout", "paint", "export"),
    yAxis: parity("text", "layout", "paint", "export"),
    chartOrientation: parity("layout", "paint", "export"),
    plotReservedSpacePercent: parity("layout", "paint", "export"),
  },
  timeline: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    diagramMarginX: inert("Copied Journey/Sequence field; Timeline 11.16.0 never consumes it."),
    diagramMarginY: inert("Copied Journey/Sequence field; Timeline 11.16.0 never consumes it."),
    leftMargin: parity(...layout),
    width: inert("Copied Journey field; Timeline uses fixed LR/TD node widths."),
    height: inert("Copied Journey field; Timeline measures node heights from text."),
    padding: parity("viewport", "export"),
    boxMargin: inert("Copied Journey/Sequence field; Timeline 11.16.0 never consumes it."),
    boxTextMargin: inert("Copied Journey/Sequence field; Timeline uses fixed node padding."),
    noteMargin: inert("Copied Journey/Sequence field; Timeline 11.16.0 never consumes it."),
    messageMargin: inert("Copied Journey/Sequence field; Timeline 11.16.0 never consumes it."),
    messageAlign: inert("Copied Journey/Sequence field; Timeline 11.16.0 never consumes it."),
    bottomMarginAdj: inert("Copied Journey/Sequence field; Timeline 11.16.0 never consumes it."),
    rightAngles: inert("Copied Journey/Sequence field; Timeline 11.16.0 never consumes it."),
    taskFontSize: inert("Copied Journey field; Timeline labels use the root theme fontSize."),
    taskFontFamily: inert("Copied Journey field; Timeline labels use the root theme fontFamily."),
    taskMargin: inert("Copied Journey field; Timeline uses fixed LR/TD task spacing."),
    activationWidth: inert("Copied Sequence field; Timeline 11.16.0 never consumes it."),
    textPlacement: inert("Copied Journey field; Timeline uses its SVG text/wrap path directly."),
    actorColours: inert(
      "Array-valued copied Journey config is removed by the Mermaid source-entry sanitizer and is not consumed by Timeline.",
    ),
    sectionFills: inert(
      "Array-valued copied Journey config is removed by the Mermaid source-entry sanitizer and is not consumed by Timeline.",
    ),
    sectionColours: inert(
      "Array-valued copied Journey config is removed by the Mermaid source-entry sanitizer and is not consumed by Timeline.",
    ),
    disableMulticolor: parity("paint", "export"),
  },
  packet: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    rowHeight: parity(...layout),
    bitWidth: parity(...layout),
    bitsPerRow: parity("text", ...layout),
    showBits: parity("text", ...layout),
    paddingX: parity("layout", "paint", "export"),
    paddingY: parity(...layout),
  },
  kanban: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: inert(
      "Kanban 11.16.0 mistakenly reads mindmap.useMaxWidth when configuring its SVG; kanban.useMaxWidth is retained but inert.",
    ),
    padding: inert(
      "Kanban 11.16.0 mistakenly reads mindmap.padding for node/viewBox padding; kanban.padding is retained but inert.",
    ),
    sectionWidth: parity("layout", "paint", "viewport", "export"),
    ticketBaseUrl: parity("interaction", "export"),
  },
  mindmap: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: {
      ...parity("viewport", "export"),
      families: ["mindmap", "kanban"],
      note: "Mindmap consumes its own value; Mermaid 11.16.0 Kanban also reads mindmap.useMaxWidth instead of kanban.useMaxWidth.",
    },
    padding: {
      ...parity("layout", "paint", "viewport", "export"),
      families: ["mindmap", "kanban"],
      note: "Mindmap consumes its own value; Mermaid 11.16.0 Kanban also reads mindmap.padding instead of kanban.padding.",
    },
    maxNodeWidth: parity("text", "layout", "paint", "viewport", "export"),
    layoutAlgorithm: inert(
      "Declared by MindmapDiagramConfig but never read; Mindmap selects the renderer from the top-level layout key.",
    ),
  },
  railroad: {
    useWidth: allRailroad(inert("Only Gantt consumes BaseDiagramConfig.useWidth.")),
    useMaxWidth: allRailroad(parity("viewport", "export")),
    compactMode: allRailroad(inert("Removed by the Mermaid 11.16.0 source-entry config sanitizer.")),
    padding: allRailroad(parity("layout", "paint", "viewport", "export")),
    verticalSeparation: allRailroad(inert("Removed by the Mermaid 11.16.0 source-entry config sanitizer.")),
    horizontalSeparation: allRailroad(inert("Removed by the Mermaid 11.16.0 source-entry config sanitizer.")),
    arcRadius: allRailroad(inert("Removed by the Mermaid 11.16.0 source-entry config sanitizer.")),
    fontSize: allRailroad(parity(...textLayout)),
    fontFamily: allRailroad(parity(...textLayout)),
    terminalFill: allRailroad(parity("paint", "export")),
    terminalStroke: allRailroad(parity("paint", "export")),
    terminalTextColor: allRailroad(parity("text", "paint", "export")),
    nonTerminalFill: allRailroad(parity("paint", "export")),
    nonTerminalStroke: allRailroad(parity("paint", "export")),
    nonTerminalTextColor: allRailroad(parity("text", "paint", "export")),
    lineColor: allRailroad(parity("paint", "export")),
    strokeWidth: allRailroad(parity("paint", "export")),
    markerFill: allRailroad(parity("paint", "export")),
    commentFill: allRailroad(inert("Removed by the source-entry sanitizer; no shipped Railroad grammar emits a comment node.")),
    commentStroke: allRailroad(inert("Removed by the source-entry sanitizer; no shipped Railroad grammar emits a comment node.")),
    commentTextColor: allRailroad(inert("Removed by the source-entry sanitizer; no shipped Railroad grammar emits a comment node.")),
    specialFill: allRailroad(parity("paint", "export")),
    specialStroke: allRailroad(parity("paint", "export")),
    ruleNameColor: allRailroad(parity("text", "paint", "export")),
    showMarkers: allRailroad(inert("Removed by the Mermaid 11.16.0 source-entry config sanitizer.")),
    markerRadius: allRailroad(inert("Removed by the Mermaid 11.16.0 source-entry config sanitizer.")),
  },
  block: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    padding: parity("text", "layout", "paint", "viewport", "export"),
  },
  gitGraph: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    titleTopMargin: parity("paint", "viewport", "export"),
    diagramPadding: parity("viewport", "export"),
    nodeLabel: inert(
      "GitGraphDiagramConfig retains the legacy nodeLabel object, but the 11.16.0 renderer never reads it.",
    ),
    mainBranchName: parity("text", "layout", "paint", "viewport", "export"),
    mainBranchOrder: parity("layout", "paint", "viewport", "export"),
    showCommitLabel: parity("text", "paint", "viewport", "export"),
    showBranches: parity("text", "paint", "viewport", "export"),
    rotateCommitLabel: parity("layout", "paint", "viewport", "export"),
    parallelCommits: parity("layout", "paint", "viewport", "export"),
    arrowMarkerAbsolute: inert(
      "GitGraph 11.16.0 draws paths directly and does not serialize SVG arrow-marker URLs.",
    ),
  },
  c4: c4Policies,
  treeView: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    rowIndent: parity(...layout),
    paddingX: parity(...layout),
    paddingY: parity(...layout),
    lineThickness: parity(...layout),
    showIcons: {
      ...parity("layout", "paint", "viewport", "export"),
      note: "Icons reserve the upstream 18px slot and affect sizing even though Mermaid 11.16.0 strips the generated <use> elements from the final SVG.",
    },
    defaultIconPack: parity("layout", "paint", "viewport", "export"),
    filenameIcons: parity("layout", "paint", "viewport", "export"),
    extensionIcons: parity("layout", "paint", "viewport", "export"),
  },
  eventmodeling: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    padding: parity("layout", "paint", "viewport", "export"),
    rowHeight: inert(
      "Event Modeling 11.16.0 declares rowHeight but uses fixed swimlane and box geometry.",
    ),
  },
  ishikawa: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    diagramPadding: parity("viewport", "export"),
  },
  venn: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    width: parity("layout", "paint", "viewport", "export"),
    height: parity("layout", "paint", "viewport", "export"),
    padding: parity("layout", "paint", "export"),
    useDebugLayout: parity("paint", "export"),
  },
  sankey: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    width: parity("layout", "paint", "viewport", "export"),
    height: parity("layout", "paint", "viewport", "export"),
    linkColor: parity("paint", "export"),
    nodeAlignment: parity("layout", "paint", "viewport", "export"),
    showValues: parity("text", "layout", "paint", "viewport", "export"),
    prefix: parity("text", "paint", "viewport", "export"),
    suffix: parity("text", "paint", "viewport", "export"),
    nodeWidth: parity("layout", "paint", "viewport", "export"),
    nodePadding: parity("layout", "paint", "viewport", "export"),
    labelStyle: parity("text", "paint", "viewport", "export"),
    nodeColors: parity("paint", "export"),
  },
  treemap: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    padding: parity("layout", "paint", "viewport", "export"),
    diagramPadding: parity("viewport", "export"),
    showValues: parity("text", "layout", "paint", "viewport", "export"),
    nodeWidth: parity("layout", "paint", "viewport", "export"),
    nodeHeight: parity("layout", "paint", "viewport", "export"),
    borderWidth: inert(
      "Treemap 11.16.0 declares borderWidth but its styles use fixed section/leaf widths.",
    ),
    valueFontSize: inert(
      "Treemap 11.16.0 declares valueFontSize but derives value text size from each tile.",
    ),
    labelFontSize: inert(
      "Treemap 11.16.0 declares labelFontSize but derives label text size from each tile.",
    ),
    valueFormat: parity("text", "paint", "viewport", "export"),
  },
  cynefin: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    width: parity("layout", "paint", "viewport", "export"),
    height: parity("layout", "paint", "viewport", "export"),
    padding: parity("layout", "paint", "viewport", "export"),
    showDomainDescriptions: parity("text", "layout", "paint", "viewport", "export"),
    boundaryAmplitude: parity("layout", "paint", "viewport", "export"),
    seed: parity("layout", "paint", "export"),
  },
  architecture: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    padding: parity("layout", "viewport", "export"),
    iconSize: parity("layout", "paint", "viewport", "export"),
    fontSize: parity("text", "layout", "paint", "viewport", "export"),
    randomize: parity("layout", "viewport", "export"),
    nodeSeparation: parity("layout", "viewport", "export"),
    idealEdgeLengthMultiplier: parity("layout", "viewport", "export"),
    edgeElasticity: parity("layout", "viewport", "export"),
    numIter: parity("layout", "viewport", "export"),
    seed: parity("layout", "viewport", "export"),
  },
  "wardley-beta": {
    useWidth: inert(
      "Only Gantt consumes BaseDiagramConfig.useWidth; Wardley's source config object is removed by the 11.16 source-entry sanitizer.",
    ),
    useMaxWidth: inert(
      "Wardley's source config object is removed by the 11.16 source-entry sanitizer.",
    ),
    width: inert(
      "Wardley's source config object is removed; only the diagram-local size statement changes width.",
    ),
    height: inert(
      "Wardley's source config object is removed; only the diagram-local size statement changes height.",
    ),
    padding: inert(
      "Wardley's source config object is removed by the 11.16 source-entry sanitizer.",
    ),
    nodeRadius: inert(
      "Wardley's source config object is removed by the 11.16 source-entry sanitizer.",
    ),
    nodeLabelOffset: inert(
      "Wardley's source config object is removed by the 11.16 source-entry sanitizer.",
    ),
    axisFontSize: inert(
      "Wardley's source config object is removed by the 11.16 source-entry sanitizer.",
    ),
    labelFontSize: inert(
      "Wardley's source config object is removed by the 11.16 source-entry sanitizer.",
    ),
    showGrid: inert(
      "Wardley's source config object is removed by the 11.16 source-entry sanitizer.",
    ),
  },
  gantt: {
    useWidth: parity("layout", "paint", "viewport", "export"),
    useMaxWidth: parity("viewport", "export"),
    titleTopMargin: parity("layout", "paint", "viewport", "export"),
    barHeight: parity("layout", "paint", "viewport", "export"),
    barGap: parity("layout", "paint", "viewport", "export"),
    topPadding: parity("layout", "paint", "viewport", "export"),
    rightPadding: parity("layout", "paint", "viewport", "export"),
    leftPadding: parity("layout", "paint", "viewport", "export"),
    gridLineStartPadding: parity("layout", "paint", "viewport", "export"),
    fontSize: parity("text", "layout", "paint", "viewport", "export"),
    sectionFontSize: parity("text", "layout", "paint", "viewport", "export"),
    numberSectionStyles: parity("paint", "export"),
    axisFormat: parity("text", "paint", "export"),
    tickInterval: parity("text", "layout", "paint", "export"),
    topAxis: parity("layout", "paint", "viewport", "export"),
    displayMode: parity("layout", "paint", "viewport", "export"),
    weekday: parity("layout", "paint", "export"),
  },
  requirement: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: inert(
      "The 11.16 requirement renderer reads config.state.useMaxWidth (setupViewPortForSVG " +
      "conf=state), never config.requirement.useMaxWidth — setting requirement.useMaxWidth " +
      "has no observable effect. The viewport-sizing effect is real but sourced from state.",
    ),
    rect_fill: inert(
      "Dead config in mermaid 11.16.0 — requirement.rect_fill is never consumed; the " +
      "requirementBox fill resolves to theme mainBkg (or inline style/classDef).",
    ),
    text_color: inert(
      "Dead config in mermaid 11.16.0 — requirement.text_color is never consumed; the " +
      "requirementBox text color resolves to theme primaryTextColor.",
    ),
    rect_border_size: inert(
      "Dead config in mermaid 11.16.0 — requirement.rect_border_size is never consumed; " +
      "the box stroke-width resolves to theme strokeWidth.",
    ),
    rect_border_color: inert(
      "Dead config in mermaid 11.16.0 — requirement.rect_border_color is never consumed; " +
      "the box stroke resolves to theme border1 (or borderColorArray[colorIndex]).",
    ),
    rect_min_width: inert(
      "Dead config in mermaid 11.16.0 — requirement.rect_min_width is never consumed; " +
      "the box width is measured from text bbox + padding(20).",
    ),
    rect_min_height: inert(
      "Dead config in mermaid 11.16.0 — requirement.rect_min_height is never consumed; " +
      "the box height is measured from text bbox + padding(20).",
    ),
    fontSize: inert(
      "Dead config in mermaid 11.16.0 — requirement.fontSize is never consumed; the theme " +
      "fontSize is used (same as er.fontSize / class.fontSize).",
    ),
    rect_padding: inert(
      "Dead config in mermaid 11.16.0 — requirement.rect_padding is never consumed; the " +
      "box padding is hardcoded to 20 in requirementBox.ts.",
    ),
    line_height: inert(
      "Dead config in mermaid 11.16.0 — requirement.line_height is never consumed; the " +
      "row height is measured from text bbox (addText3 return value).",
    ),
  },
};

const shared = [
  {
    path: "theme",
    families: [
      "flowchart",
      "swimlane",
      "sequence",
      "class",
      "state",
      "er",
      "requirement",
      "pie",
      "quadrantChart",
      "journey",
      "radar",
      "xyChart",
      "timeline",
      "packet",
      "kanban",
      "mindmap",
      "block",
      "gitGraph",
      "c4",
      "gantt",
      "info",
      "treeView",
      "eventmodeling",
      "ishikawa",
      "venn",
      "sankey",
      "treemap",
      "cynefin",
      "wardley-beta",
      "architecture",
    ],
    ...parity("text", "layout", "paint", "viewport", "export"),
  },
  {
    path: "themeVariables.*",
    families: [
      "flowchart",
      "swimlane",
      "sequence",
      "class",
      "state",
      "er",
      "requirement",
      "pie",
      "quadrantChart",
      "journey",
      "radar",
      "xyChart",
      "timeline",
      "packet",
      "kanban",
      "mindmap",
      "block",
      "gitGraph",
      "c4",
      "gantt",
      "info",
      "treeView",
      "eventmodeling",
      "ishikawa",
      "venn",
      "sankey",
      "treemap",
      "cynefin",
      "wardley-beta",
      "architecture",
    ],
    ...partial(
      ["text", "layout", "paint", "viewport", "export"],
      ["text", "layout", "paint", "viewport", "export"],
      "Exhaustive per-key golden over the 285-key resolved inventory (theme-variables-inventory.json): 227 keys byte-locked across all 11 themes via FlowThemeVariables::get(); the remaining 58 are enumerated with per-key rationale in MermaidThemeTest's themeVariablesRemainingKeys (upstream-unconsumed palette slots, sequence-local keys resolved in the adapter, and family-local styles).",
    ),
  },
  {
    path: "fontFamily",
    families: [
      "flowchart",
      "swimlane",
      "sequence",
      "class",
      "state",
      "er",
      "requirement",
      "pie",
      "quadrantChart",
      "journey",
      "radar",
      "xyChart",
      "timeline",
      "packet",
      "kanban",
      "mindmap",
      "block",
      "gitGraph",
      "c4",
      "gantt",
      "info",
      "treeView",
      "eventmodeling",
      "ishikawa",
      "venn",
      "sankey",
      "treemap",
      "cynefin",
      "architecture",
    ],
    ...parity("text", "layout", "paint", "viewport", "export"),
  },
  {
    path: "htmlLabels",
    families: ["flowchart", "swimlane", "class", "requirement", "kanban", "mindmap", "block"],
    ...parity(...textLayout),
  },
  {
    path: "look",
    families: ["flowchart", "swimlane", "class", "state", "timeline", "kanban", "mindmap", "block", "ishikawa", "venn"],
    ...parity(...interactiveLayout),
    note: "Classic/Neo/handDrawn routing, exact-case fallback, RoughJS geometry, interaction, and export are covered by family and cross-family upstream oracles.",
  },
  {
    path: "handDrawnSeed",
    families: ["flowchart", "swimlane", "class", "state", "kanban", "mindmap", "block", "ishikawa", "venn"],
    ...parity("layout", "paint", "interaction", "export"),
  },
  {
    path: "markdownAutoWrap",
    families: ["kanban", "mindmap"],
    ...parity("text", "layout", "paint", "viewport", "export"),
    note: "False disables Markdown label wrapping; other source values follow JavaScript truthiness.",
  },
  {
    path: "layout",
    families: ["flowchart", "swimlane", "class", "state", "mindmap"],
    ...parity(...interactiveLayout),
    note: "Dagre, Mindmap CoSE, exact-case selection, registered-name fallback, and State's runtime-error boundary are covered by production geometry oracles.",
  },
  ...[
    "mergeEdges",
    "nodePlacementStrategy",
    "cycleBreakingStrategy",
    "forceNodeModelOrder",
    "considerModelOrder",
  ].map((field) => ({
    path: `elk.${field}`,
    families: ["flowchart"],
    ...inert(
      "Mermaid 11.16 retains this external-ELK option, but the pinned runtime registers no ELK loader and renders through Dagre.",
    ),
  })),
  {
    path: "wrap",
    families: ["sequence", "c4"],
    ...parity(...textLayout),
    note: "Includes the %%{wrap}%% directive promoted by Mermaid preprocessing.",
  },
  {
    path: "maxEdges",
    families: ["flowchart"],
    ...parity("parsed"),
    note: "Mermaid's secure source config strips this key, so documents cannot lower or raise the default 500-edge boundary; Muffin retains the same source behavior.",
  },
  {
    path: "maxTextSize",
    families: [
      "flowchart", "sequence", "class", "state", "er", "requirement",
      "swimlane",
      "pie", "quadrantChart", "journey", "radar", "xyChart",
      "timeline", "packet",
      "kanban",
      "mindmap",
      "block",
      "gitGraph",
      "c4",
      "gantt",
      "info",
      "treeView",
      "eventmodeling",
      "ishikawa",
      "venn",
      "sankey",
      "treemap",
      "cynefin",
      "wardley-beta",
      "architecture",
    ],
    ...parity("parsed"),
    note: "Mermaid's secure source config strips this key; source documents cannot change the global default text boundary in either renderer.",
  },
  {
    path: "securityLevel",
    families: [
      "flowchart",
      "swimlane",
      "sequence",
      "class",
      "state",
      "er",
      "requirement",
      "pie",
      "quadrantChart",
      "journey",
      "radar",
      "xyChart",
      "timeline",
      "packet",
      "kanban",
      "mindmap",
      "block",
      "gitGraph",
      "c4",
      "gantt",
      "info",
      "treeView",
      "eventmodeling",
      "ishikawa",
      "venn",
      "sankey",
      "treemap",
      "cynefin",
      "wardley-beta",
      "architecture",
    ],
    ...policy(
      "security-fixed",
      ["interaction", "export"],
      [],
      "The desktop renderer always applies its strict URL and content policy.",
    ),
  },
  {
    path: "arrowMarkerAbsolute",
    families: ["flowchart", "swimlane", "sequence"],
    ...parity("export"),
    note:
      "Flowchart, Swimlane, and Sequence serialize absolute marker references when the SVG export context supplies its document URL; other 11.16.0 families retain fragment references.",
  },
  {
    path: "deterministicIds",
    families: [
      "flowchart",
      "swimlane",
      "sequence",
      "class",
      "state",
      "er",
      "requirement",
      "pie",
      "quadrantChart",
      "journey",
      "radar",
      "xyChart",
      "timeline",
      "packet",
      "kanban",
      "mindmap",
      "block",
      "gitGraph",
      "c4",
      "gantt",
      "info",
      "treeView",
      "eventmodeling",
      "ishikawa",
      "venn",
      "sankey",
      "treemap",
      "cynefin",
      "wardley-beta",
      "architecture",
    ],
    ...parity("export"),
  },
  {
    path: "deterministicIDSeed",
    families: [
      "flowchart",
      "swimlane",
      "sequence",
      "class",
      "state",
      "er",
      "requirement",
      "pie",
      "quadrantChart",
      "journey",
      "radar",
      "xyChart",
      "timeline",
      "packet",
      "kanban",
      "mindmap",
      "block",
      "gitGraph",
      "c4",
      "gantt",
      "info",
      "treeView",
      "eventmodeling",
      "ishikawa",
      "venn",
      "sankey",
      "treemap",
      "cynefin",
      "wardley-beta",
      "architecture",
    ],
    ...parity("export"),
  },
  {
    path: "themeCSS",
    families: [
      "flowchart",
      "swimlane",
      "sequence",
      "class",
      "state",
      "er",
      "requirement",
      "pie",
      "quadrantChart",
      "journey",
      "radar",
      "xyChart",
      "timeline",
      "packet",
      "kanban",
      "mindmap",
      "block",
      "gitGraph",
      "c4",
      "gantt",
      "info",
      "treeView",
      "eventmodeling",
      "ishikawa",
      "venn",
      "sankey",
      "treemap",
      "cynefin",
      "wardley-beta",
      "architecture",
    ],
    ...parity("text", "paint", "export"),
    note:
      "Arbitrary themeCSS is resolved through the per-family CSS cascade against real upstream DOM oracles (mermaid-theme-css.json, 117 cases). Wardley's draw() clears the svg before painting, so themeCSS is upstream-inert there and native parity holds by construction. Geometry feedback beyond paint is demonstrated per family in the themeCSS fixtures: flowchart, swimlane, sequence, class, state, pie, mindmap, sankey, treeview, block, ishikawa, requirement, timeline, kanban, gitGraph, treemap, architecture, and railroad.",
  },
];

// Railroad has four independent parser/detector frontends over one renderer
// and one `railroad` config object. Shared top-level configuration therefore
// reaches all four dialects, while the interface rows below remain unique.
const railroadSharedPaths = new Set([
  "theme",
  "themeVariables.*",
  "fontFamily",
  "maxTextSize",
  "securityLevel",
  "deterministicIds",
  "deterministicIDSeed",
  "themeCSS",
]);
for (const entry of shared) {
  if (railroadSharedPaths.has(entry.path))
    entry.families.push(...railroadFamilies);
}

function interfaceProperties(name) {
  const marker = `export interface ${name}`;
  const start = configTypes.indexOf(marker);
  if (start < 0) throw new Error(`Missing ${name} in config.type.d.ts`);
  const open = configTypes.indexOf("{", start);
  let depth = 1;
  let end = open + 1;
  for (; end < configTypes.length && depth > 0; ++end) {
    if (configTypes[end] === "{") ++depth;
    else if (configTypes[end] === "}") --depth;
  }
  const body = configTypes
    .slice(open + 1, end - 1)
    .replace(/\/\*[\s\S]*?\*\//g, "");
  const result = [];
  depth = 0;
  for (const line of body.split(/\r?\n/)) {
    if (depth === 0) {
      const match = /^\s*([A-Za-z_$][\w$]*)\??\s*:/.exec(line);
      if (match) result.push(match[1]);
    }
    for (const character of line) {
      if (character === "{") ++depth;
      else if (character === "}") --depth;
    }
  }
  return result;
}

const baseFields = interfaceProperties("BaseDiagramConfig");
const interfaces = {
  flowchart: "FlowchartDiagramConfig",
  swimlane: "SwimlaneDiagramConfig",
  sequence: "SequenceDiagramConfig",
  class: "ClassDiagramConfig",
  state: "StateDiagramConfig",
  er: "ErDiagramConfig",
  requirement: "RequirementDiagramConfig",
  pie: "PieDiagramConfig",
  quadrantChart: "QuadrantChartConfig",
  journey: "JourneyDiagramConfig",
  radar: "RadarDiagramConfig",
  xyChart: "XYChartConfig",
  timeline: "TimelineDiagramConfig",
  packet: "PacketDiagramConfig",
  kanban: "KanbanDiagramConfig",
  mindmap: "MindmapDiagramConfig",
  railroad: "RailroadDiagramConfig",
  block: "BlockDiagramConfig",
  gitGraph: "GitGraphDiagramConfig",
  c4: "C4DiagramConfig",
  treeView: "TreeViewDiagramConfig",
  eventmodeling: "EventModelingDiagramConfig",
  ishikawa: "IshikawaDiagramConfig",
  venn: "VennDiagramConfig",
  sankey: "SankeyDiagramConfig",
  treemap: "TreemapDiagramConfig",
  cynefin: "CynefinDiagramConfig",
  architecture: "ArchitectureDiagramConfig",
  "wardley-beta": "WardleyDiagramConfig",
  gantt: "GanttDiagramConfig",
};

const entries = [];
for (const [family, interfaceName] of Object.entries(interfaces)) {
  const fields = [...new Set([...baseFields, ...interfaceProperties(interfaceName)])];
  const policies = familyPolicies[family];
  const missing = fields.filter((field) => !policies[field]);
  const extra = Object.keys(policies).filter((field) => !fields.includes(field));
  if (missing.length || extra.length) {
    throw new Error(
      `${family} config classification mismatch; missing=[${missing}], extra=[${extra}]`,
    );
  }
  for (const field of fields) {
    const section = defaultConfig[family] ?? {};
    const hasDefault = Object.prototype.hasOwnProperty.call(section, field);
    const item = {
      family,
      interface: interfaceName,
      path: `${family}.${field}`,
      field,
      hasDefault,
      ...policies[field],
    };
    if (hasDefault && section[field] !== undefined) item.default = section[field];
    entries.push(item);
  }
}

const withParsed = (entry) => ({
  ...entry,
  upstream: ["parsed", ...entry.upstream.filter((value) => value !== "parsed")],
  native: ["parsed", ...entry.native.filter((value) => value !== "parsed")],
});
const normalizedEntries = [...shared, ...entries].map(withParsed);
const duplicatePaths = normalizedEntries
  .map((entry) => entry.path)
  .filter((entryPath, index, paths) => paths.indexOf(entryPath) !== index);
if (duplicatePaths.length) {
  throw new Error(
    `duplicate config paths: ${[...new Set(duplicatePaths)].join(", ")}`,
  );
}
for (const entry of normalizedEntries) {
  for (const side of ["upstream", "native"]) {
    const unknown = entry[side].filter((effect) => !dimensions.includes(effect));
    if (unknown.length) {
      throw new Error(`${entry.path} has unknown ${side} effects: ${unknown}`);
    }
  }
}

const summary = {};
for (const entry of normalizedEntries) {
  summary[entry.status] = (summary[entry.status] ?? 0) + 1;
}
const payload = {
  upstream: {
    package: packageJson.name,
    version: packageJson.version,
    configTypeSha256: createHash("sha256").update(configTypes).digest("hex"),
  },
  dimensions,
  scope: {
    families: [
      ...Object.keys(interfaces),
      "railroadEbnf",
      "railroadAbnf",
      "railroadPeg",
      "info",
    ],
    note: "Effects are direct observable stages; export includes PNG and native SVG. Absolute marker URL parity is evaluated with an explicit document URL export context.",
  },
  summary,
  entries: normalizedEntries,
};
const canonical = JSON.stringify(payload);
payload.fixtureSha256 = createHash("sha256").update(canonical).digest("hex");
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, `${JSON.stringify(payload, null, 2)}\n`);
console.log(`Wrote ${normalizedEntries.length} config effects to ${output}`);
console.log(`fixtureSha256=${payload.fixtureSha256}`);
