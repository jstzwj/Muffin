#include "document/NodeIndex.h"

#include <algorithm>

namespace muffin {

void NodeIndex::rebuild(MarkdownNode& root) {
  nodes_.clear();
  blocksInDocumentOrder_.clear();
  addSubtreeInDocumentOrder(root);
}

void NodeIndex::addSubtree(MarkdownNode& node) {
  addSubtreeInDocumentOrder(node);
}

void NodeIndex::removeSubtree(MarkdownNode& node) {
  QHash<NodeId, bool> removedIds;
  collectSubtreeIds(node, removedIds);
  for (auto it = removedIds.constBegin(); it != removedIds.constEnd(); ++it) {
    nodes_.remove(it.key());
  }
  blocksInDocumentOrder_.erase(
      std::remove_if(
          blocksInDocumentOrder_.begin(),
          blocksInDocumentOrder_.end(),
          [&removedIds](const MarkdownNode* block) {
            return block && removedIds.contains(block->id());
          }),
      blocksInDocumentOrder_.end());
}

MarkdownNode* NodeIndex::find(NodeId id) const {
  return nodes_.value(id, nullptr);
}

bool NodeIndex::contains(NodeId id) const {
  return nodes_.contains(id);
}

MarkdownNode* NodeIndex::firstBlock() const {
  return blocksInDocumentOrder_.isEmpty() ? nullptr : blocksInDocumentOrder_.first();
}

MarkdownNode* NodeIndex::lastBlock() const {
  return blocksInDocumentOrder_.isEmpty() ? nullptr : blocksInDocumentOrder_.last();
}

qsizetype NodeIndex::size() const {
  return nodes_.size();
}

void NodeIndex::addSubtreeInDocumentOrder(MarkdownNode& node) {
  nodes_.insert(node.id(), &node);
  if (node.type() != BlockType::Document) {
    blocksInDocumentOrder_.push_back(&node);
  }
  for (const auto& child : node.children()) {
    addSubtreeInDocumentOrder(*child);
  }
}

void NodeIndex::collectSubtreeIds(MarkdownNode& node, QHash<NodeId, bool>& removedIds) const {
  removedIds.insert(node.id(), true);
  for (const auto& child : node.children()) {
    collectSubtreeIds(*child, removedIds);
  }
}

}  // namespace muffin
