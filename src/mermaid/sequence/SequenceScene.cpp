#include "mermaid/sequence/SequenceScene.h"

namespace muffin::mermaid::sequence {

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout, SequenceSceneStyle style) {
  SequenceScene scene;
  scene.bounds = layout.bounds;
  scene.boxes = layout.boxes;
  scene.participants = layout.participants;
  scene.messages = layout.messages;
  scene.activations = layout.activations;
  scene.notes = layout.notes;
  scene.fragments = layout.fragments;
  scene.sequenceNumbers = layout.sequenceNumbers;
  scene.style = std::move(style);
  for (const auto& box : scene.boxes)
    scene.boxLabels.append(parseSequenceLabel(box.label, SequenceLabelKind::Box));
  for (const auto& actor : scene.participants)
    scene.participantLabels.append(parseSequenceLabel(actor.label, SequenceLabelKind::Participant));
  for (const auto& message : scene.messages)
    scene.messageLabels.append(parseSequenceLabel(message.label, SequenceLabelKind::Message));
  for (const auto& note : scene.notes)
    scene.noteLabels.append(parseSequenceLabel(note.label, SequenceLabelKind::Note));
  for (const auto& fragment : scene.fragments) {
    scene.fragmentKindLabels.append(parseSequenceLabel(fragment.kind, SequenceLabelKind::Box));
    scene.fragmentLabels.append(parseSequenceLabel(fragment.label, SequenceLabelKind::Fragment));
  }
  return scene;
}

}  // namespace muffin::mermaid::sequence
