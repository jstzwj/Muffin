#include "theme/CssThemeParser.h"

#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>

#include <algorithm>
#include <functional>

namespace muffin {
namespace {

// Skip whitespace and /* ... */ comments starting at `i`. Returns the new index.
int skipWsComments(const QString& text, int i, int n) {
  while (i < n) {
    const QChar c = text.at(i);
    if (c.isSpace()) {
      ++i;
    } else if (c == QLatin1Char('/') && i + 1 < n && text.at(i + 1) == QLatin1Char('*')) {
      int end = text.indexOf(QStringLiteral("*/"), i + 2);
      i = (end < 0) ? n : end + 2;
    } else {
      break;
    }
  }
  return i;
}

// If text[i] starts a quoted string, return its (unquoted) content and advance
// `i` past the closing quote. Otherwise leave i unchanged and return empty.
QString tryReadString(const QString& text, int& i, int n) {
  if (i >= n) { return {}; }
  const QChar quote = text.at(i);
  if (quote != QLatin1Char('"') && quote != QLatin1Char('\'')) { return {}; }
  QString out;
  int j = i + 1;
  while (j < n) {
    const QChar c = text.at(j);
    if (c == QLatin1Char('\\') && j + 1 < n) {  // CSS escape — keep next char literally
      out += text.at(j + 1);
      j += 2;
      continue;
    }
    if (c == quote) { break; }
    out += c;
    ++j;
  }
  i = (j < n) ? j + 1 : n;  // consume closing quote
  return out;
}

// Find the next occurrence of `delim` at structural top level: not inside a
// string, comment, () or []. Returns its index, or -1 if not found.
int findDelim(const QString& text, int from, int n, QChar delim) {
  int i = from;
  int paren = 0, brk = 0;
  while (i < n) {
    const QChar c = text.at(i);
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
      int tmp = i;
      tryReadString(text, tmp, n);
      i = tmp;
      continue;
    }
    if (c == QLatin1Char('/') && i + 1 < n && text.at(i + 1) == QLatin1Char('*')) {
      int end = text.indexOf(QStringLiteral("*/"), i + 2);
      i = (end < 0) ? n : end + 2;
      continue;
    }
    // Check the delimiter BEFORE adjusting depth, so that when the delimiter IS
    // a bracket (e.g. finding the ')' that closes a var()) it is returned at the
    // current depth rather than swallowed by the balance tracking.
    if (paren == 0 && brk == 0 && c == delim) { return i; }
    if (c == QLatin1Char('(')) { ++paren; }
    else if (c == QLatin1Char(')')) { paren = qMax(0, paren - 1); }
    else if (c == QLatin1Char('[')) { ++brk; }
    else if (c == QLatin1Char(']')) { brk = qMax(0, brk - 1); }
    ++i;
  }
  return -1;
}

// text[openIdx] is '{'. Return the index of the matching '}', respecting
// strings, comments, ()/[] and nested {}. -1 if unmatched.
int matchingBrace(const QString& text, int openIdx, int n) {
  int i = openIdx + 1;
  int depth = 1;
  while (i < n) {
    const QChar c = text.at(i);
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
      int tmp = i;
      tryReadString(text, tmp, n);
      i = tmp;
      continue;
    }
    if (c == QLatin1Char('/') && i + 1 < n && text.at(i + 1) == QLatin1Char('*')) {
      int end = text.indexOf(QStringLiteral("*/"), i + 2);
      i = (end < 0) ? n : end + 2;
      continue;
    }
    if (c == QLatin1Char('{')) { ++depth; }
    else if (c == QLatin1Char('}')) {
      --depth;
      if (depth == 0) { return i; }
    }
    ++i;
  }
  return -1;
}

