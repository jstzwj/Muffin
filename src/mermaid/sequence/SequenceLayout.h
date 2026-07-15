#pragma once

#include "mermaid/sequence/SequenceDiagram.h"

#include <QMap>
#include <QSizeF>

namespace muffin::mermaid::sequence {

struct SequenceLayoutMeasurements {
  QMap<QString, QSizeF> participants;
  QVector<QSizeF> messages;
  QVector<QSizeF> notes;
  QVector<QSizeF> fragments;
};

struct SequenceLayoutParticipantInput {
  int actorIndex = -1;
  QString id;
  QString label;
  QString type;
  QSizeF measuredLabel;
};

struct SequenceLayoutMessageInput {
  int messageIndex = -1;
  QString from;
  QString to;
  QString label;
  int type = 0;
  QSizeF measuredLabel;
};

struct SequenceLayoutActivationInput {
  int messageIndex = -1;
  QString actor;
  int depth = 0;
  bool begin = false;
};

struct SequenceLayoutNoteInput {
  int messageIndex = -1;
  QString from;
  QString to;
  QString label;
  int placement = -1;
  QSizeF measuredLabel;
};

struct SequenceLayoutFragmentInput {
  int messageIndex = -1;
  QString kind;
  QString label;
  int depth = 0;
  QSizeF measuredLabel;
};

struct SequenceLayoutInput {
  QVector<SequenceLayoutParticipantInput> participants;
  QVector<SequenceLayoutMessageInput> messages;
  QVector<SequenceLayoutActivationInput> activations;
  QVector<SequenceLayoutNoteInput> notes;
  QVector<SequenceLayoutFragmentInput> fragments;
  int maximumActivationDepth = 0;
  int maximumFragmentDepth = 0;
};

// Converts the parser DB into the same semantic streams consumed by Mermaid's
// sequence renderer. Measurements are supplied by the label oracle, keeping
// text shaping errors separate from placement errors.
SequenceLayoutInput buildSequenceLayoutInput(
    const SequenceData& data, const SequenceLayoutMeasurements& measurements = {});

}  // namespace muffin::mermaid::sequence
