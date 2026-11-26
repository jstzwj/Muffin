#include "mermaid/flowchart/FlowchartLayout.h"

#include "mermaid/flowchart/FlowchartShapeRegistry.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/dagre/DagreLabels.h"
#include "mermaid/dagre/DagreUtil.h"

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

namespace d = muffin::mermaid::dagre; // visible to the anon-namespace helpers below

void clipMarker(QVector<QPointF>& points, qreal distance) {
  if (points.size() < 2)
    return;
  const QPointF delta = points.last() - points.at(points.size() - 2);
  const qreal length = std::hypot(delta.x(), delta.y());
  if (length <= distance || qFuzzyIsNull(length))
    return;
  points.last() -= delta * (distance / length);
}

// Marker-aware path clipping. Mermaid shortens the edge path only for filled
// arrow markers whose tip sits at the endpoint and base extends back (point,
// double_arrow_point → 4px at the end; double also has a start marker). Open
// lines and centred markers (cross, circle) are NOT shortened — the marker is
// drawn on top at the endpoint. Distances are the empirically-matched refX
// equivalents (golden-verified for arrow_point/cross/circle/open).
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
  if (point.x() < cx)
    dx = -dx;
  qreal dy = std::abs(rx * ry * py / det);
  if (point.y() < cy)
    dy = -dy;
  return QPointF(cx + dx, cy + dy);
}

