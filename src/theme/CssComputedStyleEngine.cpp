#include "theme/CssComputedStyleEngine.h"

#include "theme/CssThemeParser.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringView>

#include <algorithm>
#include <functional>
#include <vector>

namespace muffin {
namespace {

// SimpleSelector / SelectorPart / ParsedSelector now live in the header — the
// engine caches a vector<ParsedSelector> built once in its constructor.

struct Candidate {
  QString value;
  QString selector;
  bool important = false;
  int specificity = 0;
  int order = 0;
};

const QSet<QString>& inheritedProperties() {
  static const QSet<QString> props = {
      QStringLiteral("color"), QStringLiteral("font-family"), QStringLiteral("font-size"),
      QStringLiteral("line-height"), QStringLiteral("text-align"), QStringLiteral("font-weight"),
      QStringLiteral("font-style")};
  return props;
}

bool isIdentChar(QChar c) {
  return c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_');
}

QString stripSimpleNot(QString compound, SimpleSelector& out) {
  int searchFrom = 0;
  while (true) {
    const int at = compound.indexOf(QStringLiteral(":not("), searchFrom, Qt::CaseInsensitive);
    if (at < 0) { break; }
    int depth = 0;
    int end = -1;
    for (int i = at + 4; i < compound.size(); ++i) {
      const QChar c = compound.at(i);
      if (c == QLatin1Char('(')) { ++depth; }
      else if (c == QLatin1Char(')')) {
        --depth;
        if (depth == 0) { end = i; break; }
      }
    }
    if (end < 0) { break; }
    QString arg = compound.mid(at + 5, end - at - 5).trimmed();
    if (arg.startsWith(QLatin1Char('.'))) { out.notClasses << arg.mid(1).toLower(); }
    else if (arg.startsWith(QLatin1Char('#'))) { out.notId = arg.mid(1).toLower(); }
    else if (!arg.isEmpty()) { out.notTag = arg.toLower(); }
    compound.remove(at, end - at + 1);
    searchFrom = at;
  }
  return compound;
}

// Parse a CSS An+B micro-syntax (the argument of :nth-child / :nth-of-type).
// Accepts: `even`(2n), `odd`(2n+1), an integer N (the Nth, a=0), and `an+b`
// forms: `2n`, `2n+1`, `n`, `n+3`, `-n+3`, `+3`. Returns valid=false for anything
// else. `a=0` means "exactly the b-th" element.
struct NthExpr { bool valid = false; int a = 0; int b = 0; };
NthExpr parseNth(const QString& arg) {
  NthExpr e;
  const QString s = arg.trimmed().toLower();
  if (s == QStringLiteral("even")) { return {true, 2, 0}; }
  if (s == QStringLiteral("odd")) { return {true, 2, 1}; }
  static const QRegularExpression re(QStringLiteral("^([+-]?\\d*)n\\s*([+-]\\s*\\d+)?$"));
  const QRegularExpressionMatch m = re.match(s);
  if (m.hasMatch()) {
    const QString aStr = m.captured(1);
    if (aStr.isEmpty() || aStr == QStringLiteral("+")) { e.a = 1; }
    else if (aStr == QStringLiteral("-")) { e.a = -1; }
    else { e.a = aStr.toInt(); }
    QString bStr = m.captured(2);
    if (!bStr.isEmpty()) { e.b = bStr.replace(QStringLiteral(" "), QString()).toInt(); }
    e.valid = true;
    return e;
  }
  bool ok = false;
  const int n = s.toInt(&ok);
  if (ok && n > 0) { return {true, 0, n}; }  // bare N → exactly the Nth
  return e;
}

// 1-based position p matches An+B iff some integer k≥0 satisfies p = a*k + b.
// For a==0 that is p==b. The `a*k+b==p` re-check guards against C++ truncation
// giving a spurious k.
bool nthPositionMatches(int a, int b, int p) {
  if (a == 0) { return p == b; }
  if (a * p < 0 && (p - b) % a != 0) { return false; }  // quick reject on sign mismatch
  const int k = (p - b) / a;
  return k >= 0 && a * k + b == p;
}

SimpleSelector parseCompound(QString compound) {
  SimpleSelector out;
  compound = stripSimpleNot(compound.trimmed(), out);
  int i = 0;
  const int n = compound.size();
  if (i < n && (compound.at(i).isLetter() || compound.at(i) == QLatin1Char('*'))) {
    int j = i;
    if (compound.at(i) == QLatin1Char('*')) {
      ++j;
    } else {
      while (j < n && isIdentChar(compound.at(j))) { ++j; }
      out.tag = compound.mid(i, j - i).toLower();
    }
    i = j;
  }
  while (i < n) {
    const QChar c = compound.at(i);
    if (c == QLatin1Char('#')) {
      int j = ++i;
      while (j < n && isIdentChar(compound.at(j))) { ++j; }
      out.id = compound.mid(i, j - i).toLower();
      i = j;
    } else if (c == QLatin1Char('.')) {
      int j = ++i;
      while (j < n && isIdentChar(compound.at(j))) { ++j; }
      const QString cls = compound.mid(i, j - i).toLower();
      out.classes << cls;
      if (cls == QStringLiteral("md-focus")) { out.mdFocus = true; }
      i = j;
    } else if (c == QLatin1Char(':')) {
      const bool element = (i + 1 < n && compound.at(i + 1) == QLatin1Char(':'));
      int j = element ? i + 2 : i + 1;
      const int nameStart = j;
      while (j < n && (compound.at(j).isLetterOrNumber() || compound.at(j) == QLatin1Char('-'))) { ++j; }
      const QString name = compound.mid(nameStart, j - nameStart).toLower();
      QString arg;
      if (j < n && compound.at(j) == QLatin1Char('(')) {
        int depth = 0;
        int end = -1;
        for (int k = j; k < n; ++k) {
          if (compound.at(k) == QLatin1Char('(')) { ++depth; }
          else if (compound.at(k) == QLatin1Char(')')) {
            --depth;
            if (depth == 0) { end = k; break; }
          }
        }
        if (end >= 0) {
          arg = compound.mid(j + 1, end - j - 1).trimmed().toLower();
          j = end + 1;
        } else {
          j = n;
        }
      }
      if (element || name == QStringLiteral("before") || name == QStringLiteral("after") ||
          name == QStringLiteral("selection") || name == QStringLiteral("marker")) {
        if (out.pseudoElement.isEmpty()) { out.pseudoElement = name; }
      } else if (name == QStringLiteral("hover")) { out.hover = true; }
      else if (name == QStringLiteral("focus")) { out.focus = true; }
      else if (name == QStringLiteral("active")) { out.active = true; }
      else if (name == QStringLiteral("visited")) { out.visited = true; }
      else if (name == QStringLiteral("first-child")) { out.firstChild = true; }
      else if (name == QStringLiteral("last-child")) { out.lastChild = true; }
      else if (name == QStringLiteral("only-child")) { out.onlyChild = true; }
      else if (name == QStringLiteral("first-of-type")) { out.firstOfType = true; }
      else if (name == QStringLiteral("nth-child") || name == QStringLiteral("nth-of-type")) {
        const NthExpr e = parseNth(arg);
        if (!e.valid) { out.unsupported = true; }
        else if (name == QStringLiteral("nth-child")) { out.nthChild = true; out.nthA = e.a; out.nthB = e.b; }
        else { out.nthOfType = true; out.nthA = e.a; out.nthB = e.b; }
      }
      else if (name == QStringLiteral("has")) {
        // :has(<simple>) where <simple> is [>] tag[.class] (.class), (.class), tag.
        // Full relative-selector :has (e.g. :has(> div .x)) is out of scope; a
        // compound argument marks the selector non-matching rather than risk a
        // partial/incorrect match.
        QString h = arg;
        bool direct = false;
        if (h.startsWith(QLatin1Char('>'))) { direct = true; h = h.mid(1).trimmed(); }
        // Reject combinators inside the argument (descendant/child beyond the leading >).
        if (h.contains(QLatin1Char(' ')) || h.contains(QLatin1Char('>'))) { out.unsupported = true; }
        else {
          QString tagPart = h;
          QString clsPart;
          const int dot = h.indexOf(QLatin1Char('.'));
          if (dot >= 0) { clsPart = h.mid(dot + 1).trimmed().toLower(); tagPart = h.left(dot).trimmed(); }
          if (tagPart == QStringLiteral("*")) { tagPart.clear(); }
          if (tagPart.isEmpty() && clsPart.isEmpty()) { out.unsupported = true; }
          else {
            out.hasPresent = true;
            out.hasDirect = direct;
            out.hasTag = tagPart.toLower();
            out.hasClass = clsPart;
          }
        }
      }
      else if (name == QStringLiteral("not") || name == QStringLiteral("root")) {
        // handled/safe no-op: :not(...) was stripped above; :root can match via variables elsewhere.
      }
      else {
        // Unsupported structural/content pseudos (:empty, :last-of-type, :only-of-type,
        // :lang, …) cannot be evaluated against our model. Treating only the base tag
        // as a match would globalize targeted rules (e.g. p:has(img) centering every
        // paragraph), so make the selector non-matching.
        out.unsupported = true;
      }
      i = j;
    } else if (c == QLatin1Char('[')) {
      int close = compound.indexOf(QLatin1Char(']'), i);
      i = close < 0 ? n : close + 1;
    } else {
      ++i;
    }
  }
  return out;
}

bool selectorRequiresExportContext(const QString& selector) {
  int paren = 0, brk = 0;
  bool inString = false;
  QChar quote;
  for (int i = 0; i < selector.size(); ++i) {
    const QChar c = selector.at(i);
    if (inString) {
      if (c == quote) { inString = false; }
      else if (c == QLatin1Char('\\') && i + 1 < selector.size()) { ++i; }
      continue;
    }
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inString = true; quote = c; continue; }
    if (c == QLatin1Char('(')) { ++paren; continue; }
    if (c == QLatin1Char(')')) { paren = qMax(0, paren - 1); continue; }
    if (c == QLatin1Char('[')) { ++brk; continue; }
    if (c == QLatin1Char(']')) { brk = qMax(0, brk - 1); continue; }
    if (paren != 0 || brk != 0 || c != QLatin1Char('.')) { continue; }
    int j = i + 1;
    while (j < selector.size() && isIdentChar(selector.at(j))) { ++j; }
    const QString cls = selector.mid(i + 1, j - i - 1).toLower();
    if (cls == QStringLiteral("typora-export") || cls == QStringLiteral("typora-export-sidebar") ||
        cls == QStringLiteral("typora-export-content")) {
      return true;
    }
    i = j - 1;
  }
  return false;
}

int specificityOf(const QString& selector) {
  int a = selector.count(QLatin1Char('#'));
  int b = selector.count(QLatin1Char('.')) + selector.count(QLatin1Char('[')) + selector.count(QLatin1Char(':'));
  static const QRegularExpression tagRe(QStringLiteral("(^|[\\s>+~])[a-zA-Z]"));
  int c = 0;
  auto it = tagRe.globalMatch(selector);
  while (it.hasNext()) { ++c; it.next(); }
  return a * 10000 + b * 100 + c;
}

ParsedSelector parseSelector(const QString& selector) {
  ParsedSelector parsed;
  parsed.exportOnly = selectorRequiresExportContext(selector);
  parsed.specificity = specificityOf(selector);
  QVector<QString> compounds;
  QVector<QChar> relations;
  QString cur;
  int paren = 0, brk = 0;
  bool inString = false;
  QChar quote;
  QChar nextRelation = QChar();
  auto flush = [&]() {
    const QString t = cur.trimmed();
    if (!t.isEmpty()) {
      compounds.push_back(t);
      relations.push_back(nextRelation);
      nextRelation = QLatin1Char(' ');
    }
    cur.clear();
  };
  for (int i = 0; i < selector.size(); ++i) {
    const QChar c = selector.at(i);
    if (inString) {
      cur += c;
      if (c == quote) { inString = false; }
      else if (c == QLatin1Char('\\') && i + 1 < selector.size()) { cur += selector.at(++i); }
      continue;
    }
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inString = true; quote = c; cur += c; continue; }
    if (c == QLatin1Char('(')) { ++paren; cur += c; continue; }
    if (c == QLatin1Char(')')) { paren = qMax(0, paren - 1); cur += c; continue; }
    if (c == QLatin1Char('[')) { ++brk; cur += c; continue; }
    if (c == QLatin1Char(']')) { brk = qMax(0, brk - 1); cur += c; continue; }
    if (paren == 0 && brk == 0 && (c == QLatin1Char('>') || c == QLatin1Char('+') || c == QLatin1Char('~'))) {
      flush();
      nextRelation = c;
      continue;
    }
    if (paren == 0 && brk == 0 && c.isSpace()) {
      flush();
      continue;
    }
    cur += c;
  }
  flush();
  if (compounds.isEmpty()) { return parsed; }
  for (int i = 0; i < compounds.size(); ++i) {
    SelectorPart part;
    part.simple = parseCompound(compounds.at(i));
    part.combinator = i == 0 ? QChar() : relations.at(i);
    parsed.interactive = parsed.interactive || part.simple.hover || part.simple.focus || part.simple.active ||
                         part.simple.visited || part.simple.mdFocus;
    parsed.parts.push_back(part);
  }
  parsed.valid = true;
  return parsed;
}

