#include "theme/CssComputedStyleEngine.h"
#include <QDataStream>
#include <QIODevice>
#include "diagnostics/ThemePerformance.h"
#include "diagnostics/ScopedPerfProbe.h"
#include "theme/CssGridStyle.h"
#include "theme/CssLayoutStyle.h"

#include "theme/CssThemeParser.h"
#include "theme/CssSelectorUtils.h"
#include "theme/TyporaEditorOnly.h"
#include "theme/CssValueParser.h"
#include "theme/CssContent.h"
#include "theme/CssImageResources.h"
#include "theme/CssBlendMode.h"
#include "theme/CssFilter.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringView>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

namespace muffin {
quint64 nextCssElementIdentity() {
  static std::atomic<quint64> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}
namespace {

Q_LOGGING_CATEGORY(cssDetailPerf, "muffin.perf.css", QtWarningMsg)

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
  static const QSet<QString> props = {QStringLiteral("color"),           QStringLiteral("font-family"),
                                      QStringLiteral("font-size"),       QStringLiteral("line-height"),
                                      QStringLiteral("text-align"),      QStringLiteral("font-weight"),
                                      QStringLiteral("font-style"),      QStringLiteral("fill"),
                                      QStringLiteral("fill-opacity"),    QStringLiteral("stroke"),
                                      QStringLiteral("stroke-opacity"),  QStringLiteral("stroke-width"),
                                      QStringLiteral("visibility"),      QStringLiteral("letter-spacing"),
                                      QStringLiteral("word-spacing"),    QStringLiteral("text-transform"),
                                      QStringLiteral("list-style-type"), QStringLiteral("list-style-position"),
                                      QStringLiteral("white-space"),     QStringLiteral("overflow-wrap"),
                                      QStringLiteral("word-break")};
  return props;
}

ParsedSelector parseSelector(const QString& selector);
bool selectorMatches(const ParsedSelector& selector, const CssElement& element, const CssElementState& state,
                     bool ignoreStates = false);

// Parse a CSS An+B micro-syntax (the argument of :nth-child / :nth-of-type).
// Accepts: `even`(2n), `odd`(2n+1), an integer N (the Nth, a=0), and `an+b`
// forms: `2n`, `2n+1`, `n`, `n+3`, `-n+3`, `+3`. Returns valid=false for anything
// else. `a=0` means "exactly the b-th" element.
struct NthExpr {
  bool valid = false;
  int a = 0;
  int b = 0;
};
NthExpr parseNth(const QString& arg) {
  NthExpr e;
  const QString s = arg.trimmed().toLower();
  if (s == QStringLiteral("even")) {
    return {true, 2, 0};
  }
  if (s == QStringLiteral("odd")) {
    return {true, 2, 1};
  }
  static const QRegularExpression re(QStringLiteral("^([+-]?\\d*)n\\s*([+-]\\s*\\d+)?$"));
  const QRegularExpressionMatch m = re.match(s);
  if (m.hasMatch()) {
    const QString aStr = m.captured(1);
    if (aStr.isEmpty() || aStr == QStringLiteral("+")) {
      e.a = 1;
    } else if (aStr == QStringLiteral("-")) {
      e.a = -1;
    } else {
      e.a = aStr.toInt();
    }
    QString bStr = m.captured(2);
    if (!bStr.isEmpty()) {
      e.b = bStr.replace(QStringLiteral(" "), QString()).toInt();
    }
    e.valid = true;
    return e;
  }
  bool ok = false;
  const int n = s.toInt(&ok);
  if (ok && n > 0) {
    return {true, 0, n};
  } // bare N → exactly the Nth
  return e;
}

// 1-based position p matches An+B iff some integer k≥0 satisfies p = a*k + b.
// For a==0 that is p==b. The `a*k+b==p` re-check guards against C++ truncation
// giving a spurious k.
bool nthPositionMatches(int a, int b, int p) {
  if (a == 0) {
    return p == b;
  }
  if (a * p < 0 && (p - b) % a != 0) {
    return false;
  } // quick reject on sign mismatch
  const int k = (p - b) / a;
  return k >= 0 && a * k + b == p;
}

SimpleSelector parseCompound(QString compound) {
  SimpleSelector out;
  compound = compound.trimmed();
  int i = 0;
  const int n = compound.size();
  if (i < n && (compound.at(i).isLetter() || compound.at(i) == QLatin1Char('*'))) {
    int j = i;
    if (compound.at(i) == QLatin1Char('*')) {
      ++j;
    } else {
      while (j < n && isIdentChar(compound.at(j))) {
        ++j;
      }
      out.tag = compound.mid(i, j - i).toLower();
    }
    i = j;
  }
  while (i < n) {
    const QChar c = compound.at(i);
    if (c == QLatin1Char('#')) {
      int j = ++i;
      while (j < n && isIdentChar(compound.at(j))) {
        ++j;
      }
      out.id = compound.mid(i, j - i);
      i = j;
    } else if (c == QLatin1Char('.')) {
      int j = ++i;
      while (j < n && isIdentChar(compound.at(j))) {
        ++j;
      }
      const QString cls = compound.mid(i, j - i);
      out.classes << cls;
      if (cls == QStringLiteral("md-focus")) {
        out.mdFocus = true;
      }
      if (isTyporaEditorOnlyClass(cls)) {
        out.editorOnly = true;
      }
      i = j;
    } else if (c == QLatin1Char(':')) {
      const bool element = (i + 1 < n && compound.at(i + 1) == QLatin1Char(':'));
      int j = element ? i + 2 : i + 1;
      const int nameStart = j;
      while (j < n && (compound.at(j).isLetterOrNumber() || compound.at(j) == QLatin1Char('-'))) {
        ++j;
      }
      const QString name = compound.mid(nameStart, j - nameStart).toLower();
      QString arg;
      if (j < n && compound.at(j) == QLatin1Char('(')) {
        int depth = 0;
        int end = -1;
        for (int k = j; k < n; ++k) {
          if (compound.at(k) == QLatin1Char('(')) {
            ++depth;
          } else if (compound.at(k) == QLatin1Char(')')) {
            --depth;
            if (depth == 0) {
              end = k;
              break;
            }
          }
        }
        if (end >= 0) {
          arg = compound.mid(j + 1, end - j - 1).trimmed();
          j = end + 1;
        } else {
          j = n;
        }
      }
      if (element || name == QStringLiteral("before") || name == QStringLiteral("after") ||
          name == QStringLiteral("selection") || name == QStringLiteral("marker")) {
        if (out.pseudoElement.isEmpty()) {
          out.pseudoElement = name;
        }
      } else if (name == QStringLiteral("hover")) {
        out.hover = true;
      } else if (name == QStringLiteral("focus")) {
        out.focus = true;
      } else if (name == QStringLiteral("active")) {
        out.active = true;
      } else if (name == QStringLiteral("visited")) {
        out.visited = true;
      } else if (name == QStringLiteral("first-child")) {
        out.firstChild = true;
      } else if (name == QStringLiteral("last-child")) {
        out.lastChild = true;
      } else if (name == QStringLiteral("only-child")) {
        out.onlyChild = true;
      } else if (name == QStringLiteral("first-of-type")) {
        out.firstOfType = true;
      } else if (name == QStringLiteral("nth-child") || name == QStringLiteral("nth-of-type")) {
        const NthExpr e = parseNth(arg);
        if (!e.valid) {
          out.unsupported = true;
        } else if (name == QStringLiteral("nth-child")) {
          out.nthChild = true;
          out.nthA = e.a;
          out.nthB = e.b;
        } else {
          out.nthOfType = true;
          out.nthA = e.a;
          out.nthB = e.b;
        }
      } else if (name == QLatin1String("is") || name == QLatin1String("where") || name == QLatin1String("not")) {
        std::vector<ParsedSelector> alternatives;
        for (const auto& argument : CssThemeParser::splitTopLevelCommas(arg)) {
          auto parsed = parseSelector(argument);
          const bool supported =
              parsed.valid && std::none_of(parsed.parts.begin(), parsed.parts.end(), [](const auto& part) {
                return part.simple.unsupported || !part.simple.pseudoElement.isEmpty();
              });
          if (supported)
            alternatives.push_back(std::move(parsed));
          else if (name == QLatin1String("not"))
            out.unsupported = true;
        }
        if (alternatives.empty())
          out.unsupported = true;
        if (name == QLatin1String("not"))
          out.exclusions.insert(out.exclusions.end(), alternatives.begin(), alternatives.end());
        else
          out.alternatives.push_back(std::move(alternatives));
      } else if (name == QStringLiteral("has")) {
        // :has(<simple>) where <simple> is [>] tag[.class] (.class), (.class), tag.
        // Full relative-selector :has (e.g. :has(> div .x)) is out of scope; a
        // compound argument marks the selector non-matching rather than risk a
        // partial/incorrect match.
        QString h = arg;
        bool direct = false;
        if (h.startsWith(QLatin1Char('>'))) {
          direct = true;
          h = h.mid(1).trimmed();
        }
        // Reject combinators inside the argument (descendant/child beyond the leading >).
        static const QRegularExpression simpleHas(
            QStringLiteral("^(?:[a-zA-Z_][-a-zA-Z0-9_]*|\\*)?(?:\\.[-a-zA-Z_][-a-zA-Z0-9_]*)?$"));
        if (!simpleHas.match(h).hasMatch()) {
          out.unsupported = true;
        } else {
          QString tagPart = h;
          QString clsPart;
          const int dot = h.indexOf(QLatin1Char('.'));
          if (dot >= 0) {
            clsPart = h.mid(dot + 1).trimmed().toLower();
            tagPart = h.left(dot).trimmed();
          }
          if (tagPart == QStringLiteral("*")) {
            tagPart.clear();
          }
          if (tagPart.isEmpty() && clsPart.isEmpty()) {
            out.unsupported = true;
          } else {
            out.hasPresent = true;
            out.hasDirect = direct;
            out.hasTag = tagPart.toLower();
            out.hasClass = clsPart;
          }
        }
      } else if (name == QStringLiteral("root")) {
        // :root matches the document root element (html). Model it as a tag selector for
        // "html" so :root element declarations reach the root element (the adapter exposes
        // the root with tag "html"). :root variables are still collected at parse time.
        if (out.tag.isEmpty()) {
          out.tag = QStringLiteral("html");
        }
      } else {
        // Unsupported structural/content pseudos (:empty, :last-of-type, :only-of-type,
        // :lang, …) cannot be evaluated against our model. Treating only the base tag
        // as a match would globalize targeted rules (e.g. p:has(img) centering every
        // paragraph), so make the selector non-matching.
        out.unsupported = true;
      }
      i = j;
    } else if (c == QLatin1Char('[')) {
      int close = i + 1;
      bool inString = false;
      QChar quote;
      for (; close < n; ++close) {
        const QChar current = compound.at(close);
        if (inString) {
          if (current == quote)
            inString = false;
          else if (current == QLatin1Char('\\') && close + 1 < n)
            ++close;
        } else if (current == QLatin1Char('"') || current == QLatin1Char('\'')) {
          inString = true;
          quote = current;
        } else if (current == QLatin1Char(']')) {
          break;
        }
      }
      if (close >= n) {
        out.unsupported = true;
        i = n;
        continue;
      }
      QString body = compound.mid(i + 1, close - i - 1).trimmed();
      CssAttributeSelector attr;
      static const QRegularExpression attrRe(
          QStringLiteral("^([_a-zA-Z][-_a-zA-Z0-9:.]*)(?:\\s*(~=|\\|=|\\^=|\\$=|\\*=|=)\\s*"
                         "(?:\"((?:\\\\.|[^\"])*)\"|'((?:\\\\.|[^'])*)'|([^\\s]+))\\s*([iIsS])?)?$"));
      const QRegularExpressionMatch match = attrRe.match(body);
      if (!match.hasMatch()) {
        out.unsupported = true;
      } else {
        attr.name = match.captured(1).toLower();
        const QString op = match.captured(2);
        if (op == QLatin1String("="))
          attr.op = CssAttributeSelector::Operator::Equals;
        else if (op == QLatin1String("~="))
          attr.op = CssAttributeSelector::Operator::IncludesWord;
        else if (op == QLatin1String("|="))
          attr.op = CssAttributeSelector::Operator::DashMatch;
        else if (op == QLatin1String("^="))
          attr.op = CssAttributeSelector::Operator::Prefix;
        else if (op == QLatin1String("$="))
          attr.op = CssAttributeSelector::Operator::Suffix;
        else if (op == QLatin1String("*="))
          attr.op = CssAttributeSelector::Operator::Contains;
        attr.value = !match.captured(3).isNull()   ? match.captured(3)
                     : !match.captured(4).isNull() ? match.captured(4)
                                                   : match.captured(5);
        attr.value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        attr.value.replace(QStringLiteral("\\'"), QStringLiteral("'"));
        attr.value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        attr.caseInsensitive = match.captured(6).compare(QLatin1String("i"), Qt::CaseInsensitive) == 0;
        out.attributes.push_back(std::move(attr));
      }
      i = close + 1;
    } else {
      ++i;
    }
  }
  return out;
}

// selectorRequiresExportContext / specificityOf / isIdentChar live in theme/CssSelectorUtils.h
// (shared with CssThemeMapper).
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
      if (c == quote) {
        inString = false;
      } else if (c == QLatin1Char('\\') && i + 1 < selector.size()) {
        cur += selector.at(++i);
      }
      continue;
    }
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
      inString = true;
      quote = c;
      cur += c;
      continue;
    }
    if (c == QLatin1Char('(')) {
      ++paren;
      cur += c;
      continue;
    }
    if (c == QLatin1Char(')')) {
      paren = qMax(0, paren - 1);
      cur += c;
      continue;
    }
    if (c == QLatin1Char('[')) {
      ++brk;
      cur += c;
      continue;
    }
    if (c == QLatin1Char(']')) {
      brk = qMax(0, brk - 1);
      cur += c;
      continue;
    }
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
  if (compounds.isEmpty()) {
    return parsed;
  }
  for (int i = 0; i < compounds.size(); ++i) {
    SelectorPart part;
    part.simple = parseCompound(compounds.at(i));
    part.combinator = i == 0 ? QChar() : relations.at(i);
    parsed.interactive = parsed.interactive || part.simple.hover || part.simple.focus || part.simple.active ||
                         part.simple.visited || part.simple.mdFocus;
    for (const auto& group : part.simple.alternatives)
      for (const auto& nested : group)
        parsed.interactive = parsed.interactive || nested.interactive;
    for (const auto& nested : part.simple.exclusions)
      parsed.interactive = parsed.interactive || nested.interactive;
    parsed.parts.push_back(part);
  }
  // Mirror the flat mapper: a rule whose rightmost compound carries a Typora editor-only
  // class (md-meta-block, ty-*, …) is editor chrome Muffin never renders — drop it at match
  // time so its hacks (e.g. pixyll's 2000px padding) never leak into computed styles.
  parsed.editorOnly = !parsed.parts.isEmpty() && parsed.parts.last().simple.editorOnly;
  parsed.valid = true;
  return parsed;
}

