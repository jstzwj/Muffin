#include "mermaid/sequence/SequenceLayout.h"

#include <algorithm>
#include <limits>

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
constexpr int kCriticalStart = 27;
constexpr int kCriticalEnd = 29;
constexpr int kBreakStart = 30;
constexpr int kBreakEnd = 31;
constexpr int kParOverStart = 32;

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
         type == 53 || type == 54 || type == 55 || type == 56 || type == 45 || type == 46;
}
bool adjustsStartForMarker(int type) {
  return type == 33 || type == 34 || type == 45 || type == 46 || type == 55 || type == 56;
}

QString selfMessagePath(qreal x, qreal y, bool rightAngles) {
  if (rightAngles)
    return QStringLiteral("M %1,%2 H %3 V %4 H %1")
        .arg(x).arg(y).arg(x + 75.0).arg(y + 25.0);
  return QStringLiteral("M %1,%2 C %3,%4 %3,%5 %1,%6")
      .arg(x).arg(y).arg(x + 60.0).arg(y - 10.0).arg(y + 30.0).arg(y + 20.0);
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
  QVector<qreal> actorWidths(data.actors.size(), options.width);
  QVector<qreal> actorMargins(data.actors.size(), options.actorMargin);
  for (qsizetype index = 0; index < data.actors.size(); ++index) {
    actorIndex.insert(data.actors[index].id, static_cast<int>(index));
    actorWidths[index] = std::max(options.width,
                                  std::round(measurements.participants.value(data.actors[index].id).width()) +
                                      2.0 * options.wrapPadding);
  }

  QMap<QString, qreal> maximumMessageWidth;
  for (qsizetype messageIndex = 0; messageIndex < data.messages.size(); ++messageIndex) {
    const SequenceMessage& message = data.messages[messageIndex];
    if (!actorIndex.contains(message.from) || !actorIndex.contains(message.to)) continue;
    const SequenceActor& actor = data.actors[actorIndex.value(message.to)];
    const bool note = message.placement >= 0;
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
  for (qsizetype index = 0; index < data.actors.size(); ++index) {
    const SequenceActor& actor = data.actors[index];
    const qreal x = previousWidth + previousMargin;
    SequenceLayoutParticipant participant;
    participant.id = actor.id;
    participant.type = actor.type;
    participant.label = actor.description;
    participant.logicalRect = QRectF(x, 0.0, actorWidths[index], options.height);
    participant.margin = actorMargins[index];
    participant.anchorX = x + actorWidths[index] / 2.0;
    participant.lifelineStartY = actor.type == QLatin1String("actor")
        ? options.height + 3.0 * options.boxTextMargin : options.height;
    result.participants.append(participant);
    placedActor.insert(actor.id, static_cast<int>(index));
    bounds.insert(x, 0.0, x + actorWidths[index], options.height);
    previousWidth += actorWidths[index] + previousMargin;
    previousMargin = actorMargins[index];
  }
  bounds.vertical = options.height;

  QVector<OpenActivation> active;
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
    const QString text = messageText(message.message);
    if (message.type == kActivationStart) {
      const auto& actor = participantFor(message.from);
      int stacked = 0;
      for (const OpenActivation& activation : active) if (activation.actor == message.from) ++stacked;
      // Mermaid's emitted SVG aligns the first top-level activation with its
      // triggering line; nested or fragment-contained activations retain the
      // renderer's 2px start offset.
      active.append({static_cast<int>(messageIndex), message.from, stacked + 1,
                     actor.anchorX + (stacked - 1) * options.activationWidth / 2.0,
                     actor.anchorX + (stacked - 1) * options.activationWidth / 2.0 + options.activationWidth,
                     bounds.vertical + (stacked > 0 || !bounds.fragments.isEmpty() ? 2.0 : 0.0)});
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
    const int lines = std::max(1, static_cast<int>(text.count(QLatin1Char('\n'))) + 1);
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
    placed.startX = startX;
    placed.stopX = stopX;
    placed.lineY = lineY;
    placed.labelRect = QRectF((startX + stopX - measured.width()) / 2.0,
                              startY + 10.0, measured.width(), measured.height());
    if (qFuzzyCompare(startX + 1.0, stopX + 1.0))
      placed.path = selfMessagePath(startX, lineY, options.rightAngles);
    placed.dashed = message.type == 1 || message.type == 4 || message.type == 6 ||
                    message.type == 34 || (message.type >= 51 && message.type <= 58);
    if (message.type == 0 || message.type == 1 || message.type == 33 || message.type == 34)
      placed.markerEnd = QStringLiteral("arrow");
    else if (message.type == 3 || message.type == 4)
      placed.markerEnd = QStringLiteral("cross");
    else if (message.type == 24 || message.type == 25)
      placed.markerEnd = QStringLiteral("point");
    else if (!isOpenArrow(message.type))
      placed.markerEnd = QStringLiteral("half");
    if (message.type == 33 || message.type == 34 || adjustsStartForMarker(message.type))
      placed.markerStart = message.type == 33 || message.type == 34
          ? QStringLiteral("arrow") : QStringLiteral("half");
    result.messages.append(placed);
  }

  std::sort(result.activations.begin(), result.activations.end(),
            [](const auto& left, const auto& right) { return left.messageIndex < right.messageIndex; });
  const qreal lifelineStop = bounds.vertical + 2.0 * options.boxMargin;
  for (SequenceLayoutParticipant& participant : result.participants) {
    participant.lifelineStopY = lifelineStop;
    bounds.insert(participant.logicalRect.left(), lifelineStop,
                  participant.logicalRect.right(), lifelineStop + participant.logicalRect.height());
  }
  result.bounds = bounds.hasBounds ? bounds.all : QRectF{};
  return result;
}

}  // namespace muffin::mermaid::sequence
