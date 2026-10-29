#include "theme/NodeCssElement.h"

#include "document/MarkdownNode.h"
#include "document/MarkdownTypes.h"
#include "theme/MarkdownAlertIcon.h"
#include "html/HtmlParser.h"
#include "html/HtmlBoxBuilder.h"
#include "html/HtmlBox.h"

#include <QString>

#include <vector>
#include <functional>
#include <algorithm>

namespace muffin {
namespace {
QString cssTagForInline(InlineType type) {
  switch (type) {
  case InlineType::Image:
    return QStringLiteral("img");
  case InlineType::Code:
    return QStringLiteral("code");
  case InlineType::Link:
    return QStringLiteral("a");
  case InlineType::Strong:
    return QStringLiteral("strong");
  case InlineType::Emphasis:
    return QStringLiteral("em");
  case InlineType::Strikethrough:
    return QStringLiteral("del");
  case InlineType::Highlight:
    return QStringLiteral("mark");
  case InlineType::Subscript:
    return QStringLiteral("sub");
  case InlineType::Superscript:
    return QStringLiteral("sup");
  case InlineType::TaskMarker:
    return QStringLiteral("input");
  case InlineType::InlineMath:
    return QStringLiteral("span");
  default:
    return QString();
  }
}

bool inlineTreeHasTag(const QVector<InlineNode>& roots, const QString& tag, bool descendants) {
  std::vector<const InlineNode*> pending;
  pending.reserve(static_cast<std::size_t>(roots.size()));
  for (const InlineNode& node : roots) {
    pending.push_back(&node);
  }
  while (!pending.empty()) {
    const InlineNode* node = pending.back();
    pending.pop_back();
    const auto semanticTag = cssTagForInline(node->type());
    if (semanticTag == tag || (tag == "span" && !markdownInlineKind(semanticTag).isEmpty())) {
      return true;
    }
    if (descendants) {
      for (const InlineNode& child : node->children()) {
        pending.push_back(&child);
      }
    }
  }
  return false;
}

bool inlineTreeHasHtml(const QVector<InlineNode>& nodes) {
  for (const auto& node : nodes)
    if (node.type() == InlineType::HtmlInline || inlineTreeHasHtml(node.children()))
      return true;
  return false;
}

bool hasSyntheticAlertTitle(const MarkdownNode& node) {
  return node.parent() && node.parent()->alertKind() != AlertKind::None;
}

} // namespace

QString cssTagForNode(const MarkdownNode& node) {
  switch (node.type()) {
  case BlockType::Paragraph:
    return QStringLiteral("p");
  case BlockType::Heading:
    return QStringLiteral("h%1").arg(node.headingLevel());
  case BlockType::BlockQuote:
    return QStringLiteral("blockquote");
  case BlockType::List:
    return node.listKind() == ListKind::Ordered ? QStringLiteral("ol") : QStringLiteral("ul");
  case BlockType::ListItem:
    return QStringLiteral("li");
  case BlockType::CodeFence:
  case BlockType::FrontMatter:
    return QStringLiteral("pre");
  case BlockType::Table:
    return QStringLiteral("table");
  case BlockType::TableRow:
    return QStringLiteral("tr");
  case BlockType::TableCell:
    return node.parent() && node.parent()->tableRowIsHeader() ? QStringLiteral("th") : QStringLiteral("td");
  case BlockType::ThematicBreak:
    return QStringLiteral("hr");
  case BlockType::MathBlock:
    return QStringLiteral("div");
  default:
    return QString();
  }
}

NodeCssElementBuilder::NodeCssElementBuilder(bool maintainTypeIndex) : maintainTypeIndex_(maintainTypeIndex) {
  host_.html.cacheId = nextCssElementIdentity();
  host_.body.cacheId = nextCssElementIdentity();
  host_.content.cacheId = nextCssElementIdentity();
  host_.write.cacheId = nextCssElementIdentity();
}

QByteArray cssSemanticSignature(const MarkdownNode& root) {
  // Exact, process-local shape encoding: prefix lengths preserve tree/string
  // boundaries without hashing away CSS-visible differences. Small enums and
  // counts occupy one byte instead of a QDataStream call and 4/8 bytes each.
  QByteArray data;
  data.reserve(96);
  const auto number = [&](quint64 value) {
    while (value >= 128) {
      data.append(char((value & 127) | 128));
      value >>= 7;
    }
    data.append(char(value));
  };
  const auto string = [&](const QString& value) {
    number(value.isNull() ? 0 : quint64(value.size()) + 1);
    data.append(reinterpret_cast<const char*>(value.constData()), value.size() * qsizetype(sizeof(QChar)));
  };
  const auto inlines = [&](const auto& self, const QVector<InlineNode>& nodes) -> void {
    number(quint64(nodes.size()));
    for (const auto& node : nodes) {
      number(quint64(node.type()));
      if (node.type() == InlineType::HtmlInline)
        string(node.text());
      if (node.type() == InlineType::Link)
        string(node.href());
      self(self, node.children());
    }
  };
  const auto block = [&](const auto& self, const MarkdownNode& node) -> void {
    number(quint64(node.type()));
    number(quint64(node.headingLevel()));
    number(quint64(node.listKind()));
    number(node.isTaskItem());
    number(node.taskChecked());
    number(quint64(node.alertKind()));
    string(node.codeLanguage());
    if (node.type() == BlockType::HtmlBlock)
      string(node.literal());
    number(node.tableRowIsHeader());
    // Alert title text produces semantic classes rather than ordinary glyphs.
    const bool alertTitle = hasSyntheticAlertTitle(node) && !node.parent()->children().empty() &&
                            node.parent()->children().front().get() == &node && !node.inlines().isEmpty() &&
                            node.inlines().front().text().startsWith("[!") &&
                            node.inlines().front().text().endsWith(']');
    string(alertTitle ? node.inlines().front().text() : QString());
    inlines(inlines, node.inlines());
    number(quint64(node.children().size()));
    for (const auto& child : node.children())
      self(self, *child);
  };
  block(block, root);
  return data;
}

CssElement* NodeCssElementBuilder::makeOwned() const {
  pool_.push_back(std::make_unique<CssElement>());
  pool_.back()->cacheId = nextCssElementIdentity();
  return pool_.back().get();
}

const CssElement* NodeCssElementBuilder::ensure(const MarkdownNode& node) const {
  const auto flow = flowFor(node);
  const auto found = cache_.constFind(&node);
  if (found != cache_.constEnd()) {
    return found.value();
  }

  CssElement* element = makeOwned();
  element->tag = cssTagForNode(node);
  element->navigator = this;
  if (const auto* container = flow->container(node)) {
    element->tag = container->attributes.tag;
    element->id = container->attributes.id;
    element->classes = container->attributes.classes;
    element->attributes = container->attributes.attributes;
    element->inlineDeclarations = container->attributes.inlineDeclarations;
    element->presentationDeclarations = container->attributes.presentationDeclarations;
  }
  addDocumentHostClasses(*element);
  if (node.type() == BlockType::Document) {
    element->id = QStringLiteral("write");
    element->tag = QStringLiteral("div");
    element->classes << QStringLiteral("md-document");
  } else if (node.type() == BlockType::List) {
    const bool taskList = std::any_of(node.children().cbegin(), node.children().cend(), [](const auto& child) {
      return child && child->type() == BlockType::ListItem && child->isTaskItem();
    });
    if (taskList)
      element->classes << QStringLiteral("task-list");
  } else if (node.type() == BlockType::ListItem && node.isTaskItem()) {
    element->classes << QStringLiteral("md-task-list-item");
    element->attributes.insert(QStringLiteral("data-task"), QStringLiteral("true"));
    element->attributes.insert(QStringLiteral("aria-checked"),
                               node.taskChecked() ? QStringLiteral("true") : QStringLiteral("false"));
    if (node.taskChecked())
      element->attributes.insert(QStringLiteral("checked"), QStringLiteral("checked"));
  } else if (node.type() == BlockType::BlockQuote && node.alertKind() != AlertKind::None) {
    element->classes << QStringLiteral("md-alert");
    const auto alertName = [&] {
      switch (node.alertKind()) {
      case AlertKind::Note:
        return QStringLiteral("note");
      case AlertKind::Tip:
        return QStringLiteral("tip");
      case AlertKind::Important:
        return QStringLiteral("important");
      case AlertKind::Warning:
        return QStringLiteral("warning");
      case AlertKind::Caution:
        return QStringLiteral("caution");
      default:
        return QString();
      }
    }();
    if (!alertName.isEmpty())
      element->classes << QStringLiteral("md-alert-") + alertName;
    element->attributes.insert(QStringLiteral("data-alert"), alertName);
  } else if (node.type() == BlockType::Table) {
    element->classes << QStringLiteral("md-table");
  } else if (node.type() == BlockType::MathBlock) {
    element->classes << QStringLiteral("md-math-block") << QStringLiteral("mathjax-block");
  }
  if (node.type() == BlockType::CodeFence || node.type() == BlockType::FrontMatter) {
    element->attributes.insert(QStringLiteral("data-code-block"), QStringLiteral("true"));
    const QString language = node.codeLanguage().trimmed().toLower();
    if (!language.isEmpty()) {
      element->attributes.insert(QStringLiteral("data-language"), language);
      element->attributes.insert(QStringLiteral("lang"), language);
      element->classes << QStringLiteral("language-") + language;
    }
  }
  cache_.insert(&node, element);
  if (node.type() != BlockType::Document) {
    NodeBinding binding{node.topLevelBlock()->id(), {}};
    for (const auto* ancestor = &node; ancestor != node.topLevelBlock(); ancestor = ancestor->parent())
      binding.path.prepend(ancestor->siblingIndex());
    bindings_.insert(&node, std::move(binding));
  }
  nodes_.insert(element, &node);
  const auto* parent = flow->parent(node);
  element->parent = parent ? ensure(*parent) : (node.type() == BlockType::Document ? &host_.content : &host_.write);
  if (node.type() == BlockType::Table) {
    auto* wrapper = makeOwned();
    wrapper->tag = QStringLiteral("figure");
    wrapper->classes << QStringLiteral("table-figure");
    wrapper->parent = element->parent;
    wrapper->navigator = this;
    nodes_.insert(wrapper, &node);
    synthetic_[&node].insert(QStringLiteral("figure"), wrapper);
    element->parent = wrapper;
    element->childIndex = element->typeIndex = 0;
  }
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

std::shared_ptr<const MarkdownFlow> NodeCssElementBuilder::flowFor(const MarkdownNode& node) const {
  if (!flow_) {
    const auto* root = &node;
    while (root->parent())
      root = root->parent();
    flow_ = std::make_shared<MarkdownFlow>(*root);
  }
  return flow_;
}

const CssElement* NodeCssElementBuilder::build(const MarkdownNode& node, const QString& key) {
  const auto* parent = ensure(node);
  const auto pseudoSeparator = key.indexOf(QStringLiteral("::"));
  if (pseudoSeparator >= 0) {
    const auto pseudo = key.mid(pseudoSeparator + 2).section(QLatin1Char(':'), 0, 0);
    return buildPseudo(*parent, pseudo);
  }
  const QString tag = key.section(QLatin1Char(' '), -1).section(QLatin1Char(':'), 0, 0);
  if (node.type() == BlockType::Table && (key == ".table-figure" || tag == "figure"))
    return parent->parent;
  if (tag.isEmpty() || tag == parent->tag || tag.startsWith(QLatin1Char('#')))
    return parent;
  auto& children = synthetic_[&node];
  if (!children.contains(tag)) {
    auto* child = makeOwned();
    child->tag = tag;
    addDocumentHostClasses(*child);
    child->parent = parent;
    if (tag == "code" && parent->tag == "pre")
      child->classes << QStringLiteral("md-fencescode");
    if (tag == "input" && node.isTaskItem()) {
      child->attributes.insert(QStringLiteral("type"), QStringLiteral("checkbox"));
      if (node.taskChecked())
        child->attributes.insert(QStringLiteral("checked"), QStringLiteral("checked"));
    }
    const QString kind = markdownInlineKind(tag);
    if (parent->tag != "pre" && !kind.isEmpty()) {
      auto* wrapper = makeOwned();
      wrapper->tag = QStringLiteral("span");
      wrapper->attributes.insert(QStringLiteral("md-inline"), kind);
      wrapper->parent = parent;
      child->parent = wrapper;
    }
    children.insert(tag, child);
  }
  return children.value(tag);
}

const CssElement* NodeCssElementBuilder::buildPseudo(const CssElement& origin, const QString& pseudo) const {
  auto& pseudos = pseudos_[&origin];
  if (!pseudos.contains(pseudo)) {
    auto* element = makeOwned();
    const auto identity = element->cacheId;
    *element = origin;
    element->cacheId = identity;
    element->inlineDeclarations.clear();
    element->pseudoElement = pseudo;
    element->originatingElement = &origin;
    if (const auto* node = nodeFor(origin))
      nodes_.insert(element, node);
    pseudos.insert(pseudo, element);
  }
  return pseudos.value(pseudo);
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
      inlineChildren_[parent].append(child);
      child->typeIndex = typeIndices[parent][tag]++;
      child->previousSibling = previousChildren.value(parent);
      if (child->previousSibling)
        const_cast<CssElement*>(child->previousSibling)->nextSibling = child;
      previousChildren[parent] = child;
      for (const auto* ancestor = parent; ancestor; ancestor = ancestor->parent) {
        auto* writable = const_cast<CssElement*>(ancestor);
        writable->hasDescendantTags.insert(tag);
        if (ancestor == parent)
          writable->hasChildTags.insert(tag);
      }
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
                child->attributes = box.cssAttributes;
                addHtmlPresentationHints(*child);
                if (!box.href().isEmpty())
                  child->attributes.insert(QStringLiteral("href"), box.href());
                child->inlineDeclarations = CssThemeParser::parseDeclarations(box.cssInlineStyle);
                for (const auto& declaration : child->inlineDeclarations)
                  inlineViewportUnits_ = inlineViewportUnits_ || CssThemeParser::usesViewportUnits(declaration.value);
                element = child;
                const auto tag = child->tag;
                if (tag != "br" && tag != "img" && tag != "input" && tag != "hr" &&
                    !markup.endsWith(QStringLiteral("/>")))
                  htmlParents.push_back(child);
              }
            }
          }
        } else if (node.type() == InlineType::Text && owner.parent() &&
                   owner.parent()->alertKind() != AlertKind::None && !owner.parent()->children().empty() &&
                   owner.parent()->children().front().get() == &owner && &node == &owner.inlines().front() &&
                   node.text().startsWith("[!") && node.text().endsWith(']')) {
          auto* container = childElement(htmlParents.back(), QStringLiteral("span"));
          container->classes << QStringLiteral("md-alert-text-container");
          auto* title = childElement(container, QStringLiteral("span"));
          title->classes << QStringLiteral("md-alert-text")
                         << QStringLiteral("md-alert-text-") + node.text().mid(2, node.text().size() - 3).toLower();
          auto* icon = childElement(title, QStringLiteral("svg"));
          icon->attributes.insert(QStringLiteral("viewbox"), QStringLiteral("0 0 16 16"));
          icon->attributes.insert(QStringLiteral("aria-hidden"), QStringLiteral("true"));
          auto* path = childElement(icon, QStringLiteral("path"));
          path->attributes.insert(QStringLiteral("d"),
                                  QString::fromLatin1(markdownAlertIconPath(owner.parent()->alertKind())));
          inlineImages_[&owner].push_back(
              {icon, path, node.sourceStart(), markdownAlertIconSvg(owner.parent()->alertKind())});
          element = title;
        } else {
          const QString tag = cssTagForInline(node.type());
          if (!tag.isEmpty()) {
            const CssElement* inlineParent = htmlParents.back();
            QString kind = markdownInlineKind(tag);
            if (node.type() == InlineType::InlineMath)
              kind = QStringLiteral("math");
            if (!kind.isEmpty()) {
              auto* wrapper = childElement(inlineParent, QStringLiteral("span"));
              wrapper->attributes.insert(QStringLiteral("md-inline"), kind);
              inlineParent = wrapper;
            }
            auto* child = childElement(inlineParent, tag);
            if (node.type() == InlineType::Link)
              child->attributes.insert(QStringLiteral("href"), node.href());
            if (node.type() == InlineType::TaskMarker) {
              child->attributes.insert(QStringLiteral("type"), QStringLiteral("checkbox"));
              child->attributes.insert(QStringLiteral("data-task-marker"), QStringLiteral("true"));
              if (owner.isTaskItem() && owner.taskChecked())
                child->attributes.insert(QStringLiteral("checked"), QStringLiteral("checked"));
            }
            element = child;
          }
        }
        views.push_back({node.sourceStart(), node.sourceEnd(), element, views.size()});
        append(node.children(), element);
      }
    };
    const auto* ownerElement = ensure(owner);
    append(owner.inlines(), ownerElement);
    for (const auto& view : views) {
      if (view.element == ownerElement)
        continue;
      for (const auto* parent = view.element->parent; parent;
           parent = parent == ownerElement ? nullptr : parent->parent) {
        auto* writable = const_cast<CssElement*>(parent);
        writable->hasDescendantTags.insert(view.element->tag);
        for (const auto& name : view.element->classes)
          writable->hasDescendantClasses.insert(name);
        if (parent == view.element->parent) {
          writable->hasChildTags.insert(view.element->tag);
          for (const auto& name : view.element->classes)
            writable->hasChildClasses.insert(name);
        }
      }
    }
    std::stable_sort(views.begin(), views.end(),
                     [](const InlineView& a, const InlineView& b) { return a.start < b.start; });
    inlineTrees_.insert(&owner, views);
  }
  const CssElement* result = ensure(owner);
  const auto& views = inlineTrees_.constFind(&owner).value();
  // A binary search reaches the leaf in ordinary text. Walking back is needed
  // only inside an enclosing element's closing Markdown delimiters.
  auto view = std::upper_bound(views.begin(), views.end(), offset,
                               [](qsizetype value, const InlineView& item) { return value < item.start; });
  while (view != views.begin()) {
    --view;
    if (offset < view->end) {
      result = view->element;
      break;
    }
  }
  return result;
}

