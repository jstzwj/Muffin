#include "mermaid/scene/FlowScenePainter.h"

#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/scene/FlowMarkers.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRegularExpression>
#include <QTransform>

#include <cmath>

namespace muffin::mermaid::flowscene {
namespace {

QColor qcolor(const QString& s) { return color::toQColor(s); }

qreal pxSize(const QString& s) {
  static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)?)"));
  const QRegularExpressionMatch m = re.match(s);
  return m.hasMatch() ? m.captured(1).toDouble() : 16.0;
}

// SVG path `d` -> QPainterPath, also extracting the path endpoints + tangent
// directions (for marker orientation). Handles M/L/C/Q/A/Z (abs + rel) — the
// command set mermaid's d3 curve generators emit.
struct ParsedPath {
  QPainterPath path;
  QPointF startPoint, endPoint, startTangent, endTangent;
  bool hasStart = false, hasEnd = false;
};

ParsedPath parsePath(const QString& d) {
  ParsedPath p;
  static const QRegularExpression tok(
      QStringLiteral("[MmLlCcQqAaZz]|-?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?"));
  auto it = tok.globalMatch(d);
  QVector<QString> tokens;
  while (it.hasNext()) tokens.append(it.next().captured());
  QPointF cur, start;
  QPointF lastCtrl;
  char cmd = 0;
  for (int i = 0; i < tokens.size(); ++i) {
    const QString& t = tokens[i];
    if (t[0].isLetter()) { cmd = t[0].toLatin1(); continue; }
    auto point = [&](int& j, char c) -> QPointF {
      const qreal x = tokens.at(j++).toDouble();
      const qreal y = tokens.at(j++).toDouble();
      const bool rel = c >= 'a';
      return rel ? QPointF(cur.x() + x, cur.y() + y) : QPointF(x, y);
    };
    int j = i;
    if (cmd == 'M' || cmd == 'm') {
      const QPointF pt = point(j, cmd);
      if (!p.hasStart) { p.startPoint = pt; p.hasStart = true; start = pt; }
      cur = pt; p.path.moveTo(pt); cmd = cmd == 'M' ? 'L' : 'l';
    } else if (cmd == 'L' || cmd == 'l') {
      const QPointF pt = point(j, cmd);
      if (!p.hasStart) { p.startPoint = cur; p.hasStart = true; }
      p.startTangent = p.startTangent.isNull() ? (pt - cur) : p.startTangent;
      p.endTangent = pt - cur; p.endPoint = pt; p.hasEnd = true;
      cur = pt; p.path.lineTo(pt);
    } else if (cmd == 'C' || cmd == 'c') {
      const QPointF c1 = point(j, cmd), c2 = point(j, cmd), e = point(j, cmd);
      if (!p.hasStart) { p.startPoint = cur; p.hasStart = true; }
      if (p.startTangent.isNull()) p.startTangent = c1 - cur;
      p.endTangent = e - c2; p.endPoint = e; p.hasEnd = true;
      lastCtrl = c2; cur = e; p.path.cubicTo(c1, c2, e);
    } else if (cmd == 'Q' || cmd == 'q') {
      const QPointF c1 = point(j, cmd), e = point(j, cmd);
      if (!p.hasStart) { p.startPoint = cur; p.hasStart = true; }
      if (p.startTangent.isNull()) p.startTangent = c1 - cur;
      p.endTangent = e - c1; p.endPoint = e; p.hasEnd = true;
      lastCtrl = c1; cur = e; p.path.quadTo(c1, e);
    } else if (cmd == 'A' || cmd == 'a') {
      // rx ry xrot large sweep dx dy — approximate with a line (rare in flowchart edges).
      j += 5;  // rx ry xrot large sweep
      const QPointF e = point(j, cmd);
      if (!p.hasStart) { p.startPoint = cur; p.hasStart = true; }
      p.endTangent = e - cur; p.endPoint = e; p.hasEnd = true;
      cur = e; p.path.lineTo(e);
    }
    i = j - 1;
  }
  if (p.hasStart && p.startTangent.isNull() && p.hasEnd) p.startTangent = p.endTangent;
  return p;
}

