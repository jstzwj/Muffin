#pragma once

#include "document/NodeId.h"
#include "theme/CssComputedStyleEngine.h"

#include <QHash>
#include <QSet>

#include <memory>
#include <vector>

namespace muffin {

class MarkdownNode;

// Builds a CssElement view of a live MarkdownNode subtree for the selector engine.
// The load-time prototype tree (CssThemeMapper) has no siblings and no descendants,
// so sibling combinators (`+`/`~`) and structural pseudo-classes (`:first-child`,
// `:nth-child(n)`, `:has(...)`) cannot match against it. This adapter materializes
// a connected CssElement sub-graph — node + ancestors + siblings — from the real
// MarkdownNode tree, populating childIndex/typeIndex/previousSibling/nextSibling
// and the precomputed :has tag/class sets, so the SAME engine can match structural
// selectors at layout time.
//
// One builder per layout pass; cached by NodeId so a node is built once. All
// returned CssElement* (and the parent/sibling pointers they expose) stay valid
// for the builder's lifetime (instances live in an owned pool).
//
// The document root maps to the `#write` container, with synthetic `body`/`html`
// ancestors above it — mirroring the prototype tree so `#write p`, `body p`, etc.
// match exactly as they do at theme-load time.
class NodeCssElementBuilder {
public:
  NodeCssElementBuilder();

  // Build (or return the cached) CssElement for `node`. Never null.
  const CssElement* build(const MarkdownNode& node);

private:
  const CssElement* ensure(const MarkdownNode& node);
  void linkSiblingsIteratively(const MarkdownNode& parent);
  CssElement* makeOwned();
  void populateHas(CssElement& element, const MarkdownNode& node);
  void collectDescendants(CssElement& element, const MarkdownNode& node);

  std::vector<std::unique_ptr<CssElement>> pool_;
  QHash<NodeId, CssElement*> cache_;
  QSet<NodeId> linkedParents_;  // parents whose child sibling chain has been wired (once each)
  CssElement* html_ = nullptr;
  CssElement* body_ = nullptr;
};

// The CSS tag for a block node ("p", "h2", "blockquote", "ul"/"ol", "li", "pre",
// "table", "tr", "td", "hr", …). "" for the document root and unmapped types.
QString cssTagForNode(const MarkdownNode& node);

}  // namespace muffin
