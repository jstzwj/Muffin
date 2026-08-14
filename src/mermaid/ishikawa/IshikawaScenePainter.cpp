#include "mermaid/ishikawa/IshikawaScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/ishikawa/IshikawaScene.h"
#include "mermaid/rough/RoughPaint.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>

#include <cmath>
#include <algorithm>

namespace muffin::mermaid::ishikawa {
namespace {

QStringList cssFontFamilies(const QString& expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') &&
          family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') &&
          family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

editor::CssPixelFont textFont(const IshikawaScene& scene,
                              const IshikawaTextGeometry& text) {
  const QStringList families = cssFontFamilies(
      text.fontFamily.isEmpty() ? scene.style.fontFamily : text.fontFamily);
  editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(families.first(), text.fontSize);
  if (families.size() > 1) font.font.setFamilies(families);
  font.font.setWeight(text.weight);
  font.font.setStyle(text.fontStyle);
  return font;
}

color::SvgPaint paintValue(const QString& value, color::SvgPaintKind kind,
                           const QColor& inherited) {
  return color::resolveSvgPaint(value, kind, inherited);
}

QColor rootTextColor(const IshikawaScene& scene) {
  const color::SvgPaint value =
      paintValue(scene.style.textColor, color::SvgPaintKind::Text,
                 QColor(Qt::black));
  return value.none || !value.color.isValid() ? QColor(Qt::black) : value.color;
}

QPen linePen(const IshikawaScene& scene, const QString& value, qreal width,
             qreal opacity) {
  const color::SvgPaint stroke =
      paintValue(value, color::SvgPaintKind::Stroke,
                 rootTextColor(scene));
  if (stroke.none || !stroke.color.isValid() || !(width > 0.0))
    return Qt::NoPen;
  QColor used = stroke.color;
  used.setAlphaF(used.alphaF() * std::clamp(opacity, 0.0, 1.0));
  QPen pen(used, width);
  pen.setCapStyle(Qt::SquareCap);
  pen.setJoinStyle(Qt::MiterJoin);
  return pen;
}

QBrush fillBrush(const IshikawaScene& scene, const QString& value,
                 qreal opacity = 1.0) {
  const color::SvgPaint fill =
      paintValue(value, color::SvgPaintKind::Fill, rootTextColor(scene));
  if (fill.none || !fill.color.isValid()) return Qt::NoBrush;
  QColor used = fill.color;
  used.setAlphaF(used.alphaF() * std::clamp(opacity, 0.0, 1.0));
  return QBrush(used);
}

void drawText(const IshikawaScene& scene, const IshikawaTextGeometry& text,
              QPainter& painter) {
  if (!text.visible) return;
  const color::SvgPaint fill =
      paintValue(text.fill, color::SvgPaintKind::Text,
                 QColor(Qt::black));
  if (fill.none || !fill.color.isValid() || !(text.fontSize > 0.0)) return;
  const editor::CssPixelFont font = textFont(scene, text);
  const QFontMetricsF metrics(font.font);
  painter.save();
  painter.setFont(font.font);
  QColor used = fill.color;
  used.setAlphaF(used.alphaF() * std::clamp(text.opacity, 0.0, 1.0));
  painter.setPen(used);
  for (int index = 0; index < text.lines.size(); ++index) {
    QString visible = text.lines.at(index);
    visible.replace(QRegularExpression(QStringLiteral(
                        R"([\x{0009}-\x{000d}\x{0020}]+)")),
                    QStringLiteral(" "));
    visible = visible.trimmed();
    if (visible.isEmpty()) continue;
    qreal x = text.anchor.x() + text.translation.x();
    const qreal advance = metrics.horizontalAdvance(visible) * font.scale;
    if (text.textAnchor == IshikawaTextAnchor::Middle) x -= advance / 2.0;
    else if (text.textAnchor == IshikawaTextAnchor::End) x -= advance;
    qreal baseline =
        text.firstY + index * text.lineStep + text.translation.y();
    if (text.baseline == IshikawaTextBaseline::Middle)
      baseline += metrics.xHeight() * font.scale / 2.0;
    else if (text.baseline == IshikawaTextBaseline::Hanging)
      baseline += metrics.ascent() * font.scale * 0.8;
    painter.save();
    painter.translate(x, baseline);
    painter.scale(font.scale, font.scale);
    painter.drawText(QPointF(), visible);
    painter.restore();
  }
  painter.restore();
}

void drawMarker(const IshikawaScene& scene, const QLineF& line,
                QPainter& painter) {
  if (line.length() == 0.0 || !scene.style.markerVisible) return;
  const QBrush fill = fillBrush(
      scene, scene.style.markerFill.isEmpty() ? scene.style.lineColor
                                              : scene.style.markerFill,
      scene.style.markerOpacity);
  if (fill.style() == Qt::NoBrush) return;
  const QPointF unit = (line.p2() - line.p1()) / line.length();
  const QPointF perpendicular(-unit.y(), unit.x());
  const qreal size = 6.0;
  QPolygonF marker;
  marker << line.p1()
         << line.p1() + unit * size * 2.0 + perpendicular * size
         << line.p1() + unit * size * 2.0 - perpendicular * size;
  painter.setPen(Qt::NoPen);
  painter.setBrush(fill);
  painter.drawPolygon(marker);
}

}  // namespace

void paintIshikawaScene(const IshikawaScene& scene, QPainter& painter,
                        const MermaidPaintOptions&) {
  for (const IshikawaPaintEntry& entry : scene.paintOrder) {
    switch (entry.kind) {
      case IshikawaPrimitiveKind::Line: {
        const IshikawaLineGeometry& line = scene.lines.at(entry.index);
        if (!line.visible) break;
        if (line.rough) {
          const QPen stroke(linePen(scene, line.stroke, line.strokeWidth,
                                    line.strokeOpacity));
          rough::drawRoughDrawable(painter, line.roughDrawable, Qt::NoBrush,
                                   stroke, Qt::NoPen);
        } else {
          painter.setPen(linePen(scene, line.stroke, line.strokeWidth,
                                 line.strokeOpacity));
          painter.setBrush(Qt::NoBrush);
          painter.drawLine(line.line);
          if (line.markerStart) drawMarker(scene, line.line, painter);
        }
        break;
      }
      case IshikawaPrimitiveKind::Path: {
        const IshikawaPathGeometry& path = scene.paths.at(entry.index);
        if (!path.visible) break;
        if (path.rough) {
          const QBrush fill = fillBrush(scene, path.fill, path.fillOpacity);
          const QPen stroke = linePen(scene, path.stroke, path.strokeWidth,
                                      path.strokeOpacity);
          const QPen hachure(
              path.className.isEmpty() ? stroke.color()
                                       : fill.color(),
              path.className.isEmpty() ? 1.0 : 2.5);
          rough::drawRoughDrawable(
              painter, path.roughDrawable, fill, stroke, hachure);
        } else {
          painter.setPen(linePen(scene, path.stroke, path.strokeWidth,
                                 path.strokeOpacity));
          painter.setBrush(fillBrush(scene, path.fill, path.fillOpacity));
          painter.drawPath(path.path);
        }
        break;
      }
      case IshikawaPrimitiveKind::Rect: {
        const IshikawaRectGeometry& rect = scene.rects.at(entry.index);
        if (!rect.visible) break;
        if (rect.rough) {
          const QBrush fill = fillBrush(scene, rect.fill, rect.fillOpacity);
          rough::drawRoughDrawable(
              painter, rect.roughDrawable, fill,
              linePen(scene, rect.stroke, rect.strokeWidth,
                      rect.strokeOpacity),
              QPen(fill.color(), 2.5));
        } else {
          painter.setPen(linePen(scene, rect.stroke, rect.strokeWidth,
                                 rect.strokeOpacity));
          painter.setBrush(fillBrush(scene, rect.fill, rect.fillOpacity));
          painter.drawRect(rect.rect);
        }
        break;
      }
      case IshikawaPrimitiveKind::Text:
        drawText(scene, scene.texts.at(entry.index), painter);
        break;
    }
  }
}

}  // namespace muffin::mermaid::ishikawa
