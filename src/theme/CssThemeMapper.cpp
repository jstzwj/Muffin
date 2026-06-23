#include "theme/CssThemeMapper.h"

#include "theme/ThemeDefinition.h"
#include "theme/CssThemeParser.h"

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <vector>

namespace muffin {
namespace {

// Analysed right-most compound of a selector — enough to decide which semantic
// token it can feed and to exclude state pseudo-classes (:hover etc.).
struct SelInfo {
  QString tag;            // rightmost type selector, lowercased ("body","h2",…)
  bool idWrite = false;   // rightmost compound targets #write
  bool classFences = false;  // .md-fences (community code-fence class)
  bool mdFocus = false;      // .md-focus (community editor's focused/active-block class)
  QString pseudoElement;  // "selection","before","after","marker",… (without ::)
  bool hover = false;
  bool focus = false;
  bool visited = false;
  bool active = false;
  bool nthEven = false;   // :nth-child(even) / :nth-of-type(even)
};

constexpr qreal kUnboundedPageWidth = 100000.0;

// Extract the last compound of a selector (after the final combinator
// space/>/+/~), respecting (...) and [...].
QString lastCompound(const QString& selector) {
  const int n = selector.size();
  int depthParen = 0, depthBrk = 0;
  int lastStart = 0;
  for (int i = 0; i < n; ++i) {
    const QChar c = selector.at(i);
    if (c == QLatin1Char('(')) { ++depthParen; continue; }
    if (c == QLatin1Char(')')) { if (depthParen > 0) --depthParen; continue; }
    if (c == QLatin1Char('[')) { ++depthBrk; continue; }
    if (c == QLatin1Char(']')) { if (depthBrk > 0) --depthBrk; continue; }
    if (depthParen == 0 && depthBrk == 0 &&
        (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n') ||
         c == QLatin1Char('>') || c == QLatin1Char('+') || c == QLatin1Char('~'))) {
      lastStart = i + 1;
    }
  }
  return selector.mid(lastStart).trimmed();
}

SelInfo analyzeSelector(const QString& selector) {
  SelInfo info;
  const QString compound = lastCompound(selector);
  if (compound.isEmpty()) { return info; }
  int i = 0;
  const int n = compound.size();
  // optional leading combinator
  while (i < n && (compound.at(i) == QLatin1Char('>') || compound.at(i) == QLatin1Char('+') ||
                   compound.at(i) == QLatin1Char('~') || compound.at(i).isSpace())) {
    ++i;
  }
  // leading type selector (tag)
  int tagEnd = i;
  while (tagEnd < n) {
    const QChar c = compound.at(tagEnd);
    if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) { ++tagEnd; } else { break; }
  }
  if (tagEnd > i) { info.tag = compound.mid(i, tagEnd - i).toLower(); }
  i = tagEnd;
  // remaining simple selectors
  while (i < n) {
    const QChar c = compound.at(i);
    if (c == QLatin1Char('#')) {
      int j = ++i;
      while (j < n && (compound.at(j).isLetterOrNumber() || compound.at(j) == QLatin1Char('-') ||
                       compound.at(j) == QLatin1Char('_'))) { ++j; }
      if (compound.mid(i, j - i).toLower() == QStringLiteral("write")) { info.idWrite = true; }
      i = j;
    } else if (c == QLatin1Char('.')) {
      int j = ++i;
      while (j < n && (compound.at(j).isLetterOrNumber() || compound.at(j) == QLatin1Char('-') ||
                       compound.at(j) == QLatin1Char('_'))) { ++j; }
      const QString cls = compound.mid(i, j - i).toLower();
      if (cls == QStringLiteral("md-fences")) { info.classFences = true; }
      else if (cls == QStringLiteral("md-focus")) { info.mdFocus = true; }
      i = j;
    } else if (c == QLatin1Char(':')) {
      bool element = (i + 1 < n && compound.at(i + 1) == QLatin1Char(':'));
      int j = element ? i + 2 : i + 1;
      int nameStart = j;
      while (j < n && (compound.at(j).isLetterOrNumber() || compound.at(j) == QLatin1Char('-'))) { ++j; }
      const QString name = compound.mid(nameStart, j - nameStart).toLower();
      // optional (...) argument
      QString arg;
      if (j < n && compound.at(j) == QLatin1Char('(')) {
        int close = compound.indexOf(QLatin1Char(')'), j);
        arg = compound.mid(j + 1, (close < 0 ? n : close) - (j + 1)).trimmed().toLower();
        j = (close < 0 ? n : close + 1);
      }
      if (element) {
        if (info.pseudoElement.isEmpty()) { info.pseudoElement = name; }
      } else {
        // CSS2 allowed single-colon pseudo-elements (:before/:after). Community
        // themes still use that spelling, so normalize it to the same decoration
        // channel as ::before/::after instead of treating it as an ignored state.
        if ((name == QStringLiteral("before") || name == QStringLiteral("after") ||
             name == QStringLiteral("selection") || name == QStringLiteral("marker")) &&
            info.pseudoElement.isEmpty()) {
          info.pseudoElement = name;
        }
        else if (name == QStringLiteral("hover")) { info.hover = true; }
        else if (name == QStringLiteral("focus")) { info.focus = true; }
        else if (name == QStringLiteral("visited")) { info.visited = true; }
        else if (name == QStringLiteral("active")) { info.active = true; }
        else if ((name == QStringLiteral("nth-child") || name == QStringLiteral("nth-of-type")) &&
                 (arg == QStringLiteral("even") || arg.contains(QStringLiteral("2n")))) {
          info.nthEven = true;
        }
      }
      i = j;
    } else if (c == QLatin1Char('[')) {
      int close = compound.indexOf(QLatin1Char(']'), i);
      i = (close < 0 ? n : close + 1);
    } else {
      ++i;
    }
  }
  return info;
}

bool isIdentChar(QChar c) {
  return c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_');
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
    // Export/outline/sidebar shells from community themes are not present in
    // Muffin's live editor DOM. If these selectors enter the semantic cascade as
    // plain `#write` or `h2` rules, their higher specificity lets export-only page
    // sizing and decorations override the live editor style (e.g. `width: 90%`).
    if (cls == QStringLiteral("typora-export") || cls == QStringLiteral("typora-export-sidebar") ||
        cls == QStringLiteral("typora-export-content")) {
      return true;
    }
    i = j - 1;
  }
  return false;
}

// Coarse CSS specificity for the whole selector: (a=#id, b=class/attr/pseudo,
// c=type). Packed so larger == more specific. Good enough for cascade ties
// (themes rarely set the same token on conflicting selectors).
int specificity(const QString& selector) {
  int a = selector.count(QLatin1Char('#'));
  int b = selector.count(QLatin1Char('.')) + selector.count(QLatin1Char('[')) + selector.count(QLatin1Char(':'));
  // type selectors: a letter starting a compound (after a combinator or at start),
  // i.e. not attached to # . : [
  static const QRegularExpression tagRe(QStringLiteral("(^|[\\s>+~])[a-zA-Z]"));
  int c = 0;
  auto it = tagRe.globalMatch(selector);
  while (it.hasNext()) { ++c; it.next(); }
  return a * 10000 + b * 100 + c;
}

struct Candidate {
  QString rawValue;
  bool important = false;
  int spec = 0;
  int order = 0;
};
bool beats(const Candidate& a, const Candidate& b) {
  if (a.important != b.important) { return a.important; }
  if (a.spec != b.spec) { return a.spec > b.spec; }
  return a.order > b.order;
}

// A flat, ordered view of all (rule, selector, declaration) triples from
// non-dark-scoped rules. Pre-analysing selectors lets the extractors query by
// element/property without re-parsing.
struct FlatDecl {
  SelInfo info;
  QString property;  // lowercased
  QString value;     // raw (var() not yet resolved)
  bool important;
  int spec;
  int order;
};

std::vector<FlatDecl> flatten(const CssThemeSheet& sheet) {
  std::vector<FlatDecl> out;
  int order = 0;
  for (const CssRule& rule : sheet.rules()) {
    if (rule.darkScope) { continue; }  // dark @media variant not switched into (v1)
    for (const QString& selector : rule.selectors) {
      if (selectorRequiresExportContext(selector)) { continue; }
      const SelInfo info = analyzeSelector(selector);
      // Interactive / editor-focus states (:hover, :focus, :active, :visited,
      // .md-focus) describe appearances that never apply to a static document
      // render — there is no pointer-over or focused block on the painted page.
      // They also outrank the base rule (higher specificity AND later source
      // order), so without this filter a `code:hover { background:
      // var(--primary-color) }` — a FULL-SATURATION colour meant only for the
      // pointer-over state — becomes the *default* code background. That is the
      // real "headings/code/blockquote turned cyan-purple" regression on the
      // community phycat family: the leak bypasses the color-mix() evaluator
      // entirely, because the winning rule is a plain var(), not a tint. Drop
      // every interactive-state selector from the cascade so the base rule wins.
      if (info.hover || info.focus || info.active || info.visited || info.mdFocus) { continue; }
      const int spec = specificity(selector);
      for (const CssDeclaration& decl : rule.declarations) {
        FlatDecl fd;
        fd.info = info;
        fd.property = decl.property;
        fd.value = decl.value;
        fd.important = decl.important;
        fd.spec = spec;
        fd.order = order++;
        out.push_back(std::move(fd));
      }
    }
  }
  return out;
}

// Best raw (un-resolved) value among declarations matching one of `properties`
// whose selector satisfies `target`. Returns empty if none.
QString bestValue(const std::vector<FlatDecl>& flat, const std::vector<QString>& properties,
                  const std::function<bool(const SelInfo&)>& target) {
  Candidate best;
  bool have = false;
  for (const FlatDecl& fd : flat) {
    if (!target(fd.info)) { continue; }
    if (std::find(properties.begin(), properties.end(), fd.property) == properties.end()) { continue; }
    Candidate c{fd.value, fd.important, fd.spec, fd.order};
    if (!have || beats(c, best)) { best = c; have = true; }
  }
  return have ? best.rawValue : QString();
}

