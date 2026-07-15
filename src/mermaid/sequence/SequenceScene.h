#pragma once

#include "mermaid/sequence/SequenceLayout.h"

namespace muffin::mermaid::sequence {

struct SequenceSceneStyle {
  QString actorFill = QStringLiteral("#ECECFF");
  QString actorStroke = QStringLiteral("#9370DB");
  QString textColor = QStringLiteral("#333333");
  QString signalColor = QStringLiteral("#333333");
  QString lifelineColor = QStringLiteral("#999999");
  QString noteFill = QStringLiteral("#fff5ad");
  QString noteStroke = QStringLiteral("#aaaa33");
  QString activationFill = QStringLiteral("#f4f4f4");
  QString activationStroke = QStringLiteral("#666666");
  QString fragmentFill = QStringLiteral("transparent");
  QString fragmentStroke = QStringLiteral("#666666");
  QString labelFill = QStringLiteral("#eaeaea");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
};

// Immutable geometry consumed by the sequence painter. The scene never reads
// the parser DB and never performs placement.
struct SequenceScene {
  QRectF bounds;
  QVector<SequenceLayoutParticipant> participants;
  QVector<SequenceLayoutMessage> messages;
  QVector<SequenceLayoutActivation> activations;
  QVector<SequenceLayoutNote> notes;
  QVector<SequenceLayoutFragment> fragments;
  SequenceSceneStyle style;
};

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout,
                                 SequenceSceneStyle style = {});

}  // namespace muffin::mermaid::sequence
