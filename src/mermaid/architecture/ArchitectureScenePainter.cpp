#include "mermaid/architecture/ArchitectureScenePainter.h"

#include "mermaid/architecture/ArchitectureScene.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>

#include <cmath>

namespace muffin::mermaid::architecture {
namespace {

QString visibleText(QString text) {
  text.replace(QRegularExpression(QStringLiteral(R"([\t\n\r\f ]+)")),
               QStringLiteral(" "));
  return text.trimmed();
}

QStringList fontFamilies(const QString& expression) {
  QStringList result;
  for (QString item : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    item = item.trimmed();
    if (item.size() >= 2 &&
        ((item.front() == QLatin1Char('"') && item.back() == QLatin1Char('"')) ||
         (item.front() == QLatin1Char('\'') && item.back() == QLatin1Char('\''))))
      item = item.mid(1, item.size() - 2);
    if (!item.isEmpty()) result.append(item);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

qreal configNumber(const QJsonValue& value, qreal fallback) {
  const double result = editor::jsNumberValue(value);
  return std::isfinite(result) ? result : fallback;
}

QFont cssFont(const ArchitectureScene& scene, qreal size) {
  const QStringList families = fontFamilies(scene.style.fontFamily);
  auto css = editor::makeUnhintedCssPixelFont(families.first(), size);
  if (families.size() > 1) css.font.setFamilies(families);
  return css.font;
}

void drawText(QPainter& painter, const ArchitectureScene& scene,
              const QString& source, const QPointF& anchor, qreal size,
              Qt::Alignment alignment = Qt::AlignHCenter,
              qreal rotation = 0.0, const QColor& color = Qt::black) {
  const QString text = visibleText(source);
  if (text.isEmpty() || !(size > 0.0)) return;
  const QStringList families = fontFamilies(scene.style.fontFamily);
  auto font = editor::makeUnhintedCssPixelFont(families.first(), size);
  if (families.size() > 1) font.font.setFamilies(families);
  const qreal advance = QFontMetricsF(font.font).horizontalAdvance(text) * font.scale;
  const auto metrics = flowchart::flowLabelFontBoundingMetrics(
      scene.style.fontFamily, size, QFont::Normal, QFont::StyleNormal);
  qreal x = 0.0;
  if (alignment.testFlag(Qt::AlignHCenter)) x = -advance / 2.0;
  else if (alignment.testFlag(Qt::AlignRight)) x = -advance;
  painter.save();
  painter.translate(anchor);
  painter.rotate(rotation);
  painter.scale(font.scale, font.scale);
  painter.setFont(font.font);
  painter.setPen(color);
  painter.drawText(QPointF(x / font.scale, metrics.ascent / font.scale), text);
  painter.restore();
}

QPen whiteIconPen(qreal scale) {
  QPen pen(Qt::white, 2.0 * scale);
  pen.setJoinStyle(Qt::MiterJoin);
  pen.setCapStyle(Qt::FlatCap);
  return pen;
}

void drawIcon80(QPainter& painter, const QString& icon, const QString& iconText,
                qreal size, const ArchitectureScene& scene) {
  const qreal scale = size / 80.0;
  painter.save();
  painter.scale(scale, scale);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(QStringLiteral("#087ebf")));
  painter.drawRect(QRectF(0, 0, 80, 80));
  painter.setBrush(Qt::NoBrush);
  painter.setPen(whiteIconPen(1.0));
  if (icon == QLatin1String("database")) {
    painter.drawEllipse(QRectF(20, 15, 40, 14.28));
    painter.drawLine(QPointF(20, 22.14), QPointF(20, 57.86));
    painter.drawLine(QPointF(60, 22.14), QPointF(60, 57.86));
    for (qreal y : {34.05, 45.95, 57.86})
      painter.drawArc(QRectF(20, y - 7.14, 40, 14.28), 180 * 16, 180 * 16);
  } else if (icon == QLatin1String("server")) {
    painter.drawRoundedRect(QRectF(17.5, 17.5, 45, 45), 2, 2);
    painter.drawLine(QPointF(17.5, 32.5), QPointF(62.5, 32.5));
    painter.drawLine(QPointF(17.5, 47.5), QPointF(62.5, 47.5));
    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    for (qreal y : {25.0, 40.0, 55.0})
      for (qreal x : {22.5, 27.5, 32.5})
        painter.drawEllipse(QPointF(x, y), .75, .75);
  } else if (icon == QLatin1String("disk")) {
    painter.drawRoundedRect(QRectF(20, 15, 40, 50), 1, 1);
    painter.drawEllipse(QRectF(26, 19.17, 28, 29.16));
    painter.drawEllipse(QRectF(36, 29.58, 8, 8.34));
  } else if (icon == QLatin1String("internet")) {
    painter.drawEllipse(QPointF(40, 40), 22.5, 22.5);
    painter.drawLine(QPointF(40, 17.5), QPointF(40, 62.5));
    painter.drawLine(QPointF(17.5, 40), QPointF(62.5, 40));
    QPainterPath left;
    left.moveTo(39.99, 17.51);
    left.cubicTo(24.71, 28.61, 24.71, 51.39, 39.99, 62.49);
    QPainterPath right;
    right.moveTo(40.01, 17.51);
    right.cubicTo(55.29, 28.61, 55.29, 51.39, 40.01, 62.49);
    painter.drawPath(left);
    painter.drawPath(right);
    painter.drawLine(QPointF(19.75, 30.1), QPointF(60.25, 30.1));
    painter.drawLine(QPointF(19.75, 49.9), QPointF(60.25, 49.9));
  } else if (icon == QLatin1String("cloud")) {
    QPainterPath path;
    path.moveTo(65, 47.5);
    path.cubicTo(65, 50.26, 62.76, 52.5, 60, 52.5);
    path.lineTo(20, 52.5);
    path.cubicTo(17.24, 52.5, 15, 50.26, 15, 47.5);
    path.cubicTo(15, 45.63, 16.03, 43.99, 17.56, 43.14);
    path.cubicTo(17.52, 42.93, 17.5, 42.72, 17.5, 42.5);
    path.cubicTo(17.5, 39.9, 19.98, 37.76, 23.15, 37.53);
    path.cubicTo(24.8, 33.02, 29.49, 29.77, 35, 29.77);
    path.cubicTo(35.86, 29.77, 36.69, 29.85, 37.5, 30);
    path.cubicTo(39.59, 28.43, 42.19, 27.5, 45, 27.5);
    path.cubicTo(51.1, 27.5, 56.19, 31.88, 57.28, 37.67);
    path.cubicTo(59.42, 38.23, 61, 40.18, 61, 42.5);
    path.cubicTo(61, 42.53, 61, 42.57, 60.99, 42.6);
    path.cubicTo(63.28, 43.06, 65, 45.08, 65, 47.5);
    painter.drawPath(path);
  } else if (iconText.isEmpty() && icon.isEmpty()) {
    // blank icon background only
  } else if (!iconText.isEmpty()) {
    painter.restore();
    drawText(painter, scene, iconText, QPointF(size / 2.0, size / 2.0 - 8.0),
             configNumber(scene.config.fontSize, 16.0), Qt::AlignHCenter,
             0.0, Qt::white);
    return;
  } else {
    drawText(painter, scene, QStringLiteral("?"), QPointF(40, 24), 32,
             Qt::AlignHCenter, 0, Qt::white);
  }
  painter.restore();
}

QColor textColor(const ArchitectureScene& scene) {
  const auto paint = color::resolveSvgPaint(
      scene.style.textColor, color::SvgPaintKind::Text, Qt::black);
  return paint.none ? QColor(Qt::transparent) : paint.color;
}

}  // namespace

void paintArchitectureScene(const ArchitectureScene& scene, QPainter& painter,
                            const MermaidPaintOptions&) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const qreal fontSize = configNumber(scene.config.fontSize, 16.0);
  const qreal iconSize = configNumber(scene.config.iconSize, 80.0);
  const qreal padding = configNumber(scene.config.padding, 40.0);
  const auto lengthContext = editor::pieCssLengthContext(
      scene.style.fontFamily, fontSize);
  const qreal diagonal = std::hypot(scene.bounds.width(), scene.bounds.height()) /
                         std::sqrt(2.0);
  const qreal edgeWidth = editor::cssStrokeWidthPx(
      scene.style.edgeWidth, lengthContext, diagonal);
  const qreal groupWidth = editor::cssStrokeWidthPx(
      scene.style.groupBorderWidth, lengthContext, diagonal);
  const auto edgePaint = color::resolveSvgPaint(
      scene.style.edgeColor, color::SvgPaintKind::Stroke, Qt::black);
  const auto arrowPaint = color::resolveSvgPaint(
      scene.style.arrowColor, color::SvgPaintKind::Fill, Qt::black);
  const auto groupPaint = color::resolveSvgPaint(
      scene.style.groupBorderColor, color::SvgPaintKind::Stroke, Qt::black);