// intersectLine(p1, p2, q1, q2): intersection of line (p1->p2) with segment
// (q1->q2). Returns nullopt when they don't cross. p1 is the ray origin (node
// centre), p2 the approach point.
std::optional<QPointF> intersectLine(const QPointF& p1, const QPointF& p2, const QPointF& q1, const QPointF& q2) {
  const qreal a1 = p2.y() - p1.y();
  const qreal b1 = p1.x() - p2.x();
  const qreal c1 = p2.x() * p1.y() - p1.x() * p2.y();
  const qreal r3 = a1 * q1.x() + b1 * q1.y() + c1;
  const qreal r4 = a1 * q2.x() + b1 * q2.y() + c1;
  const auto sameSign = [](qreal x, qreal y) { return x * y > 0.0; };
  if (r3 != 0.0 && r4 != 0.0 && sameSign(r3, r4))
    return std::nullopt;
  const qreal a2 = q2.y() - q1.y();
  const qreal b2 = q1.x() - q2.x();
  const qreal c2 = q2.x() * q1.y() - q1.x() * q2.y();
  const qreal r1 = a2 * p1.x() + b2 * p1.y() + c2;
  const qreal r2 = a2 * p2.x() + b2 * p2.y() + c2;
  if (std::abs(r1) < 1e-6 && std::abs(r2) < 1e-6 && sameSign(r1, r2))
    return std::nullopt;
  const qreal denom = a1 * b2 - a2 * b1;
  if (denom == 0.0)
    return std::nullopt;
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

QPointF intersectPolygon(const d::DagreNodeLabel& node, const QVector<QPointF>& polyPoints, const QPointF& point) {
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
    if (auto hit =
            intersectLine(center, point, QPointF(left + p1.x(), top + p1.y()), QPointF(left + p2.x(), top + p2.y())))
      hits.push_back(*hit);
  }
  if (hits.isEmpty())
    return center; // mermaid returns `node` (its centre)
  if (hits.size() > 1) {
    std::sort(hits.begin(), hits.end(), [&point](const QPointF& p, const QPointF& q) {
      return std::hypot(p.x() - point.x(), p.y() - point.y()) < std::hypot(q.x() - point.x(), q.y() - point.y());
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
QPointF intersectNodeForShape(const d::DagreNodeLabel& node, const QString& type, const QPointF& point, FlowLook look) {
  // State terminals install Mermaid's circle intersect handler after their
  // RoughJS bounds have updated width/height. That handler intentionally uses
  // width / 2 for both axes, even when the rough bbox is not square.
  if (type == QLatin1String("stateStart") || type == QLatin1String("stateEnd")) {
    return intersectEllipse(node, node.width / 2.0, node.width / 2.0, point);
  }
  // A state's choice meets a transition on its diamond through
  // intersect.polygon(), which keeps intersectLine's half-pixel bias (the
  // flowchart diamond is the one handler that takes it back out below).
  if (type == QLatin1String("choice"))
    return intersectPolygon(node, flowShapePolygonPoints(QStringLiteral("diamond"), node.width, node.height, look),
                            point);
  const QString ctype = canonicalShape(type);
  if (ctype == QLatin1String("circle") || ctype == QLatin1String("double_circle") ||
      ctype == QLatin1String("filled_circle") || ctype == QLatin1String("crossed_circle") ||
      ctype == QLatin1String("small_circle") || ctype == QLatin1String("framed_circle")) {
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

} // namespace

void clipFlowEdgeForMarkers(QVector<QPointF>& points, const QString& type) {
  const bool filled = type == QLatin1String("arrow_point") || type == QLatin1String("double_arrow_point");
  if (filled)
    clipMarker(points, 4.0);
  // neo look: state edges carry arrow_barb_neo, whose upstream
  // markerOffsets entry (5.5) pulls the endpoint back — the -margin marker
  // then tips 2px past the shortened end (the deliberate stroke gap).
  if (type == QLatin1String("arrow_barb_neo"))
    clipMarker(points, 5.5);
  if (type == QLatin1String("double_arrow_point") && points.size() >= 2) {
    const QPointF delta = points.first() - points.at(1);
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length > 4.0 && !qFuzzyIsNull(length))
      points.first() -= delta * (4.0 / length);
  }
}

// measureLabel is exported (external linkage) so FlowchartShapes.cpp can measure
// a label for shapes whose outline depends on it (bang, cloud).
QSizeF measureLabel(const QString& text, const FlowTextOptions& options) {
  return measureLabel(text, QStringLiteral("text"), options);
}

FlowLabelDocument flowLabelDocument(const QString& text, const QString& labelType, const FlowTextOptions& options) {
  FlowLabelDocument document =
      options.htmlLabels ? parseFlowLabel(text, labelType) : parseFlowSvgLabel(text, labelType);
  document.baseWeight = options.fontWeight;
  prepareFlowLabelMath(document, options.fontPixelSize);
  if (options.maximumLineWidth > 0.0)
    document = wrapFlowLabel(document, options.fontFamily, options.fontPixelSize, options.maximumLineWidth);
  return document;
}

QSizeF measureLabel(const QString& text, const QString& labelType, const FlowTextOptions& options) {
  const FlowLabelDocument document = flowLabelDocument(text, labelType, options);
  QSizeF result;
  if (options.htmlLabels) {
    result = measureFlowLabel(document, options.fontFamily, options.fontPixelSize, options.lineHeight);
    // addHtmlSpan switches to display: table and an explicit CSS width when
    // the max-width constraint wraps the label, so the box keeps that width
    // instead of shrinking to the longest wrapped line.
    const bool wrapped = !document.visualLines.isEmpty() && options.maximumLineWidth > 0.0;
    if (document.math.isEmpty()) {
      // The width of an HTML label is the one Blink gives its inline box:
      // every run's advance rounded up to 1/64 px, a line at a time.
      qreal width = measureChromiumInlineLayoutWidth(document, options.fontFamily, options.fontPixelSize);
      if (wrapped)
        width = std::max(width, options.maximumLineWidth);
      result.setWidth(width);
    } else if (wrapped) {
      // A label with math is laid out as a row of boxes (layoutFlowLabel),
      // which keeps its own widths.
      result.setWidth(std::max(result.width(), options.maximumLineWidth));
    }
  } else {
    const QRectF visual = measureChromiumSvgTextBounds(document, options.fontFamily, options.fontPixelSize,
                                                       QFont::Normal, 1.0, options.chromiumSvgTerminalPhase);
    result = visual.size();
    result.setWidth(std::max(visual.width(),
                             measureChromiumInlineLayoutWidth(document, options.fontFamily, options.fontPixelSize)));
  }
  return result;
}

FlowEdgeLabelLayout layoutFlowchartEdgeLabel(const FlowEdge& edge, const FlowTextOptions& options) {
  FlowEdgeLabelLayout result;
  // The dagre edge pipeline always builds its formatted document with the SVG
  // parser first; useHtmlLabels only selects the resulting DOM container and
  // bbox branch. Keeping those decisions separate also preserves Mermaid's
  // rich edge-label Math/Markdown sizing.
  result.document = parseFlowSvgLabel(edge.text, edge.labelType);
  result.document.baseWeight = options.fontWeight;
  QSizeF content = measureFlowLabel(result.document, options.fontFamily, options.fontPixelSize, options.lineHeight);
  const qreal maximumWidth = std::max<qreal>(0.0, options.maximumLineWidth - 4.0);
  if (!result.document.text.contains(QLatin1Char('\n')) && maximumWidth > 0.0 && content.width() > maximumWidth) {
    result.document = wrapFlowLabel(result.document, options.fontFamily, options.fontPixelSize, maximumWidth);
    if (!options.htmlLabels)
      result.document.visualLineAdvance = flowSvgFormattedTextLineStep(options.fontPixelSize);
    content = measureFlowLabel(result.document, options.fontFamily, options.fontPixelSize, options.lineHeight);
  }
  if (!options.htmlLabels) {
    const QRectF ink = measureFlowSvgTextBounds(result.document, options.fontFamily, options.fontPixelSize);
    content.setWidth(std::max(content.width(), ink.width()));
  }
  result.size = content;
  if (options.edgeLabelRectNode)
    return result;
  result.size.rwidth() += 4.0;
  result.size.setWidth(std::max<qreal>(30.0, result.size.width()));
  const qsizetype lineCount = !result.document.visualLines.isEmpty()
                                  ? result.document.visualLines.size()
                                  : result.document.text.count(QLatin1Char('\n')) + 1;
  if (lineCount > 1) {
    result.size.setHeight(flowSvgFormattedTextBlockHeight(options.fontFamily, options.fontPixelSize, lineCount));
  } else {
    result.size.setHeight(flowLabelFontBoundingMetrics(options.fontFamily, options.fontPixelSize).height() + 4.0);
  }
  return result;
}

QSizeF measureFlowchartEdgeLabel(const FlowEdge& edge, const FlowTextOptions& options) {
  return layoutFlowchartEdgeLabel(edge, options).size;
}

QSizeF measureFlowchartClusterLabel(const FlowSubgraph& subgraph, const FlowTextOptions& options) {
  FlowTextOptions clusterOptions = options;
  clusterOptions.lineHeight = 17.0;
  FlowLabelDocument document = parseFlowSvgLabel(subgraph.title, subgraph.labelType);
  document.baseWeight = options.fontWeight;
  if (clusterOptions.htmlLabels)
    return measureFlowLabel(document, clusterOptions.fontFamily, clusterOptions.fontPixelSize,
                            clusterOptions.lineHeight);
  const QRectF visual = measureChromiumSvgTextBounds(document, clusterOptions.fontFamily, clusterOptions.fontPixelSize,
                                                     QFont::Normal, 1.0, clusterOptions.chromiumSvgTerminalPhase);
  QSizeF result = visual.size();
  result.setWidth(std::max(result.width(), measureChromiumInlineLayoutWidth(document, clusterOptions.fontFamily,
                                                                            clusterOptions.fontPixelSize)));
  return result;
}

QMap<QString, QSizeF> measureFlowchartNodes(const FlowchartData& data, FlowTextOptions options) {
  return measureFlowchartNodes(data, options, {});
}

QMap<QString, QSizeF> measureFlowchartNodes(const FlowchartData& data, FlowTextOptions options,
                                            const QMap<QString, FlowTextOptions>& perNodeOptions) {
  QMap<QString, QSizeF> result;
  for (const FlowVertex& vertex : data.vertices) {
    const FlowTextOptions nodeOptions = perNodeOptions.value(vertex.id, options);
    const QSizeF label = measureLabel(vertex.text, vertex.labelType, nodeOptions);
    QSizeF size;
    const QString type = canonicalShape(vertex.type);
    const qreal pad = nodeOptions.verticalPadding; // mermaid flowchart node.padding (default 15)
    const bool neo = nodeOptions.look == FlowLook::Neo;
    if (neo && type == QLatin1String("rect")) {
      // squareRect.ts: neo fixes the horizontal/vertical label padding at 16/12.
      size = QSizeF(label.width() + 32.0, label.height() + 24.0);
    } else if (type == QLatin1String("round")) {
      size = QSizeF(label.width() + nodeOptions.horizontalPadding, label.height() + nodeOptions.verticalPadding * 2.0);
    } else if (type == QLatin1String("circle")) {
      // circle.ts deliberately derives the radius from label width only.
      const qreal diameter =
          neo ? label.width() + 64.0 : std::max(label.width(), label.height()) + nodeOptions.verticalPadding;
      size = QSizeF(diameter, diameter);
    } else if (type == QLatin1String("diamond")) {
      const qreal diameter = label.width() + label.height() + nodeOptions.verticalPadding * 2.0;
      size = QSizeF(diameter, diameter);
    } else if (type == QLatin1String("stadium")) {
      const qreal height = label.height() + (neo ? 24.0 : nodeOptions.verticalPadding);
      size = QSizeF(label.width() + height / 4.0 + (neo ? 40.0 : nodeOptions.horizontalPadding / 2.0), height);
    } else if (type == QLatin1String("subroutine")) {
      size = QSizeF(label.width() + 16.0 + (neo ? 28.0 : nodeOptions.horizontalPadding / 2.0),
                    label.height() + (neo ? 12.0 : nodeOptions.verticalPadding));
    } else if (type == QLatin1String("cylinder")) {
      const qreal width = label.width() + (neo ? 24.0 : nodeOptions.horizontalPadding / 2.0);
      const qreal radiusY = (width / 2.0) / (2.5 + width / 50.0);
      size = QSizeF(width, label.height() + (neo ? 24.0 : nodeOptions.verticalPadding) + radiusY * 3.0);
    } else if (type == QLatin1String("odd")) {
      if (neo) {
        const qreal height = label.height() + 24.0;
        size = QSizeF(label.width() + 42.0 + height / 4.0, height);
      } else {
        const qreal height = label.height() + nodeOptions.verticalPadding;
        size = QSizeF(label.width() + nodeOptions.horizontalPadding / 2.0 + height / 4.0, height);
      }
    } else if (type == QLatin1String("hexagon")) {
      const qreal height = label.height() + (neo ? 70.0 : nodeOptions.verticalPadding);
      size =
          QSizeF(label.width() + (neo ? 32.0 : nodeOptions.horizontalPadding / 2.0) + 2.0 * height / (neo ? 3.5 : 4.0),
                 height);
    } else if (type == QLatin1String("trapezoid")) {
      const qreal height = label.height() + nodeOptions.verticalPadding;
      size = QSizeF(label.width() + (neo ? 30.0 : nodeOptions.horizontalPadding / 2.0) + height, height);
    } else if (type == QLatin1String("inv_trapezoid")) {
      const qreal height = label.height() + nodeOptions.verticalPadding * 2.0;
      size = QSizeF(label.width() + (neo ? 60.0 : nodeOptions.horizontalPadding) + height, height);
    } else if (type == QLatin1String("lean_right") || type == QLatin1String("lean_left")) {
      const qreal height = label.height() + nodeOptions.verticalPadding;
      size = QSizeF(label.width() + (neo ? 30.0 : nodeOptions.horizontalPadding / 2.0) + height, height);
    } else if (type == QLatin1String("triangle") || type == QLatin1String("flipped_triangle")) {
      // triangle/flippedTriangle: w=h=labelW+labelH+pad (tw=h, post-override).
      const qreal s = label.width() + label.height() + (neo ? 30.0 : pad);
      size = QSizeF(s, s);
    } else if (type == QLatin1String("hourglass")) {
      size = QSizeF(30.0, 30.0); // label-less, fixed min 30x30
    } else if (type == QLatin1String("notched_pentagon")) {
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad), label.height() + (neo ? 24.0 : 2.0 * pad));
    } else if (type == QLatin1String("card")) {
      size = neo ? QSizeF(label.width() + 56.0, label.height() + 48.0)
                 : QSizeF(label.width() + pad + 12.0, label.height() + pad);
    } else if (type == QLatin1String("sloped_rect")) {
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad), (label.height() + (neo ? 24.0 : 2.0 * pad)) * 1.5);
    } else if (type == QLatin1String("divided_rect")) {
      const qreal p = neo ? 16.0 : pad;
      size = QSizeF(label.width() + p, (label.height() + p) * 1.2);
    } else if (type == QLatin1String("lightning_bolt")) {
      size = QSizeF(35.0, 70.0); // label-less, fixed 35x70 (bbox = w x 2h)
    } else if (type == QLatin1String("double_circle")) {
      const qreal d = label.width() + (neo ? 32.0 : 2.0 * pad);
      size = QSizeF(d, d);
    } else if (type == QLatin1String("filled_circle")) {
      size = QSizeF(14.0, 14.0); // r=7
    } else if (type == QLatin1String("crossed_circle")) {
      size = QSizeF(60.0, 60.0); // r=30
    } else if (type == QLatin1String("fork")) {
      // forkJoin: TB 70x10, LR/RL 10x70 (direction-aware).
      if (data.direction == QLatin1String("LR") || data.direction == QLatin1String("RL"))
        size = QSizeF(10.0, 70.0);
      else
        size = QSizeF(70.0, 10.0);
    } else if (type == QLatin1String("text")) {
      size = QSizeF(label.width() + pad, label.height() + pad); // padding once
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
    } else if (type == QLatin1String("small_circle") || type == QLatin1String("framed_circle")) {
      size = QSizeF(14.0, 14.0); // stateStart/stateEnd: fixed r=7 (label-less)
    } else if (type == QLatin1String("window_pane")) {
      // windowPane: totalWidth = label.w + 2*pad + rectOffset(10) = label.w + 40.
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad) + 10.0, label.height() + (neo ? 24.0 : 2.0 * pad) + 10.0);
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
      size = QSizeF(label.width() + (neo ? 32.0 : 2.0 * pad), label.height() + (neo ? 24.0 : 2.0 * pad));
    } else if (type == QLatin1String("curved_trapezoid")) {
      // curvedTrapezoid: w = max(20, (label.w+2*pad)*1.25); h = label.h + 2*pad;
      // the right-side arc bulges OUT to x=w (angle 270->90 passes through 180
      // where cos=-1 -> x = rw+radius = w), so node.width = w.
      const qreal h = label.height() + (neo ? 24.0 : 2.0 * pad);
      const qreal w = std::max(20.0, (label.width() + (neo ? 32.0 : 2.0 * pad)) * 1.25);
      size = QSizeF(w, h);
    } else if (type == QLatin1String("brace_left") || type == QLatin1String("brace_right") ||
               type == QLatin1String("braces")) {
      // curlyBrace*: w = label.w + pad; h = label.h + pad; radius = max(5, 0.1*h);
      // node.height = h + 2*radius; node.width = w + k*radius where k=2 (left,
      // right) or 2.5 (braces: +radius/2 from the right brace's outer arc).
      const qreal w = label.width() + (neo ? (type == QLatin1String("brace_left") ? 18.0 : 36.0) : pad);
      const qreal h = label.height() + (neo ? (type == QLatin1String("brace_left") ? 12.0 : 24.0) : pad);
      const qreal radius = std::max(5.0, h * 0.1);
      const qreal kW = (type == QLatin1String("braces")) ? 2.5 : 2.0;
      size = QSizeF(w + kW * radius, h + 2.0 * radius);
    } else if (type == QLatin1String("bang") || type == QLatin1String("cloud")) {
      // bang/cloud: SVG-arc path whose bbox is an affine function of the label-
      // derived path params. flowShapeArcShapeSize evaluates the forward model.
      size = flowShapeArcShapeSize(type, label.width(), label.height());
    } else {
      size = QSizeF(label.width() + nodeOptions.horizontalPadding * 2.0,
                    label.height() + nodeOptions.verticalPadding * 2.0);
    }
    result.insert(vertex.id, size);
  }
  return result;
}

QPointF intersectFlowShape(const FlowVertex& vertex, const QRectF& nodeRect, const QPointF& toward, FlowLook look) {
  d::DagreNodeLabel node;
  node.x = nodeRect.center().x();
  node.y = nodeRect.center().y();
  node.width = nodeRect.width();
  node.height = nodeRect.height();
  return intersectNodeForShape(node, vertex.type, toward, look);
}

FlowLayoutResult layoutFlowchartNodes(const FlowchartData& data, const QMap<QString, QSizeF>& measuredNodes,
                                      FlowLayoutOptions options) {
  return layoutFlowchartNodesDagre(data, measuredNodes, options);
}

} // namespace muffin::mermaid::flowchart