QVector<CssInlineImage> NodeCssElementBuilder::inlineImages(const MarkdownNode& owner) const {
  buildInline(owner, 0);
  return inlineImages_.value(&owner);
}

NodeId NodeCssElementBuilder::sourceNodeId(const CssElement& element) const {
  const auto* node = nodeFor(element);
  return node && ensure(*node) == &element ? node->id() : NodeId{};
}

QString NodeCssElementBuilder::semanticKey(const CssElement& element) const {
  if (const auto id = sourceNodeId(element); id.isValid())
    return id.toString();
  QStringList path;
  for (const auto* current = &element; current; current = current->parent) {
    if (const auto id = sourceNodeId(*current); id.isValid())
      return id.toString() + '/' + path.join('/');
    path.prepend(current->tag + ':' + QString::number(current->childIndex));
  }
  return "host/" + element.tag + '/' + element.id;
}

bool NodeCssElementBuilder::mayHaveCounterSubjects(const MarkdownNode& node,
                                                   const QVector<CssCounterSubject>& subjects) const {
  // Tag index first: ordinary paragraphs need no adapter or CSS computation
  // when all block subjects are headings, lists, fences, or semantic classes.
  const auto tag = cssTagForNode(node);
  bool blockCandidate = false;
  for (const auto& subject : subjects) {
    if ((!subject.tag.isEmpty() && subject.tag != tag) || !subject.id.isEmpty())
      continue;
    if (!subject.classes.isEmpty() && node.type() == BlockType::Paragraph)
      continue;
    if (subject.mayMatch(*ensure(node))) {
      blockCandidate = true;
      break;
    }
  }
  if (blockCandidate)
    return true;
  // HTML can introduce arbitrary classes, variables and local declarations;
  // the ordinary inline tag index alone must never exclude it.
  if (inlineTreeHasHtml(node.inlines()) || hasSyntheticAlertTitle(node))
    return true;
  const QSet<QString> tags{"a", "code", "em", "strong", "del", "mark", "sub", "sup", "img", "input", "span"};
  bool possibleInline = false;
  for (const auto& subject : subjects) {
    if ((!subject.tag.isEmpty() && !tags.contains(subject.tag)) || !subject.id.isEmpty() || !subject.classes.isEmpty())
      continue;
    const auto* element = ensure(node);
    CssElement probe;
    probe.parent = element;
    auto ancestry = subject;
    ancestry.tag.clear();
    if (ancestry.mayMatch(probe)) {
      possibleInline = true;
      break;
    }
  }
  if (possibleInline && mayHaveInlineSubjects(*ensure(node), subjects))
    return true;
  return std::any_of(node.children().begin(), node.children().end(),
                     [&](const auto& child) { return mayHaveCounterSubjects(*child, subjects); });
}

