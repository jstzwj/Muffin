#include "mermaid/sequence/SequenceScenePainter.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/theme/MermaidColor.h"

#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::sequence {
namespace {

QColor color(const QString& value) { return mermaid::color::toQColor(value); }

void includeBounds(QRectF& target, bool& initialized, const QRectF& value) {
  if (value.isNull()) return;
  QRectF normalized = value.normalized();
  if (normalized.width() <= 0.0) normalized.adjust(-0.5, 0.0, 0.5, 0.0);
  if (normalized.height() <= 0.0) normalized.adjust(0.0, -0.5, 0.0, 0.5);
  if (!initialized) {
    target = normalized;
    initialized = true;
  } else {
    target = target.united(normalized);
  }
}

QRectF participantBounds(const SequenceLayoutParticipant& actor) {
  QRectF bounds;
  bool initialized = false;
  includeBounds(bounds, initialized,
      QRectF(QPointF(actor.anchorX, actor.lifelineStartY),
             QPointF(actor.anchorX, actor.lifelineStopY)));
  if (actor.drawTop) {
    includeBounds(bounds, initialized, actor.topPaintedBounds);
    includeBounds(bounds, initialized, actor.topLabelRect);
    for (const QPainterPath& path : actor.topShapePaths)
      includeBounds(bounds, initialized, path.boundingRect());
  }
  if (actor.drawBottom) {
    includeBounds(bounds, initialized, actor.bottomPaintedBounds);
    includeBounds(bounds, initialized, actor.bottomLabelRect);
    for (const QPainterPath& path : actor.bottomShapePaths)
      includeBounds(bounds, initialized, path.boundingRect());
  }
  return bounds;
}

QRectF messageBounds(const SequenceLayoutMessage& message,
                     const SequenceLayoutNumber* number) {
  QRectF bounds;
  bool initialized = false;
  if (message.painterPath.isEmpty()) {
    includeBounds(bounds, initialized,
        QRectF(QPointF(message.startX, message.lineY),
               QPointF(message.stopX, message.lineY)));
  } else {
    includeBounds(bounds, initialized, message.painterPath.boundingRect());
  }
  includeBounds(bounds, initialized, message.labelRect);
  for (const QPointF& center : message.centralConnections)
    includeBounds(bounds, initialized,
                  QRectF(center - QPointF(5.0, 5.0), QSizeF(10.0, 10.0)));
  if (number)
    includeBounds(bounds, initialized,
                  QRectF(number->position.x() - 10.0,
                         number->position.y() - 14.0, 20.0, 20.0));
  return initialized ? bounds.adjusted(-12.0, -12.0, 12.0, 12.0) : bounds;
}

// Chrome renders short CSS dashes (stroke-dasharray 2,2 / 3,3) as crisp
// on/off segments; Qt's raster engine solidifies dash periods below ~5px, so
// a CustomDashLine pen cannot reproduce them. Paint the on-segments
// explicitly — one phase restart per edge, exactly like upstream's separate
// `<line class="loopLine">` elements (each browser line resets its dash
// phase at its own start point).
void drawDashedEdge(QPainter& painter, const QPointF& a, const QPointF& b,
                    qreal dashOn, qreal dashOff) {
  const QLineF line(a, b);
  const qreal length = line.length();
  if (length <= 0.0) return;
  const QPointF unit(line.dx() / length, line.dy() / length);
  for (qreal t = 0.0; t < length; t += dashOn + dashOff) {
    const qreal span = qMin(dashOn, length - t);
    if (span <= 0.0) break;
    painter.drawLine(a + unit * t, a + unit * (t + span));
  }
}

void centeredText(QPainter& painter, const SequenceLabelDocument& label, const QRectF& rect,
                  const SequenceSceneStyle& style, const QString& textColor,
                  flowchart::FlowLabelAlign align = flowchart::FlowLabelAlign::Center,
                  qreal alignMargin = 0.0,
                  const QString& fontFamily = {}, qreal fontSize = 0.0) {
  const QString& family = fontFamily.isEmpty() ? style.fontFamily : fontFamily;
  const qreal size = fontSize > 0.0 ? fontSize : style.fontSize;
  paintSequenceLabel(painter, label, rect, family,
                     size, size * 1.375,
                     color(textColor), true, align, alignMargin);
}

// Mermaid drawKatex() centers Math labels directly (noteText: rect center;
// message: span center) and never reads noteAlign/messageAlign. So a label
// containing Math must ignore the configured align and stay centered regardless.
flowchart::FlowLabelAlign effectiveAlign(const SequenceLabelDocument& label,
                                         flowchart::FlowLabelAlign configured) {
  return label.richText.math.isEmpty() ? configured
                                       : flowchart::FlowLabelAlign::Center;
}

void participantShape(QPainter& painter, const SequenceLayoutParticipant& actor,
                      const SequenceLabelDocument& label, bool footer,
                      const SequenceSceneStyle& style) {
  QColor strokeColor = color(style.actorStroke);
  QColor fillColor = color(style.actorFill);
  // redux-color / redux-dark-color: every actor shape rotates through
  // borderColorArray/bkgColorArray by visible-actor index. An empty bkg
  // array (redux-dark-color) keeps the .actor CSS fill (d3 removes the
  // inline override), i.e. actorFill.
  if (!style.reduxActorBorderColorArray.isEmpty() && actor.actorIndex >= 0) {
    const int slot = actor.actorIndex % style.reduxActorBorderColorArray.size();
    strokeColor = color(style.reduxActorBorderColorArray.at(slot));
    if (!style.reduxActorBkgColorArray.isEmpty())
      fillColor = color(style.reduxActorBkgColorArray.at(slot));
  }
  painter.setPen(QPen(strokeColor, style.actorStrokeWidth));
  painter.setBrush(fillColor);
  const auto& paths = footer ? actor.bottomShapePaths : actor.topShapePaths;
  const QRectF labelRect = footer ? actor.bottomLabelRect : actor.topLabelRect;
  if (actor.type == QLatin1String("actor") || actor.type == QLatin1String("boundary") ||
      actor.type == QLatin1String("control") || actor.type == QLatin1String("entity")) {
    painter.setBrush(Qt::NoBrush);
  }
  for (const QPainterPath& path : paths) painter.drawPath(path);
  centeredText(painter, label, labelRect, style, style.actorTextColor,
               flowchart::FlowLabelAlign::Center, 0.0,
               style.actorFontFamily, style.actorFontSize);
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

void paintSequenceScene(const SequenceScene& scene, QPainter& painter,
                        const MermaidPaintOptions& options) {
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);

  QHash<int, const SequenceLayoutNumber*> numbersByMessage;
  numbersByMessage.reserve(scene.sequenceNumbers.size());
  for (const SequenceLayoutNumber& number : scene.sequenceNumbers)
    numbersByMessage.insert(number.messageIndex, &number);

  for (qsizetype index = 0; index < scene.boxes.size(); ++index) {
    const auto& box = scene.boxes[index];
    if (!mermaidPrimitiveIsVisible(box.rect.united(box.labelRect), options))
      continue;
    painter.setPen(QPen(color(scene.style.boxStroke), 1.0));
    // QBrush(...) explicitly: a `cond ? Qt::NoBrush : QColor` ternary
    // resolves through QColor's converting constructor and paints SOLID
    // BLACK (color0) instead of no brush.
    painter.setBrush(box.fill == QLatin1String("transparent")
                         ? QBrush(Qt::NoBrush) : QBrush(color(box.fill)));
    painter.drawRect(box.rect);
    if (!box.label.isEmpty()) centeredText(
        painter, scene.boxLabels[index], box.labelRect, scene.style,
        scene.style.labelTextColor, flowchart::FlowLabelAlign::Center, 0.0,
        scene.style.actorFontFamily, scene.style.actorFontSize);
  }
  for (qsizetype index = 0; index < scene.participants.size(); ++index) {
    const auto& actor = scene.participants[index];
    if (!mermaidPrimitiveIsVisible(participantBounds(actor), options)) continue;
    // Lifelines are SOLID 2px (golden basic.png): the sheet's bare
    // `#id .actor-man circle, #id line { fill: ...; stroke-width: 2px }` tag
    // rule beats the 0.5px presentation attribute, and nothing declares a
    // dasharray (computed stroke-dasharray: none).
    painter.setPen(QPen(color(scene.style.lifelineColor), 2.0));
    painter.drawLine(QPointF(actor.anchorX, actor.lifelineStartY),
                     QPointF(actor.anchorX, actor.lifelineStopY));
    if (actor.drawTop) participantShape(painter, actor, scene.participantLabels[index], false, scene.style);
    if (actor.drawBottom) participantShape(painter, actor, scene.participantLabels[index], true, scene.style);
  }
  for (const auto& activation : scene.activations) {
    if (!mermaidPrimitiveIsVisible(activation.rect, options)) continue;
    QColor strokeColor = color(scene.style.activationStroke);
    QColor fillColor = color(scene.style.activationFill);
    // redux-color / redux-dark-color activation rotation; the fill falls back
    // to mainBkg when bkgColorArray is empty (redux-dark-color) — upstream's
    // `?? mainBkg` exists only on the activation site.
    if (!scene.style.reduxActorBorderColorArray.isEmpty() && activation.actorIndex >= 0) {
      const int slot =
          activation.actorIndex % scene.style.reduxActorBorderColorArray.size();
      strokeColor = color(scene.style.reduxActorBorderColorArray.at(slot));
      fillColor = scene.style.reduxActorBkgColorArray.isEmpty()
                      ? color(scene.style.reduxActivationBkgFallback)
                      : color(scene.style.reduxActorBkgColorArray.at(slot));
    }
    painter.setPen(QPen(strokeColor, 1.0));
    painter.setBrush(fillColor);
    painter.drawRect(activation.rect);
  }
  for (qsizetype index = 0; index < scene.fragments.size(); ++index) {
    const auto& fragment = scene.fragments[index];
    if (!mermaidPrimitiveIsVisible(fragment.rect, options)) continue;
    if (fragment.kind == QLatin1String("rect")) {
      // `rect <color>` is a borderless background highlight over the contained
      // messages (mermaid drawBackgroundRect). Its label IS the color spec; a
      // colorless rect falls back to the RAW themeVariables chain. There is no
      // tag box, fragment label, or section divider — unlike loop/alt/etc.
      const QString fillSpec = fragment.label.trimmed().isEmpty()
                                   ? scene.style.rectFallbackFill
                                   : fragment.label;
      painter.setPen(Qt::NoPen);
      painter.setBrush(color(fillSpec));
      painter.drawRect(fragment.rect);
      continue;
    }
    // `.loopLine` (styles.js): stroke-width 2px, stroke-dasharray 2,2 — the
    // fragment border is four DASHED lines in every theme (golden
    // nested-fragment.png: crisp 2px on / 2px off, per-edge phase restart).
    // Qt solidifies sub-5px dash periods, hence the manual segmentation; the
    // pen must be FlatCap (SVG's butt default — SquareCap would extend each
    // 2px segment by half the width on both ends and fuse the dash row).
    QPen borderPen(color(scene.style.fragmentStroke), 2.0);
    borderPen.setCapStyle(Qt::FlatCap);
    painter.setPen(borderPen);
    painter.setBrush(scene.style.fragmentFill == QLatin1String("transparent")
                         ? QBrush(Qt::NoBrush) : QBrush(color(scene.style.fragmentFill)));
    if (scene.style.fragmentFill != QLatin1String("transparent"))
      painter.fillRect(fragment.rect, color(scene.style.fragmentFill));
    drawDashedEdge(painter, fragment.rect.topLeft(), fragment.rect.topRight(), 2.0, 2.0);
    drawDashedEdge(painter, fragment.rect.topRight(), fragment.rect.bottomRight(), 2.0, 2.0);
    drawDashedEdge(painter, fragment.rect.bottomLeft(), fragment.rect.bottomRight(), 2.0, 2.0);
    drawDashedEdge(painter, fragment.rect.topLeft(), fragment.rect.bottomLeft(), 2.0, 2.0);
    const QRectF tag(fragment.rect.x(), fragment.rect.y(), 50.0, 20.0);
    painter.setPen(QPen(color(scene.style.labelStroke), 1.0));
    painter.setBrush(color(scene.style.labelFill));
    painter.drawRect(tag);
    centeredText(painter, scene.fragmentKindLabels[index], tag, scene.style,
                 scene.style.labelTextColor);
    centeredText(painter, scene.fragmentLabels[index],
                 QRectF(fragment.rect.x() + 50.0, fragment.rect.y(),
                        fragment.rect.width() - 50.0, 30.0), scene.style,
                 scene.style.loopTextColor);
    // Section separators are loopLines with an INLINE stroke-dasharray "3, 3"
    // (drawLoop) overriding the class rule; stroke-width stays 2px. Manual
    // segmentation for the same sub-5px-period reason as the border.
    QPen sectionPen(color(scene.style.fragmentStroke), 2.0);
    sectionPen.setCapStyle(Qt::FlatCap);
    painter.setPen(sectionPen);
    for (qreal y : fragment.sectionY) {
      drawDashedEdge(painter, QPointF(fragment.rect.left(), y),
                     QPointF(fragment.rect.right(), y), 3.0, 3.0);
    }
  }
  for (qsizetype index = 0; index < scene.notes.size(); ++index) {
    const auto& note = scene.notes[index];
    if (!mermaidPrimitiveIsVisible(note.rect, options)) continue;
    painter.setPen(QPen(color(scene.style.noteStroke), 1.0));
    painter.setBrush(color(scene.style.noteFill));
    painter.drawRect(note.rect);
    centeredText(painter, scene.noteLabels[index], note.rect, scene.style,
                 scene.style.noteTextColor,
                 effectiveAlign(scene.noteLabels[index], scene.style.noteAlign),
                 scene.style.noteMargin, scene.style.noteFontFamily,
                 scene.style.noteFontSize);
  }
  for (qsizetype index = 0; index < scene.messages.size(); ++index) {
    const auto& message = scene.messages[index];
    const SequenceLayoutNumber* number =
        numbersByMessage.value(message.messageIndex, nullptr);
    if (!mermaidPrimitiveIsVisible(messageBounds(message, number), options))
      continue;
    // `.messageLine0/1 { stroke-width: 1.5 }` wins the cascade over the
    // renderer's stroke-width=2 presentation attribute; dotted arrows carry an
    // INLINE stroke-dasharray "3, 3" (golden basic.png: 1.5px lines, crisp
    // 3on/3off). Straight spans are segmented manually; the curved self-loop
    // path keeps a CustomDashLine pen ({2,2} at the 1.5px width = 3/3, a
    // 6px period Qt renders correctly). FlatCap everywhere: SVG lines are
    // butt-capped, and SquareCap would lengthen each dash by half the width.
    QPen pen(color(scene.style.signalColor), 1.5);
    pen.setCapStyle(Qt::FlatCap);
    if (message.dashed) pen.setDashPattern({2.0, 2.0});
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (message.painterPath.isEmpty()) {
      if (message.dashed) {
        drawDashedEdge(painter, QPointF(message.startX, message.lineY),
                       QPointF(message.stopX, message.lineY), 3.0, 3.0);
      } else {
        painter.drawLine(QPointF(message.startX, message.lineY),
                         QPointF(message.stopX, message.lineY));
      }
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
    if (options.paintEdgeMarkers) {
      marker(painter, message.markerEnd, end, message.markerEndDirection,
             color(scene.style.signalColor));
      marker(painter, message.markerStart, QPointF(message.startX, message.lineY),
             message.markerStartDirection, color(scene.style.signalColor));
    }
    centeredText(painter, scene.messageLabels[index], message.alignRect, scene.style,
                 scene.style.signalTextColor,
                 effectiveAlign(scene.messageLabels[index], scene.style.messageAlign),
                 scene.style.wrapPadding, scene.style.messageFontFamily,
                 scene.style.messageFontSize);
    if (number) {
      painter.setPen(QPen(color(scene.style.signalColor), 1.0));
      painter.setBrush(color(scene.style.actorFill));
      painter.drawEllipse(QPointF(number->position.x(), number->position.y() - 4.0), 6.0, 6.0);
      QFont font(scene.style.fontFamily);
      font.setPixelSize(qRound(number->fontSize));
      MermaidFontRegistry::configureFont(font, scene.style.fontFamily);
      painter.setFont(font);
      painter.setPen(color(scene.style.sequenceNumberColor));
      painter.drawText(QRectF(number->position.x() - 10.0, number->position.y() - 10.0,
                              20.0, 14.0), Qt::AlignCenter, number->text);
    }
  }

  // Popups are appended last in Mermaid's SVG, above messages and actors.
  for (const SequenceSceneMenu& menu : scene.menus) {
    const bool visible = scene.forceMenus ||
        (options.openSequenceMenus &&
         options.openSequenceMenus->contains(menu.actorId));
    if (!visible ||
        !mermaidPrimitiveIsVisible(menu.panelRect, options)) {
      continue;
    }
    painter.setPen(QPen(color(scene.style.actorStroke), scene.style.actorStrokeWidth));
    painter.setBrush(color(scene.style.actorFill));
    painter.drawRect(menu.panelRect);
    for (const SequenceSceneMenuItem& item : menu.items) {
      paintSequenceLabel(
          painter, item.labelDocument, item.labelRect,
          scene.style.actorFontFamily, scene.style.actorFontSize,
          scene.style.actorFontSize * 1.375,
          color(scene.style.actorTextColor), true);
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

QRectF sequenceViewportRect(const SequenceScene& scene,
                            SequenceViewportOptions options) {
  const QRectF logical = scene.logicalBounds.isNull() ? scene.bounds
                                                       : scene.logicalBounds;
  const qreal mirrorAdjustment = options.mirrorActors
      ? options.boxMargin - options.bottomMarginAdj : 0.0;
  return QRectF(logical.left() - options.diagramMarginX,
                logical.top() - options.diagramMarginY,
                logical.width() + 2.0 * options.diagramMarginX,
                logical.height() + 2.0 * options.diagramMarginY -
                    mirrorAdjustment);
}

QImage renderSequenceSceneToImage(const SequenceScene& scene, qreal dpr,
                                  SequenceViewportOptions options) {
  const QRectF viewport = sequenceViewportRect(scene, options);
  QImage image(qCeil(viewport.width() * dpr),
               qCeil(viewport.height() * dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.scale(dpr, dpr);
  painter.translate(-viewport.left(), -viewport.top());
  paintSequenceScene(scene, painter);
  return image;
}

void SequenceScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintSequenceScene(*this, painter, options);
}

}  // namespace muffin::mermaid::sequence
