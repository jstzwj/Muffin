#include "mermaid/info/InfoDiagram.h"

#include <QRegularExpression>

namespace muffin::mermaid::info {
namespace {

struct Cursor {
  const QString& source;
  qsizetype offset = 0;
  int line = 1;
  int column = 1;

  bool atEnd() const { return offset >= source.size(); }
  QChar current() const { return atEnd() ? QChar() : source.at(offset); }

  void advance() {
    if (atEnd()) return;
    if (source.at(offset) == QLatin1Char('\r')) {
      ++offset;
      if (offset < source.size() && source.at(offset) == QLatin1Char('\n'))
        ++offset;
      ++line;
      column = 1;
      return;
    }
    if (source.at(offset++) == QLatin1Char('\n')) {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  }

  void skipWhitespace() {
    while (!atEnd() && current().isSpace()) advance();
  }
};

bool wordAt(const Cursor& cursor, QLatin1StringView word) {
  if (cursor.source.mid(cursor.offset, word.size()) != word) return false;
  const qsizetype end = cursor.offset + word.size();
  return end >= cursor.source.size() || cursor.source.at(end).isSpace();
}

void consume(Cursor& cursor, qsizetype count) {
  while (count-- > 0) cursor.advance();
}

QString normalizedLangiumLexerMessage(int line, int column, qsizetype offset,
                                      QChar character, qsizetype skipped,
                                      bool includeParsingPrefix = true) {
  return (includeParsingPrefix ? QStringLiteral("Parsing failed: ") : QString()) +
         QStringLiteral(
             "Lexer error on line %1, column %2: "
             "unexpected character: ->%3<- at offset: %4, skipped %5 "
             "characters.")
      .arg(line)
      .arg(column)
      .arg(character)
      .arg(offset)
      .arg(skipped);
}

[[noreturn]] void lexerError(const Cursor& cursor, qsizetype skipped = 1,
                             bool missingHeader = false) {
  QString message = normalizedLangiumLexerMessage(
      cursor.line, cursor.column, cursor.offset, cursor.current(), skipped);
  if (missingHeader) {
    message += QStringLiteral(
        " Parse error on line ?, column ?: Expecting token of type 'info' "
        "but found ``.");
  }
  throw InfoParseError(message, cursor.line, cursor.column,
                       InfoErrorKind::Lexer);
}

[[noreturn]] void lexerErrorsToEnd(Cursor cursor) {
  QString message;
  int firstLine = cursor.line;
  int firstColumn = cursor.column;
  bool foundError = false;
  while (!cursor.atEnd()) {
    if (cursor.current().isSpace()) {
      cursor.advance();
      continue;
    }
    if (!foundError) {
      firstLine = cursor.line;
      firstColumn = cursor.column;
      foundError = true;
    }
    const Cursor errorStart = cursor;
    qsizetype skipped = 0;
    do {
      cursor.advance();
      ++skipped;
    } while (!cursor.atEnd() && !cursor.current().isSpace());
    if (!message.isEmpty()) message += QLatin1Char(' ');
    message += normalizedLangiumLexerMessage(
        errorStart.line, errorStart.column, errorStart.offset,
        errorStart.current(), skipped, message.isEmpty());
  }
  throw InfoParseError(message, firstLine, firstColumn, InfoErrorKind::Lexer);
}

[[noreturn]] void parserError(const Cursor& cursor, const QString& found) {
  throw InfoParseError(
      QStringLiteral("Parsing failed: Parse error on line %1, column %2: "
                     "Expecting end of file but found `%3`.")
          .arg(cursor.line)
          .arg(cursor.column)
          .arg(found),
      cursor.line, cursor.column, InfoErrorKind::Parser);
}

QString lineRemainder(Cursor& cursor) {
  const qsizetype begin = cursor.offset;
  while (!cursor.atEnd() && cursor.current() != QLatin1Char('\n') &&
         cursor.current() != QLatin1Char('\r'))
    cursor.advance();
  return cursor.source.mid(begin, cursor.offset - begin);
}

QString deindentAccDescr(QString value) {
  value = value.trimmed();
  static const QRegularExpression indentation(QStringLiteral(R"(\r?\n\s+)"));
  value.replace(indentation, QStringLiteral("\n"));
  return value;
}

}  // namespace

InfoParseError::InfoParseError(const QString& message, int errorLine,
                               int errorColumn, InfoErrorKind errorKind)
    : std::runtime_error(message.toUtf8().constData()),
      line(errorLine),
      column(errorColumn),
      kind(errorKind) {}

InfoData InfoDiagram::parse(const QString& source) {
  Cursor cursor{source};
  cursor.skipWhitespace();
  if (!wordAt(cursor, QLatin1StringView("info"))) {
    qsizetype skipped = 0;
    while (cursor.offset + skipped < source.size() &&
           !source.at(cursor.offset + skipped).isSpace())
      ++skipped;
    lexerError(cursor, qMax<qsizetype>(1, skipped), true);
  }
  consume(cursor, 4);
  cursor.skipWhitespace();

  if (wordAt(cursor, QLatin1StringView("showInfo"))) {
    consume(cursor, 8);
    cursor.skipWhitespace();
  }

  InfoData data;
  while (!cursor.atEnd()) {
    if (cursor.source.mid(cursor.offset, 2) == QLatin1String("%%")) {
      (void)lineRemainder(cursor);
      cursor.skipWhitespace();
      continue;
    }
    if (wordAt(cursor, QLatin1StringView("showInfo")))
      parserError(cursor, QStringLiteral("showInfo"));

    if (cursor.source.mid(cursor.offset, 6) == QLatin1String("title:")) {
      consume(cursor, 5);
      lexerErrorsToEnd(cursor);
    }

    if (wordAt(cursor, QLatin1StringView("title"))) {
      consume(cursor, 5);
      if (!cursor.atEnd() && (cursor.current() == QLatin1Char(' ') ||
                              cursor.current() == QLatin1Char('\t'))) {
        while (!cursor.atEnd() &&
               (cursor.current() == QLatin1Char(' ') ||
                cursor.current() == QLatin1Char('\t')))
          cursor.advance();
        data.title = lineRemainder(cursor).trimmed();
      } else {
        data.title.clear();
      }
      cursor.skipWhitespace();
      continue;
    }

    if (cursor.source.mid(cursor.offset, 8) == QLatin1String("accTitle")) {
      consume(cursor, 8);
      while (!cursor.atEnd() &&
             (cursor.current() == QLatin1Char(' ') ||
              cursor.current() == QLatin1Char('\t')))
        cursor.advance();
      if (cursor.atEnd() || cursor.current() != QLatin1Char(':'))
        lexerError(cursor);
      cursor.advance();
      data.accTitle = lineRemainder(cursor).trimmed();
      cursor.skipWhitespace();
      continue;
    }

    if (cursor.source.mid(cursor.offset, 8) == QLatin1String("accDescr")) {
      consume(cursor, 8);
      while (!cursor.atEnd() &&
             (cursor.current() == QLatin1Char(' ') ||
              cursor.current() == QLatin1Char('\t')))
        cursor.advance();
      if (!cursor.atEnd() && cursor.current() == QLatin1Char(':')) {
        cursor.advance();
        data.accDescr = lineRemainder(cursor).trimmed();
      } else {
        cursor.skipWhitespace();
        if (cursor.atEnd() || cursor.current() != QLatin1Char('{'))
          lexerError(cursor);
        cursor.advance();
        const qsizetype begin = cursor.offset;
        while (!cursor.atEnd() && cursor.current() != QLatin1Char('}'))
          cursor.advance();
        if (cursor.atEnd()) lexerError(cursor);
        data.accDescr = deindentAccDescr(
            cursor.source.mid(begin, cursor.offset - begin));
        cursor.advance();
      }
      cursor.skipWhitespace();
      continue;
    }

    qsizetype skipped = 0;
    while (cursor.offset + skipped < source.size() &&
           !source.at(cursor.offset + skipped).isSpace())
      ++skipped;
    lexerError(cursor, qMax<qsizetype>(1, skipped));
  }
  return data;
}

}  // namespace muffin::mermaid::info
