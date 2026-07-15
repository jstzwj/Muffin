#include "mermaid/scene/FlowScenePainter.h"
#include "mermaid/MermaidFontRegistry.h"

#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/flowchart/FlowchartShapes.h"
#include "mermaid/scene/FlowMarkers.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
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
                const QPointF& tangent, const QColor& color, bool useMargin) {
  if (type.isEmpty() || tangent.isNull()) return;
  const MarkerGeometry g = markerGeometry(type, useMargin);
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
  const QString family = label.fontFamily.isEmpty() ? fontFamily : label.fontFamily;
  QFont font(family);
  MermaidFontRegistry::configureFont(font, family);
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

void drawNodeShape(QPainter& painter, const FlowSceneNode& n, QRectF r,
                   const QPointF& offset = {}) {
  r.translate(offset);
  if (n.shapeType == QLatin1String("text")) return;
  const auto sineEdge = [](qreal x1, qreal x2, qreal y, qreal amplitude,
                           qreal cycles, QPainterPath& path) {
    constexpr int steps = 50;
    for (int i = 1; i <= steps; ++i) {
      const qreal t = qreal(i) / steps;
      const qreal x = x1 + (x2 - x1) * t;
      path.lineTo(x, y + amplitude * std::sin(2.0 * M_PI * cycles * t));
    }
  };
  if (n.shapeType == QLatin1String("multi_document")) {
    const qreal o = 10.0;
    const auto documentLayer = [&](const QRectF& layer) {
      const qreal internalH = (layer.height() - o) / 1.25;
      const qreal amplitude = internalH / 4.0;
      const qreal waveY = layer.bottom() - amplitude;
      QPainterPath path(QPointF(layer.left(), waveY));
      sineEdge(layer.left(), layer.right(), waveY, amplitude, 0.8, path);
      path.lineTo(layer.right(), layer.top());
      path.lineTo(layer.left(), layer.top());
      path.closeSubpath();
      painter.drawPath(path);
    };
    documentLayer(r.adjusted(2.0 * o, 0.0, 0.0, -2.0 * o));
    documentLayer(r.adjusted(o, o, -o, -o));
    documentLayer(r.adjusted(0.0, 2.0 * o, -2.0 * o, 0.0));
  } else if (n.shapeType == QLatin1String("document") ||
      n.shapeType == QLatin1String("lined_document") ||
      n.shapeType == QLatin1String("tagged_document")) {
    const qreal internalH = r.height() / (n.shapeType == QLatin1String("tagged_document") ? 1.25 : 1.5);
    const qreal amplitude = internalH / (n.shapeType == QLatin1String("tagged_document") ? 8.0 : 4.0);
    const qreal top = r.top();
    const qreal waveY = r.bottom() - amplitude;
    QPainterPath path;
    path.moveTo(r.left(), waveY);
    sineEdge(r.left(), r.right(), waveY, amplitude, 0.8, path);
    path.lineTo(r.right(), top);
    path.lineTo(r.left(), top);
    path.closeSubpath();
    painter.drawPath(path);
    if (n.shapeType == QLatin1String("lined_document"))
      painter.drawLine(QPointF(r.left() + r.width() / 11.0, top),
                       QPointF(r.left() + r.width() / 11.0, waveY));
    if (n.shapeType == QLatin1String("tagged_document")) {
      const qreal tag = std::min(r.width(), internalH) * 0.22;
      QPainterPath fold;
      fold.moveTo(r.right() - tag, r.bottom() - amplitude * 0.2);
      fold.lineTo(r.right() - tag, r.bottom() - tag - amplitude * 0.2);
      fold.lineTo(r.right(), r.bottom() - amplitude * 0.2);
      painter.drawPath(fold);
    }
  } else if (n.shapeType == QLatin1String("flag")) {
    const qreal internalH = r.height() / 1.5;
    const qreal amplitude = internalH / 8.0;
    const qreal topWave = r.top() + amplitude;
    const qreal bottomWave = r.bottom() - amplitude;
    QPainterPath path;
    path.moveTo(r.left(), bottomWave);
    sineEdge(r.left(), r.right(), bottomWave, amplitude, 1.0, path);
    path.lineTo(r.right(), topWave);
    sineEdge(r.right(), r.left(), topWave, -amplitude, -1.0, path);
    path.closeSubpath();
    painter.drawPath(path);
  } else if (n.shapeType == QLatin1String("stacked_rect")) {
    const qreal o = 10.0;
    painter.drawRect(r.adjusted(2.0 * o, 0.0, 0.0, -2.0 * o));
    painter.drawRect(r.adjusted(o, o, -o, -o));
    painter.drawRect(r.adjusted(0.0, 2.0 * o, -2.0 * o, 0.0));
  } else if (n.shapeType == QLatin1String("tagged_rect")) {
    const qreal tag = r.height() * 0.2;
    QPainterPath path;
    path.moveTo(r.left(), r.top());
    path.lineTo(r.right() - tag, r.top());
    path.lineTo(r.right(), r.top() + tag);
    path.lineTo(r.right(), r.bottom());
    path.lineTo(r.left(), r.bottom());
    path.closeSubpath();
    painter.drawPath(path);
    painter.drawLine(QPointF(r.right() - tag, r.top()),
                     QPointF(r.right() - tag, r.top() + tag));
    painter.drawLine(QPointF(r.right() - tag, r.top() + tag),
                     QPointF(r.right(), r.top() + tag));
  } else if (n.shapeType == QLatin1String("brace_left") ||
             n.shapeType == QLatin1String("brace_right") ||
             n.shapeType == QLatin1String("braces")) {
    painter.save();
    painter.setBrush(Qt::NoBrush);
    const qreal mid = r.center().y();
    const auto brace = [&](qreal x, qreal direction) {
      const qreal reach = 8.0 * direction;
      QPainterPath path(QPointF(x, r.top()));
      path.cubicTo(x - reach, r.top(), x - reach, mid - 5.0, x, mid);
      path.cubicTo(x - reach, mid + 5.0, x - reach, r.bottom(), x, r.bottom());
      painter.drawPath(path);
    };
    if (n.shapeType == QLatin1String("braces")) brace(r.left() + 8.0, -1.0);
    brace(r.right() - 8.0, 1.0);
    painter.restore();
  } else if (n.shapeKind == QLatin1String("roundedRect")) {
    painter.drawRoundedRect(r, n.cornerRadius, n.cornerRadius);
  } else if (n.shapeKind == QLatin1String("ellipse")) {
    painter.drawEllipse(r);
    if (n.shapeType == QLatin1String("double_circle") ||
        n.shapeType == QLatin1String("framed_circle"))
      painter.drawEllipse(r.adjusted(4.0, 4.0, -4.0, -4.0));
    if (n.shapeType == QLatin1String("crossed_circle")) {
      painter.drawLine(r.topLeft(), r.bottomRight());
      painter.drawLine(r.topRight(), r.bottomLeft());
    }
  } else if (n.shapeKind == QLatin1String("polygon") && !n.points.isEmpty()) {
    QPolygonF poly;
    for (const QPointF& pt : n.points)
      poly.append(QPointF(n.cx + offset.x() + pt.x(), n.cy + offset.y() + pt.y()));
    painter.drawPolygon(poly);
  } else if (n.shapeKind == QLatin1String("cylinder")) {
    const QRectF topEllipse(r.left(), r.top(), r.width(), n.radiusY * 2.0);
    const QRectF bottomEllipse(r.left(), r.bottom() - n.radiusY * 2.0,
                               r.width(), n.radiusY * 2.0);
    QPainterPath cyl;
    cyl.moveTo(r.left(), r.top() + n.radiusY);
    cyl.arcTo(topEllipse, 180.0, -180.0);
    cyl.lineTo(r.right(), r.bottom() - n.radiusY);
    cyl.arcTo(bottomEllipse, 0.0, -180.0);
    cyl.closeSubpath();
    painter.drawPath(cyl);
    if (n.shapeType == QLatin1String("lined_cylinder"))
      painter.drawLine(QPointF(r.left(), r.top() + 2.0 * n.radiusY),
                       QPointF(r.right(), r.top() + 2.0 * n.radiusY));
  } else if (n.shapeKind == QLatin1String("horizontalCylinder")) {
    QPainterPath path;
    path.moveTo(r.left() + n.radiusX, r.top());
    path.lineTo(r.right() - n.radiusX, r.top());
    path.cubicTo(r.right() + n.radiusX, r.top(), r.right() + n.radiusX, r.bottom(),
                 r.right() - n.radiusX, r.bottom());
    path.lineTo(r.left() + n.radiusX, r.bottom());
    path.cubicTo(r.left() - n.radiusX, r.bottom(), r.left() - n.radiusX, r.top(),
                 r.left() + n.radiusX, r.top());
    path.closeSubpath();
    painter.drawPath(path);
    painter.drawEllipse(QRectF(r.left(), r.top(), 2.0 * n.radiusX, r.height()));
  } else {
    painter.drawRect(r);
  }
}

void drawNeoShadow(QPainter& painter, const FlowSceneNode& node, const QRectF& bounds,
                   const FlowScene& scene) {
  // SVG/CSS: drop-shadow(0px 1px 2px rgba(0,0,0,.25)). A normalized discrete
  // Gaussian gives stable results on every QPainter backend while preserving
  // the source alpha silhouette used by CSS drop-shadow.
  constexpr int radius = 4;
  constexpr qreal sigma = 2.0;
  qreal weights[2 * radius + 1][2 * radius + 1];
  qreal sum = 0.0;
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      const qreal weight = std::exp(-(x * x + y * y) / (2.0 * sigma * sigma));
      weights[y + radius][x + radius] = weight;
      sum += weight;
    }
  }
  painter.setPen(Qt::NoPen);
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      QColor shadow = qcolor(scene.shadowColor);
      shadow.setAlphaF(scene.shadowOpacity * weights[y + radius][x + radius] / sum);
      painter.setBrush(shadow);
      drawNodeShape(painter, node, bounds,
                    QPointF(x + scene.shadowOffsetX, y + scene.shadowOffsetY));
    }
  }
}

