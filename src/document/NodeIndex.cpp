#include "document/NodeIndex.h"

#include <algorithm>

namespace muffin {

void NodeIndex::rebuild(MarkdownNode& root) {
  rootPtr_ = &root;
  orderDirty_ = false;
  nodes_.clear();
  blocksInDocumentOrder_.clear();
  addSubtreeInDocumentOrder(root);
}

void NodeIndex::addSubtree(MarkdownNode& node) {
  ensureFreshOrder();
  addSubtreeInDocumentOrder(node);
}

void NodeIndex::removeSubtree(MarkdownNode& node) {
  ensureFreshOrder();
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

void NodeIndex::removeSubtreesFromLookup(const std::vector<MarkdownNode*>& subtrees) {
  QHash<NodeId, bool> removedIds;
  for (MarkdownNode* subtree : subtrees) {
    if (subtree) {
      collectSubtreeIds(*subtree, removedIds);
    }
  }
  for (auto it = removedIds.constBegin(); it != removedIds.constEnd(); ++it) {
    nodes_.remove(it.key());
  }
  // blocksInDocumentOrder_ is intentionally left stale; it is rebuilt from the tree on the next
  // firstBlock()/lastBlock() (the only readers). find() reads only nodes_, now up to date.
  orderDirty_ = true;
}

void NodeIndex::addSubtreesToLookup(const std::vector<MarkdownNode*>& subtrees) {
  for (MarkdownNode* subtree : subtrees) {
    if (subtree) {
      addSubtreeInDocumentOrder(*subtree);
    }
  }
  orderDirty_ = true;
}

MarkdownNode* NodeIndex::find(NodeId id) const {
  return nodes_.value(id, nullptr);
}

bool NodeIndex::contains(NodeId id) const {
  return nodes_.contains(id);
}

MarkdownNode* NodeIndex::firstBlock() {
  ensureFreshOrder();
  return blocksInDocumentOrder_.isEmpty() ? nullptr : blocksInDocumentOrder_.first();
}

MarkdownNode* NodeIndex::lastBlock() {
  ensureFreshOrder();
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

void NodeIndex::ensureFreshOrder() {
  if (!orderDirty_) {
    return;
  }
  // Re-derive both structures from the authoritative tree. After a replaceTopLevelRange the tree
  // already holds the new top-level children, so this is correct; it runs only for firstBlock/
  // lastBlock (test-only), never on the find() hot path.
  orderDirty_ = false;
  nodes_.clear();
  blocksInDocumentOrder_.clear();
  if (rootPtr_) {
    addSubtreeInDocumentOrder(*rootPtr_);
  }
}

}  // namespace muffin
