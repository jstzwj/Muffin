#include "mermaid/sequence/SequenceScenePainter.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::sequence {
namespace {

QColor color(const QString& value) { return mermaid::color::toQColor(value); }

void centeredText(QPainter& painter, const QString& text, const QRectF& rect,
                  const SequenceSceneStyle& style) {
  QFont font(style.fontFamily);
  font.setPixelSize(qRound(style.fontSize));
  MermaidFontRegistry::configureFont(font, style.fontFamily);
  painter.setFont(font);
  painter.setPen(color(style.textColor));
  painter.drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, text);
}

void participantShape(QPainter& painter, const SequenceLayoutParticipant& actor,
                      bool footer, const SequenceSceneStyle& style) {
  painter.setPen(QPen(color(style.actorStroke), 2.0));
  painter.setBrush(color(style.actorFill));
  const auto& paths = footer ? actor.bottomShapePaths : actor.topShapePaths;
  const QRectF labelRect = footer ? actor.bottomLabelRect : actor.topLabelRect;
  if (actor.type == QLatin1String("actor") || actor.type == QLatin1String("boundary") ||
      actor.type == QLatin1String("control") || actor.type == QLatin1String("entity")) {
    painter.setBrush(Qt::NoBrush);
  }
  for (const QPainterPath& path : paths) painter.drawPath(path);
  centeredText(painter, actor.label, labelRect, style);
}

void marker(QPainter& painter, const QString& type, QPointF point, QPointF direction,
            const QColor& color) {
  const qreal length = std::hypot(direction.x(), direction.y());
  if (length <= 0.0 || type.isEmpty()) return;
  const QPointF u(direction.x() / length, direction.y() / length);
  const QPointF n(-u.y(), u.x());
  painter.setPen(QPen(color, 2.0));
  auto map = [&](qreal x, qreal y, qreal refX, qreal refY) {
    return point + u * (x - refX) + n * (y - refY);
  };
  QPainterPath path;
  if (type == QLatin1String("arrowhead")) {
    painter.setBrush(color); path.moveTo(map(-1,0,7.9,5)); path.lineTo(map(10,5,7.9,5));
    path.lineTo(map(0,10,7.9,5)); path.closeSubpath();
  } else if (type == QLatin1String("filled-head")) {
    painter.setBrush(color); path.moveTo(map(18,7,15.5,7)); path.lineTo(map(9,13,15.5,7));
    path.lineTo(map(14,7,15.5,7)); path.lineTo(map(9,1,15.5,7)); path.closeSubpath();
  } else if (type == QLatin1String("crosshead")) {
    painter.setBrush(Qt::NoBrush); path.moveTo(map(1,2,4,4.5)); path.lineTo(map(6,7,4,4.5));
    path.moveTo(map(6,2,4,4.5)); path.lineTo(map(1,7,4,4.5));
  } else {
    const bool solid = type.startsWith(QLatin1String("solid"));
    const bool top = type.contains(QLatin1String("Top"));
    const qreal refX = solid ? 7.9 : 7.5, refY = top ? (solid ? 7.25 : 7.0) : (solid ? 0.75 : 0.0);
    painter.setBrush(solid ? color : Qt::NoBrush);
    path.moveTo(map(0, top ? 0 : 7, refX, refY));
    path.lineTo(map(solid ? 10 : 7, top ? 8 : 0, refX, refY));
    if (solid) { path.lineTo(map(0, top ? 8 : 0, refX, refY)); path.closeSubpath(); }
  }
  painter.drawPath(path);
}

}  // namespace

