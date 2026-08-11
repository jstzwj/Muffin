#include "mermaid/ishikawa/IshikawaDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace muffin::mermaid::ishikawa {
namespace {

struct StackEntry {
  int level = 0;
  IshikawaNode* node = nullptr;
};

QString jisonErrorMessage(int line, const QString& preview, int caret,
                          const QString& expected,
                          const QString& actual) {
  return QStringLiteral("Parse error on line %1: %2 %3^ Expecting %4, got '%5'")
      .arg(line)
      .arg(preview, QString(std::max(0, caret), QLatin1Char('-')), expected,
           actual);
}

bool isHeaderAt(const QString& value, qsizetype offset,
                qsizetype* consumed = nullptr) {
  const QString tail = value.mid(offset);
  const auto asciiWord = [](QChar ch) {
    const ushort u = ch.unicode();
    return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
           (u >= '0' && u <= '9') || u == '_';
  };
  const auto matchKeyword = [&](QLatin1String keyword) {
    if (!tail.startsWith(keyword, Qt::CaseInsensitive)) return false;
    const qsizetype length = keyword.size();
    if (tail.size() > length && asciiWord(tail.at(length))) return false;
    if (consumed) *consumed = length;
    return true;
  };
  return matchKeyword(QLatin1String("ishikawa-beta")) ||
         matchKeyword(QLatin1String("ishikawa"));
}

bool isCommentLine(const QString& line) {
  return line.trimmed().startsWith(QStringLiteral("%%"));
}

int indentationLength(const QString& line) {
  int result = 0;
  while (result < line.size()) {
    const QChar ch = line.at(result);
    if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t') &&
        ch != QLatin1Char('\r') && ch != QLatin1Char('\f') &&
        ch != QChar(0x000b))
      break;
    ++result;
  }
  return result;
}

void addNode(IshikawaData& data, QVector<StackEntry>& stack,
             std::optional<int>& baseLevel, int rawLevel, QString text) {
  const HtmlSanitizer sanitizer;
  const QString label = sanitizer.sanitizedMermaidText(text.trimmed());
  if (!data.hasRoot) {
    data.hasRoot = true;
    data.root = IshikawaNode{label, {}};
    data.title = label;
    stack = {{0, &data.root}};
    return;
  }

  if (!baseLevel) baseLevel = rawLevel;
  const int level = std::max(1, rawLevel - *baseLevel + 1);
  while (stack.size() > 1 && stack.back().level >= level) stack.removeLast();
  IshikawaNode* parent = stack.back().node;
  parent->children.append(IshikawaNode{label, {}});
  stack.append({level, &parent->children.back()});
}

}  // namespace

IshikawaParseError::IshikawaParseError(IshikawaErrorKind kind, int line,
                                       int column, QString token,
                                       const QString& message)
    : std::runtime_error(message.toStdString()),
      kind(kind),
      line(line),
      column(column),
      token(std::move(token)) {}

IshikawaData IshikawaDiagram::parse(const QString& source) {
  IshikawaData data;
  QVector<StackEntry> stack;
  std::optional<int> baseLevel;

  const QStringList lines = source.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  int headerLine = -1;
  qsizetype headerOffset = 0;
  qsizetype headerLength = 0;
  QString headerSuffix;

  for (int i = 0; i < lines.size(); ++i) {
    const QString& line = lines.at(i);
    if (line.trimmed().isEmpty() || isCommentLine(line)) continue;
    headerOffset = indentationLength(line);
    if (!isHeaderAt(line, headerOffset, &headerLength))
      throw IshikawaParseError(
          IshikawaErrorKind::Parser, i + 1, int(headerOffset + 1),
          QStringLiteral("ISHIKAWA"),
          jisonErrorMessage(i + 1, line.trimmed(), int(headerOffset),
                            QStringLiteral("'ISHIKAWA'"),
                            QStringLiteral("TEXT")));
    headerLine = i;
    headerSuffix = line.mid(headerOffset + headerLength);
    break;
  }

  if (headerLine < 0)
    throw IshikawaParseError(IshikawaErrorKind::Parser, 1, 1,
                             QStringLiteral("EOF"),
                             jisonErrorMessage(1, QString(), 0,
                                               QStringLiteral("'ISHIKAWA'"),
                                               QStringLiteral("EOF")));

  const QString suffixText = headerSuffix.trimmed();
  if (!suffixText.isEmpty())
    addNode(data, stack, baseLevel, indentationLength(headerSuffix), suffixText);

  const bool hasPhysicalTerminator =
      headerLine + 1 < lines.size() || source.endsWith(QLatin1Char('\n'));
  if (!hasPhysicalTerminator && suffixText.isEmpty())
    throw IshikawaParseError(
        IshikawaErrorKind::Parser, headerLine + 2,
        int(headerOffset + headerLength + 1), QStringLiteral("EOF"),
        jisonErrorMessage(
            headerLine + 2, lines.at(headerLine).trimmed(),
            int(headerOffset + headerLength),
            QStringLiteral("'SPACELINE', 'SPACELIST', 'TEXT'"),
            QStringLiteral("EOF")));

  bool beforeFirstNode = !data.hasRoot;
  for (int i = headerLine + 1; i < lines.size(); ++i) {
    const QString& line = lines.at(i);
    if (isCommentLine(line)) continue;
    if (line.trimmed().isEmpty()) {
      // Jison's SPACELINE production is accepted as an empty statement except
      // for an indented blank immediately after the header, where the
      // following TEXT token has no valid reduction.
      if (beforeFirstNode && !line.isEmpty() && i + 1 < lines.size()) {
        int next = i + 1;
        while (next < lines.size() &&
               (lines.at(next).trimmed().isEmpty() ||
                isCommentLine(lines.at(next))))
          ++next;
        if (next < lines.size())
          throw IshikawaParseError(
              IshikawaErrorKind::Parser, next + 1,
              9, QStringLiteral("TEXT"),
              jisonErrorMessage(
                  next + 1,
                  lines.at(headerLine).trimmed() + QLatin1Char(' ') +
                      lines.at(next).trimmed(),
                  int(headerLength + line.size()),
                  QStringLiteral("'SPACELINE', 'NL', 'EOF'"),
                  QStringLiteral("TEXT")));
      }
      continue;
    }

    const int indent = indentationLength(line);
    if (isHeaderAt(line, indent)) {
      QString preview;
      int caret = 0;
      for (int matchedLine = headerLine; matchedLine < i; ++matchedLine) {
        if (isCommentLine(lines.at(matchedLine)) ||
            lines.at(matchedLine).trimmed().isEmpty())
          continue;
        const QString tokenText = lines.at(matchedLine).trimmed();
        preview += tokenText;
        caret += tokenText.size();
      }
      preview += line.trimmed();
      if (i + 1 < lines.size() && !lines.at(i + 1).trimmed().isEmpty())
        preview += QLatin1Char(' ') + lines.at(i + 1).trimmed();
      throw IshikawaParseError(
          IshikawaErrorKind::Parser, i + 1,
          indent + 7, QStringLiteral("ISHIKAWA"),
          jisonErrorMessage(
              i + 1, preview, caret,
              QStringLiteral("'SPACELINE', 'NL', 'EOF', 'SPACELIST', 'TEXT'"),
              QStringLiteral("ISHIKAWA")));
    }

    addNode(data, stack, baseLevel, indent, line.mid(indent));
    beforeFirstNode = false;
  }

  return data;
}

}  // namespace muffin::mermaid::ishikawa
