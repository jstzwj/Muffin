// RequirementScenePainter — paints requirementBox nodes + relationship edges.
//
// Per CLAUDE.md / the lupdate convention this .cpp has NO `namespace muffin {}`
// block; helpers live in an anonymous namespace and the public function uses a
// fully-qualified name. No tr() calls.

#include "mermaid/requirement/RequirementScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/requirement/RequirementScene.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace flowchart = muffin::mermaid::flowchart;
namespace scene = muffin::mermaid::scene;

namespace {

constexpr qreal kReqPi = 3.14159265358979323846;

QColor resolveColor(const QString& value) {
  return muffin::mermaid::color::toQColor(value);
}

// Draws a row's text at its center (relative to the node center).
void paintRow(QPainter& painter, const muffin::mermaid::requirement::RequirementSceneRow& row,
              const QPointF& nodeCenter,
              const muffin::mermaid::requirement::RequirementSceneStyle& style) {
  if (row.text.isEmpty() || row.size.width() <= 0.0) return;
  const QPointF center = nodeCenter + row.center;
  const QRectF rect(center.x() - row.size.width() / 2.0,
                    center.y() - row.size.height() / 2.0,
                    row.size.width(), row.size.height());
  painter.save();
  painter.setClipRect(rect);
  flowchart::paintFlowLabel(painter, row.document, rect, style.fontFamily,
                            style.fontSize, style.lineHeight,
                            resolveColor(style.textColor), true);
  painter.restore();
}

// Draws the requirement_contains marker (circle r=9 + cross) at the start of an
// edge, rotated to the edge tangent. chunk-52WLFC77.mjs ~1034-1039.
void drawContainsMarker(QPainter& painter, const QPointF& endpoint,
                        const QPointF& tangent, const QColor& stroke) {
  const qreal angleDeg = std::atan2(tangent.y(), tangent.x()) * 180.0 / kReqPi;
  painter.save();
  painter.translate(endpoint);
  painter.rotate(angleDeg);
  // refX=0, refY=10 → anchor at local (0, 10). The marker viewBox is 20×20.
  painter.translate(0.0, -10.0);
  QPen pen(stroke, 1.0);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  // Circle: cx=10, cy=10, r=9, fill=none.
  painter.drawEllipse(QPointF(10.0, 10.0), 9.0, 9.0);
  // Horizontal line: (1,10) → (19,10).
  painter.drawLine(QPointF(1.0, 10.0), QPointF(19.0, 10.0));
  // Vertical line: (10,1) → (10,19).
  painter.drawLine(QPointF(10.0, 1.0), QPointF(10.0, 19.0));
  painter.restore();
}

// Draws the requirement_arrow marker (open V) at the end of an edge, rotated to
// the edge tangent. chunk-52WLFC77.mjs ~1013-1021.
void drawArrowMarker(QPainter& painter, const QPointF& endpoint,
                     const QPointF& tangent, const QColor& stroke) {
  const qreal angleDeg = std::atan2(tangent.y(), tangent.x()) * 180.0 / kReqPi;
  painter.save();
  painter.translate(endpoint);
  painter.rotate(angleDeg);
  // refX=20, refY=10 → anchor at local (20, 10).
  painter.translate(-20.0, -10.0);
  QPen pen(stroke, 1.0);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  // Open V: M0,0 L20,10 M20,10 L0,20.
  painter.drawLine(QPointF(0.0, 0.0), QPointF(20.0, 10.0));
  painter.drawLine(QPointF(20.0, 10.0), QPointF(0.0, 20.0));
  painter.restore();
}

// Continuous polyline for marker tangent estimation. Prefers `points`;
// otherwise stitches segments (dropping the shared joint vertex).
QVector<QPointF> edgePolyline(const muffin::mermaid::requirement::RequirementSceneEdge& edge) {
  if (!edge.points.isEmpty()) return edge.points;
  QVector<QPointF> flat;
  for (const QVector<QPointF>& seg : edge.segments) {
    if (seg.isEmpty()) continue;
    if (flat.isEmpty()) {
      flat = seg;
    } else {
      const qsizetype offset = (!flat.isEmpty() && flat.last() == seg.first()) ? 1 : 0;
      for (qsizetype i = offset; i < seg.size(); ++i) flat.append(seg.at(i));
    }
  }
  return flat;
}

QPainterPath edgePath(const muffin::mermaid::requirement::RequirementSceneEdge& edge) {
  if (!edge.path.isEmpty()) return scene::parseSvgPath(edge.path);
  QPainterPath path;
  const QVector<QPointF> points = edgePolyline(edge);
  if (points.isEmpty()) return path;
  path.moveTo(points.first());
  for (qsizetype i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
  return path;
}

}  // namespace

namespace muffin::mermaid::requirement {

void paintRequirementScene(const RequirementScene& scene, QPainter& painter,
                           const MermaidPaintOptions& options) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const QColor lineColor = resolveColor(scene.style.lineColor);

