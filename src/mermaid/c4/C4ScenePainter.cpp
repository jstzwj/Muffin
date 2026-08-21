#include "mermaid/c4/C4ScenePainter.h"

#include "mermaid/c4/C4Scene.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/scene/SvgStroke.h"
#include "mermaid/theme/MermaidColor.h"

#include <QByteArray>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QRegularExpression>

#include <cmath>

namespace muffin::mermaid::c4 {
namespace {

QColor paintColor(const QString& value, const QColor& fallback = Qt::black) {
  const color::SvgPaint paint = color::resolveSvgPaint(
      value, color::SvgPaintKind::Fill, fallback);
  return paint.none || !paint.color.isValid() ? fallback : paint.color;
}

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

QFont::Weight weight(const QString& value) {
  const QString lower = value.trimmed().toLower();
  if (lower == QLatin1String("bold") || lower == QLatin1String("bolder"))
    return QFont::Bold;
  bool ok = false;
  const int numeric = lower.toInt(&ok);
  return ok && numeric >= 700 ? QFont::Bold : QFont::Normal;
}

editor::CssPixelFont primitiveFont(const C4Primitive& primitive) {
  // themeCSS font declarations only ever repaint — c4 measures through the
  // config fonts — so the css slot simply overrides the inline style values.
  const qreal fontSize = primitive.css.fontSize >= 0.0
                             ? primitive.css.fontSize
                             : primitive.fontSize;
  const QString family = !primitive.css.fontFamily.trimmed().isEmpty()
                             ? primitive.css.fontFamily
                             : primitive.fontFamily;
  const QString fontWeightValue = !primitive.css.fontWeight.trimmed().isEmpty()
                                      ? primitive.css.fontWeight
                                      : primitive.fontWeight;
  bool italic = primitive.italic;
  if (!primitive.css.fontStyle.trimmed().isEmpty())
    italic = primitive.css.fontStyle.compare(QLatin1String("italic"),
                                             Qt::CaseInsensitive) == 0;
  const QStringList families = cssFamilies(family);
  editor::CssPixelFont result = editor::makeUnhintedCssPixelFont(
      families.first(), fontSize);
  if (families.size() > 1) result.font.setFamilies(families);
  result.font.setWeight(weight(fontWeightValue));
  result.font.setItalic(italic);
  return result;
}

QStringList lines(QString value) {
  static const QRegularExpression breaks(
      QStringLiteral(R"(<br\s*/?>|\r?\n)"),
      QRegularExpression::CaseInsensitiveOption);
  value.replace(breaks, QStringLiteral("\n"));
  return value.split(QLatin1Char('\n'));
}

void drawText(QPainter& painter, const C4Primitive& primitive,
              const QColor& rootFill) {
  const qreal fontSize = primitive.css.fontSize >= 0.0
                             ? primitive.css.fontSize
                             : primitive.fontSize;
  if (primitive.text.isEmpty() || !(fontSize > 0.0)) return;
  const editor::CssPixelFont font = primitiveFont(primitive);
  if (!(font.scale > 0.0)) return;
  const QFontMetricsF metrics(font.font);
  const QStringList textLines = lines(primitive.text);
  painter.save();
  painter.setFont(font.font);
  const QString fill = !primitive.css.fill.trimmed().isEmpty()
                           ? primitive.css.fill
                           : primitive.fill;
  painter.setPen(paintColor(fill, rootFill));
  painter.translate(primitive.position);
  painter.scale(font.scale, font.scale);
  for (qsizetype i = 0; i < textLines.size(); ++i) {
    const QString& text = textLines.at(i);
    const qreal advance = metrics.horizontalAdvance(text);
    qreal x = primitive.middleAnchor ? -advance / 2.0 : 0.0;
    qreal y = 0.0;
    if (primitive.mathematicalBaseline) {
      y += metrics.xHeight() / 2.0;
      y += (i * fontSize - fontSize * (textLines.size() - 1) / 2.0) /
           font.scale;
      y += primitive.textDy / font.scale;
    }
    if (primitive.forcedTextWidth > 0.0 && advance > 0.0) {
      painter.save();
      painter.translate(x, y);
      painter.scale(primitive.forcedTextWidth / (advance * font.scale), 1.0);
      painter.drawText(QPointF(0.0, 0.0), text);
      painter.restore();
    } else {
      painter.drawText(QPointF(x, y), text);
    }
  }
  painter.restore();
}

const QImage& personImage() {
  static const QImage image = QImage::fromData(QByteArray::fromBase64(
      QByteArrayLiteral("iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAIAAADYYG7QAAACD0lEQVR4Xu2YoU4EMRCGT+4j8Ai8AhaH4QHgAUjQuFMECUgMIUgwJAgMhgQsAYUiJCiQIBBY+EITsjfTdme6V24v4c8vyGbb+ZjOtN0bNcvjQXmkH83WvYBWto6PLm6v7p7uH1/w2fXD+PBycX1Pv2l3IdDm/vn7x+dXQiAubRzoURa7gRZWd0iGRIiJbOnhnfYBQZNJjNbuyY2eJG8fkDE3bbG4ep6MHUAsgYxmE3nVs6VsBWJSGccsOlFPmLIViMzLOB7pCVO2AtHJMohH7Fh6zqitQK7m0rJvAVYgGcEpe//PLdDz65sM4pF9N7ICcXDKIB5Nv6j7tD0NoSdM2QrU9Gg0ewE1LqBhHR3BBdvj2vapnidjHxD/q6vd7Pvhr31AwcY8eXMTXAKECZZJFXuEq27aLgQK5uLMohCenGGuGewOxSjBvYBqeG6B+Nqiblggdjnc+ZXDy+FNFpFzw76O3UBAROuXh6FoiAcf5g9eTvUgzy0nWg6I8cXHRUpg5bOVBCo+KDpFajOf23GgPme7RSQ+lacIENUgJ6gg1k6HjgOlqnLqip4tEuhv0hNEMXUD0clyXE3p6pZA0S2nnvTlXwLJEZWlb7cTQH1+USgTN4VhAenm/wea1OCAOmqo6fE1WCb9WSKBah+rbUWPWAmE2Rvk0ApiB45eOyNAzU8xcTvj8KvkKEoOaIYeHNA3ZuygAvFMUO0AAAAASUVORK5CYII=")));
  return image;
}

void drawArrow(QPainter& painter, const QPointF& point, qreal angle,
               bool start, const QColor& color) {
  painter.save();
  painter.translate(point);
  painter.rotate(angle * 180.0 / M_PI);
  QPolygonF polygon;
  if (start)
    polygon << QPointF(9, -5) << QPointF(-1, 0) << QPointF(9, 5);
  else
    polygon << QPointF(-9, -5) << QPointF(1, 0) << QPointF(-9, 5);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawPolygon(polygon);
  painter.restore();
}

void drawMarkers(QPainter& painter, const C4Primitive& primitive,
                 const QPointF& start, const QPointF& end) {
  const qreal angle = std::atan2(end.y() - start.y(), end.x() - start.x());
  // Arrowhead/arrowend marker paths carry no fill attribute upstream and
  // inherit the svg root fill (textColor), not the line stroke.
  const QColor color = paintColor(primitive.markerFill.isEmpty()
                                      ? primitive.stroke
                                      : primitive.markerFill);
  if (primitive.markerStart) drawArrow(painter, start, angle, true, color);
  if (primitive.markerEnd) drawArrow(painter, end, angle, false, color);
}

}  // namespace

void paintC4Scene(const C4Scene& scene, QPainter& painter,
                  const MermaidPaintOptions&) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  // Mermaid paints `#id { fill: textColor }` on the svg root; c4's own
  // elements resolve fill keywords against it, while stroke inheritance
  // lands on the root's initial `none`.
  const QColor rootFill = color::resolveSvgPaint(
      scene.style.rootTextColor, color::SvgPaintKind::Fill,
      QColor(Qt::black)).color;
  // themeCSS stroke-width resolves like any CSS length: em/ex against the
  // root font, percentages against the viewBox diagonal.
  CssLengthContext lengthContext = editor::pieCssLengthContext(
      scene.style.rootFontFamily, scene.style.rootFontSize);
  lengthContext.viewportPx = scene.bounds.size();
  const qreal diagonal = std::hypot(scene.bounds.width(),
                                    scene.bounds.height()) / std::sqrt(2.0);
  for (const C4Primitive& primitive : scene.primitives) {
    if (!primitive.css.visible) continue;
    const qreal opacity = primitive.css.opacity >= 0.0 ? primitive.css.opacity
                                                       : 1.0;
    painter.save();
    painter.setOpacity(opacity);
    const QString strokeValue = !primitive.css.stroke.trimmed().isEmpty()
                                    ? primitive.css.stroke
                                    : primitive.stroke;
    const QString fillValue = !primitive.css.fill.trimmed().isEmpty()
                                  ? primitive.css.fill
                                  : primitive.fill;
    const qreal strokeWidth = !primitive.css.strokeWidth.trimmed().isEmpty()
        ? editor::cssStrokeWidthPx(primitive.css.strokeWidth, lengthContext,
                                   diagonal)
        : primitive.strokeWidth;
    const color::SvgPaint strokePaint = color::resolveSvgPaint(
        strokeValue, color::SvgPaintKind::Stroke, {});
    QPen pen = (strokePaint.none || !strokePaint.color.isValid() ||
                !(strokeWidth > 0.0))
                   ? QPen(Qt::NoPen)
                   : QPen(strokePaint.color, strokeWidth);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    if (!primitive.dash.isEmpty())
      pen.setDashPattern(scene::normalizedSvgDashPattern(
          primitive.dash, strokeWidth));
    painter.setPen(pen);
    const color::SvgPaint fillPaint = color::resolveSvgPaint(
        fillValue, color::SvgPaintKind::Fill, rootFill);
    painter.setBrush(fillPaint.none || !fillPaint.color.isValid()
                         ? QBrush(Qt::NoBrush)
                         : QBrush(fillPaint.color));
    switch (primitive.kind) {
      case C4PrimitiveKind::Rect:
        painter.drawRoundedRect(primitive.rect, primitive.rx, primitive.rx);
        break;
      case C4PrimitiveKind::Path:
        painter.drawPath(primitive.path);
        if (primitive.markerStart || primitive.markerEnd) {
          const QPointF start = primitive.path.pointAtPercent(0.0);
          const QPointF end = primitive.path.pointAtPercent(1.0);
          drawMarkers(painter, primitive, start, end);
        }
        break;
      case C4PrimitiveKind::Line:
        painter.drawLine(primitive.line);
        drawMarkers(painter, primitive, primitive.line.p1(), primitive.line.p2());
        break;
      case C4PrimitiveKind::Text:
        drawText(painter, primitive, rootFill);
        break;
      case C4PrimitiveKind::Image:
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(primitive.rect, personImage());
        break;
    }
    painter.restore();
  }
}

}  // namespace muffin::mermaid::c4