// Parse one rgb/rgba/hsl/hsla functional-colour argument group into a QColor.
// QColor's string ctor is unreliable for rgba() with a float alpha across Qt
// versions, so the numeric path is the source of truth for functional notation.
qreal channel255(const QString& part) {
  QString t = part.trimmed();
  const bool pct = t.endsWith(QLatin1Char('%'));
  if (pct) { t.chop(1); }
  bool ok = false;
  const double v = t.toDouble(&ok);
  if (!ok) { return -1.0; }
  return pct ? v / 100.0 * 255.0 : v;
}
QColor parseFunctionalColor(const QString& fn, const QString& args) {
  const QStringList parts = CssThemeParser::splitTopLevelCommas(args);
  auto alphaOf = [](const QString& p) -> int {
    QString a = p.trimmed();
    const bool pct = a.endsWith(QLatin1Char('%'));
    if (pct) { a.chop(1); }
    bool ok = false;
    const double av = a.toDouble(&ok);
    if (!ok) { return 255; }
    return qBound(0, qRound(pct ? av / 100.0 * 255.0 : (av <= 1.0 ? av * 255.0 : av)), 255);
  };
  if (fn == QStringLiteral("rgb") || fn == QStringLiteral("rgba")) {
    if (parts.size() < 3) { return QColor(); }
    const double r = channel255(parts[0]);
    const double g = channel255(parts[1]);
    const double b = channel255(parts[2]);
    if (r < 0 || g < 0 || b < 0) { return QColor(); }
    const int alpha = parts.size() >= 4 ? alphaOf(parts[3]) : 255;
    return QColor(qBound(0, qRound(r), 255), qBound(0, qRound(g), 255), qBound(0, qRound(b), 255), alpha);
  }
  if (fn == QStringLiteral("hsl") || fn == QStringLiteral("hsla")) {
    if (parts.size() < 3) { return QColor(); }
    QString h0 = parts[0].trimmed();
    if (h0.endsWith(QStringLiteral("deg"))) { h0.chop(3); }
    bool ok = false;
    const double h = h0.toDouble(&ok);
    if (!ok) { return QColor(); }
    auto sl = [](const QString& p) -> double {
      QString t = p.trimmed();
      if (t.endsWith(QLatin1Char('%'))) { t.chop(1); }
      return t.toDouble();
    };
    const double s = sl(parts[1]);
    const double l = sl(parts[2]);
    QColor c = QColor::fromHslF(h / 360.0, qBound(0.0, s / 100.0, 1.0), qBound(0.0, l / 100.0, 1.0));
    c.setAlpha(parts.size() >= 4 ? alphaOf(parts[3]) : 255);
    return c;
  }
  return QColor();
}

// --- color-mix() (CSS Color 4) ------------------------------------------------
// Community themes tint backgrounds/borders with color-mix(in srgb, <color>,
// transparent N%). It was previously unimplemented, so extractColor fell
// through to the #hex scanner and grabbed the operand verbatim — e.g. a code
// background of color-mix(in srgb, var(--primary-color), transparent 90%)
// became FULL-SATURATION #00f3ff instead of a 10% cyan wash, and a blockquote
// border became full #bd93f9 (the "headings/code/blockquote turned cyan-purple"
// regression). We evaluate the premultiplied blend per spec and KEEP alpha, so
// the painter composites the tint over the page background correctly. Only
// `in srgb` (and any other space, treated as srgb — good enough for community
// themes, which only ever use srgb) is supported; hue-interpolation hints are
// accepted and ignored.

// Forward decl: operands recurse through the full colour parser (hex/rgb/named/
// var() — and even a nested color-mix()).
QColor extractColor(const QString& value, const QHash<QString, QString>& vars);

// Locate the first balanced color-mix(...) call in `s`; return its inner args
// (between the parens), or empty. Balanced because operands may themselves
// contain parentheses (rgba(), nested color-mix()). Works whether the call is
// the whole value or embedded in a shorthand like `1px solid color-mix(...)`.
QString findColorMixInner(const QString& s) {
  const int at = s.indexOf(QStringLiteral("color-mix("), 0, Qt::CaseInsensitive);
  if (at < 0) { return QString(); }
  const int openParen = at + QStringLiteral("color-mix").length();  // index of '('
  int depth = 0;
  for (int i = openParen; i < s.size(); ++i) {
    const QChar ch = s.at(i);
    if (ch == QLatin1Char('(')) {
      ++depth;
    } else if (ch == QLatin1Char(')')) {
      --depth;
      if (depth == 0) { return s.mid(openParen + 1, i - openParen - 1); }
    }
  }
  return QString();  // unbalanced — leave to the legacy fallback
}

// Resolve one color-mix operand. `transparent` is a valid zero-alpha colour,
// but the legacy word scanner skips it, so handle it explicitly here.
QColor operandColor(const QString& token, const QHash<QString, QString>& vars) {
  const QString t = token.trimmed();
  if (t.compare(QStringLiteral("transparent"), Qt::CaseInsensitive) == 0) { return QColor(0, 0, 0, 0); }
  if (t.compare(QStringLiteral("currentcolor"), Qt::CaseInsensitive) == 0) { return QColor(); }  // unresolvable
  return extractColor(t, vars);
}

// Premultiplied blend of two colours by normalised weights; alpha preserved
// so the result composites correctly when painted over the page.
QColor blendColors(const QColor& c1, const QColor& c2, double w1, double w2) {
  const double a1 = c1.alphaF();
  const double a2 = c2.alphaF();
  const double ra = w1 * a1 + w2 * a2;
  if (ra <= 0.0) { return QColor(0, 0, 0, 0); }
  const auto channel = [&](int v1, int v2) {
    const double prem = w1 * (v1 / 255.0) * a1 + w2 * (v2 / 255.0) * a2;  // premultiplied
    return qBound(0, qRound(prem / ra * 255.0), 255);                      // un-premultiply
  };
  return QColor(channel(c1.red(), c2.red()), channel(c1.green(), c2.green()),
                channel(c1.blue(), c2.blue()), qBound(0, qRound(ra * 255.0), 255));
}

struct MixOperand { QColor color; double pct; };  // pct = -1 when no percentage given

// Parse one operand group "<color> [<p>%]" → colour + optional weight.
MixOperand readMixOperand(const QString& group, const QHash<QString, QString>& vars) {
  QString t = group.trimmed();
  double pct = -1.0;
  static const QRegularExpression trailingPct(QStringLiteral("\\s+(-?[0-9]+(?:\\.[0-9]+)?)%\\s*$"));
  const QRegularExpressionMatch m = trailingPct.match(t);
  if (m.hasMatch()) {
    pct = m.captured(1).toDouble();
    t = t.left(m.capturedStart()).trimmed();
  }
  return {operandColor(t, vars), pct};
}

// Parse "in <space>[, <hint>], c1 [p1%], c2 [p2%]" and return the blend.
QColor parseColorMix(const QString& inner, const QHash<QString, QString>& vars) {
  const QStringList groups = CssThemeParser::splitTopLevelCommas(inner);
  int idx = 0;
  // Optional leading interpolation spec: `in srgb`, `in oklch longer hue`, …
  if (idx < groups.size() && groups[idx].trimmed().startsWith(QStringLiteral("in"), Qt::CaseInsensitive)) {
    ++idx;
  }
  if (groups.size() - idx < 2) { return QColor(); }
  const MixOperand a = readMixOperand(groups[idx], vars);
  const MixOperand b = readMixOperand(groups[idx + 1], vars);
  if (!a.color.isValid() || !b.color.isValid()) { return QColor(); }
  double w1, w2;
  if (a.pct < 0 && b.pct < 0) {
    w1 = w2 = 0.5;
  } else if (a.pct < 0) {
    w2 = qBound(0.0, b.pct / 100.0, 1.0);
    w1 = 1.0 - w2;
  } else if (b.pct < 0) {
    w1 = qBound(0.0, a.pct / 100.0, 1.0);
    w2 = 1.0 - w1;
  } else {
    const double s = a.pct + b.pct;
    if (s <= 0.0) { w1 = w2 = 0.5; } else { w1 = a.pct / s; w2 = b.pct / s; }
  }
  return blendColors(a.color, b.color, w1, w2);
}

QColor extractColor(const QString& value, const QHash<QString, QString>& vars) {
  const QString resolved = CssThemeParser::resolveVars(value, vars).trimmed();
  if (resolved.isEmpty()) { return QColor(); }
  // color-mix() — evaluate before the #hex scanner can grab the operand verbatim.
  if (const QString mixInner = findColorMixInner(resolved); !mixInner.isEmpty()) {
    if (QColor mixed = parseColorMix(mixInner, vars); mixed.isValid()) { return mixed; }
  }
  QColor direct(resolved);
  if (direct.isValid()) { return direct; }
  // Functional rgb()/rgba()/hsl()/hsla() — parsed numerically.
  static const QRegularExpression funcRe(QStringLiteral("(rgba?|hsla?)\\(([^)]*)\\)"),
                                         QRegularExpression::CaseInsensitiveOption);
  auto fit = funcRe.globalMatch(resolved);
  while (fit.hasNext()) {
    const QRegularExpressionMatch m = fit.next();
    QColor c = parseFunctionalColor(m.captured(1).toLower(), m.captured(2));
    if (c.isValid()) { return c; }
  }
  // #hex token inside a shorthand (e.g. `1px solid #d0d7de`).
  static const QRegularExpression hexRe(QStringLiteral("#[0-9a-fA-F]{3,8}"));
  auto hit = hexRe.globalMatch(resolved);
  while (hit.hasNext()) {
    QColor c(hit.next().captured(0));
    if (c.isValid()) { return c; }
  }
  // Bare named colour word (e.g. `red`) — last resort, skipping style keywords.
  static const QRegularExpression wordRe(QStringLiteral("[a-zA-Z]+"));
  auto wit = wordRe.globalMatch(resolved);
  while (wit.hasNext()) {
    const QString w = wit.next().captured(0);
    if (w == QStringLiteral("solid") || w == QStringLiteral("dashed") || w == QStringLiteral("dotted") ||
        w == QStringLiteral("none") || w == QStringLiteral("double") || w == QStringLiteral("ridge") ||
        w == QStringLiteral("groove") || w == QStringLiteral("outset") || w == QStringLiteral("inset") ||
        w == QStringLiteral("transparent") || w == QStringLiteral("currentColor")) {
      continue;
    }
    QColor c(w);
    if (c.isValid()) { return c; }
  }
  return QColor();
}

QColor colorToken(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars,
                  const std::vector<QString>& properties, const std::function<bool(const SelInfo&)>& target) {
  return extractColor(bestValue(flat, properties, target), vars);
}

// CSS font-family list, quotes stripped, joined with '\n' so RenderTheme can
// preserve the full fallback order instead of keeping only the first family.
QString firstFamily(const QString& value, const QHash<QString, QString>& vars) {
  const QString resolved = CssThemeParser::resolveVars(value, vars).trimmed();
  const QStringList parts = CssThemeParser::splitTopLevelCommas(resolved);
  QStringList out;
  for (const QString& p : parts) {
    QString f = p.trimmed();
    if (f.size() >= 2 && f.front() == QLatin1Char('"') && f.back() == QLatin1Char('"')) { f = f.mid(1, f.size() - 2); }
    if (f.size() >= 2 && f.front() == QLatin1Char('\'') && f.back() == QLatin1Char('\'')) { f = f.mid(1, f.size() - 2); }
    f = f.trimmed();
    if (!f.isEmpty()) { out << f; }
  }
  return out.join(QLatin1Char('\n'));
}

