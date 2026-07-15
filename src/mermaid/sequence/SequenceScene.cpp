#include "mermaid/sequence/SequenceScene.h"

namespace muffin::mermaid::sequence {

SequenceScene buildSequenceScene(const SequenceLayoutResult& layout) {
  return {layout.bounds, layout.participants, layout.messages, layout.activations,
          layout.notes, layout.fragments};
}

}  // namespace muffin::mermaid::sequence
