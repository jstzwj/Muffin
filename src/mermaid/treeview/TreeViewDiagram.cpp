#include "mermaid/treeview/TreeViewDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace muffin::mermaid::treeview {
namespace {

constexpr QChar kThinHorizontal(0x2500);
constexpr QChar kHeavyHorizontal(0x2501);
constexpr QChar kThinVertical(0x2502);
constexpr QChar kHeavyVertical(0x2503);
constexpr QChar kThinEnd(0x2514);
constexpr QChar kHeavyEnd(0x2517);
constexpr QChar kThinBranch(0x251c);
constexpr QChar kHeavyBranch(0x2523);

bool isBoxChar(QChar ch) {
  return ch == kThinHorizontal || ch == kHeavyHorizontal ||
         ch == kThinVertical || ch == kHeavyVertical || ch == kThinEnd ||
         ch == kHeavyEnd || ch == kThinBranch || ch == kHeavyBranch;
}

bool isBranchChar(QChar ch) {
  return ch == kThinEnd || ch == kHeavyEnd || ch == kThinBranch ||
         ch == kHeavyBranch;
}

bool isDashChar(QChar ch) {
  return ch == kThinHorizontal || ch == kHeavyHorizontal;
}

bool isInlineWhitespace(QChar ch) {
  return ch == QLatin1Char(' ') || ch == QLatin1Char('\t');
}

QString sanitizeMetadata(QString value) {
  return HtmlSanitizer().sanitizedMermaidText(value.trimmed());
}

QString normalizeAccDescr(QString value) {
  value = value.trimmed();
  static const QRegularExpression indentation(QStringLiteral(R"(\r?\n\s+)"));
  value.replace(indentation, QStringLiteral("\n"));
  return sanitizeMetadata(value);
}

struct PreprocessedSource {
  QString text;
  QHash<int, int> lineMap;
};

bool metadataLine(const QString& line) {
  static const QRegularExpression expression(
      QStringLiteral(R"(^\s*(title[\t ]|accTitle[\t ]*:|accDescr[\t ]*[:{]))"));
  return expression.match(line).hasMatch();
}

bool commentLine(const QString& line) {
  static const QRegularExpression expression(QStringLiteral(R"(^\s*%%)"));
  return expression.match(line).hasMatch();
}

bool decorationOnly(const QString& line) {
  for (const QChar ch : line) {
    if (!ch.isSpace() && ch != kThinVertical && ch != kHeavyVertical)
      return false;
  }
  return true;
}

int firstBranchIndex(const QString& line) {
  for (int i = 0; i < line.size(); ++i)
    if (isBranchChar(line.at(i))) return i;
  return -1;
}

bool containsBoxChar(const QString& line) {
  return std::any_of(line.cbegin(), line.cend(), isBoxChar);
}

[[noreturn]] void preprocessError(int line, const QString& message) {
  throw TreeViewParseError(QStringLiteral("Line %1: %2").arg(line).arg(message),
                           line, 0, TreeViewErrorKind::Preprocess);
}

PreprocessedSource preprocessBoxDrawing(const QString& input) {
  QString normalizedInput = input;
  normalizedInput.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  normalizedInput.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  const QStringList lines = normalizedInput.split(QLatin1Char('\n'));
  int keywordIndex = -1;
  for (int i = 0; i < lines.size(); ++i) {
    if (lines.at(i).trimmed() == QLatin1String("treeView-beta")) {
      keywordIndex = i;
      break;
    }
  }
  if (keywordIndex < 0) return {input, {}};

  QStringList contentLines;
  for (int i = keywordIndex + 1; i < lines.size(); ++i) {
    const QString& line = lines.at(i);
    if (line.trimmed().isEmpty() || commentLine(line) || metadataLine(line) ||
        decorationOnly(line))
      continue;
    QString normalized = line;
    normalized.replace(QLatin1Char('\t'), QStringLiteral("    "));
    contentLines.append(normalized);
  }
  if (std::none_of(contentLines.cbegin(), contentLines.cend(), containsBoxChar))
    return {input, {}};

  int segmentWidth = 4;
  for (const QString& line : contentLines) {
    const int index = firstBranchIndex(line);
    if (index > 0) {
      segmentWidth = index;
      break;
    }
  }

  QStringList output;
  QHash<int, int> lineMap;
  auto append = [&](const QString& line, int originalLine) {
    output.append(line);
    lineMap.insert(output.size(), originalLine);
  };
  for (int i = 0; i <= keywordIndex; ++i) append(lines.at(i), i + 1);

  for (int i = keywordIndex + 1; i < lines.size(); ++i) {
    const QString& line = lines.at(i);
    const int originalLine = i + 1;
    if (line.trimmed().isEmpty() || commentLine(line) || metadataLine(line)) {
      append(line, originalLine);
      continue;
    }
    if (decorationOnly(line)) continue;

    QString normalized = line;
    normalized.replace(QLatin1Char('\t'), QStringLiteral("    "));
    const int branch = firstBranchIndex(normalized);
    if (branch >= 0) {
      const int depth = qRound(qreal(branch) / qreal(segmentWidth)) + 1;
      int pos = branch + 1;
      while (pos < normalized.size() && isDashChar(normalized.at(pos))) ++pos;
      while (pos < normalized.size() && normalized.at(pos) == QLatin1Char(' '))
        ++pos;
      const QString content = normalized.mid(pos).trimmed();
      if (content.isEmpty()) {
        preprocessError(
            originalLine,
            QStringLiteral("Empty node ") + QChar(0x2014) +
                QStringLiteral(" expected a filename or directory name after "
                               "the box-drawing prefix"));
      }
      append(QString(depth * 4, QLatin1Char(' ')) + content, originalLine);
      continue;
    }

    bool onlyDecorations = true;
    for (const QChar ch : normalized) {
      if (!ch.isSpace() && !isBoxChar(ch)) {
        onlyDecorations = false;
        break;
      }
    }
    if (onlyDecorations) continue;
    if (containsBoxChar(normalized)) {
      append(line, originalLine);
    } else if (!normalized.isEmpty() && normalized.front().isSpace()) {
      preprocessError(
          originalLine,
          QStringLiteral("Unexpected indentation without box-drawing "
                         "characters. In box-drawing format, use ") +
              QChar(0x251c) + QChar(0x2500) + QChar(0x2500) +
              QStringLiteral(" or ") + QChar(0x2514) + QChar(0x2500) +
              QChar(0x2500) +
              QStringLiteral(" prefixes for indented nodes."));
    } else {
      append(line, originalLine);
    }
  }
  return {output.join(QLatin1Char('\n')), std::move(lineMap)};
}

int originalLine(const QHash<int, int>& lineMap, int line) {
  return lineMap.value(line, line);
}

qsizetype lineOffset(const QStringList& lines, int lineIndex, int column) {
  qsizetype result = 0;
  for (int i = 0; i < lineIndex; ++i) result += lines.at(i).size() + 1;
  return result + column;
}

[[noreturn]] void lexerError(const QStringList& lines,
                             const QHash<int, int>& lineMap, int lineIndex,
                             int column, QChar character,
                             qsizetype skipped = 1) {
  const int mappedLine = originalLine(lineMap, lineIndex + 1);
  const qsizetype offset = lineOffset(lines, lineIndex, column);
  throw TreeViewParseError(
      QStringLiteral("Parsing failed: Lexer error on line %1, column %2: "
                     "unexpected character: ->%3<- at offset: %4, skipped %5 "
                     "characters.")
          .arg(mappedLine)
          .arg(column + 1)
          .arg(character)
          .arg(offset)
          .arg(skipped),
      mappedLine, column + 1, TreeViewErrorKind::Lexer);
}

[[noreturn]] void parserError(const QHash<int, int>& lineMap, int lineIndex,
                              int column, const QString& expected,
                              const QString& found) {
  const int mappedLine = originalLine(lineMap, lineIndex + 1);
  throw TreeViewParseError(
      QStringLiteral("Parsing failed: Parse error on line %1, column %2: "
                     "Expecting %3 but found `%4`.")
          .arg(mappedLine)
          .arg(column + 1)
          .arg(expected, found),
      mappedLine, column + 1, TreeViewErrorKind::Parser);
}

bool isClassStart(QChar ch) {
  return (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
         (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
         ch == QLatin1Char('_');
}

bool isWordOrHyphen(QChar ch) {
  return ch.isLetterOrNumber() || ch == QLatin1Char('_') ||
         ch == QLatin1Char('-');
}

int validClassAnnotationAt(const QString& line, int position) {
  int pos = position;
  if (pos >= line.size() || !isInlineWhitespace(line.at(pos))) return -1;
  while (pos < line.size() && isInlineWhitespace(line.at(pos))) ++pos;
  if (line.mid(pos, 3) != QLatin1String(":::")) return -1;
  pos += 3;
  while (pos < line.size() && isInlineWhitespace(line.at(pos))) ++pos;
  return pos < line.size() && isClassStart(line.at(pos)) ? pos : -1;
}

bool annotationBoundaryAt(const QString& line, int position) {
  int pos = position;
  if (pos >= line.size() || !isInlineWhitespace(line.at(pos))) return false;
  while (pos < line.size() && isInlineWhitespace(line.at(pos))) ++pos;
  if (line.mid(pos, 2) == QLatin1String("##")) return true;
  if (line.mid(pos, 5) == QLatin1String("icon(")) return true;
  return validClassAnnotationAt(line, position) >= 0;
}

struct ParsedNode {
  int level = 0;
  QString name;
  bool hasIcon = false;
  QString icon;
  QString cssClass;
  QString description;
};

QVector<ParsedNode> parseNodeLine(const QStringList& lines,
                                  const QHash<int, int>& lineMap,
                                  int lineIndex) {
  const QString& line = lines.at(lineIndex);
  QVector<ParsedNode> result;
  int pos = 0;
  int indentation = 0;
  while (pos < line.size() && isInlineWhitespace(line.at(pos))) {
    ++pos;
    ++indentation;
  }
  bool first = true;

  while (pos < line.size()) {
    ParsedNode node;
    node.level = first ? indentation : 0;
    first = false;

    if (line.at(pos) == QLatin1Char('"') ||
        line.at(pos) == QLatin1Char('\'')) {
      const QChar quote = line.at(pos++);
      const int end = line.indexOf(quote, pos);
      if (end < 0) lexerError(lines, lineMap, lineIndex, pos - 1, quote);
      node.name = line.mid(pos, end - pos);
      pos = end + 1;
    } else {
      if (line.mid(pos, 3) == QLatin1String(":::") ||
          line.mid(pos, 5) == QLatin1String("icon(") ||
          line.mid(pos, 2) == QLatin1String("##"))
        lexerError(lines, lineMap, lineIndex, pos, line.at(pos));
      const int begin = pos;
      while (pos < line.size()) {
        if (annotationBoundaryAt(line, pos)) break;
        ++pos;
      }
      int end = pos;
      while (end > begin && isInlineWhitespace(line.at(end - 1))) --end;
      node.name = line.mid(begin, end - begin);
      if (node.name.isEmpty())
        lexerError(lines, lineMap, lineIndex, begin, line.at(begin));
    }

    bool descriptionConsumed = false;
    while (pos < line.size()) {
      const int whitespaceBegin = pos;
      while (pos < line.size() && isInlineWhitespace(line.at(pos))) ++pos;
      if (pos >= line.size()) break;

      if (line.mid(pos, 3) == QLatin1String(":::")) {
        const int nameStart = validClassAnnotationAt(line, whitespaceBegin);
        if (nameStart < 0)
          lexerError(lines, lineMap, lineIndex, pos, line.at(pos));
        int end = nameStart + 1;
        while (end < line.size() && isWordOrHyphen(line.at(end))) ++end;
        node.cssClass = line.mid(nameStart, end - nameStart);
        pos = end;
        continue;
      }

      if (line.mid(pos, 5) == QLatin1String("icon(")) {
        const int tokenStart = pos;
        pos += 5;
        const int valueStart = pos;
        while (pos < line.size() && isWordOrHyphen(line.at(pos))) ++pos;
        if (pos < line.size() && line.at(pos) == QLatin1Char(':')) {
          ++pos;
          const int suffix = pos;
          while (pos < line.size() && isWordOrHyphen(line.at(pos))) ++pos;
          if (pos == suffix)
            lexerError(lines, lineMap, lineIndex, tokenStart, line.at(tokenStart));
        }
        if (pos >= line.size() || line.at(pos) != QLatin1Char(')'))
          lexerError(lines, lineMap, lineIndex, tokenStart, line.at(tokenStart));
        node.hasIcon = true;
        node.icon = line.mid(valueStart, pos - valueStart);
        if (node.icon.isEmpty()) node.icon = QStringLiteral("none");
        ++pos;
        continue;
      }

      if (line.mid(pos, 2) == QLatin1String("##")) {
        pos += 2;
        node.description =
            HtmlSanitizer().sanitizedMermaidText(line.mid(pos).trimmed());
        pos = line.size();
        descriptionConsumed = true;
        break;
      }

      // A quoted token can be followed by another TreeNode on the same physical
      // line. A bare first token cannot reach this branch because BARE_NAME
      // greedily consumes every non-annotation character through end-of-line.
      break;
    }
    result.append(std::move(node));
    if (descriptionConsumed) break;
  }
  return result;
}

void addNode(TreeViewData& data, QVector<int>& stack, ParsedNode parsed) {
  while (!stack.isEmpty() &&
         parsed.level <= data.nodes.at(stack.back()).level)
    stack.pop_back();
  const int parent = stack.isEmpty() ? data.rootIndex : stack.back();

  TreeViewNode node;
  node.id = data.nodes.size();
  node.level = parsed.level;
  node.name = std::move(parsed.name);
  node.directory = node.name.endsWith(QLatin1Char('/'));
  if (node.directory) node.name.chop(1);
  node.hasIcon = parsed.hasIcon;
  node.icon = std::move(parsed.icon);
  node.cssClass = std::move(parsed.cssClass);
  node.description = std::move(parsed.description);
  data.nodes.append(std::move(node));
  data.nodes[parent].children.append(data.nodes.back().id);
  stack.append(data.nodes.back().id);
}

}  // namespace

TreeViewParseError::TreeViewParseError(const QString& message, int errorLine,
                                       int errorColumn,
                                       TreeViewErrorKind errorKind)
    : std::runtime_error(message.toUtf8().constData()),
      line(errorLine),
      column(errorColumn),
      kind(errorKind) {}

TreeViewData TreeViewDiagram::parse(const QString& source) {
  const PreprocessedSource preprocessed = preprocessBoxDrawing(source);
  QString normalized = preprocessed.text;
  normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  const QStringList lines = normalized.split(QLatin1Char('\n'));

  TreeViewData data;
  TreeViewNode root;
  root.id = 0;
  root.level = -1;
  root.name = QStringLiteral("/");
  root.directory = true;
  data.nodes.append(std::move(root));

  int lineIndex = 0;
  while (lineIndex < lines.size() &&
         (lines.at(lineIndex).trimmed().isEmpty() ||
          commentLine(lines.at(lineIndex))))
    ++lineIndex;
  if (lineIndex >= lines.size())
    parserError(preprocessed.lineMap, qMax(0, lines.size() - 1), 0,
                QStringLiteral("token of type 'treeView-beta'"), QString());
  if (lines.at(lineIndex).trimmed() != QLatin1String("treeView-beta")) {
    parserError(preprocessed.lineMap, lineIndex, 0,
                QStringLiteral("token of type 'treeView-beta'"),
                lines.at(lineIndex).trimmed());
  }
  ++lineIndex;

  bool metadataOpen = true;
  QVector<int> stack{data.rootIndex};
  while (lineIndex < lines.size()) {
    const QString& line = lines.at(lineIndex);
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || commentLine(line)) {
      ++lineIndex;
      continue;
    }

    if (metadataOpen &&
        (trimmed == QLatin1String("title") ||
         trimmed.startsWith(QLatin1String("title ")) ||
         trimmed.startsWith(QLatin1String("title\t")))) {
      data.title = sanitizeMetadata(trimmed.mid(5));
      ++lineIndex;
      continue;
    }
    static const QRegularExpression accTitle(
        QStringLiteral(R"(^[\t ]*accTitle[\t ]*:(.*)$)"));
    const QRegularExpressionMatch accTitleMatch = accTitle.match(line);
    if (metadataOpen && accTitleMatch.hasMatch()) {
      data.accTitle = sanitizeMetadata(accTitleMatch.captured(1));
      ++lineIndex;
      continue;
    }
    static const QRegularExpression accDescrLine(
        QStringLiteral(R"(^[\t ]*accDescr[\t ]*:(.*)$)"));
    const QRegularExpressionMatch accDescrMatch = accDescrLine.match(line);
    if (metadataOpen && accDescrMatch.hasMatch()) {
      data.accDescr = sanitizeMetadata(accDescrMatch.captured(1));
      ++lineIndex;
      continue;
    }
    static const QRegularExpression accDescrBlock(
        QStringLiteral(R"(^[\t ]*accDescr\s*\{(.*)$)"));
    const QRegularExpressionMatch blockMatch = accDescrBlock.match(line);
    if (metadataOpen && blockMatch.hasMatch()) {
      QString body = blockMatch.captured(1);
      int close = body.indexOf(QLatin1Char('}'));
      while (close < 0 && ++lineIndex < lines.size()) {
        body += QLatin1Char('\n') + lines.at(lineIndex);
        close = body.indexOf(QLatin1Char('}'));
      }
      if (close < 0)
        lexerError(lines, preprocessed.lineMap, lineIndex - 1,
                   lines.at(lineIndex - 1).size(), QChar());
      data.accDescr = normalizeAccDescr(body.left(close));
      ++lineIndex;
      continue;
    }

    if (!metadataOpen && metadataLine(line)) {
      parserError(preprocessed.lineMap, lineIndex, 0,
                  QStringLiteral("token of type 'TreeNode'"), trimmed);
    }
    metadataOpen = false;
    const QVector<ParsedNode> parsed =
        parseNodeLine(lines, preprocessed.lineMap, lineIndex);
    for (ParsedNode node : parsed) addNode(data, stack, std::move(node));
    ++lineIndex;
  }
  return data;
}

}  // namespace muffin::mermaid::treeview