bool fontStackLooksSerif(const QString& stack) {
  for (const QString& raw : stack.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    const QString f = raw.trimmed().toLower();
    if (f == QStringLiteral("serif") || f.contains(QStringLiteral("serif")) ||
        f.contains(QStringLiteral("times")) || f.contains(QStringLiteral("palatino")) ||
        f.contains(QStringLiteral("georgia")) || f.contains(QStringLiteral("garamond")) ||
        f.contains(QStringLiteral("baskerville")) || f.contains(QStringLiteral("vollkorn")) ||
        f.contains(QStringLiteral("cambria"))) {
      return true;
    }
  }
  return false;
}

QStringList splitTopLevelSpaces(const QString& text) {
  QStringList out;
  QString cur;
  int paren = 0;
  bool inString = false;
  QChar quote;
  for (int i = 0; i < text.size(); ++i) {
    const QChar c = text.at(i);
    if (inString) {
      cur += c;
      if (c == quote) { inString = false; }
      continue;
    }
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inString = true; quote = c; cur += c; continue; }
    if (c == QLatin1Char('(')) { ++paren; cur += c; continue; }
    if (c == QLatin1Char(')')) { if (paren > 0) --paren; cur += c; continue; }
    if (paren == 0 && c.isSpace()) {
      if (!cur.trimmed().isEmpty()) { out << cur.trimmed(); cur.clear(); }
    } else {
      cur += c;
    }
  }
  if (!cur.trimmed().isEmpty()) { out << cur.trimmed(); }
  return out;
}

qreal pxToPt(qreal px) { return px * 72.0 / 96.0; }
qreal ptToPx(qreal pt) { return pt * 96.0 / 72.0; }

// CSS length → points. Handles px/rem/em/%/pt/numbers relative to the supplied em size.
qreal lengthToPt(const QString& value, const QHash<QString, QString>& vars, qreal emPx = 16.0) {
  const QString resolved = CssThemeParser::resolveVars(value, vars).trimmed();
  if (resolved.isEmpty()) { return 0.0; }
  // Parse leading number (optional sign, digits, decimal point).
  int i = 0;
  while (i < resolved.size() && (resolved.at(i).isDigit() || resolved.at(i) == QLatin1Char('.') ||
                                 resolved.at(i) == QLatin1Char('-') || resolved.at(i) == QLatin1Char('+'))) {
    ++i;
  }
  const QString numStr = resolved.left(i);
  const QString unit = resolved.mid(i).trimmed().toLower();
  bool ok = false;
  const qreal n = numStr.toDouble(&ok);
  if (!ok) { return 0.0; }
  if (unit == QStringLiteral("px")) { return pxToPt(n); }
  if (unit == QStringLiteral("pt")) { return n; }
  if (unit == QStringLiteral("rem")) { return pxToPt(n * 16.0); }
  if (unit == QStringLiteral("em")) { return pxToPt(n * emPx); }
  if (unit == QStringLiteral("%")) { return pxToPt(n / 100.0 * emPx); }
  if (unit.isEmpty()) { return pxToPt(n); }  // bare number → treat as px
  return 0.0;
}

qreal lengthToPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx = 16.0, qreal rootPx = -1.0) {
  const QString resolved = CssThemeParser::resolveVars(value, vars).trimmed();
  if (resolved.isEmpty() || resolved == QStringLiteral("auto")) { return 0.0; }
  int i = 0;
  while (i < resolved.size() && (resolved.at(i).isDigit() || resolved.at(i) == QLatin1Char('.') ||
                                 resolved.at(i) == QLatin1Char('-') || resolved.at(i) == QLatin1Char('+'))) {
    ++i;
  }
  bool ok = false;
  const qreal n = resolved.left(i).toDouble(&ok);
  if (!ok) { return 0.0; }
  const QString unit = resolved.mid(i).trimmed().toLower();
  if (unit == QStringLiteral("px") || unit.isEmpty()) { return n; }
  if (unit == QStringLiteral("pt")) { return n * 96.0 / 72.0; }
  if (unit == QStringLiteral("em")) { return n * emPx; }
  if (unit == QStringLiteral("rem")) { return n * (rootPx > 0.0 ? rootPx : emPx); }  // rem is root-relative
  if (unit == QStringLiteral("%")) { return n / 100.0 * emPx; }
  return 0.0;
}

bool isIntrinsicPageWidthKeyword(const QString& resolved) {
  return resolved == QLatin1String("max-content") || resolved == QLatin1String("fit-content") ||
         resolved == QLatin1String("min-content") || resolved == QLatin1String("none") ||
         resolved == QLatin1String("available") || resolved == QLatin1String("stretch");
}

qreal pageWidthToPxOrSentinel(const QString& raw, const QHash<QString, QString>& vars, qreal emPx) {
  const QString resolved = CssThemeParser::resolveVars(raw, vars).trimmed().toLower();
  if (resolved.isEmpty() || resolved == QLatin1String("auto")) { return 0.0; }
  // Page width percentages are containing-block-relative CSS lengths. The generic
  // lengthToPx() intentionally treats percentages as em-relative for local boxes;
  // using it here turns `width: 90%` into ~14px and collapses #write. The layout
  // already clamps this sentinel to the viewport, which is the right approximation
  // for fill/percent/intrinsic page widths in Muffin's current page model.
  if (resolved.endsWith(QLatin1Char('%')) || isIntrinsicPageWidthKeyword(resolved)) {
    return kUnboundedPageWidth;
  }
  return lengthToPx(resolved, vars, emPx);
}

QMarginsF boxToMarginsPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx = 16.0, qreal rootPx = -1.0) {
  const QStringList parts = splitTopLevelSpaces(CssThemeParser::resolveVars(value, vars));
  if (parts.isEmpty()) { return QMarginsF(); }
  qreal v[4] = {};
  for (int i = 0; i < qMin(4, parts.size()); ++i) { v[i] = lengthToPx(parts.at(i), vars, emPx, rootPx); }
  qreal top = v[0], right = v[0], bottom = v[0], left = v[0];
  if (parts.size() == 2) { right = left = v[1]; }
  else if (parts.size() == 3) { right = left = v[1]; bottom = v[2]; }
  else if (parts.size() >= 4) { right = v[1]; bottom = v[2]; left = v[3]; }
  return QMarginsF(left, top, right, bottom);
}

// A box (margin/padding) resolved from its shorthand plus any per-side longhand
// overrides (padding-top, margin-bottom, …). Themes commonly set the shorthand
// and then one or two longhands (e.g. github's `padding: 30px; padding-bottom:
// 100px`), so reading only the shorthand silently dropped those sides. `present`
// is true when the theme declared the box at all — needed to tell an explicit
// zero margin apart from "no margin rule" (both parse to a null QMarginsF).
struct Box { QMarginsF margins; bool present; };
Box readBox(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars,
            const std::function<bool(const SelInfo&)>& target, const QString& shorthand, qreal emPx, qreal rootPx = -1.0) {
  qreal top = 0.0, right = 0.0, bottom = 0.0, left = 0.0;
  bool present = false;
  const QString sh = bestValue(flat, {shorthand}, target);
  if (!sh.isEmpty()) {
    const QMarginsF b = boxToMarginsPx(sh, vars, emPx, rootPx);
    top = b.top(); right = b.right(); bottom = b.bottom(); left = b.left();
    present = true;
  }
  const auto side = [&](const QString& prop, qreal& dst) {
    const QString v = bestValue(flat, {prop}, target);
    if (!v.isEmpty()) { dst = lengthToPx(v, vars, emPx, rootPx); present = true; }
  };
  side(shorthand + QStringLiteral("-top"), top);
  side(shorthand + QStringLiteral("-right"), right);
  side(shorthand + QStringLiteral("-bottom"), bottom);
  side(shorthand + QStringLiteral("-left"), left);
  return {QMarginsF(left, top, right, bottom), present};
}

qreal borderWidthPx(const QString& value, const QHash<QString, QString>& vars, qreal emPx = 16.0) {
  for (const QString& part : splitTopLevelSpaces(CssThemeParser::resolveVars(value, vars))) {
    const qreal px = lengthToPx(part, vars, emPx);
    if (px > 0.0) { return px; }
  }
  return 0.0;
}

Qt::Alignment parseTextAlign(const QString& raw, const QHash<QString, QString>& vars) {
  const QString v = CssThemeParser::resolveVars(raw, vars).trimmed().toLower();
  if (v == QStringLiteral("left") || v == QStringLiteral("start")) { return Qt::AlignLeft; }
  if (v == QStringLiteral("right") || v == QStringLiteral("end")) { return Qt::AlignRight; }
  if (v == QStringLiteral("center")) { return Qt::AlignHCenter; }
  if (v == QStringLiteral("justify")) { return Qt::AlignJustify; }
  return Qt::Alignment();
}

struct ParsedFontWeight { int weight = 0; bool present = false; };
ParsedFontWeight parseFontWeight(const QString& raw, const QHash<QString, QString>& vars) {
  const QString v = CssThemeParser::resolveVars(raw, vars).trimmed().toLower();
  if (v.isEmpty()) { return {}; }
  if (v == QStringLiteral("normal")) { return {QFont::Normal, true}; }
  if (v == QStringLiteral("bold") || v == QStringLiteral("bolder")) { return {QFont::Bold, true}; }
  if (v == QStringLiteral("lighter")) { return {QFont::Light, true}; }
  bool ok = false;
  const int numeric = v.toInt(&ok);
  if (ok) { return {qBound(static_cast<int>(QFont::Thin), numeric, static_cast<int>(QFont::Black)), true}; }
  return {};
}

struct ParsedItalic { bool italic = false; bool present = false; };
ParsedItalic parseFontItalic(const QString& raw, const QHash<QString, QString>& vars) {
  const QString v = CssThemeParser::resolveVars(raw, vars).trimmed().toLower();
  if (v == QStringLiteral("italic") || v == QStringLiteral("oblique")) { return {true, true}; }
  if (v == QStringLiteral("normal")) { return {false, true}; }
  return {};
}

qreal parseLineHeightMultiplier(const QString& raw, const QHash<QString, QString>& vars, qreal fontPx) {
  const QString v = CssThemeParser::resolveVars(raw, vars).trimmed().toLower();
  if (v.isEmpty() || v == QStringLiteral("normal")) { return 0.0; }
  bool ok = false;
  const qreal n = v.toDouble(&ok);
  if (ok && n > 0.0) { return n; }
  if (fontPx <= 0.0) { return 0.0; }
  const qreal px = lengthToPx(v, vars, fontPx);
  return px > 0.0 ? px / fontPx : 0.0;
}

qreal headingEmPx(const ThemeTypography& ty, int level, qreal bodyPx) {
  const int idx = qBound(0, level - 1, 5);
  if (ty.headingSizePt[idx] > 0.0) { return ptToPx(ty.headingSizePt[idx]); }
  return bodyPx > 0.0 ? bodyPx : 16.0;
}

