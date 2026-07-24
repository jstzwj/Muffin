#include "mermaid/flowchart/FlowchartLayout.h"

#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/FlowchartShapeRegistry.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/dagre/DagreLabels.h"
#include "mermaid/dagre/DagreUtil.h"
#include "mermaid/dagre/Layout.h"

#include <QQueue>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace muffin::mermaid::flowchart {
namespace {

namespace d = muffin::mermaid::dagre;  // visible to the anon-namespace helpers below

// Edge path generation is a 1:1 port of d3-shape's curve state machines
// (basis/linear/step/stepBefore/stepAfter/cardinal/monotoneX/monotoneY/bumpX/
// bumpY/catmullRom/natural), driven the way mermaid's `d3.line().curve(...)`
// drives them. See D3Curves.h. The previous hand-written basis/linear/step
// helpers reproduced curveBasis/curveLinear/curveStep for 2+ points; the
// faithful port reproduces all twelve d3 curves including their edge cases;
// D3Curves.h also carries Mermaid's separate quadratic rounded-path generator.
QString pathForCurve(const QVector<QPointF>& points, const QString& curve) {
  return d3curve::pathForCurve(points, curve);
}

void clipMarker(QVector<QPointF>& points, qreal distance) {
  if (points.size() < 2) return;
  const QPointF delta = points.last() - points.at(points.size() - 2);
  const qreal length = std::hypot(delta.x(), delta.y());
  if (length <= distance || qFuzzyIsNull(length)) return;
  points.last() -= delta * (distance / length);
}

// Marker-aware path clipping. Mermaid shortens the edge path only for filled
// arrow markers whose tip sits at the endpoint and base extends back (point,
// double_arrow_point → 4px at the end; double also has a start marker). Open
// lines and centred markers (cross, circle) are NOT shortened — the marker is
// drawn on top at the endpoint. Distances are the empirically-matched refX
// equivalents (golden-verified for arrow_point/cross/circle/open).
void clipForMarkers(QVector<QPointF>& points, const QString& type) {
  const bool filled = type == QLatin1String("arrow_point") ||
                      type == QLatin1String("double_arrow_point");
  if (filled) clipMarker(points, 4.0);
  if (type == QLatin1String("double_arrow_point") && points.size() >= 2) {
    const QPointF delta = points.first() - points.at(1);
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length > 4.0 && !qFuzzyIsNull(length)) points.first() -= delta * (4.0 / length);
  }
}

// --- per-shape border intersection (mermaid dagre-wrapper intersect/*) ---
// mermaid's renderer drops dagre's rect-intersected edge endpoints and
// re-intersects the first/last interior point against each node's shape via
// `tail.intersect` / `head.intersect`. These four helpers are 1:1 ports of
// node_modules/mermaid/dist/.../chunk-65BZPYT2.mjs (intersectEllipse/Circle/Polygon
// + intersectLine). intersectRect is reused from DagreUtil.

QPointF intersectEllipse(const d::DagreNodeLabel& node, qreal rx, qreal ry, const QPointF& point) {
  const qreal cx = node.x.value_or(0.0);
  const qreal cy = node.y.value_or(0.0);
  const qreal px = cx - point.x();
  const qreal py = cy - point.y();
  const qreal det = std::sqrt(rx * rx * py * py + ry * ry * px * px);
  qreal dx = std::abs(rx * ry * px / det);
  if (point.x() < cx) dx = -dx;
  qreal dy = std::abs(rx * ry * py / det);
  if (point.y() < cy) dy = -dy;
  return QPointF(cx + dx, cy + dy);
}

// intersectLine(p1, p2, q1, q2): intersection of line (p1->p2) with segment
// (q1->q2). Returns nullopt when they don't cross. p1 is the ray origin (node
// centre), p2 the approach point.
std::optional<QPointF> intersectLine(const QPointF& p1, const QPointF& p2,
                                     const QPointF& q1, const QPointF& q2) {
  const qreal a1 = p2.y() - p1.y();
  const qreal b1 = p1.x() - p2.x();
  const qreal c1 = p2.x() * p1.y() - p1.x() * p2.y();
  const qreal r3 = a1 * q1.x() + b1 * q1.y() + c1;
  const qreal r4 = a1 * q2.x() + b1 * q2.y() + c1;
  const auto sameSign = [](qreal x, qreal y) { return x * y > 0.0; };
  if (r3 != 0.0 && r4 != 0.0 && sameSign(r3, r4)) return std::nullopt;
  const qreal a2 = q2.y() - q1.y();
  const qreal b2 = q1.x() - q2.x();
  const qreal c2 = q2.x() * q1.y() - q1.x() * q2.y();
  const qreal r1 = a2 * p1.x() + b2 * p1.y() + c2;
  const qreal r2 = a2 * p2.x() + b2 * p2.y() + c2;
  if (std::abs(r1) < 1e-6 && std::abs(r2) < 1e-6 && sameSign(r1, r2)) return std::nullopt;
  const qreal denom = a1 * b2 - a2 * b1;
  if (denom == 0.0) return std::nullopt;
  // 1:1 port of dagre-d3's intersectLine, including its `offset = abs(denom/2)`
  // Graphics-Gems integer-rounding bias. The bias shifts each coordinate by
  // 0.5*sign(trueResult): for the positive-quadrant layouts mermaid always
  // produces (SVG coords >= 0, nodes placed at positive x/y), this is a uniform
  // +0.5 in both axes. Every polygon shape handler uses this biased result
  // directly EXCEPT the diamond (`question`), whose calcIntersect subtracts
  // (0.5,0.5) afterwards — netting the true quotient. That compensation is
  // applied in intersectNodeForShape so the diamond stays unbiased (matching
  // basic-shapes/diag-shapes) while other polygons carry the +0.5 bias
  // (matching mermaid's intersect_default.polygon, verified on hexagon).
  const qreal offset = std::abs(denom / 2.0);
  qreal num = b1 * c2 - b2 * c1;
  const qreal x = num < 0.0 ? (num - offset) / denom : (num + offset) / denom;
  num = a2 * c1 - a1 * c2;
  const qreal y = num < 0.0 ? (num - offset) / denom : (num + offset) / denom;
  return QPointF(x, y);
}

QPointF intersectPolygon(const d::DagreNodeLabel& node, const QVector<QPointF>& polyPoints,
                         const QPointF& point) {
  const qreal x1 = node.x.value_or(0.0);
  const qreal y1 = node.y.value_or(0.0);
  qreal minX = std::numeric_limits<qreal>::infinity();
  qreal minY = std::numeric_limits<qreal>::infinity();
  for (const QPointF& p : polyPoints) {
    minX = std::min(minX, p.x());
    minY = std::min(minY, p.y());
  }
  const qreal left = x1 - node.width / 2.0 - minX;
  const qreal top = y1 - node.height / 2.0 - minY;
  const QPointF center(x1, y1);
  QVector<QPointF> hits;
  for (int i = 0; i < polyPoints.size(); ++i) {
    const QPointF& p1 = polyPoints.at(i);
    const QPointF& p2 = polyPoints.at(i < polyPoints.size() - 1 ? i + 1 : 0);
    if (auto hit = intersectLine(center, point, QPointF(left + p1.x(), top + p1.y()),
                                 QPointF(left + p2.x(), top + p2.y())))
      hits.push_back(*hit);
  }
  if (hits.isEmpty()) return center;  // mermaid returns `node` (its centre)
  if (hits.size() > 1) {
    std::sort(hits.begin(), hits.end(), [&point](const QPointF& p, const QPointF& q) {
      return std::hypot(p.x() - point.x(), p.y() - point.y()) <
             std::hypot(q.x() - point.x(), q.y() - point.y());
    });
  }
  return hits.first();
}

