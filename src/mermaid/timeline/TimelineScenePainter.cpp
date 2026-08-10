#include "mermaid/timeline/TimelineScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/timeline/TimelineScene.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::timeline {
namespace {

QColor brighten(QColor color, bool enabled) {
  if (!enabled || !color.isValid()) return color;
  color.setRed(std::min(255, qRound(color.red() * 1.2)));
  color.setGreen(std::min(255, qRound(color.green() * 1.2)));
  color.setBlue(std::min(255, qRound(color.blue() * 1.2)));
  return color;
}

qreal svgLineCoordinate(qreal value) {
  return std::isfinite(value) ? value : 0.0;
}

QPointF svgLinePoint(const QPointF& point) {
  return QPointF(svgLineCoordinate(point.x()), svgLineCoordinate(point.y()));
}

QStringList cssFontFamilies(const QString& expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

editor::CssPixelFont paintFont(const TimelineSceneStyle& style, qreal size,
                               QFont::Weight weight) {
  const QStringList families = cssFontFamilies(style.fontFamily);
  editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(families.first(), size);
  if (families.size() > 1) font.font.setFamilies(families);
  font.font.setWeight(weight);
  return font;
}

QPainterPath nodePath(const TimelineNodeGeometry& node) {
  QPainterPath path;
  const qreal w = node.width;
  const qreal h = node.height;
  path.moveTo(0.0, h - 5.0);
  path.lineTo(0.0, node.rounded ? 5.0 : 0.0);
  if (node.rounded) {
    path.quadTo(0.0, 0.0, 5.0, 0.0);
    path.lineTo(w - 5.0, 0.0);
    path.quadTo(w, 0.0, w, 5.0);
  } else {
    path.lineTo(w, 0.0);
  }
  path.lineTo(w, h);
  path.lineTo(0.0, h);
  path.closeSubpath();
  return path;
}

QColor inheritedColor(const TimelineScene& scene) {
  const QString value = scene.style.textColor.trimmed();
  if (color::isParsableColor(value)) return color::toQColor(value);
  return QColor(Qt::black);
}

color::SvgPaint rootSvgFill(const TimelineScene& scene) {
  const QString value = scene.style.textColor.trimmed();
  if (value.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0)
    return {.none = true, .color = QColor()};
  if (color::isParsableColor(value))
    return {.none = false, .color = color::toQColor(value)};
  // The SVG root has no parent SVG paint to inherit from. Empty, garbage and
  // CSS-wide/currentColor keywords all resolve to SVG's initial black fill.
  return {.none = false, .color = QColor(Qt::black)};
}

color::SvgPaint inheritedFill(const QString& value,
                              const TimelineScene& scene) {
  const QString raw = value.trimmed();
  const QString lower = raw.toLower();
  const color::SvgPaint root = rootSvgFill(scene);
  if (raw.isEmpty() || lower == QLatin1String("inherit") ||
      lower == QLatin1String("unset") || lower == QLatin1String("revert") ||
      lower == QLatin1String("revert-layer") ||
      (!color::isParsableColor(raw) && lower != QLatin1String("none") &&
       lower != QLatin1String("currentcolor") &&
       lower != QLatin1String("initial")))
    return root;
  if (lower == QLatin1String("none")) return {.none = true, .color = QColor()};
  if (lower == QLatin1String("currentcolor") ||
      lower == QLatin1String("initial"))
    return {.none = false, .color = QColor(Qt::black)};
  return {.none = false, .color = color::toQColor(raw)};
}

QBrush gradientBrush(const TimelineScene& scene,
                     const TimelineNodeGeometry& node) {
  QLinearGradient gradient(0.0, 0.0, node.width, 0.0);
  gradient.setColorAt(0.0, brighten(color::toQColor(scene.style.gradientStart),
                                    node.eventBrightness));
  gradient.setColorAt(1.0, brighten(color::toQColor(scene.style.gradientStop),
                                    node.eventBrightness));
  return QBrush(gradient);
}

void paintNode(const TimelineScene& scene, const TimelineNodeGeometry& node,
               QPainter& painter) {
  if (!std::isfinite(node.position.x()) ||
      !std::isfinite(node.position.y()))
    return;
  painter.save();
  painter.translate(node.position);
  const bool validPathGeometry = std::isfinite(node.width) &&
                                 std::isfinite(node.height);
  const bool partialTopEdge = std::isfinite(node.width) &&
                              !std::isfinite(node.height);
  QPainterPath path;
  if (validPathGeometry) {
    path = nodePath(node);
  } else if (partialTopEdge) {
    path.moveTo(0.0, 0.0);
    path.lineTo(node.width, 0.0);
  }
  const QColor inherited = inheritedColor(scene);
  color::SvgPaint fill = inheritedFill(node.fill, scene);
  color::SvgPaint stroke = color::resolveSvgPaint(
      node.stroke, color::SvgPaintKind::Stroke, inherited);
  fill.color = brighten(fill.color, node.eventBrightness);
  stroke.color = brighten(stroke.color, node.eventBrightness);

  const qreal fillSourceAlpha =
      validPathGeometry && !fill.none ? fill.color.alphaF() : 0.0;
  const QColor gradientStart = color::toQColor(scene.style.gradientStart);
  const QColor gradientStop = color::toQColor(scene.style.gradientStop);
  const qreal outlineSourceAlpha =
      node.gradientStroke && node.strokeWidth > 0.0
          ? std::max(gradientStart.alphaF(), gradientStop.alphaF())
          : !stroke.none && node.strokeWidth > 0.0 ? stroke.color.alphaF()
                                                   : 0.0;
  const bool fillSourceVisible = fillSourceAlpha > 0.0;
  const bool outlineSourceVisible = outlineSourceAlpha > 0.0;
  if (!path.isEmpty() && node.dropShadow &&
      (fillSourceVisible || outlineSourceVisible)) {
    QColor shadow = scene.style.themeName.contains(QStringLiteral("dark"),
                                                   Qt::CaseSensitive)
                        ? QColor(Qt::white)
                        : QColor(Qt::black);
    shadow.setAlphaF(scene.style.themeName.contains(QStringLiteral("dark"),
                                                   Qt::CaseSensitive)
                         ? 0.2
                         : 0.06);
    shadow = brighten(shadow, node.eventBrightness);
    QPainterPath outline;
    if (outlineSourceVisible) {
      QPainterPathStroker stroker;
      stroker.setWidth(node.strokeWidth);
      stroker.setCapStyle(Qt::FlatCap);
      stroker.setJoinStyle(Qt::MiterJoin);
      outline = stroker.createStroke(path);
    }
    painter.save();
    painter.translate(4.0, 4.0);
    auto paintShadow = [&](const QPainterPath& shape, qreal sourceAlpha) {
      if (shape.isEmpty() || !(sourceAlpha > 0.0)) return;
      QColor paint = shadow;
      paint.setAlphaF(std::clamp(shadow.alphaF() * sourceAlpha, 0.0, 1.0));
      painter.fillPath(shape, paint);
    };
    if (fillSourceVisible && outlineSourceVisible) {
      const QPainterPath overlap = path.intersected(outline);
      paintShadow(path.subtracted(outline), fillSourceAlpha);
      paintShadow(outline.subtracted(path), outlineSourceAlpha);
      paintShadow(overlap, outlineSourceAlpha +
                               fillSourceAlpha * (1.0 - outlineSourceAlpha));
    } else if (fillSourceVisible) {
      paintShadow(path, fillSourceAlpha);
    } else {
      paintShadow(outline, outlineSourceAlpha);
    }
    painter.restore();
  }

  if (!path.isEmpty()) {
    painter.setBrush(fill.none ? QBrush(Qt::NoBrush) : QBrush(fill.color));
    if (node.gradientStroke && node.strokeWidth > 0.0) {
      QPen pen(gradientBrush(scene, node), node.strokeWidth);
      pen.setCapStyle(Qt::FlatCap);
      pen.setJoinStyle(Qt::MiterJoin);
      painter.setPen(pen);
    } else if (stroke.none || !(node.strokeWidth > 0.0)) {
      painter.setPen(Qt::NoPen);
    } else {
      QPen pen(stroke.color, node.strokeWidth);
      pen.setCapStyle(Qt::FlatCap);
      pen.setJoinStyle(Qt::MiterJoin);
      painter.setPen(pen);
    }
    painter.drawPath(path);
  }

  if (validPathGeometry && node.dividerVisible && node.dividerWidth > 0.0) {
    QPen divider;
    if (node.gradientStroke) {
      divider = QPen(gradientBrush(scene, node), node.dividerWidth);
    } else {
      color::SvgPaint paint = color::resolveSvgPaint(
          node.dividerStroke, color::SvgPaintKind::Stroke, inherited);
      paint.color = brighten(paint.color, node.eventBrightness);
      divider = paint.none ? QPen(Qt::NoPen) : QPen(paint.color, node.dividerWidth);
    }
    divider.setCapStyle(Qt::FlatCap);
    painter.setPen(divider);
    painter.drawLine(QPointF(0.0, node.height), QPointF(node.width, node.height));
  }

  color::SvgPaint textPaint = inheritedFill(node.textFill, scene);
  if (!textPaint.none && scene.style.fontSize > 0.0) {
    textPaint.color = brighten(textPaint.color, node.eventBrightness);
    editor::CssPixelFont font = paintFont(
        scene.style, scene.style.fontSize, scene.style.nodeFontWeight);
    painter.setFont(font.font);
    painter.setPen(textPaint.color);
    for (const TimelineTextLine& line : node.textLines) {
      if (line.visibleText.isEmpty()) continue;
      painter.save();
      painter.translate(node.textOffset + line.baseline);
      painter.scale(font.scale, font.scale);
      painter.drawText(QPointF(-font.horizontalAdvance(line.visibleText) /
                                   (2.0 * font.scale),
                               0.0),
                       line.visibleText);
      painter.restore();
    }
  }
  painter.restore();
}

void paintArrow(const TimelineScene& scene, const TimelineLineGeometry& line,
                QPainter& painter) {
  if (!line.markerEnd || !line.markerResolved || !(line.strokeWidth > 0.0)) return;
  const color::SvgPaint markerFill = rootSvgFill(scene);
  if (markerFill.none) return;
  const QPointF start = svgLinePoint(line.start);
  const QPointF end = svgLinePoint(line.end);
  const QPointF delta = end - start;
  const qreal angle = std::atan2(delta.y(), delta.x());
  const qreal c = std::cos(angle);
  const qreal s = std::sin(angle);
  auto map = [&](qreal x, qreal y) {
    x *= line.strokeWidth;
    y *= line.strokeWidth;
    return end + QPointF(x * c - y * s, x * s + y * c);
  };
  QPolygonF polygon;
  polygon << map(-5.0, -2.0) << map(-5.0, 2.0) << map(1.0, 0.0);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(markerFill.color);
  painter.drawPolygon(polygon);
  painter.restore();
}

void paintLine(const TimelineScene& scene, const TimelineLineGeometry& line,
               QPainter& painter) {
  const QString raw = line.stroke.trimmed();
  color::SvgPaint stroke;
  const bool keyword = raw.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0 ||
                       raw.compare(QStringLiteral("currentColor"), Qt::CaseInsensitive) == 0 ||
                       raw.compare(QStringLiteral("inherit"), Qt::CaseInsensitive) == 0 ||
                       raw.compare(QStringLiteral("initial"), Qt::CaseInsensitive) == 0 ||
                       raw.compare(QStringLiteral("unset"), Qt::CaseInsensitive) == 0 ||
                       raw.compare(QStringLiteral("revert"), Qt::CaseInsensitive) == 0;
  if (raw.isEmpty() || (!keyword && !color::isParsableColor(raw))) {
    // The stylesheet declaration was dropped; the line's presentation
    // attribute stroke="black" remains active.
    stroke = {.none = false, .color = QColor(Qt::black)};
  } else {
    stroke = color::resolveSvgPaint(raw, color::SvgPaintKind::Stroke,
                                    inheritedColor(scene));
  }
  if (!(line.strokeWidth > 0.0)) return;
  if (!stroke.none) {
    QPen pen(stroke.color, line.strokeWidth);
    pen.setCapStyle(Qt::FlatCap);
    if (!line.dashPattern.isEmpty()) {
      QVector<qreal> scaled;
      scaled.reserve(line.dashPattern.size());
      for (qreal value : line.dashPattern)
        scaled.append(value / line.strokeWidth);
      pen.setDashPattern(scaled);
    }
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(svgLinePoint(line.start), svgLinePoint(line.end));
  }
  paintArrow(scene, line, painter);
}

void paintTitle(const TimelineScene& scene, QPainter& painter) {
  if (!scene.titleGeometry.visible || !(scene.titleGeometry.fontSize > 0.0)) return;
  const color::SvgPaint fill = inheritedFill(scene.titleGeometry.fill, scene);
  if (fill.none) return;
  editor::CssPixelFont font =
      paintFont(scene.style, scene.titleGeometry.fontSize, QFont::Bold);
  painter.save();
  painter.setFont(font.font);
  painter.setPen(fill.color);
  painter.translate(scene.titleGeometry.baseline);
  painter.scale(font.scale, font.scale);
  painter.drawText(QPointF(0.0, 0.0), scene.titleGeometry.text);
  painter.restore();
}

struct PaintItem {
  enum class Kind { Node, Line, Title } kind;
  int index = -1;
  int order = 0;
};

}  // namespace

void paintTimelineScene(const TimelineScene& scene, QPainter& painter,
                        const MermaidPaintOptions& options) {
  QVector<PaintItem> items;
  for (qsizetype i = 0; i < scene.nodes.size(); ++i)
    items.append({PaintItem::Kind::Node, static_cast<int>(i),
                  scene.nodes.at(i).paintOrder});
  for (qsizetype i = 0; i < scene.lines.size(); ++i)
    items.append({PaintItem::Kind::Line, static_cast<int>(i),
                  scene.lines.at(i).paintOrder});
  if (scene.titleGeometry.visible)
    items.append({PaintItem::Kind::Title, -1, scene.titleGeometry.paintOrder});
  std::stable_sort(items.begin(), items.end(),
                   [](const PaintItem& a, const PaintItem& b) {
                     return a.order < b.order;
                   });

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  for (const PaintItem& item : items) {
    if (item.kind == PaintItem::Kind::Node) {
      const TimelineNodeGeometry& node = scene.nodes.at(item.index);
      if (mermaidPrimitiveIsVisible(
              QRectF(node.position, QSizeF(node.width, node.height)), options))
        paintNode(scene, node, painter);
    } else if (item.kind == PaintItem::Kind::Line) {
      const TimelineLineGeometry& line = scene.lines.at(item.index);
      if (mermaidPrimitiveIsVisible(QRectF(line.start, line.end).normalized(),
                                    options))
        paintLine(scene, line, painter);
    } else if (mermaidPrimitiveIsVisible(scene.titleGeometry.logicalBounds,
                                         options)) {
      paintTitle(scene, painter);
    }
  }
  painter.restore();
}

}  // namespace muffin::mermaid::timeline
