// RequirementScenePainter — paints requirementBox nodes + relationship edges.
//
// Per CLAUDE.md / the lupdate convention this .cpp has NO `namespace muffin {}`
// block; helpers live in an anonymous namespace and the public function uses a
// fully-qualified name. No tr() calls.

#include "mermaid/requirement/RequirementScenePainter.h"

#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/requirement/RequirementScene.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/scene/SvgStroke.h"
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

// Draws a row's text at its center (relative to the node center). Commit 3: the
// per-node resolved font/color live on the row (font-size/family/line-height/color
// fall back to the scene base when the node has no labelStyle for them); the
// weight/style/spacing/decoration ride on row.document (FlowLabel Commit-2 fields).
void paintRow(QPainter& painter, const muffin::mermaid::requirement::RequirementSceneRow& row,
              const QPointF& nodeCenter,
              const muffin::mermaid::requirement::RequirementSceneStyle& style) {
  if (row.text.isEmpty() || row.size.width() <= 0.0) return;
  if (!row.visible) return;
  if (row.fontPixelSize == 0.0) return;  // font-size:0 -> text absent (no paint)
  const qreal fontSize = row.fontPixelSize >= 0.0 ? row.fontPixelSize : style.fontSize;
  const QString fontFamily = row.fontFamily.isEmpty() ? style.fontFamily : row.fontFamily;
  const qreal lineHeight = row.lineHeight >= 0.0 ? row.lineHeight : style.lineHeight;
  const QColor color = row.color.isValid() ? row.color : resolveColor(style.textColor);
  const QPointF center = nodeCenter + row.center;
  const QRectF rect(center.x() - row.size.width() / 2.0,
                    center.y() - row.size.height() / 2.0,
                    row.size.width(), row.size.height());
  painter.save();
  painter.setOpacity(painter.opacity() * row.opacity);
  painter.setClipRect(rect);
  flowchart::paintFlowLabel(painter, row.document, rect, fontFamily, fontSize, lineHeight,
                            color, true);
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

  // (1) Relationship edges + markers + labels. Drawn before nodes so node boxes
  // paint over line ends where they meet the box outline.
  for (const RequirementSceneEdge& edge : scene.edges) {
    const QRectF pathCull = edge.pathBounds.isValid() ? edge.pathBounds : scene.bounds;
    if (edge.visible && mermaidPrimitiveIsVisible(pathCull, options)) {
      QPen pen(resolveColor(edge.stroke), edge.strokeWidth);
      pen.setCapStyle(Qt::FlatCap);
      pen.setJoinStyle(Qt::MiterJoin);
      if (!edge.isContains) {
        // Dashed: stroke-dasharray: 10,7.
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern(scene::normalizedSvgDashPattern(
            {10.0, 7.0}, edge.strokeWidth));
      }
      painter.save();
      painter.setOpacity(painter.opacity() * edge.opacity * edge.strokeOpacity);
      painter.setPen(pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawPath(edgePath(edge));
      painter.restore();

      const QVector<QPointF> pts = edgePolyline(edge);
      const RequirementMarkerDefinition* marker = nullptr;
      for (const RequirementMarkerDefinition& candidate : scene.markers) {
        if ((edge.isContains && candidate.isContains) ||
            (!edge.isContains && !candidate.isContains)) {
          marker = &candidate;
          break;
        }
      }
      if (options.paintEdgeMarkers && marker && marker->visible &&
          pts.size() >= 2) {
        painter.save();
        painter.setOpacity(painter.opacity() * marker->opacity);
        const QColor markerStroke = resolveColor(marker->stroke);
        if (edge.isContains) {
          const QPointF startDir = pts.at(1) - pts.first();
          drawContainsMarker(painter, pts.first(), startDir, markerStroke);
        } else {
          const QPointF endDir = pts.last() - pts.at(pts.size() - 2);
          drawArrowMarker(painter, pts.last(), endDir, markerStroke);
        }
        painter.restore();
      }
    }

    // Centered edge label. div.labelBkg is the painted foreignObject background;
    // g.label and span.edgeLabel computed backgrounds remain separate state.
    if (edge.labelPosition && !edge.label.isEmpty()) {
      const QPointF labelCenter = *edge.labelPosition;
      const QRectF labelRect(
          labelCenter.x() - edge.labelSize.width() / 2.0,
          labelCenter.y() - edge.labelSize.height() / 2.0,
          edge.labelSize.width(), edge.labelSize.height());
      if (mermaidPrimitiveIsVisible(labelRect, options)) {
        const auto paintBackground = [&](const RequirementPaintedBackground& bg) {
          if (!bg.displayed) return;
          const QColor color = resolveColor(bg.color);
          if (!color.isValid() || color.alpha() == 0) return;
          painter.save();
          painter.setOpacity(painter.opacity() * bg.effectiveOpacity);
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawRect(labelRect);
          painter.restore();
        };
        paintBackground(edge.labelContainerBg);
        // Chrome reports span.edgeLabel's computed background-color, but the
        // inline span inside Requirement's SVG foreignObject does not emit a
        // painted background fragment. The browser PNG contains only the
        // div.labelBkg layer. Keep labelTextBg in the scene for computed-style
        // parity, but do not synthesize pixels the DOM renderer does not paint.
        if (edge.paintedSpanComputed.displayed) {
          painter.save();
          painter.setOpacity(painter.opacity() *
                             edge.paintedSpanComputed.effectiveOpacity);
          painter.setClipRect(labelRect);
          const QString labelFamily = requirementEffectiveFontFamily(
              edge.labelTextStyle, scene.style.fontFamily);
          const qreal labelSize = requirementEffectiveFontSize(
              edge.labelTextStyle, scene.style.fontSize);
          if (scene.style.htmlLabels) {
            const qreal labelHeight = edge.labelTextStyle.lineHeightPx >= 0.0
                ? edge.labelTextStyle.lineHeightPx : labelSize * 1.5;
            flowchart::paintFlowLabel(painter, edge.labelDocument, labelRect,
                                      labelFamily, labelSize, labelHeight,
                                      resolveColor(edge.labelColor), true);
          } else {
            // htmlLabels:false: createFormattedText positions the <text>
            // inside the ±2px background rect with rows advancing at the fixed
            // 1.1em dy from the font-cell top (CSS line-height is inert for
            // SVG tspans); the cell-height line height reproduces the
            // baseline = cell top + hhea ascent.
            const flowchart::FlowLabelFontMetrics font =
                flowchart::flowLabelFontBoundingMetrics(labelFamily, labelSize);
            flowchart::paintFlowLabel(painter, edge.labelDocument,
                                      labelRect.adjusted(2.0, 2.0, -2.0, -2.0),
                                      labelFamily, labelSize, font.height(),
                                      resolveColor(edge.labelColor), true);
          }
          painter.restore();
        }
      }
    }
  }

  // (2) Requirement box nodes.
  for (const RequirementSceneNode& node : scene.nodes) {
    if (!node.visible) continue;
    const QRectF box(node.center.x() - node.size.width() / 2.0,
                     node.center.y() - node.size.height() / 2.0,
                     node.size.width(), node.size.height());
    if (!mermaidPrimitiveIsVisible(box, options)) continue;
    // Resolved box paint (compileStyles last-wins over the theme base). The box
    // outline and the divider are INDEPENDENT: an unset-style stroke value
    // (none/inherit/invalid) hides the outline but the divider may still paint
    // in the theme color; only `none` (or stroke-width<=0) hides both.
    // fill:none paints no fill. Rounded rect (requirementBox uses roughjs
    // rectangle with roughness=0 → a plain rect; mermaid applies rx via the
    // label-container CSS; a small rounding matches).
    //
    // Qt dash units are pen-width multiples (a non-cosmetic pen); SVG/rough
    // dashes are in px. Scale by 1/strokeWidth so `stroke-dasharray:5` renders a
    // 5px period regardless of stroke-width.
    const qreal dashInv = node.strokeWidth > 0.0 ? 1.0 / node.strokeWidth : 1.0;
    const bool hasDash = node.dashArray.size() >= 2 &&
                         (node.dashArray.at(0) != 0.0 || node.dashArray.at(1) != 0.0);
    QPen boxPen;
    if (!node.outlineVisible) {
      boxPen = Qt::NoPen;  // outline hidden (stroke none/inherit/invalid or width<=0)
    } else {
      boxPen.setColor(resolveColor(node.outlineStroke));
      boxPen.setWidthF(node.strokeWidth);  // > 0: outlineVisible is false when width==0
      if (hasDash) {
        boxPen.setStyle(Qt::CustomDashLine);
        boxPen.setDashPattern({node.dashArray.at(0) * dashInv, node.dashArray.at(1) * dashInv});
      }
    }
    painter.save();
    painter.setOpacity(painter.opacity() * node.opacity);
    if (boxPen.style() != Qt::NoPen)
      boxPen.setColor(QColor::fromRgbF(boxPen.color().redF(),
                                      boxPen.color().greenF(),
                                      boxPen.color().blueF(),
                                      node.strokeOpacity));
    painter.setPen(boxPen);
    // Explicit if/else: a ternary here resolves Qt::NoBrush through the
    // QColor(Qt::GlobalColor) ctor (Qt::NoBrush == 0 == Qt::color0 -> black),
    // painting a black fill instead of none.
    if (node.fillNone)
      painter.setBrush(Qt::NoBrush);
    else
      painter.setBrush(QColor::fromRgbF(resolveColor(node.fill).redF(),
                                        resolveColor(node.fill).greenF(),
                                        resolveColor(node.fill).blueF(),
                                        node.fillOpacity));
    painter.drawRoundedRect(box, 5.0, 5.0);
    painter.restore();

    // Divider line under the name (only if body rows present). Its Y is
    // precomputed on the node (mermaid's body-top: top + typeHeight + nameHeight
    // + gap) — see buildRequirementScene. The divider is INDEPENDENT of the
    // outline: an inherit/invalid stroke hides the outline but the divider keeps
    // the theme color (dividerVisible stays true); only `none` or stroke-width<=0
    // hide the divider. Both share strokeWidth/dashArray.
    if (node.hasDivider) {
      const qreal divY = node.center.y() + node.dividerY;
      const QPointF p1(node.center.x() - node.size.width() / 2.0, divY);
      const QPointF p2(node.center.x() + node.size.width() / 2.0, divY);
      const RequirementPaintedPathStyle painted = node.dividerChildPaths.isEmpty()
          ? RequirementPaintedPathStyle{node.dividerStroke,
                                        node.dividerStrokeWidth,
                                        node.dividerStrokeOpacity,
                                        node.dividerRootVisible &&
                                            node.dividerVisible}
          : node.dividerChildPaths.first();
      QPen divPen;
      if (!painted.displayed || painted.stroke.trimmed().compare(
              QLatin1String("none"), Qt::CaseInsensitive) == 0 ||
          painted.strokeWidth <= 0.0) {
        divPen = Qt::NoPen;
      } else {
        divPen.setColor(resolveColor(painted.stroke));
        divPen.setWidthF(painted.strokeWidth);
        if (hasDash) {
          divPen.setStyle(Qt::CustomDashLine);
          divPen.setDashPattern({node.dashArray.at(0) * dashInv, node.dashArray.at(1) * dashInv});
        }
      }
      painter.save();
      painter.setOpacity(painter.opacity() * painted.effectiveStrokeOpacity);
      painter.setPen(divPen);
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(p1, p2);
      painter.restore();
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
