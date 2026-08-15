#include "mermaid/railroad/RailroadDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

namespace muffin::mermaid::railroad {
namespace {

[[noreturn]] void fail(RailroadErrorKind kind, int line, int column,
                       const QString& token, const QString& message) {
  throw RailroadParseError(kind, line, column, token, message);
}

bool isAsciiSpace(QChar ch) {
  return ch == QLatin1Char(' ') || ch == QLatin1Char('\t') ||
         ch == QLatin1Char('\r') || ch == QLatin1Char('\n');
}

bool isJsWhitespace(QChar ch) {
  const ushort u = ch.unicode();
  if ((u >= 0x0009 && u <= 0x000d) || u == 0x0020 || u == 0x00a0 ||
      u == 0x1680 || u == 0x2028 || u == 0x2029 || u == 0x202f ||
      u == 0x205f || u == 0x3000 || u == 0xfeff)
    return true;
  return u >= 0x2000 && u <= 0x200a;
}

bool isIdStart(QChar ch) {
  return (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
         (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
         ch == QLatin1Char('_');
}

bool isIdContinue(QChar ch) {
  return isIdStart(ch) ||
         (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
         ch == QLatin1Char('-');
}

QString decodeEscapedString(const QString& quoted) {
  QString value;
  for (qsizetype i = 1; i + 1 < quoted.size(); ++i) {
    QChar ch = quoted.at(i);
    if (ch == QLatin1Char('\\') && i + 2 < quoted.size()) {
      const QChar escaped = quoted.at(++i);
      if (escaped == QLatin1Char('n')) value += QLatin1Char('\n');
      else if (escaped == QLatin1Char('r')) value += QLatin1Char('\r');
      else if (escaped == QLatin1Char('t')) value += QLatin1Char('\t');
      else value += escaped;
    } else {
      value += ch;
    }
  }
  return value;
}

QString collapseInline(QString value) {
  value = value.trimmed();
  static const QRegularExpression repeated(QStringLiteral(R"([\t ]{2,})"));
  value.replace(repeated, QStringLiteral(" "));
  return value;
}

QString normalizeDescription(QString value) {
  QStringList lines = value.split(QRegularExpression(QStringLiteral("\\r?\\n")),
                                  Qt::KeepEmptyParts);
  for (QString& line : lines) {
    while (!line.isEmpty() && isJsWhitespace(line.front())) line.remove(0, 1);
    while (!line.isEmpty() && isJsWhitespace(line.back())) line.chop(1);
    static const QRegularExpression repeated(QStringLiteral(R"([\t ]{2,})"));
    line.replace(repeated, QStringLiteral(" "));
  }
  QString result = lines.join(QLatin1Char('\n'));
  static const QRegularExpression blankRuns(QStringLiteral(R"([\n\r]{2,})"));
  result.replace(blankRuns, QStringLiteral("\n"));
  return result;
}

QString sanitize(QString value) {
  // DOMPurify leaves plain grammar text byte-for-byte intact. Lexbor's HTML
  // serializer necessarily entity-escapes a bare '&', which would turn PEG's
  // lookahead projection from &"x" into &amp;"x". Only invoke the fragment
  // serializer when markup is actually present.
  if (!value.contains(QLatin1Char('<'))) return value;
  return HtmlSanitizer().sanitizedMermaidText(value);
}

RailroadNode sanitizedNode(RailroadNode node) {
  node.text = sanitize(node.text);
  for (RailroadNode& child : node.children) child = sanitizedNode(std::move(child));
  for (RailroadNode& child : node.separator)
    child = sanitizedNode(std::move(child));
  return node;
}

enum class HiddenMode { Direct, Ebnf, Abnf, Peg };

class Cursor {
public:
  Cursor(QString source, HiddenMode mode)
      : source_(std::move(source)), mode_(mode) {}

  bool eof() {
    skipHidden();
    return position_ >= source_.size();
  }

  int line() const { return line_; }
  int column() const { return column_; }
  qsizetype position() const { return position_; }
  const QString& source() const { return source_; }

  bool startsRaw(QStringView value) const {
    return QStringView(source_).mid(position_, value.size()) == value;
  }

  QChar peek() {
    skipHidden();
    return position_ < source_.size() ? source_.at(position_) : QChar();
  }

  bool at(QStringView value) {
    skipHidden();
    return startsRaw(value);
  }

  bool consume(QStringView value) {
    skipHidden();
    if (!startsRaw(value)) return false;
    advance(value.size());
    return true;
  }

  void expect(QStringView value, const QString& message = {}) {
    skipHidden();
    if (startsRaw(value)) {
      advance(value.size());
      return;
    }
    parserError(message.isEmpty()
                    ? QStringLiteral("Expected '%1'").arg(value.toString())
                    : message);
  }

  void expectHeader(QStringView header) {
    skipHidden();
    const bool exact = startsRaw(header);
    const qsizetype end = position_ + header.size();
    const bool boundary = end >= source_.size() ||
                          isAsciiSpace(source_.at(end)) ||
                          QStringView(source_).mid(end, 2) == QStringView(u"%%");
    if (!exact || !boundary)
      parserError(QStringLiteral("Expected diagram header '%1'")
                      .arg(header.toString()));
    advance(header.size());
  }

  QString readIdentifier(bool abnf = false) {
    skipHidden();
    const qsizetype start = position_;
    if (position_ >= source_.size() ||
        !(abnf ? ((source_.at(position_) >= QLatin1Char('A') &&
                   source_.at(position_) <= QLatin1Char('Z')) ||
                  (source_.at(position_) >= QLatin1Char('a') &&
                   source_.at(position_) <= QLatin1Char('z')))
               : isIdStart(source_.at(position_))))
      parserError(QStringLiteral("Expected rule identifier"));
    advance(1);
    while (position_ < source_.size()) {
      const QChar ch = source_.at(position_);
      const bool accepted = abnf
          ? (((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
              (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
              (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) ||
             ch == QLatin1Char('-'))
                                 : isIdContinue(ch);
      if (!accepted) break;
      advance(1);
    }
    return source_.mid(start, position_ - start);
  }

  QString readString(bool decode) {
    skipHidden();
    if (position_ >= source_.size() ||
        (source_.at(position_) != QLatin1Char('"') &&
         source_.at(position_) != QLatin1Char('\'')))
      parserError(QStringLiteral("Expected quoted string"));
    const QChar quote = source_.at(position_);
    const qsizetype start = position_;
    advance(1);
    bool escaped = false;
    while (position_ < source_.size()) {
      const QChar ch = source_.at(position_);
      if (!escaped && ch == quote) {
        advance(1);
        const QString raw = source_.mid(start, position_ - start);
        return decode ? decodeEscapedString(raw) : raw.mid(1, raw.size() - 2);
      }
      if (decode && !escaped && ch == QLatin1Char('\\')) {
        escaped = true;
        advance(1);
        continue;
      }
      escaped = false;
      advance(1);
    }
    fail(RailroadErrorKind::Lexer, line_, column_, source_.mid(start),
         QStringLiteral("Unterminated string literal"));
  }

  QString readSpecialSequence() {
    skipHidden();
    if (!isSpecialStart()) parserError(QStringLiteral("Expected special sequence"));
    advance(1);
    const qsizetype start = position_;
    while (position_ < source_.size() && source_.at(position_) != QLatin1Char('?'))
      advance(1);
    const QString value = source_.mid(start, position_ - start).trimmed();
    expect(QStringView(u"?"));
    return value;
  }

  bool isSpecialStart() {
    skipHidden();
    if (position_ >= source_.size() || source_.at(position_) != QLatin1Char('?'))
      return false;
    bool nonSpace = false;
    for (qsizetype i = position_ + 1; i < source_.size(); ++i) {
      const QChar ch = source_.at(i);
      if (ch == QLatin1Char(';')) return false;
      if (ch == QLatin1Char('?')) return nonSpace;
      if (!isJsWhitespace(ch)) nonSpace = true;
    }
    return false;
  }

  QString readNumVal() {
    skipHidden();
    const qsizetype start = position_;
    if (position_ >= source_.size() || source_.at(position_) != QLatin1Char('%'))
      parserError(QStringLiteral("Expected ABNF numeric value"));
    advance(1);
    if (position_ >= source_.size() ||
        !QStringLiteral("xXdDbB").contains(source_.at(position_)))
      lexerError(QStringLiteral("Invalid ABNF numeric value"));
    advance(1);
    const qsizetype digits = position_;
    while (position_ < source_.size() &&
           ((source_.at(position_) >= QLatin1Char('0') &&
             source_.at(position_) <= QLatin1Char('9')) ||
            (source_.at(position_) >= QLatin1Char('a') &&
             source_.at(position_) <= QLatin1Char('f')) ||
            (source_.at(position_) >= QLatin1Char('A') &&
             source_.at(position_) <= QLatin1Char('F'))))
      advance(1);
    if (position_ == digits) lexerError(QStringLiteral("Invalid ABNF numeric value"));
    if (position_ < source_.size() &&
        (source_.at(position_) == QLatin1Char('-') ||
         source_.at(position_) == QLatin1Char('.'))) {
      advance(1);
      const qsizetype more = position_;
      while (position_ < source_.size() &&
             ((source_.at(position_) >= QLatin1Char('0') &&
               source_.at(position_) <= QLatin1Char('9')) ||
              (source_.at(position_) >= QLatin1Char('a') &&
               source_.at(position_) <= QLatin1Char('f')) ||
              (source_.at(position_) >= QLatin1Char('A') &&
               source_.at(position_) <= QLatin1Char('F'))))
        advance(1);
      if (position_ == more) lexerError(QStringLiteral("Invalid ABNF numeric value"));
    }
    return source_.mid(start, position_ - start);
  }

  QString readRepeat() {
    skipHidden();
    const qsizetype start = position_;
    while (position_ < source_.size() && source_.at(position_).isDigit())
      advance(1);
    if (position_ < source_.size() && source_.at(position_) == QLatin1Char('*')) {
      advance(1);
      while (position_ < source_.size() && source_.at(position_).isDigit())
        advance(1);
    }
    return source_.mid(start, position_ - start);
  }

  bool metadata(RailroadDialect dialect, RailroadData& data) {
    skipHidden();
    if (startsRaw(QStringView(u"accTitle"))) {
      qsizetype p = position_ + 8;
      while (p < source_.size() &&
             (source_.at(p) == QLatin1Char(' ') || source_.at(p) == QLatin1Char('\t')))
        ++p;
      if (p < source_.size() && source_.at(p) == QLatin1Char(':')) {
        advance(p - position_ + 1);
        data.accTitle = sanitize(collapseInline(readLineBeforeComment()));
        while (!data.accTitle.isEmpty() && isJsWhitespace(data.accTitle.front()))
          data.accTitle.remove(0, 1);
        return true;
      }
    }
    if (startsRaw(QStringView(u"accDescr"))) {
      qsizetype p = position_ + 8;
      while (p < source_.size() &&
             (source_.at(p) == QLatin1Char(' ') || source_.at(p) == QLatin1Char('\t')))
        ++p;
      if (p < source_.size() && source_.at(p) == QLatin1Char(':')) {
        advance(p - position_ + 1);
        data.accDescr = sanitize(collapseInline(readLineBeforeComment()));
        data.accDescr.replace(QRegularExpression(QStringLiteral("\\n\\s+")),
                              QStringLiteral("\n"));
        return true;
      }
      p = position_ + 8;
      while (p < source_.size() && isJsWhitespace(source_.at(p))) ++p;
      if (p < source_.size() && source_.at(p) == QLatin1Char('{')) {
        advance(p - position_ + 1);
        const qsizetype start = position_;
        while (position_ < source_.size() && source_.at(position_) != QLatin1Char('}'))
          advance(1);
        if (position_ >= source_.size())
          lexerError(QStringLiteral("Unterminated accessibility description"));
        const QString value = source_.mid(start, position_ - start);
        advance(1);
        data.accDescr = sanitize(normalizeDescription(value));
        data.accDescr.replace(QRegularExpression(QStringLiteral("\\n\\s+")),
                              QStringLiteral("\n"));
        return true;
      }
    }
    if (startsRaw(QStringView(u"title"))) {
      advance(5);
      QString value;
      if (position_ < source_.size() &&
          (source_.at(position_) == QLatin1Char(' ') ||
           source_.at(position_) == QLatin1Char('\t')))
        value = collapseInline(readLineBeforeComment());
      if (value.size() >= 2 &&
          ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"')) ||
           (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\'')))) {
        value = dialect == RailroadDialect::Abnf
                    ? value.mid(1, value.size() - 2)
                    : decodeEscapedString(value);
      }
      data.title = sanitize(value);
      return true;
    }
    return false;
  }

  [[noreturn]] void parserError(const QString& message) {
    skipHidden();
    fail(RailroadErrorKind::Parser, line_, column_, currentToken(), message);
  }

  [[noreturn]] void lexerError(const QString& message) {
    fail(RailroadErrorKind::Lexer, line_, column_, currentToken(), message);
  }

private:
  QString readLineBeforeComment() {
    const qsizetype start = position_;
    qsizetype end = position_;
    while (end < source_.size() && source_.at(end) != QLatin1Char('\n') &&
           source_.at(end) != QLatin1Char('\r')) {
      if (end + 1 < source_.size() && source_.at(end) == QLatin1Char('%') &&
          source_.at(end + 1) == QLatin1Char('%'))
        break;
      ++end;
    }
    advance(end - position_);
    return source_.mid(start, end - start);
  }

  QString currentToken() const {
    if (position_ >= source_.size()) return {};
    qsizetype end = position_;
    while (end < source_.size() && !isAsciiSpace(source_.at(end)) &&
           !QStringLiteral("=;(),|[]{}?*+/&!<>").contains(source_.at(end)))
      ++end;
    if (end == position_) ++end;
    return source_.mid(position_, end - position_);
  }

  void skipHidden() {
    bool changed = true;
    while (changed) {
      changed = false;
      while (position_ < source_.size() && isAsciiSpace(source_.at(position_))) {
        advance(1);
        changed = true;
      }
      if (position_ >= source_.size()) return;

      if (startsRaw(QStringView(u"---"))) {
        const qsizetype close = source_.indexOf(QStringLiteral("\n---"), position_ + 3);
        if (close >= 0) {
          qsizetype end = close + 4;
          if (end < source_.size() && source_.at(end) == QLatin1Char('\r')) ++end;
          if (end < source_.size() && source_.at(end) == QLatin1Char('\n')) ++end;
          advance(end - position_);
          changed = true;
          continue;
        }
      }
      if (startsRaw(QStringView(u"%%{"))) {
        const qsizetype close = source_.indexOf(QStringLiteral("}%%"), position_ + 3);
        if (close >= 0) {
          advance(close + 3 - position_);
          changed = true;
          continue;
        }
      }
      if (startsRaw(QStringView(u"%%"))) {
        qsizetype end = position_;
        while (end < source_.size() && source_.at(end) != QLatin1Char('\n') &&
               source_.at(end) != QLatin1Char('\r'))
          ++end;
        advance(end - position_);
        changed = true;
        continue;
      }
      if ((mode_ == HiddenMode::Direct || mode_ == HiddenMode::Ebnf) &&
          startsRaw(QStringView(u"/*"))) {
        const qsizetype close = source_.indexOf(QStringLiteral("*/"), position_ + 2);
        if (close >= 0) {
          advance(close + 2 - position_);
          changed = true;
          continue;
        }
      }
      if (mode_ == HiddenMode::Ebnf && startsRaw(QStringView(u"(*"))) {
        const qsizetype close = source_.indexOf(QStringLiteral("*)"), position_ + 2);
        if (close >= 0) {
          advance(close + 2 - position_);
          changed = true;
          continue;
        }
      }
      if (mode_ == HiddenMode::Peg && source_.at(position_) == QLatin1Char('#')) {
        qsizetype end = position_;
        while (end < source_.size() && source_.at(end) != QLatin1Char('\n') &&
               source_.at(end) != QLatin1Char('\r'))
          ++end;
        advance(end - position_);
        changed = true;
        continue;
      }
      if (mode_ == HiddenMode::Abnf && source_.at(position_) == QLatin1Char(';')) {
        qsizetype end = position_ + 1;
        while (end < source_.size() && source_.at(end) != QLatin1Char('\n') &&
               source_.at(end) != QLatin1Char('\r'))
          ++end;
        // Chevrotain's keyword token wins for a one-character terminator. Its
        // longer_alt comment wins whenever the same ';' starts a longer token.
        if (end > position_ + 1) {
          advance(end - position_);
          changed = true;
          continue;
        }
      }
    }
  }

  void advance(qsizetype count) {
    const qsizetype end = std::min(position_ + count, source_.size());
    while (position_ < end) {
      const QChar ch = source_.at(position_++);
      if (ch == QLatin1Char('\r')) {
        if (position_ < end && source_.at(position_) == QLatin1Char('\n'))
          ++position_;
        ++line_;
        column_ = 1;
      } else if (ch == QLatin1Char('\n')) {
        ++line_;
        column_ = 1;
      } else {
        ++column_;
      }
    }
  }

  QString source_;
  HiddenMode mode_;
  qsizetype position_ = 0;
  int line_ = 1;
  int column_ = 1;
};

RailroadNode leaf(RailroadNodeType type, QString text) {
  RailroadNode node;
  node.type = type;
  node.text = std::move(text);
  return node;
}

RailroadNode compound(RailroadNodeType type, QVector<RailroadNode> children) {
  if ((type == RailroadNodeType::Sequence || type == RailroadNodeType::Choice) &&
      children.size() == 1)
    return std::move(children.front());
  RailroadNode node;
  node.type = type;
  node.children = std::move(children);
  return node;
}

RailroadNode unary(RailroadNodeType type, RailroadNode child, qreal min = 0.0,
                   qreal max = std::numeric_limits<qreal>::infinity()) {
  RailroadNode node;
  node.type = type;
  node.children.append(std::move(child));
  node.min = min;
  node.max = max;
  return node;
}

class Parser {
public:
  Parser(QString source, RailroadDialect dialect)
      : dialect_(dialect), cursor_(std::move(source), hiddenMode(dialect)) {
    data_.dialect = dialect;
  }

  RailroadData parse() {
    switch (dialect_) {
      case RailroadDialect::Direct:
        cursor_.expectHeader(QStringView(u"railroad-beta"));
        break;
      case RailroadDialect::Ebnf:
        cursor_.expectHeader(QStringView(u"railroad-ebnf-beta"));
        break;
      case RailroadDialect::Abnf:
        cursor_.expectHeader(QStringView(u"railroad-abnf-beta"));
        break;
      case RailroadDialect::Peg:
        cursor_.expectHeader(QStringView(u"railroad-peg-beta"));
        break;
    }

    while (cursor_.metadata(dialect_, data_)) {}
    while (!cursor_.eof()) data_.rules.append(parseRule());

    for (RailroadRule& rule : data_.rules) {
      rule.name = sanitize(rule.name);
      rule.definition = sanitizedNode(std::move(rule.definition));
      if (!rule.comment.isEmpty()) rule.comment = sanitize(rule.comment);
    }
    return std::move(data_);
  }

private:
  static HiddenMode hiddenMode(RailroadDialect dialect) {
    switch (dialect) {
      case RailroadDialect::Direct: return HiddenMode::Direct;
      case RailroadDialect::Ebnf: return HiddenMode::Ebnf;
      case RailroadDialect::Abnf: return HiddenMode::Abnf;
      case RailroadDialect::Peg: return HiddenMode::Peg;
    }
    return HiddenMode::Direct;
  }

  RailroadRule parseRule() {
    RailroadRule rule;
    rule.name = cursor_.readIdentifier(dialect_ == RailroadDialect::Abnf);
    switch (dialect_) {
      case RailroadDialect::Direct:
        cursor_.expect(QStringView(u"="));
        rule.definition = parseDirectExpression();
        break;
      case RailroadDialect::Ebnf:
        if (!cursor_.consume(QStringView(u"::="))) cursor_.expect(QStringView(u"="));
        rule.definition = parseEbnfChoice();
        break;
      case RailroadDialect::Abnf:
        cursor_.expect(QStringView(u"="));
        rule.definition = parseAbnfAlternation();
        break;
      case RailroadDialect::Peg:
        cursor_.expect(QStringView(u"<-"));
        rule.definition = parsePegChoice();
        break;
    }
    cursor_.expect(QStringView(u";"));
    return rule;
  }

  RailroadNode parseDirectExpression() {
    const QString name = cursor_.readIdentifier();
    cursor_.expect(QStringView(u"("));
    if (name == QLatin1String("terminal") ||
        name == QLatin1String("nonterminal") ||
        name == QLatin1String("special")) {
      const QString value = cursor_.readString(true);
      cursor_.expect(QStringView(u")"));
      return leaf(name == QLatin1String("terminal")
                      ? RailroadNodeType::Terminal
                      : name == QLatin1String("nonterminal")
                            ? RailroadNodeType::NonTerminal
                            : RailroadNodeType::Special,
                  value);
    }
    if (name == QLatin1String("optional") ||
        name == QLatin1String("oneOrMore") ||
        name == QLatin1String("zeroOrMore")) {
      RailroadNode child = parseDirectExpression();
      cursor_.expect(QStringView(u")"));
      if (name == QLatin1String("optional"))
        return unary(RailroadNodeType::Optional, std::move(child));
      return unary(RailroadNodeType::Repetition, std::move(child),
                   name == QLatin1String("oneOrMore") ? 1.0 : 0.0);
    }
    if (name == QLatin1String("sequence") || name == QLatin1String("choice")) {
      QVector<RailroadNode> children;
      children.append(parseDirectExpression());
      while (cursor_.consume(QStringView(u",")))
        children.append(parseDirectExpression());
      cursor_.expect(QStringView(u")"));
      return compound(name == QLatin1String("sequence")
                          ? RailroadNodeType::Sequence
                          : RailroadNodeType::Choice,
                      std::move(children));
    }
    cursor_.parserError(QStringLiteral("Unsupported railroad expression '%1'").arg(name));
  }

  bool ebnfPrimaryStart() {
    const QChar ch = cursor_.peek();
    return ch == QLatin1Char('"') || ch == QLatin1Char('\'') ||
           ch == QLatin1Char('(') || ch == QLatin1Char('[') ||
           ch == QLatin1Char('{') || isIdStart(ch) ||
           (ch == QLatin1Char('?') && cursor_.isSpecialStart());
  }

  RailroadNode parseEbnfChoice() {
    QVector<RailroadNode> alternatives;
    alternatives.append(parseEbnfSequence());
    while (cursor_.consume(QStringView(u"|")))
      alternatives.append(parseEbnfSequence());
    return compound(RailroadNodeType::Choice, std::move(alternatives));
  }

  RailroadNode parseEbnfSequence() {
    QVector<RailroadNode> elements;
    if (!ebnfPrimaryStart()) cursor_.parserError(QStringLiteral("Expected EBNF term"));
    elements.append(parseEbnfTerm());
    while (true) {
      if (cursor_.consume(QStringView(u","))) {
        if (!ebnfPrimaryStart())
          cursor_.parserError(QStringLiteral("Expected EBNF term after comma"));
        elements.append(parseEbnfTerm());
      } else if (ebnfPrimaryStart()) {
        elements.append(parseEbnfTerm());
      } else {
        break;
      }
    }
    return compound(RailroadNodeType::Sequence, std::move(elements));
  }

  RailroadNode parseEbnfTerm() {
    RailroadNode node = parseEbnfPrimary();
    while (true) {
      if (cursor_.consume(QStringView(u"?")))
        node = unary(RailroadNodeType::Optional, std::move(node));
      else if (cursor_.consume(QStringView(u"*")))
        node = unary(RailroadNodeType::Repetition, std::move(node), 0.0);
      else if (cursor_.consume(QStringView(u"+")))
        node = unary(RailroadNodeType::Repetition, std::move(node), 1.0);
      else if (cursor_.consume(QStringView(u"-"))) {
        QVector<RailroadNode> sequence;
        sequence.append(std::move(node));
        sequence.append(leaf(RailroadNodeType::Terminal, QStringLiteral("-")));
        sequence.append(parseEbnfPrimary());
        node = compound(RailroadNodeType::Sequence, std::move(sequence));
      } else {
        break;
      }
    }
    return node;
  }

  RailroadNode parseEbnfPrimary() {
    const QChar ch = cursor_.peek();
    if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
      return leaf(RailroadNodeType::Terminal, cursor_.readString(true));
    if (ch == QLatin1Char('?') && cursor_.isSpecialStart())
      return leaf(RailroadNodeType::Special, cursor_.readSpecialSequence());
    if (isIdStart(ch))
      return leaf(RailroadNodeType::NonTerminal, cursor_.readIdentifier());
    if (cursor_.consume(QStringView(u"("))) {
      RailroadNode value = parseEbnfChoice();
      cursor_.expect(QStringView(u")"));
      return value;
    }
    if (cursor_.consume(QStringView(u"["))) {
      RailroadNode value = parseEbnfChoice();
      cursor_.expect(QStringView(u"]"));
      return unary(RailroadNodeType::Optional, std::move(value));
    }
    if (cursor_.consume(QStringView(u"{"))) {
      RailroadNode value = parseEbnfChoice();
      cursor_.expect(QStringView(u"}"));
      return unary(RailroadNodeType::Repetition, std::move(value), 0.0);
    }
    cursor_.parserError(QStringLiteral("Expected EBNF primary expression"));
  }

  bool abnfPrimaryStart() {
    const QChar ch = cursor_.peek();
    return ch == QLatin1Char('"') || ch == QLatin1Char('%') ||
           ch == QLatin1Char('(') || ch == QLatin1Char('[') || ch.isLetter();
  }

  RailroadNode parseAbnfAlternation() {
    QVector<RailroadNode> alternatives;
    alternatives.append(parseAbnfConcatenation());
    while (cursor_.consume(QStringView(u"/")))
      alternatives.append(parseAbnfConcatenation());
    return compound(RailroadNodeType::Choice, std::move(alternatives));
  }

  RailroadNode parseAbnfConcatenation() {
    QVector<RailroadNode> elements;
    if (!abnfElementStart()) cursor_.parserError(QStringLiteral("Expected ABNF element"));
    while (abnfElementStart()) elements.append(parseAbnfElement());
    return compound(RailroadNodeType::Sequence, std::move(elements));
  }

  bool abnfElementStart() {
    const QChar ch = cursor_.peek();
    return abnfPrimaryStart() || ch.isDigit() || ch == QLatin1Char('*');
  }

  RailroadNode parseAbnfElement() {
    QString repeat;
    const QChar ch = cursor_.peek();
    if (ch.isDigit() || ch == QLatin1Char('*')) repeat = cursor_.readRepeat();
    RailroadNode inner = parseAbnfPrimary();
    if (repeat.isEmpty()) return inner;
    int min = 0;
    qreal max = std::numeric_limits<qreal>::infinity();
    if (repeat.contains(QLatin1Char('*'))) {
      const QStringList parts = repeat.split(QLatin1Char('*'));
      if (!parts.value(0).isEmpty()) min = parts.value(0).toInt();
      if (!parts.value(1).isEmpty()) max = parts.value(1).toInt();
    } else {
      min = repeat.toInt();
      max = min;
    }
    if (min == 0 && max == 1.0)
      return unary(RailroadNodeType::Optional, std::move(inner));
    return unary(RailroadNodeType::Repetition, std::move(inner), min, max);
  }

  RailroadNode parseAbnfPrimary() {
    const QChar ch = cursor_.peek();
    if (ch == QLatin1Char('"'))
      return leaf(RailroadNodeType::Terminal, cursor_.readString(false));
    if (ch == QLatin1Char('%'))
      return leaf(RailroadNodeType::Terminal, cursor_.readNumVal());
    if (ch.isLetter())
      return leaf(RailroadNodeType::NonTerminal, cursor_.readIdentifier(true));
    if (cursor_.consume(QStringView(u"("))) {
      RailroadNode value = parseAbnfAlternation();
      cursor_.expect(QStringView(u")"));
      return value;
    }
    if (cursor_.consume(QStringView(u"["))) {
      RailroadNode value = parseAbnfAlternation();
      cursor_.expect(QStringView(u"]"));
      return unary(RailroadNodeType::Optional, std::move(value));
    }
    cursor_.parserError(QStringLiteral("Expected ABNF primary expression"));
  }

  bool pegPrimaryStart() {
    const QChar ch = cursor_.peek();
    return ch == QLatin1Char('"') || ch == QLatin1Char('\'') ||
           ch == QLatin1Char('(') || ch == QLatin1Char('.') || isIdStart(ch);
  }

  bool pegPrefixStart() {
    const QChar ch = cursor_.peek();
    return ch == QLatin1Char('&') || ch == QLatin1Char('!') || pegPrimaryStart();
  }

  RailroadNode parsePegChoice() {
    QVector<RailroadNode> alternatives;
    alternatives.append(parsePegSequence());
    while (cursor_.consume(QStringView(u"/")))
      alternatives.append(parsePegSequence());
    return compound(RailroadNodeType::Choice, std::move(alternatives));
  }

  RailroadNode parsePegSequence() {
    QVector<RailroadNode> elements;
    if (!pegPrefixStart()) cursor_.parserError(QStringLiteral("Expected PEG expression"));
    while (pegPrefixStart()) elements.append(parsePegPrefix());
    return compound(RailroadNodeType::Sequence, std::move(elements));
  }

  QString pegLabel(const RailroadNode& node) const {
    if (node.type == RailroadNodeType::Terminal)
      return QStringLiteral("\"") + node.text + QStringLiteral("\"");
    if (node.type == RailroadNodeType::NonTerminal ||
        node.type == RailroadNodeType::Special)
      return node.text;
    return QStringLiteral("(...)");
  }

  RailroadNode parsePegPrefix() {
    QChar op;
    if (cursor_.consume(QStringView(u"&"))) op = QLatin1Char('&');
    else if (cursor_.consume(QStringView(u"!"))) op = QLatin1Char('!');
    RailroadNode node = parsePegSuffix();
    if (!op.isNull())
      return leaf(RailroadNodeType::Special, QString(op) + pegLabel(node));
    return node;
  }

  RailroadNode parsePegSuffix() {
    RailroadNode node = parsePegPrimary();
    if (cursor_.consume(QStringView(u"?")))
      return unary(RailroadNodeType::Optional, std::move(node));
    if (cursor_.consume(QStringView(u"*")))
      return unary(RailroadNodeType::Repetition, std::move(node), 0.0);
    if (cursor_.consume(QStringView(u"+")))
      return unary(RailroadNodeType::Repetition, std::move(node), 1.0);
    return node;
  }

  RailroadNode parsePegPrimary() {
    const QChar ch = cursor_.peek();
    if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
      return leaf(RailroadNodeType::Terminal, cursor_.readString(true));
    if (isIdStart(ch))
      return leaf(RailroadNodeType::NonTerminal, cursor_.readIdentifier());
    if (cursor_.consume(QStringView(u".")))
      return leaf(RailroadNodeType::Special, QStringLiteral("."));
    if (cursor_.consume(QStringView(u"("))) {
      RailroadNode value = parsePegChoice();
      cursor_.expect(QStringView(u")"));
      return value;
    }
    cursor_.parserError(QStringLiteral("Expected PEG primary expression"));
  }

  RailroadDialect dialect_;
  Cursor cursor_;
  RailroadData data_;
};

}  // namespace

RailroadParseError::RailroadParseError(RailroadErrorKind kindValue,
                                       int lineValue, int columnValue,
                                       QString tokenValue,
                                       const QString& message)
    : std::runtime_error(message.toUtf8().constData()),
      kind(kindValue),
      line(lineValue),
      column(columnValue),
      token(std::move(tokenValue)) {}

RailroadData RailroadDiagram::parse(const QString& source,
                                    RailroadDialect dialect) {
  return Parser(source, dialect).parse();
}

}  // namespace muffin::mermaid::railroad