  // DOM order is edges, services/junctions, then group rectangles and labels.
  if (!edgePaint.none && edgeWidth > 0.0) {
    QPen pen(edgePaint.color, edgeWidth);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (const ArchitectureEdgeGeometry& edge : scene.edges) {
      QPainterPath path;
      if (!edge.points.isEmpty()) path.moveTo(edge.points.front());
      for (qsizetype i = 1; i < edge.points.size(); ++i) path.lineTo(edge.points.at(i));
      painter.drawPath(path);
      if (!edge.title.isEmpty() && edge.points.size() == 3) {
        const QPointF middle = edge.points.at(1);
        const QPointF first = edge.points.front();
        const QPointF last = edge.points.back();
        qreal rotation = 0.0;
        if (qFuzzyCompare(first.x(), last.x())) rotation = -90.0;
        else if (!qFuzzyCompare(first.y(), last.y()))
          rotation = (last.y() - first.y()) * (last.x() - first.x()) < 0 ? 45.0 : -45.0;
        drawText(painter, scene, edge.title, middle, fontSize,
                 Qt::AlignHCenter, rotation, textColor(scene));
      }
    }
  }
  if (!arrowPaint.none) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(arrowPaint.color);
    for (const ArchitectureEdgeGeometry& edge : scene.edges)
      for (const ArchitectureArrowGeometry& arrow : edge.arrows)
        painter.drawPolygon(arrow.polygon.translated(arrow.position));
  }