void paintSequenceScene(const SequenceScene& scene, QPainter& painter) {
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);

  for (const auto& actor : scene.participants) {
    painter.setPen(QPen(color(scene.style.lifelineColor), 0.5, Qt::DashLine));
    painter.drawLine(QPointF(actor.anchorX, actor.lifelineStartY),
                     QPointF(actor.anchorX, actor.lifelineStopY));
    participantShape(painter, actor, false, scene.style);
    participantShape(painter, actor, true, scene.style);
  }
  for (const auto& activation : scene.activations) {
    painter.setPen(QPen(color(scene.style.activationStroke), 1.0));
    painter.setBrush(color(scene.style.activationFill));
    painter.drawRect(activation.rect);
  }
  for (const auto& fragment : scene.fragments) {
    painter.setPen(QPen(color(scene.style.fragmentStroke), 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(fragment.rect);
    const QRectF tag(fragment.rect.x(), fragment.rect.y(), 50.0, 20.0);
    painter.setBrush(color(scene.style.labelFill));
    painter.drawRect(tag);
    centeredText(painter, fragment.kind, tag, scene.style);
    centeredText(painter, fragment.label,
                 QRectF(fragment.rect.x() + 50.0, fragment.rect.y(),
                        fragment.rect.width() - 50.0, 30.0), scene.style);
    QPen sectionPen(color(scene.style.fragmentStroke), 1.0, Qt::DashLine);
    painter.setPen(sectionPen);
    for (qreal y : fragment.sectionY)
      painter.drawLine(QPointF(fragment.rect.left(), y), QPointF(fragment.rect.right(), y));
  }
  for (const auto& note : scene.notes) {
    painter.setPen(QPen(color(scene.style.noteStroke), 1.0));
    painter.setBrush(color(scene.style.noteFill));
    painter.drawRect(note.rect);
    centeredText(painter, note.label, note.rect, scene.style);
  }
  for (const auto& message : scene.messages) {
    QPen pen(color(scene.style.signalColor), 2.0);
    if (message.dashed) pen.setDashPattern({3.0, 3.0});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (message.painterPath.isEmpty()) {
      painter.drawLine(QPointF(message.startX, message.lineY), QPointF(message.stopX, message.lineY));
    } else {
      painter.drawPath(message.painterPath);
    }
    for (const QPointF& center : message.centralConnections) {
      painter.setPen(QPen(color(scene.style.signalColor), 1.0));
      painter.setBrush(color(scene.style.actorFill));
      painter.drawEllipse(center, 5.0, 5.0);
    }
    const QPointF end = message.painterPath.isEmpty()
        ? QPointF(message.stopX, message.lineY) : message.painterPath.currentPosition();
    marker(painter, message.markerEnd, end, message.markerEndDirection,
           color(scene.style.signalColor));
    marker(painter, message.markerStart, QPointF(message.startX, message.lineY),
           message.markerStartDirection, color(scene.style.signalColor));
    centeredText(painter, message.label, message.labelRect, scene.style);
    const auto number = std::find_if(scene.sequenceNumbers.cbegin(), scene.sequenceNumbers.cend(),
        [&](const SequenceLayoutNumber& item) { return item.messageIndex == message.messageIndex; });
    if (number != scene.sequenceNumbers.cend()) {
      painter.setPen(QPen(color(scene.style.signalColor), 1.0));
      painter.setBrush(color(scene.style.actorFill));
      painter.drawEllipse(QPointF(number->position.x(), number->position.y() - 4.0), 6.0, 6.0);
      QFont font(scene.style.fontFamily);
      font.setPixelSize(qRound(number->fontSize));
      MermaidFontRegistry::configureFont(font, scene.style.fontFamily);
      painter.setFont(font);
      painter.setPen(color(scene.style.textColor));
      painter.drawText(QRectF(number->position.x() - 10.0, number->position.y() - 10.0,
                              20.0, 14.0), Qt::AlignCenter, number->text);
    }
  }
}

QImage renderSequenceSceneToImage(const SequenceScene& scene, qreal dpr, qreal padding) {
  const qreal width = scene.bounds.width() + 2.0 * padding;
  const qreal height = scene.bounds.height() + 2.0 * padding;
  QImage image(qCeil(width * dpr), qCeil(height * dpr), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.scale(dpr, dpr);
  painter.translate(padding - scene.bounds.left(), padding - scene.bounds.top());
  paintSequenceScene(scene, painter);
  return image;
}

}  // namespace muffin::mermaid::sequence