  // (1) Relationship edges + markers + labels. Drawn before nodes so node boxes
  // paint over line ends where they meet the box outline.
  for (const RequirementSceneEdge& edge : scene.edges) {
    const QRectF pathCull = edge.pathBounds.isValid() ? edge.pathBounds : scene.bounds;
    if (!mermaidPrimitiveIsVisible(pathCull, options)) continue;

    QPen pen(lineColor, 1.0);
    if (!edge.isContains) {
      // Dashed: stroke-dasharray: 10,7.
      pen.setStyle(Qt::CustomDashLine);
      pen.setDashPattern({10.0, 7.0});
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(edgePath(edge));

    const QVector<QPointF> pts = edgePolyline(edge);
    if (pts.size() >= 2) {
      if (edge.isContains) {
        // contains: marker at START. Tangent = edge direction at the start.
        const QPointF startDir = pts.at(1) - pts.first();
        drawContainsMarker(painter, pts.first(), startDir, lineColor);
      } else {
        // arrow: marker at END. Tangent = into the endpoint.
        const QPointF endDir = pts.last() - pts.at(pts.size() - 2);
        drawArrowMarker(painter, pts.last(), endDir, lineColor);
      }
    }

    // Centered edge label with a background fill (reqLabelBox).
    if (edge.labelPosition && !edge.label.isEmpty()) {
      const QPointF labelCenter = *edge.labelPosition;
      const QRectF labelRect(
          labelCenter.x() - edge.labelSize.width() / 2.0,
          labelCenter.y() - edge.labelSize.height() / 2.0,
          edge.labelSize.width(), edge.labelSize.height());
      if (mermaidPrimitiveIsVisible(labelRect, options)) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(resolveColor(scene.style.edgeLabelFill));
        painter.drawRect(labelRect);
        painter.save();
        painter.setClipRect(labelRect);
        flowchart::paintFlowLabel(painter, edge.labelDocument, labelRect,
                                  scene.style.fontFamily, scene.style.fontSize,
                                  scene.style.lineHeight,
                                  resolveColor(scene.style.edgeLabelColor), true);
        painter.restore();
      }
    }
  }

  // (2) Requirement box nodes.
  for (const RequirementSceneNode& node : scene.nodes) {
    const QRectF box(node.center.x() - node.size.width() / 2.0,
                     node.center.y() - node.size.height() / 2.0,
                     node.size.width(), node.size.height());
    if (!mermaidPrimitiveIsVisible(box, options)) continue;
    // Resolved box paint (compileStyles last-wins over the theme base). The box
    // outline and the divider share strokeWidth/dashArray; an invalid/none stroke
    // paints no outline. fill:none paints no fill. Rounded rect (requirementBox
    // uses roughjs rectangle with roughness=0 → a plain rect; mermaid applies rx
    // via the label-container CSS; a small rounding matches).
    //
    // Qt dash units are pen-width multiples (a non-cosmetic pen); SVG/rough
    // dashes are in px. Scale by 1/strokeWidth so `stroke-dasharray:5` renders a
    // 5px period regardless of stroke-width (sw=0 -> cosmetic pen, device units).
    const qreal dashInv = node.strokeWidth > 0.0 ? 1.0 / node.strokeWidth : 1.0;
    const bool hasDash = node.dashArray.size() >= 2 &&
                         (node.dashArray.at(0) != 0.0 || node.dashArray.at(1) != 0.0);
    QPen boxPen;
    if (!node.strokeValid) {
      boxPen = Qt::NoPen;
    } else {
      boxPen.setColor(resolveColor(node.stroke));
      boxPen.setWidthF(node.strokeWidth);
      if (hasDash) {
        boxPen.setStyle(Qt::CustomDashLine);
        boxPen.setDashPattern({node.dashArray.at(0) * dashInv, node.dashArray.at(1) * dashInv});
      }
    }
    painter.setPen(boxPen);
    // Explicit if/else: a ternary here resolves Qt::NoBrush through the
    // QColor(Qt::GlobalColor) ctor (Qt::NoBrush == 0 == Qt::color0 -> black),
    // painting a black fill instead of none.
    if (node.fillNone)
      painter.setBrush(Qt::NoBrush);
    else
      painter.setBrush(resolveColor(node.fill));
    painter.drawRoundedRect(box, 5.0, 5.0);

    // Divider line under the name (only if body rows present). Its Y is
    // precomputed on the node (mermaid's body-top: top + typeHeight + nameHeight
    // + gap) — see buildRequirementScene. The divider follows the box's
    // stroke/strokeWidth/dashArray (upstream applies nodeStyles to every path).
    if (node.hasDivider) {
      const qreal divY = node.center.y() + node.dividerY;
      const QPointF p1(node.center.x() - node.size.width() / 2.0, divY);
      const QPointF p2(node.center.x() + node.size.width() / 2.0, divY);
      QPen divPen;
      if (!node.strokeValid) {
        divPen = Qt::NoPen;
      } else {
        divPen.setColor(resolveColor(node.dividerStroke));
        divPen.setWidthF(node.strokeWidth);
        if (hasDash) {
          divPen.setStyle(Qt::CustomDashLine);
          divPen.setDashPattern({node.dashArray.at(0) * dashInv, node.dashArray.at(1) * dashInv});
        }
      }
      painter.setPen(divPen);
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(p1, p2);
    }

    // Rows: type line, name (bold), body rows.
    for (const RequirementSceneRow& row : node.rows)
      paintRow(painter, row, node.center, scene.style);
  }
}

void RequirementScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintRequirementScene(*this, painter, options);
}

}  // namespace muffin::mermaid::requirement