QString varValue(const QHash<QString, QString>& vars, const char* name) {
  return CssThemeParser::resolveVars(vars.value(QString::fromLatin1(name)), vars).trimmed();
}

QColor varColor(const QHash<QString, QString>& vars, const char* name) {
  const QString v = varValue(vars, name);
  if (v.isEmpty()) { return QColor(); }
  QColor c(v);
  return c.isValid() ? c : QColor();
}

bool truthy(const QString& v) {
  const QString t = v.trimmed().toLower();
  return t == QStringLiteral("1") || t == QStringLiteral("true") || t == QStringLiteral("yes") ||
         t == QStringLiteral("on");
}

QString titleCaseId(const QString& id) {
  if (id.isEmpty()) { return id; }
  QString out;
  bool cap = true;
  for (const QChar c : id) {
    if (c == QLatin1Char('-') || c == QLatin1Char('_') || c.isSpace()) { cap = true; out += QLatin1Char(' '); continue; }
    out += cap ? c.toUpper() : c;
    cap = false;
  }
  return out;
}

// Target predicates ----------------------------------------------------------
// Container predicates match the bare element, NOT its ::before/::after
// decorative overlays. Fancy themes paint a texture/watermark layer on
// #write::before (translucent gradient, masked SVG, …) whose background-colour
// is a vivid tint, not the page fill. Matching it leaked that tint into
// pageBackground — turning the page, status bar and derived chrome purple on
// such themes (e.g. a community theme whose #write::before uses a purple
// --texture-mask-color). pseudoElement must be empty for a container match.
bool isHtmlOrBody(const SelInfo& s) {
  return (s.tag == QStringLiteral("html") || s.tag == QStringLiteral("body")) && s.pseudoElement.isEmpty();
}
bool isWrite(const SelInfo& s) {
  return s.idWrite && s.pseudoElement.isEmpty();
}
bool isParagraphText(const SelInfo& s) { return s.tag == QStringLiteral("p") || s.tag == QStringLiteral("li"); }
bool isBodyLike(const SelInfo& s) {
  return isHtmlOrBody(s) || isWrite(s) || isParagraphText(s);
}
// The document text colour: html/body paint the viewport, but the content text
// often lives on #write (the content container, e.g. the pixyll theme) and
// inherits down. Reading #write here resolves themes that never colour body.
bool isDocumentContainer(const SelInfo& s) {
  return isHtmlOrBody(s) || isWrite(s);
}
bool isLink(const SelInfo& s) {
  return s.tag == QStringLiteral("a") && !s.hover && !s.focus && !s.visited && !s.active;
}
bool isAnyHeading(const SelInfo& s) {
  return s.tag.size() == 2 && s.tag.at(0) == QLatin1Char('h') &&
         s.tag.at(1) >= QLatin1Char('1') && s.tag.at(1) <= QLatin1Char('6') && !s.hover;
}
bool isHeading(const SelInfo& s, int level) {
  return s.tag == (QStringLiteral("h") + QString::number(level)) && !s.hover;
}
bool isInlineCode(const SelInfo& s) {
  return s.tag == QStringLiteral("code") || s.tag == QStringLiteral("tt") || s.tag == QStringLiteral("kbd");
}
bool isCodeBlock(const SelInfo& s) {
  return s.tag == QStringLiteral("pre") || s.classFences;
}
bool isBlockquote(const SelInfo& s) { return s.tag == QStringLiteral("blockquote"); }
bool isTable(const SelInfo& s) {
  return s.tag == QStringLiteral("table") || s.tag == QStringLiteral("th") || s.tag == QStringLiteral("td");
}
bool isThOrHead(const SelInfo& s) {
  return s.tag == QStringLiteral("th") || s.tag == QStringLiteral("thead");
}
bool isAltRow(const SelInfo& s) { return s.nthEven && s.tag == QStringLiteral("tr"); }
bool isMark(const SelInfo& s) { return s.tag == QStringLiteral("mark"); }
bool isSelection(const SelInfo& s) { return s.pseudoElement == QStringLiteral("selection"); }

// --- CSS gradient parsing (linear/radial) → GradientSpec ---------------------
// Reuses extractColor (handles var/color-mix/rgb/hex/named), so stop colours
// resolve through the same path as every other theme colour. Carried as
// rect-independent data; GradientPainter builds a QGradient per target rect.

QString gradientParenInner(const QString& s) {
  const int open = s.indexOf(QLatin1Char('('));
  if (open < 0) { return s; }
  int depth = 0;
  for (int i = open; i < s.size(); ++i) {
    const QChar c = s.at(i);
    if (c == QLatin1Char('(')) { ++depth; }
    else if (c == QLatin1Char(')')) { --depth; if (depth == 0) { return s.mid(open + 1, i - open - 1); } }
  }
  return s.mid(open + 1);
}

bool gradientPartIsColor(const QString& part, const QHash<QString, QString>& vars) {
  if (part.contains(QStringLiteral("transparent"), Qt::CaseInsensitive)) { return true; }
  return extractColor(part, vars).isValid();
}

GradientStop parseGradientStop(const QString& part, const QHash<QString, QString>& vars) {
  GradientStop s;
  if (part.contains(QStringLiteral("transparent"), Qt::CaseInsensitive)) {
    s.color = QColor(Qt::transparent);
  } else {
    s.color = extractColor(part, vars);
  }
  s.position = -1.0;  // unset marker
  static const QRegularExpression pctRe(QStringLiteral("(^|\\s)([0-9.]+)%"));
  const QRegularExpressionMatch m = pctRe.match(part);
  if (m.hasMatch()) { s.position = m.captured(2).toDouble() / 100.0; }
  return s;
}

void assignImplicitStopPositions(std::vector<GradientStop>& stops) {
  if (stops.empty()) { return; }
  if (stops.front().position < 0.0) { stops.front().position = 0.0; }
  if (stops.back().position < 0.0) { stops.back().position = 1.0; }
  for (size_t i = 0; i < stops.size(); ++i) {
    if (stops[i].position >= 0.0) { continue; }
    size_t lo = i;
    while (lo > 0 && stops[lo].position < 0.0) { --lo; }
    size_t hi = i;
    while (hi + 1 < stops.size() && stops[hi].position < 0.0) { ++hi; }
    const qreal plo = stops[lo].position;
    const qreal phi = stops[hi].position;
    const qreal count = static_cast<qreal>(hi - lo);
    stops[i].position = plo + (phi - plo) * static_cast<qreal>(i - lo) / count;
  }
  for (auto& s : stops) { s.position = qBound(0.0, s.position, 1.0); }
}

void parseGradientDirection(const QString& part, GradientSpec& spec) {
  const QString p = part.trimmed().toLower();
  if (spec.kind == GradientSpec::Kind::Linear) {
    bool ok = false;
    if (p.endsWith(QStringLiteral("deg"))) {
      const qreal a = p.left(p.size() - 3).trimmed().toDouble(&ok);
      if (ok) { spec.angleDeg = a; }
    } else if (p.endsWith(QStringLiteral("rad"))) {
      const qreal a = p.left(p.size() - 3).trimmed().toDouble(&ok);
      if (ok) { spec.angleDeg = qRadiansToDegrees(a); }
    } else if (p.startsWith(QStringLiteral("to "))) {
      const QStringList sides = p.mid(3).split(QLatin1Char(' '), Qt::SkipEmptyParts);
      qreal dx = 0.0, dy = 0.0;
      for (const QString& s : sides) {
        if (s == QStringLiteral("top")) { dy = -1.0; }
        else if (s == QStringLiteral("bottom")) { dy = 1.0; }
        else if (s == QStringLiteral("left")) { dx = -1.0; }
        else if (s == QStringLiteral("right")) { dx = 1.0; }
      }
      if (dx != 0.0 || dy != 0.0) {
        qreal a = qRadiansToDegrees(qAtan2(dx, -dy));  // CSS: 0=to top, clockwise
        if (a < 0.0) { a += 360.0; }
        spec.angleDeg = a;
      }
    }
  } else if (spec.kind == GradientSpec::Kind::Radial) {
    QString pos;
    const int atIdx = p.indexOf(QStringLiteral("at"));
    if (atIdx >= 0) { pos = p.mid(atIdx + 2).trimmed(); }
    static const QHash<QString, QPointF> named = {
        {QStringLiteral("center"), QPointF(0.5, 0.5)}, {QStringLiteral("centre"), QPointF(0.5, 0.5)},
        {QStringLiteral("top"), QPointF(0.5, 0.0)}, {QStringLiteral("bottom"), QPointF(0.5, 1.0)},
        {QStringLiteral("left"), QPointF(0.0, 0.5)}, {QStringLiteral("right"), QPointF(1.0, 0.5)},
        {QStringLiteral("top left"), QPointF(0.0, 0.0)}, {QStringLiteral("left top"), QPointF(0.0, 0.0)},
        {QStringLiteral("top right"), QPointF(1.0, 0.0)}, {QStringLiteral("right top"), QPointF(1.0, 0.0)},
        {QStringLiteral("bottom left"), QPointF(0.0, 1.0)}, {QStringLiteral("left bottom"), QPointF(0.0, 1.0)},
        {QStringLiteral("bottom right"), QPointF(1.0, 1.0)}, {QStringLiteral("right bottom"), QPointF(1.0, 1.0)}};
    if (!pos.isEmpty()) {
      if (named.contains(pos)) {
        spec.radialCenter = named.value(pos);
      } else if (pos.contains(QLatin1Char('%'))) {
        const QStringList xy = pos.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (xy.size() == 2 && xy.at(0).endsWith(QLatin1Char('%')) && xy.at(1).endsWith(QLatin1Char('%'))) {
          bool okx = false, oky = false;
          const qreal fx = xy.at(0).left(xy.at(0).size() - 1).toDouble(&okx) / 100.0;
          const qreal fy = xy.at(1).left(xy.at(1).size() - 1).toDouble(&oky) / 100.0;
          if (okx && oky) { spec.radialCenter = QPointF(fx, fy); }
        }
      } else {
        // Word combination: "center bottom", "top left", … Start centred and
        // apply each keyword (e.g. phycat's h2 `ellipse at center bottom`).
        QPointF c(0.5, 0.5);
        for (const QString& w : pos.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
          if (w == QStringLiteral("top")) { c.setY(0.0); }
          else if (w == QStringLiteral("bottom")) { c.setY(1.0); }
          else if (w == QStringLiteral("left")) { c.setX(0.0); }
          else if (w == QStringLiteral("right")) { c.setX(1.0); }
        }
        spec.radialCenter = c;
      }
    }
    if (p.contains(QStringLiteral("closest-side"))) { spec.radialRadius = 0.5; }
    else if (p.contains(QStringLiteral("farthest-side"))) { spec.radialRadius = 1.0; }
    else if (p.contains(QStringLiteral("farthest-corner"))) { spec.radialRadius = 0.7071; }
  }
}

