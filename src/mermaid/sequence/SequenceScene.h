#pragma once

#include "mermaid/sequence/SequenceLayout.h"
#include "mermaid/sequence/SequenceLabel.h"

namespace muffin::mermaid::sequence {

struct SequenceSceneStyle {
  QString actorFill = QStringLiteral("#ECECFF");
  QString actorStroke = QStringLiteral("#9370DB");
  QString textColor = QStringLiteral("#333333");
  QString actorTextColor = QStringLiteral("#333333");
  QString signalColor = QStringLiteral("#333333");
  QString signalTextColor = QStringLiteral("#333333");
  QString lifelineColor = QStringLiteral("#999999");
  QString noteFill = QStringLiteral("#fff5ad");
  QString noteStroke = QStringLiteral("#aaaa33");
  QString noteTextColor = QStringLiteral("#333333");
  QString activationFill = QStringLiteral("#f4f4f4");
  QString activationStroke = QStringLiteral("#666666");
  QString fragmentFill = QStringLiteral("transparent");
  QString fragmentStroke = QStringLiteral("#666666");
  QString loopTextColor = QStringLiteral("#333333");
  QString labelFill = QStringLiteral("#eaeaea");
  QString labelStroke = QStringLiteral("#666666");
  QString labelTextColor = QStringLiteral("#333333");
  QString sequenceNumberColor = QStringLiteral("#333333");
  QString boxStroke = QStringLiteral("rgba(0,0,0,0.5)");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
};

// Immutable geometry consumed by the sequence painter. The scene never reads
// the parser DB and never performs placement.
struct SequenceScene {
  QRectF bounds;
  QVector<SequenceLayoutBox> boxes;
  QVector<SequenceLabelDocument> boxLabels;
  QVector<SequenceLayoutParticipant> participants;
  QVector<SequenceLabelDocument> participantLabels;
  QVector<SequenceLayoutMessage> messages;
  QVector<SequenceLabelDocument> messageLabels;
  QVector<SequenceLayoutActivation> activations;
  QVector<SequenceLayoutNote> notes;
  QVector<SequenceLabelDocument> noteLabels;
  QVector<SequenceLayoutFragment> fragments;
  QVector<SequenceLabelDocument> fragmentKindLabels;
  QVector<SequenceLabelDocument> fragmentLabels;
  QVector<SequenceLayoutNumber> sequenceNumbers;
  SequenceSceneStyle style;
};

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout,
                                 SequenceSceneStyle style = {});

}  // namespace muffin::mermaid::sequence
