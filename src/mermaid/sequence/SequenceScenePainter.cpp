#include "mermaid/sequence/SequenceScenePainter.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

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
                      qreal y, const SequenceSceneStyle& style) {
  const QRectF rect(actor.logicalRect.x(), y, actor.logicalRect.width(),
                    actor.logicalRect.height());
  painter.setPen(QPen(color(style.actorStroke), 2.0));
  painter.setBrush(color(style.actorFill));
  const qreal cx = actor.anchorX;
  const QString type = actor.type;
  if (type == QLatin1String("actor")) {
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, y + 10), 15, 15);
    painter.drawLine(QPointF(cx, y + 25), QPointF(cx, y + 45));
    painter.drawLine(QPointF(cx - 18, y + 33), QPointF(cx + 18, y + 33));
    painter.drawLine(QPointF(cx, y + 45), QPointF(cx - 18, y + 60));
    painter.drawLine(QPointF(cx, y + 45), QPointF(cx + 16, y + 60));
    centeredText(painter, actor.label, QRectF(rect.x(), y + 35, rect.width(), 48), style);
  } else if (type == QLatin1String("collections")) {
    painter.drawRoundedRect(rect.translated(-6, 6), 3, 3);
    painter.drawRoundedRect(rect, 3, 3);
    centeredText(painter, actor.label, rect.translated(-6, 6), style);
  } else if (type == QLatin1String("database")) {
    QPainterPath path;
    path.moveTo(rect.left(), y + 10);
    path.cubicTo(rect.left(), y - 3, rect.right(), y - 3, rect.right(), y + 10);
    path.lineTo(rect.right(), rect.bottom() - 10);
    path.cubicTo(rect.right(), rect.bottom() + 3, rect.left(), rect.bottom() + 3, rect.left(), rect.bottom() - 10);
    path.closeSubpath();
    painter.drawPath(path);
    painter.drawEllipse(QRectF(rect.left(), y, rect.width(), 20));
    centeredText(painter, actor.label, rect, style);
  } else if (type == QLatin1String("control")) {
    painter.drawEllipse(QPointF(cx, y + 24), 20, 20);
    painter.drawLine(QPointF(cx, y + 4), QPointF(cx + 9, y - 4));
    centeredText(painter, actor.label, QRectF(rect.x(), y + 42, rect.width(), 25), style);
  } else if (type == QLatin1String("boundary")) {
    painter.drawEllipse(QPointF(cx, y + 24), 20, 20);
    painter.drawLine(QPointF(cx - 20, y + 4), QPointF(cx - 20, y + 44));
    painter.drawLine(QPointF(cx - 38, y + 24), QPointF(cx - 20, y + 24));
    centeredText(painter, actor.label, QRectF(rect.x(), y + 42, rect.width(), 25), style);
  } else if (type == QLatin1String("entity")) {
    painter.drawEllipse(QPointF(cx, y + 24), 20, 20);
    painter.drawLine(QPointF(cx - 20, y + 47), QPointF(cx + 20, y + 47));
    centeredText(painter, actor.label, QRectF(rect.x(), y + 48, rect.width(), 20), style);
  } else if (type == QLatin1String("queue")) {
    painter.drawRoundedRect(rect, 16, 16);
    painter.drawArc(QRectF(rect.right() - 25, y, 25, rect.height()), 90 * 16, 180 * 16);
    centeredText(painter, actor.label, rect, style);
  } else {
    painter.drawRoundedRect(rect, 3, 3);
    centeredText(painter, actor.label, rect, style);
  }
}

void marker(QPainter& painter, const QString& type, QPointF point, QPointF direction,
            const QColor& color) {
  const qreal length = std::hypot(direction.x(), direction.y());
  if (length <= 0.0 || type.isEmpty()) return;
  const QPointF u(direction.x() / length, direction.y() / length);
  const QPointF n(-u.y(), u.x());
  painter.setPen(QPen(color, 2.0));
  painter.setBrush(type == QLatin1String("arrow") || type == QLatin1String("point") ? color : Qt::NoBrush);
  if (type == QLatin1String("cross")) {
    painter.drawLine(point - u * 7 + n * 5, point + u * 3 - n * 5);
    painter.drawLine(point - u * 7 - n * 5, point + u * 3 + n * 5);
  } else if (type == QLatin1String("point")) {
    painter.drawEllipse(point - u * 5, 4, 4);
  } else {
    QPolygonF head{point, point - u * 10 + n * 5, point - u * 10 - n * 5};
    if (type == QLatin1String("arrow")) painter.drawPolygon(head);
    else painter.drawPolyline(head);
  }
}

}  // namespace

void paintSequenceScene(const SequenceScene& scene, QPainter& painter) {
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);

  for (const auto& actor : scene.participants) {
    painter.setPen(QPen(color(scene.style.lifelineColor), 0.5, Qt::DashLine));
    painter.drawLine(QPointF(actor.anchorX, actor.lifelineStartY),
                     QPointF(actor.anchorX, actor.lifelineStopY));
    participantShape(painter, actor, 0.0, scene.style);
    participantShape(painter, actor, actor.lifelineStopY, scene.style);
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
    if (message.path.isEmpty()) {
      painter.drawLine(QPointF(message.startX, message.lineY), QPointF(message.stopX, message.lineY));
    } else {
      QPainterPath path(QPointF(message.startX, message.lineY));
      path.cubicTo(message.startX + 60, message.lineY - 10,
                   message.startX + 60, message.lineY + 30,
                   message.startX, message.lineY + 20);
      painter.drawPath(path);
    }
    const QPointF end(message.stopX, message.path.isEmpty() ? message.lineY : message.lineY + 20);
    marker(painter, message.markerEnd, end,
           message.path.isEmpty() ? QPointF(message.stopX - message.startX, 0) : QPointF(-60, -10),
           color(scene.style.signalColor));
    marker(painter, message.markerStart, QPointF(message.startX, message.lineY),
           QPointF(message.startX - message.stopX, 0), color(scene.style.signalColor));
    centeredText(painter, message.label, message.labelRect, scene.style);
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
