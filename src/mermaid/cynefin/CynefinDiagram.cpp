#include "mermaid/cynefin/CynefinDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QHash>
#include <QRegularExpression>

#include <utility>

namespace muffin::mermaid::cynefin {
namespace {

bool horizontal(QChar c) {
  return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

QString sanitizedMetadata(const QString &value) {
  return HtmlSanitizer().sanitizedMermaidText(value);
}

QString collapseInline(QString value) {
  value = value.trimmed();
  value.replace(QRegularExpression(QStringLiteral(R"([\t ]{2,})")),
                QStringLiteral(" "));
  return value;
}

QString normalizeBlock(QString value) {
  const QStringList lines = value.split(QRegularExpression(QStringLiteral(R"(\r?\n)")),
                                        Qt::KeepEmptyParts);
  QStringList normalized;
  normalized.reserve(lines.size());
  for (QString line : lines) {
    line.remove(QRegularExpression(QStringLiteral(R"(^\s*)")));
    line.remove(QRegularExpression(QStringLiteral(R"(\s+$)")));
    line.replace(QRegularExpression(QStringLiteral(R"([\t ]{2,})")),
                 QStringLiteral(" "));
    normalized.append(line);
  }
  QString result = normalized.join(QLatin1Char('\n'));
  result.replace(QRegularExpression(QStringLiteral(R"([\n\r]{2,})")),
                 QStringLiteral("\n"));
  return result;
}

class Parser {
public:
  explicit Parser(QString source) : source_(std::move(source)) {
    source_.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source_.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  }

  CynefinData run() {
    skipHidden(true);
    const int header = index_;
    if (source_.mid(index_, 13) == QLatin1String("cynefin-beta:"))
      index_ += 13;
    else if (source_.mid(index_, 12) == QLatin1String("cynefin-beta"))
      index_ += 12;
    else
      parser(header, QStringLiteral("Expected Cynefin header"));

    while (true) {
      skipHidden(true);
      if (index_ >= source_.size())
        break;
      if (startsMetadata(QStringLiteral("accDescr"))) {
        parseAccDescr();
      } else if (startsMetadata(QStringLiteral("accTitle"))) {
        parseAccTitle();
      } else if (startsMetadata(QStringLiteral("title"))) {
        parseTitle();
      } else {
        const int start = index_;
        const QString domain = parseDomain();
        skipHorizontalAndComments();
        if (source_.mid(index_, 3) == QLatin1String("-->"))
          parseTransition(domain, index_);
        else
          parseDomainBlock(domain, start);
      }
    }
    return data_;
  }

private:
  bool startsMetadata(const QString &word) const {
    if (source_.mid(index_, word.size()) != word)
      return false;
    const int end = index_ + word.size();
    if (word == QLatin1String("title"))
      return end >= source_.size() || horizontal(source_.at(end)) ||
             source_.at(end) == QLatin1Char('\n');
    return true;
  }

  void skipHorizontal() {
    while (index_ < source_.size() && horizontal(source_.at(index_)))
      ++index_;
  }

  void skipComment() {
    if (source_.mid(index_, 2) != QLatin1String("%%") ||
        source_.mid(index_, 3) == QLatin1String("%%{"))
      return;
    while (index_ < source_.size() && source_.at(index_) != QLatin1Char('\n'))
      ++index_;
  }

  void skipHidden(bool newlines) {
    while (index_ < source_.size()) {
      const int before = index_;
      skipHorizontal();
      skipComment();
      if (newlines)
        while (index_ < source_.size() && source_.at(index_) == QLatin1Char('\n'))
          ++index_;
      if (index_ == before)
        break;
    }
  }

  void skipHorizontalAndComments() {
    while (true) {
      const int before = index_;
      skipHorizontal();
      skipComment();
      if (index_ == before)
        break;
    }
  }

  void requireEol(int origin) {
    skipHorizontalAndComments();
    if (index_ >= source_.size())
      return;
    if (source_.at(index_) == QLatin1Char(';'))
      lexer(index_, QStringLiteral("Invalid Cynefin token"));
    if (source_.at(index_) != QLatin1Char('\n'))
      parser(index_, QStringLiteral("Expected end of Cynefin statement"));
    while (index_ < source_.size() && source_.at(index_) == QLatin1Char('\n'))
      ++index_;
    Q_UNUSED(origin);
  }

  QString parseDomain() {
    static const QStringList domains{
        QStringLiteral("complicated"), QStringLiteral("confusion"),
        QStringLiteral("complex"), QStringLiteral("chaotic"),
        QStringLiteral("clear")};
    for (const QString &domain : domains) {
      if (source_.mid(index_, domain.size()) == domain) {
        index_ += domain.size();
        return domain;
      }
    }
    // Langium reports the lexer failure together with the parser's first
    // surviving token. An unknown domain word is skipped as one lexer error;
    // when a quoted item follows, that item is the parser location exposed by
    // Mermaid's source-entry error hash.
    if (index_ < source_.size() && source_.at(index_).isLetter()) {
      int next = index_;
      while (next < source_.size() && source_.at(next) != QLatin1Char('\n'))
        ++next;
      while (next < source_.size() &&
             (source_.at(next) == QLatin1Char('\n') ||
              horizontal(source_.at(next))))
        ++next;
      if (next < source_.size() &&
          (source_.at(next) == QLatin1Char('"') ||
           source_.at(next) == QLatin1Char('\'')))
        lexer(next, QStringLiteral("Invalid Cynefin token"));
    }
    lexer(index_, QStringLiteral("Invalid Cynefin token"));
  }

  QString parseString() {
    if (index_ >= source_.size() ||
        (source_.at(index_) != QLatin1Char('"') &&
         source_.at(index_) != QLatin1Char('\'')))
      parser(index_, QStringLiteral("Expected quoted Cynefin label"));
    const int start = index_;
    const QChar quote = source_.at(index_++);
    QString result;
    while (index_ < source_.size()) {
      const QChar c = source_.at(index_++);
      if (c == quote)
        return result;
      if (c != QLatin1Char('\\')) {
        result += c;
        continue;
      }
      if (index_ >= source_.size())
        lexer(start, QStringLiteral("Unterminated Cynefin string"));
      const QChar escaped = source_.at(index_++);
      switch (escaped.unicode()) {
      case 'b': result += QLatin1Char('\b'); break;
      case 'f': result += QLatin1Char('\f'); break;
      case 'n': result += QLatin1Char('\n'); break;
      case 'r': result += QLatin1Char('\r'); break;
      case 't': result += QLatin1Char('\t'); break;
      case 'v': result += QChar(0x000b); break;
      case '0': result += QChar(0); break;
      default: result += escaped; break;
      }
    }
    lexer(start, QStringLiteral("Unterminated Cynefin string"));
  }

  bool stringAhead() {
    const int saved = index_;
    skipHidden(true);
    const bool result = index_ < source_.size() &&
                        (source_.at(index_) == QLatin1Char('"') ||
                         source_.at(index_) == QLatin1Char('\''));
    if (!result)
      index_ = saved;
    return result;
  }

  void parseDomainBlock(const QString &name, int) {
    QVector<CynefinItem> items;
    while (stringAhead()) {
      items.append({parseString()});
      skipHidden(true);
    }
    auto it = domainIndexes_.find(name);
    if (it == domainIndexes_.end()) {
      domainIndexes_.insert(name, data_.domains.size());
      data_.domains.append({name, std::move(items)});
    } else {
      data_.domains[*it].items = std::move(items);
    }
  }

  void parseTransition(const QString &from, int arrowOffset) {
    index_ += 3;
    skipHorizontalAndComments();
    CynefinTransition transition;
    transition.from = from;
    transition.to = parseDomain();
    skipHorizontalAndComments();
    if (index_ < source_.size() && source_.at(index_) == QLatin1Char(':')) {
      ++index_;
      skipHorizontalAndComments();
      if (index_ >= source_.size() ||
          (source_.at(index_) != QLatin1Char('"') &&
           source_.at(index_) != QLatin1Char('\'')))
        lexer(arrowOffset, QStringLiteral("Invalid Cynefin transition label"));
      transition.label = parseString();
      transition.hasLabel = !transition.label.isEmpty();
    }
    skipHorizontalAndComments();
    if (index_ < source_.size() && source_.at(index_) != QLatin1Char('\n') &&
        source_.at(index_) != QLatin1Char(';'))
      parser(arrowOffset, QStringLiteral("Expected end of Cynefin transition"));
    requireEol(index_);
    if (transition.from != transition.to)
      data_.transitions.append(std::move(transition));
  }

  QString restOfLineBeforeComment() {
    const int start = index_;
    while (index_ < source_.size() && source_.at(index_) != QLatin1Char('\n') &&
           source_.mid(index_, 2) != QLatin1String("%%"))
      ++index_;
    return source_.mid(start, index_ - start);
  }

  void parseTitle() {
    const int start = index_;
    index_ += 5;
    QString value;
    if (index_ < source_.size() && horizontal(source_.at(index_))) {
      skipHorizontal();
      value = collapseInline(restOfLineBeforeComment());
    }
    value = sanitizedMetadata(value);
    if (!value.isEmpty()) {
      data_.title = value;
      data_.hasTitleDirective = true;
    }
    requireEol(start);
  }

  void parseAccTitle() {
    const int start = index_;
    index_ += 8;
    skipHorizontal();
    if (index_ >= source_.size() || source_.at(index_) != QLatin1Char(':'))
      lexer(start, QStringLiteral("Invalid accTitle"));
    ++index_;
    QString value = sanitizedMetadata(collapseInline(restOfLineBeforeComment()));
    if (!value.isEmpty())
      data_.accTitle = value;
    requireEol(start);
  }

  void parseAccDescr() {
    const int start = index_;
    index_ += 8;
    skipHorizontal();
    QString value;
    if (index_ < source_.size() && source_.at(index_) == QLatin1Char(':')) {
      ++index_;
      value = collapseInline(restOfLineBeforeComment());
    } else {
      while (index_ < source_.size() && source_.at(index_).isSpace())
        ++index_;
      if (index_ >= source_.size() || source_.at(index_) != QLatin1Char('{'))
        lexer(start, QStringLiteral("Invalid accDescr"));
      ++index_;
      const int content = index_;
      while (index_ < source_.size() && source_.at(index_) != QLatin1Char('}'))
        ++index_;
      if (index_ >= source_.size())
        lexer(start, QStringLiteral("Unterminated accDescr"));
      value = normalizeBlock(source_.mid(content, index_ - content));
      ++index_;
    }
    value = sanitizedMetadata(value);
    if (!value.isEmpty())
      data_.accDescr = value;
    requireEol(start);
  }

  QPair<int, int> location(int offset) const {
    int line = 1;
    int column = 1;
    for (int i = 0; i < qMin(offset, source_.size()); ++i) {
      if (source_.at(i) == QLatin1Char('\n')) {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
    return {line, column};
  }

  [[noreturn]] void parser(int offset, const QString &message) const {
    const auto [line, column] = location(offset);
    throw CynefinParseError(CynefinErrorKind::Parser, line, column,
                            offset < source_.size() ? source_.mid(offset, 1)
                                                    : QStringLiteral("EOF"),
                            message);
  }

  [[noreturn]] void lexer(int offset, const QString &message) const {
    const auto [line, column] = location(offset);
    throw CynefinParseError(CynefinErrorKind::Lexer, line, column,
                            offset < source_.size() ? source_.mid(offset, 1)
                                                    : QStringLiteral("EOF"),
                            message);
  }

  QString source_;
  int index_ = 0;
  CynefinData data_;
  QHash<QString, int> domainIndexes_;
};

} // namespace

CynefinParseError::CynefinParseError(CynefinErrorKind errorKind, int errorLine,
                                     int errorColumn, QString errorToken,
                                     const QString &message)
    : std::runtime_error(message.toStdString()), kind(errorKind),
      line(errorLine), column(errorColumn), token(std::move(errorToken)) {}

CynefinData CynefinDiagram::parse(const QString &source) {
  return Parser(source).run();
}

} // namespace muffin::mermaid::cynefin
