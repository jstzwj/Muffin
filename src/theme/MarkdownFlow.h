#pragma once

#include "document/MarkdownNode.h"
#include "theme/CssComputedStyleEngine.h"

#include <QHash>
#include <vector>

namespace muffin {

// A borrowed rendering topology, never a second editable AST. Matched HTML
// boundary blocks become containers; every text node and source range remains
// owned by the Markdown document. Unmatched/malformed markup stays literal.
class MarkdownFlow {
public:
  struct Container {
    CssElement attributes;
    const MarkdownNode* closing = nullptr;
    std::vector<const MarkdownNode*> children;
  };
  explicit MarkdownFlow(const MarkdownNode& root);
  bool empty() const { return containers_.isEmpty(); }
  const Container* container(const MarkdownNode& node) const;
  const MarkdownNode* parent(const MarkdownNode& node) const;
  const MarkdownNode* previous(const MarkdownNode& node) const;
  const MarkdownNode* next(const MarkdownNode& node) const;
  int childIndex(const MarkdownNode& node) const;
  int typeIndex(const MarkdownNode& node) const;
  std::vector<const MarkdownNode*> children(const MarkdownNode& node) const;
  bool closingBoundary(const MarkdownNode& node) const { return closing_.contains(node.id()); }

private:
  void scan(const MarkdownNode& parent);
  QHash<NodeId, Container> containers_;
  struct Relationship {
    const MarkdownNode *parent = nullptr, *previous = nullptr, *next = nullptr;
    int index = 0, typeIndex = 0;
  };
  QHash<NodeId, Relationship> relationships_;
  QHash<NodeId, std::vector<const MarkdownNode*>> children_;
  QHash<NodeId, const MarkdownNode*> closing_;
};

} // namespace muffin