GradientSpec parseGradientSpec(const QString& raw, const QHash<QString, QString>& vars) {
  GradientSpec spec;
  const QString resolved = CssThemeParser::resolveVars(raw, vars).trimmed();
  if (resolved.isEmpty()) { return spec; }
  QString inner;
  const auto startsWithKw = [&](const char* kw) {
    return resolved.startsWith(QString::fromLatin1(kw), Qt::CaseInsensitive);
  };
  if (startsWithKw("linear-gradient")) { spec.kind = GradientSpec::Kind::Linear; inner = gradientParenInner(resolved); }
  else if (startsWithKw("radial-gradient")) { spec.kind = GradientSpec::Kind::Radial; inner = gradientParenInner(resolved); }
  else { return spec; }
  const QStringList parts = CssThemeParser::splitTopLevelCommas(inner);
  if (parts.isEmpty()) { spec.kind = GradientSpec::Kind::None; return spec; }
  int idx = 0;
  if (!parts.first().trimmed().isEmpty() && !gradientPartIsColor(parts.first(), vars)) {
    parseGradientDirection(parts.first(), spec);
    idx = 1;
  }
  std::vector<GradientStop> stops;
  for (; idx < parts.size(); ++idx) {
    const GradientStop s = parseGradientStop(parts.at(idx), vars);
    if (s.color.isValid()) { stops.push_back(s); }
  }
  if (stops.empty()) { spec.kind = GradientSpec::Kind::None; return spec; }
  assignImplicitStopPositions(stops);
  spec.stops = stops;
  return spec;
}

// --- data: URI decoding (for url(data:image/svg+xml,…) pseudo-element icons) ---

QByteArray decodePercentEncoding(const QString& s) {
  QByteArray out;
  for (int i = 0; i < s.size(); ++i) {
    const QChar c = s.at(i);
    if (c == QLatin1Char('%') && i + 2 < s.size()) {
      bool ok = false;
      const int b = s.mid(i + 1, 2).toInt(&ok, 16);
      if (ok) { out.append(static_cast<char>(b)); i += 2; continue; }
    }
    out.append(c.toLatin1());
  }
  return out;
}

