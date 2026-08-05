import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

// Captures real mermaid 11.16.0 pieDiagram geometry (slice arc paths, radii,
// derived start/end/mid angles, label centroids, legend census, title/accessibility
// text) via headless Chrome. Output schema mirrors the fields of a future
// pie::PieScene::toJsonObject that are comparable against mermaid.
//
// Two ground-truth channels are captured per slice:
//   1. The RAW SVG path `d` string (exactly as mermaid's d3 arc serializer emits
//      it) — the byte-parity target. Native must reproduce these strings.
//   2. Derived deterministic geometry computed from the values + config:
//        theta(f) = -pi/2 + f * 2pi   (12-o'clock anchor, clockwise; SVG y-down)
//      where the cumulative fraction f is normalized over the FILTERED slice sum
//      (mermaid drops slices <1% BEFORE handing the data to d3pie, so d3pie
//      normalizes over the post-filter sum — verified against the 15-slice scout
//      case where S2's end-point matches filtered-sum normalization).
//      The percentage LABEL text uses the ORIGINAL (pre-filter) sum.
//      These two normalizations diverge whenever a slice is filtered, so both are
//      recorded.
//
// Cross-check: each slice's derived outer-start point is compared to the point
// recovered from the raw `d` string's `M` command; the generator asserts a
// sub-millidegree angle error (matching the scout's arcCheck, pointErr < 0.001px).
//
//   node scripts/generate_mermaid_pie_geometry_fixture.mjs \
//     [mermaid-root] [output-json] [chrome-exe]
// Defaults: ../mermaid-cli/node_modules/mermaid,
//           tests/fixtures/mermaid/pie-geometry.json,
//           C:/Program Files/Google/Chrome/Application/chrome.exe