bool stateMatches(const SimpleSelector& simple, const CssElementState& state) {
  if (simple.unsupported) { return false; }
  if (simple.hover && !state.hover) { return false; }
  if (simple.focus && !state.focus) { return false; }
  if (simple.active && !state.active) { return false; }
  if (simple.visited && !state.visited) { return false; }
  if (simple.mdFocus && !state.mdFocus) { return false; }
  return true;
}

bool simpleMatches(const SimpleSelector& simple, const CssElement& element, const CssElementState& state) {
  if (!stateMatches(simple, state)) { return false; }
  const QString tag = element.tag.toLower();
  const QString id = element.id.toLower();
  const QString pseudo = element.pseudoElement.toLower();
  if (!simple.tag.isEmpty() && simple.tag != tag) { return false; }
  if (!simple.id.isEmpty() && simple.id != id) { return false; }
  if (!simple.pseudoElement.isEmpty() && simple.pseudoElement != pseudo) { return false; }
  if (simple.pseudoElement.isEmpty() && !pseudo.isEmpty()) { return false; }
  // Structural pseudo-classes need a real sibling-aware element (childIndex/typeIndex
  // set by the adapter). On the load-time prototype they are -1, so these correctly
  // fail to match there — structural selectors are evaluated only against the live tree.
  if (simple.firstChild && element.childIndex != 0) { return false; }
  if (simple.lastChild && !(element.childIndex >= 0 && element.nextSibling == nullptr)) { return false; }
  if (simple.onlyChild && !(element.childIndex == 0 && element.nextSibling == nullptr)) { return false; }
  if (simple.firstOfType && element.typeIndex != 0) { return false; }
  if (simple.nthChild) {
    if (element.childIndex < 0 || !nthPositionMatches(simple.nthA, simple.nthB, element.childIndex + 1)) { return false; }
  }
  if (simple.nthOfType) {
    if (element.typeIndex < 0 || !nthPositionMatches(simple.nthA, simple.nthB, element.typeIndex + 1)) { return false; }
  }
  if (simple.hasPresent) {
    // :has(tag)/:has(.cls) probed against the precomputed descendant (or direct-child)
    // tag/class sets the adapter populated from the live subtree.
    const QSet<QString>& tags = simple.hasDirect ? element.hasChildTags : element.hasDescendantTags;
    const QSet<QString>& cls = simple.hasDirect ? element.hasChildClasses : element.hasDescendantClasses;
    if (!simple.hasTag.isEmpty() && !tags.contains(simple.hasTag)) { return false; }
    if (!simple.hasClass.isEmpty() && !cls.contains(simple.hasClass)) { return false; }
  }
  QStringList classes;
  for (const QString& cls : element.classes) { classes << cls.toLower(); }
  for (const QString& cls : simple.classes) {
    if (!classes.contains(cls)) { return false; }
  }
  if (!simple.notTag.isEmpty() && simple.notTag == tag) { return false; }
  if (!simple.notId.isEmpty() && simple.notId == id) { return false; }
  for (const QString& cls : simple.notClasses) {
    if (classes.contains(cls)) { return false; }
  }
  return true;
}

