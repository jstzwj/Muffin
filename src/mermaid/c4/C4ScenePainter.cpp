#include "mermaid/c4/C4ScenePainter.h"

#include "mermaid/c4/C4Scene.h"
#include "mermaid/editor/MermaidRenderSupport.h"
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
  const QStringList families = cssFamilies(primitive.fontFamily);
  editor::CssPixelFont result = editor::makeUnhintedCssPixelFont(
      families.first(), primitive.fontSize);
  if (families.size() > 1) result.font.setFamilies(families);
  result.font.setWeight(weight(primitive.fontWeight));
  result.font.setItalic(primitive.italic);
  return result;
}

QStringList lines(QString value) {
  static const QRegularExpression breaks(
      QStringLiteral(R"(<br\s*/?>|\r?\n)"),
      QRegularExpression::CaseInsensitiveOption);
  value.replace(breaks, QStringLiteral("\n"));
  return value.split(QLatin1Char('\n'));
}

void drawText(QPainter& painter, const C4Primitive& primitive) {
  if (primitive.text.isEmpty() || !(primitive.fontSize > 0.0)) return;
  const editor::CssPixelFont font = primitiveFont(primitive);
  if (!(font.scale > 0.0)) return;
  const QFontMetricsF metrics(font.font);
  const QStringList textLines = lines(primitive.text);
  painter.save();
  painter.setFont(font.font);
  painter.setPen(paintColor(primitive.fill));
  painter.translate(primitive.position);
  painter.scale(font.scale, font.scale);
  for (qsizetype i = 0; i < textLines.size(); ++i) {
    const QString& text = textLines.at(i);
    const qreal advance = metrics.horizontalAdvance(text);
    qreal x = primitive.middleAnchor ? -advance / 2.0 : 0.0;
    qreal y = 0.0;
    if (primitive.mathematicalBaseline) {
      y += metrics.xHeight() / 2.0;
      y += (i * primitive.fontSize -
            primitive.fontSize * (textLines.size() - 1) / 2.0) /
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
  const QColor color = paintColor(primitive.stroke);
  if (primitive.markerStart) drawArrow(painter, start, angle, true, color);
  if (primitive.markerEnd) drawArrow(painter, end, angle, false, color);
}

}  // namespace

void paintC4Scene(const C4Scene& scene, QPainter& painter,
                  const MermaidPaintOptions&) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  for (const C4Primitive& primitive : scene.primitives) {
    painter.save();
    QPen pen = primitive.stroke == QLatin1String("none")
                   ? QPen(Qt::NoPen)
                   : QPen(paintColor(primitive.stroke), primitive.strokeWidth);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    if (!primitive.dash.isEmpty()) pen.setDashPattern(primitive.dash);
    painter.setPen(pen);
    painter.setBrush(primitive.fill == QLatin1String("none")
                         ? QBrush(Qt::NoBrush)
                         : QBrush(paintColor(primitive.fill)));
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
        drawText(painter, primitive);
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
