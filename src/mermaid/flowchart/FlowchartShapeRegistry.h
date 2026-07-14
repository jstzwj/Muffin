#pragma once

// Mermaid 11.16.0 flowchart shape registry — canonical name resolution.
//
// A flowchart node's shape arrives as `FlowVertex::type`, set by the parser
// from EITHER the legacy bracket syntax (e.g. `(text)` -> "round") OR the
// `@{ shape: NAME }` metadata (shortName like "rounded", or an alias like
// "event"). These three naming systems must collapse to one canonical key so
// that measure/intersect/geometry handle a shape exactly once.
//
// `canonicalShape(type)` returns that canonical key. The 13 legacy shapes keep
// their existing names as canonical (so the existing if-chains work unchanged);
// the expanded shapes get descriptive canonical names. Unrecognised names fall
// back to "rect", matching mermaid's default shape.
//
// The registry audit (mermaid 11.16.0 chunk-65BZPYT2.mjs `shapesDefs`) lists
// 49 documented flowchart shapes selectable via `@{ shape: }`. `upstreamShortNames()`
// returns that list for the registry-diff test (upstream - native must empty as
// milestone E completes).

#include <QString>
#include <QStringList>
#include <QHash>

namespace muffin::mermaid::flowchart {

// Canonical shape keys. The 13 legacy names are reused as canonical; the rest
// are the expanded-shape canonicals.
inline QString canonicalShape(const QString& type) {
  static const QHash<QString, QString> map = {
      // --- legacy 13 (canonical == legacy name) ---
      {QStringLiteral("rect"), QStringLiteral("rect")},
      {QStringLiteral("proc"), QStringLiteral("rect")},
      {QStringLiteral("process"), QStringLiteral("rect")},
      {QStringLiteral("rectangle"), QStringLiteral("rect")},
      {QStringLiteral("squareRect"), QStringLiteral("rect")},
      {QStringLiteral("round"), QStringLiteral("round")},
      {QStringLiteral("rounded"), QStringLiteral("round")},
      {QStringLiteral("event"), QStringLiteral("round")},
      {QStringLiteral("roundedRect"), QStringLiteral("round")},
      {QStringLiteral("circle"), QStringLiteral("circle")},
      {QStringLiteral("circ"), QStringLiteral("circle")},
      {QStringLiteral("diamond"), QStringLiteral("diamond")},
      {QStringLiteral("diam"), QStringLiteral("diamond")},
      {QStringLiteral("decision"), QStringLiteral("diamond")},
      {QStringLiteral("question"), QStringLiteral("diamond")},
      {QStringLiteral("stadium"), QStringLiteral("stadium")},
      {QStringLiteral("terminal"), QStringLiteral("stadium")},
      {QStringLiteral("pill"), QStringLiteral("stadium")},
      {QStringLiteral("subroutine"), QStringLiteral("subroutine")},
      {QStringLiteral("fr-rect"), QStringLiteral("subroutine")},
      {QStringLiteral("subprocess"), QStringLiteral("subroutine")},
      {QStringLiteral("subproc"), QStringLiteral("subroutine")},
      {QStringLiteral("framed-rectangle"), QStringLiteral("subroutine")},
      {QStringLiteral("cylinder"), QStringLiteral("cylinder")},
      {QStringLiteral("cyl"), QStringLiteral("cylinder")},
      {QStringLiteral("db"), QStringLiteral("cylinder")},
      {QStringLiteral("database"), QStringLiteral("cylinder")},
      {QStringLiteral("odd"), QStringLiteral("odd")},
      {QStringLiteral("hexagon"), QStringLiteral("hexagon")},
      {QStringLiteral("hex"), QStringLiteral("hexagon")},
      {QStringLiteral("prepare"), QStringLiteral("hexagon")},
      {QStringLiteral("trapezoid"), QStringLiteral("trapezoid")},
      {QStringLiteral("trap-b"), QStringLiteral("trapezoid")},
      {QStringLiteral("priority"), QStringLiteral("trapezoid")},
      {QStringLiteral("trapezoid-bottom"), QStringLiteral("trapezoid")},
      {QStringLiteral("inv_trapezoid"), QStringLiteral("inv_trapezoid")},
      {QStringLiteral("trap-t"), QStringLiteral("inv_trapezoid")},
      {QStringLiteral("manual"), QStringLiteral("inv_trapezoid")},
      {QStringLiteral("trapezoid-top"), QStringLiteral("inv_trapezoid")},
      {QStringLiteral("inv-trapezoid"), QStringLiteral("inv_trapezoid")},
      {QStringLiteral("lean_right"), QStringLiteral("lean_right")},
      {QStringLiteral("lean-r"), QStringLiteral("lean_right")},
      {QStringLiteral("lean-right"), QStringLiteral("lean_right")},
      {QStringLiteral("in-out"), QStringLiteral("lean_right")},
      {QStringLiteral("lean_left"), QStringLiteral("lean_left")},
      {QStringLiteral("lean-l"), QStringLiteral("lean_left")},
      {QStringLiteral("lean-left"), QStringLiteral("lean_left")},
      {QStringLiteral("out-in"), QStringLiteral("lean_left")},
      // --- expanded shapes (canonical == descriptive name) ---
      {QStringLiteral("double_circle"), QStringLiteral("double_circle")},
      {QStringLiteral("dbl-circ"), QStringLiteral("double_circle")},
      {QStringLiteral("double-circle"), QStringLiteral("double_circle")},
      {QStringLiteral("doublecircle"), QStringLiteral("double_circle")},
      {QStringLiteral("framed_circle"), QStringLiteral("framed_circle")},
      {QStringLiteral("fr-circ"), QStringLiteral("framed_circle")},
      {QStringLiteral("stop"), QStringLiteral("framed_circle")},
      {QStringLiteral("framed-circle"), QStringLiteral("framed_circle")},
      {QStringLiteral("small_circle"), QStringLiteral("small_circle")},
      {QStringLiteral("sm-circ"), QStringLiteral("small_circle")},
      {QStringLiteral("start"), QStringLiteral("small_circle")},
      {QStringLiteral("small-circle"), QStringLiteral("small_circle")},
      {QStringLiteral("card"), QStringLiteral("card")},
      {QStringLiteral("notch-rect"), QStringLiteral("card")},
      {QStringLiteral("notched-rectangle"), QStringLiteral("card")},
      {QStringLiteral("lined_process"), QStringLiteral("lined_process")},
      {QStringLiteral("lin-rect"), QStringLiteral("lined_process")},
      {QStringLiteral("lined-rectangle"), QStringLiteral("lined_process")},
      {QStringLiteral("lined-process"), QStringLiteral("lined_process")},
      {QStringLiteral("lin-proc"), QStringLiteral("lined_process")},
      {QStringLiteral("shaded-process"), QStringLiteral("lined_process")},
      {QStringLiteral("text"), QStringLiteral("text")},
      {QStringLiteral("bang"), QStringLiteral("bang")},
      {QStringLiteral("cloud"), QStringLiteral("cloud")},
      {QStringLiteral("document"), QStringLiteral("document")},
      {QStringLiteral("doc"), QStringLiteral("document")},
      {QStringLiteral("multi_document"), QStringLiteral("multi_document")},
      {QStringLiteral("docs"), QStringLiteral("multi_document")},
      {QStringLiteral("documents"), QStringLiteral("multi_document")},
      {QStringLiteral("st-doc"), QStringLiteral("multi_document")},
      {QStringLiteral("stacked-document"), QStringLiteral("multi_document")},
      {QStringLiteral("tagged_document"), QStringLiteral("tagged_document")},
      {QStringLiteral("tag-doc"), QStringLiteral("tagged_document")},
      {QStringLiteral("tagged-document"), QStringLiteral("tagged_document")},
      {QStringLiteral("lined_document"), QStringLiteral("lined_document")},
      {QStringLiteral("lin-doc"), QStringLiteral("lined_document")},
      {QStringLiteral("lined-document"), QStringLiteral("lined_document")},
      {QStringLiteral("datastore"), QStringLiteral("datastore")},
      {QStringLiteral("data-store"), QStringLiteral("datastore")},
      {QStringLiteral("horizontal_cylinder"), QStringLiteral("horizontal_cylinder")},
      {QStringLiteral("h-cyl"), QStringLiteral("horizontal_cylinder")},
      {QStringLiteral("das"), QStringLiteral("horizontal_cylinder")},
      {QStringLiteral("horizontal-cylinder"), QStringLiteral("horizontal_cylinder")},
      {QStringLiteral("lined_cylinder"), QStringLiteral("lined_cylinder")},
      {QStringLiteral("lin-cyl"), QStringLiteral("lined_cylinder")},
      {QStringLiteral("disk"), QStringLiteral("lined_cylinder")},
      {QStringLiteral("lined-cylinder"), QStringLiteral("lined_cylinder")},
      {QStringLiteral("bow_tie_rect"), QStringLiteral("bow_tie_rect")},
      {QStringLiteral("bow-rect"), QStringLiteral("bow_tie_rect")},
      {QStringLiteral("stored-data"), QStringLiteral("bow_tie_rect")},
      {QStringLiteral("bow-tie-rectangle"), QStringLiteral("bow_tie_rect")},
      {QStringLiteral("triangle"), QStringLiteral("triangle")},
      {QStringLiteral("tri"), QStringLiteral("triangle")},
      {QStringLiteral("extract"), QStringLiteral("triangle")},
      {QStringLiteral("flipped_triangle"), QStringLiteral("flipped_triangle")},
      {QStringLiteral("flip-tri"), QStringLiteral("flipped_triangle")},
      {QStringLiteral("manual-file"), QStringLiteral("flipped_triangle")},
      {QStringLiteral("flipped-triangle"), QStringLiteral("flipped_triangle")},
      {QStringLiteral("hourglass"), QStringLiteral("hourglass")},
      {QStringLiteral("collate"), QStringLiteral("hourglass")},
      {QStringLiteral("fork"), QStringLiteral("fork")},
      {QStringLiteral("join"), QStringLiteral("fork")},
      {QStringLiteral("forkJoin"), QStringLiteral("fork")},
      {QStringLiteral("filled_circle"), QStringLiteral("filled_circle")},
      {QStringLiteral("f-circ"), QStringLiteral("filled_circle")},
      {QStringLiteral("junction"), QStringLiteral("filled_circle")},
      {QStringLiteral("filled-circle"), QStringLiteral("filled_circle")},
      {QStringLiteral("notched_pentagon"), QStringLiteral("notched_pentagon")},
      {QStringLiteral("notch-pent"), QStringLiteral("notched_pentagon")},
      {QStringLiteral("loop-limit"), QStringLiteral("notched_pentagon")},
      {QStringLiteral("notched-pentagon"), QStringLiteral("notched_pentagon")},
      {QStringLiteral("curved_trapezoid"), QStringLiteral("curved_trapezoid")},
      {QStringLiteral("curv-trap"), QStringLiteral("curved_trapezoid")},
      {QStringLiteral("curved-trapezoid"), QStringLiteral("curved_trapezoid")},
      {QStringLiteral("display"), QStringLiteral("curved_trapezoid")},
      {QStringLiteral("sloped_rect"), QStringLiteral("sloped_rect")},
      {QStringLiteral("sl-rect"), QStringLiteral("sloped_rect")},
      {QStringLiteral("manual-input"), QStringLiteral("sloped_rect")},
      {QStringLiteral("sloped-rectangle"), QStringLiteral("sloped_rect")},
      {QStringLiteral("window_pane"), QStringLiteral("window_pane")},
      {QStringLiteral("win-pane"), QStringLiteral("window_pane")},
      {QStringLiteral("internal-storage"), QStringLiteral("window_pane")},
      {QStringLiteral("window-pane"), QStringLiteral("window_pane")},
      {QStringLiteral("divided_rect"), QStringLiteral("divided_rect")},
      {QStringLiteral("div-rect"), QStringLiteral("divided_rect")},
      {QStringLiteral("div-proc"), QStringLiteral("divided_rect")},
      {QStringLiteral("divided-rectangle"), QStringLiteral("divided_rect")},
      {QStringLiteral("divided-process"), QStringLiteral("divided_rect")},
      {QStringLiteral("half_rounded_rect"), QStringLiteral("half_rounded_rect")},
      {QStringLiteral("delay"), QStringLiteral("half_rounded_rect")},
      {QStringLiteral("half-rounded-rectangle"), QStringLiteral("half_rounded_rect")},
      {QStringLiteral("brace_left"), QStringLiteral("brace_left")},
      {QStringLiteral("brace"), QStringLiteral("brace_left")},
      {QStringLiteral("comment"), QStringLiteral("brace_left")},
      {QStringLiteral("brace-l"), QStringLiteral("brace_left")},
      {QStringLiteral("brace_right"), QStringLiteral("brace_right")},
      {QStringLiteral("brace-r"), QStringLiteral("brace_right")},
      {QStringLiteral("braces"), QStringLiteral("braces")},
      {QStringLiteral("lightning_bolt"), QStringLiteral("lightning_bolt")},
      {QStringLiteral("bolt"), QStringLiteral("lightning_bolt")},
      {QStringLiteral("com-link"), QStringLiteral("lightning_bolt")},
      {QStringLiteral("lightning-bolt"), QStringLiteral("lightning_bolt")},
      {QStringLiteral("crossed_circle"), QStringLiteral("crossed_circle")},
      {QStringLiteral("cross-circ"), QStringLiteral("crossed_circle")},
      {QStringLiteral("summary"), QStringLiteral("crossed_circle")},
      {QStringLiteral("crossed-circle"), QStringLiteral("crossed_circle")},
      {QStringLiteral("flag"), QStringLiteral("flag")},
      {QStringLiteral("paper-tape"), QStringLiteral("flag")},
      {QStringLiteral("stacked_rect"), QStringLiteral("stacked_rect")},
      {QStringLiteral("st-rect"), QStringLiteral("stacked_rect")},
      {QStringLiteral("procs"), QStringLiteral("stacked_rect")},
      {QStringLiteral("processes"), QStringLiteral("stacked_rect")},
      {QStringLiteral("stacked-rectangle"), QStringLiteral("stacked_rect")},
      {QStringLiteral("tagged_rect"), QStringLiteral("tagged_rect")},
      {QStringLiteral("tag-rect"), QStringLiteral("tagged_rect")},
      {QStringLiteral("tagged-rectangle"), QStringLiteral("tagged_rect")},
      {QStringLiteral("tag-proc"), QStringLiteral("tagged_rect")},
      {QStringLiteral("tagged-process"), QStringLiteral("tagged_rect")},
  };
  auto it = map.constFind(type);
  return it == map.constEnd() ? QStringLiteral("rect") : it.value();
}

// The 49 documented flowchart shape shortNames from mermaid 11.16.0's
// `shapesDefs` (chunk-65BZPYT2.mjs). The registry-diff test compares this
// against the ported set.
inline QStringList upstreamShortNames() {
  return {
      QStringLiteral("rect"), QStringLiteral("rounded"), QStringLiteral("stadium"),
      QStringLiteral("fr-rect"), QStringLiteral("circle"), QStringLiteral("diam"),
      QStringLiteral("hex"), QStringLiteral("trap-b"), QStringLiteral("trap-t"),
      QStringLiteral("lean-r"), QStringLiteral("lean-l"), QStringLiteral("odd"),
      QStringLiteral("flag"),
      QStringLiteral("dbl-circ"), QStringLiteral("fr-circ"), QStringLiteral("sm-circ"),
      QStringLiteral("notch-rect"), QStringLiteral("lin-rect"), QStringLiteral("text"),
      QStringLiteral("bang"), QStringLiteral("cloud"),
      QStringLiteral("doc"), QStringLiteral("docs"), QStringLiteral("tag-doc"),
      QStringLiteral("lin-doc"), QStringLiteral("cyl"), QStringLiteral("datastore"),
      QStringLiteral("h-cyl"), QStringLiteral("lin-cyl"), QStringLiteral("bow-rect"),
      QStringLiteral("tri"), QStringLiteral("flip-tri"), QStringLiteral("hourglass"),
      QStringLiteral("fork"), QStringLiteral("f-circ"), QStringLiteral("notch-pent"),
      QStringLiteral("curv-trap"), QStringLiteral("sl-rect"), QStringLiteral("win-pane"),
      QStringLiteral("div-rect"), QStringLiteral("delay"),
      QStringLiteral("brace"), QStringLiteral("brace-r"), QStringLiteral("braces"),
      QStringLiteral("bolt"), QStringLiteral("cross-circ"),
      QStringLiteral("st-rect"), QStringLiteral("tag-rect"),
  };
}

// Canonical shapes with a native geometry/size/intersect implementation.
// Expanded as milestone E progresses; the registry-diff test tracks the
// remaining (upstream - native) set.
inline QStringList nativeShapeCanonicalNames() {
  return {
      // legacy 13
      QStringLiteral("rect"), QStringLiteral("round"), QStringLiteral("circle"),
      QStringLiteral("diamond"), QStringLiteral("stadium"), QStringLiteral("subroutine"),
      QStringLiteral("cylinder"), QStringLiteral("odd"), QStringLiteral("hexagon"),
      QStringLiteral("trapezoid"), QStringLiteral("inv_trapezoid"),
      QStringLiteral("lean_right"), QStringLiteral("lean_left"),
      // expanded (milestone E) — sizing + intersect + geometry ported
      QStringLiteral("triangle"), QStringLiteral("flipped_triangle"),
      QStringLiteral("hourglass"), QStringLiteral("notched_pentagon"),
      QStringLiteral("card"), QStringLiteral("sloped_rect"),
      QStringLiteral("divided_rect"), QStringLiteral("lightning_bolt"),
      QStringLiteral("double_circle"), QStringLiteral("filled_circle"),
      QStringLiteral("crossed_circle"), QStringLiteral("fork"),
      QStringLiteral("text"), QStringLiteral("datastore"),
      QStringLiteral("tagged_rect"), QStringLiteral("stacked_rect"),
      QStringLiteral("lined_process"),
      // expanded (milestone E) — wave/arc/cylinder batch
      QStringLiteral("document"), QStringLiteral("multi_document"),
      QStringLiteral("tagged_document"), QStringLiteral("lined_document"),
      QStringLiteral("flag"), QStringLiteral("bow_tie_rect"),
      QStringLiteral("half_rounded_rect"), QStringLiteral("curved_trapezoid"),
      QStringLiteral("brace_left"), QStringLiteral("brace_right"),
      QStringLiteral("braces"), QStringLiteral("bang"),
      QStringLiteral("cloud"), QStringLiteral("small_circle"),
      QStringLiteral("framed_circle"), QStringLiteral("lined_cylinder"),
      QStringLiteral("horizontal_cylinder"), QStringLiteral("window_pane"),
  };
}

}  // namespace muffin::mermaid::flowchart
