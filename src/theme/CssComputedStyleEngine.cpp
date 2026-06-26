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
  bool nthEven = false;
  bool unsupported = false;
};

struct SelectorPart {
  SimpleSelector simple;
  QChar combinator;  // relation to the part on the left: ' ' descendant, '>' child, 0 for leftmost
};

struct ParsedSelector {
  QVector<SelectorPart> parts;
  bool valid = false;
  bool exportOnly = false;
  bool interactive = false;
  int specificity = 0;
};

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
      else if ((name == QStringLiteral("nth-child") || name == QStringLiteral("nth-of-type")) &&
               (arg == QStringLiteral("even") || arg.contains(QStringLiteral("2n")))) {
        out.nthEven = true;
      }
      else if (name == QStringLiteral("not") || name == QStringLiteral("root")) {
        // handled/safe no-op: :not(...) was stripped above; :root can match via variables elsewhere.
      }
      else {
        // Structural/content pseudos such as :has(img), :last-child and :first-of-type
        // cannot be evaluated against our prototype tree. Treating only the base tag
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
    if (paren == 0 && brk == 0 && c == QLatin1Char('>')) {
      flush();
      nextRelation = QLatin1Char('>');
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
  if (simple.nthEven && !element.nthEven) { return false; }
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

void applyStyleForElement(const CssThemeSheet& sheet, const CssElement& element, const CssElementState& state,
                          CssComputedStyle& style) {
  QHash<QString, Candidate> winners;
  int order = 0;
  for (const CssRule& rule : sheet.rules()) {
    if (rule.darkScope) { continue; }
    bool matched = false;
    int spec = 0;
    QString matchedSelector;
    for (const QString& selector : rule.selectors) {
      const ParsedSelector parsed = parseSelector(selector);
      if (!selectorMatches(parsed, element, state)) { continue; }
      if (!matched || parsed.specificity > spec) {
        matched = true;
        spec = parsed.specificity;
        matchedSelector = selector;
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

CssComputedStyle parentStyleFor(const CssThemeSheet& sheet, const CssElement* parent) {
  if (!parent) {
    CssComputedStyle root;
    root.customProperties_ = sheet.variables();
    return root;
  }
  CssComputedStyle inherited = parentStyleFor(sheet, parent->parent);
  CssComputedStyle own;
  own.customProperties_ = inherited.customProperties_;
  for (auto it = inherited.properties_.constBegin(); it != inherited.properties_.constEnd(); ++it) {
    if (inheritedProperties().contains(it.key())) { own.properties_.insert(it.key(), it.value()); }
  }
  applyStyleForElement(sheet, *parent, CssElementState{}, own);
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

CssComputedStyleEngine::CssComputedStyleEngine(const CssThemeSheet& sheet) : sheet_(sheet) {}

CssComputedStyle CssComputedStyleEngine::styleFor(const CssElement& element) const {
  return styleFor(element, CssElementState{});
}

CssComputedStyle CssComputedStyleEngine::styleFor(const CssElement& element, const CssElementState& state) const {
  CssComputedStyle style = parentStyleFor(sheet_, element.parent);
  applyStyleForElement(sheet_, element, state, style);
  return style;
}

}  // namespace muffin