bool NodeCssElementBuilder::mayHaveInlineSubjects(const CssElement& element,
                                                  const QVector<CssCounterSubject>& subjects) const {
  const auto* owner = nodeFor(element);
  if (!owner || ensure(*owner) != &element)
    return true;
  QVector<CssCounterSubject> inlineSubjects;
  const QSet<QString> inlineTags{"a", "code", "em", "strong", "del", "mark", "sub", "sup", "img", "input", "span"};
  for (const auto& subject : subjects) {
    if ((!subject.tag.isEmpty() && !inlineTags.contains(subject.tag)) || !subject.id.isEmpty() ||
        !subject.classes.isEmpty())
      continue;
    CssElement ancestorProbe;
    ancestorProbe.parent = &element;
    auto ancestry = subject;
    ancestry.tag.clear();
    if (ancestry.mayMatch(ancestorProbe))
      inlineSubjects.append(subject);
  }
  // Markdown inlines have no authored classes/ids. HTML can introduce arbitrary
  // subjects, variables and counter declarations, so retain its full subtree.
  std::function<bool(const QVector<InlineNode>&)> possible = [&](const auto& nodes) {
    for (const auto& node : nodes) {
      if (node.type() == InlineType::HtmlInline)
        return true;
      const auto tag = cssTagForInline(node.type());
      if (!tag.isEmpty()) {
        CssElement leaf;
        leaf.tag = tag;
        leaf.parent = &element;
        CssElement wrapper;
        wrapper.tag = "span";
        wrapper.parent = &element;
        for (const auto& subject : inlineSubjects)
          if (subject.mayMatch(leaf) || (!markdownInlineKind(tag).isEmpty() && subject.mayMatch(wrapper)))
            return true;
      }
      if (possible(node.children()))
        return true;
    }
    return false;
  };
  // Alert title compatibility nodes include semantic classes and SVG children.
  if (owner->parent() && owner->parent()->alertKind() != AlertKind::None)
    return true;
  return possible(owner->inlines());
}

