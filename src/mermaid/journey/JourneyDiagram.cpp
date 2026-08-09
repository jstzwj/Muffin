#include "mermaid/journey/JourneyDiagram.h"

#include "mermaid/editor/MermaidRenderSupport.h"

#include <QJsonValue>
#include <QRegularExpression>

#include <utility>

namespace muffin::mermaid::journey {

JourneyParseError::JourneyParseError(const QString& message, int line)
    : std::runtime_error(message.toUtf8().constData()), line(line) {}

namespace {

QStringList splitLines(const QString& source) {
  QStringList lines;
  QString current;
  for (int i = 0; i < source.size(); ++i) {
    const QChar c = source.at(i);
    if (c == QLatin1Char('\n')) {
      lines.append(current);
      current.clear();
    } else if (c == QLatin1Char('\r')) {
      lines.append(current);
      current.clear();
      if (i + 1 < source.size() && source.at(i + 1) == QLatin1Char('\n')) ++i;
    } else {
      current.append(c);
    }
  }
  lines.append(current);
  return lines;
}

bool startsWithKeyword(const QString& text, QLatin1String keyword) {
  if (!text.startsWith(keyword, Qt::CaseInsensitive)) return false;
  return text.size() == keyword.size() || text.at(keyword.size()).isSpace() ||
         text.at(keyword.size()) == QLatin1Char(':') ||
         text.at(keyword.size()) == QLatin1Char('{');
}

bool isWholeLineComment(const QString& raw) {
  // These are the two unusual Jison comment rules. A leading single percent is
  // a comment, and any INITIAL-state line containing %% after its first
  // character is skipped in full rather than truncated at the marker.
  const QString left = raw.trimmed();
  if (left.startsWith(QLatin1Char('%')) || left.startsWith(QLatin1Char('#')))
    return true;
  return left.size() >= 3 && left.at(0) != QLatin1Char('}') &&
         left.mid(1, 2) == QStringLiteral("%%");
}

QString trimInitialWhitespace(const QString& raw) {
  qsizetype start = 0;
  while (start < raw.size() &&
         (raw.at(start).isSpace() || raw.at(start) == QChar(0xfeff)))
    ++start;
  return raw.mid(start);
}

QString withoutHashComment(const QString& line) {
  const int hash = line.indexOf(QLatin1Char('#'));
  return hash < 0 ? line : line.left(hash);
}

double jsNumber(const QString& value) {
  return editor::jsNumberValue(QJsonValue(value));
}

void addTask(JourneyData& data, const QString& currentSection,
             const QString& description, const QString& taskData) {
  const QStringList pieces = taskData.mid(1).split(QLatin1Char(':'), Qt::KeepEmptyParts);
  JourneyTask task;
  task.section = currentSection;
  task.type = currentSection;
  task.task = description;
  task.score = jsNumber(pieces.value(0));
  if (pieces.size() > 1) {
    const QStringList actors = pieces.at(1).split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (const QString& actor : actors) task.people.append(actor.trimmed());
  }
  data.tasks.append(std::move(task));
}

}  // namespace

JourneyData JourneyDiagram::parse(const QString& source) {
  JourneyData data;
  const QStringList lines = splitLines(source);
  QString currentSection;
  bool headerSeen = false;

  for (int index = 0; index < lines.size(); ++index) {
    const QString raw = lines.at(index);
    const int lineNo = index + 1;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty() || isWholeLineComment(raw)) continue;

    if (!headerSeen) {
      const QString header = trimInitialWhitespace(raw).trimmed();
      if (header.compare(QLatin1String("journey"), Qt::CaseInsensitive) != 0)
        throw JourneyParseError(QStringLiteral("missing journey header"), lineNo);
      headerSeen = true;
      continue;
    }

    // INITIAL-state `\s+` consumes indentation, but the title/section/task
    // tokens themselves retain trailing whitespace up to the newline.
    const QString left = trimInitialWhitespace(raw);
    if (startsWithKeyword(left, QLatin1String("accTitle"))) {
      const QRegularExpression re(QStringLiteral(R"(^accTitle\s*:\s*(.*)$)"),
                                  QRegularExpression::CaseInsensitiveOption);
      const auto match = re.match(left);
      if (!match.hasMatch())
        throw JourneyParseError(QStringLiteral("invalid accTitle statement"), lineNo);
      data.accTitle = match.captured(1).trimmed();
      continue;
    }
    if (startsWithKeyword(left, QLatin1String("accDescr"))) {
      const QRegularExpression single(QStringLiteral(R"(^accDescr\s*:\s*(.*)$)"),
                                      QRegularExpression::CaseInsensitiveOption);
      const auto singleMatch = single.match(left);
      if (singleMatch.hasMatch()) {
        data.accDescr = singleMatch.captured(1).trimmed();
        continue;
      }
      const QRegularExpression multi(QStringLiteral(R"(^accDescr\s*\{\s*(.*)$)"),
                                     QRegularExpression::CaseInsensitiveOption);
      const auto multiMatch = multi.match(left);
      if (!multiMatch.hasMatch())
        throw JourneyParseError(QStringLiteral("invalid accDescr statement"), lineNo);
      QString body = multiMatch.captured(1);
      int close = body.indexOf(QLatin1Char('}'));
      while (close < 0 && ++index < lines.size()) {
        body += QLatin1Char('\n') + lines.at(index);
        close = body.indexOf(QLatin1Char('}'));
      }
      if (close < 0)
        throw JourneyParseError(QStringLiteral("unterminated accDescr body"), lineNo);
      const QString trailing = body.mid(close + 1).trimmed();
      if (!trailing.isEmpty() && !trailing.startsWith(QLatin1Char('#')) &&
          !trailing.startsWith(QLatin1Char('%')))
        throw JourneyParseError(QStringLiteral("unexpected text after accDescr body"), lineNo);
      data.accDescr = body.left(close).trimmed();
      data.accDescr.replace(QRegularExpression(QStringLiteral(R"(\n\s+)")),
                            QStringLiteral("\n"));
      continue;
    }

    const QString statement = withoutHashComment(left);
    if (statement.trimmed().isEmpty()) continue;
    // The hash-comment token consumes the rest of the line before the lexer
    // can see a semicolon. Semicolons in the remaining INITIAL-state input are
    // lexical errors; accessibility states use separate text rules above.
    if (statement.contains(QLatin1Char(';')))
      throw JourneyParseError(
          QStringLiteral("Lexical error: semicolons are not valid in journey diagrams"),
          lineNo);
    if (startsWithKeyword(statement, QLatin1String("title"))) {
      static const QRegularExpression titleRe(
          QStringLiteral(R"(^title\s([^#;\r\n]+)$)"),
          QRegularExpression::CaseInsensitiveOption);
      const auto match = titleRe.match(statement);
      if (!match.hasMatch())
        throw JourneyParseError(QStringLiteral("invalid journey title"), lineNo);
      // Jison action is yytext.substr(6), deliberately without trimming.
      data.title = statement.mid(6);
      continue;
    }
    if (startsWithKeyword(statement, QLatin1String("section"))) {
      static const QRegularExpression sectionRe(
          QStringLiteral(R"(^section\s[^#:;\r\n]+$)"),
          QRegularExpression::CaseInsensitiveOption);
      if (!sectionRe.match(statement).hasMatch())
        throw JourneyParseError(QStringLiteral("invalid journey section"), lineNo);
      currentSection = statement.mid(8);
      data.sections.append(currentSection);
      continue;
    }

    const int colon = statement.indexOf(QLatin1Char(':'));
    if (colon <= 0 || colon + 1 >= statement.size())
      throw JourneyParseError(QStringLiteral("invalid journey task"), lineNo);
    const QString taskName = statement.left(colon);
    const QString taskData = statement.mid(colon);
    if (taskName.contains(QLatin1Char('#')) || taskName.contains(QLatin1Char(';')) ||
        taskData.contains(QLatin1Char('#')) || taskData.contains(QLatin1Char(';')))
      throw JourneyParseError(QStringLiteral("invalid journey task token"), lineNo);
    addTask(data, currentSection, taskName, taskData);
  }

  if (!headerSeen) throw JourneyParseError(QStringLiteral("missing journey header"));
  return data;
}

}  // namespace muffin::mermaid::journey
