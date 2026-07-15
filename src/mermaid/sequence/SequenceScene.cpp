#include "mermaid/sequence/SequenceScene.h"

namespace muffin::mermaid::sequence {

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout, SequenceSceneStyle style) {
  return {layout.bounds, layout.participants, layout.messages, layout.activations,
          layout.notes, layout.fragments, layout.sequenceNumbers, std::move(style)};
}

}  // namespace muffin::mermaid::sequence
