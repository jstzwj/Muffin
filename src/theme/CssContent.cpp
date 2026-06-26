#include "theme/CssContent.h"

#include "theme/CssThemeParser.h"

#include <QChar>
#include <QStringList>
#include <QtGlobal>

namespace muffin {

QString formatCounterValue(int value, const QString& style) {
  if (value < 1) { return QString::number(value); }
  if (style == QStringLiteral("decimal-leading-zero")) {
    return QString::number(value).rightJustified(2, QLatin1Char('0'));
  }
  if (style == QStringLiteral("lower-alpha") || style == QStringLiteral("lower-latin")) {
    QString s;
    int v = value;
    while (v > 0) { --v; s.prepend(QChar(QLatin1Char('a').toLatin1() + v % 26)); v /= 26; }
    return s;
  }
  if (style == QStringLiteral("upper-alpha") || style == QStringLiteral("upper-latin")) {
    QString s;
    int v = value;
    while (v > 0) { --v; s.prepend(QChar(QLatin1Char('A').toLatin1() + v % 26)); v /= 26; }
    return s;
  }
  if (style == QStringLiteral("lower-roman") || style == QStringLiteral("upper-roman")) {
    static const struct { int v; const char* s; } table[] = {
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
        {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}};
    QString s;
    int v = value;
    for (const auto& e : table) {
      while (v >= e.v) { s += QLatin1String(e.s); v -= e.v; }
    }
    return style == QStringLiteral("upper-roman") ? s.toUpper() : s;
  }
  if (style == QStringLiteral("lower-greek")) {
    // Greek lowercase α..ω. U+03C2 (final sigma ς) is NOT part of the CSS
    // lower-greek alphabet, so a naive 0x03B1+i walks into ς at index 17 and
    // shifts every letter from σ onward (σ→ς, τ→σ, …, ω dropped). Skip ς.
    QString s;
    int v = value;
    while (v > 0) {
      --v;
      int cp = 0x03B1 + (v % 24);
      if (cp >= 0x03C2) { ++cp; }  // jump over U+03C2 final sigma
      s.prepend(QChar(char16_t(cp)));
      v /= 24;
    }
    return s;
  }
  return QString::number(value);  // decimal / default
}

namespace {

// Read the balanced (...) argument of a `name(` function whose '(' is at index
// `at`. Returns the inner text (trimmed).
QString readParenArg(const QString& s, int at) {
  int depth = 0;
  int i = at;
  for (; i < s.size(); ++i) {
    if (s.at(i) == QLatin1Char('(')) { ++depth; }
    else if (s.at(i) == QLatin1Char(')')) {
      --depth;
      if (depth == 0) { break; }
    }
  }
  return s.mid(at + 1, (i - at) - 1).trimmed();
}

QString unquote(const QString& s) {
  QString t = s.trimmed();
  if (t.size() >= 2 && ((t.front() == QLatin1Char('"') && t.back() == QLatin1Char('"')) ||
                        (t.front() == QLatin1Char('\'') && t.back() == QLatin1Char('\'')))) {
    return t.mid(1, t.size() - 2);
  }
  return t;
}

}  // namespace

std::vector<ContentToken> parseContentTokens(const QString& content) {
  // CSS `content` is a whitespace-separated list of <string> and function tokens
  // (counter()/counters()/attr()/…). Split at top-level whitespace (respecting
  // parens and quotes), then classify each part.
  std::vector<ContentToken> out;
  const auto addLiteral = [&out](const QString& text) {
    out.push_back({ContentToken::Kind::Literal, text, QString(), QString()});
  };
  const int n = content.size();
  int i = 0;
  while (i < n) {
    while (i < n && content.at(i).isSpace()) { ++i; }
    if (i >= n) { break; }
    const int start = i;
    int paren = 0;
    bool inStr = false;
    QChar quote;
    while (i < n) {
      const QChar c = content.at(i);
      if (inStr) {
        if (c == quote) { inStr = false; }
      } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
        inStr = true; quote = c;
      } else if (c == QLatin1Char('(')) {
        ++paren;
      } else if (c == QLatin1Char(')')) {
        --paren;
      } else if (c.isSpace() && paren == 0) {
        break;
      }
      ++i;
    }
    const QString part = content.mid(start, i - start);
    if (part.trimmed().isEmpty()) { continue; }
    if (part.size() >= 2 && ((part.front() == QLatin1Char('"') && part.back() == QLatin1Char('"')) ||
                             (part.front() == QLatin1Char('\'') && part.back() == QLatin1Char('\'')))) {
      addLiteral(part.mid(1, part.size() - 2));
    } else if (part.startsWith(QStringLiteral("counters("), Qt::CaseInsensitive)) {
      // '(' sits at start+8 ("counters" is 8 chars). splitTopLevelCommas respects
      // quotes/parens, so a separator that itself contains a comma (e.g.
      // counters(c, ", ")) is not split mid-string.
      const QStringList p = CssThemeParser::splitTopLevelCommas(readParenArg(content, start + 8));
      ContentToken t; t.kind = ContentToken::Kind::Counters;
      if (!p.isEmpty()) { t.text = p.at(0).trimmed().toLower(); }
      if (p.size() >= 2) { t.sep = unquote(p.at(1)); }
      if (p.size() >= 3) { t.style = p.at(2).trimmed().toLower(); }
      out.push_back(t);
    } else if (part.startsWith(QStringLiteral("counter("), Qt::CaseInsensitive)) {
      // '(' sits at start+7 ("counter" is 7 chars).
      const QStringList p = CssThemeParser::splitTopLevelCommas(readParenArg(content, start + 7));
      ContentToken t; t.kind = ContentToken::Kind::Counter;
      if (!p.isEmpty()) { t.text = p.at(0).trimmed().toLower(); }
      if (p.size() >= 2) { t.style = p.at(1).trimmed().toLower(); }
      out.push_back(t);
    } else {
      addLiteral(part);
    }
  }
  return out;
}

QString resolveContentTokens(const std::vector<ContentToken>& tokens,
                             const std::function<int(const QString&)>& value,
                             const std::function<QVector<int>(const QString&)>& chain) {
  QString out;
  for (const ContentToken& t : tokens) {
    if (t.kind == ContentToken::Kind::Literal) {
      out += t.text;
    } else if (t.kind == ContentToken::Kind::Counter) {
      out += formatCounterValue(value(t.text), t.style);
    } else {  // Counters
      const QVector<int> levels = chain(t.text);
      for (int li = 0; li < levels.size(); ++li) {
        if (li > 0) { out += t.sep; }
        out += formatCounterValue(levels.at(li), t.style);
      }
    }
  }
  return out;
}

}  // namespace muffin