void drawMarker(QPainter& painter, const QString& type, const QPointF& at,
                const QPointF& tangent, const QColor& color) {
  if (type.isEmpty() || tangent.isNull()) return;
  const MarkerGeometry g = markerGeometry(type);
  if (g.tag.isEmpty()) return;
  const qreal angle = std::atan2(tangent.y(), tangent.x()) * 180.0 / M_PI;
  painter.save();
  painter.translate(at);
  painter.rotate(angle);
  painter.translate(-g.refX, -g.refY);
  QPen pen(color); pen.setWidthF(1.0); pen.setCapStyle(Qt::RoundCap);
  painter.setPen(pen);
  if (g.tag == QLatin1String("path") && type.contains(QLatin1String("point"))) {
    painter.setBrush(color); painter.setPen(Qt::NoPen);
    QPainterPath mp; mp.moveTo(0, 0); mp.lineTo(g.markerWidth, g.markerHeight / 2.0);
    mp.lineTo(0, g.markerHeight); mp.closeSubpath();
    // Use the actual pathData via a sub-parse if simple; the triangle above matches pointEnd.
    painter.drawPath(mp);
  } else if (g.tag == QLatin1String("circle")) {
    painter.setBrush(Qt::NoBrush); painter.setPen(pen);
    painter.drawEllipse(QPointF(g.cx, g.cy), g.r, g.r);
  } else if (g.tag == QLatin1String("path")) {
    // cross: stroke the X
    painter.setBrush(Qt::NoBrush); painter.setPen(pen);
    QPainterPath mp; mp.moveTo(1, 1); mp.lineTo(14, 14); mp.moveTo(1, 14); mp.lineTo(14, 1);
    painter.drawPath(mp);
  }
  painter.restore();
}

// The resolved QFont for a label, shared by drawLabel and the edge-label box
// measurement so the background rect is sized to the SAME metrics that draw the
// glyphs (matches Chrome's foreignObject label box instead of a char-count
// heuristic).
QFont labelFont(const FlowSceneLabel& label, const QString& fontFamily) {
  QFont font(label.fontFamily.isEmpty() ? fontFamily : label.fontFamily);
  font.setPixelSize(static_cast<int>(std::round(pxSize(label.fontSize.isEmpty() ? QStringLiteral("16px") : label.fontSize))));
  if (label.fontWeight == QLatin1String("bold")) font.setBold(true);
  return font;
}

void drawLabel(QPainter& painter, const FlowSceneLabel& label, const QRectF& rect,
               const QString& fontFamily, bool center, PaintMode mode) {
  if (label.text.isEmpty()) return;
  const QFont font = labelFont(label, fontFamily);
  const flowchart::FlowLabelDocument document = flowchart::parseFlowLabel(
      label.text, label.labelType, label.mathEnabled);
  const qreal lineHeight = (font.pixelSize() > 0 ? font.pixelSize() : 16.0) * 1.5;
  const QColor color = mode == PaintMode::CategoryMask
                           ? QColor(kCatText)
                           : qcolor(label.color.isEmpty() ? QStringLiteral("#333333")
                                                          : label.color);
  flowchart::paintFlowLabel(painter, document, rect, font.family(), font.pixelSize(),
                            lineHeight, color, center);
}

}  // namespace

