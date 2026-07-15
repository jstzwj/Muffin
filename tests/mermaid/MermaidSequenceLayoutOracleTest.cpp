#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceLayout.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
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
              QLatin1String("2bfd8ae4133e334df003fa5cf4eba1fddec089bfaa33dec541006011e9f50b48"),
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
    for (const QJsonValue& message : messages)
      measurements.messages.append(sizeOf(message.toObject().value(QStringLiteral("label")).toObject()));
    for (const QJsonValue& note : notes)
      measurements.notes.append(sizeOf(note.toObject().value(QStringLiteral("label")).toObject()));
    for (const QJsonValue& fragment : fragments) {
      const QJsonArray labels = fragment.toObject().value(QStringLiteral("labels")).toArray();
      measurements.fragments.append(labels.isEmpty() ? QSizeF{} : sizeOf(labels.first().toObject()));
    }
    const SequenceLayoutInput input = buildSequenceLayoutInput(data, measurements);

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
    }

    require(input.messages.size() == messages.size(), QStringLiteral("%1 message input count mismatch").arg(id));
    qreal previousMessageY = -1e9;
    for (const QJsonValue& messageValue : messages) {
      const QJsonObject message = messageValue.toObject();
      const QJsonObject box = message.value(QStringLiteral("box")).toObject();
      const qreal y = box.value(QStringLiteral("y")).toDouble();
      require(y > previousMessageY, QStringLiteral("%1 message vertical order is unstable").arg(id));
      previousMessageY = y;
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
    }

    require(input.notes.size() == notes.size(), QStringLiteral("%1 note input count mismatch").arg(id));
    for (const QJsonValue& noteValue : notes) {
      const QJsonObject note = noteValue.toObject();
      const QSizeF shape = sizeOf(note.value(QStringLiteral("shape")).toObject());
      require(shape.width() > 0.0 && shape.height() > 0.0,
              QStringLiteral("%1 note geometry is empty").arg(id));
    }
    require(input.fragments.size() == fragments.size(), QStringLiteral("%1 fragment input count mismatch").arg(id));
    for (const QJsonValue& fragmentValue : fragments) {
      const QSizeF outline = sizeOf(fragmentValue.toObject().value(QStringLiteral("outline")).toObject());
      require(outline.width() > 0.0 && outline.height() > 0.0,
              QStringLiteral("%1 fragment geometry is empty").arg(id));
    }

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