bool stateMatches(const SimpleSelector& simple, const CssElementState& state, const CssElement& element) {
  if (simple.unsupported) {
    return false;
  }
  if (simple.hover && !state.hover) {
    return false;
  }
  if (simple.focus && !state.focus) {
    return false;
  }
  if (simple.active && !state.active) {
    return false;
  }
  if (simple.visited && !state.visited) {
    return false;
  }
  if (simple.mdFocus && !state.mdFocus) {
    return false;
  }
  if (state.subject) {
    const auto* origin = element.originatingElement ? element.originatingElement : &element;
    if (simple.focus && origin != state.subject)
      return false;
    if (simple.hover || simple.active || simple.mdFocus) {
      auto* descendant = state.subject;
      while (descendant && descendant != origin)
        descendant = descendant->parent;
      if (!descendant)
        return false;
    }
  }
  return true;
}

const CssElement* previousSiblingOf(const CssElement& element) {
  return element.navigator ? element.navigator->previousSibling(element) : element.previousSibling;
}

const CssElement* nextSiblingOf(const CssElement& element) {
  return element.navigator ? element.navigator->nextSibling(element) : element.nextSibling;
}

int childIndexOf(const CssElement& element) {
  return element.navigator ? element.navigator->childIndex(element) : element.childIndex;
}

int typeIndexOf(const CssElement& element) {
  return element.navigator ? element.navigator->typeIndex(element) : element.typeIndex;
}

bool hasTag(const CssElement& element, const QString& tag, bool directChild) {
  if (element.navigator) {
    return element.navigator->hasTag(element, tag, directChild);
  }
  return (directChild ? element.hasChildTags : element.hasDescendantTags).contains(tag);
}

bool hasClass(const CssElement& element, const QString& className, bool directChild) {
  if (element.navigator) {
    return element.navigator->hasClass(element, className, directChild);
  }
  return (directChild ? element.hasChildClasses : element.hasDescendantClasses).contains(className);
}

bool simpleMatches(const SimpleSelector& simple, const CssElement& element, const CssElementState& state,
                   bool ignoreStates) {
  if (simple.unsupported || (!ignoreStates && !stateMatches(simple, state, element)))
    return false;
  const QString tag = element.tag.toLower();
  const QString id = element.id;
  const QString pseudo = element.pseudoElement.toLower();
  if (!simple.tag.isEmpty() && simple.tag != tag) {
    return false;
  }
  if (!simple.id.isEmpty() && simple.id != id) {
    return false;
  }
  if (!simple.pseudoElement.isEmpty() && simple.pseudoElement != pseudo) {
    return false;
  }
  if (simple.pseudoElement.isEmpty() && !pseudo.isEmpty()) {
    return false;
  }
  for (const QString& cls : simple.classes)
    if (!element.classes.contains(cls))
      return false;
  if (!simple.notTag.isEmpty() && simple.notTag == tag)
    return false;
  if (!simple.notId.isEmpty() && simple.notId == id)
    return false;
  for (const QString& cls : simple.notClasses)
    if (element.classes.contains(cls))
      return false;
  const auto& subject = element.originatingElement ? *element.originatingElement : element;
  for (const auto& group : simple.alternatives) {
    if (std::none_of(group.begin(), group.end(),
                     [&](const auto& selector) { return selectorMatches(selector, subject, state, ignoreStates); }))
      return false;
  }
  // Ignoring the state in :not(:hover) would incorrectly eliminate every
  // candidate. Dynamic exclusions must remain candidates until state matching.
  for (const auto& selector : simple.exclusions)
    if ((!ignoreStates || !selector.interactive) && selectorMatches(selector, subject, state, ignoreStates))
      return false;
  // Structural pseudo-classes query the live navigator when present. Prototype
  // elements use their explicitly populated fields and otherwise fail to match.
  if (simple.firstChild && !(element.navigator ? previousSiblingOf(element) == nullptr : element.childIndex == 0)) {
    return false;
  }
  if (simple.lastChild && !(element.navigator ? nextSiblingOf(element) == nullptr
                                              : element.childIndex >= 0 && element.nextSibling == nullptr)) {
    return false;
  }
  if (simple.onlyChild &&
      !(element.navigator ? previousSiblingOf(element) == nullptr && nextSiblingOf(element) == nullptr
                          : element.childIndex == 0 && element.nextSibling == nullptr)) {
    return false;
  }
  if (simple.firstOfType && typeIndexOf(element) != 0) {
    return false;
  }
  if (simple.nthChild) {
    const int index = childIndexOf(element);
    if (index < 0 || !nthPositionMatches(simple.nthA, simple.nthB, index + 1)) {
      return false;
    }
  }
  if (simple.nthOfType) {
    const int index = typeIndexOf(element);
    if (index < 0 || !nthPositionMatches(simple.nthA, simple.nthB, index + 1)) {
      return false;
    }
  }
  if (simple.hasPresent) {
    if (!simple.hasTag.isEmpty() && !hasTag(element, simple.hasTag, simple.hasDirect)) {
      return false;
    }
    if (!simple.hasClass.isEmpty() && !hasClass(element, simple.hasClass, simple.hasDirect)) {
      return false;
    }
  }
  QHash<QString, QString> attributes;
  if (!simple.attributes.isEmpty()) {
    for (auto it = element.attributes.constBegin(); it != element.attributes.constEnd(); ++it)
      attributes.insert(it.key().toLower(), it.value());
    if (!element.id.isEmpty())
      attributes.insert(QStringLiteral("id"), element.id);
    if (!element.classes.isEmpty())
      attributes.insert(QStringLiteral("class"), element.classes.join(QLatin1Char(' ')));
  }
  for (const CssAttributeSelector& selector : simple.attributes) {
    const auto found = attributes.constFind(selector.name);
    if (found == attributes.constEnd())
      return false;
    if (selector.op == CssAttributeSelector::Operator::Exists)
      continue;
    const Qt::CaseSensitivity cs = selector.caseInsensitive ? Qt::CaseInsensitive : Qt::CaseSensitive;
    const QString& actual = found.value();
    bool matched = false;
    switch (selector.op) {
    case CssAttributeSelector::Operator::Exists:
      matched = true;
      break;
    case CssAttributeSelector::Operator::Equals:
      matched = actual.compare(selector.value, cs) == 0;
      break;
    case CssAttributeSelector::Operator::IncludesWord:
      matched =
          actual.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).contains(selector.value, cs);
      break;
    case CssAttributeSelector::Operator::DashMatch:
      matched = actual.compare(selector.value, cs) == 0 || actual.startsWith(selector.value + QLatin1Char('-'), cs);
      break;
    case CssAttributeSelector::Operator::Prefix:
      matched = actual.startsWith(selector.value, cs);
      break;
    case CssAttributeSelector::Operator::Suffix:
      matched = actual.endsWith(selector.value, cs);
      break;
    case CssAttributeSelector::Operator::Contains:
      matched = actual.contains(selector.value, cs);
      break;
    }
    if (!matched)
      return false;
  }
  return true;
}

// Explicit-stack matcher. Previous-sibling alternatives are advanced lazily: a
// common `p ~ p` match inspects only the adjacent paragraph instead of first
// materializing every preceding sibling. kMaxMatcherSteps bounds pathological
// selectors/structures without using the C++ call stack.
bool selectorMatchesAt(const ParsedSelector& selector, int index, const CssElement* element,
                       const CssElementState& targetState, bool ignoreStates) {
  if (!element || index < 0) {
    return false;
  }
  const int last = selector.parts.size() - 1;
  enum class WorkKind { Match, PreviousSibling };
  struct WorkItem {
    int index;
    const CssElement* element;
    WorkKind kind;
  };
  std::vector<WorkItem> stack;
  stack.push_back({index, element, WorkKind::Match});
  constexpr int kMaxMatcherSteps = 100000;
  int steps = 0;
  while (!stack.empty()) {
    if (++steps > kMaxMatcherSteps) {
      return false;
    }
    const WorkItem work = stack.back();
    stack.pop_back();
    const int idx = work.index;
    const CssElement* el = work.element;
    if (!el || idx < 0) {
      continue;
    }
    if (work.kind == WorkKind::PreviousSibling) {
      if (const CssElement* previous = previousSiblingOf(*el)) {
        stack.push_back({idx, previous, WorkKind::PreviousSibling});
        stack.push_back({idx, previous, WorkKind::Match});
      }
      continue;
    }
    const CssElementState state = idx == last || targetState.subject ? targetState : CssElementState{};
    if (!simpleMatches(selector.parts.at(idx).simple, *el, state, ignoreStates)) {
      continue;
    }
    if (idx == 0) {
      return true;
    } // every part matched, left to right
    const QChar rel = selector.parts.at(idx).combinator;
    if (rel == QLatin1Char('>')) {
      stack.push_back({idx - 1, el->parent, WorkKind::Match});
    } else if (rel == QLatin1Char('+')) {
      // Adjacent sibling: the element immediately to the left.
      stack.push_back({idx - 1, previousSiblingOf(*el), WorkKind::Match});
    } else if (rel == QLatin1Char('~')) {
      // Negative sibling searches are common in theme cover-page rules. A
      // cached child-tag query can reject a missing heading level without
      // materializing/walking every preceding paragraph for every heading.
      const auto& left = selector.parts.at(idx - 1).simple;
      if (!left.tag.isEmpty() && el->parent && el->parent->navigator &&
          !el->parent->navigator->hasTag(*el->parent, left.tag, true))
        continue;
      stack.push_back({idx - 1, el, WorkKind::PreviousSibling});
    } else {
      // Descendant combinator (' '): any ancestor.
      for (const CssElement* p = el->parent; p; p = p->parent) {
        stack.push_back({idx - 1, p, WorkKind::Match});
      }
    }
  }
  return false;
}

bool selectorMatches(const ParsedSelector& selector, const CssElement& element, const CssElementState& state,
                     bool ignoreStates) {
  if (!selector.valid || selector.exportOnly || selector.editorOnly || selector.parts.isEmpty()) {
    return false;
  }
  return selectorMatchesAt(selector, selector.parts.size() - 1, &element, state, ignoreStates);
}

quint8 selectorStateMask(const ParsedSelector& selector) {
  quint8 mask = 0;
  for (const auto& part : selector.parts) {
    const auto& simple = part.simple;
    mask |= (simple.hover ? 1 : 0) | ((simple.focus || simple.mdFocus) ? 2 : 0) | (simple.active ? 4 : 0);
    for (const auto& group : simple.alternatives)
      for (const auto& nested : group)
        mask |= selectorStateMask(nested);
    for (const auto& nested : simple.exclusions)
      mask |= selectorStateMask(nested);
  }
  return mask;
}

// applyStyleForElement / parentStyleFor are now CssComputedStyleEngine members
// (they read the cached parse + sheet_). Their definitions sit below, next to
// the constructor.

} // namespace

QString CssComputedStyle::rawValue(const QString& property) const {
  const QString key = property.startsWith(QStringLiteral("--")) ? property : property.toLower();
  if (key.startsWith(QStringLiteral("--"))) {
    return customProperties_.value(key);
  }
  return properties_.value(key);
}

QString CssComputedStyle::resolvedValue(const QString& property) const {
  return CssThemeParser::substituteVars(rawValue(property), customProperties_).value_or(QString());
}