void paintFlowScene(const FlowScene& scene, QPainter& painter, const QString& fontFamily, PaintMode mode) {
  // mermaid's flowchart SVG has no background rect (transparent where there is
  // no shape); the painter matches that. scene.background is kept in the scene
  // for the JSON golden + future editor use, but is NOT painted here.
  if (mode == PaintMode::CategoryMask) {
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, false);
  }
  (void)scene.background;

  // In CategoryMask mode every element paints its reserved category colour
  // (paletteColor) instead of its resolved colour, but the geometry + draw order
  // are identical to Color mode — so the mask is the painter's own self-report.
  const auto paletteColor = [&](const QString& realColor, QRgb catColor) -> QColor {
    return mode == PaintMode::Color ? qcolor(realColor) : QColor(catColor);
  };

  // Clusters (drawn first).
  for (const FlowSceneCluster& c : scene.clusters) {
    const QRectF r(c.cx - c.width / 2.0, c.cy - c.height / 2.0, c.width, c.height);
    painter.setBrush(paletteColor(c.fill, kCatCluster));
    QPen pen(paletteColor(c.stroke, kCatCluster)); pen.setWidthF(pxSize(c.strokeWidth));
    painter.setPen(pen);
    painter.drawRoundedRect(r, 0, 0);
    if (!c.label.text.isEmpty()) {
      QRectF lr(r.left() + 4, r.top() + 2, c.width - 8, 20);
      drawLabel(painter, c.label, lr, fontFamily, false, mode);
    }
  }

  // Edges (drawn second).
  for (const FlowSceneEdge& e : scene.edges) {
    const ParsedPath pp = parsePath(e.path);
    QPen pen(paletteColor(e.stroke, kCatEdge)); pen.setWidthF(pxSize(e.strokeWidth));
    if (!e.strokeDasharray.isEmpty()) {
      QStringList parts = e.strokeDasharray.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
      QVector<qreal> dash; for (const QString& s : parts) dash.append(s.toDouble());
      // Qt requires an even-length pattern (dash/gap pairs). A mermaid dotted edge
      // has dasharray "2" (one value); duplicate an odd-length pattern so Qt
      // doesn't warn + fall back to a solid line.
      if (dash.size() % 2 != 0) dash += dash;
      pen.setDashPattern(dash);
    }
    pen.setCapStyle(Qt::RoundCap); pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen); painter.setBrush(Qt::NoBrush);
    painter.drawPath(pp.path);
    // Markers.
    const QColor mc = paletteColor(e.stroke, kCatEdge);
    if (!e.markerEnd.isEmpty()) drawMarker(painter, e.markerEnd, pp.endPoint, pp.endTangent, mc);
    if (!e.markerStart.isEmpty()) {
      QPointF t = pp.startTangent;
      drawMarker(painter, e.markerStart, pp.startPoint, QPointF(-t.x(), -t.y()), mc);  // start marker points backward
    }
    // Edge label.
    if (!e.label.text.isEmpty()) {
      const QFont font = labelFont(e.label, fontFamily);
      flowchart::FlowEdge semanticLabel;
      semanticLabel.text = e.label.text;
      semanticLabel.labelType = e.label.labelType;
      flowchart::FlowTextOptions textOptions;
      textOptions.fontFamily = font.family();
      textOptions.fontPixelSize = font.pixelSize() > 0 ? font.pixelSize() : 16.0;
      textOptions.lineHeight = textOptions.fontPixelSize * 1.5;
      const QSizeF measured = flowchart::measureFlowchartEdgeLabel(semanticLabel, textOptions);
      const QRectF lr(e.label.x - measured.width() / 2.0,
                      e.label.y - measured.height() / 2.0,
                      measured.width(), measured.height());
      painter.setPen(Qt::NoPen); painter.setBrush(paletteColor(e.label.background, kCatEdgeLabelBg));
      painter.drawRoundedRect(lr, 2, 2);
      drawLabel(painter, e.label, lr, fontFamily, true, mode);
    }
  }

  // Nodes (drawn last).
  for (const FlowSceneNode& n : scene.nodes) {
    const QRectF r(n.cx - n.width / 2.0, n.cy - n.height / 2.0, n.width, n.height);
    painter.setBrush(paletteColor(n.fill, kCatNode));
    QPen pen(paletteColor(n.stroke, kCatNode)); pen.setWidthF(pxSize(n.strokeWidth));
    painter.setPen(pen);
    if (n.shapeKind == QLatin1String("roundedRect")) {
      painter.drawRoundedRect(r, n.cornerRadius, n.cornerRadius);
    } else if (n.shapeKind == QLatin1String("ellipse")) {
      painter.drawEllipse(r);
    } else if (n.shapeKind == QLatin1String("polygon") && !n.points.isEmpty()) {
      QPolygonF poly; for (const QPointF& pt : n.points) poly.append(QPointF(n.cx + pt.x(), n.cy + pt.y()));
      painter.drawPolygon(poly);
    } else if (n.shapeKind == QLatin1String("cylinder")) {
      // Vertical database cylinder: top + bottom ellipse caps joined by the two
      // sides. Mirrors the golden-verified shape render in MermaidFlowchartLayoutTest
      // so the painter's silhouette matches the L2 shape geometry exactly.
      const QRectF topEllipse(r.left(), r.top(), r.width(), n.radiusY * 2.0);
      const QRectF bottomEllipse(r.left(), r.bottom() - n.radiusY * 2.0, r.width(), n.radiusY * 2.0);
      QPainterPath cyl;
      cyl.moveTo(r.left(), r.top() + n.radiusY);
      cyl.arcTo(topEllipse, 180.0, -180.0);
      cyl.lineTo(r.right(), r.bottom() - n.radiusY);
      cyl.arcTo(bottomEllipse, 0.0, -180.0);
      cyl.closeSubpath();
      painter.drawPath(cyl);
    } else if (n.shapeKind == QLatin1String("horizontalCylinder")) {
      // Tilted cylinder: the shared two-subpath WindingFill path (arcs sampled in
      // SVG traversal order), identical to the L2 silhouette generator.
      painter.drawPath(flowchart::flowShapeHorizontalCylinderPath(r, n.radiusX, n.radiusY));
    } else {
      painter.drawRect(r);
    }
    // Node label centered.
    if (!n.label.text.isEmpty()) drawLabel(painter, n.label, r, fontFamily, true, mode);
  }
}

QImage renderFlowSceneToImage(const FlowScene& scene, qreal dpr, qreal padding, const QString& fontFamily) {
  const qreal w = scene.bounds.width() + padding * 2.0;
  const qreal h = scene.bounds.height() + padding * 2.0;
  QImage image(static_cast<int>(std::ceil(w * dpr)), static_cast<int>(std::ceil(h * dpr)),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.scale(dpr, dpr);
  painter.translate(padding - scene.bounds.left(), padding - scene.bounds.top());
  paintFlowScene(scene, painter, fontFamily);
  painter.end();
  return image;
}

}  // namespace muffin::mermaid::flowscene
