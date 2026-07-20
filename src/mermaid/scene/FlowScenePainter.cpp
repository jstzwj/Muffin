#include "mermaid/scene/FlowScenePainter.h"
#include "mermaid/MermaidFontRegistry.h"

#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/rough/RoughOps.h"
#include "mermaid/scene/FlowMarkers.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRegularExpression>
#include <QTransform>

#include <cmath>

namespace muffin::mermaid::flowscene {
namespace {

QColor qcolor(const QString& s) { return color::toQColor(s); }

QColor svgFilterQuantizedColor(const QColor& source) {
  auto channel = [](int value) {
    const qreal srgb = value / 255.0;
    const qreal linear = srgb <= 0.04045 ? srgb / 12.92
                                         : std::pow((srgb + 0.055) / 1.055, 2.4);
    const qreal quantized = std::round(linear * 255.0) / 255.0;
    const qreal encoded = quantized <= 0.0031308
        ? quantized * 12.92
        : 1.055 * std::pow(quantized, 1.0 / 2.4) - 0.055;
    return std::clamp(static_cast<int>(std::round(encoded * 255.0)), 0, 255);
  };
  return QColor(channel(source.red()), channel(source.green()), channel(source.blue()),
                source.alpha());
}

qreal pxSize(const QString& s) {
  static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+)?)"));
  const QRegularExpressionMatch m = re.match(s);
  return m.hasMatch() ? m.captured(1).toDouble() : 16.0;
}

void drawRoughDrawable(QPainter& painter, const rough::Drawable& drawable,
                       const QBrush& fillBrush, const QPen& strokePen,
                       const QPen& fillSketchPen) {
  for (const rough::OpSet& set : drawable.sets) {
    switch (set.type) {
      case rough::OpSetType::FillPath:
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillBrush);
        break;
      case rough::OpSetType::FillSketch:
        painter.setPen(fillSketchPen);
        painter.setBrush(Qt::NoBrush);
        break;
      case rough::OpSetType::Path:
        painter.setPen(strokePen);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    painter.drawPath(rough::toPainterPath(set));
  }
}

bool isAxisAlignedRectangle(const QPainterPath& path) {
  if (path.elementCount() != 5) return false;
  const QRectF bounds = path.boundingRect();
  for (int i = 0; i < path.elementCount(); ++i) {
    const auto element = path.elementAt(i);
    if (element.type == QPainterPath::CurveToElement ||
        !((element.x == bounds.left() || element.x == bounds.right()) &&
          (element.y == bounds.top() || element.y == bounds.bottom()))) return false;
  }
  return true;
}

rough::Drawable nodeRoughDrawable(const FlowSceneNode& node,
                                  const FlowSceneShapePath& item,
                                  const rough::Options& options) {
  const QRectF bounds = item.path.boundingRect();
  if (isAxisAlignedRectangle(item.path))
    return rough::rectangle(bounds.x(), bounds.y(), bounds.width(), bounds.height(), options);
  if (node.shapeKind == QLatin1String("ellipse") && item.path.elementCount() > 5)
    return rough::ellipse(bounds.center().x(), bounds.center().y(),
                          bounds.width(), bounds.height(), options);
  if (node.shapeKind == QLatin1String("polygon") &&
      node.shapePaths.size() == 1 && !node.points.isEmpty())
    return rough::polygon(node.points, options);
  return rough::path(item.path, options);
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
  const qreal lineHeight = (font.pixelSize() > 0 ? font.pixelSize() : 16.0) * 1.5;
  const QColor color = mode == PaintMode::CategoryMask
                           ? QColor(kCatText)
                           : qcolor(label.color.isEmpty() ? QStringLiteral("#333333")
                                                          : label.color);
  flowchart::paintFlowLabel(painter, label.richText, rect, font.family(), font.pixelSize(),
                            lineHeight, color, center);
}

void drawNodeShape(QPainter& painter, const FlowSceneNode& n, QRectF r,
                   const QPointF& offset = {}, bool forceSilhouette = false,
                   bool categoryMask = false, bool handDrawn = false,
                   quint32 handDrawnSeed = 0) {
  r.translate(offset);
  if (!n.shapePaths.isEmpty()) {
    const QBrush fillBrush = painter.brush();
    const QPen strokePen = painter.pen();
    painter.save();
    painter.translate(n.cx + offset.x(), n.cy + offset.y());
    for (const FlowSceneShapePath& item : n.shapePaths) {
      QBrush itemBrush = fillBrush;
      if (!categoryMask && !forceSilhouette && !item.fillOverride.isEmpty())
        itemBrush = QBrush(qcolor(item.fillOverride));
      painter.setBrush(item.fill ? itemBrush : Qt::NoBrush);
      if (item.stroke) {
        if (categoryMask) {
          QPen boundary{QColor(kCatBoundary)};
          boundary.setWidthF(std::max<qreal>(1.0, pxSize(n.strokeWidth)));
          painter.setPen(boundary);
        } else if (forceSilhouette) {
          QPen silhouette(fillBrush.color());
          silhouette.setWidthF(std::max<qreal>(1.0, pxSize(n.strokeWidth)));
          painter.setPen(silhouette);
        } else {
          QPen itemPen = strokePen;
          if (!item.strokeOverride.isEmpty()) itemPen.setColor(qcolor(item.strokeOverride));
          painter.setPen(itemPen);
        }
      } else {
        painter.setPen(Qt::NoPen);
      }
      if (handDrawn && !forceSilhouette) {
        rough::Options options;
        options.seed = handDrawnSeed;
        options.roughness = 0.7;
        options.strokeWidth = 1.3;
        options.stroke = item.stroke ? QStringLiteral("#000") : QStringLiteral("none");
        options.fill = item.fill ? QStringLiteral("#000") : QString{};
        options.fillStyle = QStringLiteral("hachure");
        options.fillWeight = 4.0;
        options.hachureGap = 5.2;
        QPen roughStroke = strokePen;
        if (!item.strokeOverride.isEmpty()) roughStroke.setColor(qcolor(item.strokeOverride));
        if (categoryMask)
          roughStroke = QPen(QColor(kCatBoundary),
                             std::max<qreal>(5.0, options.strokeWidth + 4.0));
        QPen fillSketch(categoryMask ? QColor(kCatBoundary) : itemBrush.color());
        fillSketch.setWidthF(categoryMask ? 4.0
                                          : pxSize(n.strokeWidth));
        drawRoughDrawable(painter, nodeRoughDrawable(n, item, options), itemBrush,
                          roughStroke, fillSketch);
      } else {
        painter.drawPath(item.path);
      }
    }
    painter.restore();
    return;
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
                    QPointF(x + scene.shadowOffsetX, y + scene.shadowOffsetY), true);
    }
  }
}