bool CssComputedStyle::hasProperty(const QString& property) const {
  const QString key = property.startsWith(QStringLiteral("--")) ? property : property.toLower();
  return key.startsWith(QStringLiteral("--")) ? customProperties_.contains(key) : properties_.contains(key);
}

CssComputedStyle CssComputedStyle::withContainingWidth(qreal widthPx) const {
  CssComputedStyle copy = *this;
  copy.containingWidthPx = widthPx;
  return copy;
}

CssLengthPercentage CssComputedStyle::length(const QString& property) const {
  const auto computed = computedLengths_.constFind(property.toLower());
  if (computed != computedLengths_.cend())
    return computed.value();
  CssLengthContext context;
  context.emPx = fontSizePx * textScale;
  context.remPx = rootFontSizePx * textScale;
  context.viewportPx = viewportPx;
  return parseCssLengthPercentage(QStringView(resolvedValue(property)), context);
}

CssComputedStyleEngine::CssComputedStyleEngine(const CssThemeSheet& sheet, CssEnvironment environment)
    : CssComputedStyleEngine(sheet, environment, {}) {
}

CssComputedStyleEngine::CssComputedStyleEngine(const CssThemeSheet& sheet, CssEnvironment environment,
                                               std::shared_ptr<const QHash<QString, ParsedSelector>> selectorTemplates)
    : sourceSheet_(std::make_shared<const CssThemeSheet>(sheet)), sheet_(sheet.evaluated(environment)),
      environment_(environment) {
  diag::ScopedPerfProbe perf("css.compileSelectors", cssDetailPerf());
  // Pre-parse every selector once. The match path (applyStyleForElement) reads
  // parsedSelectors_ / ruleSelectorRange_ instead of re-running parseSelector
  // (regex + char walk) on every node × every ancestor level × every rule.
  QHash<QString, ParsedSelector> templates = selectorTemplates ? *selectorTemplates : QHash<QString, ParsedSelector>{};
  const auto& rules = sheet_.rules();
  const auto usesViewport = [](const auto& declarations) {
    return std::any_of(declarations.begin(), declarations.end(),
                       [](const auto& declaration) { return CssThemeParser::usesViewportUnits(declaration.value); });
  };
  for (auto it = sheet_.variables().cbegin(); it != sheet_.variables().cend(); ++it)
    viewportUnits_ = viewportUnits_ || CssThemeParser::usesViewportUnits(it.value());
  for (const auto& animation : sheet_.keyframes())
    for (const auto& stop : animation.stops)
      viewportUnits_ = viewportUnits_ || usesViewport(stop.declarations);
  qsizetype totalSelectors = 0;
  for (const CssRule& rule : rules) {
    totalSelectors += rule.selectors.size();
  }
  parsedSelectors_.reserve(static_cast<std::size_t>(totalSelectors));
  ruleSelectorRange_.reserve(rules.size());
  const QSet<QString> blockTags{"html",  "body",  "content",    "p",  "h1", "h2",    "h3",  "h4",
                                "h5",    "h6",    "blockquote", "ul", "ol", "li",    "pre", "table",
                                "thead", "tbody", "tr",         "td", "th", "figure"};
  const auto counterSubject = [&](const ParsedSelector& selector) {
    const auto& subject = selector.parts.back().simple;
    CssCounterSubject candidate{subject.tag == "*" ? QString() : subject.tag, subject.id, subject.classes, {}, {}};
    const bool siblings = std::any_of(selector.parts.begin(), selector.parts.end(), [](const auto& part) {
      return part.combinator == '+' || part.combinator == '~';
    });
    if (!siblings)
      for (qsizetype i = 0; i + 1 < selector.parts.size(); ++i) {
        const auto& ancestor = selector.parts[i].simple;
        candidate.ancestorClasses += ancestor.classes;
        if (blockTags.contains(ancestor.tag))
          candidate.ancestorBlockTags.append(ancestor.tag);
      }
    return candidate;
  };
  for (const CssRule& rule : rules) {
    const int start = static_cast<int>(parsedSelectors_.size());
    for (const QString& selector : rule.selectors) {
      auto found = templates.constFind(selector);
      if (found == templates.cend()) {
        auto parsed = parseSelector(selector);
        parsed.selectorText = selector;
        found = templates.insert(selector, std::move(parsed));
      }
      ParsedSelector ps = *found;
      const bool supported = std::none_of(ps.parts.cbegin(), ps.parts.cend(),
                                          [](const SelectorPart& part) { return part.simple.unsupported; });
      if (!rule.darkScope && ps.valid && supported && !ps.exportOnly && !ps.editorOnly) {
        // Export-only vh/vw declarations cannot affect the editor. In
        // particular, Typora export page heights must not invalidate every
        // paragraph and animation each time the native window changes size.
        viewportUnits_ = viewportUnits_ || usesViewport(rule.declarations);
        const auto& subject = ps.parts.back().simple;
        const bool counterCandidate =
            std::any_of(rule.declarations.begin(), rule.declarations.end(), [](const auto& decl) {
              return decl.property.startsWith("counter-") ||
                     (decl.property == "content" &&
                      (decl.value.contains("counter(", Qt::CaseInsensitive) ||
                       decl.value.contains("counters(", Qt::CaseInsensitive) || decl.value.contains("var(")));
            });
        if (counterCandidate) {
          selectorFeatures_.counterSubjects.append(counterSubject(ps));
        }
        if (subject.pseudoElement == QLatin1String("before") || subject.pseudoElement == QLatin1String("after")) {
          const auto key = qMakePair(subject.tag == QLatin1String("*") ? QString() : subject.tag, subject.id);
          auto& candidates = selectorFeatures_.generatedPseudoSubjects[subject.pseudoElement];
          if (!candidates.contains(key))
            candidates.push_back(key);
        }
        bool interactive = ps.interactive;
        std::function<void(const ParsedSelector&)> features = [&](const ParsedSelector& selector) {
          interactive = interactive || selector.interactive;
          for (const SelectorPart& part : selector.parts) {
            const SimpleSelector& simple = part.simple;
            selectorFeatures_.hasStructuralRules =
                selectorFeatures_.hasStructuralRules || simple.firstChild || simple.lastChild || simple.onlyChild ||
                simple.firstOfType || simple.nthChild || simple.nthOfType || simple.hasPresent ||
                part.combinator == QLatin1Char('+') || part.combinator == QLatin1Char('~');
            selectorFeatures_.needsTypeIndex =
                selectorFeatures_.needsTypeIndex || simple.firstOfType || simple.nthOfType;
            for (const auto& group : simple.alternatives)
              for (const auto& nested : group)
                features(nested);
            for (const auto& nested : simple.exclusions)
              features(nested);
          }
        };
        features(ps);
        const bool contextual = std::any_of(ps.parts.begin(), ps.parts.end(), [](const auto& part) {
          const auto& simple = part.simple;
          return simple.hasPresent || simple.firstChild || simple.lastChild || simple.onlyChild || simple.firstOfType ||
                 simple.nthChild || simple.nthOfType || part.combinator == '+' || part.combinator == '~' ||
                 !simple.alternatives.empty() || !simple.exclusions.empty();
        });
        if (contextual && std::any_of(rule.declarations.begin(), rule.declarations.end(), [](const auto& decl) {
              return decl.property.startsWith("counter-") || decl.property.startsWith("--") ||
                     decl.property == "content" || decl.property == "display";
            })) {
          selectorFeatures_.counterContextSensitive = true;
          selectorFeatures_.counterContextSubjects.append(counterSubject(ps));
        }
        if (interactive)
          selectorFeatures_.interactiveSubjects.insert((subject.tag == QLatin1String("*") ? QString() : subject.tag) +
                                                       '/' + subject.pseudoElement);
      }
      parsedSelectors_.push_back(std::move(ps));
    }
    ruleSelectorRange_.emplace_back(start, static_cast<int>(parsedSelectors_.size()));
  }
  selectorTemplates_ = std::make_shared<const QHash<QString, ParsedSelector>>(std::move(templates));
}

std::shared_ptr<CssComputedStyleEngine> CssComputedStyleEngine::withEnvironment(CssEnvironment environment) const {
  if (!sameActiveRules(environment)) {
    auto result = std::shared_ptr<CssComputedStyleEngine>(
        new CssComputedStyleEngine(*sourceSheet_, environment, selectorTemplates_));
    result->generation_ = generation_ + 1;
    return result;
  }
  auto result = std::make_shared<CssComputedStyleEngine>(*this);
  result->environment_ = environment;
  // Cascade matches contain addresses into sheet_; a copied sheet has its own
  // declaration storage. Only selector trees and candidate indices are reused.
  result->clearCache();
  return result;
}

