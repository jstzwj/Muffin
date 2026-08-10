#include "mermaid/eventmodeling/EventModelingDiagram.h"

#include <QRegularExpression>
#include <QStringList>

#include <utility>

namespace muffin::mermaid::eventmodeling {
namespace {

enum class TokenKind {
  Word,
  Number,
  Arrow,
  DataRefOpen,
  DataRefClose,
  Dot,
  Backtick,
  InlineData,
  BlockData,
  End,
};

struct Token {
  TokenKind kind = TokenKind::End;
  QString text;
  int line = 1;
  int column = 1;
};

bool isWordStart(QChar ch) {
  return ch == QLatin1Char('_') ||
         (ch.unicode() >= 'A' && ch.unicode() <= 'Z') ||
         (ch.unicode() >= 'a' && ch.unicode() <= 'z');
}

bool isWordPart(QChar ch) {
  return isWordStart(ch) ||
         (ch.unicode() >= '0' && ch.unicode() <= '9');
}

class Lexer {
 public:
  explicit Lexer(QString source) : source_(std::move(source)) {}

  Token next() {
    skipHidden();
    if (position_ >= source_.size()) return make(TokenKind::End, QString());

    const int start = position_;
    const int line = line_;
    const int column = column_;
    const auto finish = [&](TokenKind kind) {
      return Token{kind, source_.mid(start, position_ - start), line, column};
    };

    if (source_.mid(position_, 3) == QLatin1String("->>")) {
      advance(3);
      return finish(TokenKind::Arrow);
    }
    if (source_.mid(position_, 2) == QLatin1String("[[")) {
      advance(2);
      return finish(TokenKind::DataRefOpen);
    }
    if (source_.mid(position_, 2) == QLatin1String("]]")) {
      advance(2);
      return finish(TokenKind::DataRefClose);
    }

    const QChar ch = source_.at(position_);
    if (ch == QLatin1Char('.')) {
      advance();
      return finish(TokenKind::Dot);
    }
    if (ch == QLatin1Char('`')) {
      advance();
      return finish(TokenKind::Backtick);
    }
    if (isWordStart(ch)) {
      advance();
      while (position_ < source_.size() && isWordPart(source_.at(position_)))
        advance();
      return finish(TokenKind::Word);
    }
    if (ch.isDigit()) {
      int count = 0;
      while (position_ < source_.size() && source_.at(position_).isDigit() &&
             count < 3) {
        advance();
        ++count;
      }
      return finish(TokenKind::Number);
    }
    if (ch == QLatin1Char('{')) {
      const int lineEnd = source_.indexOf(QRegularExpression("[\\r\\n]"), position_);
      const int end = lineEnd < 0 ? source_.size() : lineEnd;
      const int close = source_.lastIndexOf(QLatin1Char('}'), end - 1);
      if (close >= position_) {
        advance(close - position_ + 1);
        return finish(TokenKind::InlineData);
      }
      static const QRegularExpression block(
          QStringLiteral(R"(\A\{[\t ]*\r?\n(?:[\S\s]*?\r?\n)?\}(?:\r?\n|(?!\S)))"));
      const QRegularExpressionMatch match = block.match(source_.mid(position_));
      if (match.hasMatch()) {
        advance(match.capturedLength());
        return finish(TokenKind::BlockData);
      }
      throwError(EventModelingErrorKind::Lexer,
                 QStringLiteral("unexpected character: {"));
    }
    if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
      const int lineEnd = source_.indexOf(QRegularExpression("[\\r\\n]"), position_);
      const int end = lineEnd < 0 ? source_.size() : lineEnd;
      const int close = source_.lastIndexOf(ch, end - 1);
      if (close > position_) {
        advance(close - position_ + 1);
        return finish(TokenKind::InlineData);
      }
      throwError(EventModelingErrorKind::Lexer,
                 QStringLiteral("unterminated inline data"));
    }

    throwError(EventModelingErrorKind::Lexer,
               QStringLiteral("unexpected character: %1").arg(ch));
  }

 private:
  Token make(TokenKind kind, const QString& text) const {
    return Token{kind, text, line_, column_};
  }

  [[noreturn]] void throwError(EventModelingErrorKind kind,
                               const QString& message) const {
    throw EventModelingParseError(kind, line_, column_, message);
  }