// Dispatch to the shape's intersect. Circle-based shapes (circle,
// double_circle, filled_circle, crossed_circle) all intersect a circle of
// radius = node.width/2 (each handler sets node.width = 2*radius). Polygon-
// geometry shapes use their centred outline points via intersectPolygon — the
// polygon bbox aligns with the node bbox, so intersectPolygon places them
// correctly. Everything else (rect/round/stadium/subroutine/cylinder/datastore/
// text/tagged_rect/lined_process/divided_rect) falls through to intersectRect.
QPointF intersectNodeForShape(const d::DagreNodeLabel& node, const QString& type,
                              const QPointF& point, FlowLook look) {
  const QString ctype = canonicalShape(type);
  if (ctype == QLatin1String("circle") ||
      ctype == QLatin1String("double_circle") ||
      ctype == QLatin1String("filled_circle") ||
      ctype == QLatin1String("crossed_circle") ||
      ctype == QLatin1String("small_circle") ||
      ctype == QLatin1String("framed_circle")) {
    return intersectEllipse(node, node.width / 2.0, node.height / 2.0, point);
  }
  // bang/cloud render an arc outline (so flowShapePolygonPoints is non-empty)
  // but their handler intersects with intersect_default.rect — short-circuit
  // before the polygon path so edges meet the rect, not the burst silhouette.
  if (ctype == QLatin1String("bang") || ctype == QLatin1String("cloud")) {
    return d::intersectRect(node, point);
  }
  const QVector<QPointF> pts = flowShapePolygonPoints(ctype, node.width, node.height, look);
  if (!pts.isEmpty()) {
    QPointF hit = intersectPolygon(node, pts, point);
    // The diamond (`question`) is the one polygon whose handler compensates for
    // intersectLine's +0.5 bias: calcIntersect returns {res.x-0.5, res.y-0.5}.
    // For mermaid's positive-quadrant layouts this nets the true quotient, so
    // the diamond endpoints stay unbiased (basic-shapes/diag-shapes). Every other
    // polygon keeps the biased result above.
    if (ctype == QLatin1String("diamond")) {
      hit.rx() -= 0.5;
      hit.ry() -= 0.5;
    }
    return hit;
  }
  return d::intersectRect(node, point);
}

}  // namespace

// measureLabel is exported (external linkage) so FlowchartShapes.cpp can measure
// a label for shapes whose outline depends on it (bang, cloud).
QSizeF measureLabel(const QString& text, const FlowTextOptions& options) {
  return measureLabel(text, QStringLiteral("text"), options);
}

QSizeF measureLabel(const QString& text, const QString& labelType,
                    const FlowTextOptions& options) {
  FlowLabelDocument document = parseFlowLabel(text, labelType);
  prepareFlowLabelMath(document, options.fontPixelSize);
  return measureFlowLabel(document, options.fontFamily,
                          options.fontPixelSize, options.lineHeight);
}

FlowEdgeLabelLayout layoutFlowchartEdgeLabel(
    const FlowEdge& edge, const FlowTextOptions& options) {
  FlowEdgeLabelLayout result;
  result.document = parseFlowSvgLabel(edge.text, edge.labelType);
  QSizeF content = measureFlowLabel(result.document, options.fontFamily,
                                   options.fontPixelSize, options.lineHeight);
  if (!result.document.text.contains(QLatin1Char('\n')) &&
      content.width() > 196.0) {
    result.document = wrapFlowLabel(result.document, options.fontFamily,
                                    options.fontPixelSize, 196.0);
    result.document.visualLineAdvance =
        flowSvgFormattedTextLineStep(options.fontPixelSize);
    content = measureFlowLabel(result.document, options.fontFamily,
                               options.fontPixelSize, options.lineHeight);
  }
  result.size = content;
  result.size.rwidth() += 4.0;
  result.size.setWidth(std::max<qreal>(30.0, result.size.width()));
  const qsizetype lineCount = !result.document.visualLines.isEmpty()
      ? result.document.visualLines.size()
      : result.document.text.count(QLatin1Char('\n')) + 1;
  if (lineCount > 1) {
    result.size.setHeight(flowSvgFormattedTextBlockHeight(
        options.fontFamily, options.fontPixelSize, lineCount));
  } else {
    result.size.setHeight(flowLabelFontBoundingMetrics(
                              options.fontFamily,
                              options.fontPixelSize).height() + 4.0);
  }
  return result;
}

QSizeF measureFlowchartEdgeLabel(const FlowEdge& edge,
                                 const FlowTextOptions& options) {
  return layoutFlowchartEdgeLabel(edge, options).size;
}

QSizeF measureFlowchartClusterLabel(const FlowSubgraph& subgraph,
                                    const FlowTextOptions& options) {
  FlowTextOptions clusterOptions = options;
  clusterOptions.lineHeight = 17.0;
  return measureFlowLabel(parseFlowSvgLabel(subgraph.title, subgraph.labelType),
                          clusterOptions.fontFamily, clusterOptions.fontPixelSize,
                          clusterOptions.lineHeight);
}