namespace {
std::vector<CssDeclaration> expandDeclaration(const CssDeclaration& decl, const QHash<QString, QString>& vars) {
  if (decl.property.startsWith("-webkit-mask")) {
    auto canonical = decl;
    canonical.property.remove(0, 8);
    return expandDeclaration(canonical, vars);
  }
  std::vector<CssDeclaration> result{decl};
  const QString property = decl.property, value = CssThemeParser::resolveVars(decl.value, vars).trimmed();
  const auto add = [&](const QString& key, const QString& v) { result.push_back({key, v, decl.important}); };
  const QStringList sides{QStringLiteral("top"), QStringLiteral("right"), QStringLiteral("bottom"),
                          QStringLiteral("left")};
  const bool wide = value == QStringLiteral("inherit") || value == QStringLiteral("initial") ||
                    value == QStringLiteral("unset") || value == QLatin1String("revert") ||
                    value == QLatin1String("revert-layer");
  if (property == "mask") {
    const QStringList fields{"image", "position", "size", "repeat", "origin", "clip", "mode", "composite"};
    QVector<QStringList> columns(fields.size());
    for (const auto& layer : CssThemeParser::splitTopLevelCommas(value)) {
      QStringList values{"none", "0% 0%", "auto", "repeat", "border-box", "border-box", "match-source", "add"};
      QString separated;
      int depth = 0;
      for (auto ch : layer) {
        if (ch == '(')
          ++depth;
        if (ch == ')')
          --depth;
        separated += ch == '/' && depth == 0 ? QString(" / ") : QString(ch);
      }
      QStringList position, size, repeats, boxes;
      bool sizing = false, imageSeen = false, modeSeen = false, compositeSeen = false;
      for (const auto& token : splitTopLevelSpaces(separated)) {
        const auto lower = token.toLower();
        if (token == "/") {
          if (sizing || position.isEmpty())
            return {};
          sizing = true;
          continue;
        }
        if (token.contains("gradient(", Qt::CaseInsensitive) || token.startsWith("url(", Qt::CaseInsensitive) ||
            lower == "none") {
          if (imageSeen)
            return {};
          imageSeen = true;
          values[0] = token;
        } else if (QStringList{"repeat", "no-repeat", "repeat-x", "repeat-y", "space", "round"}.contains(lower))
          repeats << lower;
        else if (QStringList{"border-box", "padding-box", "content-box"}.contains(lower))
          boxes << lower;
        else if (QStringList{"alpha", "luminance", "match-source"}.contains(lower)) {
          if (modeSeen)
            return {};
          modeSeen = true;
          values[6] = lower;
        } else if (QStringList{"add", "subtract", "intersect", "exclude"}.contains(lower)) {
          if (compositeSeen)
            return {};
          compositeSeen = true;
          values[7] = lower;
        } else
          (sizing ? size : position) << token;
      }
      if (boxes.size() > 2 || (sizing && size.isEmpty()))
        return {};
      if (!position.isEmpty())
        values[1] = position.join(' ');
      if (!size.isEmpty())
        values[2] = size.join(' ');
      if (!repeats.isEmpty())
        values[3] = repeats.join(' ');
      if (!boxes.isEmpty()) {
        values[4] = boxes[0];
        values[5] = boxes.size() > 1 ? boxes[1] : boxes[0];
      }
      for (qsizetype i = 0; i < fields.size(); ++i)
        columns[i] << values[i];
    }
    for (qsizetype i = 0; i < fields.size(); ++i)
      add("mask-" + fields[i], wide ? value : columns[i].join(','));
  } else if (property == "transition" || property == "animation") {
    const bool animation = property == "animation";
    const QStringList fields =
        animation ? QStringList{"name",      "duration",  "timing-function", "delay", "iteration-count",
                                "direction", "fill-mode", "play-state"}
                  : QStringList{"property", "duration", "timing-function", "delay"};
    if (wide) {
      for (const auto& field : fields)
        add(property + '-' + field, value);
      return result;
    }
    QVector<QStringList> columns(fields.size());
    for (const auto& layer : CssThemeParser::splitTopLevelCommas(value)) {
      QStringList values = animation ? QStringList{"none", "0s", "ease", "0s", "1", "normal", "none", "running"}
                                     : QStringList{"all", "0s", "ease", "0s"};
      int times = 0;
      for (const auto& token : splitTopLevelSpaces(layer)) {
        const auto lower = token.toLower();
        if (lower.endsWith('s')) {
          auto number = lower;
          number.chop(lower.endsWith("ms") ? 2 : 1);
          bool ok = false;
          number.toDouble(&ok);
          if (ok) {
            if (times >= 2)
              return {};
            values[times++ == 0 ? 1 : 3] = token;
            continue;
          }
        }
        if (QStringList{"linear", "ease", "ease-in", "ease-out", "ease-in-out", "step-start", "step-end"}.contains(
                lower) ||
            lower.startsWith("cubic-bezier(") || lower.startsWith("steps("))
          values[2] = token;
        else if (animation && QStringList{"normal", "reverse", "alternate", "alternate-reverse"}.contains(lower))
          values[5] = token;
        else if (animation && QStringList{"forwards", "backwards", "both"}.contains(lower))
          values[6] = token;
        else if (animation && (lower == "running" || lower == "paused"))
          values[7] = token;
        else {
          bool numeric = false;
          token.toDouble(&numeric);
          if (animation && (numeric || lower == "infinite"))
            values[4] = token;
          else
            values[0] = token;
        }
      }
      for (int i = 0; i < fields.size(); ++i)
        columns[i].append(values[i]);
    }
    for (int i = 0; i < fields.size(); ++i)
      add(property + '-' + fields[i], columns[i].join(','));
    return result;
  }
  if (property == "grid-column" || property == "grid-row" || property == "grid-area") {
    const QStringList fields =
        property == "grid-area" ? QStringList{"grid-row-start", "grid-column-start", "grid-row-end", "grid-column-end"}
                                : QStringList{property + "-start", property + "-end"};
    if (wide) {
      for (const auto& field : fields)
        add(field, value);
    } else {
      const auto parts = value.split('/');
      if (parts.empty() || parts.size() > fields.size())
        return {};
      QStringList expanded;
      for (int i = 0; i < fields.size(); ++i) {
        if (i < parts.size())
          expanded.push_back(parts[i].trimmed());
        else {
          const auto fallback = expanded[property == "grid-area" && i == 3 ? 1 : 0];
          const auto line = parseCssGridLine(fallback);
          expanded.push_back(line && !line->name.isEmpty() && !line->span && !line->number ? fallback
                                                                                           : QStringLiteral("auto"));
        }
        add(fields[i], expanded.back());
      }
    }
    return result;
  }
  if (property == "place-items" || property == "place-self" || property == "place-content") {
    const auto suffix = property.mid(6);
    if (wide) {
      add("align-" + suffix, value);
      add("justify-" + suffix, value);
    } else {
      const auto parts = splitTopLevelSpaces(value);
      if (parts.empty() || parts.size() > 2)
        return {};
      add("align-" + suffix, parts[0]);
      add("justify-" + suffix, parts.size() == 1 ? parts[0] : parts[1]);
    }
    return result;
  }
  if (property == "flex" || property == "flex-flow" || property == "gap" || property == "overflow") {
    const auto keywordValue = value.toLower();
    if (wide) {
      const QStringList fields = property == "flex"        ? QStringList{"flex-grow", "flex-shrink", "flex-basis"}
                                 : property == "flex-flow" ? QStringList{"flex-direction", "flex-wrap"}
                                 : property == "gap"       ? QStringList{"row-gap", "column-gap"}
                                                           : QStringList{"overflow-x", "overflow-y"};
      for (const auto& field : fields)
        add(field, value);
    } else if (property == "flex") {
      QString grow = "1", shrink = "1", basis = "0%";
      const auto parts = splitTopLevelSpaces(value);
      if (keywordValue == "none") {
        grow = "0";
        shrink = "0";
        basis = "auto";
      } else if (keywordValue == "auto")
        basis = "auto";
      else if (keywordValue == "initial") {
        grow = "0";
        basis = "auto";
      } else {
        int numbers = 0;
        bool basisSeen = false;
        for (const auto& part : parts) {
          bool numeric = false;
          part.toDouble(&numeric);
          if (numeric && numbers < 2) {
            (numbers++ == 0 ? grow : shrink) = part;
          } else if (!basisSeen) {
            basis = part;
            basisSeen = true;
          } else
            return {};
        }
      }
      add("flex-grow", grow);
      add("flex-shrink", shrink);
      add("flex-basis", basis);
    } else if (property == "flex-flow") {
      QString direction = "row", wrap = "nowrap";
      bool directionSeen = false, wrapSeen = false;
      for (const auto& part : splitTopLevelSpaces(keywordValue)) {
        if (QStringList{"row", "row-reverse", "column", "column-reverse"}.contains(part) && !directionSeen) {
          direction = part;
          directionSeen = true;
        } else if (QStringList{"nowrap", "wrap", "wrap-reverse"}.contains(part) && !wrapSeen) {
          wrap = part;
          wrapSeen = true;
        } else
          return {};
      }
      add("flex-direction", direction);
      add("flex-wrap", wrap);
    } else {
      const auto parts = splitTopLevelSpaces(value);
      if (parts.empty() || parts.size() > 2)
        return {};
      add(property == "gap" ? "row-gap" : "overflow-x", parts[0]);
      add(property == "gap" ? "column-gap" : "overflow-y", parts.size() == 2 ? parts[1] : parts[0]);
    }
    return result;
  }
  if (wide && (property == QLatin1String("font") || property == QLatin1String("background") ||
               property == QLatin1String("list-style") || property == QLatin1String("text-decoration"))) {
    const QStringList fields =
        property == QLatin1String("font") ? QStringList{"font-size",   "font-family",  "line-height", "font-style",
                                                        "font-weight", "font-variant", "font-stretch"}
        : property == QLatin1String("background")
            ? QStringList{"background-color",      "background-image",      "background-position",
                          "background-position-x", "background-position-y", "background-size",
                          "background-repeat",     "background-origin",     "background-clip",
                          "background-attachment"}
        : property == QLatin1String("list-style")
            ? QStringList{"list-style-type", "list-style-position", "list-style-image"}
            : QStringList{"text-decoration-line", "text-decoration-style", "text-decoration-color",
                          "text-decoration-thickness"};
    for (const auto& field : fields)
      add(field, value);
    return result;
  }
  if (property == QStringLiteral("margin") || property == QStringLiteral("padding") ||
      property == QStringLiteral("border-width") || property == QStringLiteral("border-style") ||
      property == QStringLiteral("border-color")) {
    const auto parts = splitTopLevelSpaces(value);
    if (parts.isEmpty() || parts.size() > 4)
      return result;
    const QString values[] = {parts[0], parts.size() > 1 ? parts[1] : parts[0], parts.size() > 2 ? parts[2] : parts[0],
                              parts.size() > 3 ? parts[3] : (parts.size() > 1 ? parts[1] : parts[0])};
    for (int i = 0; i < 4; ++i)
      add(property.startsWith(QStringLiteral("border-")) ? QStringLiteral("border-") + sides[i] + property.mid(6)
                                                         : property + QLatin1Char('-') + sides[i],
          values[i]);
  } else if (property == QStringLiteral("border") ||
             (property.startsWith(QStringLiteral("border-")) && sides.contains(property.mid(7)))) {
    QString width = QStringLiteral("medium"), style = QStringLiteral("none"), color = QStringLiteral("currentColor");
    if (wide)
      width = style = color = value;
    else {
      bool widthSeen = false, styleSeen = false, colorSeen = false;
      for (const QString& part : splitTopLevelSpaces(value)) {
        static const QSet<QString> styles{"none",   "hidden", "solid", "dotted", "dashed",
                                          "double", "groove", "ridge", "inset",  "outset"};
        const auto keyword = part.toLower();
        const auto length = parseCssLengthPercentage(QStringView(part), {});
        if (styles.contains(keyword)) {
          if (styleSeen)
            return {};
          styleSeen = true;
          style = keyword;
        } else if (keyword == QLatin1String("thin") || keyword == QLatin1String("medium") ||
                   keyword == QLatin1String("thick") ||
                   (length.status == CssLengthStatus::Valid && !length.hasPercentage)) {
          if (widthSeen)
            return {};
          widthSeen = true;
          width = part;
        } else if (isCssColorValue(part)) {
          if (colorSeen)
            return {};
          colorSeen = true;
          color = part;
        } else
          return {};
      }
    }
    for (const QString& side : sides) {
      if (property != QStringLiteral("border") && property != QStringLiteral("border-") + side)
        continue;
      add(QStringLiteral("border-") + side + QStringLiteral("-width"), width);
      add(QStringLiteral("border-") + side + QStringLiteral("-style"), style);
      add(QStringLiteral("border-") + side + QStringLiteral("-color"), color);
    }
  } else if (property == QStringLiteral("font")) {
    static const QRegularExpression re(
        QStringLiteral(R"(^(.+?\s+)?([\d.]+(?:px|pt|em|rem|%))(?:\s*/\s*([^\s]+))?\s+(.+)$)"));
    const auto m = re.match(value);
    if (!m.hasMatch())
      return {};
    if (m.hasMatch()) {
      add(QStringLiteral("font-variant"), QStringLiteral("normal"));
      add(QStringLiteral("font-stretch"), QStringLiteral("normal"));
      add(QStringLiteral("font-size"), m.captured(2));
      add(QStringLiteral("font-family"), m.captured(4));
      add(QStringLiteral("line-height"), m.captured(3).isEmpty() ? QStringLiteral("normal") : m.captured(3));
      const auto prefix = splitTopLevelSpaces(m.captured(1));
      add(QStringLiteral("font-style"),
          prefix.contains(QStringLiteral("italic")) ? QStringLiteral("italic") : QStringLiteral("normal"));
      QString weight = QStringLiteral("normal");
      for (const QString& token : prefix)
        if (token == QStringLiteral("bold") || token.toInt() > 0)
          weight = token;
      add(QStringLiteral("font-weight"), weight);
    }
  } else if (property == QStringLiteral("text-decoration")) {
    QStringList lines;
    QString style = QStringLiteral("solid"), color = QStringLiteral("currentColor"), thickness = QStringLiteral("auto");
    bool styleSeen = false, colorSeen = false, thicknessSeen = false;
    for (const auto& part : splitTopLevelSpaces(value)) {
      const auto keyword = part.toLower();
      if (QStringList{"none", "underline", "overline", "line-through"}.contains(keyword)) {
        if (lines.contains(keyword))
          return {};
        lines << keyword;
      } else if (QStringList{"solid", "double", "dotted", "dashed", "wavy"}.contains(keyword)) {
        if (styleSeen)
          return {};
        styleSeen = true;
        style = part;
      } else if (isCssColorValue(part)) {
        if (colorSeen)
          return {};
        colorSeen = true;
        color = part;
      } else if (keyword == "auto" || keyword == "from-font" ||
                 parseCssLengthPercentage(QStringView(part), {}).status == CssLengthStatus::Valid) {
        if (thicknessSeen)
          return {};
        thicknessSeen = true;
        thickness = part;
      } else
        return {};
    }
    if (lines.size() > 1 && lines.contains(QStringLiteral("none")))
      return {};
    add(QStringLiteral("text-decoration-line"),
        lines.isEmpty() ? QStringLiteral("none") : lines.join(QLatin1Char(' ')));
    add(QStringLiteral("text-decoration-style"), style);
    add(QStringLiteral("text-decoration-color"), color);
    add(QStringLiteral("text-decoration-thickness"), thickness);
  } else if (property == QStringLiteral("background-position")) {
    // Store axes as well, so a later shorthand resets an earlier longhand.
    QStringList x, y;
    if (wide) {
      // CSS-wide values apply to both axes. Treating `unset` as a one-token
      // position would manufacture a `center` Y axis, including when an
      // unresolved var() invalidates the whole declaration at computed time.
      x << value;
      y << value;
    } else
      for (const auto& layer : CssThemeParser::splitTopLevelCommas(value)) {
        auto parts = splitTopLevelSpaces(layer);
        if (parts.size() <= 2 && !parts.isEmpty()) {
          if (parts.size() == 1)
            parts << "center";
          if (parts[0] == "top" || parts[0] == "bottom" || parts[1] == "left" || parts[1] == "right")
            std::swap(parts[0], parts[1]);
          x << parts[0];
          y << parts[1];
        } else {
          // The mapper resolves the edge-offset grammar from the original value.
          x << "initial";
          y << "initial";
        }
      }
    add("background-position-x", x.join(','));
    add("background-position-y", y.join(','));
  } else if (property == QStringLiteral("background")) {
    add("background-color", "transparent");
    const QStringList fields{"image",  "position", "position-x", "position-y", "size",
                             "repeat", "origin",   "clip",       "attachment"};
    QVector<QStringList> columns(fields.size());
    for (const auto& layer : CssThemeParser::splitTopLevelCommas(value)) {
      QStringList values{"none", "0% 0%", "0%", "0%", "auto", "repeat", "padding-box", "border-box", "scroll"};
      QString separated;
      int depth = 0;
      for (const auto ch : layer) {
        if (ch == '(')
          ++depth;
        if (ch == ')')
          --depth;
        separated += ch == '/' && depth == 0 ? QStringLiteral(" / ") : QString(ch);
      }
      QStringList position, size, repeats, boxes;
      bool sizing = false;
      for (const QString& part : splitTopLevelSpaces(separated)) {
        const auto lower = part.toLower();
        if (part == "/") {
          sizing = true;
          continue;
        }
        if (isCssColorValue(part))
          add("background-color", part);
        else if (part.contains("gradient(", Qt::CaseInsensitive) || part.startsWith("url(", Qt::CaseInsensitive))
          values[0] = part;
        else if (QStringList{"repeat", "no-repeat", "repeat-x", "repeat-y", "space", "round"}.contains(lower))
          repeats << lower;
        else if (QStringList{"border-box", "padding-box", "content-box"}.contains(lower))
          boxes << lower;
        else if (QStringList{"scroll", "fixed", "local"}.contains(lower))
          values[8] = lower;
        else if (lower != "none")
          (sizing ? size : position) << part;
      }
      if (!position.isEmpty()) {
        values[1] = position.join(' ');
        if (position.size() == 1)
          position << "center";
        if (position.size() == 2) {
          if (position[0] == "top" || position[0] == "bottom" || position[1] == "left" || position[1] == "right")
            std::swap(position[0], position[1]);
          values[2] = position[0];
          values[3] = position[1];
        } else {
          values[2] = "initial";
          values[3] = "initial";
        }
      }
      if (!size.isEmpty())
        values[4] = size.join(' ');
      if (!repeats.isEmpty())
        values[5] = repeats.join(' ');
      if (!boxes.isEmpty()) {
        values[6] = boxes[0];
        values[7] = boxes.size() > 1 ? boxes[1] : boxes[0];
      }
      for (qsizetype i = 0; i < fields.size(); ++i)
        columns[i] << values[i];
    }
    for (qsizetype i = 0; i < fields.size(); ++i)
      add("background-" + fields[i], columns[i].join(','));
  }
  return result;
}

bool cssWide(const QString& value) {
  return value == QLatin1String("inherit") || value == QLatin1String("initial") || value == QLatin1String("unset") ||
         value == QLatin1String("revert") || value == QLatin1String("revert-layer");
}

bool validDeclarationValue(const QString& property, const QString& raw, const QHash<QString, QString>&) {
  const QString value = raw.trimmed(), lower = value.toLower();
  if (value.isEmpty())
    return false;
  if (cssWide(lower))
    return true;
  if (property == "counter-reset" || property == "counter-set" || property == "counter-increment")
    return parseCounterOperations(value, property == "counter-increment" ? 1 : 0).has_value();
  if (property == "filter" || property == "backdrop-filter")
    return parseCssFilters(value).has_value();
  if (property == "mask" || property.startsWith("mask-") || property.startsWith("-webkit-mask"))
    return CssComputedStyleEngine::supportsDeclaration(property, value);
  if (property == "background-repeat" || property == "background-blend-mode")
    return CssComputedStyleEngine::supportsDeclaration(property, value);
  if (property == "content" && lower.contains("attr("))
    return !parseContentTokens(value).empty();
  const auto oneOf = [&](std::initializer_list<const char*> values) {
    return std::any_of(values.begin(), values.end(), [&](const char* v) { return lower == QLatin1String(v); });
  };
  if (property == "aspect-ratio")
    return parseCssAspectRatio(value).has_value();
  if (property == "font-weight") {
    if (oneOf({"normal", "bold", "bolder", "lighter"}))
      return true;
    bool ok = false;
    const auto weight = value.toDouble(&ok);
    return ok && std::isfinite(weight) && weight >= 1 && weight <= 1000;
  }
  if (property == "font-style")
    return oneOf({"normal", "italic", "oblique"});
  if (property == "display")
    return oneOf({"none", "block", "inline", "inline-block", "flex", "inline-flex", "grid", "inline-grid", "flow-root",
                  "table", "table-row-group", "table-header-group", "table-footer-group", "table-row", "table-cell",
                  "list-item"});
  if (property == "grid-template-columns" || property == "grid-template-rows" || property == "grid-auto-columns" ||
      property == "grid-auto-rows")
    return (property.startsWith("grid-template") || !lower.contains("repeat(")) &&
           parseCssGridTracks(raw, {}, property.startsWith("grid-template")).has_value();
  if (property == "grid-template-areas")
    return parseCssGridAreas(raw).has_value();
  if (property == "grid-column-start" || property == "grid-column-end" || property == "grid-row-start" ||
      property == "grid-row-end")
    return parseCssGridLine(raw).has_value();
  if (property == "grid-auto-flow") {
    auto parts = splitTopLevelSpaces(lower);
    parts.removeDuplicates();
    return parts.size() <= 2 && !parts.empty() &&
           std::all_of(parts.begin(), parts.end(),
                       [](const QString& part) { return part == "row" || part == "column" || part == "dense"; }) &&
           !(parts.contains("row") && parts.contains("column")) && parts.size() == splitTopLevelSpaces(lower).size();
  }
  if (property == "justify-items" || property == "justify-self")
    return oneOf({"normal", "stretch", "start", "end", "flex-start", "flex-end", "center"}) ||
           (property == "justify-self" && lower == "auto");
  if (property == "flex-direction")
    return oneOf({"row", "row-reverse", "column", "column-reverse"});
  if (property == "flex-wrap")
    return oneOf({"nowrap", "wrap", "wrap-reverse"});
  if (property == "flex-grow" || property == "flex-shrink") {
    bool ok = false;
    const double n = lower.toDouble(&ok);
    return ok && std::isfinite(n) && n >= 0;
  }
  if (property == "order") {
    bool ok = false;
    lower.toInt(&ok);
    return ok;
  }
  if (property == "justify-content")
    return oneOf({"normal", "start", "end", "flex-start", "flex-end", "center", "space-between", "space-around",
                  "space-evenly"});
  if (property == "align-items" || property == "align-self")
    return oneOf({"normal", "auto", "stretch", "start", "end", "flex-start", "flex-end", "center", "baseline",
                  "first baseline", "last baseline"});
  if (property == "align-content")
    return oneOf({"normal", "stretch", "start", "end", "flex-start", "flex-end", "center", "space-between",
                  "space-around", "space-evenly"});
  if (property == "overflow-x" || property == "overflow-y")
    return oneOf({"visible", "hidden", "clip", "auto", "scroll"});
  if (property == "overflow-wrap")
    return oneOf({"normal", "break-word", "anywhere"});
  if (property == "word-break")
    return oneOf({"normal", "break-all", "keep-all", "break-word"});
  if (property == "flex-basis" || property == "row-gap" || property == "column-gap") {
    if (property == "flex-basis" && oneOf({"auto", "content", "min-content", "max-content", "fit-content"}))
      return true;
    if (property != "flex-basis" && lower == "normal")
      return true;
    const auto length = parseCssLengthPercentage(QStringView(lower), {});
    return length.status == CssLengthStatus::Valid &&
           (lower.startsWith("calc(") || (length.px >= 0 && length.fraction >= 0));
  }
  if (property == QLatin1String("color") || property.endsWith(QLatin1String("-color"))) {
    const auto parts = property == QLatin1String("border-color") ? splitTopLevelSpaces(value) : QStringList{value};
    return parts.size() <= 4 && std::all_of(parts.begin(), parts.end(), isCssColorValue);
  }
  const bool padding = property == QLatin1String("padding") || property.startsWith(QLatin1String("padding-"));
  const bool margin = property == QLatin1String("margin") || property.startsWith(QLatin1String("margin-"));
  const bool borderWidth = property.startsWith(QLatin1String("border-")) && property.endsWith(QLatin1String("width"));
  const bool size = property == QLatin1String("font-size") || property == QLatin1String("width") ||
                    property == QLatin1String("height") || property.startsWith(QLatin1String("min-")) ||
                    property.startsWith(QLatin1String("max-"));
  if (padding || margin || borderWidth || size || property == QLatin1String("line-height") ||
      property == QLatin1String("letter-spacing") || property == QLatin1String("word-spacing")) {
    const auto parts = splitTopLevelSpaces(value);
    const bool shorthand = property == QLatin1String("padding") || property == QLatin1String("margin") ||
                           property == QLatin1String("border-width");
    if (parts.isEmpty() || parts.size() > (shorthand ? 4 : 1))
      return false;
    for (const auto& part : parts) {
      const auto keyword = part.toLower();
      if ((margin || (size && property != QLatin1String("font-size"))) && keyword == QLatin1String("auto"))
        continue;
      if (size && property != QLatin1String("font-size") &&
          (keyword == QLatin1String("none") || keyword == QLatin1String("fit-content") ||
           keyword == QLatin1String("min-content") || keyword == QLatin1String("max-content")))
        continue;
      if (borderWidth &&
          (keyword == QLatin1String("thin") || keyword == QLatin1String("medium") || keyword == QLatin1String("thick")))
        continue;
      if ((property == QLatin1String("line-height") || property.endsWith(QLatin1String("spacing"))) &&
          keyword == QLatin1String("normal"))
        continue;
      if (property == QLatin1String("font-size") && QStringList{"xx-small", "x-small", "small", "medium", "large",
                                                                "x-large", "xx-large", "xxx-large", "smaller", "larger"}
                                                        .contains(keyword))
        continue;
      const auto length = parseCssLengthPercentage(QStringView(part), {}, property == QLatin1String("line-height"));
      if (length.status != CssLengthStatus::Valid || (borderWidth && length.hasPercentage))
        return false;
      // calc() is range-clamped at used-value time, literal negatives are invalid.
      if (!margin && property != QLatin1String("letter-spacing") && property != QLatin1String("word-spacing") &&
          !keyword.startsWith(QLatin1String("calc(")) && (length.px < 0 || length.fraction < 0))
        return false;
    }
    return true;
  }
  if (property == QLatin1String("background")) {
    const QStringList keywords{"none",        "repeat",  "repeat-x", "repeat-y", "no-repeat",   "space",
                               "round",       "scroll",  "fixed",    "local",    "padding-box", "border-box",
                               "content-box", "left",    "right",    "top",      "bottom",      "center",
                               "cover",       "contain", "auto",     "/"};
    int colors = 0;
    for (const auto& layer : CssThemeParser::splitTopLevelCommas(value)) {
      for (const auto& token : splitTopLevelSpaces(layer)) {
        if (isCssColorValue(token)) {
          if (++colors > 1)
            return false;
          continue;
        }
        const auto part = token.toLower();
        if (keywords.contains(part))
          continue;
        if ((part.startsWith(QLatin1String("url(")) || part.contains(QLatin1String("gradient("))) && part.endsWith(')'))
          continue;
        const auto length = parseCssLengthPercentage(QStringView(part), {});
        if (length.status == CssLengthStatus::Valid)
          continue;
        // Position/size may share a slash with no surrounding spaces.
        if (part.contains('/') && !part.contains('(')) {
          bool valid = true;
          for (const auto& piece : part.split('/'))
            valid = valid && (keywords.contains(piece) ||
                              parseCssLengthPercentage(QStringView(piece), {}).status == CssLengthStatus::Valid);
          if (valid)
            continue;
        }
        return false;
      }
    }
    return true;
  }
  if (property == QLatin1String("box-sizing"))
    return lower == QLatin1String("content-box") || lower == QLatin1String("border-box");
  if (property == QLatin1String("border-style") ||
      (property.startsWith(QLatin1String("border-")) && property.endsWith(QLatin1String("-style")))) {
    const auto parts = splitTopLevelSpaces(lower);
    const QStringList styles{"none",   "hidden", "solid", "dotted", "dashed",
                             "double", "groove", "ridge", "inset",  "outset"};
    return parts.size() <= (property == QLatin1String("border-style") ? 4 : 1) &&
           std::all_of(parts.begin(), parts.end(), [&](const auto& part) { return styles.contains(part); });
  }
  return true;
}

} // namespace

