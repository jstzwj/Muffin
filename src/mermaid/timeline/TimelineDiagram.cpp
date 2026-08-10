#include "mermaid/timeline/TimelineDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QRegularExpression>
#include <QStringView>

#include <optional>
#include <utility>

namespace muffin::mermaid::timeline {

TimelineParseError::TimelineParseError(const QString& message, int line,
                                       int column, TimelineErrorKind kind)
    : std::runtime_error(message.toUtf8().constData()),
      line(line),
      column(column),
      kind(kind) {}

namespace {

enum class TokenKind {
  End,
  Newline,
  Timeline,
  TimelineLr,
  TimelineTd,
  Title,
  AccTitle,
  AccTitleValue,
  AccDescr,
  AccDescrValue,
  AccDescrBlockValue,
  Section,
  Period,
  Event,
  Invalid,
};

struct Token {
  TokenKind kind = TokenKind::End;
  QString text;
  int line = 1;
  int column = 1;
};

bool jsWhitespace(QChar ch) {
  const ushort value = ch.unicode();
  return (value >= 0x0009 && value <= 0x000d) || value == 0x0020 ||
         value == 0x00a0 || value == 0x1680 ||
         (value >= 0x2000 && value <= 0x200a) || value == 0x2028 ||
         value == 0x2029 || value == 0x202f || value == 0x205f ||
         value == 0x3000 || value == 0xfeff;
}

QString jsWhitespacePattern() {
  return QStringLiteral(
      R"([\x{0009}-\x{000d}\x{0020}\x{00a0}\x{1680}\x{2000}-\x{200a}\x{2028}\x{2029}\x{202f}\x{205f}\x{3000}\x{feff}])");
}

QString asciiCaseInsensitiveKeyword(QStringView keyword) {
  QString pattern;
  pattern.reserve(keyword.size() * 4);
  for (const QChar ch : keyword) {
    const ushort value = ch.unicode();
    if ((value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z')) {
      const QChar lower(value >= 'A' && value <= 'Z' ? value + 0x20 : value);
      const QChar upper(value >= 'a' && value <= 'z' ? value - 0x20 : value);
      pattern += QLatin1Char('[');
      pattern += lower;
      pattern += upper;
      pattern += QLatin1Char(']');
    } else {
      pattern += QRegularExpression::escape(QString(ch));
    }
  }
  return pattern;
}

QString jsTrimmed(QString value) {
  qsizetype begin = 0;
  while (begin < value.size() && jsWhitespace(value.at(begin))) ++begin;
  qsizetype end = value.size();
  while (end > begin && jsWhitespace(value.at(end - 1))) --end;
  return value.mid(begin, end - begin);
}

QString jsTrimmedStart(QString value) {
  qsizetype begin = 0;
  while (begin < value.size() && jsWhitespace(value.at(begin))) ++begin;
  return value.mid(begin);
}

QString deindentAccessibilityDescription(QString value) {
  QString result;
  result.reserve(value.size());
  for (qsizetype index = 0; index < value.size();) {
    const QChar ch = value.at(index++);
    result += ch;
    if (ch != QLatin1Char('\n')) continue;
    while (index < value.size() && jsWhitespace(value.at(index))) ++index;
  }
  return result;
}

QString sanitizedTitle(QString value) {
  // DOMPurify preserves text-node whitespace at the fragment edges. Lexbor's
  // document parser discards it around the synthetic body, so carry that
  // whitespace across the structured sanitizer explicitly.
  qsizetype begin = 0;
  while (begin < value.size() && jsWhitespace(value.at(begin))) ++begin;
  qsizetype end = value.size();
  while (end > begin && jsWhitespace(value.at(end - 1))) --end;
  return value.left(begin) +
         HtmlSanitizer().sanitizedMermaidText(value.mid(begin, end - begin)) +
         value.mid(end);
}

QString sanitizedAccTitle(QString value) {
  value = HtmlSanitizer().sanitizedMermaidText(jsTrimmed(std::move(value)));
  return jsTrimmedStart(std::move(value));
}

QString sanitizedAccDescription(QString value) {
  value = HtmlSanitizer().sanitizedMermaidText(jsTrimmed(std::move(value)));
  return deindentAccessibilityDescription(std::move(value));
}

class Lexer final {
public:
  explicit Lexer(QString source) : source_(std::move(source)) {}

  Token next() {
    for (;;) {
      if (state_ == State::AccTitle || state_ == State::AccDescr)
        return takeAccessibilityValue();
      if (state_ == State::AccDescrBlock) {
        if (atEnd())
          return {TokenKind::Invalid, QString(), line_ + 1, 1};
        if (peek() == QLatin1Char('}')) {
          advance(1);
          state_ = State::Initial;
          continue;
        }
        const qsizetype close = source_.indexOf(QLatin1Char('}'), pos_);
        const qsizetype end = close < 0 ? source_.size() : close;
        const int line = line_;
        const int column = column_;
        const QString value = source_.mid(pos_, end - pos_);
        advance(end - pos_);
        if (value.isEmpty()) continue;
        return {TokenKind::AccDescrBlockValue, value, line, column};
      }

      if (atEnd()) return {TokenKind::End, QString(), line_, column_};

      // The order is the generated Jison lexer's rule order. In particular,
      // the second comment rule deliberately makes `A%%comment` disappear,
      // while `AB%%comment` remains a period.
      if (peek() == QLatin1Char('%') && peek(1) != QLatin1Char('{')) {
        skipToNewline();
        continue;
      }
      if (peek() != QLatin1Char('}') && peek(1) == QLatin1Char('%') &&
          peek(2) == QLatin1Char('%')) {
        advance(1);
        skipToNewline();
        continue;
      }
      if (peek() == QLatin1Char('\n')) {
        const int line = line_;
        const int column = column_;
        const qsizetype start = pos_;
        while (peek() == QLatin1Char('\n')) advance(1);
        return {TokenKind::Newline, source_.mid(start, pos_ - start), line,
                column};
      }
      if (jsWhitespace(peek())) {
        do {
          advance(1);
        } while (!atEnd() && jsWhitespace(peek()));
        continue;
      }
      if (peek() == QLatin1Char('#')) {
        skipToNewline();
        continue;
      }

      const QString whitespace = jsWhitespacePattern();
      if (auto token = takeRegex(
              TokenKind::TimelineLr,
              asciiCaseInsensitiveKeyword(u"timeline") +
                  QStringLiteral("[ \\t]+") + asciiCaseInsensitiveKeyword(u"LR") +
                  QStringLiteral("(?![A-Za-z0-9_])")))
        return *token;
      if (auto token = takeRegex(
              TokenKind::TimelineTd,
              asciiCaseInsensitiveKeyword(u"timeline") +
                  QStringLiteral("[ \\t]+") + asciiCaseInsensitiveKeyword(u"TD") +
                  QStringLiteral("(?![A-Za-z0-9_])")))
        return *token;
      if (auto token = takeRegex(
              TokenKind::Timeline,
              asciiCaseInsensitiveKeyword(u"timeline") +
                  QStringLiteral("(?![A-Za-z0-9_])")))
        return *token;
      if (auto token = takeRegex(
              TokenKind::Title, asciiCaseInsensitiveKeyword(u"title") +
                                    whitespace + QStringLiteral("[^\\n]+")))
        return *token;
      if (auto token = takeRegex(
              TokenKind::AccTitle,
              asciiCaseInsensitiveKeyword(u"accTitle") + whitespace +
                  QStringLiteral("*:") + whitespace + QStringLiteral("*"))) {
        state_ = State::AccTitle;
        return *token;
      }
      if (auto token = takeRegex(
              TokenKind::AccDescr,
              asciiCaseInsensitiveKeyword(u"accDescr") + whitespace +
                  QStringLiteral("*:") + whitespace + QStringLiteral("*"))) {
        state_ = State::AccDescr;
        return *token;
      }
      if (auto token = takeRegex(
              TokenKind::AccDescr,
              asciiCaseInsensitiveKeyword(u"accDescr") + whitespace +
                  QStringLiteral("*\\{") + whitespace + QStringLiteral("*"))) {
        state_ = State::AccDescrBlock;
        continue;
      }
      if (auto token = takeRegex(
              TokenKind::Section, asciiCaseInsensitiveKeyword(u"section") +
                                      whitespace + QStringLiteral("[^:\\n]+")))
        return *token;
      if (auto token = takeRegex(
              TokenKind::Event,
              QStringLiteral(":") + whitespace +
                  QStringLiteral("(?:[^:\\n]|:(?!") + whitespace +
                  QStringLiteral("))+")))
        return *token;
      if (auto token = takeRegex(TokenKind::Period,
                                 QStringLiteral(R"([^#:\n]+)")))
        return *token;

      const Token invalid{TokenKind::Invalid, QString(peek()), line_, column_};
      advance(1);
      return invalid;
    }
  }

private:
  enum class State { Initial, AccTitle, AccDescr, AccDescrBlock };

  bool atEnd() const { return pos_ >= source_.size(); }

  QChar peek(qsizetype offset = 0) const {
    const qsizetype at = pos_ + offset;
    return at >= 0 && at < source_.size() ? source_.at(at) : QChar();
  }

  void advance(qsizetype count) {
    for (qsizetype i = 0; i < count && pos_ < source_.size(); ++i) {
      const QChar ch = source_.at(pos_);
      if (ch == QLatin1Char('\r')) {
        ++line_;
        column_ = 1;
      } else if (ch == QLatin1Char('\n')) {
        if (pos_ == 0 || source_.at(pos_ - 1) != QLatin1Char('\r')) ++line_;
        column_ = 1;
      } else {
        ++column_;
      }
      ++pos_;
    }
  }

  void skipToNewline() {
    while (!atEnd() && peek() != QLatin1Char('\n')) advance(1);
  }

  std::optional<Token> takeRegex(TokenKind kind, const QString& pattern) {
    const QRegularExpression expression(
        QStringLiteral(R"(\A(?:%1))").arg(pattern));
    const QRegularExpressionMatch match = expression.match(source_.mid(pos_));
    if (!match.hasMatch() || match.capturedLength() <= 0) return std::nullopt;
    const Token token{kind, match.captured(), line_, column_};
    advance(match.capturedLength());
    return token;
  }

  Token takeAccessibilityValue() {
    const TokenKind kind = state_ == State::AccTitle
                               ? TokenKind::AccTitleValue
                               : TokenKind::AccDescrValue;
    if (atEnd()) return {TokenKind::End, QString(), line_ + 1, 1};
    const int line = line_;
    const int column = column_;
    const qsizetype newline = source_.indexOf(QLatin1Char('\n'), pos_);
    const qsizetype end = newline < 0 ? source_.size() : newline;
    const QString value = source_.mid(pos_, end - pos_);
    advance(end - pos_);
    state_ = State::Initial;
    return {kind, value, line, column};
  }

  QString source_;
  qsizetype pos_ = 0;
  int line_ = 1;
  int column_ = 1;
  State state_ = State::Initial;
};

[[noreturn]] void parserError(const Token& token, const QString& message) {
  throw TimelineParseError(message, token.line, token.column);
}

}  // namespace

TimelineData TimelineDiagram::parse(const QString& source) {
  TimelineData data;
  Lexer lexer(source);
  Token token = lexer.next();
  if (token.kind == TokenKind::TimelineTd) {
    data.direction = TimelineDirection::TopDown;
  } else if (token.kind == TokenKind::TimelineLr ||
             token.kind == TokenKind::Timeline) {
    data.direction = TimelineDirection::LeftToRight;
  } else {
    parserError(token, QStringLiteral("missing timeline header"));
  }

  Token lastShifted = token;
  QString currentSection;
  for (;;) {
    token = lexer.next();
    switch (token.kind) {
      case TokenKind::End:
        return data;
      case TokenKind::Newline:
        lastShifted = token;
        break;
      case TokenKind::Title:
        data.title = sanitizedTitle(token.text.mid(6));
        lastShifted = token;
        break;
      case TokenKind::AccTitle: {
        const Token value = lexer.next();
        if (value.kind != TokenKind::AccTitleValue)
          parserError(value, QStringLiteral("accTitle requires a value"));
        data.accTitle = sanitizedAccTitle(value.text);
        lastShifted = value;
        break;
      }
      case TokenKind::AccDescr: {
        const Token value = lexer.next();
        if (value.kind != TokenKind::AccDescrValue)
          parserError(value, QStringLiteral("accDescr requires a value"));
        data.accDescr = sanitizedAccDescription(value.text);
        lastShifted = value;
        break;
      }
      case TokenKind::AccDescrBlockValue:
        data.accDescr = sanitizedAccDescription(token.text);
        lastShifted = token;
        break;
      case TokenKind::Section:
        currentSection = token.text.mid(8);
        data.sections.append(currentSection);
        lastShifted = token;
        break;
      case TokenKind::Period: {
        TimelineTask task;
        // Upstream's module-global ID drifts across clear(), but the owning
        // parse snapshot exposes only the stable per-document ordering.
        task.id = data.tasks.size();
        task.section = currentSection;
        task.type = currentSection;
        task.task = token.text;
        data.tasks.append(std::move(task));
        lastShifted = token;
        break;
      }
      case TokenKind::Event:
        if (data.tasks.isEmpty()) {
          throw TimelineParseError(
              QStringLiteral(
                  "Cannot read properties of undefined (reading 'events')"),
              token.line, token.column, TimelineErrorKind::Runtime);
        }
        data.tasks.last().events.append(token.text.mid(2));
        lastShifted = token;
        break;
      case TokenKind::Invalid:
        parserError(Token{token.kind, token.text, token.line,
                          token.text.isEmpty() ? token.column : lastShifted.column},
                    QStringLiteral("unrecognized timeline token"));
      case TokenKind::Timeline:
      case TokenKind::TimelineLr:
      case TokenKind::TimelineTd:
      case TokenKind::AccTitleValue:
      case TokenKind::AccDescrValue:
        parserError(Token{token.kind, token.text, token.line, lastShifted.column},
                    QStringLiteral("unexpected timeline token"));
    }
  }
}

}  // namespace muffin::mermaid::timeline
