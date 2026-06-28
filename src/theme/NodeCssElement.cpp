#include "theme/NodeCssElement.h"

#include "document/MarkdownNode.h"
#include "document/MarkdownTypes.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QString>

namespace muffin {

Q_LOGGING_CATEGORY(cssPerf, "muffin.perf", QtWarningMsg)

namespace {

// CSS tag for an inline node — used to populate the :has(...) tag sets
// (`:has(img)`, `:has(code)`, `:has(a)`, …). "" for non-element inlines.
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
    case InlineType::InlineMath: return QStringLiteral("span");  // math renders as an inline box
    default: return QString();
  }
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
  // Synthetic ancestors above the document root, matching the prototype tree so
  // `body …` / `html …` / `#write …` selectors resolve identically at layout time.
  html_ = makeOwned();
  html_->tag = QStringLiteral("html");
  body_ = makeOwned();
  body_->tag = QStringLiteral("body");
  body_->parent = html_;
}

CssElement* NodeCssElementBuilder::makeOwned() {
  pool_.push_back(std::make_unique<CssElement>());
  return pool_.back().get();
}

const CssElement* NodeCssElementBuilder::ensure(const MarkdownNode& node) {
  const auto it = cache_.constFind(node.id());
  if (it != cache_.constEnd()) { return it.value(); }

  CssElement* e = makeOwned();
  e->tag = cssTagForNode(node);
  if (node.type() == BlockType::CodeFence) { e->classes << QStringLiteral("md-fences"); }
  if (node.type() == BlockType::Document) {
    // The document root IS the Typora `#write` container.
    e->id = QStringLiteral("write");
    e->tag.clear();
  }
  // Cache the element BEFORE recursing into parent/siblings. Adjacent siblings
  // reference each other (N.next = M, M.prev = N); without this early insert the
  // mutual reference would re-enter ensure mid-build and recurse without bound.
  cache_.insert(node.id(), e);

  // Parent chain: real parent, or the synthetic body for the document root.
  if (MarkdownNode* p = node.parent()) {
    e->parent = ensure(*p);
  } else {
    e->parent = body_;
  }

  // Wire this node's sibling group ITERATIVELY (once per parent). The old code recursed along the
  // sibling chain via ensure(nextSibling)/ensure(previousSibling) — recursion depth = sibling
  // count, which overflowed the 1MB stack on long flat block lists (100k+ top-level blocks →
  // APPCRASH 0xc00000fd). linkSiblingsIteratively builds every sibling in a loop instead.
  if (MarkdownNode* p = node.parent()) {
    linkSiblingsIteratively(*p);
  }

  populateHas(*e, node);
  return e;
}

const CssElement* NodeCssElementBuilder::build(const MarkdownNode& node) {
  return ensure(node);
}

void NodeCssElementBuilder::resetSiblingLinks() {
  linkedParents_.clear();
}

void NodeCssElementBuilder::linkSiblingsIteratively(const MarkdownNode& parent) {
  // Build every child's element and wire the sibling chain in ONE pass. Idempotent via
  // linkedParents_ — the N children each reach here through ensure, but only the first does the
  // work; the rest short-circuit. This replaces the recursive ensure(nextSibling) chain that
  // overflowed the stack on long flat block lists.
  if (linkedParents_.contains(parent.id())) {
    return;
  }
  linkedParents_.insert(parent.id());

  QElapsedTimer linkTimer;
  const bool measure = cssPerf().isDebugEnabled();
  if (measure) {
    linkTimer.start();
  }
  const auto& siblings = parent.children();
  CssElement* prev = nullptr;
  int idx = 0;
  QHash<QString, int> typeCounts;  // only touched when maintainTypeIndex_ (else unused, ~free)
  for (const std::unique_ptr<MarkdownNode>& s : siblings) {
    CssElement* cur = const_cast<CssElement*>(ensure(*s));  // memoized; ensure no longer recurses siblings
    cur->childIndex = idx++;
    if (maintainTypeIndex_) {
      cur->typeIndex = typeCounts[cur->tag]++;  // the per-sibling QString hash — skipped when no :*-of-type
    }
    cur->previousSibling = prev;
    cur->nextSibling = nullptr;
    if (prev) {
      prev->nextSibling = cur;
    }
    prev = cur;
  }
  if (measure) {
    qCDebug(cssPerf).nospace() << "css.linkSiblings n=" << siblings.size() << " "
                               << linkTimer.nsecsElapsed() / 1000000.0 << " ms";
  }
}

// Populate the precomputed :has tag sets: direct children (block children + the
// paragraph's inlines) into the *Child sets, and every descendant into the
// *Descendant sets. Markdown nodes carry no CSS classes, so the class sets stay
// empty (only HTML blocks could set them, which we do not parse).
void NodeCssElementBuilder::populateHas(CssElement& element, const MarkdownNode& node) {
  for (const std::unique_ptr<MarkdownNode>& child : node.children()) {
    const QString t = cssTagForNode(*child);
    if (!t.isEmpty()) {
      element.hasChildTags.insert(t);
      element.hasDescendantTags.insert(t);
    }
    collectDescendants(element, *child);
  }
  // A paragraph's inlines are its element children for :has purposes.
  for (const InlineNode& in : node.inlines()) {
    const QString t = cssTagForInline(in.type());
    if (!t.isEmpty()) {
      element.hasChildTags.insert(t);
      element.hasDescendantTags.insert(t);
    }
  }
}

void NodeCssElementBuilder::collectDescendants(CssElement& element, const MarkdownNode& node) {
  for (const std::unique_ptr<MarkdownNode>& child : node.children()) {
    const QString t = cssTagForNode(*child);
    if (!t.isEmpty()) { element.hasDescendantTags.insert(t); }
    collectDescendants(element, *child);
  }
  for (const InlineNode& in : node.inlines()) {
    const QString t = cssTagForInline(in.type());
    if (!t.isEmpty()) { element.hasDescendantTags.insert(t); }
  }
}

}  // namespace muffin
