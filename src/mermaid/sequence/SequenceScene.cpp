#include "mermaid/sequence/SequenceScene.h"

#include <stdexcept>

namespace muffin::mermaid::sequence {

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout,
                                 SequenceSceneStyle style,
                                 const SequencePreparedLabels& prepared,
                                 bool requirePrepared) {
  SequenceScene scene;
  scene.bounds = layout.bounds;
  scene.logicalBounds = layout.logicalBounds;
  scene.boxes = layout.boxes;
  scene.participants = layout.participants;
  scene.messages = layout.messages;
  scene.activations = layout.activations;
  scene.notes = layout.notes;
  scene.fragments = layout.fragments;
  scene.sequenceNumbers = layout.sequenceNumbers;
  scene.forceMenus = layout.forceMenus;
  scene.style = std::move(style);
  const auto fallbackLabel = [&](const QString& text,
                                 SequenceLabelKind kind) {
    return prepareSequenceLabel(
        parseSequenceLabel(text, kind), scene.style.fontSize);
  };
  const auto labelFor = [&](const auto& labels, const auto& key,
                            const QString& text, SequenceLabelKind kind) {
    const auto found = labels.constFind(key);
    if (found != labels.cend()) return found.value();
    if (requirePrepared)
      throw std::logic_error(
          QStringLiteral("Missing prepared sequence label for key '%1'")
              .arg(key).toStdString());
    return fallbackLabel(text, kind);
  };
  for (const auto& box : scene.boxes)
    scene.boxLabels.append(labelFor(prepared.boxesByIndex, box.boxIndex,
                                    box.label, SequenceLabelKind::Box));
  for (const auto& actor : scene.participants)
    scene.participantLabels.append(labelFor(
        prepared.participantsById, actor.id, actor.label,
        SequenceLabelKind::Participant));
  for (const auto& message : scene.messages)
    scene.messageLabels.append(labelFor(
        prepared.messagesByIndex, message.messageIndex, message.label,
        SequenceLabelKind::Message));
  for (const auto& note : scene.notes)
    scene.noteLabels.append(labelFor(
        prepared.notesByIndex, note.messageIndex, note.label,
        SequenceLabelKind::Note));
  for (const auto& fragment : scene.fragments) {
    scene.fragmentKindLabels.append(labelFor(
        prepared.fragmentKindsByIndex, fragment.messageIndex, fragment.kind,
        SequenceLabelKind::Box));
    scene.fragmentLabels.append(labelFor(
        prepared.fragmentsByIndex, fragment.messageIndex, fragment.label,
        SequenceLabelKind::Fragment));
  }
  for (const auto& sourceMenu : layout.menus) {
    SequenceSceneMenu menu;
    menu.actorId = sourceMenu.actorId;
    menu.panelRect = sourceMenu.panelRect;
    for (const auto& sourceItem : sourceMenu.items) {
      SequenceSceneMenuItem item;
      item.label = sourceItem.label;
      item.link = sourceItem.link;
      item.hitRect = sourceItem.hitRect;
      item.labelRect = sourceItem.labelRect;
      item.labelDocument = labelFor(
          prepared.menuItemsByKey,
          sequenceMenuLabelKey(sourceMenu.actorId, sourceItem.label),
          sourceItem.label, SequenceLabelKind::Participant);
      menu.items.append(std::move(item));
    }
    scene.menus.append(std::move(menu));
  }
  return scene;
}

}  // namespace muffin::mermaid::sequence