CssSelectorSupport CssComputedStyleEngine::selectorSupport(const QString& selector, QString* pseudoElement) {
  const auto parsed = parseSelector(selector);
  if (pseudoElement)
    *pseudoElement = parsed.parts.isEmpty() ? QString{} : parsed.parts.back().simple.pseudoElement;
  if (parsed.exportOnly)
    return CssSelectorSupport::ExportOnly;
  if (parsed.editorOnly)
    return CssSelectorSupport::EditorOnly;
  if (!parsed.valid || std::any_of(parsed.parts.begin(), parsed.parts.end(), [](const auto& part) {
        const auto& simple = part.simple;
        return simple.unsupported || (!simple.pseudoElement.isEmpty() && simple.pseudoElement != "before" &&
                                      simple.pseudoElement != "after" && simple.pseudoElement != "marker" &&
                                      simple.pseudoElement != "selection");
      }))
    return CssSelectorSupport::Unsupported;
  return CssSelectorSupport::Supported;
}

bool CssComputedStyleEngine::supportsDeclaration(const QString& property, const QString& value) {
  if (value.trimmed().isEmpty())
    return false;
  if (property.startsWith("--"))
    return true;
  static const QSet<QString> validated = {"color",
                                          "background-color",
                                          "border-color",
                                          "border-top-color",
                                          "border-right-color",
                                          "border-bottom-color",
                                          "border-left-color",
                                          "display",
                                          "aspect-ratio",
                                          "box-sizing",
                                          "width",
                                          "height",
                                          "min-width",
                                          "min-height",
                                          "max-width",
                                          "max-height",
                                          "margin",
                                          "margin-top",
                                          "margin-right",
                                          "margin-bottom",
                                          "margin-left",
                                          "padding",
                                          "padding-top",
                                          "padding-right",
                                          "padding-bottom",
                                          "padding-left",
                                          "font-size",
                                          "font-weight",
                                          "font-style",
                                          "line-height",
                                          "letter-spacing",
                                          "word-spacing",
                                          "border-width",
                                          "border-top-width",
                                          "border-right-width",
                                          "border-bottom-width",
                                          "border-left-width",
                                          "border-style",
                                          "border-top-style",
                                          "border-right-style",
                                          "border-bottom-style",
                                          "border-left-style",
                                          "grid-template-columns",
                                          "grid-template-rows",
                                          "grid-template-areas",
                                          "grid-auto-columns",
                                          "grid-auto-rows",
                                          "grid-auto-flow",
                                          "grid-column-start",
                                          "grid-column-end",
                                          "grid-row-start",
                                          "grid-row-end",
                                          "justify-items",
                                          "justify-self",
                                          "flex-direction",
                                          "flex-wrap",
                                          "flex-grow",
                                          "flex-shrink",
                                          "flex-basis",
                                          "order",
                                          "justify-content",
                                          "align-items",
                                          "align-self",
                                          "align-content",
                                          "overflow-x",
                                          "overflow-y",
                                          "overflow-wrap",
                                          "word-break",
                                          "row-gap",
                                          "column-gap"};
  if (validated.contains(property))
    return validDeclarationValue(property, value, {});
  const auto lower = value.trimmed().toLower();
  if (property == "counter-reset" || property == "counter-set" || property == "counter-increment")
    return cssWide(lower) || parseCounterOperations(value, property == "counter-increment" ? 1 : 0).has_value();
  if (property.startsWith("-webkit-mask"))
    return supportsDeclaration(property.mid(8), value);
  if (property == "mask" || property.startsWith("mask-")) {
    if (cssWide(lower))
      return true;
    if (property == "mask-image")
      return supportsDeclaration("background-image", value);
    if (property == "mask-repeat")
      return supportsDeclaration("background-repeat", value);
    const auto layers = CssThemeParser::splitTopLevelCommas(lower);
    if (property == "mask") {
      const auto declarations = expandDeclaration({property, value, false}, {});
      return declarations.size() == 9 &&
             std::all_of(declarations.begin() + 1, declarations.end(),
                         [](const auto& decl) { return supportsDeclaration(decl.property, decl.value); });
    }
    for (const auto& layer : layers) {
      if (property == "mask-mode") {
        if (layer != "alpha" && layer != "luminance" && layer != "match-source")
          return false;
      } else if (property == "mask-composite") {
        if (!QStringList{"add", "subtract", "intersect", "exclude"}.contains(layer))
          return false;
      } else if (property == "mask-origin" || property == "mask-clip") {
        if (!QStringList{"border-box", "padding-box", "content-box"}.contains(layer))
          return false;
      } else if (property == "mask-size") {
        if (layer == "cover" || layer == "contain")
          continue;
        const auto parts = splitTopLevelSpaces(layer);
        if (parts.isEmpty() || parts.size() > 2)
          return false;
        for (const auto& part : parts)
          if (part != "auto") {
            const auto v = parseCssLengthPercentage(QStringView(part), {});
            if (v.status != CssLengthStatus::Valid || v.px < 0 || v.fraction < 0)
              return false;
          }
      } else if (property == "mask-position") {
        const auto parts = splitTopLevelSpaces(layer);
        if (parts.isEmpty() || parts.size() > 4)
          return false;
        for (const auto& part : parts)
          if (!QStringList{"left", "right", "top", "bottom", "center"}.contains(part) &&
              parseCssLengthPercentage(QStringView(part), {}).status != CssLengthStatus::Valid)
            return false;
      } else
        return false;
    }
    return !layers.isEmpty();
  }
  if (property == "background-image") {
    if (lower == "none" || cssWide(lower))
      return true;
    const auto layers = CssThemeParser::splitTopLevelCommas(value);
    if (layers.isEmpty())
      return false;
    return std::all_of(layers.cbegin(), layers.cend(), [&](const auto& layer) {
      if (layer.trimmed().compare("none", Qt::CaseInsensitive) == 0)
        return true;
      const auto gradient = parseGradientSpec(layer, {});
      const auto urls = CssThemeParser::resourceUrls(layer);
      return (gradient.kind != GradientSpec::Kind::None && gradient.stops.size() >= 2) ||
             (layer.trimmed().startsWith("url(", Qt::CaseInsensitive) && layer.trimmed().endsWith(')') &&
              urls.size() == 1 && CssImageResources::supportsUrl(urls.front()));
    });
  }
  if (property == "background-repeat") {
    if (cssWide(lower))
      return true;
    const auto layers = CssThemeParser::splitTopLevelCommas(lower);
    return !layers.isEmpty() && std::all_of(layers.begin(), layers.end(), [](const auto& layer) {
      const auto parts = splitTopLevelSpaces(layer);
      if (parts.size() == 1 && (parts[0] == "repeat-x" || parts[0] == "repeat-y"))
        return true;
      return !parts.isEmpty() && parts.size() <= 2 && std::all_of(parts.begin(), parts.end(), [](const auto& part) {
        return part == "repeat" || part == "no-repeat" || part == "space" || part == "round";
      });
    });
  }
  if (property == "background-blend-mode") {
    if (cssWide(lower))
      return true;
    const auto layers = CssThemeParser::splitTopLevelCommas(lower);
    return !layers.isEmpty() && std::all_of(layers.begin(), layers.end(),
                                            [](const auto& layer) { return parseCssBlendMode(layer).has_value(); });
  }
  if (property == "background") {
    if (!validDeclarationValue(property, value, {}))
      return false;
    const auto expanded = expandDeclaration({property, value, false}, {});
    for (const auto& declaration : expanded)
      if (declaration.property == "background-image" && !supportsDeclaration(declaration.property, declaration.value))
        return false;
    return true;
  }
  if (property == "filter" || property == "backdrop-filter")
    return cssWide(lower) || parseCssFilters(value).has_value();
  if (property == "opacity") {
    if (cssWide(lower))
      return true;
    QString number = lower;
    if (number.endsWith('%'))
      number.chop(1);
    bool ok = false;
    const auto n = number.toDouble(&ok);
    return ok && std::isfinite(n);
  }
  return false;
}

