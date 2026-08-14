#pragma once

#include "theme/CssThemeParser.h"

#include <QChar>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>
#include <vector>

namespace muffin {

struct CssElement;

// Optional live-tree navigation used by layout-time elements. Prototype elements
// keep navigator=null and use the materialized fields below. Keeping navigation
// behind this narrow interface lets the selector engine query MarkdownNode's
// existing parent/sibling links without duplicating the whole document tree.
class CssElementNavigator {
public:
  virtual ~CssElementNavigator() = default;

  virtual const CssElement* previousSibling(const CssElement& element) const = 0;
  virtual const CssElement* nextSibling(const CssElement& element) const = 0;
  virtual int childIndex(const CssElement& element) const = 0;
  virtual int typeIndex(const CssElement& element) const = 0;
  virtual bool hasTag(const CssElement& element, const QString& tag, bool directChild) const = 0;
  virtual bool hasClass(const CssElement& element, const QString& className, bool directChild) const = 0;
};

// A node the selector engine matches against. The load-time prototype tree
// (CssThemeMapper::fromSheet) fills only tag/id/classes/pseudoElement/parent —
// the sibling/index/has fields stay default (null/-1/empty), so sibling
// combinators and structural pseudos never match there (correct: the prototype
// has no real siblings). The layout-time adapter (NodeCssElement) populates the
// full set from the live MarkdownNode tree, unlocking `+`/`~`, `:first-child`,
// `:nth-child(n)`, `:has(...)` etc.
struct CssElement {
  QString tag;
  QString id;
  QStringList classes;
  // SVG/HTML attributes used by Mermaid selectors (`[data-look="neo"]`,
  // `[data-et="node"]`, `[aria-*]`, ...). Attribute names are matched ASCII
  // case-insensitively; values follow normal CSS case-sensitive matching unless
  // the selector carries the `i` flag.
  QHash<QString, QString> attributes;
  QString pseudoElement;  // "", "before", "after", "selection", "marker"
  const CssElement* parent = nullptr;
  const CssElement* previousSibling = nullptr;  // adapter only; prototype stays null
  const CssElement* nextSibling = nullptr;      // adapter only
  int childIndex = -1;   // 0-based position among the parent's element children; -1 ⇒ N/A
  int typeIndex = -1;    // 0-based position among same-tag siblings; -1 ⇒ N/A
  // Precomputed descendant/child tag+class sets, populated by the adapter for
  // `:has(...)` support. `:has(img)` ⇒ hasDescendantTags contains "img"; `:has(.x)`
  // ⇒ hasDescendantClasses contains "x"; `:has(> img)` uses the *Child sets.
  // Prototype leaves these empty (its elements have no descendants).
  QSet<QString> hasDescendantTags;
  QSet<QString> hasDescendantClasses;
  QSet<QString> hasChildTags;
  QSet<QString> hasChildClasses;
  const CssElementNavigator* navigator = nullptr;  // live adapter only; never owned
};

struct CssAttributeSelector {
  enum class Operator { Exists, Equals, IncludesWord, DashMatch, Prefix,
                        Suffix, Contains };
  QString name;
  QString value;
  Operator op = Operator::Exists;
  bool caseInsensitive = false;
};

struct CssElementState {
  bool hover = false;
  bool focus = false;
  bool active = false;
  bool visited = false;
  bool mdFocus = false;
};

class CssComputedStyle {
public:
  QString rawValue(const QString& property) const;
  QString resolvedValue(const QString& property) const;
  bool hasProperty(const QString& property) const;

  const QHash<QString, QString>& customProperties() const { return customProperties_; }

  QHash<QString, QString> properties_;
  QHash<QString, QString> customProperties_;
};

// Selector parse tree. Pre-parsed once per sheet in the engine constructor and
// cached, so the match path never re-runs parseSelector (regex + char walk).
// Lives in the header (not the .cpp anon namespace) so the engine can hold a
// vector<ParsedSelector> member; these are the engine's private implementation
// detail — no other translation unit references them.
struct SimpleSelector {
  QString tag;
  QString id;
  QStringList classes;
  QStringList notClasses;
  QString notId;
  QString notTag;
  QString pseudoElement;
  bool hover = false;
  bool focus = false;
  bool active = false;
  bool visited = false;
  bool mdFocus = false;
  // Structural pseudo-classes (need a real sibling-aware tree ⇒ adapter only).
  bool firstChild = false;
  bool lastChild = false;
  bool onlyChild = false;
  bool firstOfType = false;
  bool nthChild = false;   // :nth-child(an+b) — matched against childIndex
  bool nthOfType = false;  // :nth-of-type(an+b) — matched against typeIndex
  int nthA = 0;
  int nthB = 0;
  // :has(<simple>) — descendant (default) or direct-child (`>`) tag/class probe.
  bool hasPresent = false;
  QString hasTag;
  QString hasClass;
  bool hasDirect = false;
  bool unsupported = false;
  QVector<CssAttributeSelector> attributes;
  // Rightmost compound carries a Typora editor-only class (md-meta-block, ty-*, …) —
  // propagated to ParsedSelector.editorOnly; such selectors never match (see TyporaEditorOnly.h).
  bool editorOnly = false;
};

struct SelectorPart {
  SimpleSelector simple;
  QChar combinator;  // relation to the part on the left: ' ' descendant, '>' child, '+'/'~' sibling, 0 for leftmost
};

struct ParsedSelector {
  QVector<SelectorPart> parts;
  bool valid = false;
  bool exportOnly = false;
  bool interactive = false;
  // True when the rightmost compound carries a Typora editor-only class — selector is
  // dropped at match time (selectorMatches) so editor hacks never apply. Mirrors exportOnly.
  bool editorOnly = false;
  int specificity = 0;
  QString selectorText;  // original selector string (kept for Candidate.selector trace/debug)
};

struct CssSelectorFeatures {
  bool hasStructuralRules = false;
  bool needsTypeIndex = false;
};

class CssComputedStyleEngine {
public:
  explicit CssComputedStyleEngine(const CssThemeSheet& sheet);

  CssComputedStyle styleFor(const CssElement& element) const;
  CssComputedStyle styleFor(const CssElement& element, const CssElementState& state) const;
  CssComputedStyle styleFor(
      const CssElement& element, const CssElementState& state,
      const std::vector<CssDeclaration>& inlineDeclarations) const;
  CssComputedStyle styleFor(
      const CssElement& element, const CssElementState& state,
      const std::vector<CssDeclaration>& inlineDeclarations,
      const std::vector<CssDeclaration>& presentationDeclarations) const;
  const CssSelectorFeatures& selectorFeatures() const { return selectorFeatures_; }

private:
  void applyStyleForElement(const CssElement& element, const CssElementState& state,
                            CssComputedStyle& style) const;
  CssComputedStyle parentStyleFor(const CssElement* parent) const;

  const CssThemeSheet& sheet_;
  std::vector<ParsedSelector> parsedSelectors_;          // every selector of every rule, flattened
  std::vector<std::pair<int, int>> ruleSelectorRange_;   // per-rule [start,end) into parsedSelectors_
  CssSelectorFeatures selectorFeatures_;
};

}  // namespace muffin
