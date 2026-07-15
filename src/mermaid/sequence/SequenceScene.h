#pragma once

#include "mermaid/sequence/SequenceLayout.h"

namespace muffin::mermaid::sequence {

// Immutable geometry consumed by the sequence painter. The scene never reads
// the parser DB and never performs placement.
struct SequenceScene {
  QRectF bounds;
  QVector<SequenceLayoutParticipant> participants;
  QVector<SequenceLayoutMessage> messages;
  QVector<SequenceLayoutActivation> activations;
  QVector<SequenceLayoutNote> notes;
  QVector<SequenceLayoutFragment> fragments;
};

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout);

}  // namespace muffin::mermaid::sequence