namespace {
bool mayMatchSubject(const SimpleSelector& simple, const CssElement& element) {
  if (simple.unsupported || (!simple.tag.isEmpty() && simple.tag != element.tag.toLower()) ||
      (!simple.id.isEmpty() && simple.id != element.id) || simple.pseudoElement != element.pseudoElement.toLower())
    return false;
  for (const auto& name : simple.classes)
    if (!element.classes.contains(name))
      return false;
  for (const auto& attribute : simple.attributes) {
    if (attribute.name == "id" && !element.id.isEmpty())
      continue;
    if (attribute.name == "class" && !element.classes.isEmpty())
      continue;
    bool exists = false;
    for (auto it = element.attributes.cbegin(); it != element.attributes.cend(); ++it)
      if (it.key().compare(attribute.name, Qt::CaseInsensitive) == 0) {
        exists = true;
        break;
      }
    if (!exists)
      return false;
  }
  const auto& subject = element.originatingElement ? *element.originatingElement : element;
  for (const auto& alternatives : simple.alternatives)
    if (std::none_of(alternatives.begin(), alternatives.end(), [&](const auto& alternative) {
          return alternative.valid && !alternative.parts.isEmpty() &&
                 mayMatchSubject(alternative.parts.back().simple, subject);
        }))
      return false;
  // Negations and structural/context/state predicates cannot exclude a subject
  // from this metadata-only index. The full matcher resolves all of them below.
  return true;
}

QByteArray subjectMetadata(const CssElement& element) {
  QByteArray key;
  QDataStream stream(&key, QIODevice::WriteOnly);
  QStringList attributes;
  for (auto it = element.attributes.cbegin(); it != element.attributes.cend(); ++it)
    attributes.append(it.key().toLower());
  attributes.sort();
  auto classes = element.classes;
  classes.sort();
  stream << element.tag.toLower() << element.id << element.pseudoElement.toLower() << classes << attributes;
  if (element.originatingElement)
    stream << subjectMetadata(*element.originatingElement);
  return key;
}
} // namespace

bool CssComputedStyleEngine::structuralSelectorMatches(int selector, const CssElement& element) const {
  const auto& parsed = parsedSelectors_[selector];
  const int last = int(parsed.parts.size()) - 1;
  const auto relation = parsed.parts.back().combinator;
  if (last < 1 || relation == '+' || relation == '~')
    return selectorMatches(parsed, element, {}, parsed.interactive);
  if (!simpleMatches(parsed.parts.back().simple, element, {}, parsed.interactive))
    return false;
  const auto prefixMatches = [&](const CssElement& ancestor) {
    const auto key = qMakePair(ancestor.cacheId, selector);
    if (ancestor.cacheId) {
      if (const auto found = selectorPrefixCache_.constFind(key); found != selectorPrefixCache_.cend())
        return found.value();
    }
    const bool matches = selectorMatchesAt(parsed, last - 1, &ancestor, {}, parsed.interactive);
    if (ancestor.cacheId) {
      if (selectorPrefixCache_.size() >= 8192)
        selectorPrefixCache_.clear();
      selectorPrefixCache_.insert(key, matches);
    }
    return matches;
  };
  for (const auto* ancestor = element.parent; ancestor; ancestor = ancestor->parent) {
    if (prefixMatches(*ancestor))
      return true;
    if (relation == '>')
      break;
  }
  return false;
}

CssComputedStyleEngine::ElementMatches CssComputedStyleEngine::structuralMatches(const CssElement& element) const {
  if (element.cacheId) {
    const auto found = selectorMatchCache_.constFind(element.cacheId);
    if (found != selectorMatchCache_.cend())
      return found.value();
  }
  diag::ScopedPerfProbe detail("css.selectorMiss", cssDetailPerf());
  const auto& rules = sheet_.rules();
  const auto metadata = subjectMetadata(element);
  auto candidates = ruleCandidates_.constFind(metadata);
  if (candidates == ruleCandidates_.cend()) {
    std::vector<SubjectCandidates> indices;
    for (std::size_t ri = 0; ri < rules.size(); ++ri) {
      SubjectCandidates rule{ri};
      for (int si = ruleSelectorRange_[ri].first; si < ruleSelectorRange_[ri].second; ++si) {
        const auto& selector = parsedSelectors_[si];
        if (selector.valid && !selector.exportOnly && !selector.editorOnly && !selector.parts.isEmpty() &&
            mayMatchSubject(selector.parts.back().simple, element))
          rule.selectors.append(si);
      }
      if (!rule.selectors.isEmpty())
        indices.push_back(std::move(rule));
    }
    if (ruleCandidates_.size() >= 512)
      ruleCandidates_.clear();
    ruleCandidates_.insert(metadata, std::move(indices));
    candidates = ruleCandidates_.constFind(metadata);
  }
  ElementMatches result;
  for (const auto& candidate : candidates.value()) {
    RuleCandidates rule{candidate.rule};
    for (const int si : candidate.selectors) {
      const auto& parsed = parsedSelectors_[si];
      if (!structuralSelectorMatches(si, element))
        continue;
      if (parsed.interactive) {
        rule.interactiveSelectors.push_back(si);
        result.stateMask |= selectorStateMask(parsed);
      } else if (rule.staticSelector < 0 || parsed.specificity > parsedSelectors_[rule.staticSelector].specificity) {
        rule.staticSelector = si;
      }
    }
    if (rule.staticSelector >= 0 || !rule.interactiveSelectors.empty())
      result.rules.push_back(std::move(rule));
  }
  if (element.cacheId) {
    quint64 key = result.stateMask;
    for (const auto& rule : result.rules) {
      key = qHashMulti(size_t(key), rule.rule, rule.staticSelector);
      for (const auto selector : rule.interactiveSelectors)
        key = qHashMulti(size_t(key), selector);
    }
    bool shared = false;
    if (const auto bucket = sharedSelectorMatches_.constFind(key); bucket != sharedSelectorMatches_.cend())
      for (const auto& existing : *bucket)
        if (existing == result) {
          result = existing;
          shared = true;
          break;
        }
    if (!shared) {
      if (sharedSelectorMatches_.size() >= 512)
        sharedSelectorMatches_.clear();
      sharedSelectorMatches_[key].append(result);
    }
    selectorMatchCache_.insert(element.cacheId, result);
  }
  return result;
}

std::vector<CssComputedStyleEngine::RuleMatch>
CssComputedStyleEngine::matchedRules(const CssElement& element, const CssElementState& state) const {
  diag::ScopedPerfProbe detail("css.matchedRules", cssDetailPerf());
  const auto candidates = structuralMatches(element);
  std::vector<RuleMatch> result;
  result.reserve(candidates.rules.size());
  for (const auto& rule : candidates.rules) {
    int selected = rule.staticSelector;
    for (const int si : rule.interactiveSelectors) {
      if ((selected < 0 || parsedSelectors_[si].specificity > parsedSelectors_[selected].specificity ||
           (parsedSelectors_[si].specificity == parsedSelectors_[selected].specificity && si < selected)) &&
          selectorMatches(parsedSelectors_[si], element, state))
        selected = si;
    }
    if (selected >= 0)
      result.push_back({rule.rule, selected});
  }
  return result;
}