// Read a url() or quoted target from an @import/import-prelude, starting at `i`
// positioned at 'u' (url) or at a quote. Returns the raw target string and
// advances i past the closing ')' or quote.
QString readUrlTarget(const QString& text, int& i, int n) {
  i = skipWsComments(text, i, n);
  if (i >= n) { return {}; }
  if (text.at(i) == QLatin1Char('"') || text.at(i) == QLatin1Char('\'')) {
    return tryReadString(text, i, n);
  }
  // url( ... )
  const int paren = findDelim(text, i, n, QLatin1Char('('));
  if (paren < 0) { return {}; }
  int close = findDelim(text, paren + 1, n, QLatin1Char(')'));
  if (close < 0) { close = n; }
  QString inner = text.mid(paren + 1, close - (paren + 1)).trimmed();
  i = close + 1;
  // strip surrounding quotes if present
  if (inner.size() >= 2 &&
      (inner.front() == QLatin1Char('"') || inner.front() == QLatin1Char('\'')) &&
      inner.front() == inner.back()) {
    inner = inner.mid(1, inner.size() - 2);
  }
  return inner;
}

// Remove /* ... */ comments while preserving quoted strings (a /* inside a
// string is literal content, not a comment). CSS has no line comments. This is
// applied before declaration splitting because a comment sitting between two
// ';' would otherwise be glued onto the next declaration's property text
// (e.g. `/* note */ color` → property `/* note */ color`), silently dropping
// the declaration. Strings are copied verbatim so data-URI/url() values and
// their embedded punctuation survive intact.
QString stripComments(const QString& text) {
  QString out;
  const int n = text.size();
  int i = 0;
  out.reserve(n);
  while (i < n) {
    const QChar c = text.at(i);
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
      const int start = i;
      tryReadString(text, i, n);  // advances i past the closing quote
      out += text.mid(start, i - start);
      continue;
    }
    if (c == QLatin1Char('/') && i + 1 < n && text.at(i + 1) == QLatin1Char('*')) {
      const int end = text.indexOf(QStringLiteral("*/"), i + 2);
      i = (end < 0) ? n : end + 2;
      continue;
    }
    out += c;
    ++i;
  }
  return out;
}

std::vector<CssDeclaration> parseDeclarations(const QString& block) {
  std::vector<CssDeclaration> out;
  // Strip comments first so a `/* note */` between two ';' cannot glue onto the
  // following declaration's property (findDelim skips comments while scanning,
  // but the text between two delimiters still includes them).
  const QString src = stripComments(block);
  const int n = src.size();
  int i = 0;
  while (i < n) {
    int semi = findDelim(src, i, n, QLatin1Char(';'));
    if (semi < 0) { semi = n; }
    const QString raw = src.mid(i, semi - i).trimmed();
    i = semi + 1;
    if (raw.isEmpty()) { continue; }
    const int colon = findDelim(raw, 0, raw.size(), QLatin1Char(':'));
    if (colon <= 0) { continue; }  // need a property before the ':'
    CssDeclaration d;
    d.property = raw.left(colon).trimmed().toLower();
    QString val = raw.mid(colon + 1).trimmed();
    // peel trailing !important (case-insensitive)
    const int bang = val.lastIndexOf(QLatin1Char('!'));
    if (bang >= 0 && val.mid(bang + 1).trimmed().toLower() == QLatin1String("important")) {
      d.important = true;
      val = val.left(bang).trimmed();
    }
    d.value = val;
    if (!d.property.isEmpty() && !d.value.isEmpty()) { out.push_back(std::move(d)); }
  }
  return out;
}

// First pass: collect @import targets in document order (only top-level @import;
// imports inside @media are invalid CSS anyway).
QStringList collectImports(const QString& text) {
  QStringList imports;
  const int n = text.size();
  int i = 0;
  while (i < n) {
    i = skipWsComments(text, i, n);
    if (i >= n) { break; }
    if (text.at(i) != QLatin1Char('@')) {
      // skip this rule block to keep scanning at top level
      int brace = findDelim(text, i, n, QLatin1Char('{'));
      if (brace < 0) { break; }
      i = matchingBrace(text, brace, n);
      if (i < 0) { break; }
      ++i;
      continue;
    }
    // at-rule: read keyword
    int j = i + 1;
    while (j < n) {
      const QChar c = text.at(j);
      if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) { ++j; } else { break; }
    }
    const QString keyword = text.mid(i + 1, j - i - 1).toLower();
    if (keyword == QLatin1String("import")) {
      int k = j;
      const QString target = readUrlTarget(text, k, n);
      if (!target.isEmpty()) { imports << target; }
      int semi = findDelim(text, k, n, QLatin1Char(';'));
      i = (semi < 0) ? n : semi + 1;
    } else {
      // other at-rule: skip its (optional) balanced block
      int brace = findDelim(text, j, n, QLatin1Char('{'));
      if (brace < 0) {
        int semi = findDelim(text, j, n, QLatin1Char(';'));
        i = (semi < 0) ? n : semi + 1;
      } else {
        int end = matchingBrace(text, brace, n);
        i = (end < 0) ? n : end + 1;
      }
    }
  }
  return imports;
}

