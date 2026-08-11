#include "mermaid/treemap/TreemapDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QHash>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace muffin::mermaid::treemap {
namespace {

bool isHorizontal(QChar c) {
  return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

bool isClassStart(QChar c) {
  return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
         (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
         c == QLatin1Char('_');
}

bool isClassPart(QChar c) { return isClassStart(c) || c.isDigit(); }

QString metadata(QString value) {
  return HtmlSanitizer().sanitizedMermaidText(value.trimmed());
}

QString accDescription(QString value) {
  value = metadata(std::move(value));
  value.replace(QRegularExpression(QStringLiteral(R"(\n\s+)")),
                QStringLiteral("\n"));
  return value;
}

QStringList compileStyles(const QString &source) {
  QStringList result;
  QString item;
  int parentheses = 0;
  QChar quote;
  for (QChar c : source) {
    if (!quote.isNull()) {
      item += c;
      if (c == quote)
        quote = QChar();
      continue;
    }
    if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
      quote = c;
      item += c;
    } else if (c == QLatin1Char('(')) {
      ++parentheses;
      item += c;
    } else if (c == QLatin1Char(')')) {
      parentheses = qMax(0, parentheses - 1);
      item += c;
    } else if (c == QLatin1Char(',') && parentheses == 0) {
      if (!item.trimmed().isEmpty())
        result.append(item.trimmed());
      item.clear();
    } else {
      item += c;
    }
  }
  if (!item.trimmed().isEmpty())
    result.append(item.trimmed());
  return result;
}

double jsParseFloat(QString text) {
  text.remove(QLatin1Char(','));
  static const QRegularExpression prefix(
      QStringLiteral(R"(^[\t-\r \x{00a0}\x{feff}]*([+-]?(?:(?:\d+\.?\d*)|(?:\.\d+))(?:[eE][+-]?\d+)?)?)"));
  const auto match = prefix.match(text);
  if (!match.hasMatch() || match.captured(1).isEmpty())
    return std::numeric_limits<double>::quiet_NaN();
  return match.captured(1).toDouble();
}

class Parser {
public:
  explicit Parser(QString source) : source_(std::move(source)) {
    source_.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source_.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  }

  TreemapData run() {
    skipHidden();
    const int headerStart = index_;
    if (source_.mid(index_, 12).compare(QStringLiteral("treemap-beta"),
                                        Qt::CaseInsensitive) == 0)
      index_ += 12;
    else if (source_.mid(index_, 7).compare(QStringLiteral("treemap"),
                                            Qt::CaseInsensitive) == 0)
      index_ += 7;
    else
      parser(headerStart, QStringLiteral("Expected Treemap header"));
    if (index_ < source_.size() && !source_.at(index_).isSpace() &&
        source_.at(index_) != QLatin1Char('"') &&
        source_.at(index_) != QLatin1Char('\''))
      parser(index_, QStringLiteral("Expected end of Treemap header"));

    while (true) {
      skipHidden();
      if (index_ >= source_.size())
        break;
      if (atWord(QStringLiteral("title"))) {
        parseTitle();
      } else if (atWord(QStringLiteral("accTitle"))) {
        parseAccTitle();
      } else if (atWord(QStringLiteral("accDescr"))) {
        parseAccDescr();
      } else if (atWord(QStringLiteral("classDef"))) {
        parseClassDef();
      } else if (source_.at(index_) == QLatin1Char('"') ||
                 source_.at(index_) == QLatin1Char('\'')) {
        parseNode();
      } else {
        parser(index_, QStringLiteral("Expected quoted Treemap row"));
      }
    }

    buildHierarchy();
    QHash<QString, QStringList> definitions;
    for (const auto &definition : data_.classes)
      definitions.insert(definition.id, definition.styles);
    for (auto &node : data_.nodes) {
      if (!node.classSelector.isEmpty())
        node.cssCompiledStyles = definitions.value(node.classSelector);
    }
    return data_;
  }

private:
  bool atWord(const QString &word) const {
    if (source_.mid(index_, word.size()).compare(word, Qt::CaseInsensitive) != 0)
      return false;
    const int end = index_ + word.size();
    return end >= source_.size() || !source_.at(end).isLetterOrNumber();
  }

  bool consumeWord(const QString &word) {
    if (!atWord(word))
      return false;
    index_ += word.size();
    return true;
  }

  void skipHidden() {
    while (index_ < source_.size()) {
      if (source_.at(index_).isSpace()) {
        ++index_;
        continue;
      }
      if (source_.mid(index_, 2) == QLatin1String("%%")) {
        while (index_ < source_.size() &&
               source_.at(index_) != QLatin1Char('\n'))
          ++index_;
        continue;
      }
      break;
    }
  }

  void skipHorizontal() {
    while (index_ < source_.size() && isHorizontal(source_.at(index_)))
      ++index_;
  }

  QString restOfLine() {
    const int start = index_;
    while (index_ < source_.size() && source_.at(index_) != QLatin1Char('\n'))
      ++index_;
    return source_.mid(start, index_ - start);
  }

  void parseTitle() {
    consumeWord(QStringLiteral("title"));
    if (index_ >= source_.size() || !isHorizontal(source_.at(index_)))
      parser(index_, QStringLiteral("Expected title text"));
    skipHorizontal();
    data_.title = metadata(restOfLine());
    data_.hasTitleDirective = true;
  }

  void parseAccTitle() {
    consumeWord(QStringLiteral("accTitle"));
    skipHorizontal();
    if (index_ >= source_.size() || source_.at(index_) != QLatin1Char(':'))
      parser(index_, QStringLiteral("Expected ':' after accTitle"));
    ++index_;
    skipHorizontal();
    data_.accTitle = metadata(restOfLine());
  }

  void parseAccDescr() {
    consumeWord(QStringLiteral("accDescr"));
    skipHorizontal();
    if (index_ < source_.size() && source_.at(index_) == QLatin1Char(':')) {
      ++index_;
      skipHorizontal();
      data_.accDescr = accDescription(restOfLine());
      return;
    }
    if (index_ >= source_.size() || source_.at(index_) != QLatin1Char('{'))
      parser(index_, QStringLiteral("Expected ':' or '{' after accDescr"));
    ++index_;
    const int start = index_;
    while (index_ < source_.size() && source_.at(index_) != QLatin1Char('}'))
      ++index_;
    if (index_ >= source_.size())
      parser(start, QStringLiteral("Unterminated accDescr block"));
    data_.accDescr = accDescription(source_.mid(start, index_ - start));
    ++index_;
  }

  void parseClassDef() {
    const int start = index_;
    consumeWord(QStringLiteral("classDef"));
    if (index_ >= source_.size() || !isHorizontal(source_.at(index_)))
      parser(start, QStringLiteral("Invalid classDef"));
    skipHorizontal();
    if (index_ >= source_.size() || !isClassStart(source_.at(index_)))
      parser(start, QStringLiteral("Invalid classDef name"));
    const int idStart = index_++;
    while (index_ < source_.size() && isClassPart(source_.at(index_)))
      ++index_;
    const QString id = source_.mid(idStart, index_ - idStart);
    skipHorizontal();
    QString styles = restOfLine().trimmed();
    if (styles.endsWith(QLatin1Char(';')))
      styles.chop(1);
    data_.classes.append({id, compileStyles(styles)});
  }

  QString parseString() {
    const QChar quote = source_.at(index_++);
    const int start = index_;
    while (index_ < source_.size() && source_.at(index_) != quote)
      ++index_;
    if (index_ >= source_.size())
      lexer(start, QStringLiteral("Unterminated quoted Treemap name"));
    const QString result = source_.mid(start, index_ - start);
    ++index_;
    return result;
  }

  QString parseClassSelector() {
    if (source_.mid(index_, 3) != QLatin1String(":::"))
      return {};
    index_ += 3;
    skipHorizontal();
    if (index_ >= source_.size() || !isClassStart(source_.at(index_)))
      parser(index_, QStringLiteral("Expected class selector"));
    const int start = index_++;
    while (index_ < source_.size() && isClassPart(source_.at(index_)))
      ++index_;
    return source_.mid(start, index_ - start);
  }

  int rowIndent(int stringStart) const {
    int lineStart = stringStart;
    while (lineStart > 0 && source_.at(lineStart - 1) != QLatin1Char('\n'))
      --lineStart;
    for (int i = lineStart; i < stringStart; ++i)
      if (!isHorizontal(source_.at(i)))
        return 0;
    return stringStart - lineStart;
  }

  void parseNode() {
    const int stringStart = index_;
    TreemapNode node;
    node.level = rowIndent(stringStart);
    node.name = parseString();
    skipHorizontal();
    if (source_.mid(index_, 3) != QLatin1String(":::") &&
        index_ < source_.size() &&
        (source_.at(index_) == QLatin1Char(':') ||
         source_.at(index_) == QLatin1Char(','))) {
      node.hasValue = true;
      const int delimiter = index_;
      ++index_;
      skipHorizontal();
      const int numberStart = index_;
      while (index_ < source_.size()) {
        const QChar c = source_.at(index_);
        if (!(c.isDigit() || c == QLatin1Char('_') || c == QLatin1Char('.') ||
              c == QLatin1Char(',')))
          break;
        ++index_;
      }
      if (index_ == numberStart) {
        if (index_ >= source_.size() ||
            source_.at(index_) == QLatin1Char('\n'))
          parser(delimiter, QStringLiteral("Expected Treemap number"));
        lexer(index_, QStringLiteral("Invalid Treemap number"));
      }
      node.value = jsParseFloat(source_.mid(numberStart, index_ - numberStart));
      skipHorizontal();
    }
    node.classSelector = parseClassSelector();
    data_.nodes.append(std::move(node));
  }

  void buildHierarchy() {
    QVector<int> stack;
    for (int index = 0; index < data_.nodes.size(); ++index) {
      const int level = data_.nodes.at(index).level;
      while (!stack.isEmpty() && data_.nodes.at(stack.back()).level >= level)
        stack.pop_back();
      if (stack.isEmpty())
        data_.roots.append(index);
      else
        data_.nodes[stack.back()].children.append(index);
      stack.append(index);
    }
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
    throw TreemapParseError(TreemapErrorKind::Parser, line, column,
                            offset < source_.size() ? source_.mid(offset, 1)
                                                    : QStringLiteral("EOF"),
                            message);
  }

  [[noreturn]] void lexer(int offset, const QString &message) const {
    const auto [line, column] = location(offset);
    throw TreemapParseError(TreemapErrorKind::Lexer, line, column,
                            offset < source_.size() ? source_.mid(offset, 1)
                                                    : QStringLiteral("EOF"),
                            message);
  }

  QString source_;
  int index_ = 0;
  TreemapData data_;
};

} // namespace

TreemapParseError::TreemapParseError(TreemapErrorKind errorKind, int errorLine,
                                     int errorColumn, QString errorToken,
                                     const QString &message)
    : std::runtime_error(message.toStdString()), kind(errorKind),
      line(errorLine), column(errorColumn), token(std::move(errorToken)) {}

TreemapData TreemapDiagram::parse(const QString &source) {
  return Parser(source).run();
}

} // namespace muffin::mermaid::treemap