bool selectorMatchesAt(const ParsedSelector& selector, int index, const CssElement* element,
                       const CssElementState& targetState) {
  if (!element || index < 0) { return false; }
  const CssElementState state = index == selector.parts.size() - 1 ? targetState : CssElementState{};
  if (!simpleMatches(selector.parts.at(index).simple, *element, state)) { return false; }
  if (index == 0) { return true; }
  const QChar rel = selector.parts.at(index).combinator;
  if (rel == QLatin1Char('>')) {
    return selectorMatchesAt(selector, index - 1, element->parent, targetState);
  }
  if (rel == QLatin1Char('+')) {
    // Adjacent sibling: the element immediately to the left.
    return selectorMatchesAt(selector, index - 1, element->previousSibling, targetState);
  }
  if (rel == QLatin1Char('~')) {
    // General sibling: any element to the left.
    for (const CssElement* s = element->previousSibling; s; s = s->previousSibling) {
      if (selectorMatchesAt(selector, index - 1, s, targetState)) { return true; }
    }
    return false;
  }
  for (const CssElement* p = element->parent; p; p = p->parent) {
    if (selectorMatchesAt(selector, index - 1, p, targetState)) { return true; }
  }
  return false;
}

bool selectorMatches(const ParsedSelector& selector, const CssElement& element, const CssElementState& state) {
  if (!selector.valid || selector.exportOnly || selector.parts.isEmpty()) { return false; }
  return selectorMatchesAt(selector, selector.parts.size() - 1, &element, state);
}

