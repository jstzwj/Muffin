#include "theme/MarkdownFlow.h"
#include "theme/NodeCssElement.h"
#include "theme/DocumentStyleTree.h"

#include "html/HtmlBoxBuilder.h"
#include "html/HtmlParser.h"

#include <QRegularExpression>
#include <QSet>
#include <functional>

namespace muffin {
namespace {
struct Boundary {
  QString tag;
  bool closing = false;
  CssElement attributes;
};

std::optional<Boundary> boundary(const MarkdownNode& node) {
  if (node.type() != BlockType::HtmlBlock)
    return std::nullopt;
  const QString source = node.literal().trimmed();
  // One complete boundary token. Quoted '>' is an attribute character, not
  // the end of the tag. The HTML parser below owns attribute/entity decoding.
  static const QRegularExpression token(R"(^<(\/?)([A-Za-z][A-Za-z0-9-]*)(?:\s+(?:[^<>"']|"[^"]*"|'[^']*')*)?\s*>$)");
  const auto match = token.match(source);
  if (!match.hasMatch() || source.endsWith("/>"))
    return std::nullopt;
  static const QSet<QString> containers{"div",    "section", "article", "aside",      "main",  "header",
                                        "footer", "nav",     "figure",  "blockquote", "center"};
  const QString tag = match.captured(2).toLower();
  if (!containers.contains(tag))
    return std::nullopt;
  Boundary result;
  result.tag = tag;
  result.closing = !match.captured(1).isEmpty();
  if (result.closing) {
    if (source.mid(2, source.size() - 3).trimmed().compare(tag, Qt::CaseInsensitive) != 0)
      return std::nullopt;
    return result;
  }
  html::HtmlDocument document;
  if (!document.parse(source))
    return std::nullopt;
  auto root = html::HtmlBoxBuilder().build(document);
  if (!root || root->children().size() != 1 || root->children().front()->cssTag != tag)
    return std::nullopt;
  const auto& box = *root->children().front();
  result.attributes.tag = tag;
  result.attributes.id = box.cssId;
  result.attributes.classes = box.cssClasses;
  result.attributes.attributes = box.cssAttributes;
  addHtmlPresentationHints(result.attributes);
  result.attributes.inlineDeclarations = CssThemeParser::parseDeclarations(box.cssInlineStyle);
  return result;
}
} // namespace

MarkdownFlow::MarkdownFlow(const MarkdownNode& root) {
  scan(root);
}

void MarkdownFlow::scan(const MarkdownNode& owner) {
  const auto& source = owner.children();
  if (source.empty())
    return;
  struct Open {
    size_t index;
    Boundary token;
  };
  std::vector<Open> stack;
  QHash<size_t, QPair<size_t, CssElement>> pairs;
  for (size_t i = 0; i < source.size(); ++i) {
    if (const auto token = boundary(*source[i])) {
      if (!token->closing && stack.size() < 64)
        stack.push_back({i, *token});
      else if (token->closing && !stack.empty() && stack.back().token.tag == token->tag) {
        pairs.insert(stack.back().index, {i, stack.back().token.attributes});
        stack.pop_back();
      } else {
        // Do not let malformed nesting silently consume unrelated Markdown.
        stack.clear();
        pairs.clear();
      }
    }
    scan(*source[i]);
  }
  if (pairs.isEmpty())
    return;
  std::function<std::vector<const MarkdownNode*>(size_t, size_t, const MarkdownNode*)> group;
  group = [&](size_t first, size_t last, const MarkdownNode* parent) {
    std::vector<const MarkdownNode*> result;
    for (size_t i = first; i < last; ++i) {
      const auto* node = source[i].get();
      result.push_back(node);
      relationships_[node->id()].parent = parent;
      if (const auto pair = pairs.constFind(i); pair != pairs.cend() && pair->first < last) {
        Container value;
        value.attributes = pair->second;
        value.closing = source[pair->first].get();
        value.children = group(i + 1, pair->first, node);
        closing_.insert(value.closing->id(), node);
        relationships_[value.closing->id()].parent = node;
        containers_.insert(node->id(), std::move(value));
        i = pair->first;
      }
    }
    QHash<QString, int> types;
    for (size_t i = 0; i < result.size(); ++i) {
      auto& relation = relationships_[result[i]->id()];
      relation.previous = i ? result[i - 1] : nullptr;
      relation.next = i + 1 < result.size() ? result[i + 1] : nullptr;
      relation.index = static_cast<int>(i);
      const auto* value = container(*result[i]);
      const auto tag = value                                   ? value->attributes.tag
                       : result[i]->type() == BlockType::Table ? QStringLiteral("figure")
                                                               : cssTagForNode(*result[i]);
      relation.typeIndex = types[tag]++;
    }
    return result;
  };
  children_.insert(owner.id(), group(0, source.size(), &owner));
}

const MarkdownFlow::Container* MarkdownFlow::container(const MarkdownNode& node) const {
  const auto it = containers_.constFind(node.id());
  return it == containers_.cend() ? nullptr : &*it;
}
const MarkdownNode* MarkdownFlow::parent(const MarkdownNode& node) const {
  const auto it = relationships_.constFind(node.id());
  return it == relationships_.cend() ? node.parent() : it->parent;
}
const MarkdownNode* MarkdownFlow::previous(const MarkdownNode& node) const {
  const auto it = relationships_.constFind(node.id());
  return it == relationships_.cend() ? node.previousSibling() : it->previous;
}
const MarkdownNode* MarkdownFlow::next(const MarkdownNode& node) const {
  const auto it = relationships_.constFind(node.id());
  return it == relationships_.cend() ? node.nextSibling() : it->next;
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
  if (const auto* value = container(node))
    return value->children;
  const auto found = children_.constFind(node.id());
  if (found != children_.cend())
    return *found;
  std::vector<const MarkdownNode*> result;
  result.reserve(node.children().size());
  for (const auto& child : node.children())
    result.push_back(child.get());
  return result;
}
} // namespace muffin
