#pragma once

#include "document/NodeId.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/DocumentStyleTree.h"
#include "theme/MarkdownFlow.h"

#include <QHash>

#include <memory>
#include <vector>

namespace muffin {

class MarkdownNode;

// Replaced semantic children supplied by Markdown rather than source text.
// Their source anchor stays on the owning marker; they never become editable text.
struct CssInlineImage {
  const CssElement* element = nullptr;
  const CssElement* path = nullptr;
  qsizetype sourceOffset = 0;
  QByteArray svg;
};

// Sparse live-tree adapter for the selector engine. MarkdownNode already owns the
// parent/previous/next topology, so this builder creates CssElement views only for
// nodes a selector actually visits. It never materializes an entire sibling group
// and computes positional/:has predicates on demand.
//
// The builder must not outlive the MarkdownNode tree it views. RenderTheme resets
// it whenever structural styles are invalidated. Text-only edits rebind views
// to replacement AST nodes, preserving CSS identities and interaction recipes.
class NodeCssElementBuilder final : public CssElementNavigator {
public:
  explicit NodeCssElementBuilder(bool maintainTypeIndex = true);

  const CssElement* build(const MarkdownNode& node);
  const CssElement* build(const MarkdownNode& node, const QString& key);
  const CssElement* buildInline(const MarkdownNode& owner, qsizetype sourceOffset) const;
  const CssElement* buildPseudo(const CssElement& origin, const QString& pseudo) const;
  QVector<CssInlineImage> inlineImages(const MarkdownNode& owner) const;
  QVector<const CssElement*> semanticChildren(const CssElement& element, bool includeInline = true) const;
  bool mayHaveInlineSubjects(const CssElement& element, const QVector<CssCounterSubject>& subjects) const;
  bool mayHaveCounterSubjects(const MarkdownNode& node, const QVector<CssCounterSubject>& subjects) const;
  NodeId sourceNodeId(const CssElement& element) const;
  // Stable across adapter generations, local to a Markdown block. Never uses
  // source offsets (which shift after edits) or retained AST addresses.
  QString semanticKey(const CssElement& element) const;
  const CssElement* linkForSourceRange(const MarkdownNode& owner, qsizetype start, qsizetype end,
                                       const QString& href) const;
  qsizetype materializedElementCount() const;
  // Drop inline/pseudo views of evicted layout blocks. Sparse block navigation
  // and its ancestors remain stable; returned identities can lose style memos.
  QVector<quint64> releaseInlineViews(const QSet<NodeId>& topLevelIds);
  // Exact semantic signatures must prove identical topology/attributes first.
  // Old AST addresses are keys only: replaced nodes are never dereferenced.
  bool rebindBlocks(const QHash<NodeId, const MarkdownNode*>& replacements);
  bool hasInlineViewportUnits() const { return inlineViewportUnits_; }
  std::shared_ptr<const MarkdownFlow> flowFor(const MarkdownNode& node) const;

  const CssElement* previousSibling(const CssElement& element) const override;
  const CssElement* nextSibling(const CssElement& element) const override;
  int childIndex(const CssElement& element) const override;
  int typeIndex(const CssElement& element) const override;
  bool hasTag(const CssElement& element, const QString& tag, bool directChild) const override;
  bool hasClass(const CssElement& element, const QString& className, bool directChild) const override;

private:
  bool hasTagUncached(const CssElement& element, const QString& tag, bool directChild) const;
  bool hasClassUncached(const CssElement& element, const QString& name, bool directChild) const;
  const CssElement* ensure(const MarkdownNode& node) const;
  CssElement* makeOwned() const;
  const MarkdownNode* nodeFor(const CssElement& element) const;

  mutable std::vector<std::unique_ptr<CssElement>> pool_;
  mutable QHash<const MarkdownNode*, CssElement*> cache_;
  struct NodeBinding {
    NodeId topLevel;
    QVector<qsizetype> path;
  };
  mutable QHash<const MarkdownNode*, NodeBinding> bindings_;
  mutable QHash<const CssElement*, const MarkdownNode*> nodes_;
  mutable QHash<const MarkdownNode*, QHash<QString, CssElement*>> synthetic_;
  mutable QHash<const CssElement*, QHash<QString, CssElement*>> pseudos_;
  struct InlineView {
    qsizetype start, end;
    const CssElement* element;
    qsizetype ordinal;
  };
  mutable QHash<const MarkdownNode*, QVector<InlineView>> inlineTrees_;
  mutable QHash<const MarkdownNode*, QVector<CssInlineImage>> inlineImages_;
  mutable QHash<const CssElement*, QVector<const CssElement*>> inlineChildren_;
  // The adapter views an immutable AST generation. Positive and negative :has
  // answers are stable even when more CSS views are materialized later.
  mutable QHash<const CssElement*, QHash<QString, bool>> descendantQueries_;
  mutable bool inlineViewportUnits_ = false;
  bool maintainTypeIndex_ = true;
  mutable std::shared_ptr<const MarkdownFlow> flow_;
  DocumentStyleHost host_;
};

// The CSS tag for a block node ("p", "h2", "blockquote", "ul"/"ol", "li",
// "pre", "table", "tr", "td", "hr", ...). Empty for the document root and
// unmapped types.
QString cssTagForNode(const MarkdownNode& node);
// Complete CSS-visible metadata/topology, excluding ordinary text and source
// offsets. Shared by style invalidation and counter program discovery.
QByteArray cssSemanticSignature(const MarkdownNode& root);

} // namespace muffin
