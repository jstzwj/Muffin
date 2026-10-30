#include "app/WorkspaceIgnoreRules.h"

namespace {
QString glob(QStringView pattern) {
  QString result;
  for (qsizetype i = 0; i < pattern.size(); ++i) {
    const auto c = pattern[i];
    if (c == u'\\') {
      if (++i == pattern.size())
        return {};
      result += QRegularExpression::escape(pattern.mid(i, 1).toString());
    } else if (c == u'*') {
      const auto begin = i;
      while (i + 1 < pattern.size() && pattern[i + 1] == u'*')
        ++i;
      if (i > begin && (begin == 0 || pattern[begin - 1] == u'/') &&
          (i + 1 == pattern.size() || pattern[i + 1] == u'/')) {
        if (i + 1 < pattern.size()) {
          result += QStringLiteral("(?:[^/]+/)*");
          ++i;
        } else
          result += QStringLiteral(".*");
      } else
        result += QStringLiteral("[^/]*");
    } else if (c == u'?')
      result += QStringLiteral("[^/]");
    else if (c == u'[') {
      auto end = i + 1;
      if (end < pattern.size() && (pattern[end] == u'!' || pattern[end] == u'^'))
        ++end;
      if (end < pattern.size() && pattern[end] == u']')
        ++end;
      while (end < pattern.size() && pattern[end] != u']') {
        // Keep POSIX character classes inside the surrounding character class.
        if (pattern[end] == u'[' && end + 1 < pattern.size() && pattern[end + 1] == u':') {
          const auto close = pattern.indexOf(QStringView(u":]"), end + 2);
          if (close >= 0) {
            end = close + 2;
            continue;
          }
        }
        if (pattern[end] == u'\\' && end + 1 < pattern.size())
          ++end;
        ++end;
      }
      if (end == pattern.size())
        return {};
      auto chars = pattern.mid(i + 1, end - i - 1).toString();
      if (chars.startsWith(u'!'))
        chars[0] = u'^';
      result += QStringLiteral("(?!/)[") + chars + u']';
      i = end;
    } else
      result += QRegularExpression::escape(QString(c));
  }
  return result;
}
}

void muffin::WorkspaceIgnoreRules::append(QStringView text, const QString& base) {
  for (auto line : text.split(u'\n')) {
    if (line.endsWith(u'\r'))
      line.chop(1);
    // Only unescaped trailing spaces are discarded.
    while (line.endsWith(u' ')) {
      qsizetype slashes = 0;
      for (auto i = line.size() - 2; i >= 0 && line[i] == u'\\'; --i)
        ++slashes;
      if (slashes % 2)
        break;
      line.chop(1);
    }
    if (line.isEmpty() || line.startsWith(u'#'))
      continue;
    Rule rule;
    rule.base = base.isEmpty() ? QString() : base + u'/';
    if (line.startsWith(u'!')) {
      rule.negated = true;
      line = line.mid(1);
    }
    rule.directoryOnly = line.endsWith(u'/');
    if (rule.directoryOnly)
      line.chop(1);
    const bool anchored = line.contains(u'/');
    if (line.startsWith(u'/'))
      line = line.mid(1);
    const auto expression = glob(line);
    if (expression.isEmpty())
      continue;
    QRegularExpression::PatternOptions flags = QRegularExpression::DotMatchesEverythingOption;
#ifdef Q_OS_WIN
    flags |= QRegularExpression::CaseInsensitiveOption;
#endif
    rule.pattern = QRegularExpression(QStringLiteral("\\A") + (anchored ? QString() : QStringLiteral("(?:.*/)?")) +
                                          expression + QStringLiteral("\\z"),
                                      flags);
    if (rule.pattern.isValid())
      rules_.append(std::move(rule));
  }
}

bool muffin::WorkspaceIgnoreRules::ignored(const QString& relativePath, bool directory, bool previous) const {
  for (const auto& rule : rules_) {
    if ((!rule.directoryOnly || directory) && relativePath.startsWith(rule.base) &&
        rule.pattern.matchView(QStringView(relativePath).mid(rule.base.size())).hasMatch())
      previous = !rule.negated;
  }
  return previous;
}
