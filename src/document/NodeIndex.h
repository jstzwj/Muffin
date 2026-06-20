#pragma once

#include "document/MarkdownNode.h"

#include <QHash>
#include <QVector>

#include <vector>

namespace muffin {

class NodeIndex {
public:
  void rebuild(MarkdownNode& root);
  void addSubtree(MarkdownNode& node);
  void removeSubtree(MarkdownNode& node);

  // Fast incremental path for replaceTopLevelRange (the per-keystroke local edit). Maintains only
  // the nodes_ lookup that find() reads; defers blocksInDocumentOrder_ until firstBlock/lastBlock
  // rebuild it from the authoritative tree. removeSubtreesFromLookup() must run while the old
  // nodes are still alive; addSubtreesToLookup() after the new nodes are attached to the tree.
  void removeSubtreesFromLookup(const std::vector<MarkdownNode*>& subtrees);
  void addSubtreesToLookup(const std::vector<MarkdownNode*>& subtrees);

  MarkdownNode* find(NodeId id) const;
  bool contains(NodeId id) const;

  // Document-order block access. The document root is indexed for lookup, but
  // is intentionally excluded from these ordered block helpers. They lazily rebuild the
  // order vector if a replaceTopLevelRange deferred it.
  MarkdownNode* firstBlock();
  MarkdownNode* lastBlock();
  qsizetype size() const;

private:
  void addSubtreeInDocumentOrder(MarkdownNode& node);
  void collectSubtreeIds(MarkdownNode& node, QHash<NodeId, bool>& removedIds) const;
  void ensureFreshOrder();

  QHash<NodeId, MarkdownNode*> nodes_;
  QVector<MarkdownNode*> blocksInDocumentOrder_;
  MarkdownNode* rootPtr_ = nullptr;
  bool orderDirty_ = false;
};

}  // namespace muffin
