#include "mermaid/packet/PacketDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QRegularExpression>
#include <QStringList>

#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace muffin::mermaid::packet {

PacketParseError::PacketParseError(const QString& message, int line,
                                   int column, PacketErrorKind kind)
    : std::runtime_error(message.toUtf8().constData()),
      line(line),
      column(column),
      kind(kind) {}

namespace {

constexpr qsizetype kMaxPacketSize = 10000;

bool inlineWhitespace(QChar ch) {
  return ch == QLatin1Char(' ') || ch == QLatin1Char('\t');
}

bool asciiDigit(QChar ch) {
  return ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
}

bool jsWhitespace(QChar ch) {
  const ushort u = ch.unicode();
  return (u >= 0x0009 && u <= 0x000d) || u == 0x0020 || u == 0x00a0 ||
         u == 0x1680 || (u >= 0x2000 && u <= 0x200a) || u == 0x2028 ||
         u == 0x2029 || u == 0x202f || u == 0x205f || u == 0x3000 ||
         u == 0xfeff;
}

QString normalizedSingleLine(QString value) {
  static const QRegularExpression repeatedInlineWhitespace(
      QStringLiteral(R"([\t ]{2,})"));
  value = value.trimmed();
  value.replace(repeatedInlineWhitespace, QStringLiteral(" "));
  return value;
}

QString normalizedBlock(QString value) {
  static const QRegularExpression repeatedInlineWhitespace(
      QStringLiteral(R"([\t ]{2,})"));
  value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  QStringList lines = value.split(QLatin1Char('\n'));
  for (QString& line : lines) {
    line = line.trimmed();
    line.replace(repeatedInlineWhitespace, QStringLiteral(" "));
  }
  QString result = lines.join(QLatin1Char('\n')).trimmed();
  static const QRegularExpression repeatedNewlines(QStringLiteral(R"(\n{2,})"));
  result.replace(repeatedNewlines, QStringLiteral("\n"));
  return result;
}

QString sanitizedMetadata(QString value) {
  return HtmlSanitizer().sanitizedMermaidText(value);
}

QString jsNumberString(qreal value) {
  if (std::isnan(value)) return QStringLiteral("NaN");
  if (std::isinf(value))
    return value < 0 ? QStringLiteral("-Infinity") : QStringLiteral("Infinity");
  if (value == 0.0) return QStringLiteral("0");

  char buffer[64];
  const auto converted =
      std::to_chars(buffer, buffer + sizeof(buffer), double(value),
                    std::chars_format::general);
  if (converted.ec != std::errc())
    return QString::number(value, 'g',
                           std::numeric_limits<double>::max_digits10);
  std::string raw(buffer, converted.ptr);
  const size_t exponentAt = raw.find_first_of("eE");
  if (exponentAt == std::string::npos) return QString::fromStdString(raw);

  const std::string mantissa = raw.substr(0, exponentAt);
  const int exponent = std::stoi(raw.substr(exponentAt + 1));
  if (exponent >= -6 && exponent < 21) {
    const bool negative = !mantissa.empty() && mantissa.front() == '-';
    const size_t start = negative ? 1 : 0;
    const size_t dot = mantissa.find('.', start);
    std::string digits = mantissa.substr(start);
    const int beforeDot = dot == std::string::npos
                              ? static_cast<int>(digits.size())
                              : static_cast<int>(dot - start);
    if (dot != std::string::npos) digits.erase(dot - start, 1);
    const int decimalPos = beforeDot + exponent;
    std::string fixed = negative ? "-" : "";
    if (decimalPos <= 0) {
      fixed += "0.";
      fixed.append(static_cast<size_t>(-decimalPos), '0');
      fixed += digits;
    } else if (decimalPos >= static_cast<int>(digits.size())) {
      fixed += digits;
      fixed.append(
          static_cast<size_t>(decimalPos - static_cast<int>(digits.size())),
          '0');
    } else {
      fixed += digits.substr(0, static_cast<size_t>(decimalPos));
      fixed += '.';
      fixed += digits.substr(static_cast<size_t>(decimalPos));
    }
    return QString::fromStdString(fixed);
  }

  std::string normalized = mantissa;
  normalized += 'e';
  normalized += exponent >= 0 ? '+' : '-';
  normalized += std::to_string(std::abs(exponent));
  return QString::fromStdString(normalized);
}

double jsRadixNumber(const QString& digits, int base) {
  if (digits.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
  double value = 0.0;
  for (const QChar ch : digits) {
    const int lc = ch.toLower().toLatin1();
    int digit = -1;
    if (lc >= '0' && lc <= '9') digit = lc - '0';
    else if (lc >= 'a' && lc <= 'z') digit = lc - 'a' + 10;
    if (digit < 0 || digit >= base)
      return std::numeric_limits<double>::quiet_NaN();
    value = value * base + digit;
  }
  return value;
}

double jsNumberStringValue(QString value) {
  qsizetype begin = 0;
  qsizetype end = value.size();
  while (begin < end && jsWhitespace(value.at(begin))) ++begin;
  while (end > begin && jsWhitespace(value.at(end - 1))) --end;
  value = value.mid(begin, end - begin);
  if (value.isEmpty()) return 0.0;
  if (value == QLatin1String("Infinity") ||
      value == QLatin1String("+Infinity"))
    return std::numeric_limits<double>::infinity();
  if (value == QLatin1String("-Infinity"))
    return -std::numeric_limits<double>::infinity();
  if (value.size() > 2 &&
      value.left(2).compare(QLatin1String("0x"), Qt::CaseInsensitive) == 0)
    return jsRadixNumber(value.mid(2), 16);
  if (value.size() > 2 &&
      value.left(2).compare(QLatin1String("0b"), Qt::CaseInsensitive) == 0)
    return jsRadixNumber(value.mid(2), 2);
  if (value.size() > 2 &&
      value.left(2).compare(QLatin1String("0o"), Qt::CaseInsensitive) == 0)
    return jsRadixNumber(value.mid(2), 8);
  bool ok = false;
  const double number = value.toDouble(&ok);
  return ok ? number : std::numeric_limits<double>::quiet_NaN();
}

struct BitsPerRow {
  QJsonValue raw = QJsonValue(32.0);
  double numeric = 32.0;
  double wordLengthLimit = 33.0;
};

BitsPerRow resolveBitsPerRow(const QJsonValue& input) {
  BitsPerRow result;
  if (input.isUndefined() || input.isNull() || input.isArray() ||
      input.isObject())
    return result;

  result.raw = input;
  if (input.isString()) {
    result.numeric = jsNumberStringValue(input.toString());
    result.wordLengthLimit =
        jsNumberStringValue(input.toString() + QLatin1Char('1'));
  } else if (input.isBool()) {
    result.numeric = input.toBool() ? 1.0 : 0.0;
    result.wordLengthLimit = result.numeric + 1.0;
  } else {
    result.numeric = input.toDouble();
    result.wordLengthLimit = result.numeric + 1.0;
  }
  return result;
}

class Cursor {
public:
  explicit Cursor(QString source) : text(std::move(source)) {}

  bool atEnd() const { return pos >= text.size(); }
  QChar peek(qsizetype ahead = 0) const {
    return pos + ahead < text.size() ? text.at(pos + ahead) : QChar();
  }
  int currentLine() const { return line; }
  int currentColumn() const { return column; }
  qsizetype offset() const { return pos; }

  void advance() {
    if (atEnd()) return;
    const QChar ch = text.at(pos++);
    if (ch == QLatin1Char('\r')) {
      if (!atEnd() && peek() == QLatin1Char('\n')) ++pos;
      ++line;
      column = 1;
    } else if (ch == QLatin1Char('\n')) {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  }

  void inlineWs() {
    while (!atEnd() && inlineWhitespace(peek())) advance();
  }

  void jsWs() {
    while (!atEnd() && jsWhitespace(peek())) advance();
  }

  bool newline() {
    if (peek() != QLatin1Char('\r') && peek() != QLatin1Char('\n'))
      return false;
    advance();
    return true;
  }

  void newlines() {
    while (newline()) {}
  }

  bool startsWith(QLatin1String value) const {
    return text.mid(pos, value.size()) == value;
  }

  void consume(qsizetype count) {
    while (count-- > 0) advance();
  }

  bool take(QChar ch) {
    if (peek() != ch) return false;
    advance();
    return true;
  }

  void skipComment() {
    if (!startsWith(QLatin1String("%%"))) return;
    while (!atEnd() && peek() != QLatin1Char('\r') &&
           peek() != QLatin1Char('\n'))
      advance();
  }

  QString untilLineEndOrComment() {
    const qsizetype begin = pos;
    while (!atEnd() && peek() != QLatin1Char('\r') &&
           peek() != QLatin1Char('\n') &&
           !startsWith(QLatin1String("%%")))
      advance();
    return text.mid(begin, pos - begin);
  }

  double integer() {
    const int errorLine = line;
    const int errorColumn = column;
    const qsizetype begin = pos;
    if (!asciiDigit(peek()))
      fail(QStringLiteral("expected INT"), PacketErrorKind::Parser);
    if (peek() == QLatin1Char('0')) {
      advance();
      if (asciiDigit(peek()))
        fail(QStringLiteral("invalid leading zero"), PacketErrorKind::Parser);
    } else {
      while (asciiDigit(peek())) advance();
    }
    if (peek() == QLatin1Char('.') ||
        peek() == QLatin1Char('e') || peek() == QLatin1Char('E'))
      throw PacketParseError(QStringLiteral("invalid INT"), errorLine,
                             currentColumn(), PacketErrorKind::Lexer);
    bool ok = false;
    const double result = text.mid(begin, pos - begin).toDouble(&ok);
    if (!ok && !std::isinf(result))
      throw PacketParseError(QStringLiteral("invalid INT"), errorLine,
                             errorColumn, PacketErrorKind::Lexer);
    return result;
  }

  QString string() {
    const int stringLine = line;
    const int stringColumn = column;
    if (peek() != QLatin1Char('"') && peek() != QLatin1Char('\'')) {
      // Langium reports an absent required terminal as a parser diagnostic,
      // but a present bare token is a lexer diagnostic.
      if (atEnd() || peek() == QLatin1Char('\r') ||
          peek() == QLatin1Char('\n'))
        fail(QStringLiteral("expected quoted STRING"),
             PacketErrorKind::Parser);
      fail(QStringLiteral("expected quoted STRING"), PacketErrorKind::Lexer);
    }
    const QChar quote = peek();
    advance();
    QString result;
    while (!atEnd()) {
      QChar ch = peek();
      advance();
      if (ch == quote) return result;
      if (ch != QLatin1Char('\\')) {
        result.append(ch);
        continue;
      }
      if (atEnd())
        fail(QStringLiteral("unterminated STRING escape"),
             PacketErrorKind::Lexer);
      ch = peek();
      advance();
      if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n'))
        throw PacketParseError(QStringLiteral("unterminated STRING"),
                               stringLine, stringColumn,
                               PacketErrorKind::Lexer);
      if (ch == QLatin1Char('b')) result.append(QChar(u'\b'));
      else if (ch == QLatin1Char('f')) result.append(QChar(u'\f'));
      else if (ch == QLatin1Char('n')) result.append(QChar(u'\n'));
      else if (ch == QLatin1Char('r')) result.append(QChar(u'\r'));
      else if (ch == QLatin1Char('t')) result.append(QChar(u'\t'));
      else if (ch == QLatin1Char('v')) result.append(QChar(u'\v'));
      else if (ch == QLatin1Char('0')) result.append(QChar(u'\0'));
      else result.append(ch);
    }
    throw PacketParseError(QStringLiteral("unterminated STRING"), stringLine,
                           stringColumn, PacketErrorKind::Lexer);
  }

  [[noreturn]] void fail(const QString& message, PacketErrorKind kind) const {
    throw PacketParseError(message, line, column, kind);
  }

private:
  QString text;
  qsizetype pos = 0;
  int line = 1;
  int column = 1;
};

struct AstBlock {
  std::optional<double> start;
  std::optional<double> end;
  std::optional<double> bits;
  QString label;
  int line = 1;
  int column = 1;
};

void requireEol(Cursor& cursor) {
  cursor.inlineWs();
  if (cursor.startsWith(QLatin1String("%%"))) cursor.skipComment();
  if (cursor.atEnd()) return;
  if (!cursor.newline()) {
    // A second grammar token on the same line is a parser error; characters
    // which cannot begin any packet token fail in the lexer first.
    const QChar trailing = cursor.peek();
    if (trailing == QLatin1Char(';') || trailing == QLatin1Char('$') ||
        trailing == QLatin1Char('%') || trailing.isLetter())
      cursor.fail(QStringLiteral("invalid packet character"),
                  PacketErrorKind::Lexer);
    cursor.fail(QStringLiteral("expected end of line"),
                PacketErrorKind::Parser);
  }
  cursor.newlines();
}

AstBlock parseBlock(Cursor& cursor) {
  AstBlock block;
  block.line = cursor.currentLine();
  block.column = cursor.currentColumn();
  if (cursor.take(QLatin1Char('+'))) {
    cursor.inlineWs();
    block.bits = cursor.integer();
  } else {
    block.start = cursor.integer();
    cursor.inlineWs();
    if (cursor.take(QLatin1Char('-'))) {
      cursor.inlineWs();
      block.end = cursor.integer();
    }
  }
  cursor.inlineWs();
  if (!cursor.take(QLatin1Char(':')))
    cursor.fail(QStringLiteral("expected ':'"), PacketErrorKind::Parser);
  cursor.inlineWs();
  block.label = cursor.string();
  requireEol(cursor);
  return block;
}

void parseMetadata(Cursor& cursor, PacketData& data) {
  if (cursor.startsWith(QLatin1String("title"))) {
    cursor.consume(5);
    if (!cursor.atEnd() && !inlineWhitespace(cursor.peek()) &&
        cursor.peek() != QLatin1Char('\r') &&
        cursor.peek() != QLatin1Char('\n')) {
      if (cursor.take(QLatin1Char(':'))) {
        cursor.inlineWs();
        if (!cursor.atEnd() && cursor.peek() != QLatin1Char('\r') &&
            cursor.peek() != QLatin1Char('\n'))
          cursor.fail(QStringLiteral("invalid title"), PacketErrorKind::Lexer);
        cursor.fail(QStringLiteral("invalid title"), PacketErrorKind::Parser);
      }
      cursor.fail(QStringLiteral("invalid title"), PacketErrorKind::Lexer);
    }
    cursor.inlineWs();
    data.title = sanitizedMetadata(
        normalizedSingleLine(cursor.untilLineEndOrComment()));
    requireEol(cursor);
    return;
  }
  if (cursor.startsWith(QLatin1String("accTitle"))) {
    cursor.consume(8);
    cursor.inlineWs();
    if (!cursor.take(QLatin1Char(':')))
      cursor.fail(QStringLiteral("accTitle requires ':'"),
                  PacketErrorKind::Parser);
    data.accTitle = sanitizedMetadata(
        normalizedSingleLine(cursor.untilLineEndOrComment()));
    requireEol(cursor);
    return;
  }
  cursor.consume(8);  // accDescr
  cursor.inlineWs();
  if (cursor.take(QLatin1Char(':'))) {
    data.accDescr = sanitizedMetadata(
        normalizedSingleLine(cursor.untilLineEndOrComment()));
    requireEol(cursor);
    return;
  }
  // ACC_DESCR's block alternative uses JavaScript `\s*`, unlike its colon
  // alternative which permits only horizontal space.
  cursor.jsWs();
  if (!cursor.take(QLatin1Char('{')))
    cursor.fail(QStringLiteral("invalid accDescr"), PacketErrorKind::Parser);
  QString body;
  while (!cursor.atEnd() && cursor.peek() != QLatin1Char('}')) {
    body.append(cursor.peek());
    cursor.advance();
  }
  if (!cursor.take(QLatin1Char('}')))
    cursor.fail(QStringLiteral("unterminated accDescr"),
                PacketErrorKind::Lexer);
  data.accDescr = sanitizedMetadata(normalizedBlock(body));
  requireEol(cursor);
}

std::pair<PacketBlock, std::optional<AstBlock>> nextFittingBlock(
    const AstBlock& block, qreal row, const BitsPerRow& bitsPerRow) {
  const qreal start = *block.start;
  const qreal end = *block.end;
  if (start > end)
    throw PacketParseError(
        QStringLiteral("Block start %1 is greater than block end %2.")
            .arg(jsNumberString(start), jsNumberString(end)),
        block.line, block.column, PacketErrorKind::Runtime);
  const qreal rowProduct = row * bitsPerRow.numeric;
  if (end + 1.0 <= rowProduct)
    return {PacketBlock{start, end, *block.bits, block.label}, std::nullopt};

  const qreal rowEnd = rowProduct - 1.0;
  const qreal rowStart = rowProduct;
  PacketBlock fitting{start, rowEnd, rowEnd - start, block.label};
  AstBlock next;
  next.start = rowStart;
  next.end = end;
  next.bits = end - rowStart;
  next.label = block.label;
  next.line = block.line;
  next.column = block.column;
  return {fitting, next};
}

void populate(const QVector<AstBlock>& ast, const BitsPerRow& bitsPerRow,
              PacketData& data) {
  qreal lastBit = -1.0;
  PacketWord word;
  qreal row = 1.0;

  for (AstBlock block : ast) {
    if (block.start && block.end && *block.end < *block.start) {
      throw PacketParseError(
          QStringLiteral("Packet block %1 - %2 is invalid. End must be greater than start.")
              .arg(jsNumberString(*block.start), jsNumberString(*block.end)),
          block.line, block.column, PacketErrorKind::Runtime);
    }
    if (!block.start) block.start = lastBit + 1.0;
    if (*block.start != lastBit + 1.0) {
      throw PacketParseError(
          QStringLiteral("Packet block %1 - %2 is not contiguous. It should start from %3.")
              .arg(jsNumberString(*block.start),
                   jsNumberString(block.end.value_or(*block.start)),
                   jsNumberString(lastBit + 1.0)),
          block.line, block.column, PacketErrorKind::Runtime);
    }
    if (block.bits && *block.bits == 0.0) {
      throw PacketParseError(
          QStringLiteral("Packet block %1 is invalid. Cannot have a zero bit field.")
              .arg(jsNumberString(*block.start)),
          block.line, block.column, PacketErrorKind::Runtime);
    }
    if (!block.end)
      block.end = *block.start + block.bits.value_or(1.0) - 1.0;
    if (!block.bits) block.bits = *block.end - *block.start + 1.0;
    lastBit = *block.end;

    while (qreal(word.size()) <= bitsPerRow.wordLengthLimit &&
           data.words.size() < kMaxPacketSize) {
      auto [fitting, next] = nextFittingBlock(block, row, bitsPerRow);
      word.append(std::move(fitting));
      if (word.back().end + 1.0 == row * bitsPerRow.numeric) {
        if (!word.isEmpty()) data.words.append(word);
        word.clear();
        row += 1.0;
      }
      if (!next) break;
      block = std::move(*next);
    }
  }
  if (!word.isEmpty()) data.words.append(std::move(word));
}

}  // namespace

PacketData PacketDiagram::parse(const QString& source,
                                const QJsonValue& rawBitsPerRow) {
  Cursor cursor(source);
  PacketData data;
  QVector<AstBlock> blocks;

  cursor.newlines();
  cursor.inlineWs();
  if (cursor.startsWith(QLatin1String("packet-beta"))) {
    cursor.consume(11);
  } else if (cursor.startsWith(QLatin1String("packet"))) {
    cursor.consume(6);
    if (!cursor.atEnd() && !inlineWhitespace(cursor.peek()) &&
        cursor.peek() != QLatin1Char('\r') &&
        cursor.peek() != QLatin1Char('\n') &&
        !cursor.startsWith(QLatin1String("%%")))
      throw PacketParseError(QStringLiteral("invalid packet header"),
                             cursor.currentLine(),
                             qMax(1, cursor.currentColumn() - 6),
                             PacketErrorKind::Lexer);
  } else {
    cursor.fail(QStringLiteral("missing packet header"),
                PacketErrorKind::Lexer);
  }

  while (!cursor.atEnd()) {
    cursor.inlineWs();
    if (cursor.newline()) {
      cursor.newlines();
      continue;
    }
    if (cursor.startsWith(QLatin1String("%%"))) {
      cursor.skipComment();
      cursor.newline();
      continue;
    }
    if (cursor.startsWith(QLatin1String("title")) ||
        cursor.startsWith(QLatin1String("accTitle")) ||
        cursor.startsWith(QLatin1String("accDescr"))) {
      parseMetadata(cursor, data);
      continue;
    }
    if (cursor.peek() == QLatin1Char('+') || asciiDigit(cursor.peek())) {
      blocks.append(parseBlock(cursor));
      continue;
    }
    if (cursor.peek() == QLatin1Char('-') ||
        cursor.peek() == QLatin1Char(':') ||
        cursor.peek() == QLatin1Char('"') ||
        cursor.peek() == QLatin1Char('\''))
      cursor.fail(QStringLiteral("unexpected packet token"),
                  PacketErrorKind::Parser);
    cursor.fail(QStringLiteral("unexpected packet token"),
                PacketErrorKind::Lexer);
  }

  populate(blocks, resolveBitsPerRow(rawBitsPerRow), data);
  return data;
}

}  // namespace muffin::mermaid::packet