QMap<QString, QSizeF> measureFlowchartNodes(const FlowchartData& data, FlowTextOptions options) {
  QMap<QString, QSizeF> result;
  for (const FlowVertex& vertex : data.vertices) {
    const QSizeF label = measureLabel(vertex.text, vertex.labelType, options);
    QSizeF size;
    const QString type = canonicalShape(vertex.type);
    const qreal pad = options.verticalPadding;  // mermaid flowchart node.padding (default 15)
    const bool neo = options.look == FlowLook::Neo;
    if (neo && type == QLatin1String("rect")) {
      // squareRect.ts: neo fixes the horizontal/vertical label padding at 16/12.
      size = QSizeF(label.width() + 32.0, label.height() + 24.0);
    } else if (type == QLatin1String("round")) {
      size = QSizeF(label.width() + options.horizontalPadding,
                    label.height() + options.verticalPadding * 2.0);
    } else if (type == QLatin1String("circle")) {
      // circle.ts deliberately derives the radius from label width only.
      const qreal diameter = neo ? label.width() + 64.0
                                 : std::max(label.width(), label.height()) + options.verticalPadding;
      size = QSizeF(diameter, diameter);
    } else if (type == QLatin1String("diamond")) {
      const qreal diameter = label.width() + label.height() + options.verticalPadding * 2.0;
      size = QSizeF(diameter, diameter);
    } else if (type == QLatin1String("stadium")) {
      const qreal height = label.height() + (neo ? 24.0 : options.verticalPadding);
      size = QSizeF(label.width() + height / 4.0 + (neo ? 40.0 : options.horizontalPadding / 2.0),
                    height);
    } else if (type == QLatin1String("subroutine")) {
      size = QSizeF(label.width() + 16.0 + (neo ? 28.0 : options.horizontalPadding / 2.0),
                    label.height() + (neo ? 12.0 : options.verticalPadding));
    } else if (type == QLatin1String("cylinder")) {
      const qreal width = label.width() + (neo ? 24.0 : options.horizontalPadding / 2.0);
      const qreal radiusY = (width / 2.0) / (2.5 + width / 50.0);
      size = QSizeF(width, label.height() + (neo ? 24.0 : options.verticalPadding) + radiusY * 3.0);
    } else if (type == QLatin1String("odd")) {
      if (neo) {
        const qreal height = label.height() + 24.0;
        size = QSizeF(label.width() + 42.0 + height / 4.0, height);
      }
      else {
        const qreal height = label.height() + options.verticalPadding;
        size = QSizeF(label.width() + options.horizontalPadding / 2.0 + height / 4.0,
                      height);
      }
    } else if (type == QLatin1String("hexagon")) {
      const qreal height = label.height() + (neo ? 70.0 : options.verticalPadding);
      size = QSizeF(label.width() + (neo ? 32.0 : options.horizontalPadding / 2.0) +
                        2.0 * height / (neo ? 3.5 : 4.0),
                    height);
    } else if (type == QLatin1String("trapezoid")) {
      const qreal height = label.height() + options.verticalPadding;
      size = QSizeF(label.width() + (neo ? 30.0 : options.horizontalPadding / 2.0) + height,
                    height);
    } else if (type == QLatin1String("inv_trapezoid")) {
      const qreal height = label.height() + options.verticalPadding * 2.0;
      size = QSizeF(label.width() + (neo ? 60.0 : options.horizontalPadding) + height,
                    height);
    } else if (type == QLatin1String("lean_right") ||
               type == QLatin1String("lean_left")) {
      const qreal height = label.height() + options.verticalPadding;
      size = QSizeF(label.width() + (neo ? 30.0 : options.horizontalPadding / 2.0) + height,
                    height);
    } else if (type == QLatin1String("triangle") ||
               type == QLatin1String("flipped_triangle")) {
      // triangle/flippedTriangle: w=h=labelW+labelH+pad (tw=h, post-override).
      const qreal s = label.width() + label.height() + (neo ? 30.0 : pad);
      size = QSizeF(s, s);
    } else if (type == QLatin1String("hourglass")) {
      size = QSizeF(30.0, 30.0);  // label-less, fixed min 30x30
    } else if (type == QLatin1String("notched_pentagon")) {
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad),
                    label.height() + (neo ? 24.0 : 2.0 * pad));
    } else if (type == QLatin1String("card")) {
      size = neo ? QSizeF(label.width() + 56.0, label.height() + 48.0)
                 : QSizeF(label.width() + pad + 12.0, label.height() + pad);
    } else if (type == QLatin1String("sloped_rect")) {
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad),
                    (label.height() + (neo ? 24.0 : 2.0 * pad)) * 1.5);
    } else if (type == QLatin1String("divided_rect")) {
      const qreal p = neo ? 16.0 : pad;
      size = QSizeF(label.width() + p, (label.height() + p) * 1.2);
    } else if (type == QLatin1String("lightning_bolt")) {
      size = QSizeF(35.0, 70.0);  // label-less, fixed 35x70 (bbox = w x 2h)
    } else if (type == QLatin1String("double_circle")) {
      const qreal d = label.width() + (neo ? 32.0 : 2.0 * pad);
      size = QSizeF(d, d);
    } else if (type == QLatin1String("filled_circle")) {
      size = QSizeF(14.0, 14.0);  // r=7
    } else if (type == QLatin1String("crossed_circle")) {
      size = QSizeF(60.0, 60.0);  // r=30
    } else if (type == QLatin1String("fork")) {
      // forkJoin: TB 70x10, LR/RL 10x70 (direction-aware).
      if (data.direction == QLatin1String("LR") || data.direction == QLatin1String("RL"))
        size = QSizeF(10.0, 70.0);
      else
        size = QSizeF(70.0, 10.0);
    } else if (type == QLatin1String("text")) {
      size = QSizeF(label.width() + pad, label.height() + pad);  // padding once
    } else if (type == QLatin1String("datastore")) {
      // drawRect delegate: labelPaddingX=2*pad (doubled), labelPaddingY=pad.
      size = QSizeF(label.width() + 4.0 * pad, label.height() + 2.0 * pad);
    } else if (type == QLatin1String("tagged_rect")) {
      const qreal th = label.height() + (neo ? 24.0 : 2.0 * pad);
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad) + 0.2 * th, th);
    } else if (type == QLatin1String("stacked_rect")) {
      const qreal offsets = neo ? 20.0 : 10.0;
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad) + offsets,
                    label.height() + (neo ? 24.0 : 2.0 * pad) + offsets);
    } else if (type == QLatin1String("lined_process")) {
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad) + (neo ? 8.0 : 16.0),
                    label.height() + (neo ? 24.0 : 2.0 * pad));
    } else if (type == QLatin1String("small_circle") ||
               type == QLatin1String("framed_circle")) {
      size = QSizeF(14.0, 14.0);  // stateStart/stateEnd: fixed r=7 (label-less)
    } else if (type == QLatin1String("window_pane")) {
      // windowPane: totalWidth = label.w + 2*pad + rectOffset(10) = label.w + 40.
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad) + 10.0,
                    label.height() + (neo ? 24.0 : 2.0 * pad) + 10.0);
    } else if (type == QLatin1String("lined_cylinder")) {
      // linedCylinder: w = label.w + 2*pad; ry = (w/2)/(2.5+w/50); node.height =
      // label.h + 2*pad + 3*ry (body + 2*ry for the top/bottom ellipse caps).
      const qreal width = label.width() + (neo ? 32.0 : 2.0 * pad);
      const qreal ry = (width / 2.0) / (2.5 + width / 50.0);
      size = QSizeF(width, label.height() + (neo ? 48.0 : 2.0 * pad) + 3.0 * ry);
    } else if (type == QLatin1String("horizontal_cylinder")) {
      // tiltedCylinder: h = label.h + pad/2; ry = h/2; rx = ry/(2.5+h/50);
      // node.width = label.w + pad/2 + 3*rx (body + 2*rx caps).
      const qreal height = label.height() + (neo ? 12.0 : pad / 2.0);
      const qreal ry = height / 2.0;
      const qreal rx = ry / (2.5 + height / 50.0);
      size = QSizeF(label.width() + (neo ? 12.0 : pad / 2.0) + 3.0 * rx, height);
    } else if (type == QLatin1String("document")) {
      // waveEdgedRectangle: w = label.w + 2*pad; h = label.h + 2*pad; waveAmp = h/8;
      // node.height = h + 2*waveAmp = 1.25*h.
      const qreal w = label.width() + (neo ? 32.0 : 2.0 * pad);
      const qreal h = label.height() + (neo ? 24.0 : 2.0 * pad);
      size = QSizeF(w, h * (neo ? 1.5 : 1.25));
    } else if (type == QLatin1String("flag")) {
      // waveRectangle: w = label.w + 2*pad; h = label.h + pad (labelPaddingY once);
      // node.height = h + 4*waveAmp = 1.5*h (sine bulges top AND bottom).
      const qreal w = label.width() + (neo ? 32.0 : 2.0 * pad);
      const qreal h = label.height() + (neo ? 20.0 : pad);
      size = QSizeF(w, h * 1.5);
    } else if (type == QLatin1String("multi_document")) {
      // multiWaveEdgedRectangle: w = label.w + 2*pad (h += 3*pad); node.width =
      // w + 2*rectOffset(10); node.height = 1.1875*h + 2*rectOffset.
      const qreal w = label.width() + (neo ? 32.0 : 2.0 * pad);
      const qreal h = label.height() + 3.0 * pad;
      size = QSizeF(w + 20.0, h * 1.1875 + 20.0);
    } else if (type == QLatin1String("tagged_document")) {
      // taggedWaveEdgedRectangle: w = label.w + 2*pad; h = label.h + 2*pad;
      // node.width = 1.1*w (wave rect +0.05w each side); node.height = 1.25*h.
      const qreal w = label.width() + 2.0 * pad;
      const qreal h = label.height() + 2.0 * pad;
      size = QSizeF(w * 1.1, h * 1.25);
    } else if (type == QLatin1String("lined_document")) {
      // linedWaveEdgedRect: same wave-rect bbox as tagged_document.
      const qreal w = label.width() + (neo ? 32.0 : 2.0 * pad);
      const qreal h = label.height() + (neo ? 24.0 : 2.0 * pad);
      size = QSizeF(w * 1.1, h * (neo ? 1.5 : 1.25));
    } else if (type == QLatin1String("bow_tie_rect")) {
      // bowTieRect: h = label.h + pad; ry = h/2; rx = ry/(2.5+h/50). The left
      // arc bulges OUT (to -w/2-rx) but the right arc bulges IN (to w/2-rx), so
      // the bbox's right edge is the rectangle corner at w/2 -> node.width =
      // w + rx (one rx, not two).
      const qreal h = label.height() + (neo ? 12.0 : pad);
      const qreal ry = h / 2.0;
      const qreal rx = ry / (2.5 + h / 50.0);
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad) + rx, h);
    } else if (type == QLatin1String("half_rounded_rect")) {
      // halfRoundedRectangle: w = label.w + 2*pad; h = label.h + 2*pad; radius=h/2.
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad),
                    label.height() + (neo ? 24.0 : 2.0 * pad));
    } else if (type == QLatin1String("curved_trapezoid")) {
      // curvedTrapezoid: w = max(20, (label.w+2*pad)*1.25); h = label.h + 2*pad;
      // the right-side arc bulges OUT to x=w (angle 270->90 passes through 180
      // where cos=-1 -> x = rw+radius = w), so node.width = w.
      const qreal h = label.height() + (neo ? 24.0 : 2.0 * pad);
      const qreal w = std::max(20.0, (label.width() + (neo ? 32.0 : 2.0 * pad)) * 1.25);
      size = QSizeF(w, h);
    } else if (type == QLatin1String("brace_left") ||
               type == QLatin1String("brace_right") ||
               type == QLatin1String("braces")) {
      // curlyBrace*: w = label.w + pad; h = label.h + pad; radius = max(5, 0.1*h);
      // node.height = h + 2*radius; node.width = w + k*radius where k=2 (left,
      // right) or 2.5 (braces: +radius/2 from the right brace's outer arc).
      const qreal w = label.width() + (neo ? (type == QLatin1String("brace_left") ? 18.0 : 36.0) : pad);
      const qreal h = label.height() + (neo ? (type == QLatin1String("brace_left") ? 12.0 : 24.0) : pad);
      const qreal radius = std::max(5.0, h * 0.1);
      const qreal kW = (type == QLatin1String("braces")) ? 2.5 : 2.0;
      size = QSizeF(w + kW * radius, h + 2.0 * radius);
    } else if (type == QLatin1String("bang") ||
               type == QLatin1String("cloud")) {
      // bang/cloud: SVG-arc path whose bbox is an affine function of the label-
      // derived path params. flowShapeArcShapeSize evaluates the forward model.
      size = flowShapeArcShapeSize(type, label.width(), label.height());
    } else {
      size = QSizeF(label.width() + options.horizontalPadding * 2.0,
                    label.height() + options.verticalPadding * 2.0);
    }
    result.insert(vertex.id, size);
  }
  return result;
}

