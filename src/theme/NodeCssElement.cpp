#include "theme/NodeCssElement.h"

#include "document/MarkdownNode.h"
#include "document/MarkdownTypes.h"

#include <QString>

#include <vector>

namespace muffin {
namespace {

QString cssTagForInline(InlineType type) {
  switch (type) {
    case InlineType::Image: return QStringLiteral("img");
    case InlineType::Code: return QStringLiteral("code");
    case InlineType::Link: return QStringLiteral("a");
    case InlineType::Strong: return QStringLiteral("strong");
    case InlineType::Emphasis: return QStringLiteral("em");
    case InlineType::Strikethrough: return QStringLiteral("del");
    case InlineType::Highlight: return QStringLiteral("mark");
    case InlineType::Subscript: return QStringLiteral("sub");
    case InlineType::Superscript: return QStringLiteral("sup");
    case InlineType::InlineMath: return QStringLiteral("span");
    default: return QString();
  }
}

bool sameCssTag(const MarkdownNode& left, const MarkdownNode& right) {
  if (left.type() == right.type()) {
    if (left.type() == BlockType::Heading) { return left.headingLevel() == right.headingLevel(); }
    if (left.type() == BlockType::List) { return left.listKind() == right.listKind(); }
    return true;
  }
  return (left.type() == BlockType::CodeFence || left.type() == BlockType::FrontMatter) &&
         (right.type() == BlockType::CodeFence || right.type() == BlockType::FrontMatter);
}

bool inlineTreeHasTag(const QVector<InlineNode>& roots, const QString& tag, bool descendants) {
  std::vector<const InlineNode*> pending;
  pending.reserve(static_cast<std::size_t>(roots.size()));
  for (const InlineNode& node : roots) { pending.push_back(&node); }
  while (!pending.empty()) {
    const InlineNode* node = pending.back();
    pending.pop_back();
    if (cssTagForInline(node->type()) == tag) { return true; }
    if (descendants) {
      for (const InlineNode& child : node->children()) { pending.push_back(&child); }
    }
  }
  return false;
}

}  // namespace

QString cssTagForNode(const MarkdownNode& node) {
  switch (node.type()) {
    case BlockType::Paragraph: return QStringLiteral("p");
    case BlockType::Heading: return QStringLiteral("h%1").arg(node.headingLevel());
    case BlockType::BlockQuote: return QStringLiteral("blockquote");
    case BlockType::List: return node.listKind() == ListKind::Ordered ? QStringLiteral("ol") : QStringLiteral("ul");
    case BlockType::ListItem: return QStringLiteral("li");
    case BlockType::CodeFence:
    case BlockType::FrontMatter: return QStringLiteral("pre");
    case BlockType::Table: return QStringLiteral("table");
    case BlockType::TableRow: return QStringLiteral("tr");
    case BlockType::TableCell: return QStringLiteral("td");
    case BlockType::ThematicBreak: return QStringLiteral("hr");
    default: return QString();
  }
}

NodeCssElementBuilder::NodeCssElementBuilder(bool maintainTypeIndex) : maintainTypeIndex_(maintainTypeIndex) {
  html_ = makeOwned();
  html_->tag = QStringLiteral("html");
  body_ = makeOwned();
  body_->tag = QStringLiteral("body");
  body_->parent = html_;
}

CssElement* NodeCssElementBuilder::makeOwned() const {
  pool_.push_back(std::make_unique<CssElement>());
  return pool_.back().get();
}

const CssElement* NodeCssElementBuilder::ensure(const MarkdownNode& node) const {
  const auto found = cache_.constFind(node.id());
  if (found != cache_.constEnd()) { return found.value(); }

  CssElement* element = makeOwned();
  element->tag = cssTagForNode(node);
  element->navigator = this;
  if (node.type() == BlockType::CodeFence) { element->classes << QStringLiteral("md-fences"); }
  if (node.type() == BlockType::Document) {
    element->id = QStringLiteral("write");
    element->tag.clear();
  }
  cache_.insert(node.id(), element);
  nodes_.insert(element, &node);
  element->parent = node.parent() ? ensure(*node.parent()) : body_;
  return element;
}

const CssElement* NodeCssElementBuilder::build(const MarkdownNode& node) {
  return ensure(node);
}

qsizetype NodeCssElementBuilder::materializedElementCount() const {
  return static_cast<qsizetype>(pool_.size());
}

const MarkdownNode* NodeCssElementBuilder::nodeFor(const CssElement& element) const {
  return nodes_.value(&element, nullptr);
}

const CssElement* NodeCssElementBuilder::previousSibling(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  return node && node->previousSibling() ? ensure(*node->previousSibling()) : nullptr;
}

const CssElement* NodeCssElementBuilder::nextSibling(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  return node && node->nextSibling() ? ensure(*node->nextSibling()) : nullptr;
}

int NodeCssElementBuilder::childIndex(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  if (!node || !node->parent()) { return -1; }
  int index = 0;
  for (const MarkdownNode* previous = node->previousSibling(); previous; previous = previous->previousSibling()) { ++index; }
  return index;
}

int NodeCssElementBuilder::typeIndex(const CssElement& element) const {
  if (!maintainTypeIndex_) { return -1; }
  const MarkdownNode* node = nodeFor(element);
  if (!node || !node->parent()) { return -1; }
  int index = 0;
  for (const MarkdownNode* previous = node->previousSibling(); previous; previous = previous->previousSibling()) {
    if (sameCssTag(*node, *previous)) { ++index; }
  }
  return index;
}

bool NodeCssElementBuilder::hasTag(const CssElement& element, const QString& tag, bool directChild) const {
  const MarkdownNode* node = nodeFor(element);
  if (!node) { return false; }

  for (const std::unique_ptr<MarkdownNode>& child : node->children()) {
    if (cssTagForNode(*child) == tag) { return true; }
  }
  if (inlineTreeHasTag(node->inlines(), tag, !directChild)) { return true; }
  if (directChild) { return false; }

  std::vector<const MarkdownNode*> pending;
  for (const std::unique_ptr<MarkdownNode>& child : node->children()) { pending.push_back(child.get()); }
  while (!pending.empty()) {
    const MarkdownNode* descendant = pending.back();
    pending.pop_back();
    if (inlineTreeHasTag(descendant->inlines(), tag, true)) { return true; }
    for (const std::unique_ptr<MarkdownNode>& child : descendant->children()) {
      if (cssTagForNode(*child) == tag) { return true; }
      pending.push_back(child.get());
    }
  }
  return false;
}

bool NodeCssElementBuilder::hasClass(const CssElement&, const QString&, bool) const {
  // MarkdownNode currently exposes no CSS classes. Keep class probes explicit so
  // :has(.class) never broadens into a false-positive tag-only match.
  return false;
}

}  // namespace muffin