QVector<const CssElement*> NodeCssElementBuilder::semanticChildren(const CssElement& element,
                                                                   bool includeInline) const {
  if (const auto* node = nodeFor(element)) {
    const auto* owner = ensure(*node);
    if (element.tag == "figure" && node->type() == BlockType::Table)
      return {owner};
    if (&element == owner) {
      QVector<const CssElement*> result;
      QSet<const CssElement*> seen;
      for (const auto* child : flowFor(*node)->children(*node)) {
        const auto* view = ensure(*child);
        while (view->parent && view->parent != owner)
          view = view->parent;
        if (!seen.contains(view)) {
          seen.insert(view);
          result.append(view);
        }
      }
      if (includeInline && !node->inlines().isEmpty()) {
        buildInline(*node, 0);
        result += inlineChildren_.value(owner);
      }
      return result;
    }
  }
  if (element.tag == "thead" || element.tag == "tbody") {
    QVector<const CssElement*> result;
    if (const auto* table = element.parent ? nodeFor(*element.parent) : nullptr)
      for (const auto& row : table->children()) {
        const auto* view = ensure(*row);
        if (view->parent == &element)
          result.append(view);
      }
    return result;
  }
  return inlineChildren_.value(&element);
}

qsizetype NodeCssElementBuilder::materializedElementCount() const {
  return static_cast<qsizetype>(pool_.size()) + 3; // shared html/body/content host
}