QString resolveImportPath(const QString& target, const QString& baseDir) {
  // No remote fetch in v1 — only local + qrc imports.
  if (target.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
      target.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
    return {};
  }
  if (target.startsWith(QLatin1Char(':')) || QDir::isAbsolutePath(target)) {
    return target;  // resource path or absolute filesystem path
  }
  return QDir::cleanPath(baseDir + QLatin1Char('/') + target);
}

// Strip a single layer of matching surrounding quotes (" or '), if present.
QString unquote(const QString& s) {
  if (s.size() >= 2 && ((s.front() == QLatin1Char('"') && s.back() == QLatin1Char('"')) ||
                        (s.front() == QLatin1Char('\'') && s.back() == QLatin1Char('\'')))) {
    return s.mid(1, s.size() - 2);
  }
  return s;
}

// Every LOCAL file referenced via url(...) in `text` (comments stripped first),
// resolved against `dir`, returned as absolute cleaned paths. data: URIs and
// remote URLs are skipped; missing files are dropped. Used both for @font-face
// src capture and for import-time resource discovery (fonts + background
// images). resolveImportPath handles :/ qrc, absolute, and relative paths and
// rejects http/https.
QStringList extractLocalUrlTargets(const QString& text, const QString& dir) {
  QStringList out;
  static const QRegularExpression urlRe(QStringLiteral("url\\(\\s*([^)]*?)\\s*\\)"),
                                        QRegularExpression::CaseInsensitiveOption);
  auto it = urlRe.globalMatch(stripComments(text));
  while (it.hasNext()) {
    QString target = unquote(it.next().captured(1).trimmed());
    if (target.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) { continue; }
    const QString resolved = resolveImportPath(target, dir);
    if (resolved.isEmpty()) { continue; }
    const QString canon = QDir::cleanPath(resolved);
    if (QFileInfo(canon).isFile()) { out << canon; }
  }
  return out;
}

// Parse a @keyframes block body into stops. Body looks like:
//   `from { opacity: 1 } 50% { opacity: .3 } to { opacity: 1 }`
// Stop selectors may be a comma list ("0%, 50%"); positions: from=0, to=1, N%.
std::vector<CssKeyframeStop> parseKeyframesStops(const QString& blockText) {
  std::vector<CssKeyframeStop> stops;
  const int n = blockText.size();
  int i = 0;
  while (i < n) {
    i = skipWsComments(blockText, i, n);
    if (i >= n) { break; }
    const int brace = findDelim(blockText, i, n, QLatin1Char('{'));
    if (brace < 0) { break; }
    const QString selText = blockText.mid(i, brace - i).trimmed();
    const int end = matchingBrace(blockText, brace, n);
    const QString declText = blockText.mid(brace + 1, (end < 0 ? n : end) - (brace + 1));
    const std::vector<CssDeclaration> decls = parseDeclarations(declText);
    for (const QString& sel : CssThemeParser::splitTopLevelCommas(selText)) {
      const QString t = sel.trimmed().toLower();
      qreal pos = -1.0;
      if (t == QStringLiteral("from")) { pos = 0.0; }
      else if (t == QStringLiteral("to")) { pos = 1.0; }
      else if (t.endsWith(QLatin1Char('%'))) {
        bool ok = false;
        pos = t.left(t.size() - 1).toDouble(&ok) / 100.0;
        if (!ok) { pos = -1.0; }
      }
      if (pos >= 0.0) {
        stops.push_back({pos, decls});
      }
    }
    i = (end < 0) ? n : end + 1;
  }
  std::sort(stops.begin(), stops.end(),
            [](const CssKeyframeStop& a, const CssKeyframeStop& b) { return a.position < b.position; });
  return stops;
}

