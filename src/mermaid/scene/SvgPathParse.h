#pragma once

// L2 render primitive: parse an SVG path `d` string into a QPainterPath.
//
// Shared by the class, state, and er painters, which each previously carried
// a near-identical anonymous-namespace copy (ErScenePainter.cpp documented its
// copy as mirroring ClassScenePainter / StateScenePainter). This is the
// faithful superset — it supports M/L/H/V/C/Q/Z in both absolute and relative
// forms — so it reproduces the exact QPainterPath each copy produced for any
// input the scene/layout oracles pin.
//
// The flowchart painter has a separate, richer path parser that also extracts
// edge endpoints and tangents for marker orientation; it is intentionally not
// routed through here.

class QString;
class QPainterPath;

namespace muffin::mermaid::scene {

QPainterPath parseSvgPath(const QString& d);

}  // namespace muffin::mermaid::scene
