#include "mermaid/sequence/SequenceLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QSet>

namespace muffin::mermaid::sequence {
namespace {

constexpr int kNote = 2;
constexpr int kLoopStart = 10;
constexpr int kLoopEnd = 11;
constexpr int kAltStart = 12;
constexpr int kAltEnd = 14;
constexpr int kOptStart = 15;
constexpr int kOptEnd = 16;
constexpr int kActivationStart = 17;
constexpr int kActivationEnd = 18;
constexpr int kParStart = 19;
constexpr int kParEnd = 21;
constexpr int kRectStart = 22;
constexpr int kRectEnd = 23;
constexpr int kAutonumber = 26;
constexpr int kCriticalStart = 27;
constexpr int kCriticalEnd = 29;
constexpr int kBreakStart = 30;
constexpr int kBreakEnd = 31;
constexpr int kParOverStart = 32;
constexpr int kCentralConnection = 59;
constexpr int kCentralConnectionReverse = 60;
constexpr int kCentralConnectionDual = 61;

bool isSignal(int type) {
  return (type >= 0 && type <= 6 && type != kNote) || type == 24 || type == 25 ||
         type == 33 || type == 34 || (type >= 41 && type <= 58);
}

QString fragmentKind(int type) {
  switch (type) {
    case kLoopStart: return QStringLiteral("loop");
    case kAltStart: return QStringLiteral("alt");
    case kOptStart: return QStringLiteral("opt");
    case kParStart: return QStringLiteral("par");
    case kRectStart: return QStringLiteral("rect");
    case kCriticalStart: return QStringLiteral("critical");
    case kBreakStart: return QStringLiteral("break");
    case kParOverStart: return QStringLiteral("par_over");
    default: return {};
  }
}

bool isFragmentEnd(int type) {
  return type == kLoopEnd || type == kAltEnd || type == kOptEnd || type == kParEnd ||
         type == kRectEnd || type == kCriticalEnd || type == kBreakEnd;
}

QString messageText(const QJsonValue& value) {
  if (value.isString()) return value.toString();
  return value.toVariant().toString();
}

QSizeF atOrEmpty(const QVector<QSizeF>& values, qsizetype index) {
  return index >= 0 && index < values.size() ? values.at(index) : QSizeF{};
}

QSizeF measuredAt(const QMap<int, QSizeF>& indexed, int messageIndex,
                  const QVector<QSizeF>& ordered, qsizetype orderedIndex) {
  return indexed.contains(messageIndex) ? indexed.value(messageIndex)
                                        : atOrEmpty(ordered, orderedIndex);
}

}  // namespace

SequenceLayoutInput buildSequenceLayoutInput(const SequenceData& data,
                                             const SequenceLayoutMeasurements& measurements) {
  SequenceLayoutInput result;
  result.participants.reserve(data.actors.size());
  for (qsizetype index = 0; index < data.actors.size(); ++index) {
    const SequenceActor& actor = data.actors.at(index);
    result.participants.append({static_cast<int>(index), actor.id, actor.description,
                                actor.type, measurements.participants.value(actor.id)});
  }

  QMap<QString, int> activationDepth;
  int fragmentDepth = 0;
  qsizetype signalIndex = 0, noteIndex = 0, fragmentIndex = 0;
  for (qsizetype index = 0; index < data.messages.size(); ++index) {
    const SequenceMessage& message = data.messages.at(index);
    if (message.type == kActivationStart || message.type == kActivationEnd) {
      const bool begin = message.type == kActivationStart;
      int depth = activationDepth.value(message.from);
      if (begin) ++depth;
      result.activations.append({static_cast<int>(index), message.from, depth, begin});
      activationDepth[message.from] = std::max(0, depth - (begin ? 0 : 1));
      result.maximumActivationDepth = std::max(result.maximumActivationDepth, depth);
      continue;
    }
    if (message.type == kNote) {
      result.notes.append({static_cast<int>(index), message.from, message.to,
                           messageText(message.message), message.placement,
                           measuredAt(measurements.notesByIndex, static_cast<int>(index),
                                      measurements.notes, noteIndex++)});
      continue;
    }
    const QString kind = fragmentKind(message.type);
    if (!kind.isEmpty()) {
      ++fragmentDepth;
      result.maximumFragmentDepth = std::max(result.maximumFragmentDepth, fragmentDepth);
      result.fragments.append({static_cast<int>(index), kind, messageText(message.message),
                               fragmentDepth,
                               measuredAt(measurements.fragmentsByIndex, static_cast<int>(index),
                                          measurements.fragments, fragmentIndex++)});
      continue;
    }
    if (isFragmentEnd(message.type)) {
      fragmentDepth = std::max(0, fragmentDepth - 1);
      continue;
    }
    if (isSignal(message.type)) {
      result.messages.append({static_cast<int>(index), message.from, message.to,
                              messageText(message.message), message.type,
                              measuredAt(measurements.messagesByIndex, static_cast<int>(index),
                                         measurements.messages, signalIndex++)});
    }
  }
  return result;
}

namespace {

struct OpenActivation {
  int messageIndex = -1;
  QString actor;
  int depth = 0;
  qreal startX = 0.0;
  qreal stopX = 0.0;
  qreal startY = 0.0;
};

struct OpenFragment {
  int messageIndex = -1;
  QString kind;
  QString label;
  int depth = 0;
  qreal startX = std::numeric_limits<qreal>::infinity();
  qreal stopX = -std::numeric_limits<qreal>::infinity();
  qreal startY = 0.0;
  qreal stopY = 0.0;
  QVector<qreal> sections;
};

struct PlacementBounds {
  qreal boxMargin = 10.0;
  qreal vertical = 0.0;
  QRectF all;
  bool hasBounds = false;
  QVector<OpenFragment> fragments;