enum class ScreenMediaMatch {
  NoMatch,
  LightScope,
  DarkScope,
};

// Theme CSS is evaluated for Muffin's editor surface, which is always a screen
// medium. Resolve only queries whose truth is independent of the current
// viewport. Width/orientation/resolution queries stay unresolved (and are
// therefore skipped) until the theme model can evaluate them per viewport.
// A comma-separated media list is OR: any unconditional screen branch wins.
ScreenMediaMatch matchScreenMediaQuery(const QString& rawQuery) {
  bool matchedDark = false;
  for (QString query : CssThemeParser::splitTopLevelCommas(rawQuery)) {
    query = query.simplified().toLower();
    if (query.isEmpty()) { continue; }

    if (query.startsWith(QStringLiteral("only "))) {
      query = query.mid(5).trimmed();
    }
    if (query.startsWith(QStringLiteral("not "))) {
      const QString negated = query.mid(4).trimmed();
      if (negated == QStringLiteral("print") || negated == QStringLiteral("speech")) {
        return ScreenMediaMatch::LightScope;
      }
      // `not screen`, `not all`, and feature negation do not provide an
      // unconditional screen match.
      continue;
    }

    QString conditions;
    if (query == QStringLiteral("screen") || query == QStringLiteral("all")) {
      return ScreenMediaMatch::LightScope;
    }
    if (query.startsWith(QStringLiteral("screen and "))) {
      conditions = query.mid(11).trimmed();
    } else if (query.startsWith(QStringLiteral("all and "))) {
      conditions = query.mid(8).trimmed();
    } else if (query.startsWith(QLatin1Char('('))) {
      conditions = query;
    } else {
      // `print`, `speech`, unknown media types, and malformed queries.
      continue;
    }

    if (conditions == QStringLiteral("(prefers-color-scheme: light)")) {
      return ScreenMediaMatch::LightScope;
    }
    if (conditions == QStringLiteral("(prefers-color-scheme: dark)")) {
      matchedDark = true;
    }
    // Any other feature depends on runtime environment/viewport and is skipped.
  }
  return matchedDark ? ScreenMediaMatch::DarkScope : ScreenMediaMatch::NoMatch;
}