void drawNeoShadowMask(QPainter& painter, const FlowSceneNode& node, const QRectF& bounds) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(kCatShadow));
  for (int y = -4; y <= 4; ++y)
    for (int x = -4; x <= 4; ++x)
      drawNodeShape(painter, node, bounds, QPointF(x, y + 1), true);
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
    QColor clusterFill = paletteColor(c.fill, kCatCluster);
    if (mode == PaintMode::Color && scene.look == flowchart::FlowLook::Neo &&
        !scene.useGradient)
      clusterFill = svgFilterQuantizedColor(clusterFill);
    painter.setBrush(clusterFill);
    QPen pen(paletteColor(c.stroke, kCatCluster)); pen.setWidthF(pxSize(c.strokeWidth));
    if (mode == PaintMode::Color && scene.useGradient) {
      QLinearGradient gradient(r.left(), 0.0, r.right(), 0.0);
      gradient.setColorAt(0.0, qcolor(scene.gradientStart));
      gradient.setColorAt(1.0, qcolor(scene.gradientStop));
      pen.setBrush(gradient);
    }
    painter.setPen(pen);
    if (scene.look == flowchart::FlowLook::HandDrawn) {
      rough::Options options;
      options.seed = scene.handDrawnSeed;
      options.roughness = 0.7;
      options.strokeWidth = 1.3;
      options.fill = QStringLiteral("#000");
      options.fillWeight = 3.0;
      options.hachureGap = 5.2;
      QPen hachurePen(mode == PaintMode::CategoryMask ? QColor(kCatBoundary)
                                                       : clusterFill);
      hachurePen.setWidthF(mode == PaintMode::CategoryMask ? 4.0
                                                           : pxSize(c.strokeWidth));
      if (mode == PaintMode::CategoryMask)
        pen.setWidthF(std::max<qreal>(5.0, pen.widthF() + 4.0));
      QPainterPath clusterPath;
      clusterPath.addRect(r);
      drawRoughDrawable(painter, rough::path(clusterPath, options),
                        clusterFill, pen, hachurePen);
    } else {
      painter.drawRoundedRect(r, 0, 0);
    }
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
    if (scene.look == flowchart::FlowLook::HandDrawn) {
      if (mode == PaintMode::CategoryMask)
        pen.setWidthF(std::max<qreal>(5.0, pen.widthF() + 4.0));
      rough::Options options;
      options.seed = scene.handDrawnSeed;
      options.roughness = 0.3;
      options.strokeWidth = pxSize(e.strokeWidth);
      drawRoughDrawable(painter, rough::path(pp.path, options), Qt::NoBrush,
                        pen, Qt::NoPen);
    } else {
      painter.setPen(pen); painter.setBrush(Qt::NoBrush);
      painter.drawPath(pp.path);
    }
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
    QColor nodeFill = paletteColor(n.fill, kCatNode);
    if (mode == PaintMode::Color && scene.look == flowchart::FlowLook::Neo &&
        !scene.useGradient)
      nodeFill = svgFilterQuantizedColor(nodeFill);
    painter.setBrush(nodeFill);
    QPen pen(paletteColor(n.stroke, kCatNode)); pen.setWidthF(pxSize(n.strokeWidth));
    if (mode == PaintMode::Color && scene.useGradient &&
        !scene.gradientStart.isEmpty() && !scene.gradientStop.isEmpty()) {
      const qreal gradientLeft = n.shapePaths.isEmpty() ? r.left() : -n.width / 2.0;
      const qreal gradientRight = n.shapePaths.isEmpty() ? r.right() : n.width / 2.0;
      QLinearGradient gradient(gradientLeft, 0.0, gradientRight, 0.0);
      gradient.setColorAt(0.0, qcolor(scene.gradientStart));
      gradient.setColorAt(1.0, qcolor(scene.gradientStop));
      pen.setBrush(gradient);
    }
    painter.setPen(pen);
    drawNodeShape(painter, n, r, {}, false, mode == PaintMode::CategoryMask,
                  scene.look == flowchart::FlowLook::HandDrawn,
                  scene.handDrawnSeed);
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
