#include "mermaid/sequence/SequenceDiagram.h"
#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceLabel.h"
#include "mermaid/sequence/SequenceScene.h"
#include "mermaid/sequence/SequenceScenePainter.h"
#include "mermaid/MermaidFontRegistry.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
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

bool isSignalType(int type) {
  return (type >= 0 && type <= 6 && type != 2) || type == 24 || type == 25 ||
         type == 33 || type == 34 || (type >= 41 && type <= 58);
}

void requireNear(qreal actual, qreal expected, const QString& context) {
  require(std::abs(actual - expected) <= 0.001,
          QStringLiteral("%1: native=%2 upstream=%3").arg(context).arg(actual).arg(expected));
}

QString markerName(const QString& url) {
  if (url.isEmpty()) return {};
  const qsizetype dash = url.indexOf(QLatin1Char('-'), url.indexOf(QLatin1String("sequence-layout-")) + 16);
  const qsizetype end = url.lastIndexOf(QLatin1Char(')'));
  return dash >= 0 && end > dash ? url.mid(dash + 1, end - dash - 1) : QString{};
}

void requireRectNear(const QRectF& actual, const QJsonObject& expected, qreal tolerance,
                     const QString& context) {
  const QRectF wanted(expected.value(QStringLiteral("x")).toDouble(),
                      expected.value(QStringLiteral("y")).toDouble(),
                      expected.value(QStringLiteral("width")).toDouble(),
                      expected.value(QStringLiteral("height")).toDouble());
  require(std::abs(actual.x() - wanted.x()) <= tolerance &&
              std::abs(actual.y() - wanted.y()) <= tolerance &&
              std::abs(actual.width() - wanted.width()) <= tolerance &&
              std::abs(actual.height() - wanted.height()) <= tolerance,
          QStringLiteral("%1: native=[%2,%3 %4x%5] upstream=[%6,%7 %8x%9]")
              .arg(context).arg(actual.x()).arg(actual.y()).arg(actual.width()).arg(actual.height())
              .arg(wanted.x()).arg(wanted.y()).arg(wanted.width()).arg(wanted.height()));
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected sequence layout fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open sequence layout fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Sequence layout Mermaid version drifted"));
  require(root.value(QStringLiteral("fontMode")).toString() == QLatin1String("bundled-noto"),
          QStringLiteral("Sequence layout must use the fixed Noto oracle"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("74e40fd0226142a7c572aa607619fdb7668718c2d415ac2e5f4dae301e660c53"),
          QStringLiteral("Sequence layout fixture changed; audit geometry and update its digest"));

  const QJsonObject configContract = root.value(QStringLiteral("configContract")).toObject();
  require(configContract.value(QStringLiteral("layout")).toArray().size() == 14 &&
              configContract.value(QStringLiteral("viewport")).toArray().size() == 3 &&
              configContract.value(QStringLiteral("upstreamInert")).toArray() ==
                  QJsonArray{QStringLiteral("messageMargin")},
          QStringLiteral("Sequence configuration ownership contract drifted"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 17, QStringLiteral("Sequence layout case count drifted"));
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
    require(config.value(QStringLiteral("fontFamily")).toString().contains(QLatin1String("Noto Sans")),
            QStringLiteral("%1 sequence layout font config drifted").arg(id));
    if (id == QLatin1String("custom-layout-and-viewport-config")) {
      require(config.value(QStringLiteral("actorMargin")).toInt() == 73 &&
                  config.value(QStringLiteral("messageMargin")).toInt() == 61 &&
                  config.value(QStringLiteral("activationWidth")).toInt() == 14 &&
                  config.value(QStringLiteral("diagramMarginX")).toInt() == 31 &&
                  config.value(QStringLiteral("diagramMarginY")).toInt() == 23 &&
                  config.value(QStringLiteral("bottomMarginAdj")).toInt() == 7 &&
                  config.value(QStringLiteral("wrap")).toBool(),
              QStringLiteral("%1 custom sequence config was not transferred").arg(id));
    } else if (id == QLatin1String("custom-activation-width")) {
      require(config.value(QStringLiteral("activationWidth")).toInt() == 14,
              QStringLiteral("%1 activation width config was not transferred").arg(id));
    } else {
      require(config.value(QStringLiteral("actorMargin")).toInt() == 50 &&
                  config.value(QStringLiteral("messageMargin")).toInt() == 35 &&
                  config.value(QStringLiteral("activationWidth")).toInt() == 10,
              QStringLiteral("%1 default sequence config drifted").arg(id));
    }

    const QStringList viewBoxParts = fixture.value(QStringLiteral("root")).toObject()
                                         .value(QStringLiteral("viewBox")).toString()
                                         .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    require(viewBoxParts.size() == 4,
            QStringLiteral("%1 sequence viewBox is malformed").arg(id));
    const qreal viewX = viewBoxParts.at(0).toDouble();
    const qreal viewY = viewBoxParts.at(1).toDouble();
    const qreal viewWidth = viewBoxParts.at(2).toDouble();
    const qreal viewHeight = viewBoxParts.at(3).toDouble();
    requireNear(viewY, -config.value(QStringLiteral("diagramMarginY")).toDouble(),
                QStringLiteral("%1 viewport y margin").arg(id));
    require(viewWidth > 2.0 * config.value(QStringLiteral("diagramMarginX")).toDouble() &&
                viewHeight > 2.0 * config.value(QStringLiteral("diagramMarginY")).toDouble(),
            QStringLiteral("%1 viewport has no positive logical canvas").arg(id));

    const SequenceDiagram diagram = SequenceDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    const SequenceData& data = diagram.data();
    SequenceLayoutMeasurements measurements;
    const QJsonArray participants = fixture.value(QStringLiteral("participants")).toArray();
    const QJsonArray footers = fixture.value(QStringLiteral("footers")).toArray();
    const QJsonArray participantBoxes = fixture.value(QStringLiteral("participantBoxes")).toArray();
    const QJsonArray lifelines = fixture.value(QStringLiteral("lifelines")).toArray();
    const QJsonArray messages = fixture.value(QStringLiteral("messages")).toArray();
    const QJsonArray centralConnections = fixture.value(QStringLiteral("centralConnections")).toArray();
    const QJsonArray sequenceNumbers = fixture.value(QStringLiteral("sequenceNumbers")).toArray();
    const QJsonArray activations = fixture.value(QStringLiteral("activations")).toArray();
    const QJsonArray notes = fixture.value(QStringLiteral("notes")).toArray();
    const QJsonArray fragments = fixture.value(QStringLiteral("fragments")).toArray();
    for (const QJsonValue& participant : participants) {
      const QJsonObject object = participant.toObject();
      measurements.participants.insert(object.value(QStringLiteral("id")).toString(),
                                       sizeOf(object.value(QStringLiteral("label")).toObject()));
      measurements.participantDisplayById.insert(
          object.value(QStringLiteral("id")).toString(),
          object.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString());
    }
    for (const QJsonValue& box : participantBoxes)
      measurements.boxes.append(sizeOf(box.toObject().value(QStringLiteral("label")).toObject()));
    for (const QJsonValue& message : messages) {
      const QJsonObject object = message.toObject();
      const QSizeF measured = sizeOf(object.value(QStringLiteral("label")).toObject());
      measurements.messages.append(measured);
      measurements.messagesByIndex.insert(messageIndex(object.value(QStringLiteral("id")).toString()), measured);
      measurements.messageDisplayByIndex.insert(
          messageIndex(object.value(QStringLiteral("id")).toString()),
          object.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString());
    }
    for (const QJsonValue& note : notes) {
      const QJsonObject object = note.toObject();
      const QSizeF measured = sizeOf(object.value(QStringLiteral("label")).toObject());
      measurements.notes.append(measured);
      measurements.notesByIndex.insert(messageIndex(object.value(QStringLiteral("id")).toString()), measured);
      measurements.noteDisplayByIndex.insert(
          messageIndex(object.value(QStringLiteral("id")).toString()),
          object.value(QStringLiteral("label")).toObject().value(QStringLiteral("text")).toString());
    }
    for (const QJsonValue& fragment : fragments) {
      const QJsonArray labels = fragment.toObject().value(QStringLiteral("labels")).toArray();
      measurements.fragments.append(labels.isEmpty() ? QSizeF{} : sizeOf(labels.first().toObject()));
    }
    const bool globalWrap = config.value(QStringLiteral("wrap")).toBool();
    const qreal marginWrapWidth = std::max(
        1.0, config.value(QStringLiteral("width")).toDouble() -
                 2.0 * config.value(QStringLiteral("wrapPadding")).toDouble());
    for (qsizetype index = 0; index < data.messages.size(); ++index) {
      const SequenceMessage& message = data.messages.at(index);
      if (!(message.wrap || globalWrap) ||
          (message.type != 2 && !isSignalType(message.type)))
        continue;
      auto marginLabel = parseSequenceLabel(
          message.message.toString(), message.type == 2 ? SequenceLabelKind::Note
                                                       : SequenceLabelKind::Message);
      marginLabel = wrapSequenceLabel(std::move(marginLabel),
          muffin::mermaid::MermaidFontRegistry::cssFamilyStack(), 16.0, marginWrapWidth);
      marginLabel = prepareSequenceLabel(std::move(marginLabel), 16.0);
      const QSizeF marginSize = layoutSequenceLabel(
          marginLabel, muffin::mermaid::MermaidFontRegistry::cssFamilyStack(),
          16.0, 22.0).size;
      if (message.type == 2)
        measurements.marginNotesByIndex.insert(static_cast<int>(index), marginSize);
      else
        measurements.marginMessagesByIndex.insert(static_cast<int>(index), marginSize);
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
    options.wrap = config.value(QStringLiteral("wrap")).toBool();
    options.mirrorActors = config.value(QStringLiteral("mirrorActors")).toBool(true);
    options.hideUnusedParticipants = config.value(QStringLiteral("hideUnusedParticipants")).toBool();
    const SequenceLayoutResult layout = layoutSequence(data, measurements, options);
    const SequenceScene scene = buildSequenceScene(layout);
    const qreal logicalCanvasStartX =
        viewX + config.value(QStringLiteral("diagramMarginX")).toDouble();
    const qreal logicalCanvasWidth =
        viewWidth - 2.0 * config.value(QStringLiteral("diagramMarginX")).toDouble();
    require(std::isfinite(logicalCanvasStartX) && logicalCanvasWidth > 0.0,
            QStringLiteral("%1 logical viewport model is invalid").arg(id));
    requireRectNear(layout.logicalBounds,
                    fixture.value(QStringLiteral("viewport")).toObject(), 0.001,
                    QStringLiteral("%1 logical viewport").arg(id));
    SequenceViewportOptions viewportOptions;
    viewportOptions.diagramMarginX = config.value(QStringLiteral("diagramMarginX")).toDouble();
    viewportOptions.diagramMarginY = config.value(QStringLiteral("diagramMarginY")).toDouble();
    viewportOptions.boxMargin = options.boxMargin;
    viewportOptions.bottomMarginAdj = config.value(QStringLiteral("bottomMarginAdj")).toDouble();
    viewportOptions.mirrorActors = options.mirrorActors;
    const QRectF nativeViewport = sequenceViewportRect(scene, viewportOptions);
    requireNear(nativeViewport.x(), viewX, QStringLiteral("%1 viewBox x").arg(id));
    requireNear(nativeViewport.y(), viewY, QStringLiteral("%1 viewBox y").arg(id));
    requireNear(nativeViewport.width(), viewWidth, QStringLiteral("%1 viewBox width").arg(id));
    requireNear(nativeViewport.height(), viewHeight, QStringLiteral("%1 viewBox height").arg(id));

    require(layout.participants.size() == participants.size() && lifelines.size() == participants.size(),
            QStringLiteral("%1 participant/lifeline input count mismatch").arg(id));
    require(!options.hideUnusedParticipants || input.participants.size() >= layout.participants.size(),
            QStringLiteral("%1 hidden participant semantic input was lost").arg(id));
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
      if (fixture.value(QStringLiteral("axes")).toArray().contains(QStringLiteral("participant-painted-bounds")) ||
          fixture.value(QStringLiteral("axes")).toArray().contains(QStringLiteral("participant-lifecycle")))
        requireRectNear(native->topPaintedBounds,
                        participant.value(QStringLiteral("paintedBox")).toObject(), 1.0,
                        QStringLiteral("%1/%2 painted bbox").arg(id, actorId));
      require(native->created == data.createdActors.contains(actorId) &&
                  native->destroyed == data.destroyedActors.contains(actorId),
              QStringLiteral("%1/%2 lifecycle flags mismatch").arg(id, actorId));
    }
    require(layout.boxes.size() == participantBoxes.size(),
            QStringLiteral("%1 participant box count mismatch").arg(id));
    for (const QJsonValue& boxValue : participantBoxes) {
      const QJsonObject box = boxValue.toObject();
      const qsizetype position = box.value(QStringLiteral("position")).toInteger();
      const SequenceLayoutBox& native = layout.boxes.at(position);
      const QJsonObject shape = box.value(QStringLiteral("shape")).toObject();
      requireRectNear(native.rect, shape, 0.001,
                      QStringLiteral("%1 participant box %2").arg(id).arg(position));
      require(native.fill == shape.value(QStringLiteral("fill")).toString() &&
                  native.label == box.value(QStringLiteral("label")).toObject()
                                      .value(QStringLiteral("text")).toString(),
              QStringLiteral("%1 participant box %2 style/label mismatch").arg(id).arg(position));
    }
    if (fixture.value(QStringLiteral("axes")).toArray().contains(QStringLiteral("participant-lifecycle"))) {
      for (const QJsonValue& footerValue : footers) {
        const QJsonObject footer = footerValue.toObject();
        const QString actorId = footer.value(QStringLiteral("id")).toString();
        const auto native = std::find_if(layout.participants.cbegin(), layout.participants.cend(),
                                         [&](const auto& item) { return item.id == actorId; });
        require(native != layout.participants.cend(),
                QStringLiteral("%1 orphan footer %2").arg(id, actorId));
        requireRectNear(native->bottomPaintedBounds,
                        footer.value(QStringLiteral("paintedBox")).toObject(), 1.0,
                        QStringLiteral("%1/%2 footer painted bbox").arg(id, actorId));
      }
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
      require(markerName(structure.value(QStringLiteral("markerStart")).toString()) == native.markerStart &&
                  markerName(structure.value(QStringLiteral("markerEnd")).toString()) == native.markerEnd,
              QStringLiteral("%1 message %2 marker mismatch: native=%3/%4 upstream=%5/%6")
                  .arg(id).arg(position).arg(native.markerStart, native.markerEnd,
                      markerName(structure.value(QStringLiteral("markerStart")).toString()),
                      markerName(structure.value(QStringLiteral("markerEnd")).toString())));
    }
    QVector<QPointF> nativeCentralConnections;
    for (const SequenceLayoutMessage& message : layout.messages)
      nativeCentralConnections += message.centralConnections;
    require(nativeCentralConnections.size() == centralConnections.size(),
            QStringLiteral("%1 central connection count mismatch").arg(id));
    for (qsizetype index = 0; index < centralConnections.size(); ++index) {
      const QJsonObject expected = centralConnections.at(index).toObject();
      requireNear(nativeCentralConnections.at(index).x(), expected.value(QStringLiteral("cx")).toDouble(),
                  QStringLiteral("%1 central %2 x").arg(id).arg(index));
      requireNear(nativeCentralConnections.at(index).y(), expected.value(QStringLiteral("cy")).toDouble(),
                  QStringLiteral("%1 central %2 y").arg(id).arg(index));
    }
    require(layout.sequenceNumbers.size() == sequenceNumbers.size(),
            QStringLiteral("%1 sequence number count mismatch").arg(id));
    for (qsizetype index = 0; index < sequenceNumbers.size(); ++index) {
      const QJsonObject expected = sequenceNumbers.at(index).toObject();
      const SequenceLayoutNumber& native = layout.sequenceNumbers.at(index);
      require(native.text == expected.value(QStringLiteral("text")).toString(),
              QStringLiteral("%1 sequence number %2 text mismatch").arg(id).arg(index));
      requireNear(native.position.x(), expected.value(QStringLiteral("x")).toDouble(),
                  QStringLiteral("%1 sequence number %2 x").arg(id).arg(index));
      requireNear(native.position.y(), expected.value(QStringLiteral("y")).toDouble(),
                  QStringLiteral("%1 sequence number %2 y").arg(id).arg(index));
      requireNear(native.fontSize,
                  expected.value(QStringLiteral("fontSize")).toString().chopped(2).toDouble(),
                  QStringLiteral("%1 sequence number %2 font size").arg(id).arg(index));
    }

    int activationStarts = 0;
    for (const SequenceLayoutActivationInput& activation : input.activations)
      if (activation.begin) ++activationStarts;
    require(activationStarts >= activations.size() && layout.activations.size() == activations.size(),
            QStringLiteral("%1 activation lifecycle count mismatch").arg(id));
    for (const QJsonValue& activationValue : activations) {
      const QJsonObject activation = activationValue.toObject();
      require(std::abs(activation.value(QStringLiteral("width")).toDouble() -
                       options.activationWidth) <= 0.001 &&
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

    require(scene.boxes.size() == layout.boxes.size() &&
                scene.participants.size() == layout.participants.size() &&
                scene.messages.size() == layout.messages.size() &&
                scene.activations.size() == layout.activations.size() &&
                scene.notes.size() == layout.notes.size() &&
                scene.fragments.size() == layout.fragments.size() &&
                scene.sequenceNumbers.size() == layout.sequenceNumbers.size(),
            QStringLiteral("%1 sequence scene lost ordered layout primitives").arg(id));

    participantCount += participants.size(); lifelineCount += lifelines.size();
    messageCount += messages.size(); activationCount += activations.size();
    noteCount += notes.size(); fragmentCount += fragments.size();
  }

  for (const QString& axis : {QStringLiteral("participant-size"), QStringLiteral("participant-painted-bounds"),
                              QStringLiteral("participant-lifecycle"), QStringLiteral("message-marker"), QStringLiteral("lifeline"),
                              QStringLiteral("participant-box"), QStringLiteral("participant-visibility"),
                              QStringLiteral("footer-policy"), QStringLiteral("activation-lifecycle"),
                              QStringLiteral("central-connection"), QStringLiteral("autonumber"),
                              QStringLiteral("self-message"), QStringLiteral("right-angles"),
                              QStringLiteral("message-spacing"), QStringLiteral("activation-stack"),
                              QStringLiteral("note-geometry"), QStringLiteral("fragment-geometry")})
    require(coveredAxes.contains(axis), QStringLiteral("Sequence layout axis is uncovered: %1").arg(axis));
  require(coveredAxes.contains(QStringLiteral("sequence-config")) &&
              coveredAxes.contains(QStringLiteral("viewport-config")),
          QStringLiteral("Sequence config differential axes are uncovered"));
  require(coveredAxes.contains(QStringLiteral("wrap-margin-stage")) &&
              coveredAxes.contains(QStringLiteral("wrap-final-stage")),
          QStringLiteral("Sequence two-stage wrap axes are uncovered"));
  require(participantCount >= 27 && lifelineCount == participantCount && messageCount >= 46 &&
              activationCount >= 3 && noteCount >= 5 && fragmentCount >= 6,
          QStringLiteral("Sequence layout geometry coverage regressed"));

  qDebug() << "MermaidSequenceLayoutOracleTest:" << cases.size()
           << "fixed-Noto cases covering all layout input axes passed";
  return 0;
}