// Parse top-level + @media-nested rules out of `text` into `sheet`. `baseDir`
// is the owning CSS file's directory — used to resolve @font-face src url() to
// absolute font paths (a font declared in an @import'd base must resolve
// relative to that base, not the top file).
void parseRules(const QString& text, CssThemeSheet& sheet, bool darkScope, const QString& baseDir) {
  const int n = text.size();
  int i = 0;
  while (i < n) {
    i = skipWsComments(text, i, n);
    if (i >= n) { break; }
    if (text.at(i) == QLatin1Char('@')) {
      int j = i + 1;
      while (j < n) {
        const QChar c = text.at(j);
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) { ++j; } else { break; }
      }
      const QString keyword = text.mid(i + 1, j - i - 1).toLower();
      int k = skipWsComments(text, j, n);
      if (keyword == QLatin1String("import")) {
        // already merged in the first pass; skip to ';'
        int semi = findDelim(text, k, n, QLatin1Char(';'));
        i = (semi < 0) ? n : semi + 1;
        continue;
      }
      int brace = findDelim(text, k, n, QLatin1Char('{'));
      int semi = findDelim(text, k, n, QLatin1Char(';'));
      if (brace < 0 || (semi >= 0 && semi < brace)) {
        // Block-less at-rule (e.g. @charset, @include-when-export url(...);):
        // no block of its own — consume to ';'. Without this, a ';'-terminated
        // at-rule sitting directly before a rule would swallow that rule's '{'
        // as its body and silently drop it (e.g. losing :root variables when no
        // @font-face block happens to sit between them).
        i = (semi < 0) ? n : semi + 1;
        continue;
      }
      int end = matchingBrace(text, brace, n);
      const QString blockText = text.mid(brace + 1, (end < 0 ? n : end) - (brace + 1));
      if (keyword == QLatin1String("media")) {
        const ScreenMediaMatch match = matchScreenMediaQuery(text.mid(k, brace - k));
        if (match != ScreenMediaMatch::NoMatch) {
          parseRules(blockText, sheet,
                     darkScope || match == ScreenMediaMatch::DarkScope, baseDir);
        }
      } else if (keyword == QLatin1String("font-face")) {
        // Capture family + local src font files so they can be registered with
        // QFontDatabase and the theme's font-family stacks resolve to the bundled
        // typefaces (e.g. phycat's LXGW WenKai / CascadiaCode). data:/remote src
        // and missing files are skipped by extractLocalUrlTargets. A @font-face
        // in an @import'd base resolves against that base's dir because parse()
        // recurses with the sub-sheet's own baseDir.
        QString family;
        for (const CssDeclaration& d : parseDeclarations(blockText)) {
          if (d.property == QStringLiteral("font-family") && family.isEmpty()) {
            family = unquote(d.value.trimmed());
          }
        }
        if (!family.isEmpty()) {
          for (const QString& srcPath : extractLocalUrlTargets(blockText, baseDir)) {
            sheet.addFontFace({family, srcPath});
          }
        }
      } else if (keyword == QLatin1String("keyframes")) {
        // Capture name + stops so the mapper/sampler can animate supported
        // properties (opacity/box-shadow/transform). text between the keyword
        // and '{' is the name (e.g. `@keyframes pulse` → "pulse").
        const QString name = text.mid(k, brace - k).trimmed();
        std::vector<CssKeyframeStop> stops = parseKeyframesStops(blockText);
        if (!name.isEmpty() && !stops.empty()) {
          sheet.addKeyframes({name, std::move(stops)});
        }
      }
      // all other at-rules (@page, …): ignored
      i = (end < 0) ? n : end + 1;
      continue;
    }
    // normal rule: selectors { declarations }
    int brace = findDelim(text, i, n, QLatin1Char('{'));
    if (brace < 0) { break; }
    const QString selectorText = text.mid(i, brace - i).trimmed();
    int end = matchingBrace(text, brace, n);
    const QString blockText = text.mid(brace + 1, (end < 0 ? n : end) - (brace + 1));
    if (!selectorText.isEmpty()) {
      const QStringList selectors = CssThemeParser::splitTopLevelCommas(selectorText);
      const bool isRoot =
          selectors.size() == 1 && selectors.first().trimmed() == QStringLiteral(":root");
      if (isRoot) {
        std::vector<CssDeclaration> elementDecls;
        for (const CssDeclaration& d : parseDeclarations(blockText)) {
          if (d.property.startsWith(QLatin1String("--"))) {
            sheet.setVariable(d.property, d.value);
          } else {
            elementDecls.push_back(d);
          }
        }
        // Keep the non-variable :root declarations as a rule so they reach the CSS
        // engines, which model :root as the html root element — this lets
        // `:root { font-size / color / background / … }` apply instead of being
        // silently dropped. (Variables are still resolved separately above.)
        if (!elementDecls.empty()) {
          CssRule rule;
          rule.selectors = selectors;
          rule.declarations = std::move(elementDecls);
          rule.darkScope = darkScope;
          sheet.addRule(std::move(rule));
        }
      } else {
        CssRule rule;
        rule.selectors = selectors;
        rule.declarations = parseDeclarations(blockText);
        rule.darkScope = darkScope;
        sheet.addRule(std::move(rule));
      }
    }
    i = (end < 0) ? n : end + 1;
  }
}

}  // namespace