  void advance(int count = 1) {
    for (int i = 0; i < count && position_ < source_.size(); ++i) {
      const QChar ch = source_.at(position_++);
      if (ch == QLatin1Char('\r')) {
        if (position_ < source_.size() && source_.at(position_) == QLatin1Char('\n')) {
          ++position_;
          ++i;
        }
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

  void skipHidden() {
    for (;;) {
      while (position_ < source_.size() && source_.at(position_).isSpace())
        advance();
      if (source_.mid(position_, 2) == QLatin1String("%%") ||
          source_.mid(position_, 2) == QLatin1String("//")) {
        while (position_ < source_.size() &&
               source_.at(position_) != QLatin1Char('\r') &&
               source_.at(position_) != QLatin1Char('\n'))
          advance();
        continue;
      }
      if (source_.mid(position_, 2) == QLatin1String("/*")) {
        const int close = source_.indexOf(QStringLiteral("*/"), position_ + 2);
        if (close < 0)
          throwError(EventModelingErrorKind::Lexer,
                     QStringLiteral("unterminated block comment"));
        advance(close - position_ + 2);
        continue;
      }
      break;
    }
  }

  QString source_;
  int position_ = 0;
  int line_ = 1;
  int column_ = 1;
};

bool isEntityType(const QString& value) {
  static const QStringList types = {
      QStringLiteral("rmo"),       QStringLiteral("readmodel"),
      QStringLiteral("ui"),        QStringLiteral("cmd"),
      QStringLiteral("command"),   QStringLiteral("evt"),
      QStringLiteral("event"),     QStringLiteral("pcr"),
      QStringLiteral("processor"),
  };
  return types.contains(value);
}

bool isDataType(const QString& value) {
  static const QStringList types = {
      QStringLiteral("json"), QStringLiteral("jsobj"),
      QStringLiteral("figma"), QStringLiteral("salt"),
      QStringLiteral("uri"), QStringLiteral("md"),
      QStringLiteral("html"), QStringLiteral("text"),
  };
  return types.contains(value);
}

class Parser {
 public:
  explicit Parser(const QString& source) {
    Lexer lexer(source);
    do {
      tokens_.append(lexer.next());
    } while (tokens_.back().kind != TokenKind::End);
    current_ = tokens_.front();
  }

  EventModelingData parse() {
    expectWord(QStringLiteral("eventmodeling"));
    while (current_.kind != TokenKind::End) parseStatement();
    return std::move(data_);
  }

 private:
  [[noreturn]] void error(const QString& message,
                          EventModelingErrorKind kind =
                              EventModelingErrorKind::Parser) const {
    throw EventModelingParseError(kind, current_.line, current_.column, message);
  }

  void consume() {
    if (tokenIndex_ + 1 < tokens_.size())
      current_ = tokens_.at(++tokenIndex_);
  }

  QString expect(TokenKind kind, const QString& label) {
    if (current_.kind != kind)
      error(QStringLiteral("Expected %1 but found '%2'").arg(label, current_.text));
    const QString value = current_.text;
    consume();
    return value;
  }

  void expectWord(const QString& value) {
    if (current_.kind != TokenKind::Word || current_.text != value)
      error(QStringLiteral("Expected '%1' but found '%2'").arg(value, current_.text));
    consume();
  }

  QString parseQualifiedName() {
    QString value = expect(TokenKind::Word, QStringLiteral("identifier"));
    while (current_.kind == TokenKind::Dot) {
      consume();
      value += QLatin1Char('.') + expect(TokenKind::Word, QStringLiteral("identifier"));
    }
    return value;
  }

  QString parseOptionalDataType() {
    if (current_.kind != TokenKind::Backtick) return {};
    consume();
    if (current_.kind != TokenKind::Word)
      error(QStringLiteral("Expected data type"));
    const QString value = current_.text;
    if (!isDataType(value))
      error(QStringLiteral("Unknown data type '%1'").arg(value));
    consume();
    expect(TokenKind::Backtick, QStringLiteral("'`'"));
    return value;
  }

  QString parseEntityType() {
    if (current_.kind != TokenKind::Word)
      error(QStringLiteral("Expected entity type"));
    const QString value = current_.text;
    if (!isEntityType(value))
      error(QStringLiteral("Unknown entity type '%1'").arg(value));
    consume();
    return value;
  }

  EventModelingGwtStatement parseGwtStatement() {
    const QString type = parseEntityType();
    return {type, expect(TokenKind::Word, QStringLiteral("entity identifier"))};
  }

  void parseStatement() {
    if (current_.kind != TokenKind::Word)
      error(QStringLiteral("Expected Event Modeling statement"));
    const QString keyword = current_.text;
    const int keywordLine = current_.line;
    const int keywordColumn = current_.column;
    consume();
    if (keyword == QLatin1String("entity")) {
      data_.modelEntities.append(parseQualifiedName());
    } else if (keyword == QLatin1String("tf") ||
               keyword == QLatin1String("timeframe")) {
      parseFrame(false);
    } else if (keyword == QLatin1String("rf") ||
               keyword == QLatin1String("resetframe")) {
      parseFrame(true);
    } else if (keyword == QLatin1String("data")) {
      EventModelingDataEntity entity;
      entity.name = expect(TokenKind::Word, QStringLiteral("data name"));
      entity.dataType = parseOptionalDataType();
      entity.dataBlockValue =
          expect(TokenKind::BlockData, QStringLiteral("multiline data block"));
      data_.dataEntities.append(std::move(entity));
    } else if (keyword == QLatin1String("note")) {
      EventModelingNote note;
      note.sourceFrame = expect(TokenKind::Number, QStringLiteral("frame identifier"));
      note.dataType = parseOptionalDataType();
      note.dataBlockValue =
          expect(TokenKind::BlockData, QStringLiteral("multiline data block"));
      data_.notes.append(std::move(note));
    } else if (keyword == QLatin1String("gwt")) {
      parseGwt();
    } else {
      throw EventModelingParseError(
          EventModelingErrorKind::Parser, keywordLine, keywordColumn,
          QStringLiteral("Unknown Event Modeling statement '%1'").arg(keyword));
    }
  }

  void parseFrame(bool reset) {
    EventModelingFrame frame;
    frame.reset = reset;
    frame.name = expect(TokenKind::Number, QStringLiteral("frame identifier"));
    frame.modelEntityType = parseEntityType();
    frame.entityIdentifier = parseQualifiedName();
    while (current_.kind == TokenKind::Arrow) {
      consume();
      frame.sourceFrames.append(
          expect(TokenKind::Number, QStringLiteral("source frame identifier")));
    }
    if (current_.kind == TokenKind::DataRefOpen) {
      consume();
      frame.dataReference = expect(TokenKind::Word, QStringLiteral("data reference"));
      expect(TokenKind::DataRefClose,
             QStringLiteral("']]'"));
    }
    frame.dataType = parseOptionalDataType();
    if (current_.kind == TokenKind::InlineData) {
      frame.dataInlineValue = current_.text;
      consume();
    }
    data_.frames.append(std::move(frame));
  }

  void parseGwt() {
    EventModelingGwt gwt;
    gwt.sourceFrame = expect(TokenKind::Number, QStringLiteral("frame identifier"));
    expectWord(QStringLiteral("given"));
    do {
      gwt.given.append(parseGwtStatement());
    } while (!(current_.kind == TokenKind::Word &&
               (current_.text == QLatin1String("when") ||
                current_.text == QLatin1String("then"))) &&
             current_.kind != TokenKind::End);
    if (current_.kind == TokenKind::Word && current_.text == QLatin1String("when")) {
      consume();
      do {
        gwt.when.append(parseGwtStatement());
      } while (!(current_.kind == TokenKind::Word &&
                 current_.text == QLatin1String("then")) &&
               current_.kind != TokenKind::End);
    }
    expectWord(QStringLiteral("then"));
    do {
      gwt.then.append(parseGwtStatement());
    } while (current_.kind == TokenKind::Word && isEntityType(current_.text));
    data_.gwt.append(std::move(gwt));
  }

  QVector<Token> tokens_;
  qsizetype tokenIndex_ = 0;
  Token current_;
  EventModelingData data_;
};

}  // namespace

EventModelingParseError::EventModelingParseError(
    EventModelingErrorKind kindValue, int lineValue, int columnValue,
    const QString& message)
    : std::runtime_error(message.toUtf8().constData()),
      kind(kindValue),
      line(lineValue),
      column(columnValue) {}

EventModelingData EventModelingDiagram::parse(const QString& source) {
  return Parser(source).parse();
}

}  // namespace muffin::mermaid::eventmodeling
