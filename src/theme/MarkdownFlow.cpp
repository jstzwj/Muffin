#include "theme/MarkdownFlow.h"
#include "document/HtmlContainerStructure.h"
#include "theme/NodeCssElement.h"
#include "theme/DocumentStyleTree.h"

#include "html/HtmlBoxBuilder.h"
#include "html/HtmlParser.h"

#include <functional>

namespace muffin {
namespace {
struct Boundary {
  QString tag;
  bool closing = false;
  CssElement attributes;
};

std::optional<Boundary> boundary(const MarkdownNode& node) {
  const auto token = htmlContainerBoundary(node);
  if (!token)
    return std::nullopt;
  Boundary result;
  result.tag = token->tag;
  result.closing = token->closing;
  if (result.closing)
    return result;
  const QString source = node.literal().trimmed();
  html::HtmlDocument document;
  if (!document.parse(source))
    return std::nullopt;
  auto root = html::HtmlBoxBuilder().build(document);
  if (!root || root->children().size() != 1 || root->children().front()->cssTag != token->tag)
    return std::nullopt;
  const auto& box = *root->children().front();
  result.attributes.tag = token->tag;
  result.attributes.id = box.cssId;
  result.attributes.classes = box.cssClasses;
  result.attributes.attributes = box.cssAttributes;
  addHtmlPresentationHints(result.attributes);
  result.attributes.inlineDeclarations = CssThemeParser::parseDeclarations(box.cssInlineStyle);
  return result;
}
} // namespace

MarkdownFlow::MarkdownFlow(const MarkdownNode& root) {
  auto task = rebuildSteps(root);
  task.resumeFor(-1);
}

CooperativeTask MarkdownFlow::rebuildSteps(const MarkdownNode& root) {
  containers_.clear();
  relationships_.clear();
  children_.clear();
  closing_.clear();
  nodes_.clear();
  subtree_.clear();
  auto task = scanSteps(root, NodeId{});
  while (task.resumeFor(0))
    co_yield nullptr;
}

const MarkdownNode* MarkdownFlow::address(NodeId id) const {
  const auto it = nodes_.constFind(id);
  return it == nodes_.cend() ? nullptr : it.value();
}

CooperativeTask MarkdownFlow::scanSteps(const MarkdownNode& owner, NodeId top) {
  const auto& source = owner.children();
  if (source.empty())
    co_return;
  QHash<size_t, QPair<size_t, CssElement>> pairs;
  QHash<size_t, size_t> structure;
  auto structureTask = htmlContainerPairSteps(owner, structure);
  while (structureTask.resumeFor(0))
    co_yield nullptr;
  for (size_t i = 0; i < source.size(); ++i) {
    if (const auto close = structure.constFind(i); close != structure.cend())
      if (const auto token = boundary(*source[i]))
        pairs.insert(i, {*close, token->attributes});
    // Everything below a top-level block belongs to that block; at the document root every child
    // is a block of its own.
    auto task = scanSteps(*source[i], top.isValid() ? top : source[i]->id());
    while (task.resumeFor(0))
      co_yield nullptr;
    co_yield nullptr;
  }
  if (pairs.isEmpty())
    co_return;
  // An owner whose children hold a matched pair is part of the topology from here on.
  nodes_.insert(owner.id(), &owner);
  if (top.isValid() && top != owner.id())
    subtree_[top].append(owner.id());
  std::function<CooperativeTask(size_t, size_t, NodeId, QVector<NodeId>&)> group;
  group = [&](size_t first, size_t last, NodeId parent, QVector<NodeId>& result) -> CooperativeTask {
    for (size_t i = first; i < last; ++i) {
      const auto* node = source[i].get();
      result.append(node->id());
      nodes_.insert(node->id(), node);
      if (top.isValid())
        subtree_[top].append(node->id());
      relationships_[node->id()].parent = parent;
      if (const auto pair = pairs.constFind(i); pair != pairs.cend() && pair->first < last) {
        Container value;
        value.attributes = pair->second;
        value.closing = source[pair->first].get();
        nodes_.insert(value.closing->id(), value.closing);
        if (top.isValid())
          subtree_[top].append(value.closing->id());
        closing_.insert(value.closing->id(), node->id());
        relationships_[value.closing->id()].parent = node->id();
        auto task = group(i + 1, pair->first, node->id(), value.children);
        while (task.resumeFor(0))
          co_yield nullptr;
        containers_.insert(node->id(), std::move(value));
        i = pair->first;
      }
      co_yield nullptr;
    }
    QHash<QString, int> types;
    for (size_t i = 0; i < result.size(); ++i) {
      auto& relation = relationships_[result[i]];
      relation.previous = i ? result[i - 1] : NodeId{};
      relation.next = i + 1 < result.size() ? result[i + 1] : NodeId{};
      relation.index = static_cast<int>(i);
      const auto value = containers_.constFind(result[i]);
      const auto* node = address(result[i]);
      const auto tag =
          value != containers_.cend()
              ? value->attributes.tag
              : (node && node->type() == BlockType::Table ? QStringLiteral("figure") : cssTagForNode(*node));
      relation.typeIndex = types[tag]++;
      co_yield nullptr;
    }
  };
  QVector<NodeId> result;
  auto task = group(0, source.size(), owner.id(), result);
  while (task.resumeFor(0))
    co_yield nullptr;
  children_.insert(owner.id(), std::move(result));
}

void MarkdownFlow::scan(const MarkdownNode& block) {
  auto task = scanSteps(block, block.id());
  task.resumeFor(-1);
}

const MarkdownFlow::Container* MarkdownFlow::container(const MarkdownNode& node) const {
  const auto it = containers_.constFind(node.id());
  return it == containers_.cend() ? nullptr : &*it;
}
const MarkdownNode* MarkdownFlow::parent(const MarkdownNode& node) const {
  const auto it = relationships_.constFind(node.id());
  return it == relationships_.cend() ? node.parent() : address(it->parent);
}
const MarkdownNode* MarkdownFlow::previous(const MarkdownNode& node) const {
  const auto it = relationships_.constFind(node.id());
  return it == relationships_.cend() ? node.previousSibling() : address(it->previous);
}
const MarkdownNode* MarkdownFlow::next(const MarkdownNode& node) const {
  const auto it = relationships_.constFind(node.id());
  return it == relationships_.cend() ? node.nextSibling() : address(it->next);
}
int MarkdownFlow::childIndex(const MarkdownNode& node) const {
  const auto it = relationships_.constFind(node.id());
  return it == relationships_.cend() ? node.siblingIndex() : it->index;
}
int MarkdownFlow::typeIndex(const MarkdownNode& node) const {
  const auto it = relationships_.constFind(node.id());
  return it == relationships_.cend() ? -1 : it->typeIndex;
}
std::vector<const MarkdownNode*> MarkdownFlow::children(const MarkdownNode& node) const {
  const QVector<NodeId>* ids = nullptr;
  if (const auto value = containers_.constFind(node.id()); value != containers_.cend())
    ids = &value->children;
  else if (const auto found = children_.constFind(node.id()); found != children_.cend())
    ids = &found.value();
  std::vector<const MarkdownNode*> result;
  if (!ids) {
    result.reserve(node.children().size());
    for (const auto& child : node.children())
      result.push_back(child.get());
    return result;
  }
  result.reserve(ids->size());
  for (const NodeId& id : *ids) {
    if (const auto* live = address(id)) {
      result.push_back(live);
      continue;
    }
    // Not a state the topology should reach: the document's own order is the safer answer.
    result.clear();
    result.reserve(node.children().size());
    for (const auto& child : node.children())
      result.push_back(child.get());
    return result;
  }
  return result;
}

bool MarkdownFlow::rebind(const QHash<NodeId, const MarkdownNode*>& replacements) {
  // Checked before anything moves. A block that was named anew breaks every link to it, and a
  // container boundary that stopped being one moves blocks between the slots.
  for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
    const bool named = containers_.contains(it.key()) || closing_.contains(it.key());
    if (it.value()->id() != it.key() || (named && !htmlContainerBoundary(*it.value())))
      return false;
  }
  for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
    const NodeId id = it.key();
    const auto* replacement = it.value();
    const NodeId opened = closing_.contains(id) ? closing_.value(id) : NodeId{};
    // The blocks below the one replaced are derived again from the replacement.
    for (const NodeId& below : subtree_.value(id)) {
      nodes_.remove(below);
      relationships_.remove(below);
      children_.remove(below);
      containers_.remove(below);
      closing_.remove(below);
    }
    subtree_.remove(id);
    if (nodes_.contains(id))
      nodes_.insert(id, replacement);
    if (const auto container = containers_.find(id); container != containers_.end()) {
      // The same source: the same attributes.
      if (const auto token = boundary(*replacement))
        container->attributes = token->attributes;
    }
    if (const auto container = containers_.find(opened); opened.isValid() && container != containers_.end())
      container->closing = replacement;
    scan(*replacement);
  }
  return true;
}
} // namespace muffin