QVector<quint64> NodeCssElementBuilder::releaseInlineViews(const QSet<NodeId>& topLevelIds) {
  const auto selected = [&](const MarkdownNode* node) {
    const auto binding = bindings_.constFind(node);
    return binding != bindings_.cend() && topLevelIds.contains(binding->topLevel);
  };
  // Preserve every structural/synthetic block view and all of its ancestors.
  // Other blocks' parents and navigator identities must never dangle.
  QSet<const CssElement*> retained;
  const auto retain = [&](const CssElement* element) {
    for (; element && !retained.contains(element); element = element->parent)
      retained.insert(element);
  };
  for (auto* element : cache_)
    retain(element);
  for (const auto& children : synthetic_)
    for (auto* element : children)
      retain(element);
  QSet<const CssElement*> removed;
  QVector<quint64> identities;
  for (const auto& element : pool_) {
    const MarkdownNode* node = nullptr;
    for (const CssElement* ancestor = element.get(); ancestor && !node; ancestor = ancestor->parent)
      node = nodes_.value(ancestor, nullptr);
    if (!selected(node))
      continue;
    identities.append(element->cacheId);
    if (!retained.contains(element.get()))
      removed.insert(element.get());
  }
  for (auto it = inlineTrees_.begin(); it != inlineTrees_.end();)
    if (selected(it.key()))
      it = inlineTrees_.erase(it);
    else
      ++it;
  for (auto it = inlineImages_.begin(); it != inlineImages_.end();)
    if (selected(it.key()))
      it = inlineImages_.erase(it);
    else
      ++it;
  for (auto it = inlineChildren_.begin(); it != inlineChildren_.end();) {
    if (removed.contains(it.key()))
      it = inlineChildren_.erase(it);
    else {
      it.value().removeIf([&](auto* child) { return removed.contains(child); });
      ++it;
    }
  }
  for (auto it = pseudos_.begin(); it != pseudos_.end();) {
    if (removed.contains(it.key()))
      it = pseudos_.erase(it);
    else {
      for (auto child = it->begin(); child != it->end();)
        if (removed.contains(child.value()))
          child = it->erase(child);
        else
          ++child;
      ++it;
    }
  }
  for (auto* element : removed) {
    nodes_.remove(element);
    descendantQueries_.remove(element);
  }
  std::erase_if(pool_, [&](const auto& element) { return removed.contains(element.get()); });
  return identities;
}