QByteArray extractDataUri(const QString& value) {
  if (value.isEmpty()) { return QByteArray(); }
  static const QRegularExpression re(QStringLiteral("url\\(\\s*['\"]?(data:[^)]*)['\"]?\\s*\\)"),
                                     QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(value);
  if (!m.hasMatch()) { return QByteArray(); }
  const QString dataUri = m.captured(1);
  const int comma = dataUri.indexOf(QLatin1Char(','));
  if (comma < 0) { return QByteArray(); }
  const QString meta = dataUri.mid(5, comma - 5).toLower();  // "image/svg+xml;base64" etc.
  const QString payload = dataUri.mid(comma + 1);
  if (meta.contains(QStringLiteral("base64"))) { return QByteArray::fromBase64(payload.toUtf8()); }
  return decodePercentEncoding(payload);
}

qreal opacityValue(const QString& raw, const QHash<QString, QString>& vars) {
  const QString resolved = CssThemeParser::resolveVars(raw, vars).trimmed();
  bool ok = false;
  const qreal v = resolved.toDouble(&ok);
  return ok ? qBound(0.0, v, 1.0) : 1.0;
}

// --- ::before/::after capture → PseudoElementRule ---------------------------
// flatten() keeps pseudo-element rules; group them by host and resolve each into
// a paint recipe. Host = "#write" | tag | ".md-fences" (skip when unanchored).

QString pseudoHostKey(const SelInfo& info) {
  if (info.idWrite) { return QStringLiteral("#write"); }
  if (!info.tag.isEmpty()) { return info.tag; }
  if (info.classFences) { return QStringLiteral(".md-fences"); }
  return QString();
}

std::vector<FlatDecl> filterPseudoFlat(const std::vector<FlatDecl>& flat, const QString& host, const QString& pseudo) {
  std::vector<FlatDecl> out;
  for (const FlatDecl& fd : flat) {
    if (fd.info.pseudoElement != pseudo) { continue; }
    if (pseudoHostKey(fd.info) != host) { continue; }
    out.push_back(fd);
  }
  return out;
}

std::vector<PseudoElementRule> extractPseudoRules(const std::vector<FlatDecl>& flat,
                                                   const QHash<QString, QString>& vars,
                                                   const std::function<qreal(const QString&)>& emPxForHost) {
  struct Key { QString host; QString pseudo; };
  std::vector<Key> keys;
  const auto seen = [&](const QString& h, const QString& p) {
    for (const Key& k : keys) { if (k.host == h && k.pseudo == p) { return true; } }
    return false;
  };
  for (const FlatDecl& fd : flat) {
    if (fd.info.pseudoElement != QStringLiteral("before") && fd.info.pseudoElement != QStringLiteral("after")) { continue; }
    const QString h = pseudoHostKey(fd.info);
    if (h.isEmpty()) { continue; }
    if (!seen(h, fd.info.pseudoElement)) { keys.push_back({h, fd.info.pseudoElement}); }
  }
  std::vector<PseudoElementRule> out;
  const auto allPred = [](const SelInfo&) { return true; };
  for (const Key& k : keys) {
    const std::vector<FlatDecl> sub = filterPseudoFlat(flat, k.host, k.pseudo);
    if (sub.empty()) { continue; }
    const qreal emPx = emPxForHost(k.host);
    PseudoElementRule rule;
    rule.host = k.host;
    rule.pseudo = k.pseudo;
    rule.present = true;
    const QString contentRaw = bestValue(sub, {QStringLiteral("content")}, allPred);
    QString content = CssThemeParser::resolveVars(contentRaw, vars).trimmed();
    if (content.size() >= 2 && ((content.front() == QLatin1Char('"') && content.back() == QLatin1Char('"')) ||
                                (content.front() == QLatin1Char('\'') && content.back() == QLatin1Char('\'')))) {
      content = content.mid(1, content.size() - 2);
    }
    rule.content = content;
    rule.color = colorToken(sub, vars, {QStringLiteral("color")}, allPred);
    rule.backgroundColor = colorToken(sub, vars, {QStringLiteral("background-color"), QStringLiteral("background")}, allPred);
    const QString bgImg = bestValue(sub, {QStringLiteral("background-image"), QStringLiteral("background")}, allPred);
    rule.background = parseGradientSpec(bgImg, vars);
    const QString maskImg = bestValue(sub, {QStringLiteral("mask-image"), QStringLiteral("-webkit-mask-image"),
                                             QStringLiteral("mask"), QStringLiteral("-webkit-mask")}, allPred);
    rule.maskPattern = parseGradientSpec(maskImg, vars);
    rule.maskTint = rule.backgroundColor.isValid() ? rule.backgroundColor
                                                   : colorToken(sub, vars, {QStringLiteral("background-color"), QStringLiteral("background")}, allPred);
    const QString opacityRaw = bestValue(sub, {QStringLiteral("opacity")}, allPred);
    if (!opacityRaw.isEmpty()) { rule.opacity = opacityValue(opacityRaw, vars); }
    const QString maskSize = bestValue(sub, {QStringLiteral("mask-size"), QStringLiteral("-webkit-mask-size")}, allPred);
    if (!maskSize.isEmpty()) {
      const QStringList ms = splitTopLevelSpaces(CssThemeParser::resolveVars(maskSize, vars));
      if (!ms.isEmpty()) {
        const qreal w = lengthToPx(ms.at(0), vars, emPx);
        const qreal h = ms.size() > 1 ? lengthToPx(ms.at(1), vars, emPx) : w;
        if (w > 0 && h > 0) { rule.maskTile = QSizeF(w, h); }
      }
    }
    const qreal w = lengthToPx(bestValue(sub, {QStringLiteral("width")}, allPred), vars, emPx);
    const qreal h = lengthToPx(bestValue(sub, {QStringLiteral("height")}, allPred), vars, emPx);
    if (w > 0 || h > 0) { rule.size = QSizeF(w, h); }
    const QString bb = bestValue(sub,
        {QStringLiteral("border-bottom"), QStringLiteral("border-bottom-color"),
         QStringLiteral("border-color"), QStringLiteral("border")}, allPred);
    rule.borderBottomColor = extractColor(bb, vars);
    rule.borderBottomWidth = borderWidthPx(bb, vars, emPx);
    const QByteArray contentSvg = extractDataUri(contentRaw);
    const QByteArray bgSvg = extractDataUri(bgImg);
    const QByteArray maskSvg = extractDataUri(maskImg);
    rule.svgData = !contentSvg.isEmpty() ? contentSvg : (!bgSvg.isEmpty() ? bgSvg : maskSvg);
    // An icon sourced from `mask:` is an alpha shape tinted with the
    // background-color (e.g. phycat's link ::before); render recoloured, not as-is.
    rule.svgFromMask = !maskSvg.isEmpty() && rule.svgData == maskSvg;
    out.push_back(std::move(rule));
  }
  return out;
}

// Host element OWN background-image gradients (not pseudos). Only gradients are
// captured here — solid background-colours remain on the existing theme tokens
// (codeBackground/highlight/blockquoteBackground/…) so they aren't double-painted.
std::vector<ElementBackground> extractElementBackgrounds(const std::vector<FlatDecl>& flat,
                                                          const QHash<QString, QString>& vars) {
  static const std::vector<QString> hosts = {
      QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"), QStringLiteral("h4"),
      QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"), QStringLiteral("hr"),
      QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("mark"), QStringLiteral("em"),
      QStringLiteral("a"), QStringLiteral("li")};
  std::vector<ElementBackground> out;
  for (const QString& host : hosts) {
    const auto pred = [&host](const SelInfo& s) {
      return s.pseudoElement.isEmpty() && !s.hover && !s.focus && !s.active && !s.visited && !s.mdFocus && s.tag == host;
    };
    const QString bgImg = bestValue(flat, {QStringLiteral("background-image"), QStringLiteral("background")}, pred);
    const GradientSpec grad = parseGradientSpec(bgImg, vars);
    if (grad.kind == GradientSpec::Kind::None) { continue; }
    ElementBackground eb;
    eb.host = host;
    eb.gradient = grad;
    eb.color = colorToken(flat, vars, {QStringLiteral("background-color"), QStringLiteral("background")}, pred);
    eb.opacity = 1.0;
    eb.present = true;
    out.push_back(std::move(eb));
  }
  return out;
}

// :hover rules are dropped by flatten() (the colour-leak fix); collect them here
// for the hover-effect capture. Element :hover only (no ::before/::after hover).
std::vector<FlatDecl> flattenHover(const CssThemeSheet& sheet) {
  std::vector<FlatDecl> out;
  int order = 0;
  for (const CssRule& rule : sheet.rules()) {
    if (rule.darkScope) { continue; }
    for (const QString& selector : rule.selectors) {
      if (selectorRequiresExportContext(selector)) { continue; }
      const SelInfo info = analyzeSelector(selector);
      if (!info.hover || !info.pseudoElement.isEmpty()) { continue; }
      const int spec = specificity(selector);
      for (const CssDeclaration& decl : rule.declarations) {
        FlatDecl fd;
        fd.info = info;
        fd.property = decl.property;
        fd.value = decl.value;
        fd.important = decl.important;
        fd.spec = spec;
        fd.order = order++;
        out.push_back(std::move(fd));
      }
    }
  }
  return out;
}

qreal shadowBlurPx(const QString& shadowRaw, const QHash<QString, QString>& vars) {
  const QStringList parts = splitTopLevelSpaces(CssThemeParser::resolveVars(shadowRaw, vars));
  QVector<qreal> nums;
  for (const QString& p : parts) {
    const QString t = p.trimmed();
    // Only treat length tokens (leading digit/sign/dot); skip colour tokens like rgba(...).
    if (t.isEmpty() || !(t.at(0).isDigit() || t.at(0) == QLatin1Char('+') ||
                          t.at(0) == QLatin1Char('-') || t.at(0) == QLatin1Char('.'))) {
      continue;
    }
    nums.append(lengthToPx(t, vars, 16.0));  // handles 15px / 0 / em
  }
  if (nums.size() >= 3) { return nums.at(2); }
  if (nums.size() >= 2) { return 8.0; }
  return 0.0;
}

// Tractable :hover subset: box-shadow glow (colour + blur) + background tint.
std::vector<HoverEffect> extractHoverEffects(const std::vector<FlatDecl>& flatHover, const QHash<QString, QString>& vars) {
  static const std::vector<QString> hosts = {QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
      QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"),
      QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("mark"), QStringLiteral("a"), QStringLiteral("li")};
  std::vector<HoverEffect> out;
  for (const QString& host : hosts) {
    const auto pred = [&host](const SelInfo& s) { return s.tag == host; };
    HoverEffect he;
    he.host = host;
    const QString shadow = bestValue(flatHover, {QStringLiteral("box-shadow")}, pred);
    if (!shadow.isEmpty() && !shadow.contains(QStringLiteral("none"))) {
      he.glowColor = extractColor(shadow, vars);
      he.glowBlur = shadowBlurPx(shadow, vars);
    }
    he.bgTint = colorToken(flatHover, vars, {QStringLiteral("background-color"), QStringLiteral("background")}, pred);
    if (!he.glowColor.isValid() && !he.bgTint.isValid()) { continue; }
    he.present = true;
    out.push_back(std::move(he));
  }
  return out;
}

qreal transitionMs(const QString& raw) {
  // Match a duration with optional leading dot (.3s, 0.3s, 300ms, 2s).
  static const QRegularExpression re(QStringLiteral("(\\d*\\.?\\d+)(s|ms)"));
  const QRegularExpressionMatch m = re.match(raw);
  if (!m.hasMatch()) { return 0.0; }
  const qreal v = m.captured(1).toDouble();
  return m.captured(2) == QStringLiteral("s") ? v * 1000.0 : v;
}

std::vector<TransitionSpec> extractTransitions(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars) {
  static const std::vector<QString> hosts = {QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
      QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"),
      QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("mark"), QStringLiteral("a"), QStringLiteral("li")};
  std::vector<TransitionSpec> out;
  for (const QString& host : hosts) {
    const auto pred = [&host](const SelInfo& s) {
      return s.pseudoElement.isEmpty() && !s.hover && s.tag == host;
    };
    const QString raw = bestValue(flat, {QStringLiteral("transition")}, pred);
    if (raw.isEmpty()) { continue; }
    const qreal ms = transitionMs(CssThemeParser::resolveVars(raw, vars));
    if (ms <= 0.0) { continue; }
    TransitionSpec ts;
    ts.host = host;
    ts.durationMs = ms;
    out.push_back(std::move(ts));
  }
  return out;
}

std::vector<KeyframesDef> extractKeyframes(const CssThemeSheet& sheet, const QHash<QString, QString>& vars) {
  // var() is resolved here (once, at load) so the runtime sampler needs no
  // variable table — ThemeDefinition/RenderTheme don't carry one.
  std::vector<KeyframesDef> out;
  for (const CssKeyframes& k : sheet.keyframes()) {
    KeyframesDef def;
    def.name = k.name;
    for (const CssKeyframeStop& s : k.stops) {
      KeyframeStop st;
      st.position = s.position;
      for (const CssDeclaration& d : s.declarations) {
        st.declarations.insert(d.property, CssThemeParser::resolveVars(d.value, vars));
      }
      def.stops.push_back(std::move(st));
    }
    out.push_back(std::move(def));
  }
  return out;
}

// Parse an `animation:` shorthand into an AnimationDef. Heuristic token walk:
// the name is the leftover identifier; times → duration (then delay); keywords
// → iterations/direction/easing; fill/play-state keywords are ignored.
AnimationDef parseAnimationShorthand(const QString& raw, const QHash<QString, QString>& vars, const QString& host) {
  AnimationDef a;
  a.host = host;
  a.iterations = 1;
  const QStringList parts = splitTopLevelSpaces(CssThemeParser::resolveVars(raw, vars));
  bool gotDuration = false;
  static const QSet<QString> kSkip = {QStringLiteral("forwards"), QStringLiteral("backwards"), QStringLiteral("both"),
      QStringLiteral("none"), QStringLiteral("running"), QStringLiteral("paused"), QStringLiteral("initial"), QStringLiteral("inherit")};
  static const QSet<QString> kEase = {QStringLiteral("linear"), QStringLiteral("ease"), QStringLiteral("ease-in"),
      QStringLiteral("ease-out"), QStringLiteral("ease-in-out"), QStringLiteral("step-start"), QStringLiteral("step-end")};
  for (const QString& p : parts) {
    const QString t = p.toLower();
    if (t == QStringLiteral("infinite")) { a.iterations = -1; }
    else if (t == QStringLiteral("reverse")) { a.direction = AnimationDef::Direction::Reverse; }
    else if (t == QStringLiteral("alternate")) { a.direction = AnimationDef::Direction::Alternate; }
    else if (t == QStringLiteral("alternate-reverse")) { a.direction = AnimationDef::Direction::AlternateReverse; }
    else if (kEase.contains(t) || t.startsWith(QStringLiteral("cubic-bezier")) || t.startsWith(QStringLiteral("steps"))) {
      if (a.easing.isEmpty()) { a.easing = t; }
    }
    else if (kSkip.contains(t)) { /* fill/play-state — ignored */ }
    else if (transitionMs(t) > 0.0) {
      const qreal ms = transitionMs(t);
      if (!gotDuration) { a.durationMs = ms; gotDuration = true; } else { a.delayMs = ms; }
    }
    else {
      bool ok = false;
      const int n = t.toInt(&ok);
      if (ok) { a.iterations = n; }
      else if (a.name.isEmpty()) { a.name = p; }  // the animation name (identifier)
    }
  }
  return a;
}

// Always-on `animation:` on a host element (hover/state-triggered animations
// are captured separately when that wiring lands). Resolved against keyframes
// at drive time; entries with no matching keyframes are skipped by the driver.
std::vector<AnimationDef> extractAnimations(const std::vector<FlatDecl>& flat, const QHash<QString, QString>& vars) {
  static const std::vector<QString> hosts = {QStringLiteral("h1"), QStringLiteral("h2"), QStringLiteral("h3"),
      QStringLiteral("h4"), QStringLiteral("h5"), QStringLiteral("h6"), QStringLiteral("blockquote"),
      QStringLiteral("pre"), QStringLiteral("code"), QStringLiteral("mark"), QStringLiteral("a"), QStringLiteral("li")};
  std::vector<AnimationDef> out;
  for (const QString& host : hosts) {
    const auto pred = [&host](const SelInfo& s) {
      return s.pseudoElement.isEmpty() && !s.hover && s.tag == host;
    };
    const QString raw = bestValue(flat, {QStringLiteral("animation")}, pred);
    if (raw.isEmpty()) { continue; }
    AnimationDef a = parseAnimationShorthand(raw, vars, host);
    if (a.name.isEmpty() || a.durationMs <= 0.0) { continue; }
    out.push_back(std::move(a));
  }
  return out;
}

}  // namespace

ThemeDefinition CssThemeMapper::fromCss(const QString& cssText, const QString& id, const QString& baseDir) {
  return fromSheet(CssThemeParser::parse(cssText, baseDir), id);
}

GradientSpec CssThemeMapper::parseGradient(const QString& raw, const QHash<QString, QString>& vars) {
  return parseGradientSpec(raw, vars);
}

QColor CssThemeMapper::resolveColor(const QString& value, const QHash<QString, QString>& vars) {
  return extractColor(value, vars);
}

qreal CssThemeMapper::resolveLengthPx(const QString& value, const QHash<QString, QString>& vars) {
  return lengthToPx(value, vars, 16.0);
}

