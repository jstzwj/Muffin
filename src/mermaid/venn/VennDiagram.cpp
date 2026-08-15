#include "mermaid/venn/VennDiagram.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace muffin::mermaid::venn {
namespace {

bool asciiAlpha(QChar ch) {
  const ushort value = ch.unicode();
  return (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z') || value == '_';
}

bool asciiDigit(QChar ch) {
  const ushort value = ch.unicode();
  return value >= '0' && value <= '9';
}

bool asciiWord(QChar ch) { return asciiAlpha(ch) || asciiDigit(ch); }

QString collapseErrorWhitespace(QString value) {
  value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
  return value.trimmed();
}

QString parseMessage(int line, const QString& preview, int caret,
                     const QString& expected, const QString& token) {
  return collapseErrorWhitespace(
      QStringLiteral("Parse error on line %1:\n%2\n%3^\nExpecting %4, got '%5'")
          .arg(line)
          .arg(preview, QString(std::max(0, caret), QLatin1Char('-')),
               expected, token));
}

QString lexicalMessage(int line, const QString& preview, int caret) {
  return collapseErrorWhitespace(
      QStringLiteral("Lexical error on line %1. Unrecognized text.\n%2\n%3^")
          .arg(line)
          .arg(preview, QString(std::max(0, caret), QLatin1Char('-'))));
}

QPair<QString, int> errorWindow(const QString& stream, qsizetype offset) {
  constexpr qsizetype kBefore = 20;
  constexpr qsizetype kFromToken = 19;
  const qsizetype start = std::max<qsizetype>(0, offset - kBefore);
  const qsizetype end = std::min(stream.size(), offset + kFromToken);
  return {stream.mid(start, end - start), int(offset - start)};
}

QString normalizeText(QString value) {
  value = value.trimmed();
  if (value.size() >= 2 && value.front() == QLatin1Char('"') &&
      value.back() == QLatin1Char('"'))
    return value.mid(1, value.size() - 2);
  return value;
}

QString stripComment(QString line) {
  qsizetype quote = -1;
  int squareDepth = 0;
  int parenDepth = 0;
  for (qsizetype i = 0; i + 1 < line.size(); ++i) {
    const QChar ch = line.at(i);
    if (ch == QLatin1Char('"')) quote = quote < 0 ? i : -1;
    if (quote >= 0) continue;
    if (ch == QLatin1Char('[')) ++squareDepth;
    else if (ch == QLatin1Char(']')) squareDepth = std::max(0, squareDepth - 1);
    else if (ch == QLatin1Char('(')) ++parenDepth;
    else if (ch == QLatin1Char(')')) parenDepth = std::max(0, parenDepth - 1);
    if (squareDepth == 0 && parenDepth == 0 && ch == QLatin1Char('%') &&
        line.at(i + 1) == QLatin1Char('%'))
      return line.left(i);
  }
  return line;
}

class StatementCursor {
 public:
  StatementCursor(QString text, int line, QString errorStream,
                  qsizetype streamOffset)
      : text_(std::move(text)),
        line_(line),
        errorStream_(std::move(errorStream)),
        streamOffset_(streamOffset) {}

  bool atEnd() const { return pos_ >= text_.size(); }
  QChar peek(qsizetype offset = 0) const {
    const qsizetype index = pos_ + offset;
    return index < text_.size() ? text_.at(index) : QChar();
  }
  int column() const { return int(pos_ + 1); }

  void skipSpace() {
    while (peek() == QLatin1Char(' ') || peek() == QLatin1Char('\t')) ++pos_;
  }

  bool keyword(QLatin1String word) const {
    if (text_.mid(pos_, word.size()).compare(word, Qt::CaseInsensitive) != 0)
      return false;
    const qsizetype after = pos_ + word.size();
    return after >= text_.size() || !asciiWord(text_.at(after));
  }

  void takeKeyword(QLatin1String word) {
    if (!keyword(word)) parserError(QString(word));
    pos_ += word.size();
  }

  QString identifier() {
    skipSpace();
    if (peek() == QLatin1Char('"')) {
      const qsizetype start = pos_++;
      while (!atEnd() && peek() != QLatin1Char('"')) ++pos_;
      if (atEnd()) lexerError(start);
      ++pos_;
      return normalizeText(text_.mid(start, pos_ - start));
    }
    if (!asciiAlpha(peek())) parserError(QStringLiteral("IDENTIFIER"));
    const qsizetype start = pos_++;
    while (asciiWord(peek()) || peek() == QLatin1Char('-')) ++pos_;
    return text_.mid(start, pos_ - start);
  }

  QStringList identifierList() {
    QStringList result{identifier()};
    for (;;) {
      skipSpace();
      if (peek() != QLatin1Char(',')) break;
      ++pos_;
      result.append(identifier());
    }
    return result;
  }

  std::optional<QString> label() {
    skipSpace();
    if (peek() != QLatin1Char('[')) return std::nullopt;
    const qsizetype start = pos_++;
    if (peek() == QLatin1Char('"')) {
      ++pos_;
      while (!atEnd() && peek() != QLatin1Char('"')) ++pos_;
      if (atEnd() || peek(1) != QLatin1Char(']')) lexerError(start);
      ++pos_;
      ++pos_;
      return normalizeText(text_.mid(start + 2, pos_ - start - 4));
    }
    const qsizetype content = pos_;
    while (!atEnd() && peek() != QLatin1Char(']') &&
           peek() != QLatin1Char('"'))
      ++pos_;
    if (atEnd() || peek() != QLatin1Char(']')) lexerError(start);
    const QString value = text_.mid(content, pos_ - content).trimmed();
    ++pos_;
    return value;
  }

  std::optional<double> size() {
    skipSpace();
    if (peek() != QLatin1Char(':')) return std::nullopt;
    ++pos_;
    skipSpace();
    const qsizetype start = pos_;
    if (peek() == QLatin1Char('+') || peek() == QLatin1Char('-')) ++pos_;
    bool digits = false;
    while (asciiDigit(peek())) {
      digits = true;
      ++pos_;
    }
    if (peek() == QLatin1Char('.')) {
      if (!asciiDigit(peek(1))) lexerError(pos_);
      ++pos_;
      while (asciiDigit(peek())) {
        digits = true;
        ++pos_;
      }
    }
    if (!digits) parserError(QStringLiteral("NUMERIC"));
    return text_.mid(start, pos_ - start).toDouble();
  }

  QVector<QPair<QString, QString>> styles() {
    QVector<QPair<QString, QString>> result;
    for (;;) {
      skipSpace();
      const QString key = identifier();
      skipSpace();
      if (peek() != QLatin1Char(':')) parserError(QStringLiteral("COLON"));
      ++pos_;
      skipSpace();
      const qsizetype start = pos_;
      bool quoted = false;
      int parens = 0;
      while (!atEnd()) {
        const QChar ch = peek();
        if (ch == QLatin1Char('"')) quoted = !quoted;
        else if (!quoted && ch == QLatin1Char('(')) ++parens;
        else if (!quoted && ch == QLatin1Char(')')) parens = std::max(0, parens - 1);
        if (!quoted && parens == 0 && ch == QLatin1Char(',')) {
          qsizetype look = pos_ + 1;
          while (look < text_.size() &&
                 (text_.at(look) == QLatin1Char(' ') ||
                  text_.at(look) == QLatin1Char('\t')))
            ++look;
          qsizetype word = look;
          if (word < text_.size() && asciiAlpha(text_.at(word))) {
            ++word;
            while (word < text_.size() &&
                   (asciiWord(text_.at(word)) || text_.at(word) == QLatin1Char('-')))
              ++word;
            while (word < text_.size() &&
                   (text_.at(word) == QLatin1Char(' ') ||
                    text_.at(word) == QLatin1Char('\t')))
              ++word;
            if (word < text_.size() && text_.at(word) == QLatin1Char(':')) break;
          }
        }
        ++pos_;
      }
      QString value = text_.mid(start, pos_ - start).trimmed();
      if (value.size() >= 2 && value.front() == QLatin1Char('"') &&
          value.back() == QLatin1Char('"'))
        value = value.mid(1, value.size() - 2);
      value.replace(QRegularExpression(QStringLiteral("[ \\t]+")),
                    QStringLiteral(" "));
      result.append({key, value});
      if (atEnd()) break;
      ++pos_;
    }
    return result;
  }

  void requireEnd() {
    skipSpace();
    if (atEnd()) return;
    if (peek() == QLatin1Char(';') || peek() == QLatin1Char('.') ||
        (!asciiAlpha(peek()) && !asciiDigit(peek())))
      lexerError(pos_);
    parserError(QStringLiteral("IDENTIFIER"));
  }

 private:
  [[noreturn]] void lexerError(qsizetype at) const {
    const auto window = errorWindow(errorStream_, streamOffset_ + at);
    throw VennParseError(VennErrorKind::Lexer, line_, 0, QString(),
                         lexicalMessage(line_, window.first, window.second));
  }

  [[noreturn]] void parserError(const QString& token) const {
    const auto window = errorWindow(errorStream_, streamOffset_ + pos_);
    throw VennParseError(
        VennErrorKind::Parser, line_, int(pos_), token,
        parseMessage(line_, window.first, window.second,
                      QStringLiteral("'EOF', 'NEWLINE', 'TITLE', 'SET', 'UNION', 'TEXT', 'INDENT_TEXT', 'STYLE'"),
                      token));
  }

  QString text_;
  int line_ = 1;
  qsizetype pos_ = 0;
  QString errorStream_;
  qsizetype streamOffset_ = 0;
};

bool startsKeyword(const QString& text, QLatin1String word) {
  if (!text.startsWith(word, Qt::CaseInsensitive)) return false;
  return text.size() == word.size() || !asciiWord(text.at(word.size()));
}

void appendSubset(VennData& data, QSet<QString>& knownSets,
                  QStringList sets, std::optional<QString> label,
                  std::optional<double> size) {
  std::sort(sets.begin(), sets.end());
  const int sourceCount = sets.size();
  if (sourceCount == 1) knownSets.insert(sets.front());
  VennSubset subset;
  subset.sets = std::move(sets);
  subset.size = size.value_or(10.0 / std::pow(sourceCount, 2));
  if (label && !label->isEmpty()) {
    subset.label = normalizeText(*label);
    subset.hasLabel = true;
  }
  data.subsets.append(std::move(subset));
}

}  // namespace

VennParseError::VennParseError(VennErrorKind kind, int line, int column,
                               QString token, const QString& message)
    : std::runtime_error(message.toUtf8().constData()),
      kind(kind),
      line(line),
      column(column),
      token(std::move(token)) {}

VennData VennDiagram::parse(const QString& source) {
  VennData data;
  QSet<QString> knownSets;
  QStringList currentSets;
  QString normalized = source;
  normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  const QStringList physical = normalized.split(QLatin1Char('\n'), Qt::KeepEmptyParts);

  int headerLine = -1;
  qsizetype headerAt = 0;
  qsizetype headerEnd = 0;
  for (int i = 0; i < physical.size(); ++i) {
    QString line = stripComment(physical.at(i));
    qsizetype at = 0;
    while (at < line.size() &&
           (line.at(at) == QLatin1Char(' ') || line.at(at) == QLatin1Char('\t')))
      ++at;
    if (at >= line.size()) continue;
    const QString tail = line.mid(at);
    if (!startsKeyword(tail, QLatin1String("venn-beta"))) {
      const QString preview =
          i + 1 < physical.size() ? line + stripComment(physical.at(i + 1))
                                  : line;
      throw VennParseError(
          VennErrorKind::Parser, i + 1, int(at + 1), QStringLiteral("IDENTIFIER"),
          parseMessage(i + 1, preview, int(at), QStringLiteral("'VENN', 'NEWLINE'"),
                       QStringLiteral("IDENTIFIER")));
    }
    headerLine = i;
    headerAt = at;
    headerEnd = at + QLatin1String("venn-beta").size();
    break;
  }
  if (headerLine < 0)
    throw VennParseError(VennErrorKind::Parser, 1, 1, QStringLiteral("EOF"),
                         parseMessage(1, QString(), 0,
                                      QStringLiteral("'VENN', 'NEWLINE'"),
                                      QStringLiteral("EOF")));

  QString errorStream;
  QVector<qsizetype> lineOffsets(physical.size());
  for (int i = headerLine; i < physical.size(); ++i) {
    lineOffsets[i] = errorStream.size();
    errorStream += stripComment(physical.at(i));
  }

  struct Statement {
    int line = 1;
    QString text;
    qsizetype streamOffset = 0;
  };
  QVector<Statement> statements;
  QString suffix = stripComment(physical.at(headerLine)).mid(headerEnd);
  if (!suffix.trimmed().isEmpty())
    statements.append(
        {headerLine + 1, suffix, lineOffsets.at(headerLine) + headerEnd});
  for (int i = headerLine + 1; i < physical.size(); ++i) {
    QString line = stripComment(physical.at(i));
    if (!line.trimmed().isEmpty())
      statements.append({i + 1, line, lineOffsets.at(i)});
  }

  int previousColumnAfter = 10;
  for (const Statement& statement : statements) {
    const int lineNumber = statement.line;
    QString line = statement.text;
    qsizetype leading = 0;
    while (leading < line.size() &&
           (line.at(leading) == QLatin1Char(' ') ||
            line.at(leading) == QLatin1Char('\t')))
      ++leading;
    line = line.mid(leading);

    if (startsKeyword(line, QLatin1String("title"))) {
      const qsizetype whitespace = QLatin1String("title").size();
      if (whitespace >= line.size() ||
          (line.at(whitespace) != QLatin1Char(' ') &&
           line.at(whitespace) != QLatin1Char('\t')))
        throw VennParseError(VennErrorKind::Parser, lineNumber,
                             int(leading + whitespace + 1),
                             QStringLiteral("IDENTIFIER"),
                             parseMessage(lineNumber, line, int(whitespace),
                                          QStringLiteral("'TITLE'"),
                                          QStringLiteral("IDENTIFIER")));
      data.title = line.mid(6);
      data.hasTitleDirective = true;
      continue;
    }

    StatementCursor cursor(line, lineNumber, errorStream,
                           statement.streamOffset + leading);
    if (cursor.keyword(QLatin1String("set"))) {
      cursor.takeKeyword(QLatin1String("set"));
      QStringList sets{cursor.identifier()};
      const std::optional<QString> label = cursor.label();
      const std::optional<double> size = cursor.size();
      cursor.requireEnd();
      currentSets = sets;
      appendSubset(data, knownSets, std::move(sets), label, size);
      previousColumnAfter = int(line.size() + 1);
    } else if (cursor.keyword(QLatin1String("union"))) {
      cursor.takeKeyword(QLatin1String("union"));
      QStringList sets = cursor.identifierList();
      if (sets.size() < 2)
        throw VennParseError(VennErrorKind::Runtime, lineNumber, 1,
                             QStringLiteral("union"),
                             QStringLiteral("union requires multiple identifiers"));
      QStringList unknown;
      for (const QString& set : sets)
        if (!knownSets.contains(set)) unknown.append(set);
      if (!unknown.isEmpty())
        throw VennParseError(
            VennErrorKind::Runtime, lineNumber, 1, QStringLiteral("union"),
            QStringLiteral("unknown set identifier: ") + unknown.join(QStringLiteral(", ")));
      const std::optional<QString> label = cursor.label();
      const std::optional<double> size = cursor.size();
      cursor.requireEnd();
      currentSets = sets;
      appendSubset(data, knownSets, std::move(sets), label, size);
      previousColumnAfter = int(line.size() + 1);
    } else if (cursor.keyword(QLatin1String("text"))) {
      cursor.takeKeyword(QLatin1String("text"));
      VennTextNode node;
      if (leading > 0 && !currentSets.isEmpty()) {
        node.sets = currentSets;
        node.id = normalizeText(cursor.identifier());
      } else {
        node.sets = cursor.identifierList();
        std::sort(node.sets.begin(), node.sets.end());
        node.id = normalizeText(cursor.identifier());
      }
      const std::optional<QString> label = cursor.label();
      cursor.requireEnd();
      if (label && !label->isEmpty()) {
        node.label = normalizeText(*label);
        node.hasLabel = true;
      }
      data.textNodes.append(std::move(node));
      previousColumnAfter = int(line.size() + 1);
    } else if (cursor.keyword(QLatin1String("style"))) {
      cursor.takeKeyword(QLatin1String("style"));
      VennStyleEntry entry;
      entry.targets = cursor.identifierList();
      std::sort(entry.targets.begin(), entry.targets.end());
      entry.declarations = cursor.styles();
      cursor.requireEnd();
      data.styles.append(std::move(entry));
      previousColumnAfter = int(line.size() + 1);
    } else {
      const bool lexical = !line.isEmpty() &&
                           (line.front() == QLatin1Char(';') ||
                            (!asciiAlpha(line.front()) &&
                             line.front() != QLatin1Char('"')));
      const auto window = errorWindow(errorStream,
                                      statement.streamOffset + leading);
      throw VennParseError(
          lexical ? VennErrorKind::Lexer : VennErrorKind::Parser,
          lineNumber, lexical ? 0 : previousColumnAfter,
          lexical ? QString() : QStringLiteral("IDENTIFIER"),
          lexical
              ? lexicalMessage(lineNumber, window.first, window.second)
              : parseMessage(
                    lineNumber, window.first, window.second,
                    QStringLiteral("'EOF', 'NEWLINE', 'TITLE', 'SET', 'UNION', 'TEXT', 'INDENT_TEXT', 'STYLE'"),
                    QStringLiteral("IDENTIFIER")));
    }
  }
  Q_UNUSED(headerAt);
  return data;
}

}  // namespace muffin::mermaid::venn
