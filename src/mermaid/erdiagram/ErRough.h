#pragma once

#include "mermaid/rough/RoughOps.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::er {

// What rough.js draws an erDiagram shape with beyond its own defaults. For the
// shapes of an entity it is what userNodeOverrides makes of the theme and the
// entity's classDef and inline style.
struct ErRoughOptions {
  QString fill;   // empty where nothing fills; `none` where a style says so
  QString stroke; // empty where nothing strokes
  qreal strokeWidth = 1.3;
  // The dash of the outline as the entity declares it: "5 5"; empty for none.
  QString strokeDasharray;
  // The copies erBox draws behind the shapes under the handDrawn look are
  // drawn as in the classic look: not rough, and filled solid.
  bool solid = false;
  quint32 seed = 0;
};

// The options of an entity's shapes under the handDrawn look. Its rows take
// the theme's colors for their fill and the border for their stroke, whatever
// a style says of the entity; its dividers are drawn like its rectangle.
struct ErRoughEntity {
  ErRoughOptions table;
  ErRoughOptions oddRows, evenRows;
};

// The width of an outline from what the entity declares: a number of pixels,
// or a length, and rough.js's own 1.3 where it declares none.
qreal roughStrokeWidth(const QString& declared);

rough::Options roughOptions(const ErRoughOptions& options);

// The `stroke-dasharray` rough.js gives an outline from what the entity
// declares: one number is a dash and a gap of that length, two are one of
// each, and anything else, a missing value included, is none.
QString roughStrokeDasharray(const QString& declared);

// The path data of one of rough.js's op sets, as it writes it: "M x y" and
// "C x1 y1, x2 y2, x y", one after the other.
QString roughPathData(const rough::OpSet& set);

// An SVG path of absolute M, L and C commands, as rough.js reads one.
QVector<rough::PathCommand> pathCommands(const QString& data);

// A shape as rough.js draws it, and the box it takes: tight, of every set it
// is drawn by, the sketch of the fill included. It is the box dagre lays an
// entity out by under the handDrawn look, and the one the browser measures.
struct ErRoughShape {
  rough::Drawable drawable;
  QRectF bounds;
};

// The entities' shapes, in the coordinates they are laid out in.
ErRoughShape roughRectangle(const QRectF& rect, const ErRoughOptions& options);
ErRoughShape roughPolygon(const QVector<QPointF>& points, const ErRoughOptions& options);

// The same shape, drawn at `delta` from where it was.
ErRoughShape roughTranslated(ErRoughShape shape, const QPointF& delta);

// A relationship's path, which rough.js draws at a roughness of 0.3 and never
// fills, from the path the layout gives.
ErRoughShape roughRelationship(const QString& path, quint32 seed);

// Where an `orient="auto"` marker sits on a drawn path and how it is turned:
// on its first or its last vertex, along the tangent there.
struct ErPathAnchor {
  QPointF point;
  qreal angleDeg = 0.0;
};
ErPathAnchor roughPathStart(const ErRoughShape& shape);
ErPathAnchor roughPathEnd(const ErRoughShape& shape);

} // namespace muffin::mermaid::er
