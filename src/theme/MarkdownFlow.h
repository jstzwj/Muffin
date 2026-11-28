#pragma once

#include "document/MarkdownNode.h"
#include "document/CooperativeTask.h"
#include "theme/CssComputedStyleEngine.h"

#include <QHash>
#include <vector>

namespace muffin {

// A borrowed rendering topology, never a second editable AST. Matched HTML
// boundary blocks become containers; every text node and source range remains
// owned by the Markdown document. Unmatched/malformed markup stays literal.
//
// The topology names blocks by id and keeps their current addresses in one
// table. Only owners whose children hold a matched pair are recorded. An edit
// that replaces a block in place moves the entries of that block and of the
// blocks below it (rebind); any other change derives the topology again.
class MarkdownFlow {
public:
  struct Container {
    CssElement attributes;
    // Kept as an address for the renderers: a closer is only ever replaced in
    // place, and rebind moves it with its container.
    const MarkdownNode* closing = nullptr;
    QVector<NodeId> children;
  };

  explicit MarkdownFlow(const MarkdownNode& root);
  MarkdownFlow() = default;
  CooperativeTask rebuildSteps(const MarkdownNode& root);
  bool empty() const { return containers_.isEmpty(); }
  const Container* container(const MarkdownNode& node) const;
  const MarkdownNode* parent(const MarkdownNode& node) const;
  const MarkdownNode* previous(const MarkdownNode& node) const;
  const MarkdownNode* next(const MarkdownNode& node) const;
  int childIndex(const MarkdownNode& node) const;
  int typeIndex(const MarkdownNode& node) const;
  std::vector<const MarkdownNode*> children(const MarkdownNode& node) const;
  bool closingBoundary(const MarkdownNode& node) const { return closing_.contains(node.id()); }
  // Moves the entries of each named block, and of the blocks below it, onto the
  // replacement node. Every replacement has to keep the id of the block it
  // replaces and its CSS-visible shape, which is what an equal semantic
  // signature of the edit guarantees. False when an id changed: the caller then
  // derives the whole topology again.
  bool rebind(const QHash<NodeId, const MarkdownNode*>& replacements);

private:
  CooperativeTask scanSteps(const MarkdownNode& owner, NodeId top);
  void scan(const MarkdownNode& block);
  const MarkdownNode* address(NodeId id) const;
  struct Relationship {
    NodeId parent, previous, next; // invalid at the ends of a list
    int index = 0, typeIndex = 0;
  };
  QHash<NodeId, Container> containers_;
  QHash<NodeId, Relationship> relationships_;
  QHash<NodeId, QVector<NodeId>> children_;  // owner id -> the owner's blocks, an opener standing for its group
  QHash<NodeId, NodeId> closing_;            // closer id -> opener id
  QHash<NodeId, const MarkdownNode*> nodes_; // every named block -> where it is now
  QHash<NodeId, QVector<NodeId>> subtree_;   // top-level block id -> the named blocks below it
};

} // namespace muffin
