#include "theme/CssSelectorAnalysis.h"

#include "theme/TyporaEditorOnly.h"

#include <QChar>
#include <QString>

namespace muffin {

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
      else if (cls == QStringLiteral("mathjax")) { info.classMathJax = true; }
      else if (cls == QStringLiteral("md-focus")) { info.mdFocus = true; }
      if (isTyporaEditorOnlyClass(cls)) { info.editorOnly = true; }
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
        else if (name == QStringLiteral("root")) {
          // :root matches the document root element (html). Route it as a tag selector for
          // "html" so :root element declarations (font-size, color, background, …) reach the
          // root via the isHtmlOrBody predicate, instead of being silently dropped. (:root CSS
          // variables are still collected earlier by CssThemeParser.)
          if (info.tag.isEmpty()) { info.tag = QStringLiteral("html"); }
        }
        else if (name == QStringLiteral("not")) {
          // Safe no-op in the flattened semantic mapper: `a:not(.md-toc-inner)`
          // should still feed the normal link token. (Complex :not() args aren't modelled here.)
        }
        else if ((name == QStringLiteral("nth-child") || name == QStringLiteral("nth-of-type")) &&
                 (arg == QStringLiteral("even") || arg.contains(QStringLiteral("2n")))) {
          info.nthEven = true;
        }
        else {
          // Structural pseudos such as :has(img), :last-child or :first-of-type
          // cannot be evaluated against this prototype-free flat view. Treating
          // only the rightmost tag as a match makes targeted rules global (e.g.
          // `#write p:has(img) { text-align:center }` centering every paragraph).
          info.unsupportedPseudoClass = true;
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

QString pseudoHostKey(const SelInfo& info) {
  if (info.idWrite) { return QStringLiteral("#write"); }
  if (!info.tag.isEmpty()) { return info.tag; }
  if (info.classFences) { return QStringLiteral(".md-fences"); }
  return QString();
}

}  // namespace muffin