void CssComputedStyleEngine::forgetElements(const QVector<quint64>& identities) const {
  for (const auto id : identities) {
    computedCache_.remove(id);
    selectorMatchCache_.remove(id);
    interactionMaskCache_.remove(id);
    counterDependencyCache_.remove(id);
  }
  const QSet<quint64> removed(identities.begin(), identities.end());
  for (auto it = selectorPrefixCache_.begin(); it != selectorPrefixCache_.end();)
    if (removed.contains(it.key().first))
      it = selectorPrefixCache_.erase(it);
    else
      ++it;
}

CssComputedStyleEngine::CacheStats CssComputedStyleEngine::cacheStats() const {
  CacheStats result{selectorMatchCache_.size(), resolvedStyleCount_};
  for (const auto& bucket : cascadeCache_)
    result.cascades += bucket.size();
  for (const auto& bucket : valueCache_)
    result.values += bucket.size();
  for (const auto& bucket : sharedSelectorMatches_)
    result.matchSets += bucket.size();
  return result;
}

QByteArray CssComputedStyleEngine::counterDependencySignature(const CssElement& element) const {
  if (element.cacheId) {
    const auto found = counterDependencyCache_.constFind(element.cacheId);
    if (found != counterDependencyCache_.cend())
      return found.value();
  }
  const auto relevant = [](const CssDeclaration& declaration) {
    return declaration.property.startsWith("counter-") || declaration.property.startsWith("--") ||
           declaration.property == "content" || declaration.property == "display";
  };
  QByteArray result;
  QDataStream stream(&result, QIODevice::WriteOnly);
  const auto* parent = element.originatingElement ? element.originatingElement : element.parent;
  stream << (parent ? counterDependencySignature(*parent) : QByteArray{});
  for (const auto& match : matchedRules(element, {})) {
    if (std::any_of(sheet_.rules()[match.rule].declarations.begin(), sheet_.rules()[match.rule].declarations.end(),
                    relevant))
      stream << quint64(match.rule) << qint32(parsedSelectors_[match.selector].specificity);
  }
  stream << quint64(-1);
  for (const auto& declaration : element.inlineDeclarations)
    if (relevant(declaration))
      stream << declaration.property << declaration.value << declaration.important << declaration.resourceBaseDir;
  if (element.cacheId)
    counterDependencyCache_.insert(element.cacheId, result);
  return result;
}

quint8 CssComputedStyleEngine::interactiveStateMask(const CssElement& element) const {
  if (element.cacheId) {
    const auto found = interactionMaskCache_.constFind(element.cacheId);
    if (found != interactionMaskCache_.cend())
      return found.value();
  }
  quint8 mask = structuralMatches(element).stateMask;
  const auto* parent = element.originatingElement ? element.originatingElement : element.parent;
  if (parent)
    mask |= interactiveStateMask(*parent);
  if (element.cacheId)
    interactionMaskCache_.insert(element.cacheId, mask);
  return mask;
}

void CssComputedStyleEngine::applyStyleForElement(const std::vector<RuleMatch>& matched, CssComputedStyle& style,
                                                  const std::vector<CssDeclaration>& inlineDeclarations,
                                                  const std::vector<CssDeclaration>& presentationDeclarations) const {
  diag::ScopedPerfProbe cascadePerf("css.applyCascade", cssDetailPerf());
  struct Match {
    const std::vector<CssDeclaration>* declarations;
    QString selector;
    int specificity;
  };
  std::vector<Match> matches;
  if (!presentationDeclarations.empty())
    matches.push_back({&presentationDeclarations, {}, 0});
  for (const auto& match : matched) {
    const auto& selector = parsedSelectors_[match.selector];
    matches.push_back({&sheet_.rules()[match.rule].declarations, selector.selectorText, selector.specificity});
  }
  if (!inlineDeclarations.empty())
    matches.push_back({&inlineDeclarations, QStringLiteral("style attribute"), 1000000});
  const bool reusable = inlineDeclarations.empty() && presentationDeclarations.empty();
  const CssComputedStyle cascadeInput = style;
  std::vector<std::pair<quintptr, int>> signature;
  quint64 signatureHash = style.fingerprint();
  if (reusable) {
    signature.reserve(matches.size());
    for (const auto& match : matches) {
      signature.emplace_back(reinterpret_cast<quintptr>(match.declarations), match.specificity);
      signatureHash = qHashMulti(signatureHash, signature.back().first, signature.back().second);
    }
    const auto bucket = cascadeCache_.constFind(signatureHash);
    if (bucket != cascadeCache_.cend()) {
      for (const auto& cached : bucket.value()) {
        if (cached.input == cascadeInput && cached.matches == signature) {
          style = cached.result;
          return;
        }
      }
    }
  }
  QHash<QString, Candidate> winners;
  const auto collect = [&](bool custom) {
    int order = 0;
    for (const auto& match : matches)
      for (const auto& decl : *match.declarations) {
        ++order;
        if (decl.property.startsWith(QStringLiteral("--")) != custom)
          continue;
        CssDeclaration resolved = decl;
        resolved.value = CssThemeParser::resolveResourceUrls(decl.value, decl.resourceBaseDir);
        if (!custom) {
          const bool variable = decl.value.contains(QStringLiteral("var("), Qt::CaseInsensitive);
          const auto substituted = CssThemeParser::substituteVars(resolved.value, style.customProperties_);
          if (!substituted || !validDeclarationValue(decl.property, *substituted, {})) {
            if (!variable)
              continue;                               // syntax-invalid declarations never enter the cascade
            resolved.value = QStringLiteral("unset"); // invalid at computed-value time
          } else
            resolved.value =
                cssWide(substituted->trimmed().toLower()) ? substituted->trimmed().toLower() : *substituted;
          const auto components = expandDeclaration(resolved, {});
          const bool valid =
              !components.empty() && std::all_of(components.begin(), components.end(), [&](const auto& component) {
                return validDeclarationValue(component.property, component.value, {});
              });
          if (!valid) {
            if (!variable)
              continue;
            resolved.value = QStringLiteral("unset");
          }
        }
        const auto expanded = custom ? std::vector<CssDeclaration>{resolved} : expandDeclaration(resolved, {});
        for (const auto& component : expanded) {
          Candidate c{component.value, match.selector, component.important, match.specificity, ++order};
          const auto it = winners.constFind(component.property);
          if (it == winners.constEnd() || cascadeBeats(c, it.value()))
            winners.insert(component.property, c);
        }
      }
  };
  collect(true);
  for (auto it = winners.cbegin(); it != winners.cend(); ++it) {
    const auto value = it.value().value.trimmed();
    if (value.compare(QLatin1String("initial"), Qt::CaseInsensitive) == 0)
      style.customProperties_.remove(it.key());
    else if (value.compare(QLatin1String("inherit"), Qt::CaseInsensitive) != 0 &&
             value.compare(QLatin1String("unset"), Qt::CaseInsensitive) != 0)
      style.customProperties_.insert(it.key(), it.value().value);
  }
  style.customProperties_ = CssThemeParser::computeCustomProperties(style.customProperties_);
  winners.clear();
  collect(false);
  for (auto it = winners.cbegin(); it != winners.cend(); ++it) {
    style.properties_.insert(it.key(), it.value().value);
    style.computedLengths_.remove(it.key());
  }
  if (reusable) {
    if (cascadeCache_.size() >= 2048)
      cascadeCache_.clear();
    cascadeCache_[signatureHash].push_back({cascadeInput, style, std::move(signature)});
  }
}

void CssComputedStyleEngine::computeValues(CssComputedStyle& style, const CssComputedStyle& parent, bool root) const {
  diag::ScopedPerfProbe valuesPerf("css.computeValues", cssDetailPerf());
  static const QHash<QString, QString> initialValues{{QStringLiteral("color"), QStringLiteral("rgb(0, 0, 0)")},
                                                     {QStringLiteral("fill"), QStringLiteral("rgb(0, 0, 0)")},
                                                     {QStringLiteral("stroke"), QStringLiteral("none")},
                                                     {QStringLiteral("font-size"), QStringLiteral("16px")},
                                                     {QStringLiteral("font-weight"), QStringLiteral("normal")},
                                                     {QStringLiteral("font-style"), QStringLiteral("normal")},
                                                     {QStringLiteral("line-height"), QStringLiteral("normal")},
                                                     {QStringLiteral("visibility"), QStringLiteral("visible")},
                                                     {"box-sizing", "content-box"},
                                                     {"display", "inline"},
                                                     {"aspect-ratio", "auto"},
                                                     {"grid-template-columns", "none"},
                                                     {"grid-template-rows", "none"},
                                                     {"grid-template-areas", "none"},
                                                     {"grid-auto-columns", "auto"},
                                                     {"grid-auto-rows", "auto"},
                                                     {"grid-auto-flow", "row"},
                                                     {"grid-column-start", "auto"},
                                                     {"grid-column-end", "auto"},
                                                     {"grid-row-start", "auto"},
                                                     {"grid-row-end", "auto"},
                                                     {"justify-items", "normal"},
                                                     {"justify-self", "auto"},
                                                     {"flex-grow", "0"},
                                                     {"flex-shrink", "1"},
                                                     {"flex-basis", "auto"},
                                                     {"flex-direction", "row"},
                                                     {"flex-wrap", "nowrap"},
                                                     {"order", "0"},
                                                     {"align-items", "normal"},
                                                     {"align-self", "auto"},
                                                     {"align-content", "normal"},
                                                     {"justify-content", "normal"},
                                                     {"row-gap", "normal"},
                                                     {"column-gap", "normal"},
                                                     {"overflow-x", "visible"},
                                                     {"overflow-y", "visible"},
                                                     {"background-color", "transparent"},
                                                     {"background-image", "none"},
                                                     {"font-family", "serif"},
                                                     {"font-variant", "normal"},
                                                     {"font-stretch", "normal"},
                                                     {"width", "auto"},
                                                     {"height", "auto"},
                                                     {"margin-top", "0px"},
                                                     {"margin-right", "0px"},
                                                     {"margin-bottom", "0px"},
                                                     {"margin-left", "0px"},
                                                     {"padding-top", "0px"},
                                                     {"padding-right", "0px"},
                                                     {"padding-bottom", "0px"},
                                                     {"padding-left", "0px"},
                                                     {"border-top-width", "medium"},
                                                     {"border-right-width", "medium"},
                                                     {"border-bottom-width", "medium"},
                                                     {"border-left-width", "medium"},
                                                     {"border-top-style", "none"},
                                                     {"border-right-style", "none"},
                                                     {"border-bottom-style", "none"},
                                                     {"border-left-style", "none"},
                                                     {"border-top-color", "currentColor"},
                                                     {"border-right-color", "currentColor"},
                                                     {"border-bottom-color", "currentColor"},
                                                     {"border-left-color", "currentColor"}};
  for (auto it = style.properties_.begin(); it != style.properties_.end();) {
    const QString value = style.resolvedValue(it.key()).trimmed().toLower();
    if (value == QStringLiteral("inherit") ||
        (value == QStringLiteral("unset") && inheritedProperties().contains(it.key()))) {
      const auto inherited = parent.properties_.constFind(it.key());
      if (inherited != parent.properties_.cend()) {
        it.value() = inherited.value();
        if (parent.computedLengths_.contains(it.key()))
          style.computedLengths_.insert(it.key(), parent.computedLengths_.value(it.key()));
        if (parent.computedGridTracks_.contains(it.key()))
          style.computedGridTracks_.insert(it.key(), parent.computedGridTracks_.value(it.key()));
        ++it;
      } else if (initialValues.contains(it.key())) {
        it.value() = initialValues.value(it.key());
        ++it;
      } else
        it = style.properties_.erase(it);
    } else if (value == QStringLiteral("initial") || value == QStringLiteral("unset") ||
               value == QLatin1String("revert") || value == QLatin1String("revert-layer")) {
      if (initialValues.contains(it.key())) {
        it.value() = initialValues.value(it.key());
        ++it;
      } else
        it = style.properties_.erase(it);
    } else
      ++it;
  }
  const auto weight = style.resolvedValue("font-weight").trimmed().toLower();
  if (weight == "bolder" || weight == "lighter") {
    const auto inherited = parent.resolvedValue("font-weight").trimmed().toLower();
    bool ok = false;
    const auto numeric = inherited.toDouble(&ok);
    const qreal base = ok ? numeric : inherited == "bold" ? 700 : 400;
    const qreal computed = weight == "bolder" ? (base < 350   ? 400
                                                 : base < 550 ? 700
                                                              : qMax<qreal>(900, base))
                           : base < 550       ? qMin<qreal>(100, base)
                           : base < 750       ? 400
                                              : 700;
    style.properties_.insert("font-weight", QString::number(computed));
  }
  const QString raw = style.resolvedValue(QStringLiteral("font-size"));
  CssLengthContext context;
  context.emPx = parent.fontSizePx;
  context.remPx = root ? 16.0 : parent.rootFontSizePx;
  context.viewportPx = QSizeF(environment_.viewportWidth, environment_.viewportHeight);
  const auto size = parseCssLengthPercentage(QStringView(raw), context);
  static const QHash<QString, qreal> absoluteSizes{{"xx-small", 9},  {"x-small", 10},  {"small", 13},
                                                   {"medium", 16},   {"large", 18},    {"x-large", 24},
                                                   {"xx-large", 32}, {"xxx-large", 48}};
  style.fontSizePx = size.status == CssLengthStatus::Valid
                         ? qMax<qreal>(0, size.used(parent.fontSizePx))
                         : absoluteSizes.value(raw.toLower(), raw == QLatin1String("smaller")  ? parent.fontSizePx / 1.2
                                                              : raw == QLatin1String("larger") ? parent.fontSizePx * 1.2
                                                                                               : parent.fontSizePx);
  style.rootFontSizePx = root ? style.fontSizePx : parent.rootFontSizePx;
  style.containingWidthPx = -1;
  style.viewportPx = context.viewportPx;
  style.textScale = environment_.textScale;
  if (!raw.isEmpty())
    style.properties_.insert(QStringLiteral("font-size"),
                             QString::number(style.fontSizePx, 'g', 12) + QStringLiteral("px"));
  const QString line = style.resolvedValue(QStringLiteral("line-height"));
  bool number = false;
  line.toDouble(&number);
  if (!line.isEmpty() && !number && line != QStringLiteral("normal")) {
    context.emPx = style.fontSizePx;
    context.remPx = style.rootFontSizePx;
    const auto height = parseCssLengthPercentage(QStringView(line), context);
    if (height.status == CssLengthStatus::Valid)
      style.properties_.insert(QStringLiteral("line-height"),
                               QString::number(qMax<qreal>(0, height.used(style.fontSizePx)), 'g', 12) +
                                   QStringLiteral("px"));
  }
  // Freeze font-relative units in the defining element's scope. Percentages
  // remain symbolic, including when an explicit inherit copies a box length.
  for (auto it = style.properties_.cbegin(); it != style.properties_.cend(); ++it) {
    if (style.computedLengths_.contains(it.key()) || it.key() == QLatin1String("font-size"))
      continue;
    context.emPx = style.fontSizePx * style.textScale;
    context.remPx = style.rootFontSizePx * style.textScale;
    const auto length = parseCssLengthPercentage(QStringView(it.value()), context);
    if (length.status == CssLengthStatus::Valid)
      style.computedLengths_.insert(it.key(), length);
  }
  for (const auto* property : {"grid-template-columns", "grid-template-rows", "grid-auto-columns", "grid-auto-rows"}) {
    if (style.computedGridTracks_.contains(property))
      continue;
    if (const auto tracks = parseCssGridTracks(style.resolvedValue(property), context))
      style.computedGridTracks_.insert(property, *tracks);
  }
}

