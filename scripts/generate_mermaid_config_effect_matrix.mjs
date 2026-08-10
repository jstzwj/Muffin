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
const configTypes = fs.readFileSync(configTypesPath, "utf8");
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

// This table is the reviewed semantic policy. Interface membership and defaults
// are generated from Mermaid itself below, so adding or removing an upstream
// field makes generation fail until its effect is classified here.
const familyPolicies = {
  flowchart: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    titleTopMargin: parity("paint", "viewport", "export"),
    subGraphTitleMargin: unsupported(
      ["layout", "viewport", "export"],
      "Cluster title margins are not forwarded to native compound layout.",
    ),
    arrowMarkerAbsolute: deferred(
      ["export"],
      "Only meaningful for SVG marker URL serialization.",
    ),
    diagramPadding: parity("viewport", "export"),
    htmlLabels: unsupported(
      textLayout,
      "Deprecated upstream alias; native flow labels currently use one structured text path.",
    ),
    nodeSpacing: parity(...interactiveLayout),
    rankSpacing: parity(...interactiveLayout),
    curve: parity(...interactiveLayout),
    padding: parity("text", ...interactiveLayout),
    defaultRenderer: partial(
      interactiveLayout,
      interactiveLayout,
      "dagre-wrapper is supported; dagre-d3 and elk return an explicit unsupported diagnostic.",
    ),
    wrappingWidth: unsupported(
      textLayout,
      "Native flow markdown wrapping does not yet consume this width.",
    ),
    inheritDir: unsupported(
      interactiveLayout,
      "Subgraph direction inheritance is parsed but not forwarded.",
    ),
  },
  sequence: {
    useWidth: inert("Only Gantt consumes BaseDiagramConfig.useWidth."),
    useMaxWidth: parity("viewport", "export"),
    arrowMarkerAbsolute: deferred(
      ["export"],
      "Only meaningful for SVG marker URL serialization.",
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
    titleTopMargin: partial(
      ["paint", "viewport", "export"],
      ["paint", "viewport", "export"],
      "Effective in the legacy renderer; the 11.16 unified renderer reads state.titleTopMargin.",
    ),
    arrowMarkerAbsolute: deferred(
      ["export"],
      "Only meaningful for SVG marker URL serialization.",
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
    defaultRenderer: partial(
      interactiveLayout,
      interactiveLayout,
      "dagre-wrapper/native routing is supported; elk returns an explicit unsupported diagnostic.",
    ),
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
    arrowMarkerAbsolute: deferred(
      ["export"],
      "Only meaningful for SVG marker URL serialization.",
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
    defaultRenderer: partial(
      interactiveLayout,
      interactiveLayout,
      "dagre-wrapper/native routing is supported; elk returns an explicit unsupported diagnostic.",
    ),
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
    ],
    ...parity("text", "layout", "paint", "viewport", "export"),
  },
  {
    path: "themeVariables.*",
    families: [
      "flowchart",
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
    ],
    ...partial(
      ["text", "layout", "paint", "viewport", "export"],
      ["text", "layout", "paint", "viewport", "export"],
      "The matrix covers the native theme-variable subset, not arbitrary Mermaid CSS variables.",
    ),
  },
  {
    path: "fontFamily",
    families: [
      "flowchart",
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
    ],
    ...parity("text", "layout", "paint", "viewport", "export"),
  },
  {
    path: "htmlLabels",
    families: ["flowchart", "class", "requirement"],
    ...partial(
      textLayout,
      textLayout,
      "Native class labels consume this option; native flow and requirement labels do not yet branch on it (Requirement currently follows the htmlLabels:true path).",
    ),
  },
  {
    path: "look",
    families: ["flowchart", "class", "state", "timeline"],
    ...partial(
      interactiveLayout,
      interactiveLayout,
      "Flowchart and Timeline are complete; state currently uses look for marker selection and class retains it without rough painting.",
    ),
  },
  {
    path: "handDrawnSeed",
    families: ["flowchart"],
    ...parity("layout", "paint", "interaction", "export"),
  },
  {
    path: "layout",
    families: ["flowchart", "class", "state"],
    ...partial(
      interactiveLayout,
      interactiveLayout,
      "dagre is supported; elk and unknown engines return unsupported-layout-engine.",
    ),
  },
  {
    path: "wrap",
    families: ["sequence"],
    ...parity(...textLayout),
    note: "Includes the %%{wrap}%% directive promoted by Mermaid preprocessing.",
  },
  {
    path: "maxEdges",
    families: ["flowchart"],
    ...unsupported(
      ["parsed"],
      "Muffin keeps a fixed safety ceiling instead of allowing source config to raise it.",
    ),
  },
  {
    path: "maxTextSize",
    families: [
      "flowchart", "sequence", "class", "state", "er", "requirement",
      "pie", "quadrantChart", "journey", "radar", "xyChart",
      "timeline", "packet",
    ],
    ...unsupported(
      ["parsed"],
      "Muffin keeps family-specific safety ceilings instead of trusting document config.",
    ),
  },
  {
    path: "securityLevel",
    families: [
      "flowchart",
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
    families: ["flowchart", "sequence", "class", "state", "er", "requirement"],
    ...deferred(["export"], "Requires native SVG marker serialization."),
  },
  {
    path: "deterministicIds",
    families: [
      "flowchart",
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
    ],
    ...parity("export"),
  },
  {
    path: "deterministicIDSeed",
    families: [
      "flowchart",
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
    ],
    ...parity("export"),
  },
  {
    path: "themeCSS",
    families: [
      "flowchart",
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
    ],
    ...unsupported(
      ["paint", "export"],
      "Native scenes consume typed theme variables rather than arbitrary browser CSS.",
    ),
  },
];

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
};

const entries = [];
for (const [family, interfaceName] of Object.entries(interfaces)) {
  const fields = [...baseFields, ...interfaceProperties(interfaceName)];
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
    families: Object.keys(interfaces),
    note: "Effects are direct observable stages; export includes PNG and native SVG. Absolute marker URL controls remain deferred.",
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