FlowLayoutResult layoutFlowchartNodes(const FlowchartData& data,
                                      const QMap<QString, QSizeF>& measuredNodes,
                                      FlowLayoutOptions options) {
  // Milestone C: the native Dagre compound pipeline is now the layout engine.
  // The legacy flat WorkGraph pipeline below is retained (layoutFlowchartNodesLegacy)
  // only as a reference; route to the faithful Dagre port.
  return layoutFlowchartNodesDagre(data, measuredNodes, options);
}

static FlowLayoutResult layoutFlowchartNodesDagreScope(
    const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
    FlowLayoutOptions options, std::vector<dagre::DagreSnapshot>* dagreSnapshots,
    const QString& retainedRoot) {
  namespace d = muffin::mermaid::dagre;
  constexpr qreal kSelfLoopLabelRectExtent = 0.1;
  struct ExtractedCluster {
    QString id;
    QSet<QString> vertices;
    QSet<QString> subgraphs;
    FlowLayoutResult layout;
    QPointF center;
    QSizeF size;
  };

  QSet<QString> subgraphIds;
  QHash<QString, QString> subgraphParent;
  for (const FlowSubgraph& sg : data.subgraphs) subgraphIds.insert(sg.id);
  for (const FlowSubgraph& sg : data.subgraphs)
    for (const QString& child : sg.nodes)
      if (subgraphIds.contains(child)) subgraphParent.insert(child, sg.id);
  auto isSubgraphDescendant = [&](QString child, const QString& ancestor) {
    QSet<QString> guard;
    while (subgraphParent.contains(child) && !guard.contains(child)) {
      guard.insert(child);
      child = subgraphParent.value(child);
      if (child == ancestor) return true;
    }
    return false;
  };

  QVector<ExtractedCluster> extracted;
  QSet<QString> claimedVertices;
  QSet<QString> claimedSubgraphs;
  for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it) {
    if (it->id == retainedRoot || claimedSubgraphs.contains(it->id) || it->nodes.isEmpty()) continue;
    QSet<QString> members;
    for (const QString& id : it->nodes)
      if (!subgraphIds.contains(id)) members.insert(id);
    for (const FlowSubgraph& nested : data.subgraphs)
      if (nested.id != it->id && isSubgraphDescendant(nested.id, it->id))
        for (const QString& id : nested.nodes)
          if (!subgraphIds.contains(id)) members.insert(id);
    if (members.isEmpty()) continue;
    bool externalConnection = false;
    for (const FlowEdge& edge : data.edges) {
      if (members.contains(edge.start) != members.contains(edge.end)) {
        externalConnection = true;
        break;
      }
    }
    // Mermaid recursively extracts explicit-direction clusters even when an
    // edge crosses their boundary. The parent graph rebinds that endpoint to
    // the extracted cluster atom; non-explicit clusters remain extractable only
    // when they are closed.
    if (externalConnection && !it->hasExplicitDir) continue;

    ExtractedCluster item;
    item.id = it->id;
    item.vertices = members;
    item.subgraphs.insert(it->id);
    for (const FlowSubgraph& nested : data.subgraphs)
      if (isSubgraphDescendant(nested.id, it->id)) item.subgraphs.insert(nested.id);

    FlowchartData inner;
    const QString parentDirection = data.direction.toUpper().isEmpty()
                                        ? QStringLiteral("TB")
                                        : data.direction.toUpper();
    inner.direction = it->hasExplicitDir && !it->dir.isEmpty()
                          ? it->dir
                          : (!retainedRoot.isEmpty()
                                 ? QStringLiteral("TB")
                                 : (parentDirection == QLatin1String("TB")
                                        ? QStringLiteral("LR")
                                        : QStringLiteral("TB")));
    inner.title = data.title;
    inner.accTitle = data.accTitle;
    inner.accDescription = data.accDescription;
    inner.classes = data.classes;
    inner.tooltips = data.tooltips;
    inner.defaultEdgeStyles = data.defaultEdgeStyles;
    inner.defaultEdgeInterpolate = data.defaultEdgeInterpolate;
    for (const FlowVertex& vertex : data.vertices)
      if (item.vertices.contains(vertex.id)) inner.vertices.push_back(vertex);
    for (const FlowEdge& edge : data.edges)
      if (item.vertices.contains(edge.start) && item.vertices.contains(edge.end))
        inner.edges.push_back(edge);
    for (const FlowSubgraph& subgraph : data.subgraphs)
      if (item.subgraphs.contains(subgraph.id)) inner.subgraphs.push_back(subgraph);

    FlowLayoutOptions innerOptions = options;
    innerOptions.rankSpacing += 25.0;
    item.layout = layoutFlowchartNodesDagreScope(inner, measuredNodes, innerOptions,
                                                 nullptr, item.id);
    const auto root = std::find_if(item.layout.clusters.cbegin(), item.layout.clusters.cend(),
                                   [&](const FlowLayoutCluster& cluster) {
                                     return cluster.id == item.id;
                                   });
    if (root == item.layout.clusters.cend()) continue;
    item.center = QPointF(root->x, root->y);
    item.size = QSizeF(root->width, root->height);
    claimedVertices.unite(item.vertices);
    claimedSubgraphs.unite(item.subgraphs);
    extracted.push_back(std::move(item));
  }

  d::DagreGraph g({.directed = true, .multigraph = true, .compound = true});
  d::DagreGraphLabel gl;
  QString dir = data.direction.toUpper();
  if (dir.isEmpty()) dir = QStringLiteral("TB");
  gl.rankdir = dir;
  gl.nodesep = options.nodeSpacing;
  gl.edgesep = options.edgeSpacing;
  gl.ranksep = options.rankSpacing;
  gl.nodePadding = options.nodePadding;
  g.setGraph(gl);

  // flowDb.getData() emits cluster nodes first, in reverse declaration order,
  // followed by vertices in insertion order. Dagre uses graph insertion order
  // to break mirror-equivalent ordering ties, so this order is observable.
  QHash<QString, QSizeF> extractedSizes;
  for (const ExtractedCluster& item : extracted) extractedSizes.insert(item.id, item.size);
  QHash<QString, QString> extractedEndpoint;
  for (const ExtractedCluster& item : extracted)
    for (const QString& vertex : item.vertices) extractedEndpoint.insert(vertex, item.id);
  const auto layoutEndpoint = [&](const QString& vertex) {
    return extractedEndpoint.value(vertex, vertex);
  };
  auto addSubgraphNode = [&](const FlowSubgraph& subgraph) {
    if (claimedSubgraphs.contains(subgraph.id) && !extractedSizes.contains(subgraph.id)) return;
    d::DagreNodeLabel label;
    if (extractedSizes.contains(subgraph.id)) {
      label.width = extractedSizes.value(subgraph.id).width();
      label.height = extractedSizes.value(subgraph.id).height();
    }
    g.setNode(subgraph.id, label);
  };
  auto addVertexNode = [&](const FlowVertex& v) {
    if (claimedVertices.contains(v.id)) return;
    const QSizeF sz = measuredNodes.value(v.id);
    d::DagreNodeLabel nl;
    nl.width = sz.width();
    nl.height = sz.height();
    // Mermaid expands forkJoin by state.padding / 2 after measuring its bar.
    if (options.look == FlowLook::Classic && canonicalShape(v.type) == QLatin1String("fork")) {
      nl.width += 4.0;
      nl.height += 4.0;
    }
    g.setNode(v.id, nl);
    if (!retainedRoot.isEmpty()) {
      const bool hasSelfLoop = std::any_of(data.edges.cbegin(), data.edges.cend(),
                                           [&](const FlowEdge& edge) {
                                             return edge.start == v.id && edge.end == v.id;
                                           });
      if (hasSelfLoop) {
        d::DagreNodeLabel dummy;
        dummy.width = kSelfLoopLabelRectExtent;
        dummy.height = kSelfLoopLabelRectExtent;
        g.setNode(v.id + QStringLiteral("---") + v.id + QStringLiteral("---1"), dummy);
        g.setNode(v.id + QStringLiteral("---") + v.id + QStringLiteral("---2"), dummy);
      }
    }
  };
  if (!retainedRoot.isEmpty()) {
    const auto root = std::find_if(data.subgraphs.cbegin(), data.subgraphs.cend(),
                                   [&](const FlowSubgraph& subgraph) {
                                     return subgraph.id == retainedRoot;
                                   });
    if (root != data.subgraphs.cend()) addSubgraphNode(*root);
  }
  for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it) {
    if (!retainedRoot.isEmpty() && it->id == retainedRoot) continue;
    addSubgraphNode(*it);
  }
  for (const FlowVertex& vertex : data.vertices) addVertexNode(vertex);
  // The parser lists a node in every subgraph scope that references it (including
  // edge endpoints), so a node can appear in several subgraph node lists. dagre
  // wants each node parented to its DIRECT (innermost) subgraph. Reconstruct that
  // by parenting each node to its deepest containing subgraph, and nesting
  // subgraphs under the subgraph that lists them.
  auto subgraphDepth = [&](const QString& id) {
    int depth = 1;
    QString p = subgraphParent.value(id);
    QSet<QString> guard;
    while (!p.isNull() && !guard.contains(p)) {
      guard.insert(p);
      ++depth;
      p = subgraphParent.value(p);
    }
    return depth;
  };
  for (const FlowSubgraph& sg : data.subgraphs)
    if (!claimedSubgraphs.contains(sg.id) && subgraphParent.contains(sg.id) &&
        !claimedSubgraphs.contains(subgraphParent.value(sg.id)))
      g.setParent(sg.id, subgraphParent.value(sg.id));
  for (const FlowSubgraph& sg : data.subgraphs) {
    if (claimedSubgraphs.contains(sg.id)) continue;
    for (const QString& child : sg.nodes) {
      if (claimedVertices.contains(child) || claimedSubgraphs.contains(child)) continue;
      if (subgraphIds.contains(child)) continue;  // nested subgraph, handled above
      QString best = sg.id;
      int bestDepth = subgraphDepth(sg.id);
      for (const FlowSubgraph& sg2 : data.subgraphs)
        if (sg2.nodes.contains(child)) {
          const int d2 = subgraphDepth(sg2.id);
          if (d2 > bestDepth) {
            best = sg2.id;
            bestDepth = d2;
          }
        }
      g.setParent(child, best);
    }
  }
  for (const ExtractedCluster& item : extracted) {
    const QString parent = subgraphParent.value(item.id);
    if (!parent.isEmpty() && g.hasNode(parent)) g.setParent(item.id, parent);
  }

  auto preparedEdgeLabel = [&](const FlowEdge& edge) {
    FlowEdgeLabelLayout prepared = options.preparedEdgeLabels.value(edge.id);
    if (prepared.document.text.isNull())
      prepared = layoutFlowchartEdgeLabel(edge);
    const QSizeF measured = options.measuredEdgeLabels.value(edge.id);
    if (measured.isValid() && !measured.isEmpty()) prepared.size = measured;
    return prepared;
  };
  auto edgeLabel = [&](const FlowEdge& edge) {
    d::DagreEdgeLabel label;
    label.minlen = 1;
    label.weight = 1;
    label.labelpos = QStringLiteral("c");
    label.labeloffset = 10;
    if (!edge.text.isEmpty()) {
      const FlowEdgeLabelLayout prepared = preparedEdgeLabel(edge);
      label.width = prepared.size.width();
      label.height = prepared.size.height();
    }
    return label;
  };
  for (const FlowEdge& fe : data.edges) {
    const QString start = layoutEndpoint(fe.start);
    const QString end = layoutEndpoint(fe.end);
    if (start == end && (start != fe.start || end != fe.end)) continue;
    d::DagreEdgeLabel el = edgeLabel(fe);
    if (start == end) {
      // The flowchart renderer expands a self-loop into a three-edge cycle
      // through two 10x10 layout nodes. Dagre's native self-edge mechanism has
      // different compound bounds and is not used by Mermaid flowcharts.
      const QString dummy1 = start + QStringLiteral("---") + start + QStringLiteral("---1");
      const QString dummy2 = start + QStringLiteral("---") + start + QStringLiteral("---2");
      // The placeholders are declared as 10x10 in Mermaid data, then Chrome's
      // insertNode(labelRect) resolves their actual pre-Dagre SVG bbox to 0.1px.
      d::DagreNodeLabel dummy;
      dummy.width = kSelfLoopLabelRectExtent;
      dummy.height = kSelfLoopLabelRectExtent;
      g.setNode(dummy1, dummy);
      g.setNode(dummy2, dummy);
      const QString parent = g.parentOf(start);
      if (!parent.isNull()) {
        g.setParent(dummy1, parent);
        g.setParent(dummy2, parent);
      }
      d::DagreEdgeLabel empty = el;
      empty.width = 0.0;
      empty.height = 0.0;
      g.setEdge(start, dummy1, empty, start + QStringLiteral("-cyclic-special-0"));
      g.setEdge(dummy1, dummy2, el, start + QStringLiteral("-cyclic-special-1"));
      g.setEdge(dummy2, start, empty, start + QStringLiteral("-cyclic-special-2"));
      continue;
    }
    if (fe.id.isEmpty())
      g.setEdge(start, end, el);
    else
      g.setEdge(start, end, el, fe.id);
  }

  d::runDagreLayout(g, dagreSnapshots);

  FlowLayoutResult result;
  QPointF origin(0.0, 0.0);
  for (const FlowVertex& vertex : data.vertices)
    if (const d::DagreNodeLabel* n = g.node(vertex.id)) {
      origin = QPointF(n->x.value_or(0.0), n->y.value_or(0.0));
      break;
    }

  for (const FlowVertex& v : data.vertices) {
    if (claimedVertices.contains(v.id)) continue;
    const d::DagreNodeLabel* n = g.node(v.id);
    FlowLayoutNode node;
    node.id = v.id;
    node.x = (n ? n->x.value_or(0.0) : 0.0) - origin.x();
    node.y = (n ? n->y.value_or(0.0) : 0.0) - origin.y();
    node.width = n ? n->width : 0.0;
    node.height = n ? n->height : 0.0;
    node.rank = (n && n->rank.has_value()) ? *n->rank : 0;
    result.nodes.push_back(node);
  }

  // Edge endpoints are re-intersected against each node's shape (mermaid's
  // tail.intersect / head.intersect), so we need each vertex's shape type.
  QHash<QString, QString> vertexType;
  for (const FlowVertex& v : data.vertices) vertexType.insert(v.id, v.type);

  for (const FlowEdge& fe : data.edges) {
    const QString start = layoutEndpoint(fe.start);
    const QString end = layoutEndpoint(fe.end);
    if (start == end && (start != fe.start || end != fe.end)) continue;
    FlowLayoutEdge out;
    out.id = fe.id;
    if (!fe.text.isEmpty()) {
      const FlowEdgeLabelLayout prepared = preparedEdgeLabel(fe);
      out.labelSize = prepared.size;
      out.labelDocument = prepared.document;
    }
    if (fe.start == fe.end) {
      const d::DagreNodeLabel* node = g.node(start);
      const d::DagreNodeLabel* dummy1 = g.node(start + QStringLiteral("---") + start +
                                               QStringLiteral("---1"));
      const d::DagreNodeLabel* dummy2 = g.node(start + QStringLiteral("---") + start +
                                               QStringLiteral("---2"));
      if (node && dummy1 && dummy2) {
        const auto segmentPoints = [&](const QString& tailId,
                                       const QString& headId,
                                       const QString& edgeName,
                                       const QString& tailType,
                                       const QString& headType) {
          QVector<QPointF> points;
          const d::DagreEdgeLabel* segment = g.edge(tailId, headId, edgeName);
          if (!segment) return points;
          points = segment->points;
          const d::DagreNodeLabel* tail = g.node(tailId);
          const d::DagreNodeLabel* head = g.node(headId);
          if (points.size() >= 2) {
            points.first() = tail
                ? intersectNodeForShape(*tail, tailType, points.value(1), options.look)
                : points.first();
            points.last() = head
                ? intersectNodeForShape(*head, headType,
                                        points.value(points.size() - 2), options.look)
                : points.last();
          }
          for (QPointF& point : points) point -= origin;
          return points;
        };
        const QString dummy1Id = start + QStringLiteral("---") + start +
                                 QStringLiteral("---1");
        const QString dummy2Id = start + QStringLiteral("---") + start +
                                 QStringLiteral("---2");
        out.segments = {
            segmentPoints(start, dummy1Id,
                          start + QStringLiteral("-cyclic-special-0"),
                          vertexType.value(start), QStringLiteral("rect")),
            segmentPoints(dummy1Id, dummy2Id,
                          start + QStringLiteral("-cyclic-special-1"),
                          QStringLiteral("rect"), QStringLiteral("rect")),
            segmentPoints(dummy2Id, start,
                          start + QStringLiteral("-cyclic-special-2"),
                          QStringLiteral("rect"), vertexType.value(start)),
        };
        const QPointF center(node->x.value_or(0.0), node->y.value_or(0.0));
        const QPointF hint((dummy1->x.value_or(0.0) + dummy2->x.value_or(0.0)) / 2.0,
                           (dummy1->y.value_or(0.0) + dummy2->y.value_or(0.0)) / 2.0);
        const QPointF delta = hint - center;
        enum class LoopSide { Top, Right, Bottom, Left };
        LoopSide side;
        if (std::abs(delta.x()) > std::abs(delta.y()))
          side = delta.x() > 0.0 ? LoopSide::Right : LoopSide::Left;
        else if (std::abs(delta.y()) > 0.0)
          side = delta.y() > 0.0 ? LoopSide::Bottom : LoopSide::Top;
        else if (dir == QLatin1String("BT"))
          side = LoopSide::Bottom;
        else if (dir == QLatin1String("LR"))
          side = LoopSide::Right;
        else if (dir == QLatin1String("RL"))
          side = LoopSide::Left;
        else
          side = LoopSide::Top;

        const d::DagreEdgeLabel selfLabel = edgeLabel(fe);
        const qreal labelWidth = selfLabel.width;
        const qreal labelHeight = selfLabel.height;
        const qreal maxSpan = std::max<qreal>(36.0, std::min<qreal>(100.0, node->width * 0.8));
        const qreal span = std::clamp(std::max(labelWidth, node->width * 0.35), 36.0, maxSpan);
        const qreal depth = std::clamp(std::min(node->width, node->height) * 0.45, 24.0, 48.0);
        const qreal x = center.x(), y = center.y();
        if (side == LoopSide::Bottom) {
          const qreal edge = y + node->height / 2.0;
          out.points = {{x - span / 2.0, edge}, {x - span / 2.0, edge + depth},
                        {x + span / 2.0, edge + depth}, {x + span / 2.0, edge}};
          out.labelX = x;
          out.labelY = edge + depth + labelHeight / 2.0 + 4.0;
        } else if (side == LoopSide::Right) {
          const qreal edge = x + node->width / 2.0;
          out.points = {{edge, y - span / 2.0}, {edge + depth, y - span / 2.0},
                        {edge + depth, y + span / 2.0}, {edge, y + span / 2.0}};
          out.labelX = edge + depth + labelWidth / 2.0 + 4.0;
          out.labelY = y;
        } else if (side == LoopSide::Left) {
          const qreal edge = x - node->width / 2.0;
          out.points = {{edge, y - span / 2.0}, {edge - depth, y - span / 2.0},
                        {edge - depth, y + span / 2.0}, {edge, y + span / 2.0}};
          out.labelX = edge - depth - labelWidth / 2.0 - 4.0;
          out.labelY = y;
        } else {
          const qreal edge = y - node->height / 2.0;
          out.points = {{x - span / 2.0, edge}, {x - span / 2.0, edge - depth},
                        {x + span / 2.0, edge - depth}, {x + span / 2.0, edge}};
          out.labelX = x;
          out.labelY = edge - depth - labelHeight / 2.0 - 4.0;
        }
        out.points.first() = intersectNodeForShape(*node, vertexType.value(start),
                                                   out.points.at(1), options.look);
        out.points.last() = intersectNodeForShape(*node, vertexType.value(start),
                                                  out.points.at(out.points.size() - 2), options.look);
        out.hasLabelPosition = !fe.text.isEmpty();
        const d::DagreEdgeLabel* middle = g.edge(
            dummy1Id, dummy2Id, start + QStringLiteral("-cyclic-special-1"));
        if (out.hasLabelPosition && middle && middle->x && middle->y) {
          out.labelX = *middle->x;
          out.labelY = *middle->y;
        }
        for (QPointF& point : out.points) point -= origin;
        if (out.hasLabelPosition) {
          out.labelX -= origin.x();
          out.labelY -= origin.y();
        }
        QVector<QPointF> rendered = out.points;
        clipForMarkers(rendered, fe.type);
        const QString curve = !fe.interpolate.isEmpty()
                                  ? fe.interpolate
                                  : (!data.defaultEdgeInterpolate.isEmpty()
                                         ? data.defaultEdgeInterpolate
                                         : options.curve);
        out.path = pathForCurve(rendered, curve);
        result.edges.push_back(out);
        continue;
      }
    }
    const d::DagreEdgeLabel* el =
        fe.id.isEmpty() ? g.edge(start, end) : g.edge(start, end, fe.id);
    if (el) {
      // mermaid's renderer drops dagre's rect-intersected endpoints and
      // re-intersects the first/last interior point against each node's shape:
      //   points = edge.points.slice(1, len-1)
      //   points.unshift(tail.intersect(points[0]))   // points[0] == edge.points[1]
      //   points.push(head.intersect(points[last]))    // points[last] == edge.points[len-2]
      // For rect/round shapes this reproduces dagre's rect intersect exactly;
      // for circle/diamond it differs on diagonal approaches.
      QVector<QPointF> absPts = el->points;
      const d::DagreNodeLabel* tailNode = g.node(start);
      const d::DagreNodeLabel* headNode = g.node(end);
      if (absPts.size() >= 3) {
        const QPointF firstInterior = absPts.at(1);
        const QPointF lastInterior = absPts.at(absPts.size() - 2);
        QVector<QPointF> reintersected;
        reintersected.reserve(absPts.size());
        reintersected.append(tailNode ? intersectNodeForShape(*tailNode, vertexType.value(start),
                                                               firstInterior, options.look)
                                      : absPts.first());
        for (qsizetype i = 1; i + 1 < absPts.size(); ++i) reintersected.append(absPts.at(i));
        reintersected.append(headNode ? intersectNodeForShape(*headNode, vertexType.value(end),
                                                              lastInterior, options.look)
                                      : absPts.last());
        absPts = reintersected;
      } else if (absPts.size() == 2) {
        // mermaid re-intersects BOTH endpoints of a 2-point edge too:
        //   points = [tail.intersect(points[0]), head.intersect(points[1])]
        // (flowDiagram chunk). For rect/round shapes this is a no-op (the point
        // is already on the rect boundary); for polygon/circle shapes it snaps
        // the endpoint onto the shape's actual border.
        absPts = {
            tailNode ? intersectNodeForShape(*tailNode, vertexType.value(start), absPts.at(0), options.look)
                     : absPts.at(0),
            headNode ? intersectNodeForShape(*headNode, vertexType.value(end), absPts.at(1), options.look)
                     : absPts.at(1),
        };
      }
      for (const QPointF& p : absPts) out.points.push_back(p - origin);
      out.reversedForLayout = el->reversed;
      if (el->x.has_value()) {
        out.hasLabelPosition = true;
        out.labelX = *el->x - origin.x();
        out.labelY = *el->y - origin.y();
      }
    }
    QVector<QPointF> rendered = out.points;
    clipForMarkers(rendered, fe.type);
    const QString curve = !fe.interpolate.isEmpty()
                              ? fe.interpolate
                              : (!data.defaultEdgeInterpolate.isEmpty()
                                     ? data.defaultEdgeInterpolate
                                     : options.curve);
    out.path = pathForCurve(rendered, curve);
    result.edges.push_back(out);
  }

  QSet<QString> selfEdgeIds;
  for (const FlowEdge& edge : data.edges)
    if (edge.start == edge.end) selfEdgeIds.insert(edge.id);
  std::stable_partition(result.edges.begin(), result.edges.end(),
                        [&](const FlowLayoutEdge& edge) {
                          return !selfEdgeIds.contains(edge.id);
                        });

  // Clusters: reverse subgraph order, matching the existing extraction.
  for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it) {
    if (claimedSubgraphs.contains(it->id)) continue;
    const d::DagreNodeLabel* n = g.node(it->id);
    if (!n || n->width <= 0.0) continue;
    FlowLayoutCluster c;
    c.id = it->id;
    c.x = n->x.value_or(0.0) - origin.x();
    c.y = n->y.value_or(0.0) - origin.y();
    c.width = n->width;
    c.height = n->height;
    const QSizeF titleSize = options.measuredClusterLabels.value(it->id);
    if (titleSize.isValid() && !titleSize.isEmpty())
      c.width = std::max(c.width, titleSize.width() + 8.0);
    result.clusters.push_back(c);
  }

  for (const ExtractedCluster& item : extracted) {
    const d::DagreNodeLabel* atom = g.node(item.id);
    if (!atom) continue;
    const QPointF atomCenter(atom->x.value_or(0.0) - origin.x(),
                             atom->y.value_or(0.0) - origin.y());
    const QPointF delta = atomCenter - item.center;
    for (FlowLayoutNode node : item.layout.nodes) {
      node.x += delta.x();
      node.y += delta.y();
      result.nodes.push_back(std::move(node));
    }
    for (FlowLayoutEdge edge : item.layout.edges) {
      for (QPointF& point : edge.points) point += delta;
      for (QVector<QPointF>& segment : edge.segments)
        for (QPointF& point : segment) point += delta;
      if (edge.hasLabelPosition) {
        edge.labelX += delta.x();
        edge.labelY += delta.y();
      }
      result.edges.push_back(std::move(edge));
    }
    for (FlowLayoutCluster cluster : item.layout.clusters) {
      cluster.x += delta.x();
      cluster.y += delta.y();
      result.clusters.push_back(std::move(cluster));
    }
  }

  QSet<QString> semanticVertexIds;
  for (const FlowVertex& vertex : data.vertices) semanticVertexIds.insert(vertex.id);
  QStringList layoutNodeOrder;
  for (const QString& id : g.nodes()) {
    const auto extractedIt = std::find_if(extracted.cbegin(), extracted.cend(),
                                          [&](const ExtractedCluster& item) {
                                            return item.id == id;
                                          });
    if (extractedIt != extracted.cend()) {
      for (const FlowLayoutNode& node : extractedIt->layout.nodes)
        layoutNodeOrder.push_back(node.id);
    } else if (semanticVertexIds.contains(id) && !claimedVertices.contains(id)) {
      layoutNodeOrder.push_back(id);
    }
  }

  QHash<QString, FlowLayoutNode> nodesById;
  for (FlowLayoutNode& node : result.nodes) nodesById.insert(node.id, std::move(node));
  QHash<QString, FlowLayoutEdge> edgesById;
  QStringList layoutEdgeOrder;
  layoutEdgeOrder.reserve(result.edges.size());
  for (const FlowLayoutEdge& edge : result.edges) layoutEdgeOrder.push_back(edge.id);
  for (FlowLayoutEdge& edge : result.edges) edgesById.insert(edge.id, std::move(edge));
  QHash<QString, FlowLayoutCluster> clustersById;
  for (FlowLayoutCluster& cluster : result.clusters)
    clustersById.insert(cluster.id, std::move(cluster));

  FlowLayoutResult ordered;
  for (const QString& nodeId : layoutNodeOrder)
    if (nodesById.contains(nodeId)) ordered.nodes.push_back(nodesById.take(nodeId));
  for (const QString& edgeId : layoutEdgeOrder)
    if (edgesById.contains(edgeId)) ordered.edges.push_back(edgesById.take(edgeId));
  for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it)
    if (clustersById.contains(it->id)) ordered.clusters.push_back(clustersById.take(it->id));

  QPointF finalOrigin;
  if (!ordered.nodes.isEmpty()) finalOrigin = QPointF(ordered.nodes.first().x, ordered.nodes.first().y);
  if (!finalOrigin.isNull()) {
    for (FlowLayoutNode& node : ordered.nodes) {
      node.x -= finalOrigin.x();
      node.y -= finalOrigin.y();
    }
    for (FlowLayoutEdge& edge : ordered.edges) {
      for (QPointF& point : edge.points) point -= finalOrigin;
      for (QVector<QPointF>& segment : edge.segments)
        for (QPointF& point : segment) point -= finalOrigin;
      if (edge.hasLabelPosition) {
        edge.labelX -= finalOrigin.x();
        edge.labelY -= finalOrigin.y();
      }
    }
    for (FlowLayoutCluster& cluster : ordered.clusters) {
      cluster.x -= finalOrigin.x();
      cluster.y -= finalOrigin.y();
    }
  }
  QHash<QString, const FlowEdge*> semanticEdges;
  for (const FlowEdge& edge : data.edges) semanticEdges.insert(edge.id, &edge);
  for (FlowLayoutEdge& edge : ordered.edges) {
    QVector<QPointF> rendered = edge.points;
    const FlowEdge* semantic = semanticEdges.value(edge.id);
    if (semantic) clipForMarkers(rendered, semantic->type);
    const QString curve = semantic && !semantic->interpolate.isEmpty()
                              ? semantic->interpolate
                              : (!data.defaultEdgeInterpolate.isEmpty()
                                     ? data.defaultEdgeInterpolate
                                     : options.curve);
    edge.path = pathForCurve(rendered, curve);
  }
  return ordered;
}

FlowLayoutResult layoutFlowchartNodesDagre(const FlowchartData& data,
                                           const QMap<QString, QSizeF>& measuredNodes,
                                           FlowLayoutOptions options,
                                           std::vector<dagre::DagreSnapshot>* dagreSnapshots) {
  return layoutFlowchartNodesDagreScope(data, measuredNodes, std::move(options),
                                        dagreSnapshots, {});
}

}  // namespace muffin::mermaid::flowchart