bool beats(const Candidate& a, const Candidate& b) {
  if (a.important != b.important) { return a.important; }
  if (a.specificity != b.specificity) { return a.specificity > b.specificity; }
  return a.order > b.order;
}

// applyStyleForElement / parentStyleFor are now CssComputedStyleEngine members
// (they read the cached parse + sheet_). Their definitions sit below, next to
// the constructor.

}  // namespace

QString CssComputedStyle::rawValue(const QString& property) const {
  const QString key = property.toLower();
  if (key.startsWith(QStringLiteral("--"))) { return customProperties_.value(key); }
  return properties_.value(key);
}

QString CssComputedStyle::resolvedValue(const QString& property) const {
  return CssThemeParser::resolveVars(rawValue(property), customProperties_);
}

bool CssComputedStyle::hasProperty(const QString& property) const {
  const QString key = property.toLower();
  return key.startsWith(QStringLiteral("--")) ? customProperties_.contains(key) : properties_.contains(key);
}

CssComputedStyleEngine::CssComputedStyleEngine(const CssThemeSheet& sheet) : sheet_(sheet) {
  // Pre-parse every selector once. The match path (applyStyleForElement) reads
  // parsedSelectors_ / ruleSelectorRange_ instead of re-running parseSelector
  // (regex + char walk) on every node × every ancestor level × every rule.
  const auto& rules = sheet_.rules();
  qsizetype totalSelectors = 0;
  for (const CssRule& rule : rules) { totalSelectors += rule.selectors.size(); }
  parsedSelectors_.reserve(static_cast<std::size_t>(totalSelectors));
  ruleSelectorRange_.reserve(rules.size());
  for (const CssRule& rule : rules) {
    const int start = static_cast<int>(parsedSelectors_.size());
    for (const QString& selector : rule.selectors) {
      ParsedSelector ps = parseSelector(selector);
      ps.selectorText = selector;
      parsedSelectors_.push_back(std::move(ps));
    }
    ruleSelectorRange_.emplace_back(start, static_cast<int>(parsedSelectors_.size()));
  }
}