bool NodeCssElementBuilder::rebindBlocks(const QHash<NodeId, const MarkdownNode*>& replacements) {
  // A boundary can change siblings outside the edited block. Rebuild the
  // borrowed topology before exposing replacement AST pointers.
  if (flow_ && !flow_->empty())
    return false;
  QHash<const MarkdownNode*, const MarkdownNode*> changed;
  for (auto it = bindings_.cbegin(); it != bindings_.cend(); ++it) {
    const auto replacement = replacements.constFind(it->topLevel);
    if (replacement == replacements.cend())
      continue;
    const auto* node = replacement.value();
    for (const auto index : it->path) {
      if (index < 0 || index >= qsizetype(node->children().size()))
        return false;
      node = node->children()[size_t(index)].get();
    }
    changed.insert(it.key(), node);
  }
  // Take every old key before inserting any new ones: the allocator can reuse
  // a deleted sibling's address for a different replacement node.
  const auto rekey = [&](auto& map) {
    using Map = std::decay_t<decltype(map)>;
    Map moved;
    for (auto it = changed.cbegin(); it != changed.cend(); ++it)
      if (map.contains(it.key()))
        moved.insert(it.value(), map.take(it.key()));
    map.insert(moved);
  };
  rekey(cache_);
  rekey(bindings_);
  rekey(synthetic_);
  rekey(inlineTrees_);
  rekey(inlineImages_);
  for (auto it = nodes_.begin(); it != nodes_.end(); ++it)
    if (const auto found = changed.constFind(it.value()); found != changed.cend())
      it.value() = found.value();
  for (const auto* node : changed) {
    bindings_[node].topLevel = node->topLevelBlock()->id();
    const auto found = inlineTrees_.find(node);
    if (found == inlineTrees_.end())
      continue;
    QVector<InlineRange> positions;
    std::function<void(const QVector<InlineNode>&)> append = [&](const auto& inlines) {
      for (const auto& inlineNode : inlines) {
        positions.append(inlineNode.sourceRange());
        append(inlineNode.children());
      }
    };
    append(node->inlines());
    for (auto& view : found.value()) {
      view.start = positions[view.ordinal].start;
      view.end = positions[view.ordinal].end;
    }
    std::stable_sort(found->begin(), found->end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    if (const auto images = inlineImages_.find(node); images != inlineImages_.end())
      for (auto& image : images.value())
        image.sourceOffset = node->inlines().front().sourceStart();
  }
  return true;
}

const CssElement* NodeCssElementBuilder::linkForSourceRange(const MarkdownNode& owner, qsizetype start, qsizetype end,
                                                            const QString& href) const {
  buildInline(owner, start);
  const auto& views = inlineTrees_.constFind(&owner).value();
  for (const auto& view : views) {
    if (view.end <= start || view.start >= end)
      continue;
    for (const auto* origin = view.element; origin; origin = origin->parent)
      if (origin->tag == "a" && origin->attributes.value("href") == href)
        return origin;
  }
  return nullptr;
}

const MarkdownNode* NodeCssElementBuilder::nodeFor(const CssElement& element) const {
  return nodes_.value(&element, nullptr);
}

const CssElement* NodeCssElementBuilder::previousSibling(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  if (node && node->type() == BlockType::Table && element.tag == "table")
    return nullptr;
  if (node && node->type() == BlockType::TableRow && node->previousSibling() &&
      node->previousSibling()->tableRowIsHeader() != node->tableRowIsHeader())
    return nullptr;
  const auto* previous = node ? flowFor(*node)->previous(*node) : nullptr;
  if (!previous)
    return nullptr;
  const auto* sibling = ensure(*previous);
  return previous->type() == BlockType::Table ? sibling->parent : sibling;
}

const CssElement* NodeCssElementBuilder::nextSibling(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  if (node && node->type() == BlockType::Table && element.tag == "table")
    return nullptr;
  if (node && node->type() == BlockType::TableRow && node->nextSibling() &&
      node->nextSibling()->tableRowIsHeader() != node->tableRowIsHeader())
    return nullptr;
  const auto* next = node ? flowFor(*node)->next(*node) : nullptr;
  if (!next)
    return nullptr;
  const auto* sibling = ensure(*next);
  return next->type() == BlockType::Table ? sibling->parent : sibling;
}

int NodeCssElementBuilder::childIndex(const CssElement& element) const {
  const MarkdownNode* node = nodeFor(element);
  if (node && flow_ && !flow_->empty() && node->type() != BlockType::TableRow &&
      !(node->type() == BlockType::Table && element.tag == "table")) {
    return flow_->childIndex(*node);
  }
  if (node && node->type() == BlockType::Table && element.tag == "table")
    return 0;
  if (node && node->type() == BlockType::TableRow && !node->tableRowIsHeader())
    return qMax(0, node->siblingIndex() - 1);
  return node ? node->siblingIndex() : -1;
}

int NodeCssElementBuilder::typeIndex(const CssElement& element) const {
  if (!maintainTypeIndex_) {
    return -1;
  }
  const MarkdownNode* node = nodeFor(element);
  if (node && flow_ && !flow_->empty() && node->type() != BlockType::TableRow &&
      !(node->type() == BlockType::Table && element.tag == "table")) {
    if (const auto index = flow_->typeIndex(*node); index >= 0)
      return index;
  }
  if (node && node->type() == BlockType::Table && element.tag == "table")
    return 0;
  if (node && node->type() == BlockType::TableRow)
    return childIndex(element);
  return node ? node->siblingTypeIndex() : -1;
}

bool NodeCssElementBuilder::hasTag(const CssElement& element, const QString& tag, bool directChild) const {
  const auto key = (directChild ? QStringLiteral("child-tag/") : QStringLiteral("tag/")) + tag;
  const auto cache = descendantQueries_.constFind(&element);
  if (cache != descendantQueries_.cend()) {
    const auto found = cache->constFind(key);
    if (found != cache->cend())
      return found.value();
  }
  const bool result = hasTagUncached(element, tag, directChild);
  descendantQueries_[&element].insert(key, result);
  return result;
}

bool NodeCssElementBuilder::hasTagUncached(const CssElement& element, const QString& tag, bool directChild) const {
  if (flow_ && !flow_->empty()) {
    for (const auto* child : semanticChildren(element))
      if (child->tag == tag || (!directChild && hasTag(*child, tag, false)))
        return true;
    return false;
  }
  const MarkdownNode* node = nodeFor(element);
  if (!node) {
    return false;
  }
  if (node->type() == BlockType::Table && element.tag == "figure")
    return tag == "table" || (!directChild && hasTag(*ensure(*node), tag, false));
  if (node->type() == BlockType::Table && element.tag == "table" && directChild)
    return std::any_of(node->children().cbegin(), node->children().cend(),
                       [&](const auto& row) { return tag == (row->tableRowIsHeader() ? "thead" : "tbody"); });

  for (const std::unique_ptr<MarkdownNode>& child : node->children()) {
    if ((child->type() == BlockType::Table ? QStringLiteral("figure") : cssTagForNode(*child)) == tag ||
        (!directChild && child->type() == BlockType::Table && tag == "table")) {
      return true;
    }
  }
  if (node->isTaskItem() && tag == "input")
    return true;
  if (!directChild && inlineTreeHasTag(node->inlines(), tag, true)) {
    return true;
  }
  if (inlineTreeHasHtml(node->inlines()) || hasSyntheticAlertTitle(*node) ||
      (directChild && inlineTreeHasTag(node->inlines(), tag, true))) {
    buildInline(*node, 0);
    if ((directChild ? element.hasChildTags : element.hasDescendantTags).contains(tag))
      return true;
  }
  if (directChild) {
    return false;
  }

  std::vector<const MarkdownNode*> pending;
  for (const std::unique_ptr<MarkdownNode>& child : node->children()) {
    pending.push_back(child.get());
  }
  while (!pending.empty()) {
    const MarkdownNode* descendant = pending.back();
    pending.pop_back();
    if (descendant->isTaskItem() && tag == "input")
      return true;
    if (inlineTreeHasTag(descendant->inlines(), tag, true)) {
      return true;
    }
    if (inlineTreeHasHtml(descendant->inlines()) || hasSyntheticAlertTitle(*descendant)) {
      buildInline(*descendant, 0);
      if (ensure(*descendant)->hasDescendantTags.contains(tag))
        return true;
    }
    for (const std::unique_ptr<MarkdownNode>& child : descendant->children()) {
      if (cssTagForNode(*child) == tag || (child->type() == BlockType::Table && tag == "figure")) {
        return true;
      }
      pending.push_back(child.get());
    }
  }
  return false;
}

bool NodeCssElementBuilder::hasClass(const CssElement& element, const QString& className, bool directChild) const {
  const auto key = (directChild ? QStringLiteral("child-class/") : QStringLiteral("class/")) + className;
  const auto cache = descendantQueries_.constFind(&element);
  if (cache != descendantQueries_.cend()) {
    const auto found = cache->constFind(key);
    if (found != cache->cend())
      return found.value();
  }
  const bool result = hasClassUncached(element, className, directChild);
  descendantQueries_[&element].insert(key, result);
  return result;
}

bool NodeCssElementBuilder::hasClassUncached(const CssElement& element, const QString& className,
                                             bool directChild) const {
  if (flow_ && !flow_->empty()) {
    for (const auto* child : semanticChildren(element))
      if (child->classes.contains(className) || (!directChild && hasClass(*child, className, false)))
        return true;
    return false;
  }
  const MarkdownNode* node = nodeFor(element);
  if (!node)
    return false;
  if (node->type() == BlockType::Table && element.tag == "figure")
    return ensure(*node)->classes.contains(className) || (!directChild && hasClass(*ensure(*node), className, false));
  if (inlineTreeHasHtml(node->inlines()) || hasSyntheticAlertTitle(*node))
    buildInline(*node, 0);
  if ((directChild ? element.hasChildClasses : element.hasDescendantClasses).contains(className))
    return true;
  for (const auto& child : node->children()) {
    const auto* childElement = child->type() == BlockType::Table ? ensure(*child)->parent : ensure(*child);
    if (childElement->classes.contains(className))
      return true;
    if (!directChild && hasClass(*childElement, className, false))
      return true;
  }
  return false;
}

} // namespace muffin