ThemeDefinition CssThemeMapper::fromSheet(const CssThemeSheet& sheet, const QString& id) {
  ThemeDefinition d;
  d.isBuiltIn = false;
  d.id = id.toLower();

  const QHash<QString, QString>& vars = sheet.variables();
  const std::vector<FlatDecl> flat = flatten(sheet);

  const std::vector<QString> bgProps = {QStringLiteral("background-color"), QStringLiteral("background")};
  const std::vector<QString> colorProps = {QStringLiteral("color")};
  const std::vector<QString> familyProps = {QStringLiteral("font-family")};
  const std::vector<QString> sizeProps = {QStringLiteral("font-size")};
  const std::vector<QString> lhProps = {QStringLiteral("line-height")};
  const std::vector<QString> alignProps = {QStringLiteral("text-align")};
  const std::vector<QString> weightProps = {QStringLiteral("font-weight")};
  const std::vector<QString> styleProps = {QStringLiteral("font-style")};
  const std::vector<QString> marginProps = {QStringLiteral("margin")};
  const std::vector<QString> paddingProps = {QStringLiteral("padding")};
  const std::vector<QString> borderLeftProps = {QStringLiteral("border-left-color"), QStringLiteral("border-left"),
                                                 QStringLiteral("border-color"), QStringLiteral("border")};
  const std::vector<QString> borderProps = {QStringLiteral("border-color"), QStringLiteral("border-left-color"),
                                             QStringLiteral("border-right-color"), QStringLiteral("border-top-color"),
                                             QStringLiteral("border-bottom-color"), QStringLiteral("border-left"),
                                             QStringLiteral("border")};

  ThemeColors& k = d.colors;

  // Page model: body/html paint the viewport; #write is the centered document
  // card. Keep k.background as the page background for legacy consumers.
  d.page.viewportBackground = colorToken(flat, vars, bgProps, isHtmlOrBody);
  d.page.pageBackground = colorToken(flat, vars, bgProps, isWrite);
  k.background = d.page.pageBackground.isValid() ? d.page.pageBackground : d.page.viewportBackground;
  k.text = colorToken(flat, vars, colorProps, isDocumentContainer);
  k.link = colorToken(flat, vars, colorProps, isLink);
  k.codeBackground = colorToken(flat, vars, bgProps, isInlineCode);
  k.codeBlockBackground = colorToken(flat, vars, bgProps, isCodeBlock);
  k.highlight = colorToken(flat, vars, bgProps, isMark);
  k.selection = colorToken(flat, vars, bgProps, isSelection);
  k.quoteBorder = colorToken(flat, vars, borderLeftProps, isBlockquote);
  k.blockquoteBackground = colorToken(flat, vars, bgProps, isBlockquote);
  k.tableBorder = colorToken(flat, vars, borderProps, isTable);
  k.tableHeaderBackground = colorToken(flat, vars, bgProps, isThOrHead);
  k.tableAlternateBackground = colorToken(flat, vars, bgProps, isAltRow);
  k.codeBorder = colorToken(flat, vars, borderProps, [](const SelInfo& s) { return isInlineCode(s) || isCodeBlock(s); });

  qreal bodyPx = lengthToPx(bestValue(flat, sizeProps, isHtmlOrBody), vars, 16.0);
  if (bodyPx <= 0.0) { bodyPx = 16.0; }
  // Host element own background-image gradients (h2 radial glow, hr gradient, …).
  d.decorations.backgrounds = extractElementBackgrounds(flat, vars);
  // :hover glow/tint (box-shadow subset) + transition durations.
  d.decorations.hoverEffects = extractHoverEffects(flattenHover(sheet), vars);
  d.decorations.transitions = extractTransitions(flat, vars);
  // @keyframes + always-on `animation:` bindings.
  d.decorations.keyframes = extractKeyframes(sheet, vars);
  d.decorations.animations = extractAnimations(flat, vars);
  const Box padBox = readBox(flat, vars, isWrite, QStringLiteral("padding"), bodyPx);
  const Box marBox = readBox(flat, vars, isWrite, QStringLiteral("margin"), bodyPx);
  d.page.pagePadding = padBox.margins;
  if (marBox.present) {
    d.page.pageMargin = marBox.margins;
    d.page.pageMarginExplicit = true;
  } else if (padBox.present) {
    // No #write margin declared, but the theme gave #write its own padding — so
    // the card already has breathing room. Honour the CSS default margin of 0
    // rather than the legacy flat-document inset, which would double the top
    // gap (e.g. github: padding 30 + legacy margin 30 = 60, vs the theme's own 30).
    d.page.pageMargin = QMarginsF();
    d.page.pageMarginExplicit = true;
  } else {
    d.page.pageMargin = QMarginsF();  // null → RenderTheme applies the legacy 30/70 inset
    d.page.pageMarginExplicit = false;
  }
  // #write max-width and width are separate CSS concepts: width is the preferred
  // used size, max-width caps it. Keep that distinction when collapsing into
  // Muffin's single pageMaxWidth field: a concrete max-width wins over width, and
  // percent/intrinsic widths mean "fill the containing block" (large sentinel,
  // clamped by DocumentLayout), never em-relative pixels.
  const QString maxWidthRaw = bestValue(flat, {QStringLiteral("max-width")}, isWrite);
  const qreal maxWidth = pageWidthToPxOrSentinel(maxWidthRaw, vars, bodyPx);
  if (maxWidth > 0.0) {
    d.page.pageMaxWidth = maxWidth;
  } else {
    d.page.pageMaxWidth = pageWidthToPxOrSentinel(bestValue(flat, {QStringLiteral("width")}, isWrite), vars, bodyPx);
  }
  d.page.pageBorderColor = colorToken(flat, vars, borderProps, isWrite);
  d.page.pageBorderWidth = borderWidthPx(bestValue(flat, {QStringLiteral("border")}, isWrite), vars, bodyPx);
  d.page.pageBorderRadius = lengthToPx(bestValue(flat, {QStringLiteral("border-radius")}, isWrite), vars, bodyPx);
  const QString shadowRaw = bestValue(flat, {QStringLiteral("box-shadow")}, isWrite);
  d.page.pageShadowColor = extractColor(shadowRaw, vars);
  const QStringList shadowParts = splitTopLevelSpaces(CssThemeParser::resolveVars(shadowRaw, vars));
  if (shadowParts.size() >= 3) {
    d.page.pageShadowOffsetY = lengthToPx(shadowParts.at(1), vars, bodyPx);
    d.page.pageShadowBlur = lengthToPx(shadowParts.at(2), vars, bodyPx);
  }

  // Per-heading colour + the h2 accent bar (cheap decoration).
  for (int level = 1; level <= 6; ++level) {
    QColor hc = colorToken(flat, vars, colorProps, [level](const SelInfo& s) { return isHeading(s, level); });
    if (hc.isValid()) { d.typography.headingColor[level - 1] = hc; }
    const qreal hs = lengthToPt(bestValue(flat, sizeProps, [level](const SelInfo& s) { return isHeading(s, level); }), vars, bodyPx);
    if (hs > 0.0) { d.typography.headingSizePt[level - 1] = hs; }
  }
  k.headingAccentColor = colorToken(flat, vars, borderLeftProps, [](const SelInfo& s) {
    return s.tag == QStringLiteral("h2") && !s.hover;
  });
  const auto emPxForHost = [&](const QString& host) {
    if (host.size() == 2 && host.at(0) == QLatin1Char('h') && host.at(1) >= QLatin1Char('1') && host.at(1) <= QLatin1Char('6')) {
      return headingEmPx(d.typography, host.at(1).digitValue(), bodyPx);
    }
    return bodyPx;
  };
  // ::before/::after decorations (gradients, SVG icons, text content, texture
  // masks), grouped by host. Empty for themes that declare none.
  d.decorations.pseudos = extractPseudoRules(flat, vars, emPxForHost);

  // CSS document-flow metrics.
  d.spacing.paragraphMargin = boxToMarginsPx(bestValue(flat, marginProps, [](const SelInfo& s) { return s.tag == QStringLiteral("p"); }), vars, bodyPx);
  d.spacing.blockquoteMargin = boxToMarginsPx(bestValue(flat, marginProps, isBlockquote), vars, bodyPx);
  d.spacing.codeBlockMargin = boxToMarginsPx(bestValue(flat, marginProps, isCodeBlock), vars, bodyPx);
  d.spacing.tableMargin = boxToMarginsPx(bestValue(flat, marginProps, [](const SelInfo& s) { return s.tag == QStringLiteral("table"); }), vars, bodyPx);
  d.spacing.listMargin = boxToMarginsPx(bestValue(flat, marginProps, [](const SelInfo& s) { return s.tag == QStringLiteral("ul") || s.tag == QStringLiteral("ol"); }), vars, bodyPx);
  d.spacing.listPaddingLeft = lengthToPx(bestValue(flat, {QStringLiteral("padding-left")}, [](const SelInfo& s) { return s.tag == QStringLiteral("ul") || s.tag == QStringLiteral("ol"); }), vars, bodyPx);
  if (d.spacing.listPaddingLeft <= 0.0) {
    d.spacing.listPaddingLeft = boxToMarginsPx(bestValue(flat, paddingProps, [](const SelInfo& s) { return s.tag == QStringLiteral("ul") || s.tag == QStringLiteral("ol"); }), vars, bodyPx).left();
  }
  for (int level = 1; level <= 6; ++level) {
    const qreal headingPx = headingEmPx(d.typography, level, bodyPx);
    // Heading MARGIN: "em" is heading-relative (uses headingPx), but "rem" is root-relative (uses
    // bodyPx). readBox threads both so e.g. github's "1rem" → root, while an "em" margin stays
    // heading-sized. Padding below stays purely heading-relative.
    d.spacing.headingMargin[level - 1] = readBox(flat, vars, [level](const SelInfo& s) { return isHeading(s, level); }, QStringLiteral("margin"), headingPx, bodyPx).margins;
    d.spacing.headingPadding[level - 1] = readBox(flat, vars, [level](const SelInfo& s) { return isHeading(s, level); }, QStringLiteral("padding"), headingPx).margins;
    const qreal headingPadLeft = lengthToPx(bestValue(flat, {QStringLiteral("padding-left")}, [level](const SelInfo& s) { return isHeading(s, level); }), vars, headingPx);
    if (headingPadLeft > 0.0) { d.spacing.headingPadding[level - 1].setLeft(headingPadLeft); }
    const QString bb = bestValue(flat, {QStringLiteral("border-bottom"), QStringLiteral("border-bottom-color")}, [level](const SelInfo& s) { return isHeading(s, level); });
    d.spacing.headingBorderBottomColor[level - 1] = extractColor(bb, vars);
    d.spacing.headingBorderBottomWidth[level - 1] = borderWidthPx(bb, vars, headingPx);
    const QString bl = bestValue(flat, {QStringLiteral("border-left"), QStringLiteral("border-left-color")}, [level](const SelInfo& s) { return isHeading(s, level); });
    d.spacing.headingBorderLeftColor[level - 1] = extractColor(bl, vars);
    d.spacing.headingBorderLeftWidth[level - 1] = borderWidthPx(bl, vars, headingPx);
  }

  // Fonts / sizes / line-height. p,li writing font overrides body font for the
  // rendered prose; html/body still provide the base size and line-height.
  d.typography.bodyFont = firstFamily(bestValue(flat, familyProps, isParagraphText), vars);
  if (d.typography.bodyFont.isEmpty()) { d.typography.bodyFont = firstFamily(bestValue(flat, familyProps, isWrite), vars); }
  if (d.typography.bodyFont.isEmpty()) { d.typography.bodyFont = firstFamily(bestValue(flat, familyProps, isHtmlOrBody), vars); }
  d.typography.headingFont = firstFamily(bestValue(flat, familyProps, isAnyHeading), vars);
  if (d.typography.headingFont.isEmpty()) { d.typography.headingFont = firstFamily(bestValue(flat, familyProps, isWrite), vars); }
  if (d.typography.headingFont.isEmpty()) { d.typography.headingFont = firstFamily(bestValue(flat, familyProps, isHtmlOrBody), vars); }
  if (d.typography.headingFont.isEmpty()) { d.typography.headingFont = d.typography.bodyFont; }
  if (fontStackLooksSerif(d.typography.bodyFont) || fontStackLooksSerif(d.typography.headingFont)) { k.serifBody = true; }
  d.typography.codeFont = firstFamily(bestValue(flat, familyProps, [](const SelInfo& s) { return isInlineCode(s) || isCodeBlock(s); }), vars);
  d.typography.bodySizePt = lengthToPt(bestValue(flat, sizeProps, isHtmlOrBody), vars, 16.0);
  d.typography.bodyAlignment = parseTextAlign(bestValue(flat, alignProps, [](const SelInfo& s) {
    return isParagraphText(s) || isWrite(s) || isHtmlOrBody(s);
  }), vars);
  const QString lhRaw = bestValue(flat, lhProps, [](const SelInfo& s) {
    return isParagraphText(s) || isWrite(s) || isHtmlOrBody(s);
  });
  const qreal lh = parseLineHeightMultiplier(lhRaw, vars, bodyPx);
  if (lh > 0.0) { d.typography.lineHeight = lh; }
  for (int level = 1; level <= 6; ++level) {
    const qreal headingPx = headingEmPx(d.typography, level, bodyPx);
    d.typography.headingAlignment[level - 1] = parseTextAlign(bestValue(flat, alignProps, [level](const SelInfo& s) { return isHeading(s, level); }), vars);
    const ParsedFontWeight fw = parseFontWeight(bestValue(flat, weightProps, [level](const SelInfo& s) { return isHeading(s, level); }), vars);
    if (fw.present) {
      d.typography.headingFontWeight[level - 1] = fw.weight;
      d.typography.headingFontWeightSet[level - 1] = true;
    }
    const ParsedItalic fs = parseFontItalic(bestValue(flat, styleProps, [level](const SelInfo& s) { return isHeading(s, level); }), vars);
    if (fs.present) {
      d.typography.headingItalic[level - 1] = fs.italic;
      d.typography.headingItalicSet[level - 1] = true;
    }
    const QString hlh = bestValue(flat, lhProps, [level](const SelInfo& s) { return isHeading(s, level); });
    const qreal v = parseLineHeightMultiplier(hlh, vars, headingPx);
    if (v > 0.0) { d.typography.headingLineHeight[level - 1] = v; }
  }

  // --- Tier 2: conventional :root variable vocabulary (gap fill) ----------
  // CSS themes speak a conventional :root vocabulary (--bg-color,
  // --text-color, --primary-color, --side-bar-bg-color, …). That vocabulary IS
  // the author's semantic declaration of "this is my background / text / accent",
  // so it is strictly more reliable than guessing from selector names. But a
  // concrete element rule from tier 1 is authoritative for what is actually
  // painted (a theme may set body { background:#fff } yet keep --bg-color for a
  // different purpose) — so these variables only fill tokens the cascade LEFT
  // INVALID, never overwrite. This is what makes "pure variable" themes (only
  // :root + an @imported base that applies the colours, e.g. the a community theme (phycat)
  // family) resolve fully without chasing selector names forever.
  {
    // Resolve a variable's value to a colour, resolving nested var() and
    // shorthands (extractColor handles both). Tries names in order.
    const auto firstVar = [&](std::initializer_list<const char*> names) -> QColor {
      for (const char* n : names) {
        const QString raw = vars.value(QString::fromLatin1(n));
        if (raw.isEmpty()) { continue; }
        const QColor c = extractColor(raw, vars);
        if (c.isValid()) { return c; }
      }
      return QColor();
    };
    // Set `member` from the first resolving variable, but only if still unset.
    const auto gap = [&](QColor ThemeColors::* member, std::initializer_list<const char*> names) {
      if ((k.*member).isValid()) { return; }
      const QColor c = firstVar(names);
      if (c.isValid()) { k.*member = c; }
    };

    // Document palette. Background also paints the viewport when no
    // html/body/#write rule did, so the page card model stays consistent.
    if (!k.background.isValid()) {
      const QColor bg = firstVar({"--bg-color"});
      if (bg.isValid()) {
        k.background = bg;
        if (!d.page.viewportBackground.isValid()) { d.page.viewportBackground = bg; }
      }
    }
    gap(&ThemeColors::text,                {"--text-color"});
    gap(&ThemeColors::muted,               {"--text-color-secondary", "--text-color-tertiary"});
    gap(&ThemeColors::link,                {"--primary-color"});
    gap(&ThemeColors::codeBackground,      {"--code-bg-color"});
    gap(&ThemeColors::codeBlockBackground, {"--code-block-bg"});
    gap(&ThemeColors::selection,           {"--select-text-bg-color"});
    gap(&ThemeColors::accent,              {"--primary-color"});
    // Chrome palette — the conventional UI-control vocabulary maps directly onto Muffin's
    // chrome tokens, so a stock CSS theme lights up the whole chrome for free
    // (previously only reachable via --muffin-*).
    gap(&ThemeColors::chromeBackground,    {"--side-bar-bg-color"});
    gap(&ThemeColors::surface,             {"--side-bar-bg-color"});
    // chromeText is the general UI/control text colour → --control-text-color.
    // NOT --active-file-text-color: that's the text of the *highlighted* active
    // file item (typically white-on-highlight), which would make all menu text
    // white/invisible (e.g. newsprint sets it to `white`).
    gap(&ThemeColors::chromeText,          {"--control-text-color"});
    gap(&ThemeColors::chromeMuted,         {"--text-color-secondary", "--text-color-tertiary"});
    gap(&ThemeColors::hover,               {"--item-hover-bg-color"});
    gap(&ThemeColors::selected,            {"--active-file-bg-color"});
  }

  // --- Tier 3: Muffin-specific extension variables (--muffin-*). Highest
  // authority — explicit Muffin intent wins over both cascade and the
  // conventional vocabulary.
  if (QColor cb = varColor(vars, "--muffin-background"); cb.isValid()) { k.background = cb; }
  if (QColor ct = varColor(vars, "--muffin-text"); ct.isValid()) { k.text = ct; }
  if (QColor cl = varColor(vars, "--muffin-link"); cl.isValid()) { k.link = cl; }
  if (QColor cc = varColor(vars, "--muffin-code-background"); cc.isValid()) { k.codeBackground = cc; }
  if (QColor ccb = varColor(vars, "--muffin-code-block-background"); ccb.isValid()) { k.codeBlockBackground = ccb; }
  if (QColor ch = varColor(vars, "--muffin-highlight"); ch.isValid()) { k.highlight = ch; }
  if (QColor cs = varColor(vars, "--muffin-selection"); cs.isValid()) { k.selection = cs; }
  if (QColor cq = varColor(vars, "--muffin-quote-border"); cq.isValid()) { k.quoteBorder = cq; }
  if (QColor cqb = varColor(vars, "--muffin-blockquote-background"); cqb.isValid()) { k.blockquoteBackground = cqb; }
  if (QColor ct2 = varColor(vars, "--muffin-table-border"); ct2.isValid()) { k.tableBorder = ct2; }
  if (QColor cth = varColor(vars, "--muffin-table-header-background"); cth.isValid()) { k.tableHeaderBackground = cth; }
  if (QColor cta = varColor(vars, "--muffin-table-alternate-background"); cta.isValid()) { k.tableAlternateBackground = cta; }
  if (QColor cbr = varColor(vars, "--muffin-code-border"); cbr.isValid()) { k.codeBorder = cbr; }
  if (QColor cha = varColor(vars, "--muffin-heading-accent"); cha.isValid()) { k.headingAccentColor = cha; }
  // Chrome palette — no CSS equivalent, so only via --muffin-*. These are
  // guarded (not bare assignment) so an absent --muffin-* leaves a tier-2 value
  // intact instead of clobbering it back to invalid.
  if (QColor cm = varColor(vars, "--muffin-muted"); cm.isValid()) { k.muted = cm; }
  if (QColor ccb = varColor(vars, "--muffin-chrome-background"); ccb.isValid()) { k.chromeBackground = ccb; }
  if (QColor cct = varColor(vars, "--muffin-chrome-text"); cct.isValid()) { k.chromeText = cct; }
  if (QColor ccm = varColor(vars, "--muffin-chrome-muted"); ccm.isValid()) { k.chromeMuted = ccm; }
  if (QColor cs = varColor(vars, "--muffin-surface"); cs.isValid()) { k.surface = cs; }
  if (QColor cc = varColor(vars, "--muffin-canvas"); cc.isValid()) { k.canvas = cc; }
  if (QColor cb = varColor(vars, "--muffin-border"); cb.isValid()) { k.border = cb; }
  if (QColor ch = varColor(vars, "--muffin-hover"); ch.isValid()) { k.hover = ch; }
  if (QColor cs2 = varColor(vars, "--muffin-selected"); cs2.isValid()) { k.selected = cs2; }
  if (QColor ca = varColor(vars, "--muffin-accent"); ca.isValid()) { k.accent = ca; }
  if (truthy(varValue(vars, "--muffin-serif-body"))) { k.serifBody = true; }

  // Derived muted text: if the theme gave a link/accent but no muted text, leave
  // it for deriveChromeDefaults to fall back to text.
  // Last-resort background: a theme that declared text but no background at all
  // (no painted rule, no --bg-color). Pick a contrasting canvas from the text
  // luminance so isDark and chrome derivation stay sane — mirrors a browser's
  // default white/dark page when CSS specifies none.
  if (!k.background.isValid() && k.text.isValid()) {
    k.background = k.text.lightness() >= 128 ? QColor(0x18, 0x18, 0x18) : QColor(0xff, 0xff, 0xff);
    if (!d.page.viewportBackground.isValid()) { d.page.viewportBackground = k.background; }
  }
  // Derive chrome defaults for anything still unset, then resolve isDark.
  ThemeDefinition::deriveChromeDefaults(k);
  // CSS themes have no "muted text" concept; ensure muted/chromeMuted resolve
  // to something valid (text colour, slightly softened) so chrome reads sanely.
  if (!k.muted.isValid()) {
    k.muted = k.text.isValid() ? (k.isDark ? k.text.darker(160) : k.text.lighter(160)) : k.text;
  }
  if (!k.chromeMuted.isValid()) { k.chromeMuted = k.muted; }
  const QString darkFlag = varValue(vars, "--muffin-dark");
  if (!darkFlag.isEmpty()) {
    k.isDark = truthy(darkFlag);
  } else {
    k.isDark = k.background.isValid() ? (k.background.lightness() < 128) : false;
  }

  // Label: explicit --muffin-label wins, else a presentable title-cased id.
  const QString labelVar = varValue(vars, "--muffin-label");
  if (!labelVar.isEmpty()) {
    d.label = labelVar;
    if (d.label.size() >= 2 && d.label.front() == QLatin1Char('"') && d.label.back() == QLatin1Char('"')) {
      d.label = d.label.mid(1, d.label.size() - 2);
    }
  } else {
    d.label = titleCaseId(d.id);
  }
  return d;
}

}  // namespace muffin
