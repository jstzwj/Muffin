// Verifier: recompute each frozen pie-geometry slice's arc path `d` using the
// exact d3 computation order (k = 2pi/filteredSum; a = k*cumulative;
// point = (r*sin(a), -r*cos(a)); 3-decimal round-tripping) and diff against the
// frozen oracle pathD strings. A 100% match proves the formula the C++
// PieScene generator uses reproduces d3 byte-for-byte (modulo ULP noise that the
// 3-decimal serialization absorbs), so the geometry oracle will be exact.
//
//   node scripts/verify_pie_pathd.mjs [tests/fixtures/mermaid/pie-geometry.json]
import fs from "node:fs";
import path from "node:path";

const fixture = path.resolve(
  process.argv[2] ?? path.join("tests", "fixtures", "mermaid", "pie-geometry.json"),
);
const root = JSON.parse(fs.readFileSync(fixture, "utf8"));

// d3-path 3-decimal serializer: round to 3 decimals, strip trailing zeros, -0 -> 0.
function fmt(v) {
  if (Math.abs(v) < 5e-4) return "0";
  const r = Math.round(v * 1000) / 1000;
  if (r === 0) return "0";
  let s = r.toFixed(3);
  s = s.replace(/0+$/, "").replace(/\.$/, "");
  return s;
}
const p = (r, deg) => { const a = (deg * Math.PI) / 180; return [r * Math.cos(a), r * Math.sin(a)]; };
const P = (r, a) => [r * Math.sin(a), -r * Math.cos(a)];  // d3 canvas convention

function genPath(startA, endA, outerR, innerR) {
  const f = (xy) => fmt(xy[0]) + "," + fmt(xy[1]);
  const span = endA - startA;
  const full = span >= 2 * Math.PI - 1e-9;
  const oStart = P(outerR, startA), oEnd = P(outerR, endA);
  if (full) {
    const opp = P(outerR, startA + Math.PI);
    if (innerR === 0)
      return `M${f(oStart)}A${fmt(outerR)},${fmt(outerR)},0,1,1,${f(opp)}A${fmt(outerR)},${fmt(outerR)},0,1,1,${f(oStart)}Z`;
    const iStart = P(innerR, startA), iOpp = P(innerR, startA + Math.PI);
    return `M${f(oStart)}A${fmt(outerR)},${fmt(outerR)},0,1,1,${f(opp)}A${fmt(outerR)},${fmt(outerR)},0,1,1,${f(oStart)}L${f(iStart)}A${fmt(innerR)},${fmt(innerR)},0,1,0,${f(iOpp)}A${fmt(innerR)},${fmt(innerR)},0,1,0,${f(iStart)}Z`;
  }
  const la = span > Math.PI ? 1 : 0;
  let d = `M${f(oStart)}A${fmt(outerR)},${fmt(outerR)},0,${la},1,${f(oEnd)}`;
  if (innerR === 0) return d + "L0,0Z";
  const iEnd = P(innerR, endA), iStart = P(innerR, startA);
  return d + `L${f(iEnd)}A${fmt(innerR)},${fmt(innerR)},0,${la},0,${f(iStart)}Z`;
}

let total = 0, exact = 0, near = 0;
for (const c of root.cases) {
  const exp = c.expected;
  const R = exp.constants.radius;
  const innerR = exp.config.effectiveDonutHole * R;
  // Reconstruct drawn sections + filtered sum from the oracle's own slice list
  // (authoritative order/values), then recompute via d3's k*cumulative order.
  const slices = exp.slices;
  const filteredSum = slices.reduce((s, x) => s + x.value, 0);
  const k = (2 * Math.PI) / filteredSum;
  let cum = 0;
  for (const s of slices) {
    const a0 = k * cum; cum += s.value; const a1 = k * cum;
    const got = genPath(a0, a1, R, innerR);
    total++;
    if (got === s.pathD) exact++;
    else {
      // Allow a single 3-decimal rounding slip (1 ULP at the 3rd decimal).
      const samePts = got.length === s.pathD.length;
      console.log(`MISMATCH ${c.id}/${s.label}\n  oracle: ${s.pathD}\n  recomputed: ${got}`);
      if (samePts) near++;
    }
  }
}
console.log(`\n${exact}/${total} byte-exact, ${near} near-misses`);
process.exit(exact === total ? 0 : 1);
