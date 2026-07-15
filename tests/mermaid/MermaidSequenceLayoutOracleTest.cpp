#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceScene.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <algorithm>
#include <cstdlib>

using namespace muffin::mermaid::sequence;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }

QSizeF sizeOf(const QJsonObject& object) {
  return {object.value(QStringLiteral("width")).toDouble(),
          object.value(QStringLiteral("height")).toDouble()};
}

qreal centerX(const QJsonObject& box) {
  return box.value(QStringLiteral("x")).toDouble() + box.value(QStringLiteral("width")).toDouble() / 2.0;
}

int messageIndex(const QString& id) {
  bool ok = false;
  const int result = id.startsWith(QLatin1Char('i')) ? id.mid(1).toInt(&ok) : -1;
  return ok ? result : -1;
}

void requireNear(qreal actual, qreal expected, const QString& context) {
  require(std::abs(actual - expected) <= 0.001,
          QStringLiteral("%1: native=%2 upstream=%3").arg(context).arg(actual).arg(expected));
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected sequence layout fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open sequence layout fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Sequence layout Mermaid version drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() == QLatin1String("bundled-noto"),
          QStringLiteral("Sequence layout must use the fixed Noto oracle"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("e4fc4d3b5301c2e2553084ae8e5fceb8dde086d6081c6f530a7d7ed143604363"),
          QStringLiteral("Sequence layout fixture changed; audit geometry and update its digest"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 6, QStringLiteral("Sequence layout case count drifted"));
  QSet<QString> ids, coveredAxes;
  int participantCount = 0, lifelineCount = 0, messageCount = 0;
  int activationCount = 0, noteCount = 0, fragmentCount = 0;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !ids.contains(id), QStringLiteral("Duplicate sequence layout case: %1").arg(id));
    ids.insert(id);
    for (const QJsonValue& axis : fixture.value(QStringLiteral("axes")).toArray()) coveredAxes.insert(axis.toString());

    const QJsonObject config = fixture.value(QStringLiteral("config")).toObject();
    require(config.value(QStringLiteral("fontFamily")).toString().contains(QLatin1String("Noto Sans")) &&
                config.value(QStringLiteral("actorMargin")).toInt() == 50 &&
                config.value(QStringLiteral("messageMargin")).toInt() == 35 &&
                config.value(QStringLiteral("activationWidth")).toInt() == 10,
            QStringLiteral("%1 sequence layout config drifted").arg(id));

    const SequenceDiagram diagram = SequenceDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    const SequenceData& data = diagram.data();
    SequenceLayoutMeasurements measurements;
    const QJsonArray participants = fixture.value(QStringLiteral("participants")).toArray();
    const QJsonArray lifelines = fixture.value(QStringLiteral("lifelines")).toArray();
    const QJsonArray messages = fixture.value(QStringLiteral("messages")).toArray();
    const QJsonArray activations = fixture.value(QStringLiteral("activations")).toArray();
    const QJsonArray notes = fixture.value(QStringLiteral("notes")).toArray();
    const QJsonArray fragments = fixture.value(QStringLiteral("fragments")).toArray();
    for (const QJsonValue& participant : participants) {
      const QJsonObject object = participant.toObject();
      measurements.participants.insert(object.value(QStringLiteral("id")).toString(),
                                       sizeOf(object.value(QStringLiteral("label")).toObject()));
    }
    for (const QJsonValue& message : messages) {
      const QJsonObject object = message.toObject();
      const QSizeF measured = sizeOf(object.value(QStringLiteral("label")).toObject());
      measurements.messages.append(measured);
      measurements.messagesByIndex.insert(messageIndex(object.value(QStringLiteral("id")).toString()), measured);
    }
    for (const QJsonValue& note : notes) {
      const QJsonObject object = note.toObject();
      const QSizeF measured = sizeOf(object.value(QStringLiteral("label")).toObject());
      measurements.notes.append(measured);
      measurements.notesByIndex.insert(messageIndex(object.value(QStringLiteral("id")).toString()), measured);
    }
    for (const QJsonValue& fragment : fragments) {
      const QJsonArray labels = fragment.toObject().value(QStringLiteral("labels")).toArray();
      measurements.fragments.append(labels.isEmpty() ? QSizeF{} : sizeOf(labels.first().toObject()));
    }
    const SequenceLayoutInput input = buildSequenceLayoutInput(data, measurements);
    SequenceLayoutOptions options;
    options.actorMargin = config.value(QStringLiteral("actorMargin")).toDouble();
    options.width = config.value(QStringLiteral("width")).toDouble();
    options.height = config.value(QStringLiteral("height")).toDouble();
    options.boxMargin = config.value(QStringLiteral("boxMargin")).toDouble();
    options.boxTextMargin = config.value(QStringLiteral("boxTextMargin")).toDouble();
    options.noteMargin = config.value(QStringLiteral("noteMargin")).toDouble();
    options.activationWidth = config.value(QStringLiteral("activationWidth")).toDouble();
    options.wrapPadding = config.value(QStringLiteral("wrapPadding")).toDouble();
    options.labelBoxWidth = config.value(QStringLiteral("labelBoxWidth")).toDouble();
    options.labelBoxHeight = config.value(QStringLiteral("labelBoxHeight")).toDouble();
    options.rightAngles = config.value(QStringLiteral("rightAngles")).toBool();
    const SequenceLayoutResult layout = layoutSequence(data, measurements, options);
    const SequenceScene scene = buildSequenceScene(layout);

    require(input.participants.size() == participants.size() && lifelines.size() == participants.size(),
            QStringLiteral("%1 participant/lifeline input count mismatch").arg(id));
    QMap<QString, qreal> participantCenters;
    QMap<QString, QString> participantTypes;
    for (const SequenceLayoutParticipantInput& participant : input.participants)
      participantTypes.insert(participant.id, participant.type);
    for (const QJsonValue& participantValue : participants) {
      const QJsonObject participant = participantValue.toObject();
      const QString actorId = participant.value(QStringLiteral("id")).toString();
      require(measurements.participants.contains(actorId) &&
                  measurements.participants.value(actorId).width() > 0.0,
              QStringLiteral("%1 participant %2 has no measured label").arg(id, actorId));
      participantCenters.insert(actorId, centerX(participant.value(QStringLiteral("box")).toObject()));
      const auto native = std::find_if(layout.participants.cbegin(), layout.participants.cend(),
                                       [&](const auto& item) { return item.id == actorId; });
      require(native != layout.participants.cend() &&
                  participant.value(QStringLiteral("structure")).toObject()
                      .value(QStringLiteral("dataType")).toString() == native->type,
              QStringLiteral("%1/%2 participant SVG type mismatch").arg(id, actorId));
    }
    for (const QJsonValue& lineValue : lifelines) {
      const QJsonObject line = lineValue.toObject();
      const QString actorId = line.value(QStringLiteral("id")).toString();
      require(participantCenters.contains(actorId), QStringLiteral("%1 orphan lifeline %2").arg(id, actorId));
      const bool centeredShape = participantTypes.value(actorId) != QLatin1String("participant") ||
          std::abs(line.value(QStringLiteral("x1")).toDouble() - participantCenters.value(actorId)) <= 0.001;
      require(std::abs(line.value(QStringLiteral("x1")).toDouble() -
                       line.value(QStringLiteral("x2")).toDouble()) <= 0.001 && centeredShape &&
                  line.value(QStringLiteral("y2")).toDouble() > line.value(QStringLiteral("y1")).toDouble(),
              QStringLiteral("%1 lifeline %2 is not centered and vertical").arg(id, actorId));
      const auto native = std::find_if(layout.participants.cbegin(), layout.participants.cend(),
                                       [&](const auto& item) { return item.id == actorId; });
      require(native != layout.participants.cend(), QStringLiteral("%1 native participant missing").arg(id));
      requireNear(native->anchorX, line.value(QStringLiteral("x1")).toDouble(),
                  QStringLiteral("%1/%2 anchor x").arg(id, actorId));
      requireNear(native->lifelineStartY, line.value(QStringLiteral("y1")).toDouble(),
                  QStringLiteral("%1/%2 lifeline start").arg(id, actorId));
      requireNear(native->lifelineStopY, line.value(QStringLiteral("y2")).toDouble(),
                  QStringLiteral("%1/%2 lifeline stop").arg(id, actorId));
    }

    require(input.messages.size() == messages.size(), QStringLiteral("%1 message input count mismatch").arg(id));
    qreal previousMessageY = -1e9;
    for (const QJsonValue& messageValue : messages) {
      const QJsonObject message = messageValue.toObject();
      const QJsonObject box = message.value(QStringLiteral("box")).toObject();
      const qreal y = box.value(QStringLiteral("y")).toDouble();
      require(y > previousMessageY, QStringLiteral("%1 message vertical order is unstable").arg(id));
      previousMessageY = y;
      const qsizetype position = message.value(QStringLiteral("position")).toInteger();
      const SequenceLayoutMessage& native = layout.messages.at(position);
      const QJsonObject line = message.value(QStringLiteral("line")).toObject();
      if (!line.isEmpty()) {
        requireNear(native.startX, line.value(QStringLiteral("x1")).toDouble(),
                    QStringLiteral("%1 message %2 x1").arg(id).arg(position));
        requireNear(native.stopX, line.value(QStringLiteral("x2")).toDouble(),
                    QStringLiteral("%1 message %2 x2").arg(id).arg(position));
        requireNear(native.lineY, line.value(QStringLiteral("y1")).toDouble(),
                    QStringLiteral("%1 message %2 y").arg(id).arg(position));
      } else {
        require(native.path == message.value(QStringLiteral("path")).toString(),
                QStringLiteral("%1 message %2 self path mismatch:\nnative: %3\nupstream: %4")
                    .arg(id).arg(position).arg(native.path, message.value(QStringLiteral("path")).toString()));
      }
      const QJsonObject structure = message.value(QStringLiteral("structure")).toObject();
      require(structure.value(QStringLiteral("className")).toString() ==
                  (native.dashed ? QLatin1String("messageLine1") : QLatin1String("messageLine0")),
              QStringLiteral("%1 message %2 dash class mismatch").arg(id).arg(position));
      const bool upstreamStart = !structure.value(QStringLiteral("markerStart")).toString().isEmpty();
      const bool upstreamEnd = !structure.value(QStringLiteral("markerEnd")).toString().isEmpty();
      require(upstreamStart == !native.markerStart.isEmpty() && upstreamEnd == !native.markerEnd.isEmpty(),
              QStringLiteral("%1 message %2 marker structure mismatch").arg(id).arg(position));
    }

    int activationStarts = 0;
    for (const SequenceLayoutActivationInput& activation : input.activations)
      if (activation.begin) ++activationStarts;
    require(activationStarts == activations.size(), QStringLiteral("%1 activation stack count mismatch").arg(id));
    for (const QJsonValue& activationValue : activations) {
      const QJsonObject activation = activationValue.toObject();
      require(std::abs(activation.value(QStringLiteral("width")).toDouble() - 10.0) <= 0.001 &&
                  activation.value(QStringLiteral("height")).toDouble() > 0.0,
              QStringLiteral("%1 activation geometry drifted").arg(id));
      const qsizetype position = activation.value(QStringLiteral("position")).toInteger();
      const QRectF native = layout.activations.at(position).rect;
      requireNear(native.x(), activation.value(QStringLiteral("x")).toDouble(),
                  QStringLiteral("%1 activation %2 x").arg(id).arg(position));
      requireNear(native.y(), activation.value(QStringLiteral("y")).toDouble(),
                  QStringLiteral("%1 activation %2 y").arg(id).arg(position));
      requireNear(native.width(), activation.value(QStringLiteral("width")).toDouble(),
                  QStringLiteral("%1 activation %2 width").arg(id).arg(position));
      requireNear(native.height(), activation.value(QStringLiteral("height")).toDouble(),
                  QStringLiteral("%1 activation %2 height").arg(id).arg(position));
      require(activation.value(QStringLiteral("className")).toString() ==
                  QStringLiteral("activation%1").arg((layout.activations.at(position).depth - 1) % 3),
              QStringLiteral("%1 activation %2 paint order/class mismatch").arg(id).arg(position));
    }

    require(input.notes.size() == notes.size(), QStringLiteral("%1 note input count mismatch").arg(id));
    for (const QJsonValue& noteValue : notes) {
      const QJsonObject note = noteValue.toObject();
      const QSizeF shape = sizeOf(note.value(QStringLiteral("shape")).toObject());
      require(shape.width() > 0.0 && shape.height() > 0.0,
              QStringLiteral("%1 note geometry is empty").arg(id));
      const qsizetype position = note.value(QStringLiteral("position")).toInteger();
      const QRectF native = layout.notes.at(position).rect;
      const QJsonObject expected = note.value(QStringLiteral("shape")).toObject();
      requireNear(native.x(), expected.value(QStringLiteral("x")).toDouble(),
                  QStringLiteral("%1 note %2 x").arg(id).arg(position));
      requireNear(native.y(), expected.value(QStringLiteral("y")).toDouble(),
                  QStringLiteral("%1 note %2 y").arg(id).arg(position));
      requireNear(native.width(), expected.value(QStringLiteral("width")).toDouble(),
                  QStringLiteral("%1 note %2 width").arg(id).arg(position));
      requireNear(native.height(), expected.value(QStringLiteral("height")).toDouble(),
                  QStringLiteral("%1 note %2 height").arg(id).arg(position));
    }
    require(input.fragments.size() == fragments.size(), QStringLiteral("%1 fragment input count mismatch").arg(id));
    for (const QJsonValue& fragmentValue : fragments) {
      const QSizeF outline = sizeOf(fragmentValue.toObject().value(QStringLiteral("outline")).toObject());
      require(outline.width() > 0.0 && outline.height() > 0.0,
              QStringLiteral("%1 fragment geometry is empty").arg(id));
      const qsizetype position = fragmentValue.toObject().value(QStringLiteral("position")).toInteger();
      const SequenceLayoutFragment& nativeFragment = layout.fragments.at(position);
      const QRectF native = nativeFragment.rect;
      const QJsonObject expected = fragmentValue.toObject().value(QStringLiteral("outline")).toObject();
      requireNear(native.x(), expected.value(QStringLiteral("x")).toDouble(),
                  QStringLiteral("%1 fragment %2 x").arg(id).arg(position));
      requireNear(native.y(), expected.value(QStringLiteral("y")).toDouble(),
                  QStringLiteral("%1 fragment %2 y").arg(id).arg(position));
      requireNear(native.width(), expected.value(QStringLiteral("width")).toDouble(),
                  QStringLiteral("%1 fragment %2 width").arg(id).arg(position));
      requireNear(native.height(), expected.value(QStringLiteral("height")).toDouble(),
                  QStringLiteral("%1 fragment %2 height").arg(id).arg(position));
      const QJsonObject structure = fragmentValue.toObject().value(QStringLiteral("structure")).toObject();
      require(structure.value(QStringLiteral("lineCount")).toInt() == 4 + nativeFragment.sectionY.size() &&
                  structure.value(QStringLiteral("sectionLineCount")).toInt() == nativeFragment.sectionY.size(),
              QStringLiteral("%1 fragment %2 SVG line structure mismatch").arg(id).arg(position));
    }

    const QJsonArray markerDefs = fixture.value(QStringLiteral("svgStructure")).toObject()
                                      .value(QStringLiteral("markers")).toArray();
    require(markerDefs.size() >= 8, QStringLiteral("%1 SVG marker defs coverage regressed").arg(id));
    for (const QJsonValue& markerValue : markerDefs) {
      const QJsonObject marker = markerValue.toObject();
      require(!marker.value(QStringLiteral("id")).toString().isEmpty() &&
                  !marker.value(QStringLiteral("markerWidth")).toString().isEmpty() &&
                  !marker.value(QStringLiteral("markerHeight")).toString().isEmpty() &&
                  !marker.value(QStringLiteral("orient")).toString().isEmpty(),
              QStringLiteral("%1 incomplete SVG marker definition").arg(id));
    }

    require(scene.participants.size() == layout.participants.size() &&
                scene.messages.size() == layout.messages.size() &&
                scene.activations.size() == layout.activations.size() &&
                scene.notes.size() == layout.notes.size() &&
                scene.fragments.size() == layout.fragments.size(),
            QStringLiteral("%1 sequence scene lost ordered layout primitives").arg(id));

    participantCount += participants.size(); lifelineCount += lifelines.size();
    messageCount += messages.size(); activationCount += activations.size();
    noteCount += notes.size(); fragmentCount += fragments.size();
  }

  for (const QString& axis : {QStringLiteral("participant-size"), QStringLiteral("lifeline"),
                              QStringLiteral("message-spacing"), QStringLiteral("activation-stack"),
                              QStringLiteral("note-geometry"), QStringLiteral("fragment-geometry")})
    require(coveredAxes.contains(axis), QStringLiteral("Sequence layout axis is uncovered: %1").arg(axis));
  require(participantCount >= 15 && lifelineCount == participantCount && messageCount >= 15 &&
              activationCount >= 3 && noteCount >= 5 && fragmentCount >= 6,
          QStringLiteral("Sequence layout geometry coverage regressed"));

  qDebug() << "MermaidSequenceLayoutOracleTest:" << cases.size()
           << "fixed-Noto cases covering all layout input axes passed";
  return 0;
}
