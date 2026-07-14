#include "mermaid/flowchart/FlowchartLayout.h"

#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/FlowchartShapeRegistry.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/dagre/DagreLabels.h"
#include "mermaid/dagre/DagreUtil.h"
#include "mermaid/dagre/Layout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace muffin::mermaid::flowchart {
namespace {

namespace d = muffin::mermaid::dagre;  // visible to the anon-namespace helpers below

QString plainLabel(QString text) {
  static const QRegularExpression breakPattern(QStringLiteral("<br\\s*/?>"),
                                                QRegularExpression::CaseInsensitiveOption);
  text.replace(breakPattern, QStringLiteral("\n"));
  text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
  if (text.startsWith(QLatin1Char('`')) && text.endsWith(QLatin1Char('`')) && text.size() >= 2) {
    text = text.mid(1, text.size() - 2);
  }
  return text;
}

// Edge path generation is a 1:1 port of d3-shape's curve state machines
// (basis/linear/step/stepBefore/stepAfter/cardinal/monotoneX/monotoneY/bumpX/
// bumpY/catmullRom/natural), driven the way mermaid's `d3.line().curve(...)`
// drives them. See D3Curves.h. The previous hand-written basis/linear/step
// helpers reproduced curveBasis/curveLinear/curveStep for 2+ points; the
// faithful port reproduces all twelve d3 curves including their edge cases.
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
QPointF intersectNodeForShape(const d::DagreNodeLabel& node, const QString& type, const QPointF& point) {
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
  const QVector<QPointF> pts = flowShapePolygonPoints(ctype, node.width, node.height);
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
// a label for shapes whose outline depends on it (bang, cloud). plainLabel
// stays internal (anonymous) but is visible here in the enclosing namespace.
QSizeF measureLabel(const QString& text, const FlowTextOptions& options) {
  QFont font(options.fontFamily);
  font.setPixelSize(qRound(options.fontPixelSize));
  font.setHintingPreference(QFont::PreferNoHinting);
  const QFontMetricsF metrics(font);
  const QStringList lines = plainLabel(text).split(QLatin1Char('\n'));
  qreal width = 0.0;
  for (const QString& line : lines) {
    QTextLayout layout(line, font);
    QTextOption textOption;
    textOption.setUseDesignMetrics(true);
    layout.setTextOption(textOption);
    layout.beginLayout();
    QTextLine textLine = layout.createLine();
    if (textLine.isValid()) textLine.setLineWidth(std::numeric_limits<qreal>::max());
    layout.endLayout();
    width = std::max(width, textLine.isValid() ? textLine.naturalTextWidth()
                                               : metrics.horizontalAdvance(line));
  }
  return QSizeF(width, std::max<qsizetype>(1, lines.size()) * options.lineHeight);
}

QMap<QString, QSizeF> measureFlowchartNodes(const FlowchartData& data, FlowTextOptions options) {
  QMap<QString, QSizeF> result;
  for (const FlowVertex& vertex : data.vertices) {
    const QSizeF label = measureLabel(vertex.text, options);
    QSizeF size;
    const QString type = canonicalShape(vertex.type);
    const qreal pad = options.verticalPadding;  // mermaid flowchart node.padding (default 15)
    if (type == QLatin1String("round")) {
      size = QSizeF(label.width() + options.horizontalPadding,
                    label.height() + options.verticalPadding * 2.0);
    } else if (type == QLatin1String("circle")) {
      const qreal diameter = std::max(label.width(), label.height()) + options.verticalPadding;
      size = QSizeF(diameter, diameter);
    } else if (type == QLatin1String("diamond")) {
      const qreal diameter = label.width() + label.height() + options.verticalPadding * 2.0;
      size = QSizeF(diameter, diameter);
    } else if (type == QLatin1String("stadium")) {
      const qreal height = label.height() + options.verticalPadding;
      size = QSizeF(label.width() + height / 4.0 + options.horizontalPadding / 2.0,
                    height);
    } else if (type == QLatin1String("subroutine")) {
      size = QSizeF(label.width() + 16.0 + options.horizontalPadding / 2.0,
                    label.height() + options.verticalPadding);
    } else if (type == QLatin1String("cylinder")) {
      const qreal width = label.width() + options.horizontalPadding / 2.0;
      const qreal radiusY = (width / 2.0) / (2.5 + width / 50.0);
      size = QSizeF(width, label.height() + options.verticalPadding + radiusY * 3.0);
    } else if (type == QLatin1String("odd")) {
      const qreal height = label.height() + options.verticalPadding;
      size = QSizeF(label.width() + options.horizontalPadding / 2.0 + height / 4.0,
                    height);
    } else if (type == QLatin1String("hexagon")) {
      const qreal height = label.height() + options.verticalPadding;
      size = QSizeF(label.width() + options.horizontalPadding / 2.0 + height / 2.0,
                    height);
    } else if (type == QLatin1String("trapezoid")) {
      const qreal height = label.height() + options.verticalPadding;
      size = QSizeF(label.width() + options.horizontalPadding / 2.0 + height,
                    height);
    } else if (type == QLatin1String("inv_trapezoid")) {
      const qreal height = label.height() + options.verticalPadding * 2.0;
      size = QSizeF(label.width() + options.horizontalPadding + height,
                    height);
    } else if (type == QLatin1String("lean_right") ||
               type == QLatin1String("lean_left")) {
      const qreal height = label.height() + options.verticalPadding;
      size = QSizeF(label.width() + options.horizontalPadding / 2.0 + height,
                    height);
    } else if (type == QLatin1String("triangle") ||
               type == QLatin1String("flipped_triangle")) {
      // triangle/flippedTriangle: w=h=labelW+labelH+pad (tw=h, post-override).
      const qreal s = label.width() + label.height() + pad;
      size = QSizeF(s, s);
    } else if (type == QLatin1String("hourglass")) {
      size = QSizeF(30.0, 30.0);  // label-less, fixed min 30x30
    } else if (type == QLatin1String("notched_pentagon")) {
      size = QSizeF(label.width() + 2.0 * pad, label.height() + 2.0 * pad);
    } else if (type == QLatin1String("card")) {
      size = QSizeF(label.width() + pad + 12.0, label.height() + pad);  // NOTCH_SIZE=12
    } else if (type == QLatin1String("sloped_rect")) {
      size = QSizeF(label.width() + 2.0 * pad, (label.height() + 2.0 * pad) * 1.5);
    } else if (type == QLatin1String("divided_rect")) {
      size = QSizeF(label.width() + pad, (label.height() + pad) * 1.2);
    } else if (type == QLatin1String("lightning_bolt")) {
      size = QSizeF(35.0, 70.0);  // label-less, fixed 35x70 (bbox = w x 2h)
    } else if (type == QLatin1String("double_circle")) {
      const qreal d = label.width() + 2.0 * pad;  // diameter = bbox.w + 2*labelPadding
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
      const qreal th = label.height() + 2.0 * pad;  // tagWidth = 0.2*totalHeight
      size = QSizeF(label.width() + 2.0 * pad + 0.2 * th, th);
    } else if (type == QLatin1String("stacked_rect")) {
      size = QSizeF(label.width() + 2.0 * pad + 10.0, label.height() + 2.0 * pad + 10.0);  // rectOffset=5
    } else if (type == QLatin1String("lined_process")) {
      size = QSizeF(label.width() + 2.0 * pad + 16.0, label.height() + 2.0 * pad);  // FRAME_WIDTH=8
    } else if (type == QLatin1String("small_circle") ||
               type == QLatin1String("framed_circle")) {
      size = QSizeF(14.0, 14.0);  // stateStart/stateEnd: fixed r=7 (label-less)
    } else if (type == QLatin1String("window_pane")) {
      // windowPane: totalWidth = label.w + 2*pad + rectOffset(10) = label.w + 40.
      size = QSizeF(label.width() + 2.0 * pad + 10.0, label.height() + 2.0 * pad + 10.0);
    } else if (type == QLatin1String("lined_cylinder")) {
      // linedCylinder: w = label.w + 2*pad; ry = (w/2)/(2.5+w/50); node.height =
      // label.h + 2*pad + 3*ry (body + 2*ry for the top/bottom ellipse caps).
      const qreal width = label.width() + 2.0 * pad;
      const qreal ry = (width / 2.0) / (2.5 + width / 50.0);
      size = QSizeF(width, label.height() + 2.0 * pad + 3.0 * ry);
    } else if (type == QLatin1String("horizontal_cylinder")) {
      // tiltedCylinder: h = label.h + pad/2; ry = h/2; rx = ry/(2.5+h/50);
      // node.width = label.w + pad/2 + 3*rx (body + 2*rx caps).
      const qreal height = label.height() + pad / 2.0;
      const qreal ry = height / 2.0;
      const qreal rx = ry / (2.5 + height / 50.0);
      size = QSizeF(label.width() + pad / 2.0 + 3.0 * rx, height);
    } else if (type == QLatin1String("document")) {
      // waveEdgedRectangle: w = label.w + 2*pad; h = label.h + 2*pad; waveAmp = h/8;
      // node.height = h + 2*waveAmp = 1.25*h.
      const qreal w = label.width() + 2.0 * pad;
      const qreal h = label.height() + 2.0 * pad;
      size = QSizeF(w, h * 1.25);
    } else if (type == QLatin1String("flag")) {
      // waveRectangle: w = label.w + 2*pad; h = label.h + pad (labelPaddingY once);
      // node.height = h + 4*waveAmp = 1.5*h (sine bulges top AND bottom).
      const qreal w = label.width() + 2.0 * pad;
      const qreal h = label.height() + pad;
      size = QSizeF(w, h * 1.5);
    } else if (type == QLatin1String("multi_document")) {
      // multiWaveEdgedRectangle: w = label.w + 2*pad (h += 3*pad); node.width =
      // w + 2*rectOffset(10); node.height = 1.1875*h + 2*rectOffset.
      const qreal w = label.width() + 2.0 * pad;
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
      const qreal w = label.width() + 2.0 * pad;
      const qreal h = label.height() + 2.0 * pad;
      size = QSizeF(w * 1.1, h * 1.25);
    } else if (type == QLatin1String("bow_tie_rect")) {
      // bowTieRect: h = label.h + pad; ry = h/2; rx = ry/(2.5+h/50). The left
      // arc bulges OUT (to -w/2-rx) but the right arc bulges IN (to w/2-rx), so
      // the bbox's right edge is the rectangle corner at w/2 -> node.width =
      // w + rx (one rx, not two).
      const qreal h = label.height() + pad;
      const qreal ry = h / 2.0;
      const qreal rx = ry / (2.5 + h / 50.0);
      size = QSizeF(label.width() + 2.0 * pad + rx, h);
    } else if (type == QLatin1String("half_rounded_rect")) {
      // halfRoundedRectangle: w = label.w + 2*pad; h = label.h + 2*pad; radius=h/2.
      size = QSizeF(label.width() + 2.0 * pad, label.height() + 2.0 * pad);
    } else if (type == QLatin1String("curved_trapezoid")) {
      // curvedTrapezoid: w = max(20, (label.w+2*pad)*1.25); h = label.h + 2*pad;
      // the right-side arc bulges OUT to x=w (angle 270->90 passes through 180
      // where cos=-1 -> x = rw+radius = w), so node.width = w.
      const qreal h = label.height() + 2.0 * pad;
      const qreal w = std::max(20.0, (label.width() + 2.0 * pad) * 1.25);
      size = QSizeF(w, h);
    } else if (type == QLatin1String("brace_left") ||
               type == QLatin1String("brace_right") ||
               type == QLatin1String("braces")) {
      // curlyBrace*: w = label.w + pad; h = label.h + pad; radius = max(5, 0.1*h);
      // node.height = h + 2*radius; node.width = w + k*radius where k=2 (left,
      // right) or 2.5 (braces: +radius/2 from the right brace's outer arc).
      const qreal w = label.width() + pad;
      const qreal h = label.height() + pad;
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

FlowLayoutResult layoutFlowchartNodesDagre(const FlowchartData& data,
                                           const QMap<QString, QSizeF>& measuredNodes,
                                           FlowLayoutOptions options,
                                           std::vector<dagre::DagreSnapshot>* dagreSnapshots) {
  namespace d = muffin::mermaid::dagre;
  d::DagreGraph g({.directed = true, .multigraph = true, .compound = true});
  d::DagreGraphLabel gl;
  QString dir = data.direction.toUpper();
  if (dir.isEmpty()) dir = QStringLiteral("TB");
  gl.rankdir = dir;
  gl.nodesep = options.nodeSpacing;
  gl.edgesep = options.edgeSpacing;
  gl.ranksep = options.rankSpacing;
  g.setGraph(gl);

  for (const FlowVertex& v : data.vertices) {
    const QSizeF sz = measuredNodes.value(v.id);
    d::DagreNodeLabel nl;
    nl.width = sz.width();
    nl.height = sz.height();
    // forkJoin mutates node.width/height += state.padding/2 (default 8/2 = 4)
    // AFTER updateNodeBounds, so dagre lays out with a size 4px larger (each
    // axis) than the rendered bar's bbox. measureFlowchartNodes returns the bbox
    // (matching the golden sizing check); apply the mutation here so layout
    // positions match mermaid (which dagre-sized off the mutated value).
    if (canonicalShape(v.type) == QLatin1String("fork")) {
      nl.width += 4.0;
      nl.height += 4.0;
    }
    g.setNode(v.id, nl);
  }
  // Insert cluster nodes in REVERSE declaration order: dagre's nesting-graph and
  // initOrder iterate g.children()/g.nodes() in insertion order, and for a
  // symmetric compound graph (e.g. compound-crossing) that order picks one of two
  // mirror-optimal layouts. Mermaid's dagre-wrapper constructs clusters in the
  // order that yields the golden's chirality, which is the reverse of declaration
  // order here. Asymmetric compound graphs are unaffected (their tie-break is
  // forced), so this only flips the symmetric case.
  for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it)
    g.setNode(it->id, d::DagreNodeLabel{});
  // The parser lists a node in every subgraph scope that references it (including
  // edge endpoints), so a node can appear in several subgraph node lists. dagre
  // wants each node parented to its DIRECT (innermost) subgraph. Reconstruct that
  // by parenting each node to its deepest containing subgraph, and nesting
  // subgraphs under the subgraph that lists them.
  QSet<QString> subgraphIds;
  for (const FlowSubgraph& sg : data.subgraphs) subgraphIds.insert(sg.id);
  QHash<QString, QString> subgraphParent;
  for (const FlowSubgraph& sg : data.subgraphs)
    for (const QString& child : sg.nodes)
      if (subgraphIds.contains(child)) subgraphParent.insert(child, sg.id);
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
    if (subgraphParent.contains(sg.id)) g.setParent(sg.id, subgraphParent.value(sg.id));
  for (const FlowSubgraph& sg : data.subgraphs) {
    for (const QString& child : sg.nodes) {
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

  for (const FlowEdge& fe : data.edges) {
    d::DagreEdgeLabel el;
    el.minlen = 1;
    el.weight = 1;
    el.labelpos = QStringLiteral("c");  // mermaid default for all flowchart edges
    el.labeloffset = 10;
    if (!fe.text.isEmpty()) {
      QSizeF lbl = options.measuredEdgeLabels.value(fe.id);
      if (!lbl.isValid() || lbl.isEmpty()) {
        lbl = measureLabel(fe.text, {});
        lbl.rwidth() += 4.0;
        lbl.setHeight(21.0);
      }
      el.width = lbl.width();
      el.height = lbl.height();
    }
    if (fe.id.isEmpty())
      g.setEdge(fe.start, fe.end, el);
    else
      g.setEdge(fe.start, fe.end, el, fe.id);
  }

  d::runDagreLayout(g, dagreSnapshots);

  FlowLayoutResult result;
  QPointF origin(0.0, 0.0);
  if (!data.vertices.isEmpty()) {
    if (const d::DagreNodeLabel* n = g.node(data.vertices.first().id))
      origin = QPointF(n->x.value_or(0.0), n->y.value_or(0.0));
  }

  for (const FlowVertex& v : data.vertices) {
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
    FlowLayoutEdge out;
    out.id = fe.id;
    if (fe.start == fe.end) {
      // Self-edge: mermaid's renderer draws its own loop and does NOT use dagre's
      // positionSelfEdges points. Replicate the flat pipeline's control-point
      // geometry (which matches the golden) against dagre's node position.
      const d::DagreNodeLabel* n = g.node(fe.start);
      if (n) {
        const qreal nx = n->x.value_or(0.0);
        const qreal ny = n->y.value_or(0.0);
        QPointF control1, control2;
        if (dir == QLatin1String("BT")) {
          control1 = QPointF(nx - 18.0, ny - n->height * 0.95);
          control2 = QPointF(nx + 18.0, ny - n->height * 0.95);
        } else if (dir == QLatin1String("LR") || dir == QLatin1String("RL")) {
          const qreal side = dir == QLatin1String("LR") ? 1.0 : -1.0;
          const qreal boundary = side * n->width / 2.0;
          const qreal loop = side * n->height * 0.45;
          control1 = QPointF(nx + boundary + loop, ny - 18.0);
          control2 = QPointF(nx + boundary + loop, ny + 18.0);
        } else {  // TB
          control1 = QPointF(nx - 18.0, ny + n->height * 0.95);
          control2 = QPointF(nx + 18.0, ny + n->height * 0.95);
        }
        QVector<QPointF> points = {d::intersectRect(*n, control1), control1, control2,
                                    d::intersectRect(*n, control2)};
        for (QPointF& p : points) p -= origin;
        out.points = points;
        QVector<QPointF> rendered = points;
        clipForMarkers(rendered, fe.type);
        out.path = pathForCurve(rendered, fe.interpolate.isEmpty() ? options.curve : fe.interpolate);
        result.edges.push_back(out);
        continue;
      }
    }
    const d::DagreEdgeLabel* el =
        fe.id.isEmpty() ? g.edge(fe.start, fe.end) : g.edge(fe.start, fe.end, fe.id);
    if (el) {
      // mermaid's renderer drops dagre's rect-intersected endpoints and
      // re-intersects the first/last interior point against each node's shape:
      //   points = edge.points.slice(1, len-1)
      //   points.unshift(tail.intersect(points[0]))   // points[0] == edge.points[1]
      //   points.push(head.intersect(points[last]))    // points[last] == edge.points[len-2]
      // For rect/round shapes this reproduces dagre's rect intersect exactly;
      // for circle/diamond it differs on diagonal approaches.
      QVector<QPointF> absPts = el->points;
      const d::DagreNodeLabel* tailNode = g.node(fe.start);
      const d::DagreNodeLabel* headNode = g.node(fe.end);
      if (absPts.size() >= 3) {
        const QPointF firstInterior = absPts.at(1);
        const QPointF lastInterior = absPts.at(absPts.size() - 2);
        QVector<QPointF> reintersected;
        reintersected.reserve(absPts.size());
        reintersected.append(tailNode ? intersectNodeForShape(*tailNode, vertexType.value(fe.start),
                                                               firstInterior)
                                      : absPts.first());
        for (qsizetype i = 1; i + 1 < absPts.size(); ++i) reintersected.append(absPts.at(i));
        reintersected.append(headNode ? intersectNodeForShape(*headNode, vertexType.value(fe.end),
                                                              lastInterior)
                                      : absPts.last());
        absPts = reintersected;
      } else if (absPts.size() == 2) {
        // mermaid re-intersects BOTH endpoints of a 2-point edge too:
        //   points = [tail.intersect(points[0]), head.intersect(points[1])]
        // (flowDiagram chunk). For rect/round shapes this is a no-op (the point
        // is already on the rect boundary); for polygon/circle shapes it snaps
        // the endpoint onto the shape's actual border.
        absPts = {
            tailNode ? intersectNodeForShape(*tailNode, vertexType.value(fe.start), absPts.at(0))
                     : absPts.at(0),
            headNode ? intersectNodeForShape(*headNode, vertexType.value(fe.end), absPts.at(1))
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
    out.path = pathForCurve(rendered, fe.interpolate.isEmpty() ? options.curve : fe.interpolate);
    result.edges.push_back(out);
  }

  // Clusters: reverse subgraph order, matching the existing extraction.
  for (auto it = data.subgraphs.rbegin(); it != data.subgraphs.rend(); ++it) {
    const d::DagreNodeLabel* n = g.node(it->id);
    if (!n || n->width <= 0.0) continue;
    FlowLayoutCluster c;
    c.id = it->id;
    c.x = n->x.value_or(0.0) - origin.x();
    c.y = n->y.value_or(0.0) - origin.y();
    c.width = n->width;
    c.height = n->height;
    result.clusters.push_back(c);
  }
  return result;
}

}  // namespace muffin::mermaid::flowchart
