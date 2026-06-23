#pragma once

#include "document/MarkdownNode.h"

namespace muffin {

// The sibling that follows `node` in document order, climbing out of containers: `node`'s next
// sibling if it has one, else the next sibling of the nearest ancestor that has one. null at the
// document end. Does NOT descend into the returned sibling — callers combine it with
// firstEditableDescendant / lastEditableDescendant when they need the editable leaf.
//
// Why this exists (and node->nextSibling() does not suffice): an editable leaf nested inside a
// container — a paragraph inside a block quote, a list item inside a list, … — has its tree
// siblings INSIDE that container. The block that actually follows it in the source (a top-level
// thematic break, the next top-level paragraph, …) sits OUTSIDE, as a sibling of the container, so
// a naive node->nextSibling() never reaches it. This climb finds it at any nesting depth.
inline MarkdownNode* nextSiblingAcrossContainers(MarkdownNode& node) {
  for (MarkdownNode* n = &node; n; n = n->parent()) {
    if (MarkdownNode* sibling = n->nextSibling()) {
      return sibling;
    }
  }
  return nullptr;
}

// The sibling that precedes `node` in document order, climbing out of containers: `node`'s previous
// sibling if it has one, else the previous sibling of the nearest ancestor that has one. null when
// `node` is the first node in the document. Never returns an ancestor: an ancestor CONTAINS `node`,
// so it does not sit "before" it — this matters for the leading-block case, where returning the
// document root would masquerade as a real predecessor. The mirror of nextSiblingAcrossContainers.
inline MarkdownNode* previousSiblingAcrossContainers(MarkdownNode& node) {
  for (MarkdownNode* n = &node; n && n->parent(); n = n->parent()) {
    if (MarkdownNode* sibling = n->previousSibling()) {
      return sibling;
    }
  }
  return nullptr;
}

}  // namespace muffin
