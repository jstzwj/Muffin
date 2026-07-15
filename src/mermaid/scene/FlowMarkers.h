#pragma once

// Native port of mermaid 11.16.0's flowchart arrowhead markers
// (chunk-4F4KDU6L.mjs addEdgeMarker + the marker <defs> at L887-948). Each
// edge `type` (arrow_point / arrow_cross / arrow_circle / arrow_open /
// double_arrow_*) selects a markerEnd and (for double_* types) markerStart.
// Markers are referenced via `marker-end`/`marker-start` on the edge path;
// `markerUnits=userSpaceOnUse`. The marker inherits the edge stroke colour
// (mermaid clones the marker per stroke colour at render time — F3 painter
// concern; F2 captures only the marker TYPE for the scene golden).

#include <QString>

namespace muffin::mermaid::flowscene {

// Marker type names matching mermaid's marker ids (`{diagramId}_flowchart-v2-{name}`).
// "pointEnd"/"pointStart", "circleEnd"/"circleStart", "crossEnd"/"crossStart".
// Empty string = no marker (arrow_open / `---` / invisible `~~~`).
QString markerEndType(const QString& edgeType);
QString markerStartType(const QString& edgeType);

// Marker geometry (the <marker> def contents), for the F3 painter. Returns the
// path `d` (or circle attrs) + viewBox + refX/refY + markerWidth/Height for the
// given type. Empty path = unknown type.
struct MarkerGeometry {
  QString viewBox;     // "0 0 W H"
  qreal refX = 0, refY = 0;
  qreal markerWidth = 0, markerHeight = 0;
  QString tag;         // "path" or "circle"
  QString pathData;    // for "path"
  qreal cx = 0, cy = 0, r = 0;  // for "circle"
};
MarkerGeometry markerGeometry(const QString& type, bool useMargin = false);

}  // namespace muffin::mermaid::flowscene
