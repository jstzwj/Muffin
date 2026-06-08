#pragma once

#include "document/MarkdownNode.h"

#include <QHash>
#include <QVector>

namespace muffin {

class NodeIndex {
public:
  void rebuild(MarkdownNode& root);
  void addSubtree(MarkdownNode& node);
  void removeSubtree(MarkdownNode& node);

  MarkdownNode* find(NodeId id) const;
  bool contains(NodeId id) const;

  // Document-order block access. The document root is indexed for lookup, but
  // is intentionally excluded from these ordered block helpers.
  MarkdownNode* firstBlock() const;
  MarkdownNode* lastBlock() const;
  qsizetype size() const;

private:
  void addSubtreeInDocumentOrder(MarkdownNode& node);
  void collectSubtreeIds(MarkdownNode& node, QHash<NodeId, bool>& removedIds) const;

  QHash<NodeId, MarkdownNode*> nodes_;
  QVector<MarkdownNode*> blocksInDocumentOrder_;
};

}  // namespace muffin