void drawNeoShadowMask(QPainter& painter, const FlowSceneNode& node, const QRectF& bounds) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(kCatShadow));
  for (int y = -4; y <= 4; ++y)
    for (int x = -4; x <= 4; ++x)
      drawNodeShape(painter, node, bounds, QPointF(x, y + 1));
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
    if (scene.look == flowchart::FlowLook::Neo) {
      painter.setPen(Qt::NoPen);
      for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
          const qreal weight = std::exp(-(x * x + y * y) / 8.0);
          QColor shadow = mode == PaintMode::Color ? qcolor(scene.shadowColor)
                                                    : QColor(kCatShadow);
          if (mode == PaintMode::Color) shadow.setAlphaF(scene.shadowOpacity * weight / 23.9907);
          painter.setBrush(shadow);
          painter.drawRect(r.translated(x + scene.shadowOffsetX,
                                        y + scene.shadowOffsetY));
        }
      }
    }
    painter.setBrush(paletteColor(c.fill, kCatCluster));
    QPen pen(paletteColor(c.stroke, kCatCluster)); pen.setWidthF(pxSize(c.strokeWidth));
    if (mode == PaintMode::Color && scene.useGradient) {
      QLinearGradient gradient(r.left(), 0.0, r.right(), 0.0);
      gradient.setColorAt(0.0, qcolor(scene.gradientStart));
      gradient.setColorAt(1.0, qcolor(scene.gradientStop));
      pen.setBrush(gradient);
    }
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
    const bool useMargin = scene.look == flowchart::FlowLook::Neo && !e.animated;
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
    if (!e.markerEnd.isEmpty()) drawMarker(painter, e.markerEnd, pp.endPoint, pp.endTangent, mc, useMargin);
    if (!e.markerStart.isEmpty()) {
      QPointF t = pp.startTangent;
      drawMarker(painter, e.markerStart, pp.startPoint, QPointF(-t.x(), -t.y()), mc, useMargin);  // start marker points backward
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
    if (scene.look == flowchart::FlowLook::Neo) {
      if (mode == PaintMode::Color) drawNeoShadow(painter, n, r, scene);
      else drawNeoShadowMask(painter, n, r);
    }
    painter.setBrush(paletteColor(n.fill, kCatNode));
    QPen pen(paletteColor(n.stroke, kCatNode)); pen.setWidthF(pxSize(n.strokeWidth));
    if (mode == PaintMode::Color && scene.useGradient &&
        !scene.gradientStart.isEmpty() && !scene.gradientStop.isEmpty()) {
      QLinearGradient gradient(r.left(), 0.0, r.right(), 0.0);
      gradient.setColorAt(0.0, qcolor(scene.gradientStart));
      gradient.setColorAt(1.0, qcolor(scene.gradientStop));
      pen.setBrush(gradient);
    }
    painter.setPen(pen);
    drawNodeShape(painter, n, r);
    // Node label centered.
    if (!n.label.text.isEmpty()) {
      QRectF labelRect = r;
      labelRect.translate(n.label.x - n.cx, n.label.y - n.cy);
      drawLabel(painter, n.label, labelRect, fontFamily, true, mode);
    }
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