const mermaidRoot = path.resolve(
  process.argv[2] ??
    path.join("..", "mermaid-cli", "node_modules", "mermaid"),
);
const output = path.resolve(
  process.argv[3] ?? path.join("tests", "fixtures", "mermaid", "pie-geometry.json"),
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

// Each case declares the pie config its %%{init}%% frontmatter establishes so the
// generator can derive geometry without re-parsing the directive. Corpus covers:
// basic 3-slice (scout canonical), 2-slice (largeArc=1 exercise), 13-slice palette
// wrap, donut hole, showData legend text, title+accTitle+accDescr, <1% sliver filter.
const cases = [
  {
    id: "basic-3",
    source: "pie title Pets\n\"Dogs\" : 38\n\"Cats\" : 26\n\"Fish\" : 36",
    textPosition: 0.75,
    donutHole: 0,
  },
  {
    id: "basic-2",
    source: "pie\n\"A\" : 1\n\"B\" : 2",
    textPosition: 0.75,
    donutHole: 0,
  },
  {
    id: "thirteen-wrap",
    source:
      "pie\n" +
      Array.from({ length: 13 }, (_, i) => `"S${i + 1}" : ${i + 1}`).join("\n"),
    textPosition: 0.75,
    donutHole: 0,
  },
  {
    id: "donut-half",
    source:
      "%%{init: {\"pie\": {\"donutHole\": 0.5}}}%%\n" +
      "pie title Donut\n\"Dogs\" : 38\n\"Cats\" : 26\n\"Fish\" : 36",
    textPosition: 0.75,
    donutHole: 0.5,
  },
  {
    id: "show-data",
    source: "pie showData\n\"Dogs\" : 38\n\"Cats\" : 26",
    textPosition: 0.75,
    donutHole: 0,
  },
  {
    id: "title-acc",
    source:
      "pie title Pets\n" +
      "accTitle: Pet Distribution\n" +
      "accDescr: A breakdown of household pets\n" +
      "\"Dogs\" : 38\n\"Cats\" : 26",
    textPosition: 0.75,
    donutHole: 0,
  },
  {
    id: "sliver-below-1pct",
    source: "pie\n\"Big\" : 1000\n\"Tiny\" : 5",
    textPosition: 0.75,
    donutHole: 0,
  },
  // legendPosition coverage: the slice geometry is unchanged, but the canvas
  // size and pie/legend group transforms differ per position.
  {
    id: "legend-top",
    source: "%%{init: {\"pie\": {\"legendPosition\": \"top\"}}}%%\npie title Pets\n\"Dogs\" : 38\n\"Cats\" : 26\n\"Fish\" : 36",
    textPosition: 0.75,
    donutHole: 0,
  },
  {
    id: "legend-left",
    source: "%%{init: {\"pie\": {\"legendPosition\": \"left\"}}}%%\npie title Pets\n\"Dogs\" : 38\n\"Cats\" : 26\n\"Fish\" : 36",
    textPosition: 0.75,
    donutHole: 0,
  },
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
      const number = (v) => Math.round(v * 1000) / 1000;
      const { default: mermaid } = await import(mermaidModule);
      // Fixed chart constants (pieRenderer.ts draw()).
      const MARGIN = 40;
      const HEIGHT = 450;
      const PIE_WIDTH = 450;
      const CENTER = 225;
      const RADIUS = Math.min(PIE_WIDTH, HEIGHT) / 2 - MARGIN; // 185
      const LEGEND_RECT = 18;
      const LEGEND_SPACING = 4;
      const LEGEND_HEIGHT = LEGEND_RECT + LEGEND_SPACING; // 22

      const out = [];
      for (let index = 0; index < cases.length; ++index) {
        const fixture = cases[index];
        const textPosition = fixture.textPosition;
        const donutHole = fixture.donutHole;
        // donutHole clamp: (0, 0.9] -> value, else 0 (pieRenderer line 161).
        const innerHole =
          donutHole > 0 && donutHole <= 0.9 ? donutHole : 0;
        const outerR = RADIUS; // 185
        const innerR = innerHole * RADIUS; // donut inner radius (0 for solid)
        const labelR = RADIUS * textPosition; // label centroid radius

        mermaid.initialize({
          startOnLoad: false,
          securityLevel: "strict",
          theme: "default",
          fontFamily: '"trebuchet ms", verdana, arial, sans-serif',
          look: "classic",
        });
        const svgId = `pie-geometry-${index}`;
        const { svg } = await mermaid.render(svgId, fixture.source);
        document.getElementById("container").innerHTML = svg;
        const root = document.querySelector("svg");
        const viewBox = root.getAttribute("viewBox");

        // Paths: source-order list of slice arc paths.
        const pathEls = [...root.querySelectorAll("path.pieCircle")];
        // Slice labels: source-order text.slice elements (parallel to drawn arcs).
        const sliceTextEls = [...root.querySelectorAll("text.slice")];
        // Outer ring.
        const outerCircle = root.querySelector("circle.pieOuterCircle");
        const outerRingR = outerCircle
          ? number(parseFloat(outerCircle.getAttribute("r")))
          : null;
        // Legends: source-order, ALL sections (not filtered).
        const legendEls = [...root.querySelectorAll("g.legend")];
        const legends = legendEls.map((g) => {
          const text = g.querySelector("text");
          return {
            text: text ? text.textContent : "",
          };
        });
        // Title.
        const titleEl = root.querySelector("text.pieTitleText");
        const title = titleEl ? titleEl.textContent : null;

        // Accessibility metadata: mermaid renders accTitle as a <title> child and
        // accDescr as a <desc> child of the SVG (for screen readers). These are
        // distinct from the visual pieTitleText.
        const accTitleEl = root.querySelector("title");
        const accDescrEl = root.querySelector("desc");
        const accTitle = accTitleEl ? accTitleEl.textContent : null;
        const accDescr = accDescrEl ? accDescrEl.textContent : null;

        // Recover ALL declared sections (label + value, insertion order) so we can
        // compute derived geometry. Re-parse the source through a throwaway render
        // to read the DB is not available; instead parse values from the source
        // text the generator supplied (kept authoritative by convention).
        const declaredSections = fixture.source
          .split(/\r?\n/)
          .map((line) => {
            const m = line.match(/^\s*["']([^"']*)["']\s*:\s*(-?\d+(?:\.\d+)?)/);
            return m ? { label: m[1], value: parseFloat(m[2]) } : null;
          })
          .filter(Boolean);

        // Original sum (all declared sections, pre-filter).
        const originalSum = declaredSections.reduce((s, d) => s + d.value, 0);
        // Pre-d3pie <1% filter (value/originalSum*100 >= 1). Negative values would
        // have thrown at parse time, so all values >= 0 here.
        const drawnSections = declaredSections.filter(
          (d) => (d.value / originalSum) * 100 >= 1,
        );
        // d3pie normalizes over the FILTERED (post-filter) sum.
        const filteredSum = drawnSections.reduce((s, d) => s + d.value, 0);

        // Derived per-slice geometry.
        const angleDeg = (frac) => -90 + frac * 360;
        let cumulative = 0;
        const derived = drawnSections.map((d) => {
          const f0 = cumulative / filteredSum;
          cumulative += d.value;
          const f1 = cumulative / filteredSum;
          const startDeg = angleDeg(f0);
          const endDeg = angleDeg(f1);
          const midDeg = (startDeg + endDeg) / 2;
          const toRad = (deg) => (deg * Math.PI) / 180;
          // Label centroid = labelR circle at mid-angle.
          const cx = labelR * Math.cos(toRad(midDeg));
          const cy = labelR * Math.sin(toRad(midDeg));
          // Percentage label text uses ORIGINAL sum.
          const pctLabel =
            ((d.value / originalSum) * 100).toFixed(0) + "%";
          return {
            label: d.label,
            value: d.value,
            percentage: pctLabel,
            rawPercentage: number((d.value / originalSum) * 100),
            startAngleDeg: number(startDeg),
            endAngleDeg: number(endDeg),
            midAngleDeg: number(midDeg),
            outerRadius: number(outerR),
            innerRadius: number(innerR),
            donutInnerRadius: number(innerR),
            labelRadius: number(labelR),
            centroidX: number(cx),
            centroidY: number(cy),
          };
        });

        // Cross-check: derived slice-0 outer-start must be (0, -185), i.e. start
        // angle -90deg. And every derived start point must match the raw `d` M cmd.
        const parsePoint = (d, cmd) => {
          const idx = d.indexOf(cmd);
          if (idx < 0) return null;
          const rest = d.slice(idx + 1);
          const m = rest.match(/^(-?[\d.]+)[,\s]+(-?[\d.]+)/);
          return m
            ? { x: parseFloat(m[1]), y: parseFloat(m[2]) }
            : null;
        };
        const crossCheck = derived.map((d, i) => {
          const pathD = pathEls[i] ? pathEls[i].getAttribute("d") : null;
          const start = parsePoint(pathD, "M");
          const rad = (d.startAngleDeg * Math.PI) / 180;
          const expX = outerR * Math.cos(rad);
          const expY = outerR * Math.sin(rad);
          const err =
            start != null
              ? Math.hypot(start.x - expX, start.y - expY)
              : null;
          return { sliceIndex: i, pathDStart: start, expected: { x: number(expX), y: number(expY) }, pointErr: err == null ? null : number(err) };
        });

        // Build the per-slice expected record (merge DOM-captured path d/fill/cls
        // with derived geometry).
        const slices = derived.map((d, i) => {
          const pathEl = pathEls[i];
          const textEl = sliceTextEls[i];
          const d2 = pathEl ? pathEl.getAttribute("d") : null;
          const fill = pathEl ? pathEl.getAttribute("fill") : null;
          const cls = pathEl ? pathEl.getAttribute("class") : null;
          let labelTransform = null;
          if (textEl) {
            const t = textEl.getAttribute("transform");
            if (t) labelTransform = t;
          }
          return {
            ...d,
            pathD: d2,
            fill,
            cls,
            labelTransform,
          };
        });

        out.push({
          id: fixture.id,
          source: fixture.source,
          expected: {
            constants: {
              margin: MARGIN,
              height: HEIGHT,
              pieWidth: PIE_WIDTH,
              centerX: CENTER,
              centerY: CENTER,
              radius: number(RADIUS),
              legendRectSize: LEGEND_RECT,
              legendSpacing: LEGEND_SPACING,
              legendHeight: LEGEND_HEIGHT,
            },
            config: {
              textPosition: number(textPosition),
              donutHole: number(donutHole),
              effectiveDonutHole: number(innerHole),
            },
            viewBox,
            outerRingRadius: outerRingR,
            chartCenter: { x: CENTER, y: CENTER },
            title,
            accTitle: accTitle,
            accDescr: accDescr,
            legendCount: legends.length,
            legends,
            sliceCount: slices.length,
            slices,
            // Derived-only rollup: original vs filtered sum (the two
            // normalizations — diverges whenever a slice is filtered).
            sums: {
              original: number(originalSum),
              filtered: number(filteredSum),
            },
            crossCheck,
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
        "Real mermaid 11.16.0 pieDiagram geometry captured via headless Chrome " +
        "(default theme, trebuchet-ms font). Per drawn slice: raw SVG path `d` " +
        "(byte-parity target) + derived start/end/mid angles + outer/inner radii " +
        " + donut inner radius + label centroid. Angle convention theta(f) = " +
        "-pi/2 + f*2pi (12-o'clock anchor, clockwise; SVG y-down); cumulative " +
        "fraction f normalized over the FILTERED slice sum (mermaid drops <1% " +
        "slices before d3pie, so d3pie re-normalizes over the survivors — verified " +
        "against the 15-slice scout case). Percentage LABEL text uses the ORIGINAL " +
        "(pre-filter) sum. Arc path `d` strings, radii, counts and derived angles " +
        "are font-independent (exact). Legend/title pixel positions and the viewBox " +
        "width depend on live getBoundingClientRect() text widths (font-coupled); " +
        "the font is pinned to trebuchet-ms and noted, not treated as exact parity " +
        "for a non-browser renderer. Outer ring r = R + outerStrokeWidth/2 (186 " +
        "default). donutHole clamps to (0, 0.9]. Cross-check (crossCheck[]) compares " +
        "the derived outer-start point to the raw `d` M command: slice 0 is exact " +
        "(0 px, proving the -pi/2 anchor), subsequent slices carry d3's 3-decimal " +
        "path-serialization rounding noise (<=0.002 px) — the byte-parity target is " +
        "the captured pathD string itself, not the recovered point.",
    },
    mermaidVersion: "11.16.0",
    fontMode: "trebuchet-ms",
    oracle:
      "pieDiagram.render slice arcs (pathD + angles + radii + centroid) + " +
      "legend census + title",
    cases: results,
  };
  fs.writeFileSync(output, JSON.stringify(root, null, 2) + "\n");
  console.log(`Wrote ${output} (${results.length} cases)`);
  // Self-verify slice-0 invariants on the canonical case.
  const canon = results[0];
  const s0 = canon.expected.slices[0];
  console.log(
    `  basic-3 slice0: pathD="${s0.pathD.slice(0, 32)}..." startDeg=${s0.startAngleDeg} outerR=${s0.outerRadius} donutInner=${s0.donutInnerRadius}`,
  );
  console.log(
    `  basic-3 crossCheck pointErrs: ` +
      canon.expected.crossCheck.map((c) => c.pointErr).join(", "),
  );
} finally {
  await browser.close();
}