  for (const ArchitectureNodeGeometry& node : scene.nodes) {
    painter.save();
    painter.translate(node.topLeft);
    if (node.kind == ArchitectureNodeKind::Service) {
      if (!node.title.isEmpty())
        drawText(painter, scene, node.title,
                 QPointF(iconSize / 2.0, iconSize + fontSize * 0.205),
                 fontSize, Qt::AlignHCenter, 0.0, textColor(scene));
      if (!node.icon.isEmpty() || !node.iconText.isEmpty())
        drawIcon80(painter, node.icon, node.iconText, iconSize, scene);
      else {
        QPainterPath box;
        box.moveTo(0, iconSize);
        box.lineTo(0, 5);
        box.quadTo(0, 0, 5, 0);
        box.lineTo(iconSize - 5, 0);
        box.quadTo(iconSize, 0, iconSize, 5);
        box.lineTo(iconSize, iconSize);
        box.closeSubpath();
        QPen pen(groupPaint.none ? QColor(Qt::transparent) : groupPaint.color,
                 groupWidth);
        pen.setDashPattern({8.0 / std::max(groupWidth, .01),
                            8.0 / std::max(groupWidth, .01)});
        painter.setPen(groupPaint.none ? Qt::NoPen : pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(box);
      }
    }
    painter.restore();
  }

  if (!groupPaint.none && groupWidth > 0.0) {
    QPen pen(groupPaint.color, groupWidth);
    pen.setDashPattern({8.0 / groupWidth, 8.0 / groupWidth});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (const ArchitectureGroupGeometry& group : scene.groups)
      painter.drawRect(group.rect);
  }
  const qreal groupIconSize = padding * 0.75;
  for (const ArchitectureGroupGeometry& group : scene.groups) {
    qreal x = group.rect.left() + 1.0;
    qreal y = group.rect.top() + 1.0;
    if (!group.icon.isEmpty()) {
      painter.save();
      painter.translate(x, y);
      drawIcon80(painter, group.icon, {}, groupIconSize, scene);
      painter.restore();
      x += groupIconSize;
      y += fontSize / 2.0 - 3.0;
    }
    if (!group.title.isEmpty())
      drawText(painter, scene, group.title, QPointF(x + 4.0, y + 2.0),
               fontSize, Qt::AlignLeft, 0.0, textColor(scene));
  }
  painter.restore();
}

}  // namespace muffin::mermaid::architecture
