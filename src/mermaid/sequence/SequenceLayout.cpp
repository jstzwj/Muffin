#include "mermaid/sequence/SequenceLayout.h"

#include <algorithm>

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
                           atOrEmpty(measurements.notes, noteIndex++)});
      continue;
    }
    const QString kind = fragmentKind(message.type);
    if (!kind.isEmpty()) {
      ++fragmentDepth;
      result.maximumFragmentDepth = std::max(result.maximumFragmentDepth, fragmentDepth);
      result.fragments.append({static_cast<int>(index), kind, messageText(message.message),
                               fragmentDepth, atOrEmpty(measurements.fragments, fragmentIndex++)});
      continue;
    }
    if (isFragmentEnd(message.type)) {
      fragmentDepth = std::max(0, fragmentDepth - 1);
      continue;
    }
    if (isSignal(message.type)) {
      result.messages.append({static_cast<int>(index), message.from, message.to,
                              messageText(message.message), message.type,
                              atOrEmpty(measurements.messages, signalIndex++)});
    }
  }
  return result;
}

}  // namespace muffin::mermaid::sequence
