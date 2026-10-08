#include "theme/NodeCssElement.h"

#include "document/MarkdownNode.h"
#include "document/MarkdownTypes.h"
#include "html/HtmlParser.h"
#include "html/HtmlBoxBuilder.h"
#include "html/HtmlBox.h"

#include <QString>

#include <vector>
#include <atomic>
#include <functional>
#include <algorithm>

namespace muffin {
namespace {
quint64 nextElementIdentity() {
  static std::atomic<quint64> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

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
    case BlockType::TableCell:
      return node.parent() && node.parent()->tableRowIsHeader() ? QStringLiteral("th") : QStringLiteral("td");
    case BlockType::ThematicBreak: return QStringLiteral("hr");
    default: return QString();
  }
}

NodeCssElementBuilder::NodeCssElementBuilder(bool maintainTypeIndex) : maintainTypeIndex_(maintainTypeIndex) {
  host_.html.cacheId = nextElementIdentity();
  host_.body.cacheId = nextElementIdentity();
  host_.write.cacheId = nextElementIdentity();
}

CssElement* NodeCssElementBuilder::makeOwned() const {
  pool_.push_back(std::make_unique<CssElement>());
  pool_.back()->cacheId = nextElementIdentity();
  return pool_.back().get();
}

const CssElement* NodeCssElementBuilder::ensure(const MarkdownNode& node) const {
  const auto found = cache_.constFind(&node);
  if (found != cache_.constEnd()) { return found.value(); }

  CssElement* element = makeOwned();
  element->tag = cssTagForNode(node);
  element->navigator = this;
  if (node.type() == BlockType::CodeFence) { element->classes << QStringLiteral("md-fences"); }
  if (node.type() == BlockType::Document) {
    element->id = QStringLiteral("write");
    element->tag = QStringLiteral("div");
  }
  cache_.insert(&node, element);
  nodes_.insert(element, &node);
  element->parent = node.parent() ? ensure(*node.parent()) : (node.type() == BlockType::Document ? &host_.body : &host_.write);
  if (node.type() == BlockType::TableRow && node.parent()) {
    const QString section = node.tableRowIsHeader() ? QStringLiteral("thead") : QStringLiteral("tbody");
    auto& sections = synthetic_[node.parent()];
    if (!sections.contains(section)) {
      auto* wrapper = makeOwned();
      wrapper->tag = section;
      wrapper->parent = ensure(*node.parent());
      sections.insert(section, wrapper);
    }
    element->parent = sections.value(section);
  }
  return element;
}

const CssElement* NodeCssElementBuilder::build(const MarkdownNode& node) {
  return ensure(node);
}

const CssElement* NodeCssElementBuilder::build(const MarkdownNode& node, const QString& key) {
  const auto* parent = ensure(node);
  const QString tag = key.section(QLatin1Char(' '), -1).section(QLatin1Char(':'), 0, 0);
  if (tag == parent->tag || tag.startsWith(QLatin1Char('#'))) return parent;
  auto& children = synthetic_[&node];
  if (!children.contains(tag)) {
    auto* child = makeOwned();
    child->tag = tag;
    child->parent = parent;
    children.insert(tag, child);
  }
  return children.value(tag);
}

const CssElement* NodeCssElementBuilder::buildInline(const MarkdownNode& owner, qsizetype offset) const {
  if (!inlineTrees_.contains(&owner)) {
    QVector<InlineView> views;
    QHash<const CssElement*, CssElement*> previousChildren;
    QHash<const CssElement*, int> childIndices;
    QHash<const CssElement*, QHash<QString, int>> typeIndices;
    const auto childElement = [&](const CssElement* parent, const QString& tag) {
      auto* child = makeOwned();
      child->tag = tag;
      child->parent = parent;
      child->childIndex = childIndices[parent]++;
      child->typeIndex = typeIndices[parent][tag]++;
      child->previousSibling = previousChildren.value(parent);
      if (child->previousSibling) const_cast<CssElement*>(child->previousSibling)->nextSibling = child;
      previousChildren[parent] = child;
      return child;
    };
    std::function<void(const QVector<InlineNode>&, const CssElement*)> append;
    append = [&](const QVector<InlineNode>& nodes, const CssElement* parent) {
      QVector<const CssElement*> htmlParents{parent};
      for (const auto& node : nodes) {
        const CssElement* element = htmlParents.back();
        if (node.type() == InlineType::HtmlInline) {
          const auto markup = node.text().trimmed();
          if (markup.startsWith(QStringLiteral("</"))) {
            const auto tag = markup.mid(2).section(QLatin1Char('>'), 0, 0).trimmed().toLower();
            for (qsizetype i = htmlParents.size() - 1; i > 0; --i) {
              if (htmlParents[i]->tag == tag) {
                htmlParents.resize(i);
                break;
              }
            }
          } else if (markup.startsWith(QLatin1Char('<')) && !markup.startsWith(QStringLiteral("<!"))) {
            // Reuse the HTML parser's attribute decoding, including quoted '>'
            // and character references. CSS declarations enter the common engine.
            html::HtmlDocument fragment;
            if (fragment.parse(markup)) {
              auto root = html::HtmlBoxBuilder().build(fragment);
              if (root && !root->children().empty()) {
                const auto& box = *root->children().front();
                auto* child = childElement(htmlParents.back(), box.cssTag);
                child->id = box.cssId;
                child->classes = box.cssClasses;
                child->inlineDeclarations = CssThemeParser::parseDeclarations(box.cssInlineStyle);
                element = child;
                const auto tag = child->tag;
                if (tag != "br" && tag != "img" && tag != "input" && tag != "hr" && !markup.endsWith(QStringLiteral("/>")))
                  htmlParents.push_back(child);
              }
            }
          }
        } else {
          const QString tag = cssTagForInline(node.type());
          if (!tag.isEmpty()) element = childElement(htmlParents.back(), tag);
        }
        views.push_back({node.sourceStart(), node.sourceEnd(), element});
        append(node.children(), element);
      }
    };
    const auto* ownerElement = ensure(owner);
    append(owner.inlines(), ownerElement);
    for (const auto& view : views) {
      if (view.element == ownerElement) continue;
      for (const auto* parent = view.element->parent; parent; parent = parent == ownerElement ? nullptr : parent->parent) {
        auto* writable = const_cast<CssElement*>(parent);
        writable->hasDescendantTags.insert(view.element->tag);
        for (const auto& name : view.element->classes) writable->hasDescendantClasses.insert(name);
        if (parent == view.element->parent) {
          writable->hasChildTags.insert(view.element->tag);
          for (const auto& name : view.element->classes) writable->hasChildClasses.insert(name);
        }
      }
    }
    std::stable_sort(views.begin(), views.end(), [](const InlineView& a, const InlineView& b) { return a.start < b.start; });
    inlineTrees_.insert(&owner, views);
  }
  const CssElement* result = ensure(owner);
  const auto& views = inlineTrees_.constFind(&owner).value();
  // A binary search reaches the leaf in ordinary text. Walking back is needed
  // only inside an enclosing element's closing Markdown delimiters.
  auto view =
      std::upper_bound(views.begin(), views.end(), offset, [](qsizetype value, const InlineView& item) { return value < item.start; });
  while (view != views.begin()) {
    --view;
    if (offset < view->end) {
      result = view->element;
      break;
    }
  }
  return result;
}

qsizetype NodeCssElementBuilder::materializedElementCount() const {
  return static_cast<qsizetype>(pool_.size()) + 2;  // shared html/body host
}

const MarkdownNode* NodeCssElementBuilder::nodeFor(const CssElement& element) const {
  return nodes_.value(&element, nullptr);
}

const CssElement* NodeCssElementBuilder::previousSibling(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  if (node && node->type() == BlockType::TableRow && node->previousSibling() &&
      node->previousSibling()->tableRowIsHeader() != node->tableRowIsHeader())
    return nullptr;
  return node && node->previousSibling() ? ensure(*node->previousSibling()) : nullptr;
}

const CssElement* NodeCssElementBuilder::nextSibling(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  if (node && node->type() == BlockType::TableRow && node->nextSibling() &&
      node->nextSibling()->tableRowIsHeader() != node->tableRowIsHeader())
    return nullptr;
  return node && node->nextSibling() ? ensure(*node->nextSibling()) : nullptr;
}

int NodeCssElementBuilder::childIndex(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  if (node && node->type() == BlockType::TableRow && !node->tableRowIsHeader()) return qMax(0, node->siblingIndex() - 1);
  return node ? node->siblingIndex() : -1;
}

int NodeCssElementBuilder::typeIndex(const CssElement& element) const {
  if (!maintainTypeIndex_) { return -1; }
  const MarkdownNode* node = nodeFor(element);
  if (node && node->type() == BlockType::TableRow) return childIndex(element);
  return node ? node->siblingTypeIndex() : -1;
}

bool NodeCssElementBuilder::hasTag(const CssElement& element, const QString& tag, bool directChild) const {
  const MarkdownNode* node = nodeFor(element);
  if (!node) { return false; }

  for (const std::unique_ptr<MarkdownNode>& child : node->children()) {
    if (cssTagForNode(*child) == tag) { return true; }
  }
  if (inlineTreeHasTag(node->inlines(), tag, !directChild)) { return true; }
  if (!node->inlines().isEmpty()) {
    buildInline(*node, 0);
    if ((directChild ? element.hasChildTags : element.hasDescendantTags).contains(tag)) return true;
  }
  if (directChild) { return false; }

  std::vector<const MarkdownNode*> pending;
  for (const std::unique_ptr<MarkdownNode>& child : node->children()) { pending.push_back(child.get()); }
  while (!pending.empty()) {
    const MarkdownNode* descendant = pending.back();
    pending.pop_back();
    if (inlineTreeHasTag(descendant->inlines(), tag, true)) { return true; }
    if (!descendant->inlines().isEmpty()) {
      buildInline(*descendant, 0);
      if (ensure(*descendant)->hasDescendantTags.contains(tag)) return true;
    }
    for (const std::unique_ptr<MarkdownNode>& child : descendant->children()) {
      if (cssTagForNode(*child) == tag) { return true; }
      pending.push_back(child.get());
    }
  }
  return false;
}

bool NodeCssElementBuilder::hasClass(const CssElement& element, const QString& className, bool directChild) const {
  const MarkdownNode* node = nodeFor(element);
  if (!node) return false;
  if (!node->inlines().isEmpty()) buildInline(*node, 0);
  if ((directChild ? element.hasChildClasses : element.hasDescendantClasses).contains(className)) return true;
  if (directChild) return false;
  for (const auto& child : node->children())
    if (hasClass(*ensure(*child), className, false)) return true;
  return false;
}

}  // namespace muffin
