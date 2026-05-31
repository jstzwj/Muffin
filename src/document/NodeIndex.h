#pragma once

#include "document/MarkdownNode.h"

#include <QHash>

namespace muffin {

class NodeIndex {
public:
  void rebuild(MarkdownNode& root);
  void addSubtree(MarkdownNode& node);
  void removeSubtree(MarkdownNode& node);

  MarkdownNode* find(NodeId id) const;
  bool contains(NodeId id) const;

  MarkdownNode* firstBlock() const;
  MarkdownNode* lastBlock() const;
  qsizetype size() const;

private:
  QHash<NodeId, MarkdownNode*> nodes_;
};

}  // namespace muffin
