#include "mermaid/sankey/SankeyDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QHash>
#include <QRegularExpression>

#include <cmath>

namespace muffin::mermaid::sankey {
namespace {

[[noreturn]] void parserError(int line, int column, const QString &token,
                              const QString &detail) {
  throw SankeyParseError(SankeyErrorKind::Parser, line, column, token, detail);
}

bool asciiFieldChar(QChar c) {
  const ushort u = c.unicode();
  return (u >= 0x20 && u <= 0x21) || (u >= 0x23 && u <= 0x2b) ||
         (u >= 0x2d && u <= 0x7e);
}

struct CsvField {
  QString value;
  int end = 0;
};

CsvField parseField(const QString &text, int start, int line) {
  if (start >= text.size())
    parserError(line, start + 1, QStringLiteral("EOF"),
                QStringLiteral("Expected Sankey CSV field"));
  if (text.at(start) != QLatin1Char('"')) {
    int i = start;
    while (i < text.size() && text.at(i) != QLatin1Char(',') &&
           text.at(i) != QLatin1Char('\n')) {
      if (!asciiFieldChar(text.at(i)))
        parserError(line, i + 1, text.mid(i, 1),
                    QStringLiteral("Invalid Sankey CSV character"));
      ++i;
    }
    if (i == start)
      parserError(line, start + 1, text.mid(start, 1),
                  QStringLiteral("Expected Sankey CSV field"));
    return {text.mid(start, i - start), i};
  }

  QString value;
  int i = start + 1;
  while (i < text.size()) {
    const QChar c = text.at(i);
    if (c == QLatin1Char('"')) {
      if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
        value += QStringLiteral("\"\"");
        i += 2;
        continue;
      }
      return {value, i + 1};
    }
    if (c != QLatin1Char(',') && c != QLatin1Char('\n') &&
        c != QLatin1Char('\r') && !asciiFieldChar(c))
      parserError(line, i + 1, text.mid(i, 1),
                  QStringLiteral("Invalid escaped Sankey CSV character"));
    if (c == QLatin1Char('\n'))
      ++line;
    value += c;
    ++i;
  }
  parserError(line, start + 1, QStringLiteral("EOF"),
              QStringLiteral("Unterminated Sankey CSV field"));
}

double jsParseFloat(const QString &source) {
  static const QRegularExpression number(QStringLiteral(
      R"(^[\x{0009}-\x{000D}\x{0020}\x{00A0}\x{FEFF}]*(?:([+-]?Infinity)|([+-]?(?:(?:\d+\.?\d*)|(?:\.\d+))(?:[eE][+-]?\d+)?)))"));
  const auto match = number.match(source);
  if (!match.hasMatch())
    return std::numeric_limits<double>::quiet_NaN();
  if (!match.captured(1).isEmpty())
    return match.captured(1).startsWith(QLatin1Char('-'))
               ? -std::numeric_limits<double>::infinity()
               : std::numeric_limits<double>::infinity();
  return match.captured(2).toDouble();
}

} // namespace

SankeyParseError::SankeyParseError(SankeyErrorKind errorKind, int errorLine,
                                   int errorColumn, QString errorToken,
                                   const QString &message)
    : std::runtime_error(message.toStdString()), kind(errorKind),
      line(errorLine), column(errorColumn), token(std::move(errorToken)) {}

SankeyData SankeyDiagram::parse(const QString &source) {
  QString text = source;
  static const QRegularExpression horizontalEdges(
      QStringLiteral(R"(^[^\S\n\r]+|[^\S\n\r]+$)"));
  text.replace(horizontalEdges, QString());
  text.replace(QRegularExpression(QStringLiteral(R"([\n\r]+)")),
               QStringLiteral("\n"));
  text = text.trimmed();

  static const QRegularExpression header(
      QStringLiteral(R"(^(sankey(?:-beta)?)(?=\b))"),
      QRegularExpression::CaseInsensitiveOption);
  const auto headerMatch = header.match(text);
  if (!headerMatch.hasMatch())
    parserError(1, 1, text.left(16), QStringLiteral("Expected Sankey header"));
  int cursor = headerMatch.capturedEnd();
  if (cursor >= text.size() || text.at(cursor) != QLatin1Char('\n'))
    parserError(1, cursor + 1,
                cursor >= text.size() ? QStringLiteral("EOF")
                                      : text.mid(cursor, 1),
                QStringLiteral("Expected newline after Sankey header"));
  ++cursor;

  SankeyData result;
  QHash<QString, qsizetype> byId;
  HtmlSanitizer sanitizer;
  int line = 2;
  while (cursor < text.size()) {
    CsvField sourceField = parseField(text, cursor, line);
    line += sourceField.value.count(QLatin1Char('\n'));
    cursor = sourceField.end;
    if (cursor >= text.size() || text.at(cursor) != QLatin1Char(','))
      parserError(line, cursor + 1,
                  cursor >= text.size() ? QStringLiteral("EOF")
                                        : text.mid(cursor, 1),
                  QStringLiteral("Expected comma after Sankey source"));
    CsvField targetField = parseField(text, cursor + 1, line);
    line += targetField.value.count(QLatin1Char('\n'));
    cursor = targetField.end;
    if (cursor >= text.size() || text.at(cursor) != QLatin1Char(','))
      parserError(line, cursor + 1,
                  cursor >= text.size() ? QStringLiteral("EOF")
                                        : text.mid(cursor, 1),
                  QStringLiteral("Expected comma after Sankey target"));
    CsvField valueField = parseField(text, cursor + 1, line);
    line += valueField.value.count(QLatin1Char('\n'));
    cursor = valueField.end;
    if (cursor < text.size() && text.at(cursor) != QLatin1Char('\n'))
      parserError(line, cursor + 1, text.mid(cursor, 1),
                  QStringLiteral("Expected newline after Sankey record"));

    auto clean = [&](QString value) {
      value = value.trimmed();
      value.replace(QStringLiteral("\"\""), QStringLiteral("\""));
      return sanitizer.sanitizedMermaidText(value);
    };
    const QString sourceId = clean(sourceField.value);
    const QString targetId = clean(targetField.value);
    auto addNode = [&](const QString &id) {
      if (!byId.contains(id)) {
        byId.insert(id, result.nodes.size());
        result.nodes.append({id});
      }
    };
    addNode(sourceId);
    addNode(targetId);
    result.links.append(
        {sourceId, targetId, jsParseFloat(valueField.value.trimmed())});
    if (cursor < text.size()) {
      ++cursor;
      ++line;
    }
  }
  if (result.links.isEmpty())
    parserError(1, text.size() + 1, QStringLiteral("EOF"),
                QStringLiteral("Expected Sankey CSV record"));
  return result;
}

} // namespace muffin::mermaid::sankey