QStringList CssThemeParser::splitTopLevelCommas(const QString& text) {
  QStringList out;
  const int n = text.size();
  int start = 0;
  int i = 0;
  while (i < n) {
    const QChar c = text.at(i);
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
      int tmp = i;
      tryReadString(text, tmp, n);
      i = tmp;
      continue;
    }
    if (c == QLatin1Char('(')) {
      int close = findDelim(text, i + 1, n, QLatin1Char(')'));
      i = (close < 0) ? n : close + 1;
      continue;
    }
    if (c == QLatin1Char(',')) {
      out << text.mid(start, i - start).trimmed();
      start = i + 1;
    }
    ++i;
  }
  out << text.mid(start).trimmed();
  out.removeAll(QString());
  return out;
}

CssThemeSheet CssThemeParser::parse(const QString& text, const QString& baseDir) {
  CssThemeSheet sheet;
  // Imports first: their rules/variables merge under this file's so the
  // importing file wins on ties (CSS cascade for top-of-file @import).
  for (const QString& target : collectImports(text)) {
    const QString resolved = resolveImportPath(target, baseDir);
    if (resolved.isEmpty()) { continue; }
    QFile f(resolved);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { continue; }
    const QString subText = QString::fromUtf8(f.readAll());
    f.close();
    CssThemeSheet sub = parse(subText, QFileInfo(resolved).absolutePath());
    sheet.mergeIn(sub);
  }
  parseRules(text, sheet, false, baseDir);
  return sheet;
}

QStringList CssThemeParser::localResourcePaths(const QString& text, const QString& baseDir) {
  // Recursively walk @import targets (so an imported base and its own @imports
  // are included) and collect every local url() resource (fonts, background
  // images) from each file. Returns absolute, cleaned, de-duplicated paths.
  // Used at import time to mirror the theme's folder next to the installed top
  // file so @import/@font-face resolve at runtime.
  QStringList out;
  QSet<QString> seen;
  const std::function<void(const QString&, const QString&, int)> walk =
      [&](const QString& fileText, const QString& fileDir, int depth) {
        if (depth > 16) { return; }
        for (const QString& target : collectImports(fileText)) {
          const QString resolved = resolveImportPath(target, fileDir);
          if (resolved.isEmpty() || !QFileInfo(resolved).isFile()) { continue; }
          const QString canon = QDir::cleanPath(resolved);
          if (seen.contains(canon)) { continue; }
          seen.insert(canon);
          out << canon;
          QFile f(canon);
          if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString sub = QString::fromUtf8(f.readAll());
            f.close();
            walk(sub, QFileInfo(canon).absolutePath(), depth + 1);
          }
        }
        for (const QString& res : extractLocalUrlTargets(fileText, fileDir)) {
          if (!seen.contains(res)) { seen.insert(res); out << res; }
        }
      };
  walk(text, baseDir, 0);
  return out;
}

QString CssThemeParser::resolveVars(const QString& value, const QHash<QString, QString>& variables) {
  // Iteratively replace var(--x [, fallback]) with resolved text. Bounded loops
  // guard against malformed input / cycles.
  QString out = value;
  for (int guard = 0; guard < 64; ++guard) {
    const int idx = out.indexOf(QStringLiteral("var("), 0, Qt::CaseInsensitive);
    if (idx < 0) { break; }
    const int close = findDelim(out, idx + 4, out.size(), QLatin1Char(')'));
    if (close < 0) { break; }
    const QString inside = out.mid(idx + 4, close - (idx + 4)).trimmed();
    // split name [, fallback] on first top-level comma
    const int comma = findDelim(inside, 0, inside.size(), QLatin1Char(','));
    const QString name = (comma < 0 ? inside : inside.left(comma)).trimmed().toLower();
    const QString fallback = (comma < 0 ? QString() : inside.mid(comma + 1).trimmed());
    QString replacement;
    auto it = variables.constFind(name);
    if (it != variables.constEnd()) {
      replacement = it.value();
    } else if (!fallback.isEmpty()) {
      replacement = fallback;
    }
    out = out.left(idx) + replacement + out.mid(close + 1);
  }
  return out;
}

}  // namespace muffin
