#include "mermaid/venn/VennScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/rough/RoughPaint.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/venn/VennScene.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QTextLayout>

#include <cmath>

namespace muffin::mermaid::venn {
namespace {

QStringList cssFamilies(const QString& expression) {
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

editor::CssPixelFont textFont(const VennScene& scene, qreal size,
                              const QString& resolvedFamily = {},
                              QFont::Weight weight = QFont::Normal,
                              QFont::Style fontStyle = QFont::StyleNormal) {
  const QStringList families = cssFamilies(
      resolvedFamily.isEmpty() ? scene.style.fontFamily : resolvedFamily);
  editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(families.first(), size);
  if (families.size() > 1) font.font.setFamilies(families);
  font.font.setWeight(weight);
  font.font.setStyle(fontStyle);
  return font;
}

color::SvgPaint textPaint(const QString& value) {
  return color::resolveSvgPaint(value, color::SvgPaintKind::Text,
                                QColor(Qt::black));
}

void paintText(const VennScene& scene, const VennTextGeometry& text,
               QPainter& painter, qreal yOffset = 0.0) {
  if (!text.visible || !(text.fontSize > 0.0) || text.lines.isEmpty()) return;
  const color::SvgPaint fill = textPaint(text.fill);
  if (fill.none) return;
  const editor::CssPixelFont font = textFont(
      scene, text.fontSize, text.fontFamily, text.fontWeight, text.fontStyle);
  const QFontMetricsF metrics(font.font);
  painter.save();
  painter.setOpacity(text.opacity);
  painter.setFont(font.font);
  painter.setPen(fill.color);
  for (int i = 0; i < text.lines.size(); ++i) {
    const QString& line = text.lines.at(i);
    if (line.isEmpty()) continue;
    const qreal advance = metrics.horizontalAdvance(line) * font.scale;
    const qreal x = text.position.x() - (text.middle ? advance / 2.0 : 0.0);
    const qreal baseline = text.position.y() + yOffset +
                           (text.firstDyEm + i * text.lineHeightEm) *
                               text.fontSize;
    painter.save();
    painter.translate(x, baseline);
    painter.scale(font.scale, font.scale);
    painter.drawText(QPointF(), line);
    painter.restore();
  }
  painter.restore();
}

QBrush fillBrush(const QString& value, qreal opacity) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Fill, QColor(Qt::black));
  if (paint.none) return Qt::NoBrush;
  QColor result = paint.color;
  result.setAlphaF(result.alphaF() * opacity);
  return QBrush(result);
}

QPen strokePen(const QString& value, qreal width, qreal opacity) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Stroke, QColor(Qt::black));
  if (paint.none || !(width > 0.0) || !std::isfinite(width)) return Qt::NoPen;
  QColor result = paint.color;
  result.setAlphaF(result.alphaF() * opacity);
  QPen pen(result, width);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  return pen;
}

void paintTextNode(const VennScene& scene, const VennTextNodeGeometry& node,
                   QPainter& painter) {
  if (!node.visible || node.source.isEmpty() || !(node.fontSize > 0.0) ||
      !(node.box.width() > 0.0) || !(node.box.height() > 0.0))
    return;
  const color::SvgPaint fill = textPaint(node.color);
  if (fill.none) return;
  const editor::CssPixelFont font = textFont(
      scene, node.fontSize, node.fontFamily, node.fontWeight, node.fontStyle);
  QTextOption option;
  option.setAlignment(Qt::AlignHCenter);
  option.setWrapMode(QTextOption::WordWrap);
  QTextLayout layout(node.source, font.font);
  layout.setTextOption(option);
  layout.beginLayout();
  QVector<QTextLine> lines;
  qreal height = 0.0;
  for (;;) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(node.box.width() / font.scale);
    line.setPosition(QPointF(0.0, height));
    height += line.height();
    lines.append(line);
  }
  layout.endLayout();
  painter.save();
  painter.setOpacity(node.opacity);
  painter.setPen(fill.color);
  painter.setFont(font.font);
  const qreal top = node.box.center().y() - height * font.scale / 2.0;
  // Invalid SVG coordinate attributes use their presentation fallback without
  // invalidating the other dimensions of the foreignObject.
  painter.translate(std::isfinite(node.box.left()) ? node.box.left() : 0.0,
                    std::isfinite(top) ? top : 0.0);
  painter.scale(font.scale, font.scale);
  layout.draw(&painter, QPointF(0.0, 0.0));
  painter.restore();
}

}  // namespace

void paintVennScene(const VennScene& scene, QPainter& painter,
                    const MermaidPaintOptions&) {
  painter.save();
  painter.setClipRect(scene.bounds, Qt::IntersectClip);
  if (!scene.titleText.lines.isEmpty()) {
    VennTextGeometry title = scene.titleText;
    const editor::CssPixelFont font = textFont(scene, title.fontSize);
    const QFontMetricsF metrics(font.font);
    title.firstDyEm = metrics.xHeight() * font.scale /
                        (2.0 * title.fontSize);
    paintText(scene, title, painter);
  }
  painter.save();
  painter.translate(0.0, scene.titleHeight);
  for (const VennAreaGeometry& area : scene.areas) {
    if (area.pathVisible && area.rough && !area.roughDrawable.sets.isEmpty()) {
      const QColor roughStroke = color::toQColor(area.roughDrawable.options.stroke);
      const QColor roughFill = color::toQColor(area.roughDrawable.options.fill);
      rough::drawRoughDrawable(
          painter, area.roughDrawable, Qt::NoBrush,
          area.roughDrawable.options.stroke == QLatin1String("none")
              ? QPen(Qt::NoPen)
              : QPen(roughStroke, area.roughDrawable.options.strokeWidth),
          QPen(roughFill, area.roughDrawable.options.fillWeight));
    } else if (area.pathVisible) {
      painter.setBrush(fillBrush(area.fill, area.fillOpacity));
      painter.setPen(strokePen(area.stroke, area.strokeWidth,
                               area.strokeOpacity));
      painter.drawPath(area.path);
    }
    paintText(scene, area.label, painter);
  }
  if (scene.useDebugLayout) {
    QPen circlePen(QColor(QStringLiteral("purple")));
    for (const VennDebugCircle& circle : scene.debugCircles) {
      circlePen.setWidthF(circle.strokeWidth);
      painter.setPen(circlePen);
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(circle.center, circle.radius, circle.radius);
    }
    QPen cellPen(QColor(QStringLiteral("teal")));
    for (const VennDebugCell& cell : scene.debugCells) {
      cellPen.setWidthF(cell.strokeWidth);
      painter.setPen(cellPen);
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(cell.rect);
    }
  }
  for (const VennTextNodeGeometry& node : scene.textNodes)
    paintTextNode(scene, node, painter);
  painter.restore();
  painter.restore();
}

}  // namespace muffin::mermaid::venn