quint64 CssComputedStyle::fingerprint() const {
  diag::ScopedPerfProbe detail("css.fingerprint", cssDetailPerf());
  // Final computed values are shared across many inline fragments and states.
  // Keep bounded snapshots so any public-map mutation detaches before a later
  // identity lookup. Scalars remain explicit inputs; hash equality alone never
  // establishes a cache hit. Do not retain image resources (not hash inputs).
  struct FingerprintMemo {
    CssComputedStyle values;
    quint64 hash = 0;
    bool valid = false;
  };
  thread_local std::array<FingerprintMemo, 128> fingerprints;
  thread_local size_t nextFingerprint = 0;
  const auto cachedFingerprint = std::find_if(fingerprints.begin(), fingerprints.end(), [&](const auto& entry) {
    const auto& value = entry.values;
    return entry.valid && value.fontSizePx == fontSizePx && value.rootFontSizePx == rootFontSizePx &&
           value.textScale == textScale && value.viewportPx == viewportPx &&
           value.containingWidthPx == containingWidthPx && value.properties_.isSharedWith(properties_) &&
           value.customProperties_.isSharedWith(customProperties_) &&
           value.computedLengths_.isSharedWith(computedLengths_) &&
           value.computedGridTracks_.isSharedWith(computedGridTracks_);
  });
  if (cachedFingerprint != fingerprints.end())
    return cachedFingerprint->hash;
  quint64 result = qHashMulti(size_t(0), fontSizePx, rootFontSizePx, textScale, viewportPx.width(), viewportPx.height(),
                              containingWidthPx);
  for (auto it = properties_.cbegin(); it != properties_.cend(); ++it)
    result += qHashMulti(size_t(0), it.key(), it.value());
  // Inherited variable maps are implicitly shared. Hashing VLOOK's embedded
  // resources again for every inline/state defeats the computed-value cache.
  // Holding a bounded snapshot also makes mutations detach, so identity reuse
  // can never return a hash for old values (including direct map mutations).
  struct VariablesHash {
    QHash<QString, QString> values;
    quint64 hash = 0;
  };
  // A document has several inherited scopes and interaction states in flight.
  // Four entries thrashed between VLOOK's root/paragraph/inline maps, rehashing
  // megabytes of embedded resources even on final-style cache hits. Keep a
  // bounded working set of shared map snapshots, validated by Qt storage identity.
  thread_local std::array<VariablesHash, 128> variablesHashes;
  thread_local size_t nextVariablesHash = 0;
  const auto cached = std::find_if(variablesHashes.begin(), variablesHashes.end(),
                                   [&](const auto& entry) { return entry.values.isSharedWith(customProperties_); });
  if (cached != variablesHashes.end())
    result += cached->hash;
  else {
    quint64 hash = 0;
    for (auto it = customProperties_.cbegin(); it != customProperties_.cend(); ++it)
      hash += qHashMulti(size_t(0), it.key(), it.value());
    variablesHashes[nextVariablesHash++ % variablesHashes.size()] = {customProperties_, hash};
    result += hash;
  }
  for (auto it = computedLengths_.cbegin(); it != computedLengths_.cend(); ++it)
    result += qHashMulti(size_t(0), it.key(), int(it->status), it->px, it->fraction, it->hasPercentage);
  for (auto it = computedGridTracks_.cbegin(); it != computedGridTracks_.cend(); ++it) {
    quint64 hash = qHashMulti(size_t(0), it.key(), it->subgrid);
    if (it->nameRepeat) {
      hash = qHashMulti(size_t(hash), it->nameRepeat->index);
      for (const auto& names : it->nameRepeat->names)
        hash = qHashMulti(size_t(hash), names);
    }
    const auto hashTracks = [&](const auto& tracks, const auto& names) {
      for (const auto& track : tracks) {
        hash = qHashMulti(size_t(hash), track.fitContent);
        for (const auto& value : {track.minimum, track.maximum})
          hash = qHashMulti(size_t(hash), int(value.kind), value.length.px, value.length.fraction,
                            value.length.hasPercentage, value.fraction);
      }
      for (const auto& line : names)
        for (const auto& name : line)
          hash = qHashMulti(size_t(hash), name);
    };
    hashTracks(it->tracks, it->lineNames);
    if (it->automatic) {
      hash = qHashMulti(size_t(hash), it->automatic->index, it->automatic->fit);
      for (const auto& name : it->automatic->beforeNames)
        hash = qHashMulti(size_t(hash), name);
      hashTracks(it->automatic->tracks, it->automatic->lineNames);
    }
    result += hash;
  }
  auto values = *this;
  values.imageResources.reset();
  fingerprints[nextFingerprint++ % fingerprints.size()] = {std::move(values), result, true};
  return result;
}

CssComputedStyle cssKeyframeStyle(CssComputedStyle base, const std::vector<CssDeclaration>& declarations) {
  for (const auto& declaration : declarations) {
    if (declaration.important)
      continue;
    auto resolved = declaration;
    resolved.value = CssThemeParser::resolveResourceUrls(declaration.value, declaration.resourceBaseDir);
    for (const auto& expanded : expandDeclaration(resolved, base.customProperties_)) {
      if (expanded.property.startsWith("--"))
        base.customProperties_[expanded.property] = expanded.value;
      else {
        base.properties_[expanded.property] = expanded.value;
        base.computedLengths_.remove(expanded.property);
      }
    }
  }
  return base;
}

CssComputedStyle CssComputedStyleEngine::parentStyleFor(const CssElement* parent) const {
  if (parent)
    return styleFor(*parent);
  CssComputedStyle initial;
  initial.customProperties_ = CssThemeParser::computeCustomProperties(sheet_.variables());
  return initial;
}
CssComputedStyle CssComputedStyleEngine::styleFor(const CssElement& element) const {
  return styleFor(element, {}, element.inlineDeclarations, element.presentationDeclarations);
}
CssComputedStyle CssComputedStyleEngine::styleFor(const CssElement& element, const CssElementState& state) const {
  return styleFor(element, state, element.inlineDeclarations, element.presentationDeclarations);
}
CssComputedStyle CssComputedStyleEngine::styleFor(const CssElement& element, const CssElementState& state,
                                                  const std::vector<CssDeclaration>& inlineDeclarations) const {
  return styleFor(element, state, inlineDeclarations, {});
}
CssComputedStyle CssComputedStyleEngine::styleFor(const CssElement& element, const CssElementState& state,
                                                  const std::vector<CssDeclaration>& inlineDeclarations,
                                                  const std::vector<CssDeclaration>& presentationDeclarations) const {
  ThemePerfScope performance(ThemePerfStage::Css);
  if (state.subject && !state.visited) {
    const quint8 bits = (state.hover ? 1 : 0) | ((state.focus || state.mdFocus) ? 2 : 0) | (state.active ? 4 : 0);
    // No applicable state dependency means exactly the ordinary snapshot,
    // including inherited custom properties. Reuse it before walking parents.
    if (!bits || !(bits & interactiveStateMask(element)))
      return styleFor(element, {}, inlineDeclarations, presentationDeclarations);
  }
  const bool cacheable = element.cacheId != 0 && inlineDeclarations.empty() && presentationDeclarations.empty() &&
                         element.inlineDeclarations.empty();
  const quint8 cacheKey = static_cast<quint8>(static_cast<int>(state.hover) | (state.focus << 1) | (state.active << 2) |
                                              (state.visited << 3) | (state.mdFocus << 4));
  if (cacheable && !state.subject) {
    const auto node = computedCache_.constFind(element.cacheId);
    if (node != computedCache_.constEnd()) {
      const auto cached = node->constFind(cacheKey);
      if (cached != node->constEnd())
        return cached.value();
    }
  }
  const auto* inheritanceParent = element.originatingElement ? element.originatingElement : element.parent;
  const bool htmlRoot = !inheritanceParent && element.tag.compare(QLatin1String("html"), Qt::CaseInsensitive) == 0;
  CssComputedStyle parent = htmlRoot                             ? CssComputedStyle{}
                            : inheritanceParent && state.subject ? styleFor(*inheritanceParent, state)
                            : element.originatingElement         ? styleFor(*element.originatingElement, state)
                                                                 : parentStyleFor(inheritanceParent);
  const auto matches = matchedRules(element, state);
  const bool root = !inheritanceParent;
  quint64 scopedKey = 0;
  if (cacheable) {
    diag::ScopedPerfProbe detail("css.finalLookup", cssDetailPerf());
    scopedKey = qHashMulti(size_t(0), root, parent.fingerprint());
    for (const auto& match : matches)
      scopedKey = qHashMulti(scopedKey, match.rule, match.selector);
    const auto bucket = scopedComputedCache_.constFind(scopedKey);
    if (bucket != scopedComputedCache_.cend()) {
      for (const auto& cached : bucket.value())
        if (cached.root == root && cached.matches == matches && cached.parent == parent) {
          if (!state.subject)
            computedCache_[element.cacheId].insert(cacheKey, cached.result);
          return cached.result;
        }
    }
  }
  countThemePerformanceWork(ThemePerfStage::Css);
  CssComputedStyle style;
  style.imageResources = sourceSheet_->imageResources();
  style.customProperties_ = parent.customProperties_;
  style.fontSizePx = parent.fontSizePx;
  style.rootFontSizePx = parent.rootFontSizePx;
  for (auto it = parent.properties_.cbegin(); it != parent.properties_.cend(); ++it)
    if (inheritedProperties().contains(it.key())) {
      style.properties_.insert(it.key(), it.value());
      if (parent.computedLengths_.contains(it.key()))
        style.computedLengths_.insert(it.key(), parent.computedLengths_.value(it.key()));
    }
  applyStyleForElement(matches, style, inlineDeclarations, presentationDeclarations);
  if (!environment_.deferComputedValues) {
    const auto valueKey = qHashMulti(size_t(0), style.fingerprint(), parent.fingerprint(), root);
    auto bucket = valueCache_.constFind(valueKey);
    bool found = false;
    if (bucket != valueCache_.cend()) {
      for (const auto& cached : bucket.value()) {
        if (cached.root == root && cached.input == style && cached.parent == parent) {
          style = cached.result;
          found = true;
          break;
        }
      }
    }
    if (!found) {
      const auto input = style;
      computeValues(style, parent, root);
      if (valueCache_.size() >= 2048)
        valueCache_.clear();
      valueCache_[valueKey].push_back({input, parent, style, root});
    }
  }
  if (cacheable) {
    // Atypical documents may have arbitrarily many distinct rule/parent inputs.
    // Bound shared memoization; live results use implicitly shared value maps.
    if (resolvedStyleCount_ >= 2048) {
      scopedComputedCache_.clear();
      resolvedStyleCount_ = 0;
      cascadeCache_.clear();
      valueCache_.clear();
    }
    scopedComputedCache_[scopedKey].push_back({root, matches, parent, style});
    ++resolvedStyleCount_;
    if (!state.subject)
      computedCache_[element.cacheId].insert(cacheKey, style);
  }
  return style;
}
} // namespace muffin