void CssComputedStyleEngine::applyStyleForElement(const CssElement& element, const CssElementState& state,
                                                  CssComputedStyle& style) const {
  QHash<QString, Candidate> winners;
  int order = 0;
  const auto& rules = sheet_.rules();
  for (std::size_t ri = 0; ri < rules.size(); ++ri) {
    const CssRule& rule = rules[ri];
    if (rule.darkScope) { continue; }
    bool matched = false;
    int spec = 0;
    QString matchedSelector;
    const int sStart = ruleSelectorRange_[ri].first;
    const int sEnd = ruleSelectorRange_[ri].second;
    for (int si = sStart; si < sEnd; ++si) {
      const ParsedSelector& parsed = parsedSelectors_[si];
      if (!selectorMatches(parsed, element, state)) { continue; }
      if (!matched || parsed.specificity > spec) {
        matched = true;
        spec = parsed.specificity;
        matchedSelector = parsed.selectorText;
      }
    }
    for (const CssDeclaration& decl : rule.declarations) {
      if (matched) {
        Candidate c{decl.value, matchedSelector, decl.important, spec, order};
        const auto it = winners.constFind(decl.property);
        if (it == winners.constEnd() || beats(c, it.value())) { winners.insert(decl.property, c); }
      }
      ++order;
    }
  }
  for (auto it = winners.constBegin(); it != winners.constEnd(); ++it) {
    if (it.key().startsWith(QStringLiteral("--"))) {
      style.customProperties_.insert(it.key(), it.value().value);
    } else {
      style.properties_.insert(it.key(), it.value().value);
    }
  }
}

