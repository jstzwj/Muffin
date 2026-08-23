// erDiagram scene painter. Mirrors the rendering idiom of
// src/mermaid/classdiagram/ClassScenePainter.cpp and
// src/mermaid/state/StateScenePainter.cpp: Antialiasing on, setPen/setBrush,
// mermaidPrimitiveIsVisible culling per primitive, a DPR-aware
// renderXToImage helper. Per IMPLEMENTATION_SPEC.md §5–§7 this module has no
// tr() calls; to honour the project lupdate convention the file defines its
// functions with fully-qualified names and keeps helpers in an anonymous
// namespace at file scope rather than wrapping a `namespace muffin {}` block.

#include "mermaid/erdiagram/ErScenePainter.h"

#include "mermaid/erdiagram/ErDiagram.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/scene/EdgeMarkerPaint.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/scene/SvgStroke.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace er = muffin::mermaid::er;
namespace flowchart = muffin::mermaid::flowchart;
namespace scene = muffin::mermaid::scene;

namespace {


QColor resolveColor(const QString& value) {
  return muffin::mermaid::color::toQColor(value);
}

// Crow's-foot marker geometry (IMPLEMENTATION_SPEC.md §5). `refX`/`refY` is the
// anchor placed at the endpoint; `path` is the exact upstream SVG `d` string;
// `hasCircle`/`circleCx`/`circleCy`/`circleR` describe the optional white-filled
// zero marker. `start` selects the entityA (Start) vs entityB (End) variant.
struct ErMarkerDef {
  qreal refX = 0.0;
  qreal refY = 0.0;
  QString path;
  bool hasCircle = false;
  qreal circleCx = 0.0;
  qreal circleCy = 0.0;
  qreal circleR = 0.0;
};

ErMarkerDef erMarkerDef(er::ErCardinality card, bool start) {
  using C = er::ErCardinality;
  switch (card) {
    case C::ExactlyOne:
      // box 18x18, two perpendicular ticks.
      return start
          ? ErMarkerDef{0.0, 9.0,
                        QStringLiteral("M9,0 L9,18 M15,0 L15,18")}
          : ErMarkerDef{18.0, 9.0,
                        QStringLiteral("M3,0 L3,18 M9,0 L9,18")};
    case C::ZeroOrOne:
      // box 30x18, one tick + white circle.
      return start
          ? ErMarkerDef{0.0, 9.0, QStringLiteral("M9,0 L9,18"),
                        true, 21.0, 9.0, 6.0}
          : ErMarkerDef{30.0, 9.0, QStringLiteral("M21,0 L21,18"),
                        true, 9.0, 9.0, 6.0};
    case C::OneOrMore:
      // box 45x36, quadratic crow's-foot leaf + one tick.
      return start
          ? ErMarkerDef{18.0, 18.0,
                        QStringLiteral("M0,18 Q18,0 36,18 Q18,36 0,18 M42,9 L42,27")}
          : ErMarkerDef{27.0, 18.0,
                        QStringLiteral("M3,9 L3,27 M9,18 Q27,0 45,18 Q27,36 9,18")};
    case C::ZeroOrMore:
      // box 57x36, quadratic crow's-foot leaf + white circle.
      return start
          ? ErMarkerDef{18.0, 18.0,
                        QStringLiteral("M0,18 Q18,0 36,18 Q18,36 0,18"),
                        true, 48.0, 18.0, 6.0}
          : ErMarkerDef{39.0, 18.0,
                        QStringLiteral("M21,18 Q39,0 57,18 Q39,36 21,18"),
                        true, 9.0, 18.0, 6.0};
  }
  return {};
}

// Draws a crow's-foot marker at `endpoint` rotated so its local +X axis aligns
// with `segDir` (the direction from this entity toward the opposite entity along
// the touching segment — see IMPLEMENTATION_SPEC.md §5.5). The local
// (refX, refY) anchor lands on the endpoint. The optional zero circle is drawn
// first with a white fill so the tick/fork stroke renders on top of it.
void drawErMarker(QPainter& painter, er::ErCardinality card, bool start,
                  const QPointF& endpoint, const QPointF& segDir,
                  const QColor& stroke, qreal strokeWidth) {
  const ErMarkerDef def = erMarkerDef(card, start);
  if (def.path.isEmpty() && !def.hasCircle) return;
  painter.save();
  scene::applyMarkerTransform(painter, endpoint, scene::tangentAngleDeg(segDir),
                              def.refX, def.refY);
  painter.setPen(QPen(stroke, strokeWidth));
  if (def.hasCircle) {
    painter.setBrush(QColor(Qt::white));
    painter.drawEllipse(QPointF(def.circleCx, def.circleCy),
                        def.circleR, def.circleR);
  }
  painter.setBrush(Qt::NoBrush);
  if (!def.path.isEmpty()) painter.drawPath(scene::parseSvgPath(def.path));
  painter.restore();
}

// NOTE: unlike requirement's edgePath, this deliberately draws only the FIRST segment when
// `points` is empty (historical er behavior); the shared stitchEdgePolyline (used for marker
// tangents) would span all segments.
QPainterPath edgePath(const er::ErSceneRelationship& rel) {
  if (!rel.path.isEmpty()) return scene::parseSvgPath(rel.path);
  const QVector<QPointF> points = !rel.points.isEmpty()
      ? rel.points
      : (rel.segments.isEmpty() ? QVector<QPointF>{} : rel.segments.first());
  QPainterPath path;
  if (points.isEmpty()) return path;
  path.moveTo(points.first());
  for (qsizetype i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
  return path;
}

// Continuous polyline for marker tangent estimation (shared stitching).
QVector<QPointF> edgePolyline(const er::ErSceneRelationship& rel) {
  return scene::stitchEdgePolyline(rel.points, rel.segments);
}

void paintLabel(QPainter& painter, const flowchart::FlowLabelDocument& document,
                const QRectF& rect, const er::ErSceneStyle& style,
                const QColor& textColor) {
  if (document.text.isEmpty()) return;
  painter.save();
  painter.setClipRect(rect);
  flowchart::paintFlowLabel(painter, document, rect, style.fontFamily,
                            style.fontSize, style.lineHeight, textColor, true);
  painter.restore();
}

// Minimal role placement (IMPLEMENTATION_SPEC.md §7 / §9): centered text offset
// perpendicular to the touching segment so the role sits beside the crow's foot
// without overlapping the edge line.
void paintRole(QPainter& painter, const QString& role, const QPointF& endpoint,
               const QPointF& segDir, const er::ErSceneStyle& style) {
  if (role.isEmpty()) return;
  const qreal len = std::hypot(segDir.x(), segDir.y());
  if (len < 1e-6) return;
  const QPointF unit(segDir.x() / len, segDir.y() / len);
  const QPointF perp(-unit.y(), unit.x());
  const QPointF anchor = endpoint + perp * 14.0 - unit * 6.0;
  const flowchart::FlowLabelDocument document =
      flowchart::parseFlowLabel(role, QStringLiteral("text"));
  const QSizeF size = flowchart::measureFlowLabel(
      document, style.fontFamily, style.fontSize, style.lineHeight);
  const QRectF rect(anchor.x() - size.width() / 2.0,
                    anchor.y() - size.height() / 2.0,
                    size.width(), size.height());
  paintLabel(painter, document, rect, style, resolveColor(style.relationshipLabelColor));
}

}  // namespace

void muffin::mermaid::er::paintErScene(const ErScene& scene, QPainter& painter,
                                       const MermaidPaintOptions& options) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  const QColor relationshipColor = resolveColor(scene.style.relationshipColor);