  void insert(qreal x1, qreal y1, qreal x2, qreal y2) {
    const QRectF rect(QPointF(std::min(x1, x2), std::min(y1, y2)),
                      QPointF(std::max(x1, x2), std::max(y1, y2)));
    all = hasBounds ? all.united(rect) : rect;
    hasBounds = true;
    for (qsizetype index = 0; index < fragments.size(); ++index) {
      OpenFragment& fragment = fragments[index];
      const qreal nesting = fragments.size() - index;
      const qreal margin = nesting * boxMargin;
      fragment.startX = std::min(fragment.startX, rect.left() - margin);
      fragment.stopX = std::max(fragment.stopX, rect.right() + margin);
      fragment.startY = std::min(fragment.startY, rect.top() - margin);
      fragment.stopY = std::max(fragment.stopY, rect.bottom() + margin);
      all = all.united(QRectF(QPointF(rect.left() - margin, rect.top() - margin),
                              QPointF(rect.right() + margin, rect.bottom() + margin)));
    }
  }
};

bool isOpenArrow(int type) { return type == 5 || type == 6; }
bool isStickOrOpenArrow(int type) {
  return isOpenArrow(type) || type == 43 || type == 44 || type == 47 || type == 48 ||
         type == 53 || type == 54 || type == 55 || type == 56 || type == 57 || type == 58 ||
         type == 45 || type == 46;
}
bool adjustsStartForMarker(int type) {
  return type == 33 || type == 34 || type == 45 || type == 46 || type == 55 || type == 56;
}

QString selfMessagePath(qreal startX, qreal endX, qreal y, bool rightAngles) {
  if (rightAngles)
    return QStringLiteral("M  %1,%2 H %3 V %4 H %5")
        .arg(startX).arg(y).arg(endX + 75.0).arg(y + 25.0).arg(endX);
  return QStringLiteral("M %1,%2 C %3,%4 %5,%6 %7,%8")
      .arg(startX).arg(y).arg(startX + 60.0).arg(y - 10.0)
      .arg(endX + 60.0).arg(y + 30.0).arg(endX).arg(y + 20.0);
}

QPainterPath selfPainterPath(qreal startX, qreal endX, qreal y, bool rightAngles) {
  QPainterPath path(QPointF(startX, y));
  if (rightAngles) {
    path.lineTo(endX + 75.0, y);
    path.lineTo(endX + 75.0, y + 25.0);
    path.lineTo(endX, y + 25.0);
  } else {
    path.cubicTo(startX + 60.0, y - 10.0, endX + 60.0, y + 30.0,
                 endX, y + 20.0);
  }
  return path;
}

bool isReverseArrow(int type) {
  return (type >= 45 && type <= 48) || (type >= 55 && type <= 58);
}

struct ParticipantPaintGeometry {
  QVector<QPainterPath> paths;
  QRectF labelRect;
  QRectF bounds;
};

ParticipantPaintGeometry participantGeometry(const SequenceLayoutParticipant& actor, qreal y,
                                             bool footer, QSizeF labelSize) {
  ParticipantPaintGeometry result;
  const QRectF rect(actor.logicalRect.x(), y, actor.logicalRect.width(),
                    actor.logicalRect.height());
  const qreal cx = actor.anchorX;
  auto add = [&](QPainterPath path) {
    result.bounds = result.bounds.isNull() ? path.boundingRect() : result.bounds.united(path.boundingRect());
    result.paths.append(std::move(path));
  };
  QPainterPath path;
  if (actor.type == QLatin1String("actor")) {
    path.addEllipse(QPointF(cx, y + 10.0), 15.0, 15.0);
    path.moveTo(cx, y + 25.0); path.lineTo(cx, y + 45.0);
    path.moveTo(cx - 18.0, y + 33.0); path.lineTo(cx + 18.0, y + 33.0);
    path.moveTo(cx - 18.0, y + 60.0); path.lineTo(cx, y + 45.0); path.lineTo(cx + 16.0, y + 60.0);
    add(path);
    result.labelRect = QRectF(rect.x(), y + 56.5, rect.width(), labelSize.height());
  } else if (actor.type == QLatin1String("collections")) {
    path.addRoundedRect(rect, 0.0, 0.0); add(path); path = {};
    path.addRoundedRect(rect.translated(-6.0, 6.0), 0.0, 0.0); add(path);
    result.labelRect = QRectF(rect.x() - 6.0, y + 6.0 + (rect.height() - labelSize.height()) / 2.0,
                              rect.width(), labelSize.height());
  } else if (actor.type == QLatin1String("queue")) {
    const qreal ry = rect.height() / 2.0;
    const qreal rx = ry / (2.5 + rect.height() / 50.0);
    path.moveTo(rect.left() + rx, rect.top());
    path.arcTo(QRectF(rect.left(), rect.top(), 2.0 * rx, rect.height()), 90.0, 180.0);
    path.lineTo(rect.right() - rx, rect.bottom());
    path.arcTo(QRectF(rect.right() - 2.0 * rx, rect.top(), 2.0 * rx, rect.height()), 270.0, 180.0);
    path.closeSubpath(); add(path); path = {};
    path.moveTo(rect.right() - rx, rect.top());
    path.arcTo(QRectF(rect.right() - 2.0 * rx, rect.top(), 2.0 * rx, rect.height()), 90.0, -180.0);
    add(path); result.labelRect = QRectF(rect.x(), y + (rect.height() - labelSize.height()) / 2.0,
                                         rect.width(), labelSize.height());
  } else if (actor.type == QLatin1String("control")) {
    path.addEllipse(QPointF(cx, y + 32.0), 22.0, 22.0);
    path.moveTo(cx, y + 10.0); path.lineTo(cx + 8.0, y + 3.0); add(path);
    result.bounds = QRectF(cx - 22.0, y + 10.0, 44.0, 44.0);
    result.labelRect = QRectF(rect.x(), y + (footer ? 48.5 : 55.5), rect.width(), labelSize.height());
  } else if (actor.type == QLatin1String("entity")) {
    const qreal cy = y + (footer ? 10.0 : 25.0) + (footer ? 22.0 : 6.0);
    path.addEllipse(QPointF(cx, cy), 22.0, 22.0);
    path.moveTo(cx - 22.0, cy + 22.0); path.lineTo(cx + 22.0, cy + 22.0); add(path);
    result.labelRect = QRectF(rect.x(), y + (footer ? 58.5 : 57.5), rect.width(), labelSize.height());
  } else if (actor.type == QLatin1String("database")) {
    const qreal w = rect.width() / 3.0, h = w, rx = w / 2.0;
    const qreal ry = rx / (2.5 + w / 50.0), left = rect.x() + w, top = y + ry;
    path.addEllipse(QRectF(left, top, w, 2.0 * ry));
    path.moveTo(left, top + ry); path.lineTo(left, top + h - ry);
    path.arcTo(QRectF(left, top + h - 2.0 * ry, w, 2.0 * ry), 180.0, -180.0);
    path.lineTo(left + w, top + ry); add(path);
    result.labelRect = QRectF(rect.x(), y + 56.5, rect.width(), labelSize.height());
  } else if (actor.type == QLatin1String("boundary")) {
    const qreal ty = y + 21.0;
    path.moveTo(cx - 55.0, ty + 12.0); path.lineTo(cx - 15.0, ty + 12.0);
    path.moveTo(cx - 55.0, ty + 2.0); path.lineTo(cx - 55.0, ty + 22.0);
    path.addEllipse(QPointF(cx, ty + 12.0), 22.0, 22.0); add(path);
    result.labelRect = QRectF(rect.x(), y + 57.5, rect.width(), labelSize.height());
  } else {
    path.addRoundedRect(rect, 3.0, 3.0); add(path);
    result.labelRect = QRectF(rect.x(), y + (rect.height() - labelSize.height()) / 2.0,
                              rect.width(), labelSize.height());
  }
  QRectF textBounds(result.labelRect.center().x() - labelSize.width() / 2.0,
                    result.labelRect.center().y() - labelSize.height() / 2.0,
                    labelSize.width(), labelSize.height());
  result.bounds = result.bounds.united(textBounds);
  return result;
}

QString markerEndForType(int type) {
  switch (type) {
    case 0: case 1: case 33: case 34: return QStringLiteral("arrowhead");
    case 3: case 4: return QStringLiteral("crosshead");
    case 24: case 25: return QStringLiteral("filled-head");
    case 41: case 51: return QStringLiteral("solidTopArrowHead");
    case 42: case 52: return QStringLiteral("solidBottomArrowHead");
    case 43: case 53: return QStringLiteral("stickTopArrowHead");
    case 44: case 54: return QStringLiteral("stickBottomArrowHead");
    default: return {};
  }
}

QString markerStartForType(int type) {
  switch (type) {
    case 33: case 34: return QStringLiteral("arrowhead");
    case 45: case 55: return QStringLiteral("solidBottomArrowHead");
    case 46: case 56: return QStringLiteral("solidTopArrowHead");
    case 47: case 57: return QStringLiteral("stickBottomArrowHead");
    case 48: case 58: return QStringLiteral("stickTopArrowHead");
    default: return {};
  }
}

bool narrowLifecycleActor(const QString& type) {
  return type == QLatin1String("actor") || type == QLatin1String("control") ||
         type == QLatin1String("entity") || type == QLatin1String("database");
}

}  // namespace

SequenceLayoutResult layoutSequence(const SequenceData& data,
                                    const SequenceLayoutMeasurements& measurements,
                                    SequenceLayoutOptions options) {
  SequenceLayoutResult result;
  const SequenceLayoutInput input = buildSequenceLayoutInput(data, measurements);
  QMap<int, QSizeF> measuredMessages, measuredNotes, measuredFragments;
  const auto rounded = [](const QSizeF& size) {
    return QSizeF(std::round(size.width()), std::round(size.height()));
  };
  for (const auto& message : input.messages)
    measuredMessages.insert(message.messageIndex, rounded(message.measuredLabel));
  for (const auto& note : input.notes)
    measuredNotes.insert(note.messageIndex, rounded(note.measuredLabel));
  for (const auto& fragment : input.fragments)
    measuredFragments.insert(fragment.messageIndex, rounded(fragment.measuredLabel));
  const qreal defaultTextHeight = measurements.participants.isEmpty()
      ? 22.0 : measurements.participants.cbegin().value().height();

  QMap<QString, int> actorIndex;
  QSet<QString> usedActors;
  if (options.hideUnusedParticipants) {
    for (const SequenceMessage& message : data.messages) {
      if (!message.from.isEmpty()) usedActors.insert(message.from);
      if (!message.to.isEmpty()) usedActors.insert(message.to);
    }
  }
  QVector<int> visibleActors;
  QVector<qreal> actorWidths(data.actors.size(), options.width);
  QVector<qreal> actorMargins(data.actors.size(), options.actorMargin);
  for (qsizetype index = 0; index < data.actors.size(); ++index) {
    actorIndex.insert(data.actors[index].id, static_cast<int>(index));
    if (!options.hideUnusedParticipants || usedActors.contains(data.actors[index].id))
      visibleActors.append(static_cast<int>(index));
    actorWidths[index] = measurements.participantDisplayById.contains(data.actors[index].id)
        ? options.width
        : std::max(options.width,
                   std::round(measurements.participants.value(data.actors[index].id).width()) +
                       2.0 * options.wrapPadding);
  }

  QMap<QString, qreal> maximumMessageWidth;
  for (qsizetype messageIndex = 0; messageIndex < data.messages.size(); ++messageIndex) {
    const SequenceMessage& message = data.messages[messageIndex];
    if (!actorIndex.contains(message.from) || !actorIndex.contains(message.to)) continue;
    const SequenceActor& actor = data.actors[actorIndex.value(message.to)];
    const bool note = message.placement >= 0;
    if (message.wrap || options.wrap) continue;
    const QSizeF measured = note ? measuredNotes.value(static_cast<int>(messageIndex))
                                 : measuredMessages.value(static_cast<int>(messageIndex));
    const qreal width = measured.width() + 2.0 * options.wrapPadding;
    auto update = [&](const QString& id, qreal candidate) {
      maximumMessageWidth[id] = std::max(maximumMessageWidth.value(id), candidate);
    };
    if (!note && message.from == actor.nextActor) update(message.to, width);
    else if (!note && message.from == actor.prevActor) update(message.from, width);
    else if (!note && message.from == message.to) {
      update(message.from, width / 2.0);
      update(message.to, width / 2.0);
    } else if (message.placement == 1) update(message.from, width);
    else if (message.placement == 0 && !actor.prevActor.isEmpty()) update(actor.prevActor, width);
    else if (message.placement == 2) {
      if (!actor.prevActor.isEmpty()) update(actor.prevActor, width / 2.0);
      if (!actor.nextActor.isEmpty()) update(message.from, width / 2.0);
    }
  }
  for (auto it = maximumMessageWidth.cbegin(); it != maximumMessageWidth.cend(); ++it) {
    const int index = actorIndex.value(it.key(), -1);
    if (index < 0) continue;
    const int next = actorIndex.value(data.actors[index].nextActor, -1);
    const qreal required = next < 0
        ? it.value() + options.actorMargin - actorWidths[index] / 2.0
        : it.value() + options.actorMargin - actorWidths[index] / 2.0 - actorWidths[next] / 2.0;
    actorMargins[index] = std::max(required, options.actorMargin);
  }

  PlacementBounds bounds;
  bounds.boxMargin = options.boxMargin;
  qreal previousWidth = 0.0, previousMargin = 0.0;
  QMap<QString, int> placedActor;
  QVector<qreal> boxStart(data.boxes.size(), 0.0);
  QVector<qreal> boxWidth(data.boxes.size(), 0.0);
  QVector<bool> boxStarted(data.boxes.size(), false);
  int previousBox = -1;
  qreal topBaseY = 0.0;
  if (!data.boxes.isEmpty()) {
    qreal titleHeight = 0.0;
    for (const QSizeF& size : measurements.boxes) titleHeight = std::max(titleHeight, size.height());
    topBaseY = options.boxMargin + titleHeight;
  }
  qreal participantHeight = options.height;
  for (auto it = measurements.participantDisplayById.cbegin();
       it != measurements.participantDisplayById.cend(); ++it)
    participantHeight = std::max(
        participantHeight, measurements.participants.value(it.key()).height());
  for (int index : visibleActors) {
    const SequenceActor& actor = data.actors[index];
    if (actor.boxIndex != previousBox) {
      if (previousBox >= 0) previousMargin += options.boxMargin + options.boxTextMargin;
      if (actor.boxIndex >= 0) {
        boxStart[actor.boxIndex] = previousWidth + previousMargin;
        boxStarted[actor.boxIndex] = true;
        previousMargin += options.boxTextMargin;
      }
      previousBox = actor.boxIndex;
    }
    // Mermaid intentionally reserves half an actor before runtime-created
    // participants so their creation message can terminate at the new box.
    // The JS truthiness check excludes only a creation recorded at index 0.
    if (data.createdActors.value(actor.id, 0) != 0)
      previousMargin += actorWidths[index] / 2.0;
    const qreal x = previousWidth + previousMargin;
    SequenceLayoutParticipant participant;
    participant.id = actor.id;
    participant.type = actor.type;
    participant.label = measurements.participantDisplayById.value(actor.id, actor.description);
    participant.logicalRect = QRectF(x, topBaseY, actorWidths[index], participantHeight);
    participant.margin = actorMargins[index];
    participant.anchorX = x + actorWidths[index] / 2.0;
    if (actor.type == QLatin1String("actor") || actor.type == QLatin1String("boundary"))
      participant.lifelineStartY = topBaseY + 80.0;
    else if (actor.type == QLatin1String("control") || actor.type == QLatin1String("entity") ||
             actor.type == QLatin1String("database"))
      participant.lifelineStartY = topBaseY + participantHeight + 2.0 * options.boxTextMargin;
    else
      participant.lifelineStartY = topBaseY + participantHeight;
    participant.topY = topBaseY;
    participant.drawBottom = options.mirrorActors;
    result.participants.append(participant);
    placedActor.insert(actor.id, result.participants.size() - 1);
    previousWidth += actorWidths[index] + previousMargin;
    if (actor.boxIndex >= 0)
      boxWidth[actor.boxIndex] = previousWidth + options.boxTextMargin - boxStart[actor.boxIndex];
    previousMargin = actorMargins[index];
  }
  bounds.vertical = topBaseY + participantHeight;

  QVector<OpenActivation> active;
  qreal sequenceIndex = 1.0;
  qreal sequenceIndexStep = 1.0;
  bool sequenceNumbersVisible = false;
  auto participantFor = [&](const QString& id) -> const SequenceLayoutParticipant& {
    return result.participants[placedActor.value(id)];
  };
  auto activationRange = [&](const QString& id) {
    const auto& actor = participantFor(id);
    qreal left = actor.anchorX - 1.0, right = actor.anchorX + 1.0;
    for (const OpenActivation& activation : active) {
      if (activation.actor != id) continue;
      left = std::min(left, activation.startX);
      right = std::max(right, activation.stopX);
    }
    return QPair<qreal, qreal>(left, right);
  };

  for (qsizetype messageIndex = 0; messageIndex < data.messages.size(); ++messageIndex) {
    const SequenceMessage& message = data.messages[messageIndex];
    const QString text = message.placement >= 0
        ? measurements.noteDisplayByIndex.value(
              static_cast<int>(messageIndex), messageText(message.message))
        : measurements.messageDisplayByIndex.value(
              static_cast<int>(messageIndex), messageText(message.message));
    if (message.type == kAutonumber) {
      const QJsonObject config = message.message.toObject();
      const qreal start = config.value(QStringLiteral("start")).toDouble();
      const qreal step = config.value(QStringLiteral("step")).toDouble();
      if (!qFuzzyIsNull(start)) sequenceIndex = start;
      if (!qFuzzyIsNull(step)) sequenceIndexStep = step;
      sequenceNumbersVisible = config.value(QStringLiteral("visible")).toBool();
      continue;
    }
    if (message.type == kActivationStart) {
      const auto& actor = participantFor(message.from);
      int stacked = 0;
      for (const OpenActivation& activation : active) if (activation.actor == message.from) ++stacked;
      const bool overlapsOpenActivation = !active.isEmpty();
      // Mermaid's emitted SVG aligns the first top-level activation with its
      // triggering line; nested or fragment-contained activations retain the
      // renderer's 2px start offset.
      active.append({static_cast<int>(messageIndex), message.from, stacked + 1,
                     actor.anchorX + (stacked - 1) * options.activationWidth / 2.0,
                     actor.anchorX + (stacked - 1) * options.activationWidth / 2.0 + options.activationWidth,
                     bounds.vertical + (overlapsOpenActivation || stacked > 0 || !bounds.fragments.isEmpty()
                                            ? 2.0 : 0.0)});
      continue;
    }
    if (message.type == kActivationEnd) {
      qsizetype found = active.size() - 1;
      while (found >= 0 && active[found].actor != message.from) --found;
      if (found < 0) continue;
      OpenActivation activation = active.takeAt(found);
      if (activation.startY + 18.0 > bounds.vertical) {
        activation.startY = bounds.vertical - 6.0;
        bounds.vertical += 12.0;
      }
      result.activations.append({activation.messageIndex, activation.actor, activation.depth,
                                 QRectF(activation.startX, activation.startY,
                                        activation.stopX - activation.startX,
                                        bounds.vertical - activation.startY)});
      bounds.insert(activation.startX, bounds.vertical - 10.0,
                    activation.stopX, bounds.vertical);
      continue;
    }

    const QString kind = fragmentKind(message.type);
    if (!kind.isEmpty()) {
      const bool background = message.type == kRectStart;
      bounds.vertical += options.boxMargin;
      OpenFragment fragment;
      fragment.messageIndex = static_cast<int>(messageIndex);
      fragment.kind = kind;
      fragment.label = text;
      fragment.depth = bounds.fragments.size() + 1;
      fragment.startY = fragment.stopY = bounds.vertical;
      bounds.fragments.append(fragment);
      const qreal post = background ? options.boxMargin : options.boxMargin + options.boxTextMargin;
      const qreal labelHeight = !background && !text.isEmpty()
          ? std::max(options.labelBoxHeight,
                     measuredFragments.value(static_cast<int>(messageIndex),
                                               QSizeF(0.0, defaultTextHeight)).height())
          : 0.0;
      bounds.vertical += post + labelHeight;
      continue;
    }
    if (message.type == 13 || message.type == 20 || message.type == 28) {
      bounds.vertical += options.boxMargin + options.boxTextMargin;
      if (!bounds.fragments.isEmpty()) bounds.fragments.last().sections.append(bounds.vertical);
      bounds.vertical += options.boxMargin +
          (text.isEmpty() ? 0.0 : std::max(options.labelBoxHeight, defaultTextHeight));
      continue;
    }
    if (isFragmentEnd(message.type)) {
      if (bounds.fragments.isEmpty()) continue;
      OpenFragment fragment = bounds.fragments.takeLast();
      bounds.vertical = std::max(bounds.vertical, fragment.stopY);
      if (!std::isfinite(fragment.startX)) {
        fragment.startX = result.participants.first().anchorX;
        fragment.stopX = result.participants.last().anchorX;
      }
      result.fragments.append({fragment.messageIndex, fragment.kind, fragment.label, fragment.depth,
                               QRectF(fragment.startX, fragment.startY,
                                      fragment.stopX - fragment.startX,
                                      fragment.stopY - fragment.startY), fragment.sections});
      continue;
    }
    if (message.type == kNote) {
      const auto& from = participantFor(message.from);
      const auto& to = participantFor(message.to);
      const QSizeF measured = measuredNotes.value(static_cast<int>(messageIndex));
      qreal width = std::max(options.width, measured.width() + 2.0 * options.noteMargin);
      qreal x = from.logicalRect.x();
      if (message.placement == 1) {
        width = std::max(from.logicalRect.width() / 2.0 + to.logicalRect.width() / 2.0,
                         measured.width() + 2.0 * options.noteMargin);
        x = from.logicalRect.x() + (from.logicalRect.width() + options.actorMargin) / 2.0;
      } else if (message.placement == 0) {
        width = std::max(from.logicalRect.width() / 2.0 + to.logicalRect.width() / 2.0,
                         measured.width() + 2.0 * options.noteMargin);
        x = from.logicalRect.x() - width +
            (from.logicalRect.width() - options.actorMargin) / 2.0;
      } else if (message.from == message.to) {
        width = std::max({from.logicalRect.width(), options.width,
                          measured.width() + 2.0 * options.noteMargin});
        x = from.logicalRect.x() + (from.logicalRect.width() - width) / 2.0;
      } else {
        width = std::abs(from.anchorX - to.anchorX) + options.actorMargin;
        x = from.logicalRect.x() < to.logicalRect.x()
            ? from.anchorX - options.actorMargin / 2.0
            : to.anchorX - options.actorMargin / 2.0;
      }
      bounds.vertical += options.boxMargin;
      const qreal height = std::round(measured.height()) + 2.0 * options.noteMargin;
      const QRectF rect(x, bounds.vertical, width, height);
      bounds.vertical += height;
      bounds.insert(rect.left(), rect.top(), rect.right(), rect.bottom());
      result.notes.append({static_cast<int>(messageIndex), message.from, message.to,
                           message.placement, text, rect});
      continue;
    }
    if (!isSignal(message.type)) continue;

    const auto fromRange = activationRange(message.from);
    const auto toRange = activationRange(message.to);
    const bool rightward = fromRange.first <= toRange.first;
    qreal startX = rightward ? fromRange.second : fromRange.first;
    qreal stopX = rightward ? toRange.first : toRange.second;
    if (message.centralConnection == kCentralConnectionReverse ||
        message.centralConnection == kCentralConnectionDual)
      startX += 4.0;
    const bool targetActivated = std::abs(toRange.first - toRange.second) > 2.0;
    const qreal direction = rightward ? -1.0 : 1.0;
    if (message.from == message.to) {
      stopX = startX;
    } else {
      if (message.activate && !targetActivated)
        stopX += direction * (options.activationWidth / 2.0 - 1.0);
      if (!isStickOrOpenArrow(message.type)) stopX += direction * 3.0;
      if (adjustsStartForMarker(message.type)) startX -= direction * 3.0;
    }
    const QSizeF measured = measuredMessages.value(static_cast<int>(messageIndex));
    const qreal startY = bounds.vertical;
    bounds.vertical += 10.0;
    const int measuredLines = defaultTextHeight > 0.0
        ? std::max(1, qRound(measured.height() / defaultTextHeight)) : 1;
    const int lines = std::max(measuredLines,
                               static_cast<int>(text.count(QLatin1Char('\n'))) + 1);
    const qreal lineHeight = measured.height() / lines;
    bounds.vertical += lineHeight;
    qreal totalOffset = measured.height() - 10.0;
    qreal lineY = 0.0;
    if (qFuzzyCompare(startX + 1.0, stopX + 1.0)) {
      lineY = bounds.vertical + totalOffset;
      if (!options.rightAngles) {
        totalOffset += options.boxMargin;
        lineY = bounds.vertical + totalOffset;
      }
      totalOffset += 30.0;
      const qreal dx = std::max(measured.width() / 2.0, options.width / 2.0);
      bounds.insert(startX - dx, bounds.vertical - 10.0 + totalOffset,
                    stopX + dx, bounds.vertical + 30.0 + totalOffset);
    } else {
      totalOffset += options.boxMargin;
      lineY = bounds.vertical + totalOffset;
      bounds.insert(startX, lineY - 10.0, stopX, lineY);
    }
    bounds.vertical += totalOffset;

    auto lifecycleParticipant = [&](const QString& id) -> SequenceLayoutParticipant& {
      return result.participants[placedActor.value(id)];
    };
    if (data.createdActors.value(message.to, -1) == messageIndex) {
      SequenceLayoutParticipant& actor = lifecycleParticipant(message.to);
      const qreal adjustment = narrowLifecycleActor(actor.type)
          ? 21.0 : actor.logicalRect.width() / 2.0 + 3.0;
      stopX += actor.anchorX < participantFor(message.from).anchorX ? adjustment : -adjustment;
      actor.created = true;
      actor.topY = lineY - actor.logicalRect.height() / 2.0;
      const qreal lifelineOffset = actor.lifelineStartY - actor.logicalRect.y();
      actor.lifelineStartY = actor.topY + lifelineOffset;
      bounds.vertical += actor.logicalRect.height() / 2.0;
    } else if (data.destroyedActors.value(message.from, -1) == messageIndex ||
               data.destroyedActors.value(message.to, -1) == messageIndex) {
      const bool sender = data.destroyedActors.value(message.from, -1) == messageIndex;
      SequenceLayoutParticipant& actor = lifecycleParticipant(sender ? message.from : message.to);
      const qreal adjustment = narrowLifecycleActor(actor.type)
          ? 18.0 + (sender ? 0.0 : 3.0) : actor.logicalRect.width() / 2.0 + (sender ? 0.0 : 3.0);
      if (options.mirrorActors) {
        if (sender)
          startX += actor.anchorX < participantFor(message.to).anchorX ? adjustment : -adjustment;
        else
          stopX += actor.anchorX < participantFor(message.from).anchorX ? adjustment : -adjustment;
      }
      actor.destroyed = true;
      actor.bottomY = lineY - actor.logicalRect.height() / 2.0;
      actor.lifelineStopY = actor.bottomY;
      bounds.vertical += actor.logicalRect.height() / 2.0;
    }
    const qreal labelStartX = startX;
    const qreal labelStopX = stopX;
    qreal drawStartX = startX;
    qreal drawStopX = stopX;
    const bool bidirectional = message.type == 33 || message.type == 34;
    const bool reverse = isReverseArrow(message.type);
    if (sequenceNumbersVisible) {
      if (bidirectional) {
        if (startX < stopX) {
          drawStartX = startX + 12.0;
        } else {
          drawStartX = startX - 6.0 - (message.centralConnection ? 5.0 : 0.0);
          if (message.centralConnection == kCentralConnectionDual ||
              message.centralConnection == kCentralConnectionReverse)
            drawStartX -= 7.5;
        }
      } else if (reverse) {
        drawStopX = stopX > startX ? stopX - 12.0 : stopX - 6.0;
        if (message.centralConnection == kCentralConnectionDual ||
            message.centralConnection == kCentralConnectionReverse)
          drawStartX -= 7.5;
        if (message.centralConnection) drawStopX += 15.0;
      } else if (!qFuzzyCompare(startX + 1.0, stopX + 1.0)) {
        drawStartX = startX + 6.0;
      }
    }
    const qreal modelHeight = lineHeight + totalOffset;
    bounds.insert(std::min({fromRange.first, fromRange.second, toRange.first, toRange.second}),
                  startY,
                  std::max({fromRange.first, fromRange.second, toRange.first, toRange.second}),
                  startY + modelHeight);
    SequenceLayoutMessage placed;
    placed.messageIndex = static_cast<int>(messageIndex);
    placed.id = message.id;
    placed.from = message.from;
    placed.to = message.to;
    placed.label = text;
    placed.type = message.type;
    placed.startX = drawStartX;
    placed.stopX = drawStopX;
    placed.lineY = lineY;
    placed.labelRect = QRectF((labelStartX + labelStopX - measured.width()) / 2.0,
                              startY + 10.0, measured.width(), measured.height());
    if (qFuzzyCompare(labelStartX + 1.0, labelStopX + 1.0)) {
      const qreal pathStartX = labelStartX +
          (sequenceNumbersVisible && (reverse || bidirectional) ? 10.0 : 0.0);
      placed.path = selfMessagePath(pathStartX, labelStopX, lineY, options.rightAngles);
      placed.painterPath = selfPainterPath(pathStartX, labelStopX, lineY, options.rightAngles);
    }
    placed.dashed = message.type == 1 || message.type == 4 || message.type == 6 || message.type == 25 ||
                    message.type == 34 || (message.type >= 51 && message.type <= 58);
    placed.markerEnd = markerEndForType(message.type);
    placed.markerStart = markerStartForType(message.type);
    placed.markerStartDirection = QPointF(drawStartX - drawStopX, 0.0);
    placed.markerEndDirection = QPointF(drawStopX - drawStartX, 0.0);
    if (!placed.painterPath.isEmpty()) {
      placed.markerStartDirection = QPointF(-60.0, options.rightAngles ? 0.0 : 10.0);
      placed.markerEndDirection = QPointF(-60.0, options.rightAngles ? 0.0 : -10.0);
    }
    if (message.centralConnection) {
      const auto& fromActor = participantFor(message.from);
      const auto& toActor = participantFor(message.to);
      qreal fromCenter = fromActor.anchorX;
      qreal toCenter = toActor.anchorX;
      if (sequenceNumbersVisible) {
        const qreal baseOffset = fromCenter <= toCenter ? 16.5 : -16.5;
        if (message.centralConnection == kCentralConnectionReverse && !reverse)
          fromCenter += baseOffset;
        else if (message.centralConnection == kCentralConnection && reverse)
          toCenter -= baseOffset;
        else if (message.centralConnection == kCentralConnectionDual) {
          if (reverse) toCenter -= baseOffset;
          else fromCenter += baseOffset;
        }
      }
      if (message.centralConnection == kCentralConnectionReverse ||
          message.centralConnection == kCentralConnectionDual)
        placed.centralConnections.append(QPointF(fromCenter, lineY));
      if (message.centralConnection == kCentralConnection ||
          message.centralConnection == kCentralConnectionDual)
        placed.centralConnections.append(QPointF(toCenter, lineY));
    }
    result.messages.append(placed);
    if (sequenceNumbersVisible) {
      const qreal fromBounds = std::min({fromRange.first, fromRange.second,
                                         toRange.first, toRange.second});
      const qreal toBounds = std::max({fromRange.first, fromRange.second,
                                       toRange.first, toRange.second});
      qreal x = fromBounds + 1.0;
      if (!qFuzzyCompare(labelStartX + 1.0, labelStopX + 1.0))
        x = reverse ? (labelStartX <= labelStopX ? toBounds - 1.0 : fromBounds + 1.0)
                    : (labelStartX <= labelStopX ? fromBounds + 1.0 : toBounds - 1.0);
      const QString number = QString::number(sequenceIndex, 'g', 12);
      const qreal fontSize = number.size() > 5 ? 7.0 : number.size() > 3 ? 9.0 : 12.0;
      result.sequenceNumbers.append({static_cast<int>(messageIndex), number,
                                     QPointF(x, lineY + 4.0), fontSize});
      bounds.insert(x - 6.0, lineY - 6.0, x + 6.0, lineY + 6.0);
    }
    sequenceIndex = std::round((sequenceIndex + sequenceIndexStep) * 100.0) / 100.0;
  }

  std::sort(result.activations.begin(), result.activations.end(),
            [](const auto& left, const auto& right) { return left.messageIndex < right.messageIndex; });
  const qreal lifelineStop = bounds.vertical + 2.0 * options.boxMargin;
  for (SequenceLayoutParticipant& participant : result.participants) {
    if (!participant.destroyed) {
      participant.lifelineStopY = options.mirrorActors ? lifelineStop : 2000.0;
      participant.bottomY = lifelineStop;
    } else if (!options.mirrorActors) {
      participant.lifelineStopY = participant.bottomY + participant.logicalRect.height() / 2.0;
      if (participant.type == QLatin1String("database")) participant.lifelineStopY += 2.5;
    }
    const QSizeF labelSize = measurements.participants.value(
        participant.id, QSizeF(0.0, defaultTextHeight));
    const ParticipantPaintGeometry top = participantGeometry(
        participant, participant.topY, false, labelSize);
    const ParticipantPaintGeometry bottom = participantGeometry(
        participant, participant.bottomY, true, labelSize);
    participant.topShapePaths = top.paths;
    participant.bottomShapePaths = participant.drawBottom ? bottom.paths : QVector<QPainterPath>{};
    participant.topLabelRect = top.labelRect;
    participant.bottomLabelRect = bottom.labelRect;
    participant.topPaintedBounds = top.bounds;
    participant.bottomPaintedBounds = participant.drawBottom ? bottom.bounds : QRectF{};
    bounds.insert(top.bounds.left(), top.bounds.top(), top.bounds.right(), top.bounds.bottom());
    if (participant.drawBottom)
      bounds.insert(bottom.bounds.left(), bottom.bounds.top(), bottom.bounds.right(), bottom.bounds.bottom());
  }
  if (!data.boxes.isEmpty()) {
    const qreal boxBottom = bounds.all.bottom() + options.boxMargin + 1.5;
    for (qsizetype index = 0; index < data.boxes.size(); ++index) {
      if (!boxStarted[index]) continue;
      const SequenceBox& source = data.boxes[index];
      const QSizeF labelSize = index < measurements.boxes.size()
          ? measurements.boxes[index] : QSizeF{};
      SequenceLayoutBox box;
      box.boxIndex = static_cast<int>(index);
      box.label = source.name;
      box.fill = source.fill;
      box.rect = QRectF(boxStart[index] - 2.0 * options.boxMargin,
                        -options.boxTextMargin,
                        boxWidth[index] + 4.0 * options.boxMargin,
                        boxBottom + options.boxTextMargin);
      box.labelRect = QRectF(box.rect.x(), options.boxTextMargin,
                             box.rect.width(), labelSize.height());
      // Mermaid lowers every newly drawn box, reversing source order in the
      // final scene while actor membership keeps its source box index.
      result.boxes.prepend(box);
      bounds.insert(box.rect.left(), box.rect.top(), box.rect.right(), box.rect.bottom());
    }
  }
  result.bounds = bounds.hasBounds ? bounds.all : QRectF{};
  return result;
}

}  // namespace muffin::mermaid::sequence
