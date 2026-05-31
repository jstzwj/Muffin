#include "document/NodeIndex.h"

namespace muffin {

void NodeIndex::rebuild(MarkdownNode& root) {
  nodes_.clear();
  addSubtree(root);
}

void NodeIndex::addSubtree(MarkdownNode& node) {
  nodes_.insert(node.id(), &node);
  for (const auto& child : node.children()) {
    addSubtree(*child);
  }
}

void NodeIndex::removeSubtree(MarkdownNode& node) {
  nodes_.remove(node.id());
  for (const auto& child : node.children()) {
    removeSubtree(*child);
  }
}

MarkdownNode* NodeIndex::find(NodeId id) const {
  return nodes_.value(id, nullptr);
}

bool NodeIndex::contains(NodeId id) const {
  return nodes_.contains(id);
}

MarkdownNode* NodeIndex::firstBlock() const {
  return nodes_.isEmpty() ? nullptr : nodes_.constBegin().value();
}

MarkdownNode* NodeIndex::lastBlock() const {
  return firstBlock();
}

qsizetype NodeIndex::size() const {
  return nodes_.size();
}

}  // namespace muffin