  // (1) Relationship paths + crow's-foot markers + roles. Drawn before entities
  // so the table boxes paint over the line ends where they meet the entity.
  for (const ErSceneRelationship& rel : scene.relationships) {
    const QRectF pathCull = rel.pathBounds.isValid()
        ? rel.pathBounds : scene.bounds;
    if (!mermaidPrimitiveIsVisible(pathCull, options)) continue;

    QPen pen(relationshipColor, scene.style.relationshipStrokeWidth);
    if (!rel.identifying) {
      // Chrome computes stroke-dasharray "8px, 8px" on .edge-pattern-dashed:
      // the er stylesheet's own 8,8 rule overrides the common sheet's `3`
      // (equal specificity, later rule wins) — er-geometry.json pins the
      // probed value. QPen entries are pen-width multiples, so normalize.
      pen.setDashPattern(scene::parseAndNormalizeSvgDashPattern(
          QStringLiteral("8,8"), scene.style.relationshipStrokeWidth));
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(edgePath(rel));

    const QVector<QPointF> pts = edgePolyline(rel);
    if (pts.size() >= 2) {
      const QPointF startDir = pts.at(1) - pts.first();
      const QPointF endDir = pts.at(pts.size() - 2) - pts.last();
      if (options.paintEdgeMarkers) {
        drawErMarker(painter, rel.cardA, true, pts.first(), startDir,
                     relationshipColor, scene.style.relationshipStrokeWidth);
        drawErMarker(painter, rel.cardB, false, pts.last(), endDir,
                     relationshipColor, scene.style.relationshipStrokeWidth);
      }
      paintRole(painter, rel.roleA, pts.first(), startDir, scene.style);
      paintRole(painter, rel.roleB, pts.last(), endDir, scene.style);
    }
  }

  // (2) Relationship labels, painted after the lines so the labelBackground
  // covers any edge crossing through the label box.
  for (const ErSceneRelationship& rel : scene.relationships) {
    if (rel.label.isEmpty() || !rel.labelPosition) continue;
    const QSizeF size = rel.labelSize.isValid()
        ? rel.labelSize
        : flowchart::measureFlowLabel(rel.labelDocument, scene.style.fontFamily,
                                      scene.style.fontSize, scene.style.lineHeight);
    const QRectF rect(rel.labelPosition->x() - size.width() / 2.0,
                      rel.labelPosition->y() - size.height() / 2.0,
                      size.width(), size.height());
    const QRectF labelCull = rel.labelBounds.isValid() ? rel.labelBounds : rect;
    if (!mermaidPrimitiveIsVisible(labelCull, options)) continue;
    painter.fillRect(rect, resolveColor(scene.style.labelBackground));
    paintLabel(painter, rel.labelDocument, rect, scene.style,
               resolveColor(scene.style.relationshipLabelColor));
  }

  // (3) Entity tables, drawn last.
  for (const ErSceneEntity& entity : scene.entities) {
    if (!mermaidPrimitiveIsVisible(entity.bounds, options)) continue;
    const QColor fill = resolveColor(entity.fill.isEmpty() ? scene.style.entityFill : entity.fill);
    const QColor stroke = resolveColor(entity.stroke.isEmpty() ? scene.style.entityStroke : entity.stroke);
    painter.setPen(QPen(stroke, scene.style.strokeWidth));
    painter.setBrush(fill);
    painter.drawRoundedRect(entity.bounds, 6.0, 6.0);

    // Thin divider beneath the header and between attribute rows. The top of
    // attributeRects[0] coincides with the header bottom, so one pass yields
    // both the header divider and the inter-row dividers.
    if (!entity.attributeRects.isEmpty()) {
      painter.setPen(QPen(stroke, 1.0));
      for (const QRectF& row : entity.attributeRects)
        painter.drawLine(row.topLeft(), row.topRight());
    }

    // Entity name centered in the header band.
    paintLabel(painter, entity.nameDocument, entity.headerRect, scene.style,
               resolveColor(scene.style.entityTitle1));

    // Attribute rows, left-aligned. paintFlowLabel centers horizontally within
    // the supplied rect, so the rect is sized to the measured text width and
    // offset by a small left padding to produce a left-aligned table cell.
    constexpr qreal kAttributeHPad = 8.0;
    for (qsizetype i = 0; i < entity.attributeDocuments.size(); ++i) {
      const flowchart::FlowLabelDocument& document = entity.attributeDocuments.at(i);
      if (document.text.isEmpty()) continue;
      const QRectF row = entity.attributeRects.value(i, entity.bounds);
      const QSizeF measured = flowchart::measureFlowLabel(
          document, scene.style.fontFamily, scene.style.fontSize,
          scene.style.lineHeight);
      const QRectF textRect(row.left() + kAttributeHPad, row.top(),
                            measured.width(), row.height());
      paintLabel(painter, document, textRect, scene.style,
                 resolveColor(scene.style.attributeColor));
    }
  }
}

QImage muffin::mermaid::er::renderErSceneToImage(const ErScene& scene,
                                                 qreal dpr, qreal padding) {
  const qreal width = std::max(1.0, scene.bounds.width() + 2.0 * padding);
  const qreal height = std::max(1.0, scene.bounds.height() + 2.0 * padding);
  QImage image(qCeil(width * dpr), qCeil(height * dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.scale(dpr, dpr);
  painter.translate(padding - scene.bounds.left(), padding - scene.bounds.top());
  paintErScene(scene, painter);
  painter.end();
  image.setDevicePixelRatio(dpr);
  return image;
}

void muffin::mermaid::er::ErScene::paint(QPainter& painter,
                                         const MermaidPaintOptions& options) const {
  paintErScene(*this, painter, options);
}