CssComputedStyle CssComputedStyleEngine::parentStyleFor(const CssElement* parent) const {
  if (!parent) {
    CssComputedStyle root;
    root.customProperties_ = sheet_.variables();
    return root;
  }
  CssComputedStyle inherited = parentStyleFor(parent->parent);
  CssComputedStyle own;
  own.customProperties_ = inherited.customProperties_;
  for (auto it = inherited.properties_.constBegin(); it != inherited.properties_.constEnd(); ++it) {
    if (inheritedProperties().contains(it.key())) { own.properties_.insert(it.key(), it.value()); }
  }
  applyStyleForElement(*parent, CssElementState{}, own);
  // CSS inheritance: a child inherits only the parent's inherited properties
  // (color, font-*, line-height, text-align) plus CSS variables — NOT padding,
  // margin, border, width, etc. Returning `own` verbatim leaked the parent's
  // non-inherited declarations into every descendant. Concrete symptom: github's
  // `#write { padding: 30px; padding-bottom: 100px }` injected padding-bottom:100px
  // into every element, and blockquote — which declares only the `padding`
  // shorthand, never the `padding-bottom` longhand — kept the leaked 100px,
  // growing every quote ~112px too tall.
  CssComputedStyle inheritable;
  inheritable.customProperties_ = own.customProperties_;
  for (auto it = own.properties_.constBegin(); it != own.properties_.constEnd(); ++it) {
    if (inheritedProperties().contains(it.key())) { inheritable.properties_.insert(it.key(), it.value()); }
  }
  return inheritable;
}

CssComputedStyle CssComputedStyleEngine::styleFor(const CssElement& element) const {
  return styleFor(element, CssElementState{});
}

CssComputedStyle CssComputedStyleEngine::styleFor(const CssElement& element, const CssElementState& state) const {
  CssComputedStyle style = parentStyleFor(element.parent);
  applyStyleForElement(element, state, style);
  return style;
}

}  // namespace muffin
