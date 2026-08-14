#include "mermaid/mindmap/MindmapScenePainter.h"

#include "mermaid/mindmap/MindmapScene.h"
#include "mermaid/rough/RoughPaint.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"

#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::mindmap {
namespace {

color::SvgPaint rootPaint(const QString& value) {
  if (value.trimmed().isEmpty()) return {false, QColor(Qt::black)};
  return color::resolveSvgPaint(value, color::SvgPaintKind::Fill, QColor(Qt::black));
}

QBrush fillBrush(const QString& value, const color::SvgPaint& root) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Fill, root.none ? QColor(Qt::black) : root.color);
  return paint.none ? QBrush(Qt::NoBrush) : QBrush(paint.color);
}

QPen strokePen(const QString& value, qreal width, const color::SvgPaint& root) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Stroke, root.none ? QColor(Qt::black) : root.color);
  if (paint.none || !(width > 0.0) || !std::isfinite(width)) return QPen(Qt::NoPen);
  QPen pen(paint.color, width); pen.setCapStyle(Qt::FlatCap); pen.setJoinStyle(Qt::MiterJoin);
  return pen;
}

void paintShadow(QPainter& painter, const QPainterPath& path) {
  QColor c(0,0,0); c.setAlphaF(.06);
  painter.save(); painter.translate(2,2); painter.setPen(Qt::NoPen);
  painter.setBrush(c); painter.drawPath(path); painter.restore();
}

}  // namespace

void paintMindmapScene(const MindmapScene& scene, QPainter& painter,
                       const MermaidPaintOptions& options) {
  const color::SvgPaint root = rootPaint(scene.style.textColor);
  for (const MindmapEdgeGeometry& edge : scene.edges) {
    const QPainterPath path = edge.handDrawn && !edge.roughDrawable.sets.isEmpty()
        ? rough::toPainterPath(edge.roughDrawable.sets.first())
        : scene::parseSvgPath(edge.path);
    if (!mermaidPrimitiveIsVisible(path.controlPointRect(), options)) continue;
    const color::SvgPaint stroke = color::resolveSvgPaint(
        edge.stroke, color::SvgPaintKind::Stroke,
        root.none ? QColor(Qt::black) : root.color);
    if (stroke.none || !(edge.strokeWidth > 0.0)) continue;
    if (edge.handDrawn) {
      rough::drawRoughDrawable(painter, edge.roughDrawable, Qt::NoBrush,
                               QPen(stroke.color, edge.strokeWidth), Qt::NoPen);
    } else {
      QPen pen(stroke.color, edge.strokeWidth); pen.setCapStyle(Qt::FlatCap);
      pen.setJoinStyle(Qt::MiterJoin); painter.setPen(pen); painter.setBrush(Qt::NoBrush);
      painter.drawPath(path);
    }
  }

  for (const MindmapNodeGeometry& node : scene.nodes) {
    const QRectF total = node.paintedBounds.united(node.label.bounds).translated(node.center);
    if (!mermaidPrimitiveIsVisible(total, options)) continue;
    painter.save(); painter.translate(node.center);
    const QBrush fill = fillBrush(node.fill, root);
    const QPen stroke = strokePen(node.stroke, node.strokeWidth, root);
    if (node.dropShadow && node.shapeVisible) paintShadow(painter, node.shapePath);
    if (node.handDrawn) {
      const QColor roughStroke = color::toQColor(node.roughDrawable.options.stroke);
      const QColor roughFill = color::toQColor(node.roughDrawable.options.fill);
      rough::drawRoughDrawable(
          painter, node.roughDrawable, Qt::NoBrush,
          QPen(roughStroke, node.roughDrawable.options.strokeWidth),
          QPen(roughFill, node.roughDrawable.options.fillWeight));
    } else {
      painter.setPen(stroke);
      if (node.gradient) {
        QLinearGradient gradient(node.localBounds.left(), 0,
                                 node.localBounds.right(), 0);
        gradient.setColorAt(0, color::toQColor(scene.style.gradientStart));
        gradient.setColorAt(1, color::toQColor(scene.style.gradientStop));
        QPen gradientPen(QBrush(gradient), node.strokeWidth);
        gradientPen.setCapStyle(Qt::FlatCap);
        gradientPen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(gradientPen);
      }
      painter.setBrush(fill);
      if (node.shapeVisible) painter.drawPath(node.shapePath);
    }
    if (node.bottomLine && node.bottomLineWidth > 0.0) {
      painter.setPen(strokePen(node.bottomLineStroke,node.bottomLineWidth,root));
      painter.setBrush(Qt::NoBrush);
      painter.drawLine(QPointF(node.localBounds.left(),node.localBounds.bottom()),
                       QPointF(node.localBounds.right(),node.localBounds.bottom()));
    }
    if (!node.label.source.isEmpty() && node.label.fontSize > 0.0) {
      const color::SvgPaint text = color::resolveSvgPaint(
          node.label.fill,color::SvgPaintKind::Text,
          root.none?QColor(Qt::black):root.color);
      if (!text.none) {
        flowchart::paintFlowLabel(painter,node.label.document,node.label.bounds,
                                  node.label.fontFamily,node.label.fontSize,
                                  scene.config.htmlLabels?node.label.fontSize*1.5:
                                      node.label.fontSize*1.1,
                                  text.color,false);
        // FlowLabel's glyph runs use the inherited color. HTML anchors are
        // genuine nested CSS runs, so repaint just those spans with the UA
        // anchor paint and underline; their measured bounds were produced by
        // the same font/advance chain in buildMindmapScene.
        if (!node.anchors.isEmpty()) {
          QFont anchorFont = flowchart::makeFlowLabelFont(
              node.label.fontFamily, node.label.fontSize);
          anchorFont.setUnderline(true);
          painter.setFont(anchorFont);
          painter.setPen(QColor(QStringLiteral("#0000ee")));
          const QFontMetricsF metrics(anchorFont);
          for (const MindmapAnchorGeometry& anchor : node.anchors) {
            const qreal baseline = anchor.bounds.top()
                + (anchor.bounds.height() - metrics.height()) / 2.0
                + metrics.ascent();
            painter.drawText(QPointF(anchor.bounds.left(), baseline), anchor.label);
          }
        }
      }
    }
    painter.restore();
  }
}

}  // namespace muffin::mermaid::mindmap
