#pragma once

#include "document/NodeId.h"
#include "theme/CssComputedStyleEngine.h"

#include <QHash>

#include <memory>
#include <vector>

namespace muffin {

class MarkdownNode;

// Sparse live-tree adapter for the selector engine. MarkdownNode already owns the
// parent/previous/next topology, so this builder creates CssElement views only for
// nodes a selector actually visits. It never materializes an entire sibling group
// and computes positional/:has predicates on demand.
//
// The builder must not outlive the MarkdownNode tree it views. RenderTheme resets
// it whenever structural styles are invalidated, which also prevents deleted or
// edited nodes from accumulating as stale cache entries.
class NodeCssElementBuilder final : public CssElementNavigator {
public:
  explicit NodeCssElementBuilder(bool maintainTypeIndex = true);

  const CssElement* build(const MarkdownNode& node);
  qsizetype materializedElementCount() const;

  const CssElement* previousSibling(const CssElement& element) const override;
  const CssElement* nextSibling(const CssElement& element) const override;
  int childIndex(const CssElement& element) const override;
  int typeIndex(const CssElement& element) const override;
  bool hasTag(const CssElement& element, const QString& tag, bool directChild) const override;
  bool hasClass(const CssElement& element, const QString& className, bool directChild) const override;

private:
  const CssElement* ensure(const MarkdownNode& node) const;
  CssElement* makeOwned() const;
  const MarkdownNode* nodeFor(const CssElement& element) const;

  mutable std::vector<std::unique_ptr<CssElement>> pool_;
  mutable QHash<NodeId, CssElement*> cache_;
  mutable QHash<const CssElement*, const MarkdownNode*> nodes_;
  bool maintainTypeIndex_ = true;
  CssElement* html_ = nullptr;
  CssElement* body_ = nullptr;
};

// The CSS tag for a block node ("p", "h2", "blockquote", "ul"/"ol", "li",
// "pre", "table", "tr", "td", "hr", ...). Empty for the document root and
// unmapped types.
QString cssTagForNode(const MarkdownNode& node);

}  // namespace muffin
